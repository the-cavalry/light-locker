/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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

#include <time.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#ifdef HAVE_XTEST
#include <X11/extensions/XTest.h>
#endif /* HAVE_XTEST */

#include <glib.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>

#include "gs-debug.h"
#include "gs-idle-monitor.h"

static void gs_idle_monitor_class_init (GSIdleMonitorClass *klass);
static void gs_idle_monitor_init       (GSIdleMonitor      *idle_monitor);
static void gs_idle_monitor_finalize   (GObject             *object);

#define GS_IDLE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_IDLE_MONITOR, GSIdleMonitorPrivate))

struct GSIdleMonitorPrivate
{
        GHashTable  *watches;
        int          sync_event_base;
        XSyncCounter counter;

        /* For use with XTest */
        int         *keycode;
        int          keycode1;
        int          keycode2;
        gboolean     have_xtest;
};

typedef struct
{
        guint                  id;
        XSyncValue             interval;
        GSIdleMonitorWatchFunc callback;
        gpointer               user_data;
        XSyncAlarm             xalarm_positive;
        XSyncAlarm             xalarm_negative;
} GSIdleMonitorWatch;

static guint32 watch_serial = 1;

G_DEFINE_TYPE (GSIdleMonitor, gs_idle_monitor, G_TYPE_OBJECT)

static gint64
_xsyncvalue_to_int64 (XSyncValue value)
{
        return ((guint64) XSyncValueHigh32 (value)) << 32
                | (guint64) XSyncValueLow32 (value);
}

static XSyncValue
_int64_to_xsyncvalue (gint64 value)
{
        XSyncValue ret;

        XSyncIntsToValue (&ret, value, ((guint64)value) >> 32);

        return ret;
}

static void
gs_idle_monitor_dispose (GObject *object)
{
        GSIdleMonitor *monitor;

        g_return_if_fail (GS_IS_IDLE_MONITOR (object));

        monitor = GS_IDLE_MONITOR (object);

        if (monitor->priv->watches != NULL) {
                g_hash_table_destroy (monitor->priv->watches);
                monitor->priv->watches = NULL;
        }

        G_OBJECT_CLASS (gs_idle_monitor_parent_class)->dispose (object);
}

static gboolean
_find_alarm (gpointer            key,
             GSIdleMonitorWatch *watch,
             XSyncAlarm         *alarm)
{
        if (watch->xalarm_positive == *alarm
            || watch->xalarm_negative == *alarm) {
                return TRUE;
        }
        return FALSE;
}

static GSIdleMonitorWatch *
find_watch_for_alarm (GSIdleMonitor *monitor,
                      XSyncAlarm     alarm)
{
        GSIdleMonitorWatch *watch;

        watch = g_hash_table_find (monitor->priv->watches,
                                   (GHRFunc)_find_alarm,
                                   &alarm);
        return watch;
}

#ifdef HAVE_XTEST
static gboolean
send_fake_event (GSIdleMonitor *monitor)
{
        if (! monitor->priv->have_xtest) {
                return FALSE;
        }

        gs_debug ("GSIdleMonitor: sending fake key");

        XLockDisplay (GDK_DISPLAY());
        XTestFakeKeyEvent (GDK_DISPLAY(),
                           *monitor->priv->keycode,
                           True,
                           CurrentTime);
        XTestFakeKeyEvent (GDK_DISPLAY(),
                           *monitor->priv->keycode,
                           False,
                           CurrentTime);
        XUnlockDisplay (GDK_DISPLAY());

        /* Swap the keycode */
        if (monitor->priv->keycode == &monitor->priv->keycode1) {
                monitor->priv->keycode = &monitor->priv->keycode2;
        } else {
                monitor->priv->keycode = &monitor->priv->keycode1;
        }

        return TRUE;
}
#endif /* HAVE_XTEST */

void
gs_idle_monitor_reset (GSIdleMonitor *monitor)
{
        g_return_if_fail (GS_IS_IDLE_MONITOR (monitor));

        /* FIXME: is there a better way to reset the IDLETIME? */
        send_fake_event (monitor);
}

static void
handle_alarm_notify_event (GSIdleMonitor         *monitor,
                           XSyncAlarmNotifyEvent *alarm_event)
{
        GSIdleMonitorWatch *watch;
        gboolean            res;
        gboolean            condition;

        watch = find_watch_for_alarm (monitor, alarm_event->alarm);

        if (watch == NULL) {
                g_warning ("Unable to find watch for alarm");
                return;
        }

        gs_debug ("Watch %d fired, idle time = %lld",
                 watch->id,
                 _xsyncvalue_to_int64 (alarm_event->counter_value));

        if (alarm_event->alarm == watch->xalarm_positive) {
                condition = TRUE;
        } else {
                condition = FALSE;
        }

        res = TRUE;
        if (watch->callback != NULL) {
                res = watch->callback (monitor,
                                       watch->id,
                                       condition,
                                       watch->user_data);
        }

        if (! res) {
                /* reset all timers */
                gs_debug ("GSIdleMonitor: callback returned FALSE; resetting idle time");
                gs_idle_monitor_reset (monitor);
        }
}

