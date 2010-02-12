/*
 * Copyright (C) 2005 Ray Strode <rstrode@redhat.com>,
 *                    Matthias Clasen <mclasen@redhat.com>,
 *                    Søren Sandmann <sandmann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Originally written by: Ray Strode <rstrode@redhat.com>
 *
 * Later contributions by: Matthias Clasen <mclasen@redhat.com>
 *                         Søren Sandmann <sandmann@redhat.com>
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <gtk/gtk.h>

#include "gs-theme-window.h"

#ifndef trunc
#define trunc(x) (((x) > 0.0) ? floor((x)) : -floor(-(x)))
#endif

#ifndef OPTIMAL_FRAME_RATE
#define OPTIMAL_FRAME_RATE (25.0)
#endif

#ifndef STAT_PRINT_FREQUENCY
#define STAT_PRINT_FREQUENCY (2000)
#endif

#ifndef FLOATER_MAX_SIZE
#define FLOATER_MAX_SIZE (128.0)
#endif

#ifndef FLOATER_MIN_SIZE
#define FLOATER_MIN_SIZE (16.0)
#endif
#ifndef FLOATER_DEFAULT_COUNT
#define FLOATER_DEFAULT_COUNT (5)
#endif

#ifndef SMALL_ANGLE
#define SMALL_ANGLE (0.025 * G_PI)
#endif

#ifndef BIG_ANGLE
#define BIG_ANGLE (0.125 * G_PI)
#endif

#ifndef GAMMA
#define GAMMA 2.2
#endif

static gboolean should_show_paths = FALSE;
static gboolean should_do_rotations = FALSE;
static gboolean should_print_stats = FALSE;
static gint max_floater_count = FLOATER_DEFAULT_COUNT;
static gchar *geometry = NULL;
static gchar **filenames = NULL;

static GOptionEntry options[] = {
       {"show-paths", 'p', 0, G_OPTION_ARG_NONE, &should_show_paths,
            N_("Show paths that images follow"), NULL},

       {"do-rotations", 'r', 0, G_OPTION_ARG_NONE, &should_do_rotations,
            N_("Occasionally rotate images as they move"), NULL},

       {"print-stats", 's', 0, G_OPTION_ARG_NONE, &should_print_stats,
            N_("Print out frame rate and other statistics"), NULL},

       {"number-of-images", 'n', 0, G_OPTION_ARG_INT, &max_floater_count,
             N_("The maximum number of images to keep on screen"), N_("MAX_IMAGES")},

       {"geometry", 0, 0, G_OPTION_ARG_STRING, &geometry,
             N_("The initial size and position of window"), N_("WIDTHxHEIGHT+X+Y")},

       {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames,
            N_("The source image to use"), NULL},

       {NULL}
};


typedef struct _Point Point;
typedef struct _Path Path;
typedef struct _Rectangle Rectangle;
typedef struct _ScreenSaverFloater ScreenSaverFloater;
typedef struct _CachedSource CachedSource;
typedef struct _ScreenSaver ScreenSaver;

struct _Point
{
  gdouble x, y;
};

struct _Path
{
  Point start_point;
  Point start_control_point;
  Point end_control_point;
  Point end_point;

  gdouble x_linear_coefficient,
          y_linear_coefficient;

  gdouble x_quadratic_coefficient,
          y_quadratic_coefficient;

  gdouble x_cubic_coefficient,
          y_cubic_coefficient;

  gdouble duration;
};

struct _CachedSource
{
  cairo_pattern_t *pattern;
  gint width, height;
};

struct _Rectangle
{
  Point top_left_point;
  Point bottom_right_point;
};

struct _ScreenSaverFloater
{
  GdkRectangle bounds;

  Point start_position;
  Point position;

  gdouble scale;
  gdouble opacity;

  Path *path;
  gdouble path_start_time;
  gdouble path_start_scale;
  gdouble path_end_scale;

  gdouble angle;
  gdouble angle_increment;
};

struct _ScreenSaver
{
  GtkWidget  *drawing_area;
  Rectangle canvas_rectangle;
  GHashTable *cached_sources;

  char *filename;

  gdouble first_update_time;

  gdouble last_calculated_stats_time,
          current_calculated_stats_time;
  gint update_count, frame_count;

  gdouble updates_per_second;
  gdouble frames_per_second;

  guint state_update_timeout_id;
  guint stats_update_timeout_id;

  GList *floaters;
  gint max_floater_count;

  guint should_do_rotations: 1;
  guint should_show_paths : 1;
  guint draw_ops_pending : 1;
};

static Path *path_new (Point *start_point,
                       Point *start_control_point,
                       Point *end_control_point,
                       Point *end_point,
                       gdouble duration);
static void path_free (Path *path);

static ScreenSaverFloater *screen_saver_floater_new (ScreenSaver *screen_saver,
                                                     Point       *position,
                                                     gdouble      scale);
static void screen_saver_floater_free (ScreenSaver        *screen_saver,
                                       ScreenSaverFloater *floater);
static gboolean screen_saver_floater_is_off_canvas (ScreenSaver        *screen_saver,
                                                    ScreenSaverFloater *floater);
static gboolean screen_saver_floater_should_bubble_up (ScreenSaver        *screen_saver,
                                                       ScreenSaverFloater *floater,
                                                       gdouble             performance_ratio,
                                                       gdouble            *duration);
static gboolean screen_saver_floater_should_come_on_screen (ScreenSaver   *screen_saver,
                                                            ScreenSaverFloater *floater,
                                                            gdouble             performance_ratio,
                                                            gdouble            *duration);
static Point screen_saver_floater_get_position_from_time (ScreenSaver        *screen_saver,
                                                          ScreenSaverFloater *floater,
                                                          gdouble             time);
static gdouble screen_saver_floater_get_scale_from_time (ScreenSaver        *screen_saver,
                                                         ScreenSaverFloater *floater,
                                                         gdouble             time);
static gdouble screen_saver_floater_get_angle_from_time (ScreenSaver        *screen_saver,
                                                         ScreenSaverFloater *floater,
                                                         gdouble             time);

static Path *screen_saver_floater_create_path_to_on_screen (ScreenSaver        *screen_saver,
                                                            ScreenSaverFloater *floater,
                                                            gdouble             duration);
static Path *screen_saver_floater_create_path_to_bubble_up (ScreenSaver        *screen_saver,
                                                            ScreenSaverFloater *floater,
                                                            gdouble             duration);
static Path *screen_saver_floater_create_path_to_random_point (ScreenSaver        *screen_saver,
                                                               ScreenSaverFloater *floater,
                                                               gdouble             duration);
static Path *screen_saver_floater_create_path (ScreenSaver        *screen_saver,
                                               ScreenSaverFloater *floater);
static void screen_saver_floater_update_state (ScreenSaver        *screen_saver,
                                               ScreenSaverFloater *floater,
                                               gdouble             time);

static gboolean screen_saver_floater_do_draw (ScreenSaver        *screen_saver,
                                              ScreenSaverFloater *floater,
                                              cairo_t            *context);

static CachedSource *cached_source_new (cairo_pattern_t *pattern,
                                        gint             width,
                                        gint             height);
static void cached_source_free (CachedSource *source);

static ScreenSaver *screen_saver_new (GtkDrawingArea  *drawing_area,
                                      const gchar     *filename,
                                      gint             max_floater_count,
                                      gboolean         should_do_rotations,
                                      gboolean         should_show_paths);
static void screen_saver_free (ScreenSaver *screen_saver);
static gdouble screen_saver_get_timestamp (ScreenSaver *screen_saver);
static void screen_saver_get_initial_state (ScreenSaver *screen_saver);
static void screen_saver_update_state (ScreenSaver *screen_saver,
                                       gdouble      time);
static gboolean screen_saver_do_update_state (ScreenSaver *screen_saver);
static gboolean screen_saver_do_update_stats (ScreenSaver *screen_saver);
static gdouble screen_saver_get_updates_per_second (ScreenSaver *screen_saver);
static gdouble screen_saver_get_frames_per_second (ScreenSaver *screen_saver);
static gdouble screen_saver_get_image_cache_usage (ScreenSaver *screen_saver);
static void screen_saver_create_floaters (ScreenSaver *screen_saver);
static void screen_saver_destroy_floaters (ScreenSaver *screen_saver);
static void screen_saver_on_size_allocate (ScreenSaver   *screen_saver,
                                           GtkAllocation *allocation);
static void screen_saver_on_expose_event (ScreenSaver    *screen_saver,
                                          GdkEventExpose *event);
static gboolean do_print_screen_saver_stats (ScreenSaver *screen_saver);
static GdkPixbuf *gamma_correct (const GdkPixbuf *input_pixbuf);

static CachedSource*
cached_source_new (cairo_pattern_t *pattern,
                   gint              width,
                   gint             height)
{
  CachedSource *source;

  source = g_new (CachedSource, 1);
  source->pattern = cairo_pattern_reference (pattern);
  source->width = width;
  source->height = height;

  return source;
}

static void
cached_source_free (CachedSource *source)
{
  if (source == NULL)
    return;

  cairo_pattern_destroy (source->pattern);

  g_free (source);
}

static Path *
path_new (Point *start_point,
          Point *start_control_point,
          Point *end_control_point,
          Point *end_point,
          gdouble duration)
{
  Path *path;

  path = g_new (Path, 1);
  path->start_point = *start_point;
  path->start_control_point = *start_control_point;
  path->end_control_point = *end_control_point;
  path->end_point = *end_point;
  path->duration = duration;

  /* we precompute the coefficients to the cubic bezier curve here
   * so that we don't have to do it repeatedly later The equation is:
   *
   * B(t) = A * t^3 + B * t^2 + C * t + start_point
   */
  path->x_linear_coefficient = 3 * (start_control_point->x - start_point->x);
  path->x_quadratic_coefficient = 3 * (end_control_point->x -
                                       start_control_point->x) -
                                  path->x_linear_coefficient;
  path->x_cubic_coefficient = end_point->x - start_point->x -
                              path->x_linear_coefficient -
                              path->x_quadratic_coefficient;

  path->y_linear_coefficient = 3 * (start_control_point->y - start_point->y);
  path->y_quadratic_coefficient = 3 * (end_control_point->y -
                                       start_control_point->y) -
                                  path->y_linear_coefficient;
  path->y_cubic_coefficient = end_point->y - start_point->y -
                              path->y_linear_coefficient -
                              path->y_quadratic_coefficient;
  return path;
}

