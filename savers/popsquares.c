/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (c) 2003 Levi Burton <donburton@sbcglobal.net>
 * Copyright (c) 1992, 1997 Jamie Zawinski <jwz@jwz.org>
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

#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

typedef struct _square {
        int x, y, w, h; 
        int color;
} square;

GdkWindow *screenhack_window     = NULL;
guint      screenhack_timeout_id = 0;

static int ncolors     = 128;
static int subdivision = 5;

GdkGC     *gc            = NULL;
GdkColor  *colors        = NULL;
square    *squares       = NULL;
int        window_width  = 400;
int        window_height = 400;

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

        if (s < 0) s = 0;
        if (v < 0) v = 0;
        if (s > 1) s = 1;
        if (v > 1) v = 1;

        S = s; V = v;
        H = (h % 360) / 60.0;
        i = H;
        f = H - i;
        p1 = V * (1 - S);
        p2 = V * (1 - (S * f));
        p3 = V * (1 - (S * (1 - f)));

        if	  (i == 0) { R = V;  G = p3; B = p1; }
        else if (i == 1) { R = p2; G = V;  B = p1; }
        else if (i == 2) { R = p1; G = V;  B = p3; }
        else if (i == 3) { R = p1; G = p2; B = V;  }
        else if (i == 4) { R = p3; G = p1; B = V;  }
        else		   { R = V;  G = p1; B = p2; }

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
        cmax = R; cmin = G; imax = 1;

        if  ( cmax < G ) { cmax = G; cmin = R; imax = 2; }
        if  ( cmax < B ) { cmax = B; imax = 3; }
        if  ( cmin > B ) { cmin = B; }

        cmm = cmax - cmin;
        V = cmax;

        if (cmm == 0)
                S = H = 0;
        else {
                S = cmm / cmax;
                if       (imax == 1)    H =       (G - B) / cmm;
                else  if (imax == 2)    H = 2.0 + (B - R) / cmm;
                else /*if (imax == 3)*/ H = 4.0 + (R - G) / cmm;
                if (H < 0) H += 6.0;
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
        if (closed)
                wanted = (wanted / 2) + 1;

        ncolors = total_ncolors;

        memset (colors, 0, n_colors * sizeof (*colors));

        if (closed)
                ncolors = (ncolors / 2) + 1;

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
                for (i = ncolors; i < n_colors; i++)
                        colors [i] = colors [n_colors - i];
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

        for (i = 0; i < nsquares; i++) 
                s[i].color = g_random_int_range (0, ncolors);
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

static guint
screenhack_init (GdkWindow *window)
{
        double    s1, v1, s2, v2 = 0;
        int       x, y;
        int       h1, h2 = 0;
        int       sw, sh, gw, gh;
        int       nsquares;
        GdkColor  fg;
        GdkColor  bg;

        gdk_drawable_get_size (window, &window_width, &window_height);

        set_colors (window, &fg, &bg);

        gc = gdk_gc_new (window);

        colors = g_new0 (GdkColor, ncolors);
        squares = g_new0 (square, nsquares);

        rgb_to_hsv (fg.red, fg.green, fg.blue, &h1, &s1, &v1);
        rgb_to_hsv (bg.red, bg.green, bg.blue, &h2, &s2, &v2);

        make_color_ramp (gdk_drawable_get_colormap (window),
                         h1, s1, v1,
                         h2, s2, v2,
                         colors,
                         ncolors,
                         TRUE,
                         TRUE,
                         FALSE);

        sw = window_width / subdivision;
        sh = window_height / subdivision;

        gw = subdivision;
        gh = subdivision;
        nsquares = gw * gh;

        for (y = 0; y < gh; y++) {
                for (x = 0; x < gw; x++) {
                        square *s = (square *) &squares [gw * y + x];
                        s->w = sw;
                        s->h = sh;
                        s->x = x * sw;
                        s->y = y * sh;
                }
        }

        randomize_square_colors (squares, nsquares, ncolors);

        return 25;
}

static gboolean
screenhack_iter (GdkWindow *window)
{
        int      border = 1;
        gboolean twitch = FALSE;
        int      x, y;
        int      sw, sh, gw, gh;
        int      nsquares;

        sw = window_width / subdivision;
        sh = window_height / subdivision;

        gw = subdivision;
        gh = subdivision;
        nsquares = gw * gh;

        for (y = 0; y < gh; y++) {
                for (x = 0; x < gw; x++) {
                        square *s = (square *) &squares [gw * y + x];

                        gdk_gc_set_foreground (gc, &(colors [s->color]));
                        gdk_draw_rectangle (window, gc, TRUE, s->x, s->y, 
                                            border ? s->w - border : s->w, 
                                            border ? s->h - border : s->h);
                        s->color++;

                        if (s->color == ncolors) {
                                if (twitch && ((g_random_int_range (0, 4)) == 0))
                                        randomize_square_colors (squares, nsquares, ncolors);
                                else
                                        s->color = g_random_int_range (0, ncolors);
                        }
                }
        }

        return TRUE;
}

static void
screenhack_destroy (void)
{
        gdk_window_clear (screenhack_window);

        g_free (squares);
        g_free (colors);
        g_object_unref (gc);
}

static GdkWindow *
new_window (void)
{
        GdkColor      color = { 0, 0, 0 };
        GdkColormap  *colormap;
        GdkScreen    *screen;
        GdkWindow    *window;
        GdkWindow    *parent_window;
        GdkWindowAttr attributes;
        gint          attributes_mask;
                
        attributes_mask = 0;
        attributes.window_type = GDK_WINDOW_TOPLEVEL;
        attributes.wclass = GDK_INPUT_OUTPUT;
        attributes.event_mask = (GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK);
        attributes.width = 400;
        attributes.height = 400;

        screen = gdk_screen_get_default ();
        parent_window = gdk_screen_get_root_window (screen);

        window = gdk_window_new (parent_window, &attributes, attributes_mask);

        colormap = gdk_drawable_get_colormap (window);
        gdk_colormap_alloc_color (colormap, &color, FALSE, TRUE);
        gdk_window_set_background (window, &color);

        return window;
}

static GdkWindow *
use_window (const char *str)
{
        GdkWindow    *window = NULL;
        unsigned long id     = 0;
        char          c;

        if (! str)
                return NULL;

        if (1 == sscanf (str, " 0x%lx %c", &id, &c)
            || 1 == sscanf (str, " %lu %c",   &id, &c)) {
                window = gdk_window_foreign_new ((Window)id);
                gdk_window_set_events (window, GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK);
        }

        return window;
}

static GdkWindow *
get_window (void)
{
        GdkWindow  *window;
        const char *env_win;

        env_win = g_getenv ("XSCREENSAVER_WINDOW");
        if (env_win) {                
                window = use_window (env_win);
        } else {
                window = new_window ();
        }

        gdk_window_show (window);

        return window;
}

static void
do_restart (void)
{
        guint delay;

        if (screenhack_timeout_id > 0) {
                g_source_remove (screenhack_timeout_id);
                screenhack_timeout_id = 0;
        }

        if (screenhack_window)
                screenhack_destroy ();
        else
                screenhack_window = get_window ();

        delay = screenhack_init (screenhack_window);

        screenhack_timeout_id = g_timeout_add (delay, (GSourceFunc)screenhack_iter, screenhack_window);
}

static void
do_configure_event (GdkEvent *event)
{
        int width;
        int height;

        width  = event->configure.width;
        height = event->configure.height;

        if (window_width == width
            && window_height == height)
                return;

        do_restart ();
}

static void
do_event (GdkEvent *event)
{
        switch (event->type) {
        case GDK_NOTHING:
                break;
        case GDK_DELETE:
                break;
        case GDK_DESTROY:
                break;
        case GDK_EXPOSE:
                break;
        case GDK_CONFIGURE:
                do_configure_event (event);
                break;
        default:
                break;
        }
}

int
main (int argc, char **argv)
{
        GMainLoop *main_loop = NULL;

        gtk_init (&argc, &argv);
        
        g_set_prgname ("popsquares");

        gdk_event_handler_set ((GdkEventFunc)do_event, NULL, NULL);

        do_restart ();

        main_loop = g_main_loop_new (NULL, FALSE);
        GDK_THREADS_LEAVE ();
        g_main_loop_run (main_loop);
        GDK_THREADS_ENTER ();

        g_main_loop_unref (main_loop);

        screenhack_destroy ();

        return 0;
}
