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

#include <gdk/gdk.h>

#include "gs-content.h"
#include "gs-debug.h"

#define MESSAGE_FONT "Sans Bold 28"

void
content_draw_cb (GtkWidget *widget,
                 cairo_t   *cr,
                 gpointer   user_data)
{
        PangoContext *context;
        PangoLayout *layout;
        PangoFontDescription *desc;
        int width, height;

        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
        cairo_paint (cr);

        width = gdk_window_get_width (gtk_widget_get_window (widget));
        height = gdk_window_get_height (gtk_widget_get_window (widget));

        cairo_translate (cr, width / 2, height / 2);

        context = gdk_pango_context_get_for_screen (gtk_widget_get_screen (widget));

        layout = pango_layout_new (context);
        pango_layout_set_text (layout, "Bye bye", -1);
        desc = pango_font_description_from_string (MESSAGE_FONT);
        pango_layout_set_font_description (layout, desc);
        pango_font_description_free (desc);

        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);

        pango_cairo_update_layout (cr, layout);
        pango_layout_get_size (layout, &width, &height);

        cairo_move_to (cr, - width / PANGO_SCALE / 2, - height / PANGO_SCALE / 2);
        pango_cairo_show_layout (cr, layout);

        g_object_unref (layout);
        g_object_unref (context);
}
