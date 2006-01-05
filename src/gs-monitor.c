/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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
#include "gs-power.h"
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
        GSPower        *power;
        GSPrefs        *prefs;
        GSFade         *fade;
};

enum {
        PROP_0
};

#define FADE_TIMEOUT 10000

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSMonitor, gs_monitor, G_TYPE_OBJECT)

static void
gs_monitor_class_init (GSMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

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
watcher_idle_notice_cb (GSWatcher *watcher,
                        gboolean   in_effect,
                        GSMonitor *monitor)
{
        gboolean activation_enabled;

        gs_debug ("Idle notice signal detected: %d", in_effect);

        /* only fade if screensaver can activate */
        activation_enabled = gs_listener_get_activation_enabled (monitor->priv->listener);

        if (! activation_enabled) {
                return TRUE;
        }

        if (in_effect) {
                /* start slow fade */
                gs_fade_set_timeout (monitor->priv->fade, FADE_TIMEOUT);
                gs_fade_set_active (monitor->priv->fade, TRUE);
        } else {
                /* cancel the fade */
                gs_fade_set_active (monitor->priv->fade, FALSE);
                gs_fade_reset (monitor->priv->fade);
        }

        return TRUE;
}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gboolean res;

        res = gs_listener_set_active (monitor->priv->listener, TRUE);
        if (res) {
                gs_manager_set_lock_active (monitor->priv->manager, TRUE);
        } else {
                g_warning ("Unable to lock the screen");
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

static gboolean
listener_active_changed_cb (GSListener *listener,
                            gboolean    active,
                            GSMonitor  *monitor)
{
        gboolean res;
        gboolean idle_watch_enabled;

        idle_watch_enabled = gs_watcher_get_enabled (monitor->priv->watcher);

        if (active) {
                if (idle_watch_enabled) {
                        /* turn off the idleness watcher */
                        res = gs_watcher_set_active (monitor->priv->watcher, FALSE);
                        if (! res) {
                                g_warning ("Unable to deactivate the idle watcher");
                                return FALSE;
                        }
                }

                /* blank the screen */
                res = gs_manager_set_active (monitor->priv->manager, TRUE);
                if (! res) {
                        g_warning ("Unable to blank the screen");

                        /* since we can't activate then reactivate the watcher
                           and give up */
                        if (idle_watch_enabled) {
                                res = gs_watcher_set_active (monitor->priv->watcher, TRUE);
                        }

                        return FALSE;
                }

                /* enable power management */
                res = gs_power_set_active (monitor->priv->power, TRUE);
                if (! res) {
                        g_warning ("Unable to activate power management");

                        /* if we can't activate power management it isn't the
                           end of the world */
                }
        } else {
                /* unblank the screen */
                res = gs_manager_set_active (monitor->priv->manager, FALSE);
                if (! res) {
                        g_warning ("Unable to unblank the screen");
                        return FALSE;
                }

                if (idle_watch_enabled) {
                        /* turn on the idleness watcher */
                        res = gs_watcher_set_active (monitor->priv->watcher, TRUE);
                        if (! res) {
                                g_warning ("Unable to activate the idle watcher");
                                return FALSE;
                        }
                }

                /* disable power management */
                res = gs_power_set_active (monitor->priv->power, FALSE);
                if (! res) {
                        g_warning ("Unable to deactivate power management");
                }
        }

        return TRUE;
}

static void
listener_throttled_changed_cb (GSListener *listener,
                               gboolean    enabled,
                               GSMonitor  *monitor)
{
        gs_manager_set_throttle_enabled (monitor->priv->manager, enabled);
}

static void
listener_poke_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        /* in case the screen isn't blanked reset the
           idle watcher */
        gs_watcher_reset (monitor->priv->watcher);

        /* turn on the monitor power */
        gs_power_set_mode (monitor->priv->power,
                           GS_POWER_MODE_ON);

        /* request that the manager unlock -
           will pop up a dialog if necessary */
        gs_manager_request_unlock (monitor->priv->manager);
}

static void
power_changed_cb (GSPower    *power,
                  GSPowerMode mode,
                  GSMonitor  *monitor)
{
        gboolean is_on;

        if (mode == GS_POWER_MODE_ON) {
                is_on = TRUE;
        } else {
                is_on = FALSE;
        }

        /* Don't run themes if the monitor power is off */
        if (! is_on) {
                gs_manager_set_throttle_enabled (monitor->priv->manager, TRUE);
        }
}

static void
_gs_monitor_update_from_prefs (GSMonitor *monitor,
                               GSPrefs   *prefs)
{
        gboolean idle_detection_enabled;
        gboolean idle_detection_active;
        gboolean activate_watch;
        gboolean manager_active;

        gs_manager_set_lock_enabled (monitor->priv->manager, monitor->priv->prefs->lock_enabled);
        gs_manager_set_lock_timeout (monitor->priv->manager, monitor->priv->prefs->lock_timeout);
        gs_manager_set_logout_enabled (monitor->priv->manager, monitor->priv->prefs->logout_enabled);
        gs_manager_set_user_switch_enabled (monitor->priv->manager, monitor->priv->prefs->user_switch_enabled);
        gs_manager_set_logout_timeout (monitor->priv->manager, monitor->priv->prefs->logout_timeout);
        gs_manager_set_logout_command (monitor->priv->manager, monitor->priv->prefs->logout_command);
        gs_manager_set_cycle_timeout (monitor->priv->manager, monitor->priv->prefs->cycle);
        gs_manager_set_mode (monitor->priv->manager, monitor->priv->prefs->mode);
        gs_manager_set_themes (monitor->priv->manager, monitor->priv->prefs->themes);

        /* enable activation in all cases except when DONT_BLANK */
        gs_listener_set_activation_enabled (monitor->priv->listener,
                                            monitor->priv->prefs->mode != GS_MODE_DONT_BLANK);

        /* idle detection always enabled */
        idle_detection_enabled = TRUE;

        gs_watcher_set_timeout (monitor->priv->watcher, monitor->priv->prefs->timeout);
        gs_watcher_set_enabled (monitor->priv->watcher, idle_detection_enabled);

        /* in the case where idle detection is reenabled we may need to
           activate the watcher too */

        manager_active = gs_manager_is_active (monitor->priv->manager);
        idle_detection_active = gs_watcher_get_active (monitor->priv->watcher);
        activate_watch = (! manager_active
                          && ! idle_detection_active
                          && idle_detection_enabled);
        if (activate_watch) {
                gs_watcher_set_active (monitor->priv->watcher, TRUE);
        }

        gs_power_set_timeouts (monitor->priv->power,
                               monitor->priv->prefs->dpms_standby,
                               monitor->priv->prefs->dpms_suspend,
                               monitor->priv->prefs->dpms_off);
        gs_power_set_enabled (monitor->priv->power,
                              monitor->priv->prefs->dpms_enabled);
}

static void
disconnect_listener_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_lock_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_quit_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_cycle_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_active_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_throttled_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_poke_cb, monitor);
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
        g_signal_connect (monitor->priv->listener, "throttle-enabled-changed",
                          G_CALLBACK (listener_throttled_changed_cb), monitor);
        g_signal_connect (monitor->priv->listener, "poke",
                          G_CALLBACK (listener_poke_cb), monitor);
}

