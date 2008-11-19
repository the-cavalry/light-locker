/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

/* compile with:
 * gcc `pkg-config --cflags --libs gtk+-2.0` -o test-idle-ext test-idle-ext.c
 */

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <gdk/gdkx.h>

static GMainLoop   *loop = NULL;
static int          sync_event_base;
static XSyncCounter counter;
static XSyncAlarm   xalarm_positive;
static XSyncAlarm   xalarm_negative;

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

static GdkFilterReturn
xevent_filter (GdkXEvent     *xevent,
               GdkEvent      *event,
               gpointer       data)
{
        XEvent                *ev;
        XSyncAlarmNotifyEvent *alarm_event;

        ev = xevent;
        if (ev->xany.type != sync_event_base + XSyncAlarmNotify) {
                return GDK_FILTER_CONTINUE;
        }

        alarm_event = xevent;

        g_debug ("Alarm fired, idle time = %lld",
                 _xsyncvalue_to_int64 (alarm_event->counter_value));

        return GDK_FILTER_CONTINUE;
}

static gboolean
init_xsync (void)
{
        int                 sync_error_base;
        int                 res;
        int                 major;
        int                 minor;
        int                 i;
        int                 ncounters;
        XSyncSystemCounter *counters;

        res = XSyncQueryExtension (GDK_DISPLAY (),
                                   &sync_event_base,
                                   &sync_error_base);
        if (! res) {
                g_warning ("Sync extension not present");
                return FALSE;
        }

        res = XSyncInitialize (GDK_DISPLAY (), &major, &minor);
        if (! res) {
                g_warning ("Unable to initialize Sync extension");
                return FALSE;
        }

        counters = XSyncListSystemCounters (GDK_DISPLAY (), &ncounters);
        for (i = 0; i < ncounters; i++) {
                if (counters[i].name != NULL
                    && strcmp (counters[i].name, "IDLETIME") == 0) {
                        counter = counters[i].counter;
                        break;
                }
        }
        XSyncFreeSystemCounterList (counters);

        if (counter == None) {
                g_warning ("IDLETIME counter not found");
                return FALSE;
        }

        /* select for sync events */
        gdk_error_trap_push ();
        XSelectInput (GDK_DISPLAY (), GDK_ROOT_WINDOW (), XSyncAlarmNotifyMask);
        if (gdk_error_trap_pop ()) {
                g_warning ("XSelectInput failed");
        }

        gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, NULL);

        return TRUE;
}

static void
test_idle_ext (void)
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
        attr.trigger.counter = counter;
        attr.trigger.value_type = XSyncAbsolute;
        attr.trigger.wait_value = _int64_to_xsyncvalue ((gint64)60000);
        attr.delta = delta;

        attr.trigger.test_type = XSyncPositiveTransition;
        xalarm_positive = XSyncCreateAlarm (GDK_DISPLAY (), flags, &attr);

        attr.trigger.test_type = XSyncNegativeTransition;
        xalarm_negative = XSyncCreateAlarm (GDK_DISPLAY (), flags, &attr);

}

int
main (int    argc,
      char **argv)
{
        gdk_init (&argc, &argv);

        init_xsync ();
        test_idle_ext ();

        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);

        return 0;
}
