/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>

#include "gnome-screensaver.h"

#include "gs-manager.h"
#include "gs-watcher.h"
#include "gs-fade.h"
#include "gs-grab.h"

#include "gs-listener-dbus.h"
#include "gs-monitor.h"
#include "gs-prefs.h"
#include "gs-debug.h"

static void     gs_monitor_class_init (GSMonitorClass *klass);
static void     gs_monitor_init       (GSMonitor      *monitor);
static void     gs_monitor_finalize   (GObject        *object);

#define GS_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_MONITOR, GSMonitorPrivate))

struct GSMonitorPrivate
{
        GSWatcher      *watcher;
        GSListener     *listener;
        GSManager      *manager;
        GSPrefs        *prefs;
        GSFade         *fade;
        GSGrab         *grab;
        guint           release_grab_id;
};

#define FADE_TIMEOUT 10000

G_DEFINE_TYPE (GSMonitor, gs_monitor, G_TYPE_OBJECT)

static void
gs_monitor_class_init (GSMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gs_monitor_finalize;

        g_type_class_add_private (klass, sizeof (GSMonitorPrivate));
}

static void
manager_activated_cb (GSManager *manager,
                      GSMonitor *monitor)
{
}

static void
manager_deactivated_cb (GSManager *manager,
                        GSMonitor *monitor)
{
        gs_listener_set_active (monitor->priv->listener, FALSE);
}

static gboolean
watcher_idle_cb (GSWatcher *watcher,
                 gboolean   is_idle,
                 GSMonitor *monitor)
{
        gboolean res;

        gs_debug ("Idle signal detected: %d", is_idle);

        res = gs_listener_set_session_idle (monitor->priv->listener, is_idle);

        return res;
}

static gboolean
release_grab_timeout (GSMonitor *monitor)
{
        gboolean manager_active;

        manager_active = gs_manager_get_active (monitor->priv->manager);
        if (! manager_active) {
                gs_grab_release (monitor->priv->grab);
        }

        monitor->priv->release_grab_id = 0;
        return FALSE;
}

static gboolean
watcher_idle_notice_cb (GSWatcher *watcher,
                        gboolean   in_effect,
                        GSMonitor *monitor)
{
        gboolean activation_enabled;
        gboolean inhibited;
        gboolean handled;

        gs_debug ("Idle notice signal detected: %d", in_effect);

        /* only fade if screensaver can activate */
        activation_enabled = gs_listener_get_activation_enabled (monitor->priv->listener);
        inhibited = gs_listener_is_inhibited (monitor->priv->listener);

        handled = FALSE;
        if (in_effect) {
                if (activation_enabled && ! inhibited) {
                        /* start slow fade */
                        if (gs_grab_grab_offscreen (monitor->priv->grab, FALSE)) {
                                gs_fade_async (monitor->priv->fade, FADE_TIMEOUT, NULL, NULL);
                        } else {
                                gs_debug ("Could not grab the keyboard so not performing idle warning fade-out");
                        }

                        handled = TRUE;
                }
        } else {
                gboolean manager_active;

                manager_active = gs_manager_get_active (monitor->priv->manager);
                /* cancel the fade unless manager was activated */
                if (! manager_active) {
                        gs_debug ("manager not active, performing fade cancellation");
                        gs_fade_reset (monitor->priv->fade);

                        /* don't release the grab immediately to prevent typing passwords into windows */
                        if (monitor->priv->release_grab_id != 0) {
                                g_source_remove (monitor->priv->release_grab_id);
                        }
                        monitor->priv->release_grab_id = g_timeout_add (1000, (GSourceFunc)release_grab_timeout, monitor);
                } else {
                        gs_debug ("manager active, skipping fade cancellation");
                }

                handled = TRUE;
        }

        return handled;
}

static void
gs_monitor_lock_screen (GSMonitor *monitor)
{
        gboolean res;
        gboolean locked;

        /* set lock flag before trying to activate screensaver
           in case something tries to react to the ActiveChanged signal */

        gs_manager_get_lock_active (monitor->priv->manager, &locked);
        gs_manager_set_lock_active (monitor->priv->manager, TRUE);
        res = gs_listener_set_active (monitor->priv->listener, TRUE);
        if (! res) {
                /* If we've failed then restore lock status */
                gs_manager_set_lock_active (monitor->priv->manager, locked);
                gs_debug ("Unable to lock the screen");
        }
}

static void
gs_monitor_simulate_user_activity (GSMonitor *monitor)
{
        /* FIXME: reset the xsync timer? */

        /* request that the manager unlock -
           will pop up a dialog if necessary */
        gs_manager_request_unlock (monitor->priv->manager);
}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        if (! monitor->priv->prefs->lock_disabled) {
                gs_monitor_lock_screen (monitor);
        } else {
                gs_debug ("Locking disabled by the administrator");
        }

}

