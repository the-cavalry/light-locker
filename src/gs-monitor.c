/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Portions derived from xscreensaver,
 * Copyright (c) 1991-2004 Jamie Zawinski <jwz@jwz.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>

#include "gnome-screensaver.h"

#include "gs-manager.h"
#include "gs-watcher-x11.h"
#include "gs-listener-dbus.h"
#include "gs-monitor.h"
#include "gs-prefs.h"

#include "dpms.h"

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
        gs_watcher_set_active (monitor->priv->watcher, TRUE);
}

static void
watcher_idle_cb (GSWatcher *watcher,
                 GSMonitor *monitor)
{
        gs_watcher_set_active (monitor->priv->watcher, FALSE);
        gs_manager_blank (monitor->priv->manager);
}

static void
listener_lock_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_watcher_set_active (monitor->priv->watcher, FALSE);
        gs_manager_set_lock_enabled (monitor->priv->manager, TRUE);
        gs_manager_blank (monitor->priv->manager);
}

static void
listener_quit_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_watcher_set_active (monitor->priv->watcher, FALSE);
        gs_manager_unblank (monitor->priv->manager);
        gnome_screensaver_quit ();
}

static void
listener_cycle_cb (GSListener *listener,
                   GSMonitor  *monitor)
{
        gs_manager_cycle (monitor->priv->manager);
}

static void
listener_activate_cb (GSListener *listener,
                      GSMonitor  *monitor)
{
        gs_watcher_set_active (monitor->priv->watcher, FALSE);
        gs_manager_blank (monitor->priv->manager);
}

static void
listener_deactivate_cb (GSListener *listener,
                        GSMonitor  *monitor)
{
        gs_manager_unblank (monitor->priv->manager);
        gs_watcher_set_active (monitor->priv->watcher, TRUE);
}

static void
listener_poke_cb (GSListener *listener,
                  GSMonitor  *monitor)
{
        gs_watcher_reset (monitor->priv->watcher);
}

static void
prefs_changed_cb (GSPrefs   *prefs,
                  GSMonitor *monitor)
{
        gs_manager_set_mode (monitor->priv->manager, monitor->priv->prefs->mode);
        gs_manager_set_themes (monitor->priv->manager, monitor->priv->prefs->themes);
        gs_manager_set_logout_enabled (monitor->priv->manager, monitor->priv->prefs->logout_enabled);
        gs_manager_set_logout_timeout (monitor->priv->manager, monitor->priv->prefs->logout_timeout);

        gs_watcher_set_timeout (monitor->priv->watcher, monitor->priv->prefs->timeout);
        gs_watcher_set_dpms (monitor->priv->watcher,
                             monitor->priv->prefs->dpms_enabled,
                             monitor->priv->prefs->dpms_standby,
                             monitor->priv->prefs->dpms_suspend,
                             monitor->priv->prefs->dpms_off);
}

static void
gs_monitor_init (GSMonitor *monitor)
{

        monitor->priv = GS_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->prefs = gs_prefs_new ();
        g_signal_connect (monitor->priv->prefs, "changed",
                          G_CALLBACK (prefs_changed_cb), monitor);

        monitor->priv->listener = gs_listener_new ();
        g_signal_connect (monitor->priv->listener, "lock",
                          G_CALLBACK (listener_lock_cb), monitor);
        g_signal_connect (monitor->priv->listener, "quit",
                          G_CALLBACK (listener_quit_cb), monitor);
        g_signal_connect (monitor->priv->listener, "cycle",
                          G_CALLBACK (listener_cycle_cb), monitor);
        g_signal_connect (monitor->priv->listener, "activate",
                          G_CALLBACK (listener_activate_cb), monitor);
        g_signal_connect (monitor->priv->listener, "deactivate",
                          G_CALLBACK (listener_deactivate_cb), monitor);
        g_signal_connect (monitor->priv->listener, "poke",
                          G_CALLBACK (listener_poke_cb), monitor);

        monitor->priv->watcher = gs_watcher_new (monitor->priv->prefs->timeout);
        gs_watcher_set_dpms (monitor->priv->watcher,
                             monitor->priv->prefs->dpms_enabled,
                             monitor->priv->prefs->dpms_standby,
                             monitor->priv->prefs->dpms_suspend,
                             monitor->priv->prefs->dpms_off);
        g_signal_connect (monitor->priv->watcher, "idle",
                          G_CALLBACK (watcher_idle_cb), monitor);

        monitor->priv->manager = gs_manager_new (monitor->priv->prefs->lock_timeout,
                                                 monitor->priv->prefs->cycle);
        gs_manager_set_logout_enabled (monitor->priv->manager, monitor->priv->prefs->logout_enabled);
        gs_manager_set_logout_timeout (monitor->priv->manager, monitor->priv->prefs->logout_timeout);

        gs_manager_set_mode (monitor->priv->manager, monitor->priv->prefs->mode);
        gs_manager_set_themes (monitor->priv->manager, monitor->priv->prefs->themes);

        g_signal_connect (monitor->priv->manager, "blanked",
                          G_CALLBACK (manager_blanked_cb), monitor);
        g_signal_connect (monitor->priv->manager, "unblanked",
                          G_CALLBACK (manager_unblanked_cb), monitor);
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
        GError    *error = NULL;

        monitor = g_object_new (GS_TYPE_MONITOR, NULL);

        if (! gs_listener_acquire (monitor->priv->listener, &error)) {
                g_object_unref (monitor);
                return NULL;
        }

        gs_watcher_set_active (monitor->priv->watcher, TRUE);

        return GS_MONITOR (monitor);
}