static void
path_free (Path *path)
{
  g_free (path);
}

static ScreenSaverFloater*
screen_saver_floater_new (ScreenSaver *screen_saver,
                          Point       *position,
                          gdouble      scale)
{
  ScreenSaverFloater *floater;

  floater = g_new (ScreenSaverFloater, 1);
  floater->bounds.width = 0;
  floater->start_position = *position;
  floater->position = *position;
  floater->scale = scale;
  floater->opacity = pow (scale, 1.0 / GAMMA);
  floater->path = NULL;
  floater->path_start_time = 0.0;
  floater->path_start_scale = 1.0;
  floater->path_end_scale = 0.0;

  floater->angle = 0.0;
  floater->angle_increment = 0.0;

  return floater;
}

void
screen_saver_floater_free (ScreenSaver        *screen_saver,
                           ScreenSaverFloater *floater)
{
  if (floater == NULL)
    return;

  path_free (floater->path);

  g_free (floater);
}

static gboolean
screen_saver_floater_is_off_canvas (ScreenSaver        *screen_saver,
                                    ScreenSaverFloater *floater)
{
  if ((floater->position.x < screen_saver->canvas_rectangle.top_left_point.x) ||
      (floater->position.x > screen_saver->canvas_rectangle.bottom_right_point.x) ||
      (floater->position.y < screen_saver->canvas_rectangle.top_left_point.y) ||
      (floater->position.y > screen_saver->canvas_rectangle.bottom_right_point.y))
    return TRUE;

  return FALSE;
}