static void
listener_quit_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_listener_set_active (monitor->priv->listener, FALSE);
        gnome_screensaver_quit ();
}

static void
listener_cycle_cb (GSListener *listener,
                   GSMonitor  *monitor)
{
        gs_manager_cycle (monitor->priv->manager);
}

static void
listener_show_message_cb (GSListener *listener,
                          const char *summary,
                          const char *body,
                          const char *icon,
                          GSMonitor  *monitor)
{
        gs_manager_show_message (monitor->priv->manager,
                                 summary,
                                 body,
                                 icon);
}

static gboolean
listener_active_changed_cb (GSListener *listener,
                            gboolean    active,
                            GSMonitor  *monitor)
{
        gboolean res;
        gboolean ret;
        gboolean idle_watch_enabled;

        res = gs_manager_set_active (monitor->priv->manager, active);
        if (! res) {
                gs_debug ("Unable to set manager active: %d", active);
                ret = FALSE;
                goto done;
        }

        ret = TRUE;

 done:

        idle_watch_enabled = gs_watcher_get_enabled (monitor->priv->watcher);
        if (ret && idle_watch_enabled) {
                res = gs_watcher_set_active (monitor->priv->watcher, !active);
                if (! res) {
                        gs_debug ("Unable to set the idle watcher active: %d", !active);
                }
        }

        return ret;
}

static void
listener_throttle_changed_cb (GSListener *listener,
                              gboolean    throttled,
                              GSMonitor  *monitor)
{
        gs_manager_set_throttled (monitor->priv->manager, throttled);
}

static void
listener_simulate_user_activity_cb (GSListener *listener,
                                    GSMonitor  *monitor)
{
        gs_monitor_simulate_user_activity (monitor);
}

static void
_gs_monitor_update_from_prefs (GSMonitor *monitor,
                               GSPrefs   *prefs)
{
        gboolean idle_detection_enabled;
        gboolean idle_detection_active;
        gboolean activate_watch;
        gboolean manager_active;
        gboolean lock_enabled;
        gboolean user_switch_enabled;

        lock_enabled = (monitor->priv->prefs->lock_enabled && !monitor->priv->prefs->lock_disabled);
        user_switch_enabled = (monitor->priv->prefs->user_switch_enabled && !monitor->priv->prefs->user_switch_disabled);

        gs_manager_set_lock_enabled (monitor->priv->manager, lock_enabled);
        gs_manager_set_lock_timeout (monitor->priv->manager, monitor->priv->prefs->lock_timeout);
        gs_manager_set_logout_enabled (monitor->priv->manager, monitor->priv->prefs->logout_enabled);
        gs_manager_set_user_switch_enabled (monitor->priv->manager, user_switch_enabled);
        gs_manager_set_keyboard_enabled (monitor->priv->manager, monitor->priv->prefs->keyboard_enabled);
        gs_manager_set_logout_timeout (monitor->priv->manager, monitor->priv->prefs->logout_timeout);
        gs_manager_set_logout_command (monitor->priv->manager, monitor->priv->prefs->logout_command);
        gs_manager_set_keyboard_command (monitor->priv->manager, monitor->priv->prefs->keyboard_command);
        gs_manager_set_cycle_timeout (monitor->priv->manager, monitor->priv->prefs->cycle);
        gs_manager_set_mode (monitor->priv->manager, monitor->priv->prefs->mode);
        gs_manager_set_themes (monitor->priv->manager, monitor->priv->prefs->themes);

        /* enable activation when allowed */
        gs_listener_set_activation_enabled (monitor->priv->listener,
                                            monitor->priv->prefs->idle_activation_enabled);

        /* idle detection always enabled */
        idle_detection_enabled = TRUE;

        gs_watcher_set_enabled (monitor->priv->watcher, idle_detection_enabled);

        /* in the case where idle detection is reenabled we may need to
           activate the watcher too */

        manager_active = gs_manager_get_active (monitor->priv->manager);
        idle_detection_active = gs_watcher_get_active (monitor->priv->watcher);
        activate_watch = (! manager_active
                          && ! idle_detection_active
                          && idle_detection_enabled);
        if (activate_watch) {
                gs_watcher_set_active (monitor->priv->watcher, TRUE);
        }

        if (monitor->priv->prefs->status_message_enabled) {
                char *text;
                g_object_get (monitor->priv->watcher,
                              "status-message", &text,
                              NULL);
                gs_manager_set_status_message (monitor->priv->manager, text);
                g_free (text);
        } else {
                gs_manager_set_status_message (monitor->priv->manager, NULL);
        }
}

