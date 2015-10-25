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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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

#include "light-locker.h"
#include "ll-config.h"

#include "gs-manager.h"

#include "gs-listener-dbus.h"
#include "gs-listener-x11.h"
#include "gs-monitor.h"
#include "gs-debug.h"

static void     gs_monitor_class_init (GSMonitorClass *klass);
static void     gs_monitor_init       (GSMonitor      *monitor);
static void     gs_monitor_finalize   (GObject        *object);

#define GS_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_MONITOR, GSMonitorPrivate))

struct GSMonitorPrivate
{
        GSListener      *listener;
        GSListenerX11   *listener_x11;
        GSManager       *manager;
        LLConfig        *conf;

        guint            late_locking : 1;
        guint            lock_on_suspend : 1;
        guint            idle_hint : 1;
        guint            perform_lock : 1;
        guint            lock_on_lid : 1;
};

G_DEFINE_TYPE (GSMonitor, gs_monitor, G_TYPE_OBJECT)

static void
gs_monitor_class_init (GSMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gs_monitor_finalize;

        g_type_class_add_private (klass, sizeof (GSMonitorPrivate));
}

static void
gs_monitor_lock_screen (GSMonitor *monitor)
{
        gboolean res;
        gboolean active;

        active = gs_manager_get_active (monitor->priv->manager);

        if (! active) {
                res = gs_listener_set_active (monitor->priv->listener, TRUE);
                if (! res) {
                        gs_debug ("Unable to lock the screen");
                }
        }

}

static gboolean
gs_monitor_lock_session (GSMonitor *monitor)
{
        gboolean visible;

        visible = gs_manager_get_session_visible (monitor->priv->manager);

        /* Only switch to greeter if we are the visible session */
        if (visible) {
                gs_listener_send_lock_session (monitor->priv->listener);
        } else {
                /* Show the content in case the session gets visible again. */
                gs_manager_show_content (monitor->priv->manager);
        }

        return FALSE;
}

static gboolean
gs_monitor_switch_greeter (GSMonitor *monitor)
{
        gboolean visible;

        visible = gs_manager_get_session_visible (monitor->priv->manager);

        /* Only switch to greeter if we are the visible session */
        if (visible) {
                gs_listener_send_switch_greeter (monitor->priv->listener);
        } else {
                /* Show the content in case the session gets visible again. */
                gs_manager_show_content (monitor->priv->manager);
        }

        return FALSE;
}

static void
manager_activated_cb (GSManager *manager,
                      GSMonitor *monitor)
{
        gs_listener_resume_suspend (monitor->priv->listener);
}

static void
manager_switch_greeter_cb (GSManager *manager,
                           GSMonitor *monitor)
{
        gs_listener_send_switch_greeter (monitor->priv->listener);
}