static gboolean
screen_saver_floater_should_come_on_screen (ScreenSaver        *screen_saver,
                                            ScreenSaverFloater *floater,
                                            gdouble             performance_ratio,
                                            gdouble            *duration)
{

  if (!screen_saver_floater_is_off_canvas (screen_saver, floater))
    return FALSE;

  if ((abs (performance_ratio - .5) >= G_MINDOUBLE) &&
      (g_random_double () > .5))
    {
      if (duration)
        *duration = g_random_double_range (3.0, 7.0);

      return TRUE;
    }

  return FALSE;
}

static gboolean
screen_saver_floater_should_bubble_up (ScreenSaver        *screen_saver,
                                       ScreenSaverFloater *floater,
                                       gdouble             performance_ratio,
                                       gdouble            *duration)
{

  if ((performance_ratio < .5) && (g_random_double () > .5))
    {
      if (duration)
        *duration = performance_ratio * 30.0;

      return TRUE;
    }

  if ((floater->scale < .3) && (g_random_double () > .6))
    {
      if (duration)
        *duration = 30.0;

      return TRUE;
    }

  return FALSE;
}

static Point
screen_saver_floater_get_position_from_time (ScreenSaver        *screen_saver,
                                             ScreenSaverFloater *floater,
                                             gdouble             time)
{
  Point point;

  time = time / floater->path->duration;

  point.x = floater->path->x_cubic_coefficient * (time * time * time) +
            floater->path->x_quadratic_coefficient * (time * time) +
            floater->path->x_linear_coefficient * (time) +
            floater->path->start_point.x;
  point.y = floater->path->y_cubic_coefficient * (time * time * time) +
            floater->path->y_quadratic_coefficient * (time * time) +
            floater->path->y_linear_coefficient * (time) +
            floater->path->start_point.y;

  return point;
}

static gdouble
screen_saver_floater_get_scale_from_time (ScreenSaver        *screen_saver,
                                          ScreenSaverFloater *floater,
                                          gdouble             time)
{
  gdouble completion_ratio, total_scale_growth, new_scale;

  completion_ratio = time / floater->path->duration;
  total_scale_growth = (floater->path_end_scale - floater->path_start_scale);
  new_scale = floater->path_start_scale + total_scale_growth * completion_ratio;

  return CLAMP (new_scale, 0.0, 1.0);
}

static gdouble
screen_saver_floater_get_angle_from_time (ScreenSaver        *screen_saver,
                                          ScreenSaverFloater *floater,
                                          gdouble             time)
{
  gdouble completion_ratio;
  gdouble total_rotation;

  completion_ratio = time / floater->path->duration;
  total_rotation = floater->angle_increment * floater->path->duration;

  return floater->angle + total_rotation * completion_ratio;
}

