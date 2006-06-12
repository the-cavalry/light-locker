/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2005-2006 William Jon McCann <mccann@jhu.edu>
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
#include <glib/gthread.h>
#include <gtk/gtk.h>

#if HAVE_EXIF
#include <libexif/exif-data.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-content.h>
#include <libexif/exif-tag.h>
#include <libexif/exif-byte-order.h>
#include <libexif/exif-format.h>
#include <libexif/exif-utils.h>
#endif /* HAVE_EXIF */

#include "gs-theme-engine.h"
#include "gste-slideshow.h"

static void     gste_slideshow_class_init (GSTESlideshowClass *klass);
static void     gste_slideshow_init       (GSTESlideshow      *engine);
static void     gste_slideshow_finalize   (GObject            *object);

struct GSTESlideshowPrivate
{
        /* Image at full opacity */
        cairo_pattern_t *pat1;
        /* Image at partial opacity */
        cairo_pattern_t *pat2;
        /* Alpha of pat2 */
        gdouble          alpha2;
        /* edges of pat2 */
        int              pat2top;
        int              pat2bottom;
        int              pat2left;
        int              pat2right;

        /* backbuffer that we do all the alpha drawing into (no round
         * trips to the X server when the server doesn't support drawing
         * pixmaps with alpha?) */
        cairo_surface_t *surf;

        gint64           fade_ticks;

        GThread         *load_thread;
        GAsyncQueue     *op_q;
        GAsyncQueue     *results_q;

        guint           results_pull_id;
        guint           update_image_id;

        GSList         *filename_list;
        char           *images_location;
        int             window_width;
        int             window_height;

        guint           timeout_id;

        GTimer         *timer;
        gboolean        fade_disabled;
};

enum {
        PROP_0,
        PROP_IMAGES_LOCATION
};

#define GSTE_SLIDESHOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSTE_TYPE_SLIDESHOW, GSTESlideshowPrivate))

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSTESlideshow, gste_slideshow, GS_TYPE_THEME_ENGINE)

#define N_FADE_TICKS 10
#define MINIMUM_FPS 3.0
#define DEFAULT_IMAGES_LOCATION DATADIR "/pixmaps/backgrounds"
#define IMAGE_LOAD_TIMEOUT 10000

typedef struct _Op
{
        char          *location;
        GSTESlideshow *slideshow;
} Op;

typedef struct _OpResult
{
        GdkPixbuf *pixbuf;
} OpResult;

#if HAVE_EXIF

enum _ExifRotation {
        NORMAL = 1,
        HFLIP,
        ROTATE180,
        VFLIP,
        ROTATE90_HFLIP,
        ROTATE90,
        ROTATE90_VFLIP,
        ROTATE270
};
typedef enum _ExifRotation ExifRotation;

static ExifRotation
get_exif_orientation (const char *filename)
{
        ExifData     *edata;
        ExifEntry    *entry;
        ExifByteOrder o;
        ExifRotation  s;
        unsigned int  i;

        edata = exif_data_new_from_file (filename);

        if (edata == NULL) {
                return 0;
        }

        o = exif_data_get_byte_order (edata);

        for (i = 0; i < EXIF_IFD_COUNT; i++) {
                entry = exif_content_get_entry (edata->ifd [i],
                                                EXIF_TAG_ORIENTATION);
                if (entry != NULL) {
                        s = (ExifRotation)exif_get_short (entry->data, o);
                        exif_data_unref (edata);

                        return s;
                }
        }

        exif_data_unref (edata);

        return 0;
}

/* Routines for rotating a GdkPixbuf.
 * Borrowed from f-spot.
 * For copyright information see libfspot/f-pixbuf-utils.c in the f-spot
 * package.
 */

/* Returns a copy of pixbuf src rotated 90 degrees clockwise or 90
   counterclockwise.  */