static void
manager_lock_cb (GSManager *manager,
                 GSMonitor *monitor)
{
        gs_monitor_lock_screen (monitor);
        if (monitor->priv->late_locking) {
                monitor->priv->perform_lock = TRUE;
        } else if (gs_manager_get_session_visible (monitor->priv->manager)) {
                /* Add a 1s delay for VT switching.
                 * This seems to preserved content exposure.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_lock_session,
                                       monitor);
        } else {
                gs_manager_show_content (monitor->priv->manager);
        }
}

static void
conf_lock_on_suspend_cb (LLConfig    *conf,
                         GParamSpec  *pspec,
                         GSMonitor   *monitor)
{
        gboolean lock_on_suspend = FALSE;

        g_object_get (G_OBJECT(conf),
                      "lock-on-suspend", &lock_on_suspend,
                      NULL);

        monitor->priv->lock_on_suspend = lock_on_suspend;

        if (lock_on_suspend) {
                gs_listener_delay_suspend (monitor->priv->listener);
        } else {
                gs_listener_resume_suspend (monitor->priv->listener);
        }
}

static void
conf_late_locking_cb (LLConfig    *conf,
                      GParamSpec  *pspec,
                      GSMonitor   *monitor)
{
        gboolean late_locking = FALSE;

        g_object_get (G_OBJECT(conf),
                      "late-locking", &late_locking,
                      NULL);

        monitor->priv->late_locking = late_locking;
}

static void
conf_lock_after_screensaver_cb (LLConfig    *conf,
                                GParamSpec  *pspec,
                                GSMonitor   *monitor)
{
        guint lock_after_screensaver = 5;

        g_object_get (G_OBJECT(conf),
                      "lock-after-screensaver", &lock_after_screensaver,
                      NULL);

        gs_manager_set_lock_after (monitor->priv->manager, lock_after_screensaver);
}

static void
conf_lock_on_lid_cb (LLConfig    *conf,
                     GParamSpec  *pspec,
                     GSMonitor   *monitor)
{
        gboolean lock_on_lid = FALSE;

        g_object_get (G_OBJECT(conf),
                      "lock-on-lid", &lock_on_lid,
                      NULL);

        monitor->priv->lock_on_lid = lock_on_lid;
}

static void
conf_idle_hint_cb (LLConfig    *conf,
                   GParamSpec  *pspec,
                   GSMonitor   *monitor)
{
        gboolean idle_hint = FALSE;

        g_object_get (G_OBJECT(conf),
                      "idle_hint", &idle_hint,
                      NULL);

        monitor->priv->idle_hint = idle_hint;

        gs_listener_set_idle_hint (monitor->priv->listener,
                                   idle_hint && gs_manager_get_blank_screen (monitor->priv->manager));
}

static void
disconnect_conf_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->conf, conf_lock_on_suspend_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->conf, conf_late_locking_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->conf, conf_lock_after_screensaver_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->conf, conf_lock_on_lid_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->conf, conf_idle_hint_cb, monitor);
}

static void
connect_conf_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->conf, "notify::lock-on-suspend",
                          G_CALLBACK (conf_lock_on_suspend_cb), monitor);
        g_signal_connect (monitor->priv->conf, "notify::late-locking",
                          G_CALLBACK (conf_late_locking_cb), monitor);
        g_signal_connect (monitor->priv->conf, "notify::lock-after-screensaver",
                          G_CALLBACK (conf_lock_after_screensaver_cb), monitor);
        g_signal_connect (monitor->priv->conf, "notify::lock-on-lid",
                          G_CALLBACK (conf_lock_on_lid_cb), monitor);
        g_signal_connect (monitor->priv->conf, "notify::idle-hint",
                          G_CALLBACK (conf_idle_hint_cb), monitor);
}

static void
listener_locked_cb (GSListener *listener,
                    GSMonitor  *monitor)
{
        gs_manager_show_content (monitor->priv->manager);
        gs_monitor_lock_screen (monitor);
        monitor->priv->perform_lock = FALSE;
}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_monitor_lock_screen (monitor);
        if (gs_listener_is_lid_closed (listener)) {
                /* Don't switch VT while the lid is closed. */
                monitor->priv->perform_lock = TRUE;
        } else if (gs_manager_get_session_visible (monitor->priv->manager)) {
                /* Add a 1s delay for VT switching.
                 * This seems to preserved content exposure.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_lock_session,
                                       monitor);
        } else {
                gs_manager_show_content (monitor->priv->manager);
        }
}

static void
listener_session_switched_cb (GSListener *listener,
                              gboolean    active,
                              GSMonitor  *monitor)
{
        gs_debug ("Session switched: %d", active);
        gs_manager_set_session_visible (monitor->priv->manager, active);
}

static gboolean
listener_active_changed_cb (GSListener *listener,
                            gboolean    active,
                            GSMonitor  *monitor)
{
        gboolean res;
        gboolean ret;

        if (monitor->priv->lock_on_suspend && !active) {
                gs_listener_delay_suspend (monitor->priv->listener);
        }

        res = gs_manager_set_active (monitor->priv->manager, active);
        if (! res) {
                gs_debug ("Unable to set manager active: %d", active);
                ret = FALSE;
                goto done;
        }

        ret = TRUE;

 done:

        return ret;
}

static void
listener_suspend_cb (GSListener *listener,
                     GSMonitor  *monitor)
{
        if (! monitor->priv->lock_on_suspend)
                return;
        /* Show the lock screen until resume.
         * We lock the screen here even when the displaymanager didn't send the signal.
         * This means that need tell the displaymanager to lock the session before it can unlock.
         */
        gs_monitor_lock_screen (monitor);
}

static void
listener_resume_cb (GSListener *listener,
                    GSMonitor  *monitor)
{
        if (! monitor->priv->lock_on_suspend)
                return;
        if (gs_listener_is_lid_closed (monitor->priv->listener)) {
                /* This will become a lock instead of a switch.
                 * As a corner case this is ok.
                 */
                /* Don't switch VT while the lid is closed. */
                monitor->priv->perform_lock = TRUE;
        } else {
                /* Add a 1s delay for resume to complete.
                 * This seems to fix backlight issues.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_switch_greeter,
                                       monitor);
        }
}

static void
listener_simulate_user_activity_cb (GSListener *listener,
                                    GSMonitor  *monitor)
{
        gs_listener_x11_simulate_activity (monitor->priv->listener_x11);
}

static gboolean
listener_blanking_cb (GSListener *listener,
                      gboolean    active,
                      GSMonitor  *monitor)
{
        if (! active)
        {
                /* Don't deactivate the screensaver if we are locked */
                if (gs_manager_get_active (monitor->priv->manager))
                        return FALSE;
        }

        return gs_listener_x11_force_blanking (monitor->priv->listener_x11, active);
}

static void
listener_inhibit_cb (GSListener *listener,
                     gboolean    active,
                     GSMonitor  *monitor)
{
        gs_listener_x11_inhibit (monitor->priv->listener_x11, active);
}

static gulong
listener_idle_time_cb (GSListener *listener,
                       GSMonitor  *monitor)
{
        return gs_listener_x11_idle_time (monitor->priv->listener_x11);
}

static void
listener_lid_closed_cb (GSListener *listener,
                        GParamSpec  *pspec,
                        GSMonitor   *monitor)
{
        gboolean closed = gs_listener_is_lid_closed (listener);

        /* If the manager requested a lock when the lid was closed.
         * We don't take the reason of the lock into account.
         * That would only complicate it.
         * In case of resume the switch would become a lock.
         * And in case of late locking, the screen saver state isn't taken into account.
         */
        if (monitor->priv->perform_lock && !closed)
        {
                /* Add a 1s delay for resume to complete.
                 * This seems to fix backlight issues.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_lock_session,
                                       monitor);
                monitor->priv->perform_lock = FALSE;
                return;
        }

        gs_manager_set_lid_closed (monitor->priv->manager, closed);

        if (! monitor->priv->lock_on_lid)
                return;

        if (closed)
        {
                /* Show the lock screen until lid open.
                 * We lock the screen here even when the displaymanager didn't send the signal.
                 * This means that need tell the displaymanager to lock the session before it can unlock.
                 */
                gs_monitor_lock_screen (monitor);
        }
        else
        {
                /* Add a 1s delay for resume to complete.
                 * This seems to fix backlight issues.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_switch_greeter,
                                       monitor);
        }
}

static void
listener_x11_blanking_changed_cb (GSListenerX11 *listener,
                                  gboolean    active,
                                  GSMonitor  *monitor)
{
        gs_debug ("Blanking changed: %d", active);
        gs_manager_set_blank_screen (monitor->priv->manager, active);
        gs_listener_set_blanked (monitor->priv->listener, active);
        if (monitor->priv->idle_hint) {
                gs_listener_set_idle_hint (monitor->priv->listener, active);
        }

        /* If late locking is enabled only lock the session if the lid isn't closed. */
        if (!active && !gs_listener_is_lid_closed (monitor->priv->listener) && monitor->priv->perform_lock) {
                gs_monitor_lock_session (monitor);
                monitor->priv->perform_lock = FALSE;
        }
}

static void
disconnect_listener_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_locked_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_lock_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_session_switched_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_active_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_suspend_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_resume_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_simulate_user_activity_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_blanking_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_inhibit_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_idle_time_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_lid_closed_cb, monitor);

        g_signal_handlers_disconnect_by_func (monitor->priv->listener_x11, listener_x11_blanking_changed_cb, monitor);
}