static Path *
screen_saver_floater_create_path_to_on_screen (ScreenSaver        *screen_saver,
                                               ScreenSaverFloater *floater,
                                               gdouble             duration)
{
  Point start_position, end_position, start_control_point, end_control_point;
  start_position = floater->position;

  end_position.x = g_random_double_range (.25, .75) *
      (screen_saver->canvas_rectangle.top_left_point.x +
       screen_saver->canvas_rectangle.bottom_right_point.x);
  end_position.y = g_random_double_range (.25, .75) *
      (screen_saver->canvas_rectangle.top_left_point.y +
       screen_saver->canvas_rectangle.bottom_right_point.y);

  start_control_point.x = start_position.x + .9 * (end_position.x - start_position.x);
  start_control_point.y = start_position.y + .9 * (end_position.y - start_position.y);

  end_control_point.x = start_position.x + 1.0 * (end_position.x - start_position.x);
  end_control_point.y = start_position.y + 1.0 * (end_position.y - start_position.y);

  return path_new (&start_position, &start_control_point, &end_control_point,
                   &end_position, duration);
}

static Path *
screen_saver_floater_create_path_to_bubble_up (ScreenSaver        *screen_saver,
                                               ScreenSaverFloater *floater,
                                               gdouble             duration)
{
  Point start_position, end_position, start_control_point, end_control_point;

  start_position = floater->position;
  end_position.x = start_position.x;
  end_position.y = screen_saver->canvas_rectangle.top_left_point.y - FLOATER_MAX_SIZE;
  start_control_point.x = .5 * start_position.x;
  start_control_point.y = .5 * start_position.y;
  end_control_point.x = 1.5 * end_position.x;
  end_control_point.y = .5 * end_position.y;

  return path_new (&start_position, &start_control_point, &end_control_point,
                   &end_position, duration);
}

static Path *
screen_saver_floater_create_path_to_random_point (ScreenSaver        *screen_saver,
                                                  ScreenSaverFloater *floater,
                                                  gdouble             duration)
{
  Point start_position, end_position, start_control_point, end_control_point;

  start_position = floater->position;

  end_position.x = start_position.x +
      (g_random_double_range (-.5, .5) * 4 * FLOATER_MAX_SIZE);
  end_position.y = start_position.y +
      (g_random_double_range (-.5, .5) * 4 * FLOATER_MAX_SIZE);

  start_control_point.x = start_position.x + .95 * (end_position.x - start_position.x);
  start_control_point.y = start_position.y + .95 * (end_position.y - start_position.y);

  end_control_point.x = start_position.x + 1.0 * (end_position.x - start_position.x);
  end_control_point.y = start_position.y + 1.0 * (end_position.y - start_position.y);

  return path_new (&start_position, &start_control_point, &end_control_point,
                   &end_position, duration);
}

static Path *
screen_saver_floater_create_path (ScreenSaver        *screen_saver,
                                  ScreenSaverFloater *floater)
{
  gdouble performance_ratio;
  gdouble duration;

  performance_ratio =
    screen_saver_get_frames_per_second (screen_saver) / OPTIMAL_FRAME_RATE;

  if (abs (performance_ratio) <= G_MINDOUBLE)
    performance_ratio = 1.0;

  if (screen_saver_floater_should_bubble_up (screen_saver, floater, performance_ratio, &duration))
    return screen_saver_floater_create_path_to_bubble_up (screen_saver, floater, duration);

  if (screen_saver_floater_should_come_on_screen (screen_saver, floater, performance_ratio, &duration))
    return screen_saver_floater_create_path_to_on_screen (screen_saver, floater, duration);

  return screen_saver_floater_create_path_to_random_point (screen_saver, floater,
                                                           g_random_double_range (3.0, 7.0));
}

static void
screen_saver_floater_update_state (ScreenSaver        *screen_saver,
                                   ScreenSaverFloater *floater,
                                   gdouble             time)
{
  gdouble performance_ratio;

  performance_ratio =
    screen_saver_get_frames_per_second (screen_saver) / OPTIMAL_FRAME_RATE;

  if (floater->path == NULL)
    {
      floater->path = screen_saver_floater_create_path (screen_saver, floater);
      floater->path_start_time = time;

      floater->path_start_scale = floater->scale;

      if (g_random_double () > .5)
        floater->path_end_scale = g_random_double_range (0.10, performance_ratio);

      /* poor man's distribution */
      if (screen_saver->should_do_rotations &&
          (g_random_double () < .75 * performance_ratio))
        {
          gint r;

          r = g_random_int_range (0, 100);
          if (r < 80)
            floater->angle_increment = 0.0;
          else if (r < 95)
            floater->angle_increment = g_random_double_range (-SMALL_ANGLE, SMALL_ANGLE);
          else
            floater->angle_increment = g_random_double_range (-BIG_ANGLE, BIG_ANGLE);
        }
    }

  if (time < (floater->path_start_time + floater->path->duration))
    {
      gdouble path_time;

      path_time = time - floater->path_start_time;

      floater->position =
          screen_saver_floater_get_position_from_time (screen_saver, floater,
                                                       path_time);
      floater->scale =
          screen_saver_floater_get_scale_from_time (screen_saver, floater, path_time);

      floater->angle =
          screen_saver_floater_get_angle_from_time (screen_saver, floater, path_time);

      floater->opacity = pow (floater->scale, 1.0 / GAMMA);
    }
  else
    {
      path_free (floater->path);

      floater->path = NULL;
      floater->path_start_time = 0.0;
    }
}

