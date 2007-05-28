/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "gs-theme-engine.h"
#include "gste-popsquares.h"

static void     gste_popsquares_class_init (GSTEPopsquaresClass *klass);
static void     gste_popsquares_init       (GSTEPopsquares      *engine);
static void     gste_popsquares_finalize   (GObject            *object);

typedef struct _square {
        int x, y, w, h;
        int color;
} square;

struct GSTEPopsquaresPrivate
{
        guint timeout_id;

        int        ncolors;
        int        subdivision;

        GdkGC     *gc;
        GdkColor  *colors;
        square    *squares;

};

#define GSTE_POPSQUARES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSTE_TYPE_POPSQUARES, GSTEPopsquaresPrivate))

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSTEPopsquares, gste_popsquares, GS_TYPE_THEME_ENGINE)

static void
hsv_to_rgb (int             h,
            double          s,
            double          v,
	    unsigned short *r,
            unsigned short *g,
            unsigned short *b)
{
        double H, S, V, R, G, B;
        double p1, p2, p3;
        double f;
        int    i;

        if (s < 0) {
                s = 0;
        }
        if (v < 0) {
                v = 0;
        }
        if (s > 1) {
                s = 1;
        }
        if (v > 1) {
                v = 1;
        }

        S = s; V = v;
        H = (h % 360) / 60.0;
        i = H;
        f = H - i;
        p1 = V * (1 - S);
        p2 = V * (1 - (S * f));
        p3 = V * (1 - (S * (1 - f)));

        if (i == 0) {
                R = V;
                G = p3;
                B = p1;
        } else if (i == 1) {
                R = p2;
                G = V;
                B = p1;
        } else if (i == 2) {
                R = p1;
                G = V;
                B = p3;
        } else if (i == 3) {
                R = p1;
                G = p2;
                B = V;
        } else if (i == 4) {
                R = p3;
                G = p1;
                B = V;
        } else {
                R = V;
                G = p1;
                B = p2;
        }

        *r = R * 65535;
        *g = G * 65535;
        *b = B * 65535;
}

static void
rgb_to_hsv (unsigned short r,
            unsigned short g,
            unsigned short b,
	    int           *h,
            double        *s,
            double        *v)
{
        double R, G, B, H, S, V;
        double cmax, cmin;
        double cmm;
        int    imax;

        R = ((double) r) / 65535.0;
        G = ((double) g) / 65535.0;
        B = ((double) b) / 65535.0;
        cmax = R;
        cmin = G;
        imax = 1;

        if (cmax < G) {
                cmax = G; cmin = R; imax = 2;
        }
        if (cmax < B) {
                cmax = B; imax = 3;
        }
        if (cmin > B) {
                cmin = B;
        }

        cmm = cmax - cmin;
        V = cmax;

        if (cmm == 0) {
                S = H = 0;
        } else {
                S = cmm / cmax;
                if (imax == 1) {
                        H = (G - B) / cmm;
                } else if (imax == 2) {
                        H = 2.0 + (B - R) / cmm;
                } else {
                        /*if (imax == 3)*/
                        H = 4.0 + (R - G) / cmm;
                }

                if (H < 0) {
                        H += 6.0;
                }
        }

        *h = (H * 60.0);
        *s = S;
        *v = V;
}

static void
make_color_ramp (GdkColormap *colormap,
		 int          h1,
                 double       s1,
                 double       v1,
		 int          h2,
                 double       s2,
                 double       v2,
		 GdkColor    *colors,
                 int          n_colors,
		 gboolean     closed,
		 gboolean     allocate,
		 gboolean     writable)
{
        double   dh, ds, dv;		/* deltas */
        int      i;
        int      ncolors, wanted;
        int      total_ncolors   = n_colors;

        wanted = total_ncolors;
        if (closed) {
                wanted = (wanted / 2) + 1;
        }

        ncolors = total_ncolors;

        memset (colors, 0, n_colors * sizeof (*colors));

        if (closed) {
                ncolors = (ncolors / 2) + 1;
        }

        /* Note: unlike other routines in this module, this function assumes that
           if h1 and h2 are more than 180 degrees apart, then the desired direction
           is always from h1 to h2 (rather than the shorter path.)  make_uniform
           depends on this.
        */
        dh = ((double)h2 - (double)h1) / ncolors;
        ds = (s2 - s1) / ncolors;
        dv = (v2 - v1) / ncolors;

        for (i = 0; i < ncolors; i++) {
                hsv_to_rgb ((int) (h1 + (i * dh)),
                            (s1 + (i * ds)),
                            (v1 + (i * dv)),
                            &colors [i].red,
                            &colors [i].green,
                            &colors [i].blue);
                if (allocate) {
                        gdk_colormap_alloc_color (colormap,
                                                  &colors [i],
                                                  writable,
                                                  TRUE);
                }
        }

        if (closed) {
                for (i = ncolors; i < n_colors; i++) {
                        colors [i] = colors [n_colors - i];
                }
        }

}

