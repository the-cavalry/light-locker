/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1992-2003 Jamie Zawinski <jwz@jwz.org>
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "fade.h"

static void fade_screens_default (int       seconds,
                                  int       ticks,
                                  gboolean  go_out,
                                  GSList   *windows);

#ifdef HAVE_XF86VMODE_GAMMA
static int xf86_gamma_fade (int      seconds,
                            int      ticks,
                            gboolean go_out,
                            GSList  *windows);
#endif /* HAVE_XF86VMODE_GAMMA */

static void
show_windows (GSList *windows)
{
        GSList *l;

        for (l = windows; l; l = l->next) {
                gtk_widget_show_now (GTK_WIDGET (l->data));
        }
}

void
fade_screens_and_show (int       seconds,
                       int       ticks,
                       gboolean  out,
                       GSList   *windows)
{
        int      oseconds = seconds;
        gboolean was_in = ! out;
        int      res;

        /* When we're asked to fade in, first fade out, then fade in.
           That way all the transitions are smooth -- from what's on the
           screen, to black, to the desktop.
        */
        if (was_in) {
                out = TRUE;
                seconds /= 3;
                if (seconds == 0)
                        seconds = 1;
        }

 AGAIN:

        res = -1;

#ifdef HAVE_XF86VMODE_GAMMA
        /* Then try to do it by fading the gamma in an XFree86-specific way... */
        res = xf86_gamma_fade (seconds, ticks, out, windows);
#endif /* HAVE_XF86VMODE_GAMMA */

        if (res != 0) {
                /* Else, do it the old-fashioned way, which (somewhat) loses if
                   there are TrueColor windows visible. */
                fade_screens_default (seconds, ticks, out, windows);
        }

        /* If we were supposed to be fading in, do so now (we just faded out,
           so now fade back in.)
        */
        if (was_in) {
                was_in = FALSE;
                out = FALSE;
                seconds = oseconds * 2 / 3;
                if (seconds == 0)
                        seconds = 1;

                goto AGAIN;
        }
}


static void
fade_screens_default (int       seconds,
                      int       ticks,
                      gboolean  go_out,
                      GSList   *windows)
{
        /* FIXME: fade using cairo ? */

        /* Show windows */
        show_windows (windows);
}


/* XFree86 4.x+ Gamma fading */

#ifdef HAVE_XF86VMODE_GAMMA

#include <X11/extensions/xf86vmode.h>

typedef struct {
        XF86VidModeGamma vmg;
        int              size;
        unsigned short  *r;
        unsigned short  *g;
        unsigned short  *b;
} xf86_gamma_info;

static int      xf86_check_gamma_extension (void);
static gboolean xf86_whack_gamma (int              screen,
                                  xf86_gamma_info *ginfo,
                                  float            ratio);

