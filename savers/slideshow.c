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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gthread.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#define N_FADE_TICKS 40
#define DEFAULT_IMAGES_LOCATION "/usr/share/backgrounds/images"
#define IMAGE_LOAD_TIMEOUT 10000

static GdkWindow   *screenhack_window     = NULL;
static guint        screenhack_timeout_id = 0;
static int          window_width          = 400;
static int          window_height         = 400;
static cairo_t     *cr;

static GdkPixbuf   *pixbuf1     = NULL;
static GdkPixbuf   *pixbuf2     = NULL;
static gint64       fade_ticks  = 0;

static GThread     *load_thread = NULL;
static GAsyncQueue *op_q        = NULL;
static GAsyncQueue *results_q   = NULL;

static guint        results_pull_id = 0;
static guint        update_image_id = 0;

static GSList      *filename_list = NULL;

static char        *images_location;

static GOptionEntry entries [] = {
        { "location", 0, 0, G_OPTION_ARG_STRING, &images_location,
          N_("Location to get images from"), N_("PATH") },
        { NULL }
};

typedef struct _Op
{
        char *location;
} Op;

typedef struct _OpResult
{
        GdkPixbuf *pixbuf;
} OpResult;

static gchar *
get_location (void)
{
        /* FIXME: check if it exists */

        if (images_location)
                return g_strdup (images_location);

        /* FIXME: try different locations */

        return g_strdup (DEFAULT_IMAGES_LOCATION);
}

static gboolean
push_load_image_func (const char *location)
{
        Op *op;

        op = g_new (Op, 1);

        op->location = g_strdup (location);

        g_async_queue_push (op_q, op);

        update_image_id = 0;
        return FALSE;
}

static void
start_new_load (guint timeout)
{
        char *location = NULL;

        /* queue a new load */
        location = get_location ();
        if (update_image_id <= 0) {
                update_image_id = g_timeout_add_full (G_PRIORITY_LOW, timeout,
                                                      (GSourceFunc)push_load_image_func,
                                                      location, g_free);
        }
}

static void
start_fade (GdkPixbuf *pixbuf)
{
        if (pixbuf2)
                g_object_unref (pixbuf2);

        pixbuf2 = g_object_ref (pixbuf);

        fade_ticks = 0;
}

static void
finish_fade (void)
{
        if (pixbuf1)
                g_object_unref (pixbuf1);
        pixbuf1 = pixbuf2;
        pixbuf2 = NULL;

        start_new_load (IMAGE_LOAD_TIMEOUT);
}

static void
update_display (void)
{
        int pw, ph;
        int x, y;
        gdouble alpha2 = 0.0;
        gdouble max_alpha = 1.0;

        if (pixbuf2) {
                /* we are in a fade */
                fade_ticks++;

                pw = gdk_pixbuf_get_width (pixbuf2);
                ph = gdk_pixbuf_get_height (pixbuf2);

                x = (window_width - pw) / 2;
                y = (window_height - ph) / 2;

                alpha2 = max_alpha * (double)fade_ticks / (double)N_FADE_TICKS;

                /* fade out areas not covered by the new image */
                /* top */
                cairo_rectangle (cr, 0, 0, window_width, y);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, alpha2);
                cairo_fill (cr);
                /* left */
                cairo_rectangle (cr, 0, 0, x, window_height);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, alpha2);
                cairo_fill (cr);
                /* bottom */
                cairo_rectangle (cr, 0, y + ph, window_width, window_height);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, alpha2);
                cairo_fill (cr);
                /* right */
                cairo_rectangle (cr, x + pw, 0, window_width, window_height);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, alpha2);
                cairo_fill (cr);
                
                gdk_cairo_set_source_pixbuf (cr,
                                             pixbuf2,
                                             x, y);

                if (alpha2 >= max_alpha) {
                        finish_fade ();
                        cairo_paint (cr);
                } else {
                        cairo_paint_with_alpha (cr, alpha2);
                }

        } else {
                /*cairo_paint (cr);*/
        }
}

static void
process_new_pixbuf (GdkPixbuf *pixbuf)
{

        if (pixbuf) {
                start_fade (pixbuf);
        } else {
                start_new_load (10);
        }
}

static void
op_result_free (OpResult *result)
{
        if (! result)
                return;

        if (result->pixbuf)
                g_object_unref (result->pixbuf);

        g_free (result);
}

static gboolean
results_pull_func (gpointer data)
{
        OpResult *result;

        GDK_THREADS_ENTER ();

        g_async_queue_lock (results_q);

        result = g_async_queue_try_pop_unlocked (results_q);
        g_assert (result);

        while (result) {
                process_new_pixbuf (result->pixbuf);
                op_result_free (result);

                result = g_async_queue_try_pop_unlocked (results_q);
        }

        results_pull_id = 0;

        g_async_queue_unlock (results_q);

        GDK_THREADS_LEAVE ();

        return FALSE;
}

static GdkPixbuf *
scale_pixbuf (GdkPixbuf *pixbuf,
              int        max_width,
              int        max_height)
{
        int        pw;
        int        ph;
	float	   scale_factor_x = 1.0;
	float	   scale_factor_y = 1.0;
	float	   scale_factor = 1.0;
	GdkPixbuf *scaled = NULL;

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

	/* Determine which dimension requires the smallest scale. */
	if (pw > max_width)
		scale_factor_x = (float) max_width / (float) pw;

	if (ph > max_height)
		scale_factor_y = (float) max_height / (float) ph;

	if (scale_factor_x > scale_factor_y)
		scale_factor = scale_factor_y;
	else
		scale_factor = scale_factor_x;

	if (1) {
		int scale_x = (int) (pw * scale_factor);
		int scale_y = (int) (ph * scale_factor);

		/* Scale bigger dimension to max icon height/width */
		scaled = gdk_pixbuf_scale_simple (pixbuf,
						  scale_x,
						  scale_y,
						  GDK_INTERP_BILINEAR);
	}

	return scaled;
}

