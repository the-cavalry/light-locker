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

#include "gs-manager.h"
#include "gs-grab.h"

#include "gs-listener-dbus.h"
#include "gs-monitor.h"
#include "gs-debug.h"

static void     gs_monitor_class_init (GSMonitorClass *klass);
static void     gs_monitor_init       (GSMonitor      *monitor);
static void     gs_monitor_finalize   (GObject        *object);

#define GS_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_MONITOR, GSMonitorPrivate))

struct GSMonitorPrivate
{
        GSListener     *listener;
        GSManager      *manager;
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
manager_switch_greeter_cb (GSManager *manager,
                           GSMonitor *monitor)
{
        gs_listener_send_switch_greeter (monitor->priv->listener);
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

static void
gs_monitor_lock_session (GSMonitor *monitor)
{
        gboolean visible;

        visible = gs_manager_get_session_visible (monitor->priv->manager);

        /* Only swith to greeter is we are the visible session */
        if (visible) {
                gs_listener_send_switch_greeter (monitor->priv->listener);
        }

}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_monitor_lock_screen (monitor);
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
        /* Show the lock screen until resome.
         * We lock the screen here even when the displaymanager didn't send the signal.
         * This means that need tell the displaymanager to lock the session before it can unlock.
         */
        gs_monitor_lock_screen (monitor);
}

static void
listener_resume_cb (GSListener *listener,
                    GSMonitor  *monitor)
{
        gs_monitor_lock_session (monitor);
}

static void
disconnect_listener_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_lock_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_session_switched_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_active_changed_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_suspend_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->listener, listener_resume_cb, monitor);
}

static void
connect_listener_signals (GSMonitor *monitor)
{
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
}

static void
disconnect_manager_signals (GSMonitor *monitor)
{
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_activated_cb, monitor);
        g_signal_handlers_disconnect_by_func (monitor->priv->manager, manager_switch_greeter_cb, monitor);
}

static void
connect_manager_signals (GSMonitor *monitor)
{
        g_signal_connect (monitor->priv->manager, "activated",
                          G_CALLBACK (manager_activated_cb), monitor);
        g_signal_connect (monitor->priv->manager, "switch-greeter",
                          G_CALLBACK (manager_switch_greeter_cb), monitor);
}

static void
gs_monitor_init (GSMonitor *monitor)
{

        monitor->priv = GS_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->listener = gs_listener_new ();
        connect_listener_signals (monitor);

        monitor->priv->grab = gs_grab_new ();

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

        disconnect_listener_signals (monitor);
        disconnect_manager_signals (monitor);

        g_object_unref (monitor->priv->grab);
        g_object_unref (monitor->priv->listener);
        g_object_unref (monitor->priv->manager);

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

        if (! gs_listener_acquire (monitor->priv->listener)) {
                return FALSE;
        }

        return TRUE;
}
