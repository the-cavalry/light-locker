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
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-fade.h"
#include "gs-debug.h"

/* XFree86 4.x+ Gamma fading */

#ifdef HAVE_XF86VMODE_GAMMA

#include <X11/extensions/xf86vmode.h>

#define XF86_MIN_GAMMA  0.1

typedef struct {
        XF86VidModeGamma vmg;
        int              size;
        unsigned short  *r;
        unsigned short  *g;
        unsigned short  *b;
} xf86_gamma_info;

#endif /* HAVE_XF86VMODE_GAMMA */

static void     gs_fade_class_init (GSFadeClass *klass);
static void     gs_fade_init       (GSFade      *fade);
static void     gs_fade_finalize   (GObject        *object);

#define GS_FADE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_FADE, GSFadePrivate))

struct GSFadePrivate
{
        guint            enabled : 1;
        guint            active : 1;

        guint            timeout;

        guint            step;
        guint            num_steps;
        guint            timer_id;

        gdouble          alpha_per_iter;
        gdouble          current_alpha;

        int              fade_type;

        int              num_screens;

#ifdef HAVE_XF86VMODE_GAMMA
        xf86_gamma_info *gamma_info;
#endif /* HAVE_XF86VMODE_GAMMA */

};

enum {
        FADED,
        LAST_SIGNAL
};

enum {
        FADE_TYPE_NONE,
        FADE_TYPE_GAMMA_NUMBER,
        FADE_TYPE_GAMMA_RAMP
};

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSFade, gs_fade, G_TYPE_OBJECT)

static gpointer fade_object = NULL;

#ifdef HAVE_XF86VMODE_GAMMA

/* This is needed because the VidMode extension doesn't work
   on remote displays -- but if the remote display has the extension
   at all, XF86VidModeQueryExtension returns true, and then
   XF86VidModeQueryVersion dies with an X error.
*/

static gboolean error_handler_hit = FALSE;

static int
ignore_all_errors_ehandler (Display     *dpy,
                            XErrorEvent *error)
{
        error_handler_hit = TRUE;

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
        error_handler_hit = FALSE;
        old_handler = XSetErrorHandler (ignore_all_errors_ehandler);

        result = XF86VidModeQueryVersion (dpy, majP, minP);

        XSync (dpy, False);
        XSetErrorHandler (old_handler);
        XSync (dpy, False);

        return (error_handler_hit
                ? False
                : result);
}