static void
add_files_to_list (GSList    **list,
                   const char *base)
{
	GDir       *d; 
	const char *d_name;

	d = g_dir_open (base, 0, NULL);
	if (! d) {
		return;
	}

	while ((d_name = g_dir_read_name (d)) != NULL) {
                char *path;

		path = g_build_filename (base, d_name, NULL);
                if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
                        add_files_to_list (list, path);
                        g_free (path);
                } else {
                        *list = g_slist_prepend (*list, path);
                }
	}

        g_dir_close (d);
}

static GSList *
build_filename_list_local_dir (const char *base)
{
        GSList *list = NULL;

        add_files_to_list (&list, base);

        return list;
}

static GdkPixbuf *
get_pixbuf_from_local_dir (const char *location)
{
        GdkPixbuf *pixbuf;
        char      *filename;
        int        i;
        GSList    *l;

        /* rebuild the cache */
        if (! filename_list) {
                filename_list = build_filename_list_local_dir (location);
        }

        /* get a random filename */
        i = g_random_int_range (0, g_slist_length (filename_list));
        l = g_slist_nth (filename_list, i);
        filename = l->data;

        pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

        g_free (filename);
        filename_list = g_slist_delete_link (filename_list, l);

        return pixbuf;
}

static GdkPixbuf *
get_pixbuf_from_location (const char *location)
{
        GdkPixbuf *pixbuf = NULL;
        gboolean   is_absolute;
        gboolean   is_dir;

        if (! location)
                return NULL;

        is_absolute = g_path_is_absolute (location);
        is_dir = g_file_test (location, G_FILE_TEST_IS_DIR);

        if (is_absolute && is_dir)
                pixbuf = get_pixbuf_from_local_dir (location);

        return pixbuf;
}

static GdkPixbuf *
get_pixbuf (const char *location,
            int   width,
            int   height)
{
        GdkPixbuf *pixbuf;
        GdkPixbuf *scaled = NULL;

        if (! location)
                return NULL;

        pixbuf = get_pixbuf_from_location (location);

        if (pixbuf) {
                scaled = scale_pixbuf (pixbuf, width, height);
                g_object_unref (pixbuf);
        }

        return scaled;
}

static void
op_load_image (const char *location)
{
        OpResult *op_result;

        op_result = g_new0 (OpResult, 1);

        op_result->pixbuf = get_pixbuf (location, window_width, window_height);

        GDK_THREADS_ENTER ();
        g_async_queue_lock (results_q);
        g_async_queue_push_unlocked (results_q, op_result);

        if (! results_pull_id)
		results_pull_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                                                   results_pull_func,
                                                   NULL, NULL);
        g_async_queue_unlock (results_q);
        GDK_THREADS_LEAVE ();
}

static gpointer
load_threadfunc (gpointer data)
{
        Op *op;

        op = g_async_queue_pop (op_q);
        while (op) {
                op_load_image (op->location);

                g_free (op->location);
                g_free (op);

                op = g_async_queue_pop (op_q);
        }
  
        return NULL;
}

static guint
screenhack_init (GdkWindow *window)
{
        GError *err;

        gdk_drawable_get_size (window, &window_width, &window_height);

        gdk_window_clear (window);

        cr = gdk_cairo_create (window);

        op_q = g_async_queue_new ();
        results_q = g_async_queue_new ();

        err = NULL;
        load_thread = g_thread_create (load_threadfunc, NULL, FALSE, &err);
        if (! load_thread) {
                g_error ("Could not create a thread to load images: %s",
                         err->message);
                g_error_free (err);
                exit (-1);
        }

        start_new_load (10);

        return 40;
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
        attributes.width = 800;
        attributes.height = 800;

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
screenhack_destroy (void)
{
        OpResult *result;

        gdk_window_clear (screenhack_window);

        cairo_destroy (cr);

        if (results_pull_id)
                g_source_remove (results_pull_id);

        result = g_async_queue_try_pop (results_q);
        while (result) {

                result = g_async_queue_try_pop (results_q);
        }
        g_async_queue_unref (results_q);


}

static gboolean
screenhack_iter (GdkWindow *window)
{
        update_display ();

        return TRUE;
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

        window_width = width;
        window_height = height;
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
                update_display ();
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
        GError    *error;
        gboolean   ret;

        g_thread_init (NULL);
        ret = gtk_init_with_args (&argc, &argv,
                                  NULL,
                                  entries,
                                  NULL,
                                  &error);
        if (! ret) {
                g_message ("%s", error->message);
                g_error_free (error);
                exit (1);
        }

        g_set_prgname ("slideshow");

        gdk_event_handler_set ((GdkEventFunc)do_event, NULL, NULL);

        do_restart ();

        main_loop = g_main_loop_new (NULL, FALSE);
        GDK_THREADS_LEAVE ();
        g_main_loop_run (main_loop);
        GDK_THREADS_ENTER ();

        g_main_loop_unref (main_loop);

        screenhack_destroy ();
        g_free (images_location);

        return 0;
}
