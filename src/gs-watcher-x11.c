/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Portions derived from xscreensaver,
 * Copyright (c) 1991-2004 Jamie Zawinski <jwz@jwz.org>
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
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gnome-screensaver.h"
#include "gs-watcher.h"
#include "gs-marshal.h"
#include "gs-debug.h"

static void     gs_watcher_class_init (GSWatcherClass *klass);
static void     gs_watcher_init       (GSWatcher      *watcher);
static void     gs_watcher_finalize   (GObject        *object);

static void     initialize_server_extensions (GSWatcher *watcher);

static gboolean check_pointer_timer          (GSWatcher      *watcher);
static void     schedule_wakeup_event        (GSWatcher      *watcher,
                                              int             when);
static gboolean watchdog_timer               (GSWatcher      *watcher);
static gboolean idle_timer                   (GSWatcher      *watcher);

#define GS_WATCHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WATCHER, GSWatcherPrivate))

typedef struct _PointerPosition PointerPosition;
struct _PointerPosition
{
        GdkScreen      *screen;
        int             x;
        int             y;
        GdkModifierType mask;
};

struct GSWatcherPrivate
{
        /* settings */
        guint           enabled : 1;
        guint           timeout;
        guint           pointer_timeout;

        guint           use_xidle_extension : 1;
        guint           use_sgi_saver_extension : 1;
        guint           use_mit_saver_extension : 1;

        guint           notice_timeout;

        /* state */
        guint            active : 1;
        guint            idle : 1;
        guint            idle_notice : 1;

        PointerPosition *poll_position;

        GTimer         *idle_timer;
        GTimer         *jump_timer;
        guint           emergency_lock : 1;

        guint           timer_id;
        guint           check_pointer_timer_id;
        guint           watchdog_timer_id;

        guint           xinerama : 1;
        guint           using_xidle_extension : 1;
        guint           using_sgi_saver_extension : 1;
        guint           using_mit_saver_extension : 1;

# ifdef HAVE_MIT_SAVER_EXTENSION
        int             mit_saver_ext_event_number;
        int             mit_saver_ext_error_number;
# endif
# ifdef HAVE_SGI_SAVER_EXTENSION
        int            sgi_saver_ext_event_number;
        int            sgi_saver_ext_error_number;
# endif

};

enum {
        IDLE_CHANGED,
        IDLE_NOTICE_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_TIMEOUT,
        PROP_DEBUG
};

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSWatcher, gs_watcher, G_TYPE_OBJECT)