static GdkFilterReturn
xevent_filter (GdkXEvent     *xevent,
               GdkEvent      *event,
               GSIdleMonitor *monitor)
{
        XEvent                *ev;
        XSyncAlarmNotifyEvent *alarm_event;

        ev = xevent;
        if (ev->xany.type != monitor->priv->sync_event_base + XSyncAlarmNotify) {
                return GDK_FILTER_CONTINUE;
        }

        alarm_event = xevent;

        handle_alarm_notify_event (monitor, alarm_event);

        return GDK_FILTER_CONTINUE;
}

static gboolean
init_xsync (GSIdleMonitor *monitor)
{
        int                 sync_error_base;
        int                 res;
        int                 major;
        int                 minor;
        int                 i;
        int                 ncounters;
        XSyncSystemCounter *counters;

        res = XSyncQueryExtension (GDK_DISPLAY (),
                                   &monitor->priv->sync_event_base,
                                   &sync_error_base);
        if (! res) {
                g_warning ("GSIdleMonitor: Sync extension not present");
                return FALSE;
        }

        res = XSyncInitialize (GDK_DISPLAY (), &major, &minor);
        if (! res) {
                g_warning ("GSIdleMonitor: Unable to initialize Sync extension");
                return FALSE;
        }

        counters = XSyncListSystemCounters (GDK_DISPLAY (), &ncounters);
        for (i = 0; i < ncounters; i++) {
                if (counters[i].name != NULL
                    && strcmp (counters[i].name, "IDLETIME") == 0) {
                        monitor->priv->counter = counters[i].counter;
                        break;
                }
        }
        XSyncFreeSystemCounterList (counters);

        if (monitor->priv->counter == None) {
                g_warning ("GSIdleMonitor: IDLETIME counter not found");
                return FALSE;
        }

        /* select for sync events */
        gdk_error_trap_push ();
        XSelectInput (GDK_DISPLAY (), GDK_ROOT_WINDOW (), XSyncAlarmNotifyMask);
        if (gdk_error_trap_pop ()) {
                g_warning ("XSelectInput failed");
        }

        gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, monitor);

        return TRUE;
}

static void
_init_xtest (GSIdleMonitor *monitor)
{
#ifdef HAVE_XTEST
        int a, b, c, d;

        XLockDisplay (GDK_DISPLAY());
        monitor->priv->have_xtest = (XTestQueryExtension (GDK_DISPLAY(), &a, &b, &c, &d) == True);
        if (monitor->priv->have_xtest) {
                monitor->priv->keycode1 = XKeysymToKeycode (GDK_DISPLAY(), XK_Alt_L);
                if (monitor->priv->keycode1 == 0) {
                        g_warning ("keycode1 not existant");
                }
                monitor->priv->keycode2 = XKeysymToKeycode (GDK_DISPLAY(), XK_Alt_R);
                if (monitor->priv->keycode2 == 0) {
                        monitor->priv->keycode2 = XKeysymToKeycode (GDK_DISPLAY(), XK_Alt_L);
                        if (monitor->priv->keycode2 == 0) {
                                g_warning ("keycode2 not existant");
                        }
                }
                monitor->priv->keycode = &monitor->priv->keycode1;
        }
        XUnlockDisplay (GDK_DISPLAY());
#endif /* HAVE_XTEST */
}

static GObject *
gs_idle_monitor_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        GSIdleMonitor *monitor;

        monitor = GS_IDLE_MONITOR (G_OBJECT_CLASS (gs_idle_monitor_parent_class)->constructor (type,
                                                                                               n_construct_properties,
                                                                                               construct_properties));

        _init_xtest (monitor);

        if (! init_xsync (monitor)) {
                g_object_unref (monitor);
                return NULL;
        }

        return G_OBJECT (monitor);
}

static void
gs_idle_monitor_class_init (GSIdleMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gs_idle_monitor_finalize;
        object_class->dispose = gs_idle_monitor_dispose;
        object_class->constructor = gs_idle_monitor_constructor;

        g_type_class_add_private (klass, sizeof (GSIdleMonitorPrivate));
}

static guint32
get_next_watch_serial (void)
{
        guint32 serial;

        serial = watch_serial++;

        if ((gint32)watch_serial < 0) {
                watch_serial = 1;
        }

        /* FIXME: make sure it isn't in the hash */

        return serial;
}