static void
connect_listener_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->listener, "locked",
                          G_CALLBACK (listener_locked_cb), monitor);
        g_signal_connect (monitor->priv->listener, "lock",
                          G_CALLBACK (listener_lock_cb), monitor);
        g_signal_connect (monitor->priv->listener, "session-switched",
                          G_CALLBACK (listener_session_switched_cb), monitor);
        g_signal_connect (monitor->priv->listener, "active-changed",
                          G_CALLBACK (listener_active_changed_cb), monitor);
        g_signal_connect (monitor->priv->listener, "suspend",
                          G_CALLBACK (listener_suspend_cb), monitor);
        g_signal_connect (monitor->priv->listener, "resume",
                          G_CALLBACK (listener_resume_cb), monitor);
        g_signal_connect (monitor->priv->listener, "simulate-user-activity",
                          G_CALLBACK (listener_simulate_user_activity_cb), monitor);
        g_signal_connect (monitor->priv->listener, "blanking",
                          G_CALLBACK (listener_blanking_cb), monitor);
        g_signal_connect (monitor->priv->listener, "inhibit",
                          G_CALLBACK (listener_inhibit_cb), monitor);
        g_signal_connect (monitor->priv->listener, "idle-time",
                          G_CALLBACK (listener_idle_time_cb), monitor);
        g_signal_connect (monitor->priv->listener, "notify::lid-closed",
                          G_CALLBACK (listener_lid_closed_cb), monitor);

        g_signal_connect (monitor->priv->listener_x11, "blanking-changed",
                          G_CALLBACK (listener_x11_blanking_changed_cb), monitor);
}

