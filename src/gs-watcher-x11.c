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
#include "gs-watcher-x11.h"

#include "dpms.h"

static void     gs_watcher_class_init (GSWatcherClass *klass);
static void     gs_watcher_init       (GSWatcher      *watcher);
static void     gs_watcher_finalize   (GObject        *object);

static void     initialize_server_extensions (GSWatcher *watcher);

static gboolean start_idle_watcher           (GSWatcher      *watcher);
static gboolean stop_idle_watcher            (GSWatcher      *watcher);
static gboolean check_pointer_timer          (GSWatcher      *watcher);
static gboolean xevent_idle                  (GSWatcher      *watcher);
static gboolean watchdog_timer               (GSWatcher      *watcher);
static void     schedule_wakeup_event        (GSWatcher      *watcher,
                                              int             when,
                                              gboolean        verbose);
static void     notice_events                (Window          window,
                                              gboolean        top,
                                              gboolean        debug);
static gboolean monitor_powered_on           (void);
static void     monitor_power_on             (void);

#define GS_WATCHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WATCHER, GSWatcherPrivate))

struct GSWatcherPrivate
{
        /* settings */
        guint           verbose : 1;
        guint           debug : 1;
        guint           timeout;
        guint           pointer_timeout;
        guint           use_xidle_extension : 1;
        guint           use_sgi_saver_extension : 1;
        guint           use_mit_saver_extension : 1;
        guint           use_proc_interrupts : 1;

        /* state */
        int             poll_mouse_last_root_x;
        int             poll_mouse_last_root_y;
        GdkModifierType poll_mouse_last_mask;
        GdkScreen      *last_activity_screen;
        time_t          last_wall_clock_time;
        time_t          last_activity_time;
        time_t          dispatch_time;
        guint           emergency_lock : 1;

        guint           xevent_idle_id;
        guint           timer_id;
        guint           check_pointer_timer_id;
        guint           watchdog_timer_id;

        guint           xinerama : 1;
        guint           using_xidle_extension : 1;
        guint           using_sgi_saver_extension : 1;
        guint           using_mit_saver_extension : 1;
        guint           using_proc_interrupts : 1;

# ifdef HAVE_MIT_SAVER_EXTENSION
        int             mit_saver_ext_event_number;
        int             mit_saver_ext_error_number;
# endif
# ifdef HAVE_SGI_SAVER_EXTENSION
        int            sgi_saver_ext_event_number;
        int            sgi_saver_ext_error_number;
# endif

        gboolean       dpms_enabled;
        guint          dpms_standby;
        guint          dpms_suspend;
        guint          dpms_off;
};

enum {
        IDLE,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_TIMEOUT
};

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSWatcher, gs_watcher, G_TYPE_OBJECT);

void
gs_watcher_reset (GSWatcher *watcher)
{
        g_return_if_fail (GS_IS_WATCHER (watcher));

        /* restart if necessary */
        if (watcher->priv->timer_id > 0) {
                gs_watcher_set_active (watcher, FALSE);
                gs_watcher_set_active (watcher, TRUE);
        }
}

void
gs_watcher_set_timeout (GSWatcher  *watcher,
                        guint       timeout)
{
        g_return_if_fail (GS_IS_WATCHER (watcher));

        watcher->priv->timeout = timeout;

        gs_watcher_reset (watcher);
}

void
gs_watcher_set_dpms (GSWatcher *watcher,
                     gboolean   enabled,
                     guint      standby,
                     guint      suspend,
                     guint      off)
{
        g_return_if_fail (GS_IS_WATCHER (watcher));

        watcher->priv->dpms_enabled = enabled;
        watcher->priv->dpms_standby = standby;
        watcher->priv->dpms_suspend = suspend;
        watcher->priv->dpms_off     = off;

        sync_server_dpms_settings (GDK_DISPLAY (),
                                   watcher->priv->dpms_enabled,
                                   watcher->priv->dpms_standby / 1000,
                                   watcher->priv->dpms_suspend / 1000,
                                   watcher->priv->dpms_off / 1000,
                                   watcher->priv->verbose);
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

        signals [IDLE] =
                g_signal_new ("idle",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWatcherClass, idle),
                              NULL,
                              NULL,
                              g_cclosure_marshal_BOOLEAN__FLAGS,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_INT);

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

