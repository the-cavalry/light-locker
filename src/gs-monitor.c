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

struct _GSMonitor
{
        GObject parent_instance;

        GSListener      *listener;
        GSListenerX11   *listener_x11;
        GSManager       *manager;
        LLConfig        *conf;

        gboolean         late_locking;
        gboolean         lock_on_suspend;
        gboolean         idle_hint;
        gboolean         perform_lock;
        gboolean         lock_on_lid;
};

G_DEFINE_TYPE (GSMonitor, gs_monitor, G_TYPE_OBJECT)

static void
gs_monitor_lock_screen (GSMonitor *monitor)
{
        gboolean res;
        gboolean active;

        active = gs_manager_get_active (monitor->manager);

        if (! active) {
                res = gs_listener_set_active (monitor->listener, TRUE);
                if (! res) {
                        gs_debug ("Unable to lock the screen");
                }
        }

}

static gboolean
gs_monitor_lock_session (GSMonitor *monitor)
{
        gboolean visible;

        visible = gs_manager_get_session_visible (monitor->manager);

        /* Only switch to greeter if we are the visible session */
        if (visible) {
                gs_listener_send_lock_session (monitor->listener);
        } else {
                /* Show the content in case the session gets visible again. */
                gs_manager_show_content (monitor->manager);
        }

        return FALSE;
}

static gboolean
gs_monitor_switch_greeter (GSMonitor *monitor)
{
        gboolean visible;

        visible = gs_manager_get_session_visible (monitor->manager);

        /* Only switch to greeter if we are the visible session */
        if (visible) {
                gs_listener_send_switch_greeter (monitor->listener);
        } else {
                /* Show the content in case the session gets visible again. */
                gs_manager_show_content (monitor->manager);
        }

        return FALSE;
}

static void
manager_activated_cb (GSManager *manager,
                      GSMonitor *monitor)
{
        gs_listener_resume_suspend (monitor->listener);
}

static void
manager_switch_greeter_cb (GSManager *manager,
                           GSMonitor *monitor)
{
        gs_listener_send_switch_greeter (monitor->listener);
}

