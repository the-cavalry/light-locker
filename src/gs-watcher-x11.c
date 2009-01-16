/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008      Red Hat, Inc.
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
#include <time.h>
#include <errno.h>

#include <string.h>
#include <gdk/gdkx.h>

#include "gs-idle-monitor.h"
#include "gs-watcher.h"
#include "gs-marshal.h"
#include "gs-debug.h"

static void     gs_watcher_class_init (GSWatcherClass *klass);
static void     gs_watcher_init       (GSWatcher      *watcher);
static void     gs_watcher_finalize   (GObject        *object);

static gboolean watchdog_timer        (GSWatcher      *watcher);

#define GS_WATCHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WATCHER, GSWatcherPrivate))

struct GSWatcherPrivate
{
        /* settings */
        guint           enabled : 1;
        guint           timeout;
        guint           delta_notice_timeout;

        /* state */
        guint           active : 1;
        guint           idle : 1;
        guint           idle_notice : 1;

        GSIdleMonitor  *idle_monitor;
        guint           notice_id;
        guint           idle_id;

        guint           watchdog_timer_id;

};

enum {
        IDLE_CHANGED,
        IDLE_NOTICE_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_TIMEOUT,
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSWatcher, gs_watcher, G_TYPE_OBJECT)

static void
remove_watchdog_timer (GSWatcher *watcher)
{
        if (watcher->priv->watchdog_timer_id != 0) {
                g_source_remove (watcher->priv->watchdog_timer_id);
                watcher->priv->watchdog_timer_id = 0;
        }
}

static void
add_watchdog_timer (GSWatcher *watcher,
                    glong      timeout)
{
        watcher->priv->watchdog_timer_id = g_timeout_add (timeout,
                                                          (GSourceFunc)watchdog_timer,
                                                          watcher);
}

void
gs_watcher_reset (GSWatcher *watcher)
{
        g_return_if_fail (GS_IS_WATCHER (watcher));

        if (watcher->priv->idle_monitor != NULL) {
                gs_idle_monitor_reset (watcher->priv->idle_monitor);
        }

        /* restart if necessary */
        if (watcher->priv->active) {
                gs_watcher_set_active (watcher, FALSE);
                gs_watcher_set_active (watcher, TRUE);
        }
}

void
gs_watcher_set_timeout (GSWatcher  *watcher,
                        guint       timeout)
{
        g_return_if_fail (GS_IS_WATCHER (watcher));

        if (watcher->priv->timeout != timeout) {
                watcher->priv->timeout = timeout;

                /* restart the timers if necessary */
                gs_watcher_reset (watcher);
        }
}