static gboolean
start_idle_watcher (GSWatcher *watcher)
{
        g_return_val_if_fail (watcher != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        watcher->priv->last_wall_clock_time = 0;
        watcher->priv->last_activity_time = time (NULL);

        check_pointer_timer (watcher);

        if (watcher->priv->check_pointer_timer_id) {
                g_source_remove (watcher->priv->check_pointer_timer_id);
                watcher->priv->check_pointer_timer_id = 0;
        }
        watcher->priv->check_pointer_timer_id = g_timeout_add (watcher->priv->pointer_timeout,
                                                               (GSourceFunc)check_pointer_timer, watcher);

        notice_events (DefaultRootWindow (GDK_DISPLAY ()), TRUE, TRUE);

        if (watcher->priv->xevent_idle_id != 0) {
                g_source_remove (watcher->priv->xevent_idle_id);
                watcher->priv->xevent_idle_id = 0;
        }
        watcher->priv->xevent_idle_id = g_idle_add ((GSourceFunc)xevent_idle, watcher);

        watchdog_timer (watcher);

        return FALSE;
}

static gboolean
stop_idle_watcher (GSWatcher *watcher)
{
        g_return_val_if_fail (watcher != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

        watcher->priv->last_wall_clock_time = 0;
        watcher->priv->last_activity_time = time (NULL);
        watcher->priv->poll_mouse_last_root_x = -1;
        watcher->priv->poll_mouse_last_root_y = -1;

        if (watcher->priv->xevent_idle_id != 0) {
                g_source_remove (watcher->priv->xevent_idle_id);
                watcher->priv->xevent_idle_id = 0;
        }

        if (watcher->priv->timer_id != 0) {
                g_source_remove (watcher->priv->timer_id);
                watcher->priv->timer_id = 0;
        }

        if (watcher->priv->check_pointer_timer_id != 0) {
                g_source_remove (watcher->priv->check_pointer_timer_id);
                watcher->priv->check_pointer_timer_id = 0;
        }

        return FALSE;
}

void
gs_watcher_set_active (GSWatcher *watcher,
                       gboolean   active)
{
        if (!active) {
                stop_idle_watcher (watcher);
                if (watcher->priv->debug)
                        g_message ("Stopping idle watcher");
        } else {
                monitor_power_on ();
                start_idle_watcher (watcher);
                if (watcher->priv->debug)
                        g_message ("Starting idle watcher");
        }
}

static void
gs_watcher_init (GSWatcher *watcher)
{

        watcher->priv = GS_WATCHER_GET_PRIVATE (watcher);

        watcher->priv->timeout = 600000;
        watcher->priv->pointer_timeout = 5000;

        watcher->priv->verbose = FALSE;
        watcher->priv->debug = FALSE;
}

static void
gs_watcher_finalize (GObject *object)
{
        GSWatcher *watcher;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_WATCHER (object));

        watcher = GS_WATCHER (object);

        g_return_if_fail (watcher->priv != NULL);

        if (watcher->priv->watchdog_timer_id != 0) {
                g_source_remove (watcher->priv->watchdog_timer_id);
                watcher->priv->watchdog_timer_id = 0;
        }

        stop_idle_watcher (watcher);

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
                        g_message ("%SGI SCREEN_SAVER extension exists, but can't be initialized;\n"
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


/* Display Power Management System (DPMS.)

On XFree86 systems, "man xset" reports:

-dpms    The -dpms option disables DPMS (Energy Star) features.
+dpms    The +dpms option enables DPMS (Energy Star) features.

dpms flags...
The dpms option allows the DPMS (Energy Star)
parameters to be set.  The option can take up to three
numerical values, or the `force' flag followed by a
DPMS state.  The `force' flags forces the server to
immediately switch to the DPMS state specified.  The
DPMS state can be one of `standby', `suspend', or
`off'.  When numerical values are given, they set the
inactivity period before the three modes are activated.
The first value given is for the `standby' mode, the
second is for the `suspend' mode, and the third is for
the `off' mode.  Setting these values implicitly
enables the DPMS features.  A value of zero disables a
particular mode.

However, note that the implementation is more than a little bogus,
in that there is code in /usr/X11R6/lib/libXdpms.a to implement all
the usual server-extension-querying utilities -- but there are no
prototypes in any header file!  Thus, the prototypes here.  (The
stuff in X11/extensions/dpms.h and X11/extensions/dpmsstr.h define
the raw X protcol, they don't define the API to libXdpms.a.)

Some documentation:
Library:  ftp://ftp.x.org/pub/R6.4/xc/doc/specs/Xext/DPMSLib.ms
Protocol: ftp://ftp.x.org/pub/R6.4/xc/doc/specs/Xext/DPMS.ms
*/

#ifdef HAVE_DPMS_EXTENSION

#include <X11/Xproto.h>			/* for CARD16 */
#include <X11/extensions/dpms.h>
#include <X11/extensions/dpmsstr.h>

extern gboolean DPMSQueryExtension (Display *dpy, int *event_ret, int *error_ret);
extern gboolean DPMSCapable        (Display *dpy);
extern Status   DPMSForceLevel     (Display *dpy, CARD16 level);
extern Status   DPMSInfo           (Display *dpy, CARD16 *power_level, BOOL *state);

#if 0 /* others we don't use */
extern Status   DPMSGetVersion     (Display *dpy, int *major_ret, int *minor_ret);
extern Status   DPMSSetTimeouts    (Display *dpy,
                                    CARD16 standby, CARD16 suspend, CARD16 off);
extern gboolean DPMSGetTimeouts    (Display *dpy,
                                    CARD16 *standby, CARD16 *suspend, CARD16 *off);
extern Status   DPMSEnable         (Display *dpy);
extern Status   DPMSDisable        (Display *dpy);
#endif /* 0 */


static gboolean
monitor_powered_on (void)
{
        gboolean result;
        int      event_number, error_number;
        BOOL     onoff = FALSE;
        CARD16   state;

        if (!DPMSQueryExtension (GDK_DISPLAY (), &event_number, &error_number))
                /* Server doesn't know -- assume the watcher is on. */
                result = TRUE;

        else if (!DPMSCapable (GDK_DISPLAY ()))
                /* Server says the watcher doesn't do power management -- so it's on. */
                result = TRUE;

        else {
                DPMSInfo (GDK_DISPLAY (), &state, &onoff);
                if (!onoff)
                        /* Server says DPMS is disabled -- so the watcher is on. */
                        result = TRUE;
                else
                        switch (state) {
                        case DPMSModeOn:      result = TRUE;  break;  /* really on */
                        case DPMSModeStandby: result = FALSE; break;  /* kinda off */
                        case DPMSModeSuspend: result = FALSE; break;  /* pretty off */
                        case DPMSModeOff:     result = FALSE; break;  /* really off */
                        default:	      result = TRUE;  break;  /* protocol error? */
                        }
        }

        return result;
}

static void
monitor_power_on (void)
{
        if (! monitor_powered_on ()) {
                DPMSForceLevel (GDK_DISPLAY (), DPMSModeOn);
                XSync (GDK_DISPLAY (), FALSE);
                if (! monitor_powered_on ())
                        g_message ("DPMSForceLevel (dpy, DPMSModeOn) did not power the monitor on?");
        }
}

#else  /* HAVE_DPMS_EXTENSION */

static gboolean
monitor_powered_on (void) 
{
        return TRUE; 
}

static void
monitor_power_on (void)
{
        return; 
}

#endif /* !HAVE_DPMS_EXTENSION */


/* If any server extensions have been requested, try and initialize them.
   Issue warnings if requests can't be honored.
*/
static void
initialize_server_extensions (GSWatcher *watcher)
{
        gboolean server_has_xidle_extension     = FALSE;
        gboolean server_has_sgi_saver_extension = FALSE;
        gboolean server_has_mit_saver_extension = FALSE;
        gboolean system_has_proc_interrupts     = FALSE;

        const char *piwhy = 0;

        watcher->priv->using_xidle_extension     = watcher->priv->use_xidle_extension;
        watcher->priv->using_sgi_saver_extension = watcher->priv->use_sgi_saver_extension;
        watcher->priv->using_mit_saver_extension = watcher->priv->use_mit_saver_extension;
        watcher->priv->using_proc_interrupts     = watcher->priv->use_proc_interrupts;

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
#ifdef HAVE_PROC_INTERRUPTS
        system_has_proc_interrupts     = query_proc_interrupts_available (&piwhy);
#endif

        if (! server_has_xidle_extension)
                watcher->priv->using_xidle_extension = FALSE;
        else if (watcher->priv->verbose) {
                if (watcher->priv->using_xidle_extension)
                        g_message ("Using XIDLE extension.");
                else
                        g_message ("Not using server's XIDLE extension.");
                }

        if (! server_has_sgi_saver_extension)
                watcher->priv->using_sgi_saver_extension = FALSE;
        else if (watcher->priv->verbose) {
                if (watcher->priv->using_sgi_saver_extension)
                        g_message ("Using SGI SCREEN_SAVER extension.");
                else
                        g_message ("Not using server's SGI SCREEN_SAVER extension.");
        }

        if (! server_has_mit_saver_extension)
                watcher->priv->using_mit_saver_extension = FALSE;
        else if (watcher->priv->verbose) {
                if (watcher->priv->using_mit_saver_extension)
                        g_message ("Using MIT-SCREEN-SAVER extension.");
                else
                        g_message ("Not using server's MIT-SCREEN-SAVER extension.");
        }

        /* These are incompatible (or at least, our support for them is...) */
        if (watcher->priv->xinerama && watcher->priv->using_mit_saver_extension) {
                watcher->priv->using_mit_saver_extension = FALSE;
                if (watcher->priv->verbose)
                        g_message ("Xinerama in use: disabling MIT-SCREEN-SAVER.");
        }

        if (! system_has_proc_interrupts) {

                watcher->priv->using_proc_interrupts = FALSE;
                if (watcher->priv->verbose && piwhy)
                        g_message ("Not using /proc/interrupts: %s.", piwhy);

        } else if (watcher->priv->verbose) {

                if (watcher->priv->using_proc_interrupts)
                        g_message ("Consulting /proc/interrupts for keyboard activity.");
                else
                        g_message ("Not consulting /proc/interrupts for keyboard activity.");
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

        XGetScreenSaver (GDK_DISPLAY (), &current_server_timeout, &current_server_interval,
                         &current_prefer_blank, &current_allow_exp);

        desired_server_timeout  = current_server_timeout;
        desired_server_interval = current_server_interval;
        desired_prefer_blank    = current_prefer_blank;
        desired_allow_exp       = current_allow_exp;

        /* On SGIs, if interval is non-zero, it is the number of seconds after
           screen saving starts at which the watcher should be powered down.
           Obviously I don't want that, so set it to 0 (meaning "never".)

           Power saving is disabled if DontPreferBlanking, but in that case,
           we don't get extension events either.  So we can't turn it off that way.

           Note: if you're running Irix 6.3 (O2), you may find that your watcher is
           powering down anyway, regardless of the xset settings.  This is fixed by
           installing SGI patches 2447 and 2537.
        */
        desired_server_interval = 0;

        /* I suspect (but am not sure) that DontAllowExposures might have
           something to do with powering off the watcher as well, at least
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
 
               if (watcher->priv->verbose)
                        g_message ("disabling server builtin screensaver:"
                                   " (xset s %d %d; xset s %s; xset s %s)",
                                   desired_server_timeout, desired_server_interval,
                                   (desired_prefer_blank ? "blank" : "noblank"),
                                   (desired_allow_exp ? "expose" : "noexpose"));

                XSetScreenSaver (GDK_DISPLAY (),
                                 desired_server_timeout, desired_server_interval,
                                 desired_prefer_blank, desired_allow_exp);

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
        int      idle;
        gboolean do_signal = FALSE;

        idle = 1000 * (time (NULL) - watcher->priv->last_activity_time);

        if (idle >= watcher->priv->timeout) {
                /* Look, we've been idle long enough.  We're done. */
                do_signal = TRUE;
        } else if (watcher->priv->emergency_lock) {
                /* Oops, the wall clock has jumped far into the future, so
                   we need to lock down in a hurry! */
                do_signal = TRUE;
        } else {
                /* The event went off, but it turns out that the user has not
                   yet been idle for long enough.  So re-signal the event.
                   Be economical: if we should blank after 5 minutes, and the
                   user has been idle for 2 minutes, then set this timer to
                   go off in 3 minutes.
                */
                if (polling_for_idleness)
                        schedule_wakeup_event (watcher, watcher->priv->timeout - idle, watcher->priv->debug);
                do_signal = FALSE;
        }

        if (do_signal) {
		gboolean res = FALSE;

                g_signal_emit (watcher, signals [IDLE], 0, 0, &res);

                /* if the event wasn't handled then schedule another timer */
                if (! res) {
                        if (polling_for_idleness)
                                schedule_wakeup_event (watcher, watcher->priv->timeout, watcher->priv->debug);
                }
        }
}

static gboolean
idle_timer (GSWatcher *watcher)
{
        /* try one last time */
        check_pointer_timer (watcher);

        watcher->priv->timer_id = 0;

        maybe_send_signal (watcher);

        return FALSE;
}

static void
schedule_wakeup_event (GSWatcher *watcher,
                       int        when,
                       gboolean   verbose)
{
        if (watcher->priv->timer_id) {
                if (verbose)
                        g_message ("idle_timer already running");
                return;
        }

        /* Wake up periodically to ask the server if we are idle. */
        watcher->priv->timer_id = g_timeout_add (when, (GSourceFunc)idle_timer, watcher);


        if (verbose)
                g_message ("starting idle_timer (%d, %d)", when, watcher->priv->timer_id);
}

/* Call this when user activity (or "simulated" activity) has been noticed.
 */
static void
reset_timers (GSWatcher *watcher)
{

        if (watcher->priv->using_mit_saver_extension || watcher->priv->using_sgi_saver_extension)
                return;

        if (watcher->priv->timer_id) {
                if (watcher->priv->debug)
                        g_message ("killing idle_timer  (%d, %d)", watcher->priv->timeout, watcher->priv->timer_id);
                g_source_remove (watcher->priv->timer_id);
                watcher->priv->timer_id = 0;
        }

        schedule_wakeup_event (watcher, watcher->priv->timeout, watcher->priv->debug);

        watcher->priv->last_activity_time = time (NULL);
}


/* An unfortunate situation is this: the saver is not active, because the
   user has been typing.  The machine is a laptop.  The user closes the lid
   and suspends it.  The CPU halts.  Some hours later, the user opens the
   lid.  At this point, Xt's timers will fire, and xscreensaver will blank
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
        time_t now = time ((time_t *) 0);
        long shift = now - watcher->priv->last_wall_clock_time;

        if (watcher->priv->debug) {
                int i = (watcher->priv->last_wall_clock_time == 0 ? 0 : shift);
                g_message ("checking wall clock for hibernation (%d:%02d:%02d).",
                           (i / (60 * 60)), ((i / 60) % 60), (i % 60));
        }

        if (watcher->priv->last_wall_clock_time != 0 && shift > (watcher->priv->timeout / 1000)) {
                if (watcher->priv->verbose)
                        g_message ("wall clock has jumped by %ld:%02ld:%02ld!",
                                   (shift / (60 * 60)), ((shift / 60) % 60), (shift % 60));

                watcher->priv->emergency_lock = TRUE;
                maybe_send_signal (watcher);
        }

        watcher->priv->last_wall_clock_time = now;
}

/* When we aren't using a server extension, this timer is used to periodically
   wake up and poll the mouse position, which is possibly more reliable than
   selecting motion events on every window.
*/
static gboolean
check_pointer_timer (GSWatcher *watcher)
{
        gboolean        active = FALSE;
        GdkDisplay     *display;
        GdkScreen      *screen;
        int             n_screens, root_x, root_y;
        GdkModifierType mask;

        if (! watcher->priv->using_proc_interrupts
            && (watcher->priv->using_xidle_extension
                || watcher->priv->using_mit_saver_extension
                || watcher->priv->using_sgi_saver_extension))
                /* If an extension is in use, we should not be polling the mouse.
                   Unless we're also checking /proc/interrupts, in which case, we should.
                */
                abort ();

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);

        gdk_display_get_pointer (display,
                                 &screen,
                                 &root_x,
                                 &root_y,
                                 &mask);

        if (root_x == watcher->priv->poll_mouse_last_root_x
            && root_y == watcher->priv->poll_mouse_last_root_y
            && screen == watcher->priv->last_activity_screen
            && mask   == watcher->priv->poll_mouse_last_mask)
                active = FALSE;
        else
                active = TRUE;

        if (active) {
                watcher->priv->last_activity_screen = screen;
                watcher->priv->poll_mouse_last_root_x = root_x;
                watcher->priv->poll_mouse_last_root_y = root_y;
                watcher->priv->poll_mouse_last_mask   = mask;

                reset_timers (watcher);
        }

        if (watcher->priv->debug)
                g_message ("Idle %d seconds", (int)(time (NULL) - watcher->priv->last_activity_time));


        check_for_clock_skew (watcher);

        return TRUE;
}

static void
notice_events_inner (Window   window,
                     gboolean top,
                     gboolean debug)
{
        XWindowAttributes attrs;
        unsigned long     events;
        Window            root, parent, *kids;
        unsigned int      nkids;
        GdkWindow        *gwindow;

        gwindow = gdk_window_lookup (window);
        if (gwindow && (window != GDK_ROOT_WINDOW ())) {
                /* If it's one of ours, don't mess up its event mask. */
                return;
        }

        if (! XQueryTree (GDK_DISPLAY (), window, &root, &parent, &kids, &nkids))
                return;

        if (window == root)
                top = FALSE;

        XGetWindowAttributes (GDK_DISPLAY (), window, &attrs);
        events = ((attrs.all_event_masks | attrs.do_not_propagate_mask) & KeyPressMask);

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
        XSelectInput (GDK_DISPLAY (), window, SubstructureNotifyMask | events);

        if (top && debug && (events & KeyPressMask)) {
                /* Only mention one window per tree (hack hack). */
                top = FALSE;
        }

        if (kids) {
                while (nkids)
                        notice_events_inner (kids [--nkids], top, debug);
                XFree ((char *) kids);
        }
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
notice_events (Window   window,
               gboolean top,
               gboolean debug)
{
        XErrorHandler old_handler;

        old_handler = XSetErrorHandler (BadWindow_ehandler);

        notice_events_inner (window, top, debug);
        XSync (GDK_DISPLAY (), FALSE);
        XSetErrorHandler (old_handler);
}

static gboolean
notice_events_timer (Window w)
{
        notice_events (w, TRUE, TRUE);

        return FALSE;
}

static gboolean
if_event_predicate (Display *dpy,
                    XEvent  *ev,
                    XPointer arg)
{
        GSWatcher *watcher = GS_WATCHER (arg);

        switch (ev->xany.type) {
        case KeyPress:
        case KeyRelease:
        case ButtonPress:
        case ButtonRelease:
                watcher->priv->last_activity_time = watcher->priv->dispatch_time;
                break;
        case ClientMessage:
                /*handle_clientmessage (watcher, &event);*/
                break;
        case CreateNotify:
                {
                        Window w = ev->xcreatewindow.window;
                        notice_events_timer (w);
                }
                break;
        default:
                break;
        }

        return FALSE;
}

static gboolean
xevent_idle (GSWatcher *watcher)
{
        fd_set         rfds;
        struct timeval tv;
        int            fd;
        int            retval;
        XEvent         event;
 
        FD_ZERO (&rfds);
        fd = ConnectionNumber (GDK_DISPLAY ());

        FD_SET (fd, &rfds);

        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        retval = select (fd + 1, &rfds, NULL, NULL, &tv);
                
        if ((0 > retval) && (EAGAIN != errno) && (EINTR != errno)) {
                g_warning ("Fatal error in select %d: %s", errno, strerror (errno));
                watcher->priv->xevent_idle_id = 0;
                gnome_screensaver_quit ();
                return FALSE;
        }
                
        watcher->priv->dispatch_time = time (NULL);
        XCheckIfEvent (GDK_DISPLAY (), &event, if_event_predicate, (XPointer) watcher);

        return TRUE;
}

/* This timer goes off every few minutes, whether the user is idle or not,
   to try and clean up anything that has gone wrong.

   It calls disable_builtin_screensaver() so that if xset has been used,
   or some other program (like xlock) has messed with the XSetScreenSaver()
   settings, they will be set back to sensible values (if a server extension
   is in use, messing with xlock can cause xscreensaver to never get a wakeup
   event, and could cause watcher power-saving to occur, and all manner of
   heinousness.)

   If the screen is currently blanked, it raises the window, in case some
   other window has been mapped on top of it.

   If the screen is currently blanked, and there is no hack running, it
   clears the window, in case there is an error message printed on it (we
   don't want the error message to burn in.)
 */

static gboolean
watchdog_timer (GSWatcher *watcher)
{

        disable_builtin_screensaver (watcher, FALSE);

        /* If the DPMS settings on the server have changed, change them back to
           configuration says they should be. */
        sync_server_dpms_settings (GDK_DISPLAY (),
                                   watcher->priv->dpms_enabled,
                                   watcher->priv->dpms_standby / 1000,
                                   watcher->priv->dpms_suspend / 1000,
                                   watcher->priv->dpms_off / 1000,
                                   watcher->priv->verbose);

        return TRUE;
}

GSWatcher *
gs_watcher_new (guint timeout)
{
        GSWatcher *watcher;

        watcher = g_object_new (GS_TYPE_WATCHER,
                                "timeout", timeout, NULL);

        initialize_server_extensions (watcher);

        disable_builtin_screensaver (watcher, FALSE);

        start_idle_watcher (watcher);

        watcher->priv->watchdog_timer_id = g_timeout_add (600000, (GSourceFunc)watchdog_timer, watcher);

        return GS_WATCHER (watcher);
}