static GdkPixbuf *
pixbuf_copy_rotate_90 (GdkPixbuf *src,
                       gboolean   counter_clockwise)
{
        GdkPixbuf *dest;
        int        has_alpha;
        int        sw, sh, srs;
        int        dw, dh, drs;
        guchar    *s_pix;
        guchar    *d_pix;
        guchar    *sp;
        guchar    *dp;
        int        i, j;
        int        a;

        if (src == NULL) {
                return NULL;
        }

        sw = gdk_pixbuf_get_width (src);
        sh = gdk_pixbuf_get_height (src);
        has_alpha = gdk_pixbuf_get_has_alpha (src);
        srs = gdk_pixbuf_get_rowstride (src);
        s_pix = gdk_pixbuf_get_pixels (src);

        dw = sh;
        dh = sw;
        dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB, has_alpha, 8, dw, dh);
        drs = gdk_pixbuf_get_rowstride (dest);
        d_pix = gdk_pixbuf_get_pixels (dest);

        a = (has_alpha ? 4 : 3);

        for (i = 0; i < sh; i++) {
                sp = s_pix + (i * srs);
                for (j = 0; j < sw; j++) {
                        if (counter_clockwise) {
                                dp = d_pix + ((dh - j - 1) * drs) + (i * a);
                        } else {
                                dp = d_pix + (j * drs) + ((dw - i - 1) * a);
                        }

                        *(dp++) = *(sp++);	/* r */
                        *(dp++) = *(sp++);	/* g */
                        *(dp++) = *(sp++);	/* b */
                        if (has_alpha) {
                                *(dp) = *(sp++);	/* a */
                        }
                }
        }

        return dest;
}

/* Returns a copy of pixbuf mirrored and or flipped.  TO do a 180 degree
   rotations set both mirror and flipped TRUE if mirror and flip are FALSE,
   result is a simple copy.  */
static GdkPixbuf *
pixbuf_copy_mirror (GdkPixbuf *src,
                    gboolean   mirror,
                    gboolean   flip)
{
        GdkPixbuf *dest;
        int        has_alpha;
        int        w, h, srs;
        int        drs;
        guchar    *s_pix;
        guchar    *d_pix;
        guchar    *sp;
        guchar    *dp;
        int        i, j;
        int        a;

        if (src == NULL) {
                return NULL;
        }

        w = gdk_pixbuf_get_width (src);
        h = gdk_pixbuf_get_height (src);
        has_alpha = gdk_pixbuf_get_has_alpha (src);
        srs = gdk_pixbuf_get_rowstride (src);
        s_pix = gdk_pixbuf_get_pixels (src);

        dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB, has_alpha, 8, w, h);
        drs = gdk_pixbuf_get_rowstride (dest);
        d_pix = gdk_pixbuf_get_pixels (dest);

        a = has_alpha ? 4 : 3;

        for (i = 0; i < h; i++)	{
                sp = s_pix + (i * srs);
                if (flip) {
                        dp = d_pix + ((h - i - 1) * drs);
                } else {
                        dp = d_pix + (i * drs);
                }

                if (mirror) {
                        dp += (w - 1) * a;
                        for (j = 0; j < w; j++) {
                                *(dp++) = *(sp++);	/* r */
                                *(dp++) = *(sp++);	/* g */
                                *(dp++) = *(sp++);	/* b */
                                if (has_alpha) {
                                        *(dp) = *(sp++);	/* a */
                                }
                                dp -= (a + 3);
                        }
                } else {
                        for (j = 0; j < w; j++) {
                                *(dp++) = *(sp++);	/* r */
                                *(dp++) = *(sp++);	/* g */
                                *(dp++) = *(sp++);	/* b */
                                if (has_alpha) {
                                        *(dp++) = *(sp++);	/* a */
                                }
                        }
                }
        }

        return dest;
}