static void
disconnect_watcher_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->watcher, watcher_idle_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->watcher, watcher_idle_notice_cb, monitor);
}

static void
connect_watcher_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->watcher, "idle_changed",
                          G_CALLBACK (watcher_idle_cb), monitor);
        g_signal_connect (monitor->priv->watcher, "idle_notice_changed",
                          G_CALLBACK (watcher_idle_notice_cb), monitor);
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
disconnect_power_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->power, power_changed_cb, monitor);
}

static void
connect_power_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->power, "changed",
                          G_CALLBACK (power_changed_cb), monitor);
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
        monitor->priv->watcher = gs_watcher_new (monitor->priv->prefs->timeout);
        connect_watcher_signals (monitor);

        monitor->priv->manager = gs_manager_new ();
        connect_manager_signals (monitor);

        monitor->priv->power = gs_power_new ();
        connect_power_signals (monitor);

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
        disconnect_power_signals (monitor);
        disconnect_prefs_signals (monitor);

        g_object_unref (monitor->priv->fade);
        g_object_unref (monitor->priv->watcher);
        g_object_unref (monitor->priv->listener);
        g_object_unref (monitor->priv->manager);
        g_object_unref (monitor->priv->power);
        g_object_unref (monitor->priv->prefs);

        G_OBJECT_CLASS (parent_class)->finalize (object);
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