static int
xf86_gamma_fade (int      seconds,
                 int      ticks,
                 gboolean go_out,
                 GSList  *windows)
{
        int              steps = seconds * ticks;
        long             usecs_per_step = (long)(seconds * 1000000) / (long)steps;
        XEvent           dummy_event;
        GTimeVal         then;
        GTimeVal         now;
        int              i, screen;
        int              status = -1;
        int              nscreens;
        xf86_gamma_info *info = NULL;
        GdkDisplay      *display;

        static int       ext_ok = -1;

        display = gdk_display_get_default ();
        nscreens = gdk_display_get_n_screens (display);

        /* Only probe the extension once: the answer isn't going to change. */
        if (ext_ok == -1)
                ext_ok = xf86_check_gamma_extension ();

        /* If this server doesn't have the gamma extension, bug out. */
        if (ext_ok == 0)
                goto FAIL;

# ifndef HAVE_XF86VMODE_GAMMA_RAMP
        if (ext_ok == 2)
                ext_ok = 1;  /* server is newer than client! */
# endif

        info = g_new0 (xf86_gamma_info, nscreens);

        /* Get the current gamma maps for all screens.
           Bug out and return -1 if we can't get them for some screen.
        */
        for (screen = 0; screen < nscreens; screen++) {
                if (ext_ok == 1) {  /* only have gamma parameter, not ramps. */

                        if (! XF86VidModeGetGamma (GDK_DISPLAY (), screen, &info [screen].vmg))
                                goto FAIL;
                }

# ifdef HAVE_XF86VMODE_GAMMA_RAMP
                else if (ext_ok == 2) {  /* have ramps */

                        if (! XF86VidModeGetGammaRampSize (GDK_DISPLAY (), screen, &info [screen].size))
                                goto FAIL;
                        if (info [screen].size <= 0)
                                goto FAIL;

                        info [screen].r = (unsigned short *)
                                calloc (info[screen].size, sizeof (unsigned short));
                        info [screen].g = (unsigned short *)
                                calloc (info[screen].size, sizeof (unsigned short));
                        info [screen].b = (unsigned short *)
                                calloc (info[screen].size, sizeof (unsigned short));

                        if (! (info [screen].r && info [screen].g && info [screen].b))
                                goto FAIL;

                        if (! XF86VidModeGetGammaRamp (GDK_DISPLAY (),
                                                       screen,
                                                       info [screen].size,
                                                       info [screen].r,
                                                       info [screen].g,
                                                       info [screen].b))
                                goto FAIL;
                }
# endif /* HAVE_XF86VMODE_GAMMA_RAMP */
                else {
                        abort ();
                }
        }

        g_get_current_time (&then);

        /* If we're fading in (from black), then first crank the gamma all the
           way down to 0, then take the windows off the screen.
        */
        if (! go_out) {
                for (screen = 0; screen < nscreens; screen++)
                        xf86_whack_gamma (screen, &info [screen], 0.0);
        }

        /* Iterate by steps of the animation... */
        for (i = (go_out ? steps : 0); (go_out ? i > 0 : i < steps); (go_out ? i-- : i++)) {

                for (screen = 0; screen < nscreens; screen++) {

                        xf86_whack_gamma (screen, &info [screen], (((float)i) / ((float)steps)));

                        /* If there is user activity, bug out.  (Bug out on keypresses or
                           mouse presses, but not motion, and not release events.  Bugging
                           out on motion made the unfade hack be totally useless, I think.)

                           We put the event back so that the calling code can notice it too.
                        */
                        if (XCheckMaskEvent (GDK_DISPLAY (), (KeyPressMask | ButtonPressMask), &dummy_event)) {
                                XPutBackEvent (GDK_DISPLAY (), &dummy_event);
                                goto DONE;
                        }

                        g_get_current_time (&now);

                        /* If we haven't already used up our alotted time, sleep to avoid
                           changing the colormap too fast. */
                        {
                                long diff = (((now.tv_sec - then.tv_sec) * 1000000) +
                                             now.tv_usec - then.tv_usec);
                                then.tv_sec = now.tv_sec;
                                then.tv_usec = now.tv_usec;
                                if (usecs_per_step > diff)
                                        g_usleep (usecs_per_step - diff);
                        }
                }
        }
  

 DONE:

        /* Show windows */
        show_windows (windows);

        /* Without this delay we get a flicker.  According to JWZ: I
           suppose there's some lossage with stale bits being in the
           hardware frame buffer or something, and this delay gives it
           time to flush out.
        */
        g_usleep (100000);  /* 1/10th second */

        for (screen = 0; screen < nscreens; screen++) {
                xf86_whack_gamma (screen, &info [screen], 1.0);
        }

        gdk_flush ();

        status = 0;

 FAIL:
        if (info) {
                for (screen = 0; screen < nscreens; screen++) {
                        if (info [screen].r)
                                free (info[screen].r);
                        if (info [screen].g)
                                free (info[screen].g);
                        if (info [screen].b)
                                free (info[screen].b);
                }

                g_free (info);
        }

        return status;
}


/* This is needed because the VidMode extension doesn't work
   on remote displays -- but if the remote display has the extension
   at all, XF86VidModeQueryExtension returns true, and then
   XF86VidModeQueryVersion dies with an X error.
*/

static Bool error_handler_hit = False;

static int
ignore_all_errors_ehandler (Display     *dpy,
                            XErrorEvent *error)
{
        error_handler_hit = True;

        return 0;
}