static void
disconnect_manager_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_activated_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_switch_greeter_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_lock_cb, monitor);
}

static void
connect_manager_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->manager, "activated",
                          G_CALLBACK (manager_activated_cb), monitor);
        g_signal_connect (monitor->priv->manager, "switch-greeter",
                          G_CALLBACK (manager_switch_greeter_cb), monitor);
        g_signal_connect (monitor->priv->manager, "lock",
                          G_CALLBACK (manager_lock_cb), monitor);
}

static void
gs_monitor_init (GSMonitor *monitor)
{

        monitor->priv = GS_MONITOR_GET_PRIVATE (monitor);

#ifdef WITH_LATE_LOCKING
        monitor->priv->late_locking = WITH_LATE_LOCKING;
#endif
#ifdef WITH_LOCK_ON_SUSPEND
        monitor->priv->lock_on_suspend = WITH_LOCK_ON_SUSPEND;
#endif
#ifdef WITH_LOCK_ON_LID
        monitor->priv->lock_on_lid = WITH_LOCK_ON_LID;
#endif
        monitor->priv->idle_hint = FALSE;

        monitor->priv->listener = gs_listener_new ();
        monitor->priv->listener_x11 = gs_listener_x11_new ();
        connect_listener_signals (monitor);

        monitor->priv->manager = gs_manager_new ();
        connect_manager_signals (monitor);
}

static void
gs_monitor_finalize (GObject *object)
{
        GSMonitor *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_MONITOR (object));

        monitor = GS_MONITOR (object);

        g_return_if_fail (monitor->priv != NULL);

        disconnect_conf_signals (monitor);
        disconnect_listener_signals (monitor);
        disconnect_manager_signals (monitor);

        g_object_unref (monitor->priv->conf);
        g_object_unref (monitor->priv->listener);
        g_object_unref (monitor->priv->listener_x11);
        g_object_unref (monitor->priv->manager);

        G_OBJECT_CLASS (gs_monitor_parent_class)->finalize (object);
}

GSMonitor *
gs_monitor_new (LLConfig *config)
{
        GSMonitor *monitor;
        gboolean late_locking = FALSE;
        gboolean lock_on_suspend = FALSE;
        gboolean lock_on_lid = FALSE;
        guint lock_after_screensaver = 5;
        gboolean idle_hint = FALSE;

        monitor = g_object_new (GS_TYPE_MONITOR, NULL);

        monitor->priv->conf = config;

        connect_conf_signals (monitor);

        g_object_get (G_OBJECT (config),
                      "late-locking", &late_locking,
                      "lock-on-suspend", &lock_on_suspend,
                      "lock-on-lid", &lock_on_lid,
                      "lock-after-screensaver", &lock_after_screensaver,
                      "idle-hint", &idle_hint,
                      NULL);

        monitor->priv->late_locking = late_locking;
        monitor->priv->lock_on_suspend = lock_on_suspend;
        monitor->priv->lock_on_lid = lock_on_lid;
        monitor->priv->idle_hint = idle_hint;

        gs_manager_set_lock_after (monitor->priv->manager, lock_after_screensaver);

        if (lock_on_suspend) {
              gs_listener_delay_suspend (monitor->priv->listener);
        }

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

        gs_listener_x11_acquire (monitor->priv->listener_x11);

        return TRUE;
}