static GdkPixbuf *
update_from_exif_data (char      *filename,
                       GdkPixbuf *pixbuf)
{
        GdkPixbuf *newpixbuf = NULL;
        GdkPixbuf *tmppixbuf = NULL;

        if (pixbuf == NULL) {
                return NULL;
        }

        switch (get_exif_orientation (filename)) {
        case NORMAL:
                break;
        case HFLIP:
                newpixbuf = pixbuf_copy_mirror (pixbuf, TRUE, FALSE);
                break;
        case ROTATE180:
                newpixbuf = pixbuf_copy_mirror (pixbuf, TRUE, TRUE);
                break;
        case VFLIP:
                newpixbuf = pixbuf_copy_mirror (pixbuf, FALSE, TRUE);
                break;
        case ROTATE90_HFLIP:
                tmppixbuf = pixbuf_copy_mirror (pixbuf, FALSE, TRUE);
                newpixbuf = pixbuf_copy_rotate_90 (tmppixbuf, FALSE);
                g_object_unref (tmppixbuf);
                break;
        case ROTATE90:
                newpixbuf = pixbuf_copy_rotate_90 (pixbuf, FALSE);
                break;
        case ROTATE90_VFLIP:
                tmppixbuf = pixbuf_copy_mirror (pixbuf, TRUE, FALSE);
                newpixbuf = pixbuf_copy_rotate_90 (tmppixbuf, FALSE);
                g_object_unref (tmppixbuf);
                break;
        case ROTATE270:
                newpixbuf = pixbuf_copy_rotate_90 (pixbuf, TRUE);
                break;
        default:
                break;
        }

        return newpixbuf;
}
#endif /* HAVE_EXIF */

static gboolean
push_load_image_func (GSTESlideshow *show)
{
        Op *op;

        gs_theme_engine_profile_msg ("Starting a new image load");

        op = g_new (Op, 1);

        op->location = g_strdup (show->priv->images_location);
        op->slideshow = g_object_ref (show);

        g_async_queue_push (show->priv->op_q, op);

        show->priv->update_image_id = 0;

        return FALSE;
}

static void
start_new_load (GSTESlideshow *show,
                guint          timeout)
{
        gs_theme_engine_profile_msg ("Scheduling a new image load");

        /* queue a new load */
        if (show->priv->update_image_id <= 0) {
                show->priv->update_image_id = g_timeout_add_full (G_PRIORITY_LOW, timeout,
                                                                  (GSourceFunc)push_load_image_func,
                                                                  show, NULL);
        }
}

static void
start_fade (GSTESlideshow *show,
            GdkPixbuf     *pixbuf)
{
        int      pw;
        int      ph;
        int      x;
        int      y;
        cairo_t *cr;
        int      window_width;
        int      window_height;

        gs_theme_engine_profile_start ("start");

        window_width = show->priv->window_width;
        window_height = show->priv->window_height;

        if (show->priv->pat2 != NULL) {
                cairo_pattern_destroy (show->priv->pat2);
        }

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);
        x = (window_width - pw) / 2;
        y = (window_height - ph) / 2;

        cr = cairo_create (show->priv->surf);

        /* XXX Handle out of memory? */
        gdk_cairo_set_source_pixbuf (cr, pixbuf, x, y);
        show->priv->pat2 = cairo_pattern_reference (cairo_get_source (cr));
        show->priv->pat2top = y;
        show->priv->pat2bottom = y + ph;
        show->priv->pat2left = x;
        show->priv->pat2right = x + pw;

        cairo_destroy (cr);

        show->priv->fade_ticks = 0;
        g_timer_start (show->priv->timer);

        gs_theme_engine_profile_end ("end");
}

static void
finish_fade (GSTESlideshow *show)
{
        gs_theme_engine_profile_start ("start");

        if (show->priv->pat1 != NULL) {
                cairo_pattern_destroy (show->priv->pat1);
        }

        show->priv->pat1 = show->priv->pat2;
        show->priv->pat2 = NULL;

        start_new_load (show, IMAGE_LOAD_TIMEOUT);

        gs_theme_engine_profile_end ("end");
}

