/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2009 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2009      Red Hat, Inc.
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

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include "libgnomeui/gnome-rr.h"

/* XFree86 4.x+ Gamma fading */


#ifdef HAVE_XF86VMODE_GAMMA

#include <X11/extensions/xf86vmode.h>

#define XF86_MIN_GAMMA  0.1

#endif /* HAVE_XF86VMODE_GAMMA */

static void     gs_fade_class_init (GSFadeClass *klass);
static void     gs_fade_init       (GSFade      *fade);
static void     gs_fade_finalize   (GObject        *object);

#define GS_FADE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_FADE, GSFadePrivate))

struct GSGammaInfo {
        int              size;
        unsigned short  *r;
        unsigned short  *g;
        unsigned short  *b;
};

struct GSFadeScreenPrivate
{
        int                 fade_type;
        int                 num_ramps;
        /* one per crtc in randr mode */
        struct GSGammaInfo *info;
        /* one per screen in theory */
        GnomeRRScreen      *rrscreen;
#ifdef HAVE_XF86VMODE_GAMMA
        /* one per screen also */
        XF86VidModeGamma    vmg;
#endif /* HAVE_XF86VMODE_GAMMA */
        gboolean (*fade_setup)           (GSFade *fade,
                                          int     screen);
        gboolean (*fade_set_alpha_gamma) (GSFade *fade,
                                          int     screen,
                                          gdouble alpha);
        void     (*fade_finish)          (GSFade *fade,
                                          int     screen);
};

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

        int              num_screens;

        struct GSFadeScreenPrivate *screen_priv;
};

enum {
        FADED,
        LAST_SIGNAL
};