static GSIdleMonitorWatch *
idle_monitor_watch_new (guint interval)
{
        GSIdleMonitorWatch *watch;

        watch = g_slice_new0 (GSIdleMonitorWatch);
        watch->interval = _int64_to_xsyncvalue ((gint64)interval);
        watch->id = get_next_watch_serial ();
        watch->xalarm_positive = None;
        watch->xalarm_negative = None;

        return watch;
}

static void
idle_monitor_watch_free (GSIdleMonitorWatch *watch)
{
        if (watch == NULL) {
                return;
        }
        if (watch->xalarm_positive != None) {
                XSyncDestroyAlarm (GDK_DISPLAY (), watch->xalarm_positive);
        }
        if (watch->xalarm_negative != None) {
                XSyncDestroyAlarm (GDK_DISPLAY (), watch->xalarm_negative);
        }
        g_slice_free (GSIdleMonitorWatch, watch);
}

static void
gs_idle_monitor_init (GSIdleMonitor *monitor)
{
        monitor->priv = GS_IDLE_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->watches = g_hash_table_new_full (NULL,
                                                        NULL,
                                                        NULL,
                                                        (GDestroyNotify)idle_monitor_watch_free);

        monitor->priv->counter = None;
}

static void
gs_idle_monitor_finalize (GObject *object)
{
        GSIdleMonitor *idle_monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_IDLE_MONITOR (object));

        idle_monitor = GS_IDLE_MONITOR (object);

        g_return_if_fail (idle_monitor->priv != NULL);

        G_OBJECT_CLASS (gs_idle_monitor_parent_class)->finalize (object);
}

GSIdleMonitor *
gs_idle_monitor_new (void)
{
        GObject *idle_monitor;

        idle_monitor = g_object_new (GS_TYPE_IDLE_MONITOR,
                                     NULL);

        return GS_IDLE_MONITOR (idle_monitor);
}

static gboolean
_xsync_alarm_set (GSIdleMonitor      *monitor,
                  GSIdleMonitorWatch *watch)
{
        XSyncAlarmAttributes attr;
        XSyncValue           delta;
        guint                flags;

        flags = XSyncCACounter
                | XSyncCAValueType
                | XSyncCATestType
                | XSyncCAValue
                | XSyncCADelta;

        XSyncIntToValue (&delta, 0);
        attr.trigger.counter = monitor->priv->counter;
        attr.trigger.value_type = XSyncAbsolute;
        attr.trigger.wait_value = watch->interval;
        attr.delta = delta;

        attr.trigger.test_type = XSyncPositiveTransition;
        if (watch->xalarm_positive != None) {
                gs_debug ("GSIdleMonitor: updating alarm for positive transition wait=%lld",
                          _xsyncvalue_to_int64 (attr.trigger.wait_value));
                XSyncChangeAlarm (GDK_DISPLAY (), watch->xalarm_positive, flags, &attr);
        } else {
                gs_debug ("GSIdleMonitor: creating new alarm for positive transition wait=%lld",
                          _xsyncvalue_to_int64 (attr.trigger.wait_value));
                watch->xalarm_positive = XSyncCreateAlarm (GDK_DISPLAY (), flags, &attr);
        }

        attr.trigger.test_type = XSyncNegativeTransition;
        if (watch->xalarm_negative != None) {
                gs_debug ("GSIdleMonitor: updating alarm for negative transition wait=%lld",
                          _xsyncvalue_to_int64 (attr.trigger.wait_value));
                XSyncChangeAlarm (GDK_DISPLAY (), watch->xalarm_negative, flags, &attr);
        } else {
                gs_debug ("GSIdleMonitor: creating new alarm for positive transition wait=%lld",
                          _xsyncvalue_to_int64 (attr.trigger.wait_value));
                watch->xalarm_negative = XSyncCreateAlarm (GDK_DISPLAY (), flags, &attr);
        }

        return TRUE;
}

guint
gs_idle_monitor_add_watch (GSIdleMonitor         *monitor,
                           guint                  interval,
                           GSIdleMonitorWatchFunc callback,
                           gpointer               user_data)
{
        GSIdleMonitorWatch *watch;

        g_return_val_if_fail (GS_IS_IDLE_MONITOR (monitor), 0);
        g_return_val_if_fail (callback != NULL, 0);

        watch = idle_monitor_watch_new (interval);
        watch->callback = callback;
        watch->user_data = user_data;

        _xsync_alarm_set (monitor, watch);

        g_hash_table_insert (monitor->priv->watches,
                             GUINT_TO_POINTER (watch->id),
                             watch);
        return watch->id;
}

void
gs_idle_monitor_remove_watch (GSIdleMonitor *monitor,
                              guint          id)
{
        g_return_if_fail (GS_IS_IDLE_MONITOR (monitor));

        g_hash_table_remove (monitor->priv->watches,
                             GUINT_TO_POINTER (id));
}