static void
randomize_square_colors (square *squares,
                         int     nsquares,
                         int     ncolors)
{
        int     i;
        square *s;

        s = squares;

        for (i = 0; i < nsquares; i++) {
                s[i].color = g_random_int_range (0, ncolors);
        }
}

static void
set_colors (GdkWindow *window,
            GdkColor  *fg,
            GdkColor  *bg)
{
        GtkWidget *widget;
        GdkColor   color;

        widget = gtk_invisible_new ();

        color = widget->style->dark [GTK_STATE_SELECTED];
        fg->red   = color.red;
        fg->green = color.green;
        fg->blue  = color.blue;
        color = widget->style->bg [GTK_STATE_SELECTED];
        bg->red   = color.red;
        bg->green = color.green;
        bg->blue  = color.blue;
}

static void
gste_popsquares_set_property (GObject            *object,
                              guint               prop_id,
                              const GValue       *value,
                              GParamSpec         *pspec)
{
        GSTEPopsquares *self;

        self = GSTE_POPSQUARES (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gste_popsquares_get_property (GObject            *object,
                              guint               prop_id,
                              GValue             *value,
                              GParamSpec         *pspec)
{
        GSTEPopsquares *self;

        self = GSTE_POPSQUARES (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
setup_squares (GSTEPopsquares *pop)
{
        int       window_width;
        int       window_height;
        int       nsquares;
        int       x, y;
        int       sw, sh, gw, gh;
        GdkWindow *window;

        window = gs_theme_engine_get_window (GS_THEME_ENGINE (pop));

        if (window == NULL) {
                return;
        }

        gs_theme_engine_get_window_size (GS_THEME_ENGINE (pop), &window_width, &window_height);

        sw = window_width / pop->priv->subdivision;
        sh = window_height / pop->priv->subdivision;

        gw = pop->priv->subdivision;
        gh = pop->priv->subdivision;
        nsquares = gw * gh;

        if (pop->priv->squares) {
                g_free (pop->priv->squares);
        }
        pop->priv->squares = g_new0 (square, nsquares);

        for (y = 0; y < gh; y++) {
                for (x = 0; x < gw; x++) {
                        square *s = (square *) &pop->priv->squares [gw * y + x];
                        s->w = sw;
                        s->h = sh;
                        s->x = x * sw;
                        s->y = y * sh;
                }
        }
}

static void
setup_colors (GSTEPopsquares *pop)
{
        double    s1, v1, s2, v2 = 0;
        int       h1, h2 = 0;
        int       nsquares;
        GdkColor  fg;
        GdkColor  bg;
        GdkWindow *window;

        window = gs_theme_engine_get_window (GS_THEME_ENGINE (pop));

        if (window == NULL) {
                return;
        }

        set_colors (window, &fg, &bg);

        if (pop->priv->gc) {
                g_object_unref (pop->priv->gc);
        }
        pop->priv->gc = gdk_gc_new (window);

        if (pop->priv->colors) {
                g_free (pop->priv->colors);
        }
        pop->priv->colors = g_new0 (GdkColor, pop->priv->ncolors);

        rgb_to_hsv (fg.red, fg.green, fg.blue, &h1, &s1, &v1);
        rgb_to_hsv (bg.red, bg.green, bg.blue, &h2, &s2, &v2);

        make_color_ramp (gtk_widget_get_colormap (GTK_WIDGET (pop)),
                         h1, s1, v1,
                         h2, s2, v2,
                         pop->priv->colors,
                         pop->priv->ncolors,
                         TRUE,
                         TRUE,
                         FALSE);

        nsquares = pop->priv->subdivision * pop->priv->subdivision;

        randomize_square_colors (pop->priv->squares, nsquares, pop->priv->ncolors);
}

static void
gste_popsquares_real_show (GtkWidget *widget)
{
        GSTEPopsquares *pop = GSTE_POPSQUARES (widget);

        /* start */
        setup_squares (pop);
        setup_colors (pop);

        if (GTK_WIDGET_CLASS (parent_class)->show) {
                GTK_WIDGET_CLASS (parent_class)->show (widget);
        }
}

static gboolean
gste_popsquares_real_expose (GtkWidget      *widget,
                             GdkEventExpose *event)
{
        gboolean handled = FALSE;

        /* draw */

        /* FIXME: should double buffer? */

        if (GTK_WIDGET_CLASS (parent_class)->expose_event) {
                handled = GTK_WIDGET_CLASS (parent_class)->expose_event (widget, event);
        }

        return handled;
}

static gboolean
gste_popsquares_real_configure (GtkWidget         *widget,
                                GdkEventConfigure *event)
{
        GSTEPopsquares *pop = GSTE_POPSQUARES (widget);
        gboolean        handled = FALSE;

        /* resize */

        /* just reset everything */
        setup_squares (pop);
        setup_colors (pop);

        /* schedule a redraw */
        gtk_widget_queue_draw (widget);

        if (GTK_WIDGET_CLASS (parent_class)->configure_event) {
                handled = GTK_WIDGET_CLASS (parent_class)->configure_event (widget, event);
        }

        return handled;
}

static void
gste_popsquares_class_init (GSTEPopsquaresClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = gste_popsquares_finalize;
        object_class->get_property = gste_popsquares_get_property;
        object_class->set_property = gste_popsquares_set_property;

        widget_class->show = gste_popsquares_real_show;
        widget_class->expose_event = gste_popsquares_real_expose;
        widget_class->configure_event = gste_popsquares_real_configure;

        g_type_class_add_private (klass, sizeof (GSTEPopsquaresPrivate));
}

static gboolean
draw_iter (GSTEPopsquares *pop)
{
        int      border = 1;
        gboolean twitch = FALSE;
        int      x, y;
        int      sw, sh, gw, gh;
        int      nsquares;
        int      window_width;
        int      window_height;
        GdkWindow *window;

        window = gs_theme_engine_get_window (GS_THEME_ENGINE (pop));

        if (window == NULL) {
                return TRUE;
        }

        gs_theme_engine_get_window_size (GS_THEME_ENGINE (pop),
                                         &window_width,
                                         &window_height);
        sw = window_width / pop->priv->subdivision;
        sh = window_height / pop->priv->subdivision;

        gw = pop->priv->subdivision;
        gh = pop->priv->subdivision;
        nsquares = gw * gh;

        for (y = 0; y < gh; y++) {
                for (x = 0; x < gw; x++) {
                        square *s = (square *) &pop->priv->squares [gw * y + x];

                        gdk_gc_set_foreground (pop->priv->gc, &(pop->priv->colors [s->color]));
                        gdk_draw_rectangle (window, pop->priv->gc, TRUE, s->x, s->y,
                                            border ? s->w - border : s->w,
                                            border ? s->h - border : s->h);
                        s->color++;

                        if (s->color == pop->priv->ncolors) {
                                if (twitch && ((g_random_int_range (0, 4)) == 0)) {
                                        randomize_square_colors (pop->priv->squares, nsquares, pop->priv->ncolors);
                                } else {
                                        s->color = g_random_int_range (0, pop->priv->ncolors);
                                }
                        }
                }
        }

        return TRUE;
}

static void
gste_popsquares_init (GSTEPopsquares *pop)
{
        int delay;

        pop->priv = GSTE_POPSQUARES_GET_PRIVATE (pop);

        pop->priv->ncolors = 128;
        pop->priv->subdivision = 5;

        delay = 25;
        pop->priv->timeout_id = g_timeout_add (delay, (GSourceFunc)draw_iter, pop);
}

static void
gste_popsquares_finalize (GObject *object)
{
        GSTEPopsquares *pop;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSTE_IS_POPSQUARES (object));

        pop = GSTE_POPSQUARES (object);

        g_return_if_fail (pop->priv != NULL);

        if (pop->priv->timeout_id > 0) {
                g_source_remove (pop->priv->timeout_id);
                pop->priv->timeout_id = 0;
        }

        g_free (pop->priv->squares);
        g_free (pop->priv->colors);
        g_object_unref (pop->priv->gc);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}