static void
gs_watcher_set_property (GObject            *object,
                         guint               prop_id,
                         const GValue       *value,
                         GParamSpec         *pspec)
{
        GSWatcher *self;

        self = GS_WATCHER (object);

        switch (prop_id) {
        case PROP_TIMEOUT:
                gs_watcher_set_timeout (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_watcher_get_property (GObject            *object,
                         guint               prop_id,
                         GValue             *value,
                         GParamSpec         *pspec)
{
        GSWatcher *self;

        self = GS_WATCHER (object);

        switch (prop_id) {
        case PROP_TIMEOUT:
                g_value_set_uint (value, self->priv->timeout);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_watcher_class_init (GSWatcherClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = gs_watcher_finalize;
        object_class->get_property = gs_watcher_get_property;
        object_class->set_property = gs_watcher_set_property;

        signals [IDLE_CHANGED] =
                g_signal_new ("idle-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWatcherClass, idle_changed),
                              NULL,
                              NULL,
                              gs_marshal_BOOLEAN__BOOLEAN,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_BOOLEAN);
        signals [IDLE_NOTICE_CHANGED] =
                g_signal_new ("idle-notice-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWatcherClass, idle_notice_changed),
                              NULL,
                              NULL,
                              gs_marshal_BOOLEAN__BOOLEAN,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_TIMEOUT,
                                         g_param_spec_uint ("timeout",
                                                            NULL,
                                                            NULL,
                                                            10000,
                                                            G_MAXUINT,
                                                            600000,
                                                            G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GSWatcherPrivate));
}

static gboolean
_gs_watcher_set_session_idle_notice (GSWatcher *watcher,
                                     gboolean   in_effect)
{
        gboolean res;

        res = FALSE;

        if (in_effect != watcher->priv->idle_notice) {

                g_signal_emit (watcher, signals [IDLE_NOTICE_CHANGED], 0, in_effect, &res);
                if (res) {
                        gs_debug ("Changing idle notice state: %d", in_effect);

                        watcher->priv->idle_notice = in_effect;
                } else {
                        gs_debug ("Idle notice signal not handled: %d", in_effect);
                }
        }

        return res;
}

static gboolean
_gs_watcher_set_session_idle (GSWatcher *watcher,
                              gboolean   is_idle)
{
        gboolean res;

        res = FALSE;

        if (is_idle != watcher->priv->idle) {

                g_signal_emit (watcher, signals [IDLE_CHANGED], 0, is_idle, &res);
                if (res) {
                        gs_debug ("Changing idle state: %d", is_idle);

                        watcher->priv->idle = is_idle;
                } else {
                        gs_debug ("Idle changed signal not handled: %d", is_idle);
                }
        }

        return res;
}

static gboolean
on_notice_timeout (GSIdleMonitor *monitor,
                   guint          id,
                   gboolean       condition,
                   GSWatcher     *watcher)
{
        gboolean res;
        res = _gs_watcher_set_session_idle_notice (watcher, condition);
        return res;
}

static gboolean
on_idle_timeout (GSIdleMonitor *monitor,
                 guint          id,
                 gboolean       condition,
                 GSWatcher     *watcher)
{
        gboolean res;

        res = _gs_watcher_set_session_idle (watcher, condition);
        _gs_watcher_set_session_idle_notice (watcher, !condition);

        /* if the event wasn't handled then schedule another timer */
        if (! res) {
                gs_debug ("Idle signal was not handled, restarting watcher");
        }

        return res;
}

static gboolean
start_idle_watcher (GSWatcher *watcher)
{
        guint notice_timeout;

        g_return_val_if_fail (watcher != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        notice_timeout = watcher->priv->timeout - watcher->priv->delta_notice_timeout;
        g_debug ("GSWatcher: adding notice watch %d", notice_timeout);
        watcher->priv->notice_id
                = gs_idle_monitor_add_watch (watcher->priv->idle_monitor,
                                             notice_timeout,
                                             (GSIdleMonitorWatchFunc)on_notice_timeout,
                                             watcher);

        g_debug ("GSWatcher: adding idle watch %d", watcher->priv->timeout);
        watcher->priv->idle_id
                = gs_idle_monitor_add_watch (watcher->priv->idle_monitor,
                                             watcher->priv->timeout,
                                             (GSIdleMonitorWatchFunc)on_idle_timeout,
                                             watcher);

        watchdog_timer (watcher);

        return FALSE;
}

static gboolean
stop_idle_watcher (GSWatcher *watcher)
{
        g_return_val_if_fail (watcher != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        if (watcher->priv->notice_id > 0) {
                gs_idle_monitor_remove_watch (watcher->priv->idle_monitor,
                                              watcher->priv->notice_id);
        }
        if (watcher->priv->idle_id > 0) {
                gs_idle_monitor_remove_watch (watcher->priv->idle_monitor,
                                              watcher->priv->idle_id);
        }

        return FALSE;
}

gboolean
gs_watcher_get_active (GSWatcher *watcher)
{
        gboolean active;

        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        active = watcher->priv->active;

        return active;
}

static void
_gs_watcher_reset_state (GSWatcher *watcher)
{
        watcher->priv->idle = FALSE;
        watcher->priv->idle_notice = FALSE;
}

static gboolean
_gs_watcher_set_active_internal (GSWatcher *watcher,
                                 gboolean   active)
{
        /* reset state */
        _gs_watcher_reset_state (watcher);

        if (! active) {
                watcher->priv->active = FALSE;
                stop_idle_watcher (watcher);
                gs_debug ("Stopping idle watcher");
        } else {
                watcher->priv->active = TRUE;
                start_idle_watcher (watcher);
                gs_debug ("Starting idle watcher");
        }

        return TRUE;
}

gboolean
gs_watcher_set_active (GSWatcher *watcher,
                       gboolean   active)
{
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        gs_debug ("turning watcher: %s", active ? "ON" : "OFF");

        if (watcher->priv->active == active) {
                gs_debug ("Idle detection is already %s",
                          active ? "active" : "inactive");
                return FALSE;
        }

        if (! watcher->priv->enabled) {
                gs_debug ("Idle detection is disabled, cannot activate");
                return FALSE;
        }

        return _gs_watcher_set_active_internal (watcher, active);
}

gboolean
gs_watcher_set_enabled (GSWatcher *watcher,
                        gboolean   enabled)
{
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        if (watcher->priv->enabled != enabled) {
                gboolean is_active = gs_watcher_get_active (watcher);

                watcher->priv->enabled = enabled;

                /* if we are disabling the watcher and we are
                   active shut it down */
                if (! enabled && is_active) {
                        _gs_watcher_set_active_internal (watcher, FALSE);
                }
        }

        return TRUE;
}

gboolean
gs_watcher_get_enabled (GSWatcher *watcher)
{
        gboolean enabled;

        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        enabled = watcher->priv->enabled;

        return enabled;
}

static void
gs_watcher_init (GSWatcher *watcher)
{
        watcher->priv = GS_WATCHER_GET_PRIVATE (watcher);

        watcher->priv->enabled = TRUE;
        watcher->priv->active = FALSE;
        watcher->priv->timeout = 600000;

        watcher->priv->idle_monitor = gs_idle_monitor_new ();

        /* time before idle signal to send notice signal */
        watcher->priv->delta_notice_timeout = 10000;

        add_watchdog_timer (watcher, 600000);
}

static void
gs_watcher_finalize (GObject *object)
{
        GSWatcher *watcher;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_WATCHER (object));

        watcher = GS_WATCHER (object);

        g_return_if_fail (watcher->priv != NULL);

        remove_watchdog_timer (watcher);

        watcher->priv->active = FALSE;
        stop_idle_watcher (watcher);

        G_OBJECT_CLASS (gs_watcher_parent_class)->finalize (object);
}

/* Figuring out what the appropriate XSetScreenSaver() parameters are
   (one wouldn't expect this to be rocket science.)
*/
static void
disable_builtin_screensaver (GSWatcher *watcher,
                             gboolean   unblank_screen)
{
        int current_server_timeout, current_server_interval;
        int current_prefer_blank,   current_allow_exp;
        int desired_server_timeout, desired_server_interval;
        int desired_prefer_blank,   desired_allow_exp;

        XGetScreenSaver (GDK_DISPLAY (),
                         &current_server_timeout,
                         &current_server_interval,
                         &current_prefer_blank,
                         &current_allow_exp);

        desired_server_timeout  = current_server_timeout;
        desired_server_interval = current_server_interval;
        desired_prefer_blank    = current_prefer_blank;
        desired_allow_exp       = current_allow_exp;

        desired_server_interval = 0;

        /* I suspect (but am not sure) that DontAllowExposures might have
           something to do with powering off the monitor as well, at least
           on some systems that don't support XDPMS?  Who know... */
        desired_allow_exp = AllowExposures;

        /* When we're not using an extension, set the server-side timeout to 0,
           so that the server never gets involved with screen blanking, and we
           do it all ourselves.  (However, when we *are* using an extension,
           we tell the server when to notify us, and rather than blanking the
           screen, the server will send us an X event telling us to blank.)
        */
        desired_server_timeout = 0;

        if (desired_server_timeout     != current_server_timeout
            || desired_server_interval != current_server_interval
            || desired_prefer_blank    != current_prefer_blank
            || desired_allow_exp       != current_allow_exp) {

                gs_debug ("disabling server builtin screensaver:"
                          " (xset s %d %d; xset s %s; xset s %s)",
                          desired_server_timeout,
                          desired_server_interval,
                          (desired_prefer_blank ? "blank" : "noblank"),
                          (desired_allow_exp ? "expose" : "noexpose"));

                XSetScreenSaver (GDK_DISPLAY (),
                                 desired_server_timeout,
                                 desired_server_interval,
                                 desired_prefer_blank,
                                 desired_allow_exp);

                XSync (GDK_DISPLAY (), FALSE);
        }

        if (unblank_screen) {
                /* Turn off the server builtin saver if it is now running. */
                XForceScreenSaver (GDK_DISPLAY (), ScreenSaverReset);
        }
}


/* This timer goes off every few minutes, whether the user is idle or not,
   to try and clean up anything that has gone wrong.

   It calls disable_builtin_screensaver() so that if xset has been used,
   or some other program (like xlock) has messed with the XSetScreenSaver()
   settings, they will be set back to sensible values (if a server extension
   is in use, messing with xlock can cause the screensaver to never get a wakeup
   event, and could cause monitor power-saving to occur, and all manner of
   heinousness.)

 */

static gboolean
watchdog_timer (GSWatcher *watcher)
{

        disable_builtin_screensaver (watcher, FALSE);

        return TRUE;
}

GSWatcher *
gs_watcher_new (guint timeout)
{
        GSWatcher *watcher;

        watcher = g_object_new (GS_TYPE_WATCHER,
                                "timeout", timeout,
                                NULL);

        return GS_WATCHER (watcher);
}