static GdkPixbuf *
gamma_correct (const GdkPixbuf *input_pixbuf)
{
    gint x, y, width, height, rowstride;
    GdkPixbuf *output_pixbuf;
    guchar *pixels;

    output_pixbuf = gdk_pixbuf_copy (input_pixbuf);
    pixels = gdk_pixbuf_get_pixels (output_pixbuf);

    width = gdk_pixbuf_get_width (output_pixbuf);
    height = gdk_pixbuf_get_height (output_pixbuf);
    rowstride = gdk_pixbuf_get_rowstride (output_pixbuf);

    for (y = 0; y < height; y++)
      for (x = 0; x < width; x++)
        {
          guchar *alpha_channel;
          guchar opacity;

          alpha_channel = pixels + y * (rowstride / 4) + x + 3;
          opacity = (guchar) (255 * pow ((*alpha_channel / 255.0), 1.0 / GAMMA));

          *alpha_channel = opacity;
        }

    return output_pixbuf;
}

static gboolean
screen_saver_floater_do_draw (ScreenSaver        *screen_saver,
                              ScreenSaverFloater *floater,
                              cairo_t            *context)
{
  gint size;
  CachedSource *source;

  size = CLAMP ((int) (FLOATER_MAX_SIZE * floater->scale),
                FLOATER_MIN_SIZE, FLOATER_MAX_SIZE);

  source = g_hash_table_lookup (screen_saver->cached_sources, GINT_TO_POINTER (size));

  if (source == NULL)
    {
      GdkPixbuf *pixbuf;
      GError *error;

      pixbuf = NULL;
      error = NULL;

      pixbuf = gdk_pixbuf_new_from_file_at_size (screen_saver->filename, size, -1,
                                                 &error);
      if (pixbuf == NULL)
        {
          g_assert (error != NULL);
          g_printerr ("%s", _(error->message));
          g_error_free (error);
          return FALSE;
        }

      if (gdk_pixbuf_get_has_alpha (pixbuf))
          gamma_correct (pixbuf);

      gdk_cairo_set_source_pixbuf (context, pixbuf, 0.0, 0.0);

      source = cached_source_new (cairo_get_source (context),
                                  gdk_pixbuf_get_width (pixbuf),
                                  gdk_pixbuf_get_height (pixbuf));
      g_object_unref (pixbuf);
      g_hash_table_insert (screen_saver->cached_sources, GINT_TO_POINTER (size),
                           source);
    }

  cairo_save (context);

  if (screen_saver->should_do_rotations && (abs (floater->angle) > G_MINDOUBLE))
    {
      floater->bounds.width = G_SQRT2 * source->width + 2;
      floater->bounds.height = G_SQRT2 * source->height + 2;
      floater->bounds.x = (int) (floater->position.x - .5 * G_SQRT2 * source->width) - 1;
      floater->bounds.y = (int) (floater->position.y - .5 * G_SQRT2 * source->height) - 1;

      cairo_translate (context,
                       trunc (floater->position.x),
                       trunc (floater->position.y));
      cairo_rotate (context, floater->angle);
      cairo_translate (context,
                       -trunc (floater->position.x),
                       -trunc (floater->position.y));
    }
  else
    {
      floater->bounds.width = source->width + 2;
      floater->bounds.height = source->height + 2;
      floater->bounds.x = (int) (floater->position.x - .5 * source->width) - 1;
      floater->bounds.y = (int) (floater->position.y - .5 * source->height) - 1;
    }

  cairo_translate (context,
                   trunc (floater->position.x - .5 * source->width),
                   trunc (floater->position.y - .5 * source->height));

  cairo_set_source (context, source->pattern);

  cairo_rectangle (context,
                   trunc (.5 * (source->width - floater->bounds.width)),
                   trunc (.5 * (source->height - floater->bounds.height)),
                   floater->bounds.width, floater->bounds.height);

  cairo_clip (context);
  cairo_paint_with_alpha (context, floater->opacity);
  cairo_restore (context);

  if (screen_saver->should_show_paths && (floater->path != NULL))
    {
      gdouble dash_pattern[] = { 5.0 };
      gint size;

      size = CLAMP ((int) (FLOATER_MAX_SIZE * floater->path_start_scale),
                    FLOATER_MIN_SIZE, FLOATER_MAX_SIZE);

      cairo_save (context);
      cairo_set_source_rgba (context, 1.0, 1.0, 1.0, .2 * floater->opacity);
      cairo_move_to (context,
                     floater->path->start_point.x,
                     floater->path->start_point.y);
      cairo_curve_to (context,
                      floater->path->start_control_point.x,
                      floater->path->start_control_point.y,
                      floater->path->end_control_point.x,
                      floater->path->end_control_point.y,
                      floater->path->end_point.x,
                      floater->path->end_point.y);
      cairo_set_line_cap (context, CAIRO_LINE_CAP_ROUND);
      cairo_stroke (context);
      cairo_set_source_rgba (context, 1.0, 0.0, 0.0, .5 * floater->opacity);
      cairo_rectangle (context,
                       floater->path->start_point.x - 3,
                       floater->path->start_point.y - 3,
                       6, 6);
      cairo_fill (context);
      cairo_set_source_rgba (context, 0.0, 0.5, 0.0, .5 * floater->opacity);
      cairo_arc (context,
                 floater->path->start_control_point.x,
                 floater->path->start_control_point.y,
                 3, 0.0, 2.0 * G_PI);
      cairo_stroke (context);
      cairo_set_source_rgba (context, 0.5, 0.0, 0.5, .5 * floater->opacity);
      cairo_arc (context,
                 floater->path->end_control_point.x,
                 floater->path->end_control_point.y,
                 3, 0.0, 2.0 * G_PI);
      cairo_stroke (context);
      cairo_set_source_rgba (context, 0.0, 0.0, 1.0, .5 * floater->opacity);
      cairo_rectangle (context,
                       floater->path->end_point.x - 3,
                       floater->path->end_point.y - 3,
                       6, 6);
      cairo_fill (context);

      cairo_set_dash (context, dash_pattern, G_N_ELEMENTS (dash_pattern), 0);
      cairo_set_source_rgba (context, .5, .5, .5, .2 * floater->scale);
      cairo_move_to (context, floater->path->start_point.x,
                     floater->path->start_point.y);
      cairo_line_to (context, floater->path->start_control_point.x,
                     floater->path->start_control_point.y);
      cairo_stroke (context);

      cairo_move_to (context, floater->path->end_point.x,
                     floater->path->end_point.y);
      cairo_line_to (context, floater->path->end_control_point.x,
                     floater->path->end_control_point.y);
      cairo_stroke (context);

      cairo_restore (context);
    }

  return TRUE;
}

