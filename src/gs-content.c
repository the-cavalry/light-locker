/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2008 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gdk/gdk.h>

#include "gs-content.h"
#include "gs-debug.h"

#define TITLE_FONT "Sans 28"
#define MESSAGE_FONT "Sans 19"

#define BOX_WIDTH(s)    ((s)*1.2)
#define BOX_HEIGHT(s)   ((s)*0.9)
#define CURVE_WIDTH(s)  ((s)*0.9)
#define CURVE_HEIGHT(s) ((s)*0.9)
#define LINE_SIZE(s)    ((s)*0.3)
#define CURVE_RADIUS(s) ((s)*0.3)
#define CURVE_BEZIER(s) (CURVE_RADIUS(s)*0.447715)

static void
draw_lock_icon (cairo_t *cr,
                int size)
{
        cairo_translate (cr, (BOX_WIDTH(size) + LINE_SIZE(size)) / 2, (CURVE_HEIGHT(size) - BOX_HEIGHT(size)) / 2);

        cairo_set_line_width (cr, LINE_SIZE(size));
        cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

        cairo_move_to (cr, - BOX_WIDTH(size) / 2, 0);
        cairo_line_to (cr, BOX_WIDTH(size) / 2, 0);
        cairo_line_to (cr, BOX_WIDTH(size) / 2, BOX_HEIGHT(size));
        cairo_line_to (cr, - BOX_WIDTH(size) / 2, BOX_HEIGHT(size));
        cairo_close_path (cr);

        cairo_fill_preserve (cr);

        cairo_move_to (cr, -CURVE_WIDTH(size) / 2, 0);
        cairo_line_to (cr, - CURVE_WIDTH(size) / 2, - CURVE_HEIGHT(size) + CURVE_RADIUS(size));
        cairo_curve_to (cr, - CURVE_WIDTH(size) / 2, - CURVE_HEIGHT(size) + CURVE_BEZIER(size),
                            - CURVE_WIDTH(size) / 2 + CURVE_BEZIER(size), - CURVE_HEIGHT(size),
                            - CURVE_WIDTH(size) / 2 + CURVE_RADIUS(size), - CURVE_HEIGHT(size));
        cairo_line_to (cr, CURVE_WIDTH(size) / 2 - CURVE_RADIUS(size), - CURVE_HEIGHT(size));
        cairo_curve_to (cr, CURVE_WIDTH(size) / 2 - CURVE_BEZIER(size), - CURVE_HEIGHT(size),
                            CURVE_WIDTH(size) / 2, - CURVE_HEIGHT(size) + CURVE_BEZIER(size),
                            CURVE_WIDTH(size) / 2, - CURVE_HEIGHT(size) + CURVE_RADIUS(size));
        cairo_line_to (cr, CURVE_WIDTH(size) / 2, 0);

        cairo_stroke (cr);
}

void
content_draw (GtkWidget *widget,
              cairo_t   *cr)
{
        PangoContext *context;
        PangoLayout *title_layout;
        PangoLayout *sub_layout;
        PangoFontDescription *desc;
        int width, height;
        int sub_width;

        width = gdk_window_get_width (gtk_widget_get_window (widget));
        height = gdk_window_get_height (gtk_widget_get_window (widget));

        cairo_translate (cr, width / 2, height / 2);

        context = gdk_pango_context_get_for_screen (gtk_widget_get_screen (widget));

        title_layout = pango_layout_new (context);
        pango_layout_set_text (title_layout, _("This session is locked"), -1);
        desc = pango_font_description_from_string (TITLE_FONT);
        pango_layout_set_font_description (title_layout, desc);
        pango_font_description_free (desc);

        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);

        pango_cairo_update_layout (cr, title_layout);
        pango_layout_get_size (title_layout, &width, &height);

        cairo_save (cr);
        /* Adjust the translation to the middle left of the icon */
        cairo_translate (cr, - width / PANGO_SCALE / 2 - height / PANGO_SCALE, - height / PANGO_SCALE / 2);
        draw_lock_icon (cr, height / PANGO_SCALE);
        cairo_restore (cr);

        cairo_move_to (cr, - width / PANGO_SCALE / 2 + height / PANGO_SCALE, - height / PANGO_SCALE);
        pango_cairo_show_layout (cr, title_layout);

        g_object_unref (title_layout);

        sub_layout = pango_layout_new (context);
        pango_layout_set_text (sub_layout, _("You'll be redirected to the unlock dialog automatically in a few seconds"), -1);
        pango_layout_set_wrap (sub_layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width (sub_layout, width + 2 * height);
        desc = pango_font_description_from_string (MESSAGE_FONT);
        pango_layout_set_font_description (sub_layout, desc);
        pango_font_description_free (desc);

        cairo_set_source_rgba (cr, 0.6, 0.6, 0.6, 1.0);

        pango_cairo_update_layout (cr, sub_layout);
        pango_layout_get_size (sub_layout, &sub_width, NULL);

        cairo_move_to (cr, - (width + 2 * height) / PANGO_SCALE / 2, height / PANGO_SCALE);
        cairo_scale (cr, (width + 2 * height) / (gdouble)sub_width, (width + 2 * height) / (gdouble)sub_width);
        pango_cairo_show_layout (cr, sub_layout);

        g_object_unref (sub_layout);
        g_object_unref (context);
}
