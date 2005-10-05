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
#include "gs-watcher-x11.h"
#include "gs-power.h"
#include "gs-listener-dbus.h"
#include "gs-monitor.h"
#include "gs-prefs.h"

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
};

enum {
        PROP_0
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSMonitor, gs_monitor, G_TYPE_OBJECT);

static void
gs_monitor_class_init (GSMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = gs_monitor_finalize;

        g_type_class_add_private (klass, sizeof (GSMonitorPrivate));
}

static void
manager_blanked_cb (GSManager *manager,
                    GSMonitor *monitor)
{
}

static void
manager_unblanked_cb (GSManager *manager,
                      GSMonitor *monitor)
{
        gs_listener_set_active (monitor->priv->listener, FALSE);
}

static gboolean
watcher_idle_cb (GSWatcher *watcher,
                 int        reserved,
                 GSMonitor *monitor)
{
        gboolean res;

        res = gs_listener_set_idle (monitor->priv->listener, TRUE);

        return res;
}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_listener_set_active (monitor->priv->listener, TRUE);
        gs_manager_set_lock_active (monitor->priv->manager, TRUE);
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
listener_active_changed_cb (GSListener *listener,
                            gboolean    active,
                            GSMonitor  *monitor)
{
        if (active) {
                /* turn off the idleness watcher */
                gs_watcher_set_active (monitor->priv->watcher, FALSE);

                /* blank the screen */
                gs_manager_blank (monitor->priv->manager);

                /* enable power management */
                gs_power_set_active (monitor->priv->power, TRUE);
        } else {
                /* unblank the screen */
                gs_manager_unblank (monitor->priv->manager);

                /* turn on the idleness watcher */
                gs_watcher_set_active (monitor->priv->watcher, TRUE);

                /* disable power management */
                gs_power_set_active (monitor->priv->power, FALSE);
        }
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
        gs_manager_set_lock_enabled (monitor->priv->manager, monitor->priv->prefs->lock);
        gs_manager_set_lock_timeout (monitor->priv->manager, monitor->priv->prefs->lock_timeout);
        gs_manager_set_logout_enabled (monitor->priv->manager, monitor->priv->prefs->logout_enabled);
        gs_manager_set_user_switch_enabled (monitor->priv->manager, monitor->priv->prefs->user_switch_enabled);
        gs_manager_set_logout_timeout (monitor->priv->manager, monitor->priv->prefs->logout_timeout);
        gs_manager_set_cycle_timeout (monitor->priv->manager, monitor->priv->prefs->cycle);
        gs_manager_set_mode (monitor->priv->manager, monitor->priv->prefs->mode);
        gs_manager_set_themes (monitor->priv->manager, monitor->priv->prefs->themes);

        gs_watcher_set_active (monitor->priv->watcher,
                               monitor->priv->prefs->mode != GS_MODE_DONT_BLANK);

        gs_watcher_set_timeout (monitor->priv->watcher, monitor->priv->prefs->timeout);

        gs_power_set_timeouts (monitor->priv->power,
                               monitor->priv->prefs->dpms_standby,
                               monitor->priv->prefs->dpms_suspend,
                               monitor->priv->prefs->dpms_off);
        gs_power_set_enabled (monitor->priv->power,
                              monitor->priv->prefs->dpms_enabled);
}

static void
gs_monitor_init (GSMonitor *monitor)
{

        monitor->priv = GS_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->prefs = gs_prefs_new ();
        g_signal_connect_swapped (monitor->priv->prefs, "changed",
                                  G_CALLBACK (_gs_monitor_update_from_prefs), monitor);

        monitor->priv->listener = gs_listener_new ();
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

        monitor->priv->watcher = gs_watcher_new (monitor->priv->prefs->timeout);
        g_signal_connect (monitor->priv->watcher, "idle",
                          G_CALLBACK (watcher_idle_cb), monitor);

        monitor->priv->manager = gs_manager_new ();
        g_signal_connect (monitor->priv->manager, "blanked",
                          G_CALLBACK (manager_blanked_cb), monitor);
        g_signal_connect (monitor->priv->manager, "unblanked",
                          G_CALLBACK (manager_unblanked_cb), monitor);

        monitor->priv->power = gs_power_new ();
        g_signal_connect (monitor->priv->power, "changed",
                          G_CALLBACK (power_changed_cb), monitor);

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

        g_object_unref (monitor->priv->watcher);
        g_object_unref (monitor->priv->listener);
        g_object_unref (monitor->priv->manager);
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

        gs_listener_set_active (monitor->priv->listener, FALSE);

        return TRUE;
}