static void
manager_lock_cb (GSManager *manager,
                 GSMonitor *monitor)
{
        gs_monitor_lock_screen (monitor);
        if (monitor->late_locking) {
                monitor->perform_lock = TRUE;
        } else if (gs_manager_get_session_visible (monitor->manager)) {
                /* Add a 1s delay for VT switching.
                 * This seems to preserved content exposure.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_lock_session,
                                       monitor);
        } else {
                gs_manager_show_content (monitor->manager);
        }
}

static void
conf_lock_on_suspend_cb (LLConfig    *conf,
                         GParamSpec  *pspec,
                         GSMonitor   *monitor)
{
        g_object_get (G_OBJECT(conf),
                      "lock-on-suspend", &monitor->lock_on_suspend,
                      NULL);

        if (monitor->lock_on_suspend) {
                gs_listener_delay_suspend (monitor->listener);
        } else {
                gs_listener_resume_suspend (monitor->listener);
        }
}

static void
conf_late_locking_cb (LLConfig    *conf,
                      GParamSpec  *pspec,
                      GSMonitor   *monitor)
{
        g_object_get (G_OBJECT(conf),
                      "late-locking", &monitor->late_locking,
                      NULL);
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

        gs_manager_set_lock_after (monitor->manager, lock_after_screensaver);
}

static void
conf_lock_on_lid_cb (LLConfig    *conf,
                     GParamSpec  *pspec,
                     GSMonitor   *monitor)
{
        g_object_get (G_OBJECT(conf),
                      "lock-on-lid", &monitor->lock_on_lid,
                      NULL);
}

static void
conf_idle_hint_cb (LLConfig    *conf,
                   GParamSpec  *pspec,
                   GSMonitor   *monitor)
{
        g_object_get (G_OBJECT(conf),
                      "idle_hint", &monitor->idle_hint,
                      NULL);

        gs_listener_set_idle_hint (monitor->listener,
                                   monitor->idle_hint && gs_manager_get_blank_screen (monitor->manager));
}

static void
listener_locked_cb (GSListener *listener,
                    GSMonitor  *monitor)
{
        gs_manager_show_content (monitor->manager);
        gs_monitor_lock_screen (monitor);
        monitor->perform_lock = FALSE;
}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_monitor_lock_screen (monitor);
        if (gs_listener_is_lid_closed (listener)) {
                /* Don't switch VT while the lid is closed. */
                monitor->perform_lock = TRUE;
        } else if (gs_manager_get_session_visible (monitor->manager)) {
                /* Add a 1s delay for VT switching.
                 * This seems to preserved content exposure.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_lock_session,
                                       monitor);
        } else {
                gs_manager_show_content (monitor->manager);
        }
}

static void
listener_session_switched_cb (GSListener *listener,
                              gboolean    active,
                              GSMonitor  *monitor)
{
        gs_debug ("Session switched: %d", active);
        gs_manager_set_session_visible (monitor->manager, active);
}

static gboolean
listener_active_changed_cb (GSListener *listener,
                            gboolean    active,
                            GSMonitor  *monitor)
{
        gboolean res;
        gboolean ret;

        if (monitor->lock_on_suspend && !active) {
                gs_listener_delay_suspend (monitor->listener);
        }

        res = gs_manager_set_active (monitor->manager, active);
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
        if (! monitor->lock_on_suspend)
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
        if (! monitor->lock_on_suspend)
                return;
        if (gs_listener_is_lid_closed (monitor->listener)) {
                /* This will become a lock instead of a switch.
                 * As a corner case this is ok.
                 */
                /* Don't switch VT while the lid is closed. */
                monitor->perform_lock = TRUE;
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
        gs_listener_x11_simulate_activity (monitor->listener_x11);
}

static gboolean
listener_blanking_cb (GSListener *listener,
                      gboolean    active,
                      GSMonitor  *monitor)
{
        if (! active)
        {
                /* Don't deactivate the screensaver if we are locked */
                if (gs_manager_get_active (monitor->manager))
                        return FALSE;
        }

        return gs_listener_x11_force_blanking (monitor->listener_x11, active);
}

static void
listener_inhibit_cb (GSListener *listener,
                     gboolean    active,
                     GSMonitor  *monitor)
{
        gs_listener_x11_inhibit (monitor->listener_x11, active);
}

static gulong
listener_idle_time_cb (GSListener *listener,
                       GSMonitor  *monitor)
{
        return gs_listener_x11_idle_time (monitor->listener_x11);
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
        if (monitor->perform_lock && !closed)
        {
                /* Add a 1s delay for resume to complete.
                 * This seems to fix backlight issues.
                 */
                g_timeout_add_seconds (1,
                                       (GSourceFunc)gs_monitor_lock_session,
                                       monitor);
                monitor->perform_lock = FALSE;
                return;
        }

        gs_manager_set_lid_closed (monitor->manager, closed);

        if (! monitor->lock_on_lid)
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
        gs_manager_set_blank_screen (monitor->manager, active);
        gs_listener_set_blanked (monitor->listener, active);
        if (monitor->idle_hint) {
                gs_listener_set_idle_hint (monitor->listener, active);
        }

        /* If late locking is enabled only lock the session if the lid isn't closed. */
        if (!active && !gs_listener_is_lid_closed (monitor->listener) && monitor->perform_lock) {
                gs_monitor_lock_session (monitor);
                monitor->perform_lock = FALSE;
        }
}

static void
gs_monitor_init (GSMonitor *monitor)
{
#ifdef WITH_LATE_LOCKING
        monitor->late_locking = WITH_LATE_LOCKING;
#endif
#ifdef WITH_LOCK_ON_SUSPEND
        monitor->lock_on_suspend = WITH_LOCK_ON_SUSPEND;
#endif
#ifdef WITH_LOCK_ON_LID
        monitor->lock_on_lid = WITH_LOCK_ON_LID;
#endif
        monitor->idle_hint = FALSE;

        monitor->listener = gs_listener_new ();
        monitor->listener_x11 = gs_listener_x11_new ();
        monitor->manager = gs_manager_new ();

        /*
         * Listener signals
         */
        g_signal_connect (monitor->listener, "locked",
                          G_CALLBACK (listener_locked_cb), monitor);
        g_signal_connect (monitor->listener, "lock",
                          G_CALLBACK (listener_lock_cb), monitor);
        g_signal_connect (monitor->listener, "session-switched",
                          G_CALLBACK (listener_session_switched_cb), monitor);
        g_signal_connect (monitor->listener, "active-changed",
                          G_CALLBACK (listener_active_changed_cb), monitor);
        g_signal_connect (monitor->listener, "suspend",
                          G_CALLBACK (listener_suspend_cb), monitor);
        g_signal_connect (monitor->listener, "resume",
                          G_CALLBACK (listener_resume_cb), monitor);
        g_signal_connect (monitor->listener, "simulate-user-activity",
                          G_CALLBACK (listener_simulate_user_activity_cb), monitor);
        g_signal_connect (monitor->listener, "blanking",
                          G_CALLBACK (listener_blanking_cb), monitor);
        g_signal_connect (monitor->listener, "inhibit",
                          G_CALLBACK (listener_inhibit_cb), monitor);
        g_signal_connect (monitor->listener, "idle-time",
                          G_CALLBACK (listener_idle_time_cb), monitor);
        g_signal_connect (monitor->listener, "notify::lid-closed",
                          G_CALLBACK (listener_lid_closed_cb), monitor);

        g_signal_connect (monitor->listener_x11, "blanking-changed",
                          G_CALLBACK (listener_x11_blanking_changed_cb), monitor);

        /*
         * Manager signals
         */
        g_signal_connect (monitor->manager, "activated",
                          G_CALLBACK (manager_activated_cb), monitor);
        g_signal_connect (monitor->manager, "switch-greeter",
                          G_CALLBACK (manager_switch_greeter_cb), monitor);
        g_signal_connect (monitor->manager, "lock",
                          G_CALLBACK (manager_lock_cb), monitor);
}

static void
gs_monitor_dispose (GObject *object)
{
        GSMonitor *monitor = GS_MONITOR (object);

        /*
         * Conf signals
         */
        g_signal_handlers_disconnect_by_func (monitor->conf, conf_lock_on_suspend_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->conf, conf_late_locking_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->conf, conf_lock_after_screensaver_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->conf, conf_lock_on_lid_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->conf, conf_idle_hint_cb, monitor);

        /*
         * Listener signals
         */
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_locked_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_lock_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_session_switched_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_active_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_suspend_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_resume_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_simulate_user_activity_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_blanking_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_inhibit_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_idle_time_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener, listener_lid_closed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->listener_x11, listener_x11_blanking_changed_cb, monitor);

        /*
         * Manager signals
         */
        g_signal_handlers_disconnect_by_func (monitor->manager, manager_activated_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->manager, manager_switch_greeter_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->manager, manager_lock_cb, monitor);

        g_clear_object (&monitor->conf);
        g_clear_object (&monitor->listener);
        g_clear_object (&monitor->listener_x11);
        g_clear_object (&monitor->manager);

        G_OBJECT_CLASS (gs_monitor_parent_class)->dispose (object);
}