static Bool
safe_XF86VidModeQueryVersion (Display *dpy,
                              int     *majP,
                              int     *minP)
{
        Bool          result;
        XErrorHandler old_handler;

        XSync (dpy, False);
        error_handler_hit = False;
        old_handler = XSetErrorHandler (ignore_all_errors_ehandler);

        result = XF86VidModeQueryVersion (dpy, majP, minP);

        XSync (dpy, False);
        XSetErrorHandler (old_handler);
        XSync (dpy, False);

        return (error_handler_hit
                ? False
                : result);
}

/* VidModeExtension version 2.0 or better is needed to do gamma.
   2.0 added gamma values; 2.1 added gamma ramps.
*/
# define XF86_VIDMODE_GAMMA_MIN_MAJOR 2
# define XF86_VIDMODE_GAMMA_MIN_MINOR 0
# define XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR 2
# define XF86_VIDMODE_GAMMA_RAMP_MIN_MINOR 1

/* Returns 0 if gamma fading not available; 1 if only gamma value setting
   is available; 2 if gamma ramps are available.
*/
static int
xf86_check_gamma_extension (void)
{
        int event, error, major, minor;

        if (! XF86VidModeQueryExtension (GDK_DISPLAY (), &event, &error))
                return 0;  /* display doesn't have the extension. */

        if (! safe_XF86VidModeQueryVersion (GDK_DISPLAY (), &major, &minor))
                return 0;  /* unable to get version number? */

        if (major < XF86_VIDMODE_GAMMA_MIN_MAJOR || 
            (major == XF86_VIDMODE_GAMMA_MIN_MAJOR &&
             minor < XF86_VIDMODE_GAMMA_MIN_MINOR))
                return 0;  /* extension is too old for gamma. */

        if (major < XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR || 
            (major == XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR &&
             minor < XF86_VIDMODE_GAMMA_RAMP_MIN_MINOR))
                return 1;  /* extension is too old for gamma ramps. */

        /* Copacetic */
        return 2;
}


/* XFree doesn't let you set gamma to a value smaller than this.
   Apparently they didn't anticipate the trick I'm doing here...
*/
#define XF86_MIN_GAMMA  0.1

static gboolean
xf86_whack_gamma (int              screen,
                  xf86_gamma_info *info,
                  float            ratio)
{
        Bool status;

        if (ratio < 0)
                ratio = 0;
        if (ratio > 1)
                ratio = 1;

        if (info->size == 0) {
                /* we only have a gamma number, not a ramp. */

                XF86VidModeGamma g2;

                g2.red   = info->vmg.red   * ratio;
                g2.green = info->vmg.green * ratio;
                g2.blue  = info->vmg.blue  * ratio;

# ifdef XF86_MIN_GAMMA
                if (g2.red < XF86_MIN_GAMMA)
                        g2.red = XF86_MIN_GAMMA;
                if (g2.green < XF86_MIN_GAMMA)
                        g2.green = XF86_MIN_GAMMA;
                if (g2.blue < XF86_MIN_GAMMA)
                        g2.blue = XF86_MIN_GAMMA;
# endif

                status = XF86VidModeSetGamma (GDK_DISPLAY (), screen, &g2);
        } else {

# ifdef HAVE_XF86VMODE_GAMMA_RAMP
                unsigned short *r, *g, *b;
                int i;

                r = (unsigned short *) malloc (info->size * sizeof (unsigned short));
                g = (unsigned short *) malloc (info->size * sizeof (unsigned short));
                b = (unsigned short *) malloc (info->size * sizeof (unsigned short));

                for (i = 0; i < info->size; i++) {
                        r[i] = info->r[i] * ratio;
                        g[i] = info->g[i] * ratio;
                        b[i] = info->b[i] * ratio;
                }

                status = XF86VidModeSetGammaRamp (GDK_DISPLAY (), screen, info->size, r, g, b);

                free (r);
                free (g);
                free (b);

# else  /* !HAVE_XF86VMODE_GAMMA_RAMP */
                abort ();
# endif /* !HAVE_XF86VMODE_GAMMA_RAMP */
        }

        gdk_flush ();

        return status;
}

#endif /* HAVE_XF86VMODE_GAMMA */