static void
update_display (GSTESlideshow *show)
{
        int      window_width;
        int      window_height;
        cairo_t *cr;

        gs_theme_engine_profile_start ("start");

        cr = cairo_create (show->priv->surf);

        gs_theme_engine_get_window_size (GS_THEME_ENGINE (show),
                                         &window_width,
                                         &window_height);

        if (show->priv->pat2 != NULL) {
                /* fade out areas not covered by the new image */
                /* top */
                cairo_rectangle (cr, 0, 0, window_width, show->priv->pat2top);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, show->priv->alpha2);
                cairo_fill (cr);
                /* left (excluding what's covered by top and bottom) */
                cairo_rectangle (cr, 0, show->priv->pat2top,
                                 show->priv->pat2left,
                                 show->priv->pat2bottom - show->priv->pat2top);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, show->priv->alpha2);
                cairo_fill (cr);
                /* bottom */
                cairo_rectangle (cr, 0, show->priv->pat2bottom, window_width,
                                 window_height - show->priv->pat2bottom);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, show->priv->alpha2);
                cairo_fill (cr);
                /* right (excluding what's covered by top and bottom) */
                cairo_rectangle (cr, show->priv->pat2right,
                                 show->priv->pat2top,
                                 window_width - show->priv->pat2right,
                                 show->priv->pat2bottom - show->priv->pat2top);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, show->priv->alpha2);
                cairo_fill (cr);

                gs_theme_engine_profile_start ("paint pattern to surface");
                cairo_set_source (cr, show->priv->pat2);

                cairo_paint_with_alpha (cr, show->priv->alpha2);
                gs_theme_engine_profile_end ("paint pattern to surface");
        } else {
                if (show->priv->pat1 != NULL) {
                        cairo_set_source (cr, show->priv->pat1);
                        cairo_paint (cr);
                }
        }

        cairo_destroy (cr);

        /* paint the image buffer into the window */
        cr = gdk_cairo_create (GTK_WIDGET (show)->window);

        cairo_set_source_surface (cr, show->priv->surf, 0, 0);

        gs_theme_engine_profile_start ("paint surface to window");
        cairo_paint (cr);
        gs_theme_engine_profile_end ("paint surface to window");

        cairo_destroy (cr);

        gs_theme_engine_profile_end ("end");
}

static gboolean
draw_iter (GSTESlideshow *show)
{
        if (show->priv->pat2 != NULL) {
                gdouble fps;
                gdouble elapsed;

                if (show->priv->fade_disabled) {
                        show->priv->alpha2 = 1.0;
                        update_display (show);
                        finish_fade (show);
                        return TRUE;
                }

                /* we are in a fade */
                show->priv->fade_ticks++;

                /*
                 * We have currently drawn pat2 with old_opacity, and we
                 * want to set alpha2 so that drawing pat2 at alpha2
                 * yields it drawn with new_opacity
                 *
                 * Solving
                 *   new_opacity = 1 - (1 - alpha2) * (1 - old_opacity)
                 * yields
                 *   alpha2 = 1 - (1 - new_opacity) / (1 - old_opacity)
                 *
                 * XXX This assumes that cairo doesn't correct alpha for
                 * the color profile.  However, any error is guaranteed
                 * to be cleaned up by the last iteration, where alpha2
                 * becomes 1 because new_opacity is 1.
                 */
                double old_opacity = (double) (show->priv->fade_ticks - 1) /
                                     (double) N_FADE_TICKS;
                double new_opacity = (double) show->priv->fade_ticks /
                                     (double) N_FADE_TICKS;
                show->priv->alpha2 = 1.0 - (1.0 - new_opacity) /
                                           (1.0 - old_opacity);

                update_display (show);

                elapsed = g_timer_elapsed (show->priv->timer, NULL);
                fps = (gdouble)show->priv->fade_ticks / elapsed;
                if (fps < MINIMUM_FPS) {
                        g_warning ("Getting less than %.2f frames per second, disabling fade", MINIMUM_FPS);
                        show->priv->fade_ticks = N_FADE_TICKS - 1;
                        show->priv->fade_disabled = TRUE;
                }

                if (show->priv->fade_ticks >= N_FADE_TICKS) {
                        finish_fade (show);
                }
        }

        return TRUE;
}