enum {
        FADE_TYPE_NONE,
        FADE_TYPE_GAMMA_NUMBER,
        FADE_TYPE_GAMMA_RAMP,
        FADE_TYPE_XRANDR,
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
                  struct GSFadeScreenPrivate *screen_priv,
                  float            ratio)
{
        Bool status;
        struct GSGammaInfo *gamma_info;

        gamma_info = screen_priv->info;

        if (!gamma_info)
                return FALSE;

        if (ratio < 0) {
                ratio = 0;
        }
        if (ratio > 1) {
                ratio = 1;
        }

        if (gamma_info->size == 0) {
                /* we only have a gamma number, not a ramp. */

                XF86VidModeGamma g2;

                g2.red   = screen_priv->vmg.red   * ratio;
                g2.green = screen_priv->vmg.green * ratio;
                g2.blue  = screen_priv->vmg.blue  * ratio;

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

                r = g_new0 (unsigned short, gamma_info->size);
                g = g_new0 (unsigned short, gamma_info->size);
                b = g_new0 (unsigned short, gamma_info->size);

                for (i = 0; i < gamma_info->size; i++) {
                        r[i] = gamma_info->r[i] * ratio;
                        g[i] = gamma_info->g[i] * ratio;
                        b[i] = gamma_info->b[i] * ratio;
                }

                status = XF86VidModeSetGammaRamp (GDK_DISPLAY (), screen, gamma_info->size, r, g, b);

                g_free (r);
                g_free (g);
                g_free (b);

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

#ifdef HAVE_XF86VMODE_GAMMA
static gboolean
gamma_fade_setup (GSFade *fade, int screen_idx)
{
        gboolean         res;
        struct GSFadeScreenPrivate *screen_priv;

        screen_priv = &fade->priv->screen_priv[screen_idx];

        if (screen_priv->info)
                return TRUE;

# ifndef HAVE_XF86VMODE_GAMMA_RAMP
        if (FADE_TYPE_GAMMA_RAMP == screen_priv->fade_type) {
                /* server is newer than client! */
                screen_priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
        }
# endif

# ifdef HAVE_XF86VMODE_GAMMA_RAMP

        screen_priv->info = g_new0(struct GSGammaInfo, 1);
        screen_priv->num_ramps = 1;

        if (FADE_TYPE_GAMMA_RAMP == screen_priv->fade_type) {
                /* have ramps */

                res = XF86VidModeGetGammaRampSize (GDK_DISPLAY (), screen_idx, &screen_priv->info->size);
                if (!res || screen_priv->info->size <= 0) {
                        screen_priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                        goto test_number;
                }

                screen_priv->info->r = g_new0 (unsigned short, screen_priv->info->size);
                screen_priv->info->g = g_new0 (unsigned short, screen_priv->info->size);
                screen_priv->info->b = g_new0 (unsigned short, screen_priv->info->size);

                if (! (screen_priv->info->r && screen_priv->info->g && screen_priv->info->b)) {
                        screen_priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                        goto test_number;
                }

                res = XF86VidModeGetGammaRamp (GDK_DISPLAY (),
                                               screen_idx,
                                               screen_priv->info->size,
                                               screen_priv->info->r,
                                               screen_priv->info->g,
                                               screen_priv->info->b);
                if (! res) {
                        screen_priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                        goto test_number;
                }
                gs_debug ("Initialized gamma ramp fade");
        }
# endif /* HAVE_XF86VMODE_GAMMA_RAMP */

 test_number:
        if (FADE_TYPE_GAMMA_NUMBER == screen_priv->fade_type) {
                /* only have gamma parameter, not ramps. */

                res = XF86VidModeGetGamma (GDK_DISPLAY (), screen_idx, &screen_priv->vmg);
                if (! res) {
                        screen_priv->fade_type = FADE_TYPE_NONE;
                        goto test_none;
                }
                gs_debug ("Initialized gamma fade for screen %d: %f %f %f",
                          screen_idx,
                          screen_priv->vmg.red,
                          screen_priv->vmg.green,
                          screen_priv->vmg.blue);
        }

 test_none:
        if (FADE_TYPE_NONE == screen_priv->fade_type) {
                goto FAIL;
        }

        return TRUE;
 FAIL:

        return FALSE;
}
#endif /* HAVE_XF86VMODE_GAMMA */

static void
screen_fade_finish (GSFade *fade, int screen_idx)
{
        struct GSFadeScreenPrivate *screen_priv;
        int i;
        screen_priv = &fade->priv->screen_priv[screen_idx];

        if (!screen_priv->info)
                return;

        for (i = 0; i < screen_priv->num_ramps; i++) {
                if (screen_priv->info[i].r)
                        g_free (screen_priv->info[i].r);
                if (screen_priv->info[i].g)
                        g_free (screen_priv->info[i].g);
                if (screen_priv->info[i].b)
                        g_free (screen_priv->info[i].b);
        }

        g_free (screen_priv->info);
        screen_priv->info = NULL;
        screen_priv->num_ramps = 0;
}

#ifdef HAVE_XF86VMODE_GAMMA
static gboolean
gamma_fade_set_alpha_gamma (GSFade *fade,
                            int screen_idx,
                            gdouble alpha)
{
        struct GSFadeScreenPrivate *screen_priv;
        gboolean res;

        screen_priv = &fade->priv->screen_priv[screen_idx];
        res = xf86_whack_gamma (screen_idx, screen_priv, alpha);

        return TRUE;
}
#endif /* HAVE_XF86VMODE_GAMMA */

static void
check_gamma_extension (GSFade *fade, int screen_idx)
{
        struct GSFadeScreenPrivate *screen_priv;
#ifdef HAVE_XF86VMODE_GAMMA
        int      event;
        int      error;
        int      major;
        int      minor;
        gboolean res;
#endif /* HAVE_XF86VMODE_GAMMA */

        screen_priv = &fade->priv->screen_priv[screen_idx];

#ifdef HAVE_XF86VMODE_GAMMA
        res = XF86VidModeQueryExtension (GDK_DISPLAY (), &event, &error);
        if (! res)
                goto fade_none;

        res = safe_XF86VidModeQueryVersion (GDK_DISPLAY (), &major, &minor);
        if (! res)
                goto fade_none;

        if (major < XF86_VIDMODE_GAMMA_MIN_MAJOR ||
            (major == XF86_VIDMODE_GAMMA_MIN_MAJOR &&
             minor < XF86_VIDMODE_GAMMA_MIN_MINOR))
                goto fade_none;

        screen_priv->fade_setup = gamma_fade_setup;
        screen_priv->fade_finish = screen_fade_finish;
        screen_priv->fade_set_alpha_gamma = gamma_fade_set_alpha_gamma;

        if (major < XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR ||
            (major == XF86_VIDMODE_GAMMA_RAMP_MIN_MAJOR &&
             minor < XF86_VIDMODE_GAMMA_RAMP_MIN_MINOR)) {
                screen_priv->fade_type = FADE_TYPE_GAMMA_NUMBER;
                return;
        }

        /* Copacetic */
        screen_priv->fade_type = FADE_TYPE_GAMMA_RAMP;
        return;
 fade_none:
#endif
        screen_priv->fade_type = FADE_TYPE_NONE;
}

/* Xrandr support */

static gboolean xrandr_fade_setup (GSFade *fade, int screen_idx)
{
        struct GSFadeScreenPrivate *screen_priv;
        GnomeRRCrtc *crtc;
        GnomeRRCrtc **crtcs;
        int crtc_count = 0;
        struct GSGammaInfo *info;
        gboolean res;

        screen_priv = &fade->priv->screen_priv[screen_idx];

        if (screen_priv->info)
                return TRUE;

        /* refresh the screen info */
        gnome_rr_screen_refresh (screen_priv->rrscreen, NULL);

        crtcs = gnome_rr_screen_list_crtcs (screen_priv->rrscreen);
        while (*crtcs) {
                crtc_count++;
                crtcs++;
        };

        screen_priv->info = g_new0 (struct GSGammaInfo, crtc_count);
        screen_priv->num_ramps = crtc_count;

        crtc_count = 0;
        crtcs = gnome_rr_screen_list_crtcs (screen_priv->rrscreen);
        while (*crtcs)
        {
                crtc = *crtcs;

                info = &screen_priv->info[crtc_count];

                /* if no mode ignore crtc */
                if (!gnome_rr_crtc_get_current_mode (crtc)) {
                        info->size = 0;
                        info->r = NULL;
                        info->g = NULL;
                        info->b = NULL;
                }
                else {
                        res = gnome_rr_crtc_get_gamma (crtc, &info->size,
                                                       &info->r, &info->g,
                                                       &info->b);
                        if (res == FALSE)
                                goto fail;
                }

                crtcs++;
                crtc_count++;
        }
        return TRUE;
 fail:
        return FALSE;
}

static void xrandr_crtc_whack_gamma (GnomeRRCrtc *crtc,
                                     struct GSGammaInfo *gamma_info,
                                     float            ratio)
{
        unsigned short *r, *g, *b;
        int i;

        if (gamma_info->size == 0)
                return;

        if (ratio < 0) {
                ratio = 0;
        }
        if (ratio > 1) {
                ratio = 1;
        }

        r = g_new0 (unsigned short, gamma_info->size);
        g = g_new0 (unsigned short, gamma_info->size);
        b = g_new0 (unsigned short, gamma_info->size);

        for (i = 0; i < gamma_info->size; i++) {
                r[i] = gamma_info->r[i] * ratio;
                g[i] = gamma_info->g[i] * ratio;
                b[i] = gamma_info->b[i] * ratio;
        }

        gnome_rr_crtc_set_gamma (crtc, gamma_info->size,
                                 r, g, b);
        g_free (r);
        g_free (g);
        g_free (b);
}

static gboolean xrandr_fade_set_alpha_gamma (GSFade *fade,
                                             int screen_idx,
                                             gdouble alpha)
{
        struct GSFadeScreenPrivate *screen_priv;
        struct GSGammaInfo *info;
        GnomeRRCrtc **crtcs;
        int i;

        screen_priv = &fade->priv->screen_priv[screen_idx];

        if (!screen_priv->info)
                return FALSE;

        crtcs = gnome_rr_screen_list_crtcs (screen_priv->rrscreen);
        i = 0;

        while (*crtcs)
        {
                info = &screen_priv->info[i];
                xrandr_crtc_whack_gamma (*crtcs, info, alpha);
                i++;
                crtcs++;
        }
        return TRUE;
}

static void
check_randr_extension (GSFade *fade, int screen_idx)
{
        GdkDisplay *display = gdk_display_get_default ();
        GdkScreen *screen = gdk_display_get_screen (display, screen_idx);
        struct GSFadeScreenPrivate *screen_priv;

        screen_priv = &fade->priv->screen_priv[screen_idx];

        screen_priv->rrscreen = gnome_rr_screen_new (screen,
                                                     NULL,
                                                     NULL,
                                                     NULL);
        if (!screen_priv->rrscreen) {
                screen_priv->fade_type = FADE_TYPE_NONE;
                return;
        }

        screen_priv->fade_type = FADE_TYPE_XRANDR;
        screen_priv->fade_setup = xrandr_fade_setup;
        screen_priv->fade_finish = screen_fade_finish;
        screen_priv->fade_set_alpha_gamma = xrandr_fade_set_alpha_gamma;
}

static gboolean
gs_fade_set_alpha (GSFade *fade,
                   gdouble alpha)
{
        gboolean ret = FALSE;
        int i;

        for (i = 0; i < fade->priv->num_screens; i++) {
                switch (fade->priv->screen_priv[i].fade_type) {
                case FADE_TYPE_GAMMA_RAMP:
                case FADE_TYPE_GAMMA_NUMBER:
                case FADE_TYPE_XRANDR:
                        ret = fade->priv->screen_priv[i].fade_set_alpha_gamma (fade, i, alpha);
                        break;
                case FADE_TYPE_NONE:
                        ret = FALSE;
                        break;
                default:
                        g_warning ("Unknown fade type");
                        ret = FALSE;
                        break;
                }
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
        struct GSFadeScreenPrivate *screen_priv;
        gboolean active_fade, res;
        int i;

        g_return_if_fail (GS_IS_FADE (fade));

        for (i = 0; i < fade->priv->num_screens; i++) {
                screen_priv = &fade->priv->screen_priv[i];
                if (screen_priv->fade_type != FADE_TYPE_NONE) {
                        res = screen_priv->fade_setup (fade, i);
                        if (res == FALSE)
                                return;
                }
        }

        if (fade->priv->timer_id > 0) {
                gs_fade_stop (fade);
        }

        fade->priv->active = TRUE;

        gs_fade_set_timeout (fade, timeout);

        active_fade = FALSE;
        for (i = 0; i < fade->priv->num_screens; i++) {
                screen_priv = &fade->priv->screen_priv[i];
                if (screen_priv->fade_type != FADE_TYPE_NONE)
                        active_fade = TRUE;
        }
        if (active_fade) {
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
        int i;
        g_return_if_fail (GS_IS_FADE (fade));

        gs_debug ("Resetting fade");

        if (fade->priv->active) {
                gs_fade_stop (fade);
        }

        fade->priv->current_alpha = 1.0;

        gs_fade_set_alpha (fade, fade->priv->current_alpha);

        for (i = 0; i < fade->priv->num_screens; i++)
                fade->priv->screen_priv[i].fade_finish (fade, i);
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
        int i;

        fade->priv = GS_FADE_GET_PRIVATE (fade);

        fade->priv->timeout = 1000;
        fade->priv->current_alpha = 1.0;

        display = gdk_display_get_default ();
        fade->priv->num_screens = gdk_display_get_n_screens (display);

        fade->priv->screen_priv = g_new0 (struct GSFadeScreenPrivate, fade->priv->num_screens);

        for (i = 0; i < fade->priv->num_screens; i++) {
                check_randr_extension (fade, i);
                if (!fade->priv->screen_priv[i].fade_type)
                        check_gamma_extension (fade, i);
                gs_debug ("Fade type: %d", fade->priv->screen_priv[i].fade_type);
        }
}

static void
gs_fade_finalize (GObject *object)
{
        GSFade *fade;
        int i;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_FADE (object));

        fade = GS_FADE (object);

        g_return_if_fail (fade->priv != NULL);

        for (i = 0; i < fade->priv->num_screens; i++)
                fade->priv->screen_priv[i].fade_finish(fade, i);

        if (fade->priv->screen_priv) {
                for (i = 0; i < fade->priv->num_screens; i++) {
                        if (!fade->priv->screen_priv[i].rrscreen)
                                continue;
                        gnome_rr_screen_destroy (fade->priv->screen_priv[i].rrscreen);
                }

                g_free (fade->priv->screen_priv);
                fade->priv->screen_priv = NULL;
        }

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
