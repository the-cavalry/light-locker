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
#include <time.h>
#include <string.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#ifdef HAVE_MIT_SAVER_EXTENSION
#include <X11/extensions/scrnsaver.h>
#endif

#ifdef HAVE_DPMS_EXTENSION
#include <X11/extensions/dpms.h>
#endif

#include "gs-listener-x11.h"
#include "gs-marshal.h"
#include "gs-debug.h"

struct _GSListenerX11
{
        GObject parent_instance;

#ifdef HAVE_MIT_SAVER_EXTENSION
        int scrnsaver_event_base;
#endif

        gboolean active;
};

enum {
        BLANKING_CHANGED,
        LAST_SIGNAL
};

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSListenerX11, gs_listener_x11, G_TYPE_OBJECT)

static GdkFilterReturn
xroot_filter (GdkXEvent *xevent,
              GdkEvent  *event,
              gpointer  data)
{
#ifdef HAVE_MIT_SAVER_EXTENSION /* Added to suppress warnings */
        GSListenerX11 *listener;
#endif
        XEvent *ev;

        g_return_val_if_fail (data != NULL, GDK_FILTER_CONTINUE);
        g_return_val_if_fail (GS_IS_LISTENER_X11 (data), GDK_FILTER_CONTINUE);

#ifdef HAVE_MIT_SAVER_EXTENSION /* Added to suppress warnings */
        listener = GS_LISTENER_X11 (data);
#endif

        ev = xevent;

        switch (ev->xany.type) {
        default:
                /* extension events */
#ifdef HAVE_MIT_SAVER_EXTENSION
                if (ev->xany.type == (listener->scrnsaver_event_base + ScreenSaverNotify)) {
                        XScreenSaverNotifyEvent *xssne = (XScreenSaverNotifyEvent *) ev;
                        switch (xssne->state) {
                        case ScreenSaverOff:
                        case ScreenSaverDisabled:
                                gs_debug ("ScreenSaver stopped");
                                if (listener->active)
                                        g_signal_emit (listener, signals [BLANKING_CHANGED], 0, FALSE);
                                listener->active = FALSE;
                                break;

                        case ScreenSaverOn:
                                gs_debug ("ScreenSaver started");
                                if (!listener->active)
                                        g_signal_emit (listener, signals [BLANKING_CHANGED], 0, TRUE);
                                listener->active = TRUE;
                                break;
                        }
                }
#endif
                break;
        }

        return GDK_FILTER_CONTINUE;
}


gboolean
gs_listener_x11_acquire (GSListenerX11 *listener)
{
#ifdef HAVE_MIT_SAVER_EXTENSION /* Added to suppress warnings */
        GdkDisplay *display;
        GdkScreen *screen;
        GdkWindow *window;
#endif
#ifdef HAVE_MIT_SAVER_EXTENSION
        int scrnsaver_error_base;
        unsigned long events;
#endif

        g_return_val_if_fail (listener != NULL, FALSE);

#ifdef HAVE_MIT_SAVER_EXTENSION /* Added to suppress warnings */
        display = gdk_display_get_default ();
        screen = gdk_display_get_default_screen (display);
        window = gdk_screen_get_root_window (screen);
#endif

#ifdef HAVE_MIT_SAVER_EXTENSION
        gdk_error_trap_push ();
        if (XScreenSaverQueryExtension (GDK_DISPLAY_XDISPLAY (display), &listener->scrnsaver_event_base, &scrnsaver_error_base)) {
                events = ScreenSaverNotifyMask;
                XScreenSaverSelectInput (GDK_DISPLAY_XDISPLAY (display), GDK_WINDOW_XID (window), events);
                gs_debug ("ScreenSaver Registered");
        } else {
                gs_debug ("ScreenSaverExtension not found");
        }

        gdk_error_trap_pop_ignored ();
#endif

        gdk_window_add_filter (NULL, (GdkFilterFunc)xroot_filter, listener);

        return TRUE;
}

void
gs_listener_x11_simulate_activity (GSListenerX11 *listener)
{
        GdkDisplay *display;

        display = gdk_display_get_default ();

        gdk_error_trap_push ();
        XResetScreenSaver (GDK_DISPLAY_XDISPLAY (display));
#ifdef HAVE_DPMS_EXTENSION
        DPMSForceLevel (GDK_DISPLAY_XDISPLAY (display), DPMSModeOn);
#endif
        gdk_error_trap_pop_ignored ();
}

gboolean
gs_listener_x11_force_blanking (GSListenerX11 *listener,
                                gboolean       active)
{
        GdkDisplay *display;

        display = gdk_display_get_default ();

        gdk_error_trap_push ();
        if (active) {
                XActivateScreenSaver (GDK_DISPLAY_XDISPLAY (display));
        } else {
                XResetScreenSaver (GDK_DISPLAY_XDISPLAY (display));
#ifdef HAVE_DPMS_EXTENSION
                DPMSForceLevel (GDK_DISPLAY_XDISPLAY (display), DPMSModeOn);
#endif
        }

        gdk_error_trap_pop_ignored ();
        /* TODO: what should the return value be? */
        return active;
}

void
gs_listener_x11_inhibit (GSListenerX11 *listener,
                         gboolean       active)
{
#ifdef HAVE_MIT_SAVER_EXTENSION
        GdkDisplay *display;

        display = gdk_display_get_default ();

        gdk_error_trap_push ();
        XScreenSaverSuspend (GDK_DISPLAY_XDISPLAY (display), active);

        gdk_error_trap_pop_ignored ();
#endif
}

gulong
gs_listener_x11_idle_time (GSListenerX11 *listener)
{
        gulong secs = 0;
#ifdef HAVE_MIT_SAVER_EXTENSION
        GdkDisplay *display;
        GdkScreen *screen;
        GdkWindow *window;
        XScreenSaverInfo scrnsaver_info;

        display = gdk_display_get_default ();
        screen = gdk_display_get_default_screen (display);
        window = gdk_screen_get_root_window (screen);

        gdk_error_trap_push ();
        if (XScreenSaverQueryInfo (GDK_DISPLAY_XDISPLAY (display), GDK_WINDOW_XID (window), &scrnsaver_info)) {
                secs = scrnsaver_info.idle / 1000;
        } else {
                gs_debug ("ScreenSaverExtension not found");
        }

        gdk_error_trap_pop_ignored ();
#endif
        return secs;
}

static void
gs_listener_x11_init (GSListenerX11 *listener)
{
}

static void
gs_listener_x11_finalize (GObject *object)
{
        gdk_window_remove_filter (NULL, (GdkFilterFunc)xroot_filter, NULL);

        G_OBJECT_CLASS (gs_listener_x11_parent_class)->finalize (object);
}

static void
gs_listener_x11_class_init (GSListenerX11Class *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = gs_listener_x11_finalize;

        signals [BLANKING_CHANGED] =
                g_signal_new ("blanking-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);
}

GSListenerX11 *
gs_listener_x11_new (void)
{
        GSListenerX11 *listener;

        listener = g_object_new (GS_TYPE_LISTENER_X11, NULL);

        return GS_LISTENER_X11 (listener);
}