static void
process_new_pixbuf (GSTESlideshow *show,
                    GdkPixbuf     *pixbuf)
{
        gs_theme_engine_profile_msg ("Processing a new image");

        if (pixbuf != NULL) {
                start_fade (show, pixbuf);
        } else {
                start_new_load (show, 10);
        }
}

static void
op_result_free (OpResult *result)
{
        if (result == NULL) {
                return;
        }

        if (result->pixbuf != NULL) {
                g_object_unref (result->pixbuf);
        }

        g_free (result);
}

static gboolean
results_pull_func (GSTESlideshow *show)
{
        OpResult *result;

        GDK_THREADS_ENTER ();

        g_async_queue_lock (show->priv->results_q);

        result = g_async_queue_try_pop_unlocked (show->priv->results_q);
        g_assert (result);

        while (result != NULL) {
                process_new_pixbuf (show, result->pixbuf);
                op_result_free (result);

                result = g_async_queue_try_pop_unlocked (show->priv->results_q);
        }

        show->priv->results_pull_id = 0;

        g_async_queue_unlock (show->priv->results_q);

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
        float      scale_factor_x = 1.0;
        float      scale_factor_y = 1.0;
        float      scale_factor = 1.0;
        GdkPixbuf *scaled = NULL;

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

        /* If the image is less than 256 wide or high then it
           is probably a thumbnail and we should ignore it */
        if (pw < 256 || ph < 256) {
                return NULL;
        }

        /* Determine which dimension requires the smallest scale. */
        scale_factor_x = (float) max_width / (float) pw;
        scale_factor_y = (float) max_height / (float) ph;

        if (scale_factor_x > scale_factor_y) {
                scale_factor = scale_factor_y;
        } else {
                scale_factor = scale_factor_x;
        }

        if (1) {
                int scale_x = (int) (pw * scale_factor);
                int scale_y = (int) (ph * scale_factor);

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
        if (d == NULL) {
                g_warning ("Could not open directory: %s", base);
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
get_pixbuf_from_local_dir (GSTESlideshow *show,
                           const char    *location)
{
        GdkPixbuf *pixbuf;
        char      *filename;
        int        i;
        GSList    *l;

        /* rebuild the cache */
        if (show->priv->filename_list == NULL) {
                show->priv->filename_list = build_filename_list_local_dir (location);
        }

        /* get a random filename */
        i = g_random_int_range (0, g_slist_length (show->priv->filename_list));
        l = g_slist_nth (show->priv->filename_list, i);
        filename = l->data;

        pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

#if HAVE_EXIF
        /* try to get exif data and rotate the image if necessary */
        if (pixbuf != NULL) {
                GdkPixbuf *rotated;

                rotated = update_from_exif_data (filename, pixbuf);
                if (rotated) {
                        g_object_unref (pixbuf);
                        pixbuf = rotated;
                }
        }
#endif /* HAVE_EXIF */

        g_free (filename);
        show->priv->filename_list = g_slist_delete_link (show->priv->filename_list, l);

        return pixbuf;
}

static GdkPixbuf *
get_pixbuf_from_location (GSTESlideshow *show,
                          const char    *location)
{
        GdkPixbuf *pixbuf = NULL;
        gboolean   is_dir;

        if (location == NULL) {
                return NULL;
        }

        is_dir = g_file_test (location, G_FILE_TEST_IS_DIR);

        if (is_dir) {
                pixbuf = get_pixbuf_from_local_dir (show, location);
        }

        return pixbuf;
}

static GdkPixbuf *
get_pixbuf (GSTESlideshow *show,
            const char    *location,
            int            width,
            int            height)
{
        GdkPixbuf *pixbuf;
        GdkPixbuf *scaled = NULL;

        if (location == NULL) {
                return NULL;
        }

        pixbuf = get_pixbuf_from_location (show, location);

        if (pixbuf != NULL) {
                scaled = scale_pixbuf (pixbuf, width, height);
                g_object_unref (pixbuf);
        }

        return scaled;
}

static void
op_load_image (GSTESlideshow *show,
               const char    *location)
{
        OpResult *op_result;
        int       window_width;
        int       window_height;

        window_width = show->priv->window_width;
        window_height = show->priv->window_height;

        op_result = g_new0 (OpResult, 1);

        op_result->pixbuf = get_pixbuf (show,
                                        location,
                                        window_width,
                                        window_height);

        GDK_THREADS_ENTER ();
        g_async_queue_lock (show->priv->results_q);
        g_async_queue_push_unlocked (show->priv->results_q, op_result);

        if (show->priv->results_pull_id == 0) {
		show->priv->results_pull_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                                                               (GSourceFunc)results_pull_func,
                                                               show, NULL);
        }

        g_async_queue_unlock (show->priv->results_q);
        GDK_THREADS_LEAVE ();
}

static gpointer
load_threadfunc (GAsyncQueue *op_q)
{
        Op *op;

        op = g_async_queue_pop (op_q);
        while (op) {
                op_load_image (op->slideshow,
                               op->location);

                if (op->slideshow != NULL) {
                        g_object_unref (op->slideshow);
                }
                g_free (op->location);
                g_free (op);

                op = g_async_queue_pop (op_q);
        }

        return NULL;
}

void
gste_slideshow_set_images_location (GSTESlideshow *show,
                                    const char    *location)
{
        g_return_if_fail (GSTE_IS_SLIDESHOW (show));

        g_free (show->priv->images_location);
        show->priv->images_location = g_strdup (location);
}

static void
gste_slideshow_set_property (GObject            *object,
                             guint               prop_id,
                             const GValue       *value,
                             GParamSpec         *pspec)
{
        GSTESlideshow *self;

        self = GSTE_SLIDESHOW (object);

        switch (prop_id) {
        case PROP_IMAGES_LOCATION:
                gste_slideshow_set_images_location (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gste_slideshow_get_property (GObject            *object,
                             guint               prop_id,
                             GValue             *value,
                             GParamSpec         *pspec)
{
        GSTESlideshow *self;

        self = GSTE_SLIDESHOW (object);

        switch (prop_id) {
        case PROP_IMAGES_LOCATION:
                g_value_set_string (value, self->priv->images_location);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gste_slideshow_real_show (GtkWidget *widget)
{
        GSTESlideshow *show = GSTE_SLIDESHOW (widget);
        int            delay;

        if (GTK_WIDGET_CLASS (parent_class)->show) {
                GTK_WIDGET_CLASS (parent_class)->show (widget);
        }

        start_new_load (show, 10);

        delay = 25;
        show->priv->timeout_id = g_timeout_add (delay, (GSourceFunc)draw_iter, show);

        if (show->priv->timer != NULL) {
                g_timer_destroy (show->priv->timer);
        }
        show->priv->timer = g_timer_new ();
}

static gboolean
gste_slideshow_real_expose (GtkWidget      *widget,
                            GdkEventExpose *event)
{
        GSTESlideshow *show = GSTE_SLIDESHOW (widget);
        gboolean       handled = FALSE;

        update_display (show);

        if (GTK_WIDGET_CLASS (parent_class)->expose_event) {
                handled = GTK_WIDGET_CLASS (parent_class)->expose_event (widget, event);
        }

        return handled;
}

static gboolean
gste_slideshow_real_configure (GtkWidget         *widget,
                               GdkEventConfigure *event)
{
        GSTESlideshow *show = GSTE_SLIDESHOW (widget);
        gboolean       handled = FALSE;
        cairo_t       *cr;

        /* resize */
        gs_theme_engine_get_window_size (GS_THEME_ENGINE (show),
                                         &show->priv->window_width,
                                         &show->priv->window_height);

        gs_theme_engine_profile_msg ("Resize to x:%d y:%d",
                                     show->priv->window_width,
                                     show->priv->window_height);

        if (show->priv->surf != NULL) {
                cairo_surface_destroy (show->priv->surf);
        }

        cr = gdk_cairo_create (widget->window);
        show->priv->surf = cairo_surface_create_similar (cairo_get_target (cr),
                                                         CAIRO_CONTENT_COLOR,
                                                         show->priv->window_width,
                                                         show->priv->window_height);
        cairo_destroy (cr);

        /* schedule a redraw */
        gtk_widget_queue_draw (widget);

        if (GTK_WIDGET_CLASS (parent_class)->configure_event) {
                handled = GTK_WIDGET_CLASS (parent_class)->configure_event (widget, event);
        }

        return handled;
}

static void
gste_slideshow_class_init (GSTESlideshowClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = gste_slideshow_finalize;
        object_class->get_property = gste_slideshow_get_property;
        object_class->set_property = gste_slideshow_set_property;

        widget_class->show = gste_slideshow_real_show;
        widget_class->expose_event = gste_slideshow_real_expose;
        widget_class->configure_event = gste_slideshow_real_configure;

        g_type_class_add_private (klass, sizeof (GSTESlideshowPrivate));

        g_object_class_install_property (object_class,
                                         PROP_IMAGES_LOCATION,
                                         g_param_spec_string ("images-location",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));

}

static void
set_colormap (GtkWidget *widget)
{
        GdkScreen   *screen;
        GdkColormap *colormap;

        screen = gtk_widget_get_screen (widget);
        colormap = gdk_screen_get_rgba_colormap (screen);
        if (colormap == NULL) {
                colormap = gdk_screen_get_rgb_colormap (screen);
        }

        gtk_widget_set_colormap (widget, colormap);
}

static void
gste_slideshow_init (GSTESlideshow *show)
{
        GError *error;

        show->priv = GSTE_SLIDESHOW_GET_PRIVATE (show);

        show->priv->images_location = g_strdup (DEFAULT_IMAGES_LOCATION);

        show->priv->op_q = g_async_queue_new ();
        show->priv->results_q = g_async_queue_new ();

        error = NULL;
        show->priv->load_thread = g_thread_create ((GThreadFunc)load_threadfunc, show->priv->op_q, FALSE, &error);
        if (show->priv->load_thread == NULL) {
                g_error ("Could not create a thread to load images: %s",
                         error->message);
                g_error_free (error);
                exit (-1);
        }

        set_colormap (GTK_WIDGET (show));
}

static void
gste_slideshow_finalize (GObject *object)
{
        GSTESlideshow *show;
        gpointer       result;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSTE_IS_SLIDESHOW (object));

        show = GSTE_SLIDESHOW (object);

        g_return_if_fail (show->priv != NULL);

        if (show->priv->surf) {
                cairo_surface_destroy (show->priv->surf);
        }

        if (show->priv->timeout_id > 0) {
                g_source_remove (show->priv->timeout_id);
                show->priv->timeout_id = 0;
        }

        if (show->priv->results_pull_id > 0) {
                g_source_remove (show->priv->results_pull_id);
        }

        if (show->priv->results_q != NULL) {
                result = g_async_queue_try_pop (show->priv->results_q);

                while (result) {
                        result = g_async_queue_try_pop (show->priv->results_q);
                }
                g_async_queue_unref (show->priv->results_q);
        }

        g_free (show->priv->images_location);
        show->priv->images_location = NULL;

        if (show->priv->timer != NULL) {
                g_timer_destroy (show->priv->timer);
        }

        G_OBJECT_CLASS (parent_class)->finalize (object);
}