static gboolean
xf86_whack_gamma (int              screen,
                  xf86_gamma_info *info,
                  float            ratio)
{
        Bool status;

        if (ratio < 0) {
                ratio = 0;
        }
        if (ratio > 1) {
                ratio = 1;
        }

        if (info->size == 0) {
                /* we only have a gamma number, not a ramp. */

                XF86VidModeGamma g2;

                g2.red   = info->vmg.red   * ratio;
                g2.green = info->vmg.green * ratio;
                g2.blue  = info->vmg.blue  * ratio;

                if (g2.red < XF86_MIN_GAMMA) {
                        g2.red = XF86_MIN_GAMMA;
                }
                if (g2.green < XF86_MIN_GAMMA) {
                        g2.green = XF86_MIN_GAMMA;
                }
                if (g2.blue < XF86_MIN_GAMMA) {
                        g2.blue = XF86_MIN_GAMMA;
                }

                status = XF86VidModeSetGamma (GDK_DISPLAY (), screen, &g2);
        } else {

# ifdef HAVE_XF86VMODE_GAMMA_RAMP
                unsigned short *r, *g, *b;
                int i;

                r = g_new0 (unsigned short, info->size);
                g = g_new0 (unsigned short, info->size);
                b = g_new0 (unsigned short, info->size);

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

/* VidModeExtension version 2.0 or better is needed to do gamma.
   2.0 added gamma values; 2.1 added gamma ramps.
*/
# define XF86_VIDMODE_GAMMA_MIN_MAJOR 2
# define XF86_VIDMODE_GAMMA_MIN_MINOR 0
# define XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR 2
# define XF86_VIDMODE_GAMMA_RAMP_MIN_MINOR 1

static int
check_gamma_extension (void)
{
#ifdef HAVE_XF86VMODE_GAMMA
        int      event;
        int      error;
        int      major;
        int      minor;
        gboolean res;

        res = XF86VidModeQueryExtension (GDK_DISPLAY (), &event, &error);
        if (! res) {
                return FADE_TYPE_NONE;  /* display doesn't have the extension. */
        }

        res = safe_XF86VidModeQueryVersion (GDK_DISPLAY (), &major, &minor);
        if (! res) {
                return FADE_TYPE_NONE;  /* unable to get version number? */
        }

        if (major < XF86_VIDMODE_GAMMA_MIN_MAJOR ||
            (major == XF86_VIDMODE_GAMMA_MIN_MAJOR &&
             minor < XF86_VIDMODE_GAMMA_MIN_MINOR)) {
                return FADE_TYPE_NONE;  /* extension is too old for gamma. */
        }

        if (major < XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR ||
            (major == XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR &&
             minor < XF86_VIDMODE_GAMMA_RAMP_MIN_MINOR)) {
                return FADE_TYPE_GAMMA_NUMBER;  /* extension is too old for gamma ramps. */
        }

        /* Copacetic */
        return FADE_TYPE_GAMMA_RAMP;
#else
        return FADE_TYPE_NONE;
#endif /* HAVE_XF86VMODE_GAMMA */
}

gboolean
gs_fade_get_enabled (GSFade *fade)
{
        g_return_val_if_fail (GS_IS_FADE (fade), FALSE);

        return fade->priv->enabled;
}

void
gs_fade_set_enabled (GSFade  *fade,
                     gboolean enabled)
{
        g_return_if_fail (GS_IS_FADE (fade));

        if (fade->priv->enabled != enabled) {
                fade->priv->enabled = enabled;
        }
}

static gboolean
gamma_info_init (GSFade *fade)
{
#ifdef HAVE_XF86VMODE_GAMMA
        int              screen;
        xf86_gamma_info *info;
        gboolean         res;

# ifndef HAVE_XF86VMODE_GAMMA_RAMP
        if (FADE_TYPE_GAMMA_RAMP == fade->priv->fade_type) {
                /* server is newer than client! */
                fade->priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
        }
# endif

        if (fade->priv->gamma_info != NULL) {
                return TRUE;
        }

        info = g_new0 (xf86_gamma_info, fade->priv->num_screens);
        fade->priv->gamma_info = info;

        /* Get the current gamma maps for all screens.
           Bug out and return -1 if we can't get them for some screen.
        */
        for (screen = 0; screen < fade->priv->num_screens; screen++) {

# ifdef HAVE_XF86VMODE_GAMMA_RAMP

                if (FADE_TYPE_GAMMA_RAMP == fade->priv->fade_type) {
                        /* have ramps */

                        res = XF86VidModeGetGammaRampSize (GDK_DISPLAY (), screen, &info [screen].size);
                        if (! res || info [screen].size <= 0) {
                                fade->priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                                goto test_number;
                        }

                        info [screen].r = g_new0 (unsigned short, info[screen].size);
                        info [screen].g = g_new0 (unsigned short, info[screen].size);
                        info [screen].b = g_new0 (unsigned short, info[screen].size);

                        if (! (info [screen].r && info [screen].g && info [screen].b)) {
                                fade->priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                                goto test_number;
                        }

                        res = XF86VidModeGetGammaRamp (GDK_DISPLAY (),
                                                       screen,
                                                       info [screen].size,
                                                       info [screen].r,
                                                       info [screen].g,
                                                       info [screen].b);
                        if (! res) {
                                fade->priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                                goto test_number;
                        }
                        gs_debug ("Initialized gamma ramp fade");
                }
# endif /* HAVE_XF86VMODE_GAMMA_RAMP */

 test_number:
                if (FADE_TYPE_GAMMA_NUMBER == fade->priv->fade_type) {
                        /* only have gamma parameter, not ramps. */

                        res = XF86VidModeGetGamma (GDK_DISPLAY (), screen, &info [screen].vmg);
                        if (! res) {
                                fade->priv->fade_type = FADE_TYPE_NONE;
                                goto test_none;
                        }
                        gs_debug ("Initialized gamma fade for screen %d: %f %f %f",
                                  screen,
                                  info [screen].vmg.red,
                                  info [screen].vmg.green,
                                  info [screen].vmg.blue);
                }

 test_none:
                if (FADE_TYPE_NONE == fade->priv->fade_type) {
                        goto FAIL;
                }
        }

        return TRUE;
 FAIL:

#endif /* HAVE_XF86VMODE_GAMMA */

return FALSE;
}

static void
gamma_info_free (GSFade *fade)
{
#ifdef HAVE_XF86VMODE_GAMMA

        if (fade->priv->gamma_info) {
                int screen;

                for (screen = 0; screen < fade->priv->num_screens; screen++) {
                        if (fade->priv->gamma_info [screen].r) {
                                g_free (fade->priv->gamma_info[screen].r);
                        }
                        if (fade->priv->gamma_info [screen].g) {
                                g_free (fade->priv->gamma_info[screen].g);
                        }
                        if (fade->priv->gamma_info [screen].b) {
                                g_free (fade->priv->gamma_info[screen].b);
                        }
                }

                g_free (fade->priv->gamma_info);
                fade->priv->gamma_info = NULL;
        }

#endif /* HAVE_XF86VMODE_GAMMA */
}

static gboolean
gs_fade_set_alpha_gamma (GSFade *fade,
                         gdouble alpha)
{
#ifdef HAVE_XF86VMODE_GAMMA
        int      screen;
        gboolean res;

        if (fade->priv->gamma_info != NULL) {
                for (screen = 0; screen < fade->priv->num_screens; screen++) {
                        res = xf86_whack_gamma (screen, &fade->priv->gamma_info [screen], alpha);
                }
        }

        return TRUE;
#else
        return FALSE;
#endif /* HAVE_XF86VMODE_GAMMA */
}

static gboolean
gs_fade_set_alpha (GSFade *fade,
                   gdouble alpha)
{
        gboolean ret;

        switch (fade->priv->fade_type) {
        case FADE_TYPE_GAMMA_RAMP:
        case FADE_TYPE_GAMMA_NUMBER:
                ret = gs_fade_set_alpha_gamma (fade, alpha);
                break;
        case FADE_TYPE_NONE:
                ret = FALSE;
                break;
        default:
                g_warning ("Unknown fade type");
                ret = FALSE;
                break;
        }

        return ret;
}

static gboolean
gs_fade_out_iter (GSFade *fade)
{
        gboolean ret;

        if (fade->priv->current_alpha < 0.01) {
                return FALSE;
        }

        fade->priv->current_alpha -= fade->priv->alpha_per_iter;

        ret = gs_fade_set_alpha (fade, fade->priv->current_alpha);

        return ret;
}

static gboolean
gs_fade_stop (GSFade *fade)
{
        if (fade->priv->timer_id > 0) {
                g_source_remove (fade->priv->timer_id);
                fade->priv->timer_id = 0;
        }

        fade->priv->step = 0;
        fade->priv->active = FALSE;

        return TRUE;
}

void
gs_fade_finish (GSFade *fade)
{
        g_return_if_fail (GS_IS_FADE (fade));

        if (! fade->priv->active) {
                return;
        }

        gs_fade_stop (fade);

        g_signal_emit (fade, signals [FADED], 0);

        fade->priv->active = FALSE;
}

static gboolean
fade_out_timer (GSFade *fade)
{
        gboolean res;

        res = gs_fade_out_iter (fade);

        /* if failed then fade is complete */
        if (! res) {
                gs_fade_finish (fade);
                return FALSE;
        }

        return TRUE;
}

gboolean
gs_fade_get_active (GSFade *fade)
{
        g_return_val_if_fail (GS_IS_FADE (fade), FALSE);

        return fade->priv->active;
}

static void
gs_fade_set_timeout (GSFade   *fade,
                     guint     timeout)
{
        g_return_if_fail (GS_IS_FADE (fade));

        fade->priv->timeout = timeout;
}

static void
gs_fade_start (GSFade *fade,
               guint   timeout)
{
        guint steps_per_sec = 30;
        guint msecs_per_step;

        g_return_if_fail (GS_IS_FADE (fade));

        gamma_info_init (fade);

        if (fade->priv->timer_id > 0) {
                gs_fade_stop (fade);
        }

        fade->priv->active = TRUE;

        gs_fade_set_timeout (fade, timeout);

        if (fade->priv->fade_type != FADE_TYPE_NONE) {
                guint num_steps;

                num_steps = (fade->priv->timeout / 1000) * steps_per_sec;
                msecs_per_step = 1000 / steps_per_sec;
                fade->priv->alpha_per_iter = 1.0 / (gdouble)num_steps;

                fade->priv->timer_id = g_timeout_add (msecs_per_step, (GSourceFunc)fade_out_timer, fade);
        } else {
                gs_fade_finish (fade);
        }
}

typedef struct
{
        GSFadeDoneFunc done_cb;
        gpointer       data;
} FadedCallbackData;

static void
gs_fade_async_callback (GSFade            *fade,
                        FadedCallbackData *cdata)
{
        g_signal_handlers_disconnect_by_func (fade,
                                              gs_fade_async_callback, 
                                              cdata);

        if (cdata->done_cb) {
                cdata->done_cb (fade, cdata->data);
        }

        g_free (cdata);
}

void
gs_fade_async (GSFade        *fade,
               guint          timeout,
               GSFadeDoneFunc func,
               gpointer       data)
{
        g_return_if_fail (GS_IS_FADE (fade));

        /* if fade is active then pause it */
        if (fade->priv->active) {
                gs_fade_stop (fade);
        }

        if (func) {
                FadedCallbackData *cb_data;

                cb_data = g_new0 (FadedCallbackData, 1);
                cb_data->done_cb = func;
                cb_data->data = data;

                g_signal_connect (fade, "faded",
                                  G_CALLBACK (gs_fade_async_callback),
                                  cb_data);
        }

        gs_fade_start (fade, timeout);
}

static void
gs_fade_sync_callback (GSFade *fade,
                       int    *flag)
{
        *flag = TRUE;
        g_signal_handlers_disconnect_by_func (fade,
                                              gs_fade_sync_callback,
                                              flag);
}

void
gs_fade_sync (GSFade        *fade,
              guint          timeout)
{
        int      flag = FALSE;

        g_return_if_fail (GS_IS_FADE (fade));

        /* if fade is active then pause it */
        if (fade->priv->active) {
                gs_fade_stop (fade);
        }

        g_signal_connect (fade, "faded",
                          G_CALLBACK (gs_fade_sync_callback),
                          &flag);

        gs_fade_start (fade, timeout);

        while (! flag) {
                gtk_main_iteration ();
        }
}

void
gs_fade_reset (GSFade *fade)
{
        g_return_if_fail (GS_IS_FADE (fade));

        gs_debug ("Resetting fade");

        if (fade->priv->active) {
                gs_fade_stop (fade);
        }

        fade->priv->current_alpha = 1.0;

        gs_fade_set_alpha (fade, fade->priv->current_alpha);

        gamma_info_free (fade);
}

static void
gs_fade_class_init (GSFadeClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gs_fade_finalize;

        signals [FADED] =
                g_signal_new ("faded",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSFadeClass, faded),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0, G_TYPE_NONE);

        g_type_class_add_private (klass, sizeof (GSFadePrivate));
}

static void
gs_fade_init (GSFade *fade)
{
        GdkDisplay *display;

        fade->priv = GS_FADE_GET_PRIVATE (fade);

        fade->priv->timeout = 1000;
        fade->priv->current_alpha = 1.0;

        fade->priv->fade_type = check_gamma_extension ();

        gs_debug ("Fade type: %d", fade->priv->fade_type);

        display = gdk_display_get_default ();
        fade->priv->num_screens = gdk_display_get_n_screens (display);

#ifdef HAVE_XF86VMODE_GAMMA
        fade->priv->gamma_info = NULL;
#endif
}

static void
gs_fade_finalize (GObject *object)
{
        GSFade *fade;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_FADE (object));

        fade = GS_FADE (object);

        g_return_if_fail (fade->priv != NULL);

        gamma_info_free (fade);

        G_OBJECT_CLASS (gs_fade_parent_class)->finalize (object);
}

GSFade *
gs_fade_new (void)
{
        if (fade_object) {
                g_object_ref (fade_object);
        } else {
                fade_object = g_object_new (GS_TYPE_FADE, NULL);
                g_object_add_weak_pointer (fade_object,
                                           (gpointer *) &fade_object);
        }

        return GS_FADE (fade_object);
}