static void
disconnect_listener_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_lock_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_quit_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_cycle_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_active_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_throttle_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_simulate_user_activity_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_show_message_cb, monitor);
}

static void
connect_listener_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->listener, "lock",
                          G_CALLBACK (listener_lock_cb), monitor);
        g_signal_connect (monitor->priv->listener, "quit",
                          G_CALLBACK (listener_quit_cb), monitor);
        g_signal_connect (monitor->priv->listener, "cycle",
                          G_CALLBACK (listener_cycle_cb), monitor);
        g_signal_connect (monitor->priv->listener, "active-changed",
                          G_CALLBACK (listener_active_changed_cb), monitor);
        g_signal_connect (monitor->priv->listener, "throttle-changed",
                          G_CALLBACK (listener_throttle_changed_cb), monitor);
        g_signal_connect (monitor->priv->listener, "simulate-user-activity",
                          G_CALLBACK (listener_simulate_user_activity_cb), monitor);
        g_signal_connect (monitor->priv->listener, "show-message",
                          G_CALLBACK (listener_show_message_cb), monitor);
}

static void
on_watcher_status_message_changed (GSWatcher  *watcher,
                                   GParamSpec *pspec,
                                   GSMonitor  *monitor)
{
        char *text;
        g_object_get (watcher, "status-message", &text, NULL);
        gs_manager_set_status_message (monitor->priv->manager, text);
        g_free (text);
}

static void
disconnect_watcher_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->watcher, watcher_idle_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->watcher, watcher_idle_notice_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->watcher, on_watcher_status_message_changed, monitor);
}

static void
connect_watcher_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->watcher, "idle-changed",
                          G_CALLBACK (watcher_idle_cb), monitor);
        g_signal_connect (monitor->priv->watcher, "idle-notice-changed",
                          G_CALLBACK (watcher_idle_notice_cb), monitor);
        g_signal_connect (monitor->priv->watcher, "notify::status-message",
                          G_CALLBACK (on_watcher_status_message_changed), monitor);

}

static void
disconnect_manager_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_activated_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_deactivated_cb, monitor);
}

static void
connect_manager_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->manager, "activated",
                          G_CALLBACK (manager_activated_cb), monitor);
        g_signal_connect (monitor->priv->manager, "deactivated",
                          G_CALLBACK (manager_deactivated_cb), monitor);
}

static void
disconnect_prefs_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->prefs, _gs_monitor_update_from_prefs, monitor);
}

static void
connect_prefs_signals (GSMonitor *monitor)
{
        g_signal_connect_swapped (monitor->priv->prefs, "changed",
                                  G_CALLBACK (_gs_monitor_update_from_prefs), monitor);
}

static void
gs_monitor_init (GSMonitor *monitor)
{

        monitor->priv = GS_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->prefs = gs_prefs_new ();
        connect_prefs_signals (monitor);

        monitor->priv->listener = gs_listener_new ();
        connect_listener_signals (monitor);

        monitor->priv->fade = gs_fade_new ();
        monitor->priv->grab = gs_grab_new ();

        monitor->priv->watcher = gs_watcher_new ();
        connect_watcher_signals (monitor);

        monitor->priv->manager = gs_manager_new ();
        connect_manager_signals (monitor);

        _gs_monitor_update_from_prefs (monitor, monitor->priv->prefs);
}

static void
gs_monitor_finalize (GObject *object)
{
        GSMonitor *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_MONITOR (object));

        monitor = GS_MONITOR (object);

        g_return_if_fail (monitor->priv != NULL);

        disconnect_watcher_signals (monitor);
        disconnect_listener_signals (monitor);
        disconnect_manager_signals (monitor);
        disconnect_prefs_signals (monitor);

        g_object_unref (monitor->priv->fade);
        g_object_unref (monitor->priv->grab);
        g_object_unref (monitor->priv->watcher);
        g_object_unref (monitor->priv->listener);
        g_object_unref (monitor->priv->manager);
        g_object_unref (monitor->priv->prefs);

        G_OBJECT_CLASS (gs_monitor_parent_class)->finalize (object);
}

GSMonitor *
gs_monitor_new (void)
{
        GSMonitor *monitor;

        monitor = g_object_new (GS_TYPE_MONITOR, NULL);

        return GS_MONITOR (monitor);
}

gboolean
gs_monitor_start (GSMonitor *monitor,
                  GError   **error)
{
        g_return_val_if_fail (GS_IS_MONITOR (monitor), FALSE);

        if (! gs_listener_acquire (monitor->priv->listener, error)) {
                return FALSE;
        }

        return TRUE;
}