void
gs_watcher_reset (GSWatcher *watcher)
{
        g_return_if_fail (GS_IS_WATCHER (watcher));

        /* just return quietly if not enabled */
        if (! watcher->priv->enabled) {
                return;
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

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize     = gs_watcher_finalize;
        object_class->get_property = gs_watcher_get_property;
        object_class->set_property = gs_watcher_set_property;

        signals [IDLE_CHANGED] =
                g_signal_new ("idle_changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWatcherClass, idle_changed),
                              NULL,
                              NULL,
                              gs_marshal_BOOLEAN__BOOLEAN,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_BOOLEAN);
        signals [IDLE_NOTICE_CHANGED] =
                g_signal_new ("idle_notice_changed",
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
                                                            G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (GSWatcherPrivate));
}

static int
saver_ehandler (Display     *dpy,
                XErrorEvent *error)
{
        g_warning ("BUG BUG BUG!");
        exit (1);
}

static int
BadWindow_ehandler (Display     *dpy,
                    XErrorEvent *error)
{
    /* When we notice a window being created, we spawn a timer that waits
       30 seconds or so, and then selects events on that window.  This error
       handler is used so that we can cope with the fact that the window
       may have been destroyed <30 seconds after it was created.
    */
    if (error->error_code == BadWindow ||
        error->error_code == BadMatch ||
        error->error_code == BadDrawable)
        return 0;
    else
        return saver_ehandler (dpy, error);
}

static void
notice_events_inner (Window   window,
                     gboolean enable,
                     gboolean top)
{
        XWindowAttributes attrs;
        unsigned long     events;
        Window            root;
        Window            parent;
        Window           *kids;
        unsigned int      nkids;
        int               status;
        GdkWindow        *gwindow;

        gwindow = gdk_window_lookup (window);
        if (gwindow && (window != GDK_ROOT_WINDOW ())) {
                /* If it's one of ours, don't mess up its event mask. */
                return;
        }

        kids = NULL;
        status = XQueryTree (GDK_DISPLAY (), window, &root, &parent, &kids, &nkids);

        if (status == 0) {
                if (kids)
                        XFree (kids);
                return;
        }

        if (window == root)
                top = FALSE;

        XGetWindowAttributes (GDK_DISPLAY (), window, &attrs);

        if (enable) {
                /* Select for KeyPress on all windows that already have it selected */
                events = ((attrs.all_event_masks | attrs.do_not_propagate_mask) & KeyPressMask);

                /* Keep already selected events.  This is important when the
                   window == GDK_ROOT_WINDOW () since the mask will contain
                   StructureNotifyMask that is essential for RANDR support */
                events = attrs.your_event_mask | events;

                /* Select for SubstructureNotify on all windows */
                events = SubstructureNotifyMask | events;

                /* Select for PropertyNotify events to get user time changes */
                events = PropertyChangeMask | events;
        } else {
                /* We want to disable all events */

                /* Don't mess up the root window */
                if (window == GDK_ROOT_WINDOW ()) {
                        events = attrs.your_event_mask;
                } else {
                        events = 0;
                }
        }

        /* Select for SubstructureNotify on all windows.
           Select for KeyPress on all windows that already have it selected.

           Note that we can't select for ButtonPress, because of X braindamage:
           only one client at a time may select for ButtonPress on a given
           window, though any number can select for KeyPress.  Someone explain
           *that* to me.

           So, if the user spends a while clicking the mouse without ever moving
           the mouse or touching the keyboard, we won't know that they've been
           active, and the screensaver will come on.  That sucks, but I don't
           know how to get around it.

           Since X presents mouse wheels as clicks, this applies to those, too:
           scrolling through a document using only the mouse wheel doesn't
           count as activity...  Fortunately, /proc/interrupts helps, on
           systems that have it.  Oh, if it's a PS/2 mouse, not serial or USB.
           This sucks!
        */

        XSelectInput (GDK_DISPLAY (), window, events);

        if (top && (events & KeyPressMask)) {
                /* Only mention one window per tree */
                top = FALSE;
                if (enable)
                        gs_debug ("Adding events for 0x%lX", (unsigned long)window);
                else
                        gs_debug ("Removing events for 0x%lX", (unsigned long)window);
        }

        if (kids) {
                while (nkids)
                        notice_events_inner (kids [--nkids], enable, top);

                XFree (kids);
        }
}

static void
notice_events (Window   window,
               gboolean enable,
               gboolean top)
{
        XErrorHandler old_handler;

        old_handler = XSetErrorHandler (BadWindow_ehandler);

        notice_events_inner (window, enable, top);
        XSync (GDK_DISPLAY (), FALSE);
        XSetErrorHandler (old_handler);
}

typedef struct _NoticeTimerData NoticeTimerData;
struct _NoticeTimerData
{
        GSWatcher *watcher;
        Window     window;
        gboolean   enable;
};

static gboolean
notice_events_timer (NoticeTimerData *data)
{
        gboolean is_top = TRUE;
        gboolean enable = data->enable;
        Window   window = data->window;

        notice_events (window, enable, is_top);

        return FALSE;
}

static void
stop_notice_events (GSWatcher *watcher,
                    Window     window)
{
        NoticeTimerData *data;

        data = g_new0 (NoticeTimerData, 1);

        data->window = window;
        data->watcher = watcher;
        data->enable = FALSE;

        notice_events_timer (data);
        g_free (data);
}

static void
start_notice_events_timer (GSWatcher *watcher,
                           Window     window)
{
        NoticeTimerData *data;

        data = g_new0 (NoticeTimerData, 1);

        data->window = window;
        data->watcher = watcher;
        data->enable = TRUE;

        /* FIXME: we should defer noticing events
           using a timeout so that we ignore short
           lived windows */

        /* The hard part is keeping track of all the
           timeouts for each window so that we can
           remove them when necessary */
        if (0) {
                guint timeout;

                timeout = 10000;
                g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
                                    timeout,
                                    (GSourceFunc)notice_events_timer,
                                    data,
                                    (GDestroyNotify)g_free);
        } else {
                notice_events_timer (data);
                g_free (data);
        }

}