static ScreenSaver *
screen_saver_new (GtkDrawingArea  *drawing_area,
                  const gchar     *filename,
                  gint             max_floater_count,
                  gboolean         should_do_rotations,
                  gboolean         should_show_paths)
{
  ScreenSaver *screen_saver;

  screen_saver = g_new (ScreenSaver, 1);
  screen_saver->filename = g_strdup (filename);
  screen_saver->drawing_area = GTK_WIDGET (drawing_area);
  screen_saver->cached_sources =
      g_hash_table_new_full (NULL, NULL, NULL,
                             (GDestroyNotify) cached_source_free);

  g_signal_connect_swapped (G_OBJECT (drawing_area), "size-allocate",
                            G_CALLBACK (screen_saver_on_size_allocate),
                            screen_saver);

  g_signal_connect_swapped (G_OBJECT (drawing_area), "expose-event",
                            G_CALLBACK (screen_saver_on_expose_event),
                            screen_saver);

  screen_saver->first_update_time = 0.0;
  screen_saver->current_calculated_stats_time = 0.0;
  screen_saver->last_calculated_stats_time = 0.0;
  screen_saver->update_count = 0;
  screen_saver->frame_count = 0;
  screen_saver->updates_per_second = 0.0;
  screen_saver->frames_per_second = 0.0;
  screen_saver->floaters = NULL;
  screen_saver->max_floater_count = max_floater_count;

  screen_saver->should_show_paths = should_show_paths;
  screen_saver->should_do_rotations = should_do_rotations;

  screen_saver_get_initial_state (screen_saver);

  screen_saver->state_update_timeout_id =
      g_timeout_add (1000 / (2.0 * OPTIMAL_FRAME_RATE),
                     (GSourceFunc) screen_saver_do_update_state, screen_saver);

  screen_saver->stats_update_timeout_id =
      g_timeout_add (1000, (GSourceFunc) screen_saver_do_update_stats,
                     screen_saver);

  return screen_saver;
}

static void
screen_saver_free (ScreenSaver *screen_saver)
{
  if (screen_saver == NULL)
    return;

  g_free (screen_saver->filename);

  g_hash_table_destroy (screen_saver->cached_sources);

  if (screen_saver->state_update_timeout_id != 0)
    g_source_remove (screen_saver->state_update_timeout_id);

  if (screen_saver->stats_update_timeout_id != 0)
    g_source_remove (screen_saver->stats_update_timeout_id);

  screen_saver_destroy_floaters (screen_saver);

  g_free (screen_saver);
}

static gdouble
screen_saver_get_timestamp (ScreenSaver *screen_saver)
{
  const gdouble microseconds_per_second = (gdouble ) G_USEC_PER_SEC;
  gdouble timestamp;
  GTimeVal now = { 0L, /* zero-filled */ };

  g_get_current_time (&now);
  timestamp = ((microseconds_per_second * now.tv_sec) + now.tv_usec) /
              microseconds_per_second;

  return timestamp;
}

