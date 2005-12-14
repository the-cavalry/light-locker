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

#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-theme-engine.h"
#include "gste-popsquares.h"

static GMainLoop *main_loop = NULL;

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

        gdk_window_clear (window);

        gdk_window_show (window);

        return window;
}

static void
do_event (GdkEvent      *event,
          GSThemeEngine *engine)
{
        gs_theme_engine_event (engine, event);
}

int
main (int argc, char **argv)
{
        GSThemeEngine *engine;
        GdkWindow     *window;

        gtk_init (&argc, &argv);
        
        window = get_window ();

        g_set_prgname ("popsquares");

        engine = g_object_new (GSTE_TYPE_POPSQUARES, NULL);
        gs_theme_engine_set_window (engine, window);

        gdk_event_handler_set ((GdkEventFunc)do_event, engine, NULL);

        gs_theme_engine_show (engine);

        main_loop = g_main_loop_new (NULL, FALSE);
        GDK_THREADS_LEAVE ();
        g_main_loop_run (main_loop);
        GDK_THREADS_ENTER ();

        g_main_loop_unref (main_loop);

        gs_theme_engine_destroy (engine);

        return 0;
}