static void
remove_idle_timer (GSWatcher *watcher)
{
        if (watcher->priv->timer_id != 0) {
                gs_debug ("killing idle_timer  (%u, %u)",
                          watcher->priv->timeout,
                          watcher->priv->timer_id);

                g_source_remove (watcher->priv->timer_id);
                watcher->priv->timer_id = 0;
        }
}

static void
add_idle_timer (GSWatcher *watcher,
                glong      timeout)
{
        watcher->priv->timer_id = g_timeout_add (timeout, (GSourceFunc)idle_timer, watcher);

        gs_debug ("starting idle_timer (%ld, %u)", timeout, watcher->priv->timer_id);
}

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

/* Call this when user activity (or "simulated" activity) has been noticed.
 */
static void
reset_timers (GSWatcher *watcher)
{

        if (watcher->priv->using_mit_saver_extension || watcher->priv->using_sgi_saver_extension) {
                return;
        }

        remove_idle_timer (watcher);

        schedule_wakeup_event (watcher, watcher->priv->timeout);

        g_timer_start (watcher->priv->idle_timer);
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

static void
_gs_watcher_notice_activity (GSWatcher *watcher)
{
        if (! watcher->priv->active) {
                gs_debug ("Noticed activity but watcher is inactive");
                return;
        }

        gs_debug ("Activity detected: resetting timers");

        /* if an idle notice was sent, cancel it */
        if (watcher->priv->idle_notice) {
                gboolean in_effect = FALSE;
                _gs_watcher_set_session_idle_notice (watcher, in_effect);
        }

        /* if idle signal was sent, cancel it */
        if (watcher->priv->idle) {
                gboolean is_idle = FALSE;
                _gs_watcher_set_session_idle (watcher, is_idle);
        }

        reset_timers (watcher);
}

static void
_gs_watcher_notice_window_created (GSWatcher *watcher,
                                   Window     window)
{
        gs_debug ("Window created: noticing activity on 0x%lX", (unsigned long)window);

        start_notice_events_timer (watcher, window);
}

static void
gs_watcher_xevent (GSWatcher *watcher,
                   GdkXEvent *xevent)
{
        XEvent *ev;

        /* do nothing if we aren't watching */
        if (! watcher->priv->active) {
                return;
        }

        ev = xevent;

        switch (ev->xany.type) {
        case KeyPress:
        case KeyRelease:
        case ButtonPress:
        case ButtonRelease:
                _gs_watcher_notice_activity (watcher);
                break;
        case PropertyNotify:
                if (ev->xproperty.atom == gdk_x11_get_xatom_by_name ("_NET_WM_USER_TIME")) {
                        _gs_watcher_notice_activity (watcher);
                }
                break;
        case CreateNotify:
                {
                        Window window = ev->xcreatewindow.window;
                        _gs_watcher_notice_window_created (watcher,
                                                           window);
                }
                break;
        default:
                break;
        }

}

static GdkFilterReturn
xevent_filter (GdkXEvent *xevent,
               GdkEvent  *event,
               GSWatcher *watcher)
{
        gs_watcher_xevent (watcher, xevent);

        return GDK_FILTER_CONTINUE;
}

static void
remove_check_pointer_timer (GSWatcher *watcher)
{
        if (watcher->priv->check_pointer_timer_id != 0) {
                g_source_remove (watcher->priv->check_pointer_timer_id);
                watcher->priv->check_pointer_timer_id = 0;
        }
}

static void
add_check_pointer_timer (GSWatcher *watcher,
                         glong      timeout)
{
        watcher->priv->check_pointer_timer_id = g_timeout_add (timeout,
                                                               (GSourceFunc)check_pointer_timer, watcher);

}

static void
start_pointer_poll (GSWatcher *watcher)
{
        /* run once to set baseline */
        check_pointer_timer (watcher);

        remove_check_pointer_timer (watcher);

        add_check_pointer_timer (watcher, watcher->priv->pointer_timeout);
}

static void
_gs_watcher_pointer_position_free (PointerPosition *pos)
{
        if (! pos)
                return;

        g_free (pos);
        pos = NULL;
}

static void
_gs_watcher_set_pointer_position (GSWatcher       *watcher,
                                  PointerPosition *pos)
{
        if (watcher->priv->poll_position)
                _gs_watcher_pointer_position_free (watcher->priv->poll_position);

        watcher->priv->poll_position = pos;
}

static void
stop_pointer_poll (GSWatcher *watcher)
{
        remove_check_pointer_timer (watcher);

        _gs_watcher_set_pointer_position (watcher, NULL);
}

static gboolean
start_idle_watcher (GSWatcher *watcher)
{
        g_return_val_if_fail (watcher != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        g_timer_start (watcher->priv->jump_timer);
        g_timer_start (watcher->priv->idle_timer);

        start_pointer_poll (watcher);

        gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, watcher);
        start_notice_events_timer (watcher, DefaultRootWindow (GDK_DISPLAY ()));

        watchdog_timer (watcher);

        return FALSE;
}

static gboolean
stop_idle_watcher (GSWatcher *watcher)
{
        g_return_val_if_fail (watcher != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        g_timer_stop (watcher->priv->jump_timer);
        g_timer_stop (watcher->priv->idle_timer);

        remove_idle_timer (watcher);

        stop_pointer_poll (watcher);

        stop_notice_events (watcher, DefaultRootWindow (GDK_DISPLAY ()));
        gdk_window_remove_filter (NULL, (GdkFilterFunc)xevent_filter, watcher);

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

static gboolean
_gs_watcher_set_active_internal (GSWatcher *watcher,
                                 gboolean   active)
{
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
        watcher->priv->pointer_timeout = 2000;

        /* time before idle signal to send notice signal */
        watcher->priv->notice_timeout = 10000;

        watcher->priv->idle_timer = g_timer_new ();
        watcher->priv->jump_timer = g_timer_new ();

        initialize_server_extensions (watcher);

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

        _gs_watcher_pointer_position_free (watcher->priv->poll_position);

        g_timer_destroy (watcher->priv->idle_timer);
        watcher->priv->idle_timer = NULL;
        g_timer_destroy (watcher->priv->jump_timer);
        watcher->priv->jump_timer = NULL;

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

#ifdef HAVE_MIT_SAVER_EXTENSION

# include <X11/extensions/scrnsaver.h>

static gboolean
query_mit_saver_extension (int *event_number,
                           int *error_number)
{
        return XScreenSaverQueryExtension (GDK_DISPLAY (),
                                           event_number,
                                           error_number);
}

static int
ignore_all_errors_ehandler (Display     *dpy,
                            XErrorEvent *error)
{
        return 0;
}


/* MIT SCREEN-SAVER server extension hackery.
 */
static gboolean
init_mit_saver_extension ()
{
        int         i;
        GdkDisplay *display   = gdk_display_get_default ();
        int         n_screens = gdk_display_get_n_screens (display);
        Pixmap     *blank_pix = (Pixmap *) calloc (sizeof (Pixmap), n_screens);

        for (i = 0; i < n_screens; i++) {
                XID        kill_id   = 0;
                Atom       kill_type = 0;
                GdkScreen *screen    = gdk_display_get_screen (display, i);
                Window     root      = RootWindowOfScreen (GDK_SCREEN_XSCREEN (screen));

                blank_pix[i] = XCreatePixmap (GDK_DISPLAY (), root, 1, 1, 1);

                /* Kill off the old MIT-SCREEN-SAVER client if there is one.
                   This tends to generate X errors, though (possibly due to a bug
                   in the server extension itself?) so just ignore errors here. */
                if (XScreenSaverGetRegistered (GDK_DISPLAY (),
                                               XScreenNumberOfScreen (GDK_SCREEN_XSCREEN (screen)),
                                               &kill_id, &kill_type)
                    && kill_id != blank_pix[i]) {
                        XErrorHandler old_handler =
                                XSetErrorHandler (ignore_all_errors_ehandler);

                        XKillClient (GDK_DISPLAY (), kill_id);
                        XSync (GDK_DISPLAY (), FALSE);
                        XSetErrorHandler (old_handler);
                }

                XScreenSaverSelectInput (GDK_DISPLAY (), root, ScreenSaverNotifyMask);
                XScreenSaverRegister (GDK_DISPLAY (),
                                      XScreenNumberOfScreen (GDK_SCREEN_XSCREEN (screen)),
                                      (XID) blank_pix [i],
                                      gdk_x11_get_xatom_by_name_for_display (display, "XA_PIXMAP"));
        }

        free (blank_pix);

        return TRUE;
}
#endif /* HAVE_MIT_SAVER_EXTENSION */


/* SGI SCREEN_SAVER server extension hackery.
 */

#ifdef HAVE_SGI_SAVER_EXTENSION

# include <X11/extensions/XScreenSaver.h>

gboolean
query_sgi_saver_extension (int *event_number,
                           int *error_number)
{
        return XScreenSaverQueryExtension (GDK_DISPLAY (),
                                           event_number,
                                           error_number);
}

static gboolean
init_sgi_saver_extension (gboolean is_blank_active)
{
        int         i;
        GdkDisplay *display   = gdk_display_get_default ();
        int         n_screens = gdk_display_get_n_screens (display);

        if (is_blank_active)
                /* If you mess with this while the server thinks it's active,
                   the server crashes. */
                return FALSE;

        for (i = 0; i < n_screens; i++) {
                GdkScreen *screen = gdk_display_get_screen (display, i);

                XScreenSaverDisable (GDK_DISPLAY (), XScreenNumberOfScreen (GDK_SCREEN_XSCREEN (screen)));
                if (! XScreenSaverEnable (GDK_DISPLAY (), XScreenNumberOfScreen (GDK_SCREEN_XSCREEN (screen)))) {

                        g_warning ("%SGI SCREEN_SAVER extension exists, but can't be initialized;\n"
                                   "perhaps some other screensaver program is already running?");

                        return FALSE;
                }
        }

        return TRUE;
}

#endif /* HAVE_SGI_SAVER_EXTENSION */


/* XIDLE server extension hackery.
 */

#ifdef HAVE_XIDLE_EXTENSION

# include <X11/extensions/xidle.h>

gboolean
query_xidle_extension (void)
{
        int event_number;
        int error_number;

        return XidleQueryExtension (GDK_DISPLAY (), &event_number, &error_number);
}

#endif /* HAVE_XIDLE_EXTENSION */

/* If any server extensions have been requested, try and initialize them.
   Issue warnings if requests can't be honored.
*/
static void
initialize_server_extensions (GSWatcher *watcher)
{
        gboolean server_has_xidle_extension     = FALSE;
        gboolean server_has_sgi_saver_extension = FALSE;
        gboolean server_has_mit_saver_extension = FALSE;

        watcher->priv->using_xidle_extension     = watcher->priv->use_xidle_extension;
        watcher->priv->using_sgi_saver_extension = watcher->priv->use_sgi_saver_extension;
        watcher->priv->using_mit_saver_extension = watcher->priv->use_mit_saver_extension;

#ifdef HAVE_XIDLE_EXTENSION
        server_has_xidle_extension     = query_xidle_extension ();
#endif
#ifdef HAVE_SGI_SAVER_EXTENSION
        server_has_sgi_saver_extension = query_sgi_saver_extension (&watcher->priv->sgi_saver_ext_event_number,
                                                                    &watcher->priv->sgi_saver_ext_error_number);
#endif
#ifdef HAVE_MIT_SAVER_EXTENSION
        server_has_mit_saver_extension = query_mit_saver_extension (&watcher->priv->mit_saver_ext_event_number,
                                                                    &watcher->priv->mit_saver_ext_error_number);
#endif

        if (! server_has_xidle_extension) {
                watcher->priv->using_xidle_extension = FALSE;
        } else {
                if (watcher->priv->using_xidle_extension) {
                        gs_debug ("Using XIDLE extension.");
                } else {
                        gs_debug ("Not using server's XIDLE extension.");
                }
        }

        if (! server_has_sgi_saver_extension) {
                watcher->priv->using_sgi_saver_extension = FALSE;
        } else {
                if (watcher->priv->using_sgi_saver_extension) {
                        gs_debug ("Using SGI SCREEN_SAVER extension.");
                } else {
                        gs_debug ("Not using server's SGI SCREEN_SAVER extension.");
                }
        }

        if (! server_has_mit_saver_extension) {
                watcher->priv->using_mit_saver_extension = FALSE;
        } else {
                if (watcher->priv->using_mit_saver_extension) {
                        gs_debug ("Using MIT-SCREEN-SAVER extension.");
                } else {
                        gs_debug ("Not using server's MIT-SCREEN-SAVER extension.");
                }
        }

        /* These are incompatible (or at least, our support for them is...) */
        if (watcher->priv->xinerama && watcher->priv->using_mit_saver_extension) {
                watcher->priv->using_mit_saver_extension = FALSE;

                gs_debug ("Xinerama in use: disabling MIT-SCREEN-SAVER.");
        }
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

        /* On SGIs, if interval is non-zero, it is the number of seconds after
           screen saving starts at which the monitor should be powered down.
           Obviously I don't want that, so set it to 0 (meaning "never".)

           Power saving is disabled if DontPreferBlanking, but in that case,
           we don't get extension events either.  So we can't turn it off that way.

           Note: if you're running Irix 6.3 (O2), you may find that your monitor is
           powering down anyway, regardless of the xset settings.  This is fixed by
           installing SGI patches 2447 and 2537.
        */
        desired_server_interval = 0;

        /* I suspect (but am not sure) that DontAllowExposures might have
           something to do with powering off the monitor as well, at least
           on some systems that don't support XDPMS?  Who know... */
        desired_allow_exp = AllowExposures;

        if (watcher->priv->using_mit_saver_extension || watcher->priv->using_sgi_saver_extension) {

                desired_server_timeout = (watcher->priv->timeout / 1000);

                /* The SGI extension won't give us events unless blanking is on.
                   I think (unsure right now) that the MIT extension is the opposite. */
                if (watcher->priv->using_sgi_saver_extension)
                        desired_prefer_blank = PreferBlanking;
                else
                        desired_prefer_blank = DontPreferBlanking;
        } else {
                /* When we're not using an extension, set the server-side timeout to 0,
                   so that the server never gets involved with screen blanking, and we
                   do it all ourselves.  (However, when we *are* using an extension,
                   we tell the server when to notify us, and rather than blanking the
                   screen, the server will send us an X event telling us to blank.)
                */

                desired_server_timeout = 0;
        }

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


#if defined(HAVE_MIT_SAVER_EXTENSION) || defined(HAVE_SGI_SAVER_EXTENSION)
        {
                static gboolean extension_initted = FALSE;

                if (! extension_initted) {

                        extension_initted = TRUE;

# ifdef HAVE_MIT_SAVER_EXTENSION
                        if (watcher->priv->using_mit_saver_extension)
                                init_mit_saver_extension ();
# endif

# ifdef HAVE_SGI_SAVER_EXTENSION
                        if (watcher->priv->using_sgi_saver_extension) {
                                gboolean blank_active = FALSE;
                                watcher->priv->using_sgi_saver_extension = init_sgi_saver_extension (blank_active);
                        }
# endif

                }
        }
#endif /* HAVE_MIT_SAVER_EXTENSION || HAVE_SGI_SAVER_EXTENSION */

        if (unblank_screen)
                /* Turn off the server builtin saver if it is now running. */
                XForceScreenSaver (GDK_DISPLAY (), ScreenSaverReset);
}

static void
maybe_send_signal (GSWatcher *watcher)
{
        gboolean polling_for_idleness = TRUE;
        gint64   elapsed;
        gboolean do_idle_signal = FALSE;
        gboolean do_notice_signal = FALSE;

        if (! watcher->priv->active) {
                gs_debug ("Checking for idleness but watcher is inactive");
                return;
        }

        if (watcher->priv->idle) {
                /* already idle, do nothing */
                return;
        }

        elapsed = 1000 * g_timer_elapsed (watcher->priv->idle_timer, NULL);

        if (elapsed >= watcher->priv->timeout) {
                /* Look, we've been idle long enough.  We're done. */
                do_idle_signal = TRUE;
        } else if (watcher->priv->emergency_lock) {
                /* Oops, the wall clock has jumped far into the future, so
                   we need to lock down in a hurry! */
                gs_debug ("Doing emergency lock");
                do_idle_signal = TRUE;
        } else {
                /* The event went off, but it turns out that the user has not
                   yet been idle for long enough.  So re-signal the event.
                   Be economical: if we should blank after 5 minutes, and the
                   user has been idle for 2 minutes, then set this timer to
                   go off in 3 minutes.
                */

                if (polling_for_idleness) {
                        guint time_left;

                        time_left = watcher->priv->timeout - elapsed;

                        if (time_left <= watcher->priv->notice_timeout) {
                                do_notice_signal = TRUE;
                        }

                        schedule_wakeup_event (watcher, time_left);
                }

                do_idle_signal = FALSE;
        }

        if (do_notice_signal && ! watcher->priv->idle_notice) {
		gboolean res = FALSE;
                gboolean in_effect = TRUE;

                res = _gs_watcher_set_session_idle_notice (watcher, in_effect);
        }

        if (do_idle_signal) {
		gboolean res = FALSE;
                gboolean is_idle = TRUE;

                res = _gs_watcher_set_session_idle (watcher, is_idle);
                _gs_watcher_set_session_idle_notice (watcher, !is_idle);

                /* if the event wasn't handled then schedule another timer */
                if (! res) {
                        gs_debug ("Idle signal was not handled, restarting watcher");
                        gs_watcher_reset (watcher);
                }
        }
}

static gboolean
idle_timer (GSWatcher *watcher)
{
        gs_debug ("in idle timer");

        /* try one last time */
        check_pointer_timer (watcher);

        watcher->priv->timer_id = 0;

        maybe_send_signal (watcher);

        return FALSE;
}

static void
schedule_wakeup_event (GSWatcher *watcher,
                       int        when)
{
        guint timeout;

        if (watcher->priv->timer_id) {
                gs_debug ("idle_timer already running");
                return;
        }
        
        timeout = when;

        /* Wake up before idle so we can send a notice signal */
        if (timeout > watcher->priv->notice_timeout) {
                timeout -= watcher->priv->notice_timeout;
        }

        /* Wake up periodically to ask the server if we are idle. */
        add_idle_timer (watcher, timeout);
}

/* An unfortunate situation is this: the saver is not active, because the
   user has been typing.  The machine is a laptop.  The user closes the lid
   and suspends it.  The CPU halts.  Some hours later, the user opens the
   lid.  At this point, Xt's timers will fire, and the screensaver will blank
   the screen.

   So far so good -- well, not really, but it's the best that we can do,
   since the OS doesn't send us a signal *before* shutdown -- but if the
   user had delayed locking (lockTimeout > 0) then we should start off
   in the locked state, rather than only locking N minutes from when the
   lid was opened.  Also, eschewing fading is probably a good idea, to
   clamp down as soon as possible.

   We only do this when we'd be polling the mouse position anyway.
   This amounts to an assumption that machines with APM support also
   have /proc/interrupts.
*/
static void
check_for_clock_skew (GSWatcher *watcher)
{
        time_t now;
        gint64 shift;
        gint64 i;
        gint64 i_h;
        gint64 i_m;
        gint64 i_s;

        now   = time (NULL);
        shift = g_timer_elapsed (watcher->priv->jump_timer, NULL);
        i     = shift;

        i_h = i / (60 * 60);
        i_m = (i / 60) % 60;
        i_s = i % 60;
        gs_debug ("checking wall clock for hibernation, changed: %" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
                  i_h, i_m, i_s);

        if (shift > (watcher->priv->timeout / 1000)) {
                gint64 s_h;
                gint64 s_m;
                gint64 s_s;

                s_h = shift / (60 * 60);
                s_m = (shift / 60) % 60;
                s_s = shift % 60;

                gs_debug ("wall clock has jumped by %" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
                          s_h, s_m, s_s);

                watcher->priv->emergency_lock = TRUE;
                maybe_send_signal (watcher);
                watcher->priv->emergency_lock = FALSE;
        }

        g_timer_start (watcher->priv->jump_timer);
}

static PointerPosition *
_gs_watcher_pointer_position_read (GSWatcher *watcher)
{
        GdkDisplay      *display;
        PointerPosition *pos;

        pos = g_new0 (PointerPosition, 1);

        display = gdk_display_get_default ();

        gdk_display_get_pointer (display,
                                 &pos->screen,
                                 &pos->x,
                                 &pos->y,
                                 &pos->mask);
        return pos;
}

static gboolean
_gs_watcher_pointer_position_compare (PointerPosition *pos1,
                                      PointerPosition *pos2)
{
        gboolean changed;

        if (! pos1)
                return TRUE;

        if (! pos2)
                return TRUE;

        changed = (pos1->x != pos2->x
                   || pos1->y != pos2->y
                   || pos1->screen != pos2->screen
                   || pos1->mask   != pos2->mask);

        return changed;
}

static void
_gs_watcher_check_pointer_position (GSWatcher *watcher)
{
        PointerPosition *pos;
        gboolean         changed;
        gint64           elapsed;

        pos = _gs_watcher_pointer_position_read (watcher);

        changed = _gs_watcher_pointer_position_compare (watcher->priv->poll_position,
                                                        pos);

        if (changed) {
                _gs_watcher_set_pointer_position (watcher, pos);
                _gs_watcher_notice_activity (watcher);
        } else {
                _gs_watcher_pointer_position_free (pos);
        }

        elapsed = g_timer_elapsed (watcher->priv->idle_timer, NULL);
        gs_debug ("Idle %" G_GINT64_FORMAT " seconds", elapsed);

        check_for_clock_skew (watcher);
}

/* When we aren't using a server extension, this timer is used to periodically
   wake up and poll the mouse position, which is possibly more reliable than
   selecting motion events on every window.
*/
static gboolean
check_pointer_timer (GSWatcher *watcher)
{
        _gs_watcher_check_pointer_position (watcher);

        return TRUE;
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
                                "timeout", timeout, NULL);

        return GS_WATCHER (watcher);
}