static void
screen_saver_create_floaters (ScreenSaver *screen_saver)
{
  gint i;

  for (i = 0; i < screen_saver->max_floater_count; i++)
    {
      ScreenSaverFloater *floater;
      Point position;
      gdouble scale;

      position.x = g_random_double_range (screen_saver->canvas_rectangle.top_left_point.x,
                                          screen_saver->canvas_rectangle.bottom_right_point.x);
      position.y = g_random_double_range (screen_saver->canvas_rectangle.top_left_point.y,
                                          screen_saver->canvas_rectangle.bottom_right_point.y);

      scale = g_random_double ();

      floater = screen_saver_floater_new (screen_saver, &position, scale);

      screen_saver->floaters = g_list_prepend (screen_saver->floaters,
                                               floater);
    }
}

static gdouble
screen_saver_get_updates_per_second (ScreenSaver *screen_saver)
{
  return screen_saver->updates_per_second;
}

static gdouble
screen_saver_get_frames_per_second (ScreenSaver *screen_saver)
{
  return screen_saver->frames_per_second;
}

static gdouble
screen_saver_get_image_cache_usage (ScreenSaver *screen_saver)
{
  static const gdouble cache_capacity = (FLOATER_MAX_SIZE - FLOATER_MIN_SIZE + 1);

  return g_hash_table_size (screen_saver->cached_sources) / cache_capacity;
}

static void
screen_saver_destroy_floaters (ScreenSaver *screen_saver)
{
  if (screen_saver->floaters == NULL)
    return;

  g_list_foreach (screen_saver->floaters, (GFunc) screen_saver_floater_free,
                  NULL);
  g_list_free (screen_saver->floaters);

  screen_saver->floaters = NULL;
}

static void
screen_saver_on_size_allocate (ScreenSaver   *screen_saver,
                               GtkAllocation *allocation)
{
  Rectangle canvas_rectangle;

  canvas_rectangle.top_left_point.x = allocation->x - .1 * allocation->width;
  canvas_rectangle.top_left_point.y = allocation->y - .1 * allocation->height;

  canvas_rectangle.bottom_right_point.x = allocation->x + (1.1 * allocation->width);
  canvas_rectangle.bottom_right_point.y = allocation->y + (1.1 * allocation->height);

  screen_saver->canvas_rectangle = canvas_rectangle;
}

static gint
compare_floaters (ScreenSaverFloater *a,
                  ScreenSaverFloater *b)
{
  if (a->scale > b->scale)
    return 1;
  else if (abs (a->scale - b->scale) <= G_MINDOUBLE)
    return 0;
  else
    return -1;
}

static void
screen_saver_on_expose_event (ScreenSaver    *screen_saver,
                              GdkEventExpose *event)
{
  GList *tmp;
  cairo_t *context;

  if (screen_saver->floaters == NULL)
    screen_saver_create_floaters (screen_saver);

  context = gdk_cairo_create (screen_saver->drawing_area->window);

  cairo_rectangle (context,
                   (double) event->area.x,
                   (double) event->area.y,
                   (double) event->area.width,
                   (double) event->area.height);
  cairo_clip (context);

  screen_saver->floaters = g_list_sort (screen_saver->floaters,
                                        (GCompareFunc)compare_floaters);

  for (tmp = screen_saver->floaters; tmp != NULL; tmp = tmp->next)
    {
      ScreenSaverFloater *floater;
      GdkRectangle rect;
      gint size;

      floater = (ScreenSaverFloater *) tmp->data;

      size = CLAMP ((int) (FLOATER_MAX_SIZE * floater->scale),
		    FLOATER_MIN_SIZE, FLOATER_MAX_SIZE);

      rect.x = (int) (floater->position.x - .5 * G_SQRT2 * size);
      rect.y = (int) (floater->position.y - .5 * G_SQRT2 * size);
      rect.width = G_SQRT2 * size;
      rect.height = G_SQRT2 * size;

      if (!gdk_region_rect_in (event->region, &rect))
        continue;

      if (!screen_saver_floater_do_draw (screen_saver, floater, context))
        {
          gtk_main_quit ();
          break;
        }
    }

  cairo_destroy (context);

  screen_saver->draw_ops_pending = TRUE;
  screen_saver->frame_count++;
}

static void
screen_saver_update_state (ScreenSaver *screen_saver,
                           gdouble      time)
{
  GList *tmp;

  tmp = screen_saver->floaters;
  while (tmp != NULL)
    {
      ScreenSaverFloater *floater;
      floater = (ScreenSaverFloater *) tmp->data;

      screen_saver_floater_update_state (screen_saver, floater, time);

      if (GTK_WIDGET_REALIZED (screen_saver->drawing_area)
          && (floater->bounds.width > 0) && (floater->bounds.height > 0))
        {
          gint size;
          size = CLAMP ((int) (FLOATER_MAX_SIZE * floater->scale),
                        FLOATER_MIN_SIZE, FLOATER_MAX_SIZE);

          gtk_widget_queue_draw_area (screen_saver->drawing_area,
                                      floater->bounds.x,
                                      floater->bounds.y,
                                      floater->bounds.width,
                                      floater->bounds.height);

          /* the edges could concievably be spread across two
           * pixels so we add +2 to invalidated region
           */
          if (screen_saver->should_do_rotations)
            gtk_widget_queue_draw_area (screen_saver->drawing_area,
                                        (int) (floater->position.x -
                                               .5 * G_SQRT2 * size),
                                        (int) (floater->position.y -
                                               .5 * G_SQRT2 * size),
                                        G_SQRT2 * size + 2,
                                        G_SQRT2 * size + 2);
          else
            gtk_widget_queue_draw_area (screen_saver->drawing_area,
                                        (int) (floater->position.x -
                                               .5 * size),
                                        (int) (floater->position.y -
                                               .5 * size),
                                        size + 2, size + 2);

          if  (screen_saver->should_show_paths)
            gtk_widget_queue_draw (screen_saver->drawing_area);
        }

      tmp = tmp->next;
    }
}