static void
gs_monitor_class_init (GSMonitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gs_monitor_dispose;
}

GSMonitor *
gs_monitor_new (LLConfig *config)
{
        GSMonitor *monitor;
        guint lock_after_screensaver = 5;

        monitor = g_object_new (GS_TYPE_MONITOR, NULL);

        monitor->conf = config;

        g_signal_connect (monitor->conf, "notify::lock-on-suspend",
                          G_CALLBACK (conf_lock_on_suspend_cb), monitor);
        g_signal_connect (monitor->conf, "notify::late-locking",
                          G_CALLBACK (conf_late_locking_cb), monitor);
        g_signal_connect (monitor->conf, "notify::lock-after-screensaver",
                          G_CALLBACK (conf_lock_after_screensaver_cb), monitor);
        g_signal_connect (monitor->conf, "notify::lock-on-lid",
                          G_CALLBACK (conf_lock_on_lid_cb), monitor);
        g_signal_connect (monitor->conf, "notify::idle-hint",
                          G_CALLBACK (conf_idle_hint_cb), monitor);

        g_object_get (G_OBJECT (config),
                      "late-locking", &monitor->late_locking,
                      "lock-on-suspend", &monitor->lock_on_suspend,
                      "lock-on-lid", &monitor->lock_on_lid,
                      "idle-hint", &monitor->idle_hint,
                      "lock-after-screensaver", &lock_after_screensaver,
                      NULL);

        gs_manager_set_lock_after (monitor->manager, lock_after_screensaver);

        if (monitor->lock_on_suspend) {
              gs_listener_delay_suspend (monitor->listener);
        }

        return GS_MONITOR (monitor);
}

gboolean
gs_monitor_start (GSMonitor *monitor,
                  GError   **error)
{
        g_return_val_if_fail (GS_IS_MONITOR (monitor), FALSE);

        if (! gs_listener_acquire (monitor->listener, error)) {
                return FALSE;
        }

        gs_listener_x11_acquire (monitor->listener_x11);

        return TRUE;
}