static void
screen_saver_get_initial_state (ScreenSaver *screen_saver)
{
  screen_saver->first_update_time = screen_saver_get_timestamp (screen_saver);
  screen_saver_update_state (screen_saver, 0.0);
}

static gboolean
screen_saver_do_update_state (ScreenSaver *screen_saver)
{
  gdouble current_update_time;

  /* flush pending requests to the X server and block for
   * replies before proceeding to help prevent the X server from
   * getting overrun with requests
   */
  if (screen_saver->draw_ops_pending)
    {
      gdk_flush ();
      screen_saver->draw_ops_pending = FALSE;
    }

  current_update_time = screen_saver_get_timestamp (screen_saver);
  screen_saver_update_state (screen_saver, current_update_time -
                                           screen_saver->first_update_time);
  screen_saver->update_count++;
  return TRUE;
}

static gboolean
screen_saver_do_update_stats (ScreenSaver *screen_saver)
{
  gdouble last_calculated_stats_time, seconds_since_last_stats_update;

  last_calculated_stats_time = screen_saver->current_calculated_stats_time;
  screen_saver->current_calculated_stats_time =
      screen_saver_get_timestamp (screen_saver);
  screen_saver->last_calculated_stats_time = last_calculated_stats_time;

  if (abs (last_calculated_stats_time) <= G_MINDOUBLE)
    return TRUE;

  seconds_since_last_stats_update =
      screen_saver->current_calculated_stats_time - last_calculated_stats_time;

  screen_saver->updates_per_second =
      screen_saver->update_count / seconds_since_last_stats_update;
  screen_saver->frames_per_second =
      screen_saver->frame_count / seconds_since_last_stats_update;

  screen_saver->update_count = 0;
  screen_saver->frame_count = 0;

  return TRUE;
}

static gboolean
do_print_screen_saver_stats (ScreenSaver *screen_saver)
{

  g_print ("updates per second: %.2f, frames per second: %.2f, "
           "image cache %.0f%% full\n",
           screen_saver_get_updates_per_second (screen_saver),
           screen_saver_get_frames_per_second (screen_saver),
           screen_saver_get_image_cache_usage (screen_saver) * 100.0);

  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  ScreenSaver *screen_saver;
  GtkWidget *window;
  GtkWidget *drawing_area;

  GtkStateType state;

  GError *error;

  error = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gtk_init_with_args (&argc, &argv,
                      /* translators: the word "image" here
                       * represents a command line argument
                       */
                      _("image - floats images around the screen"),
                      options, GETTEXT_PACKAGE, &error);


  if (error != NULL)
    {
      g_printerr (_("%s. See --help for usage information.\n"),
                  _(error->message));
      g_error_free (error);
      return EX_SOFTWARE;
    }

  if ((filenames == NULL) || (filenames[0] == NULL) ||
      (filenames[1] != NULL))
    {
      g_printerr (_("You must specify one image.  See --help for usage "
                    "information.\n"));
      return EX_USAGE;
    }

  window = gs_theme_window_new ();

  g_signal_connect (G_OBJECT (window), "delete-event",
                    G_CALLBACK (gtk_main_quit), NULL);

  drawing_area = gtk_drawing_area_new ();

  state = (GtkStateType) 0;
  while (state < (GtkStateType) G_N_ELEMENTS (drawing_area->style->bg))
    {
      gtk_widget_modify_bg (drawing_area, state, &drawing_area->style->mid[state]);
      state++;
    }

  gtk_widget_show (drawing_area);
  gtk_container_add (GTK_CONTAINER (window), drawing_area);

  screen_saver = screen_saver_new (GTK_DRAWING_AREA (drawing_area),
                                   filenames[0], max_floater_count,
                                   should_do_rotations, should_show_paths);
  g_strfreev (filenames);

  if (should_print_stats)
    g_timeout_add (STAT_PRINT_FREQUENCY,
                   (GSourceFunc) do_print_screen_saver_stats,
                   screen_saver);

  if ((geometry == NULL)
      || !gtk_window_parse_geometry (GTK_WINDOW (window), geometry))
    gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);

  gtk_widget_show (window);

  gtk_main ();

  screen_saver_free (screen_saver);

  return EX_OK;
}
