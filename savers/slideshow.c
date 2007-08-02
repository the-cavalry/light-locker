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
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gs-theme-window.h"
#include "gs-theme-engine.h"
#include "gste-slideshow.h"

#include "xdg-user-dir-lookup.h"

int
main (int argc, char **argv)
{
        GSThemeEngine *engine;
        GtkWidget     *window;
        GError        *error;
        gboolean       ret;
        char          *location = NULL;
        char          *background_color = NULL;
        gboolean       sort_images = FALSE;
        gboolean       no_stretch = FALSE;
        GOptionEntry  entries [] = {
                { "location", 0, 0, G_OPTION_ARG_STRING, &location,
                  N_("Location to get images from"), N_("PATH") },
                { "background-color", 0, 0, G_OPTION_ARG_STRING, &background_color,
                  N_("Color to use for images background"), N_("\"#rrggbb\"") },
                { "sort-images", 0, 0, G_OPTION_ARG_NONE, &sort_images,
                  N_("Do not randomize pictures from location"), NULL },
                { "no-stretch", 0, 0, G_OPTION_ARG_NONE, &no_stretch,
                  N_("Do not try to stretch images on screen"), NULL },
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        error = NULL;

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

        g_chdir (g_get_home_dir ());

        g_set_prgname ("slideshow");

        window = gs_theme_window_new ();
        g_signal_connect (G_OBJECT (window), "delete-event",
                          G_CALLBACK (gtk_main_quit), NULL);

        engine = g_object_new (GSTE_TYPE_SLIDESHOW, NULL);

        if (location == NULL) {
                location = xdg_user_dir_lookup ("PICTURES");
                if (location == NULL ||
                    strcmp (location, "/tmp") == 0 ||
                    strcmp (location, g_get_home_dir ()) == 0) {
                        free (location);
                        location = g_build_filename (g_get_home_dir (), "Pictures", NULL);
                }
        }

        if (location != NULL) {
                g_object_set (engine, "images-location", location, NULL);
        }

        if (sort_images) {
                g_object_set (engine, "sort-images", sort_images, NULL);
        }

        if (background_color != NULL) {
                g_object_set (engine, "background-color", background_color, NULL);
        }

        if (no_stretch) {
                g_object_set (engine, "no-stretch", no_stretch, NULL);
        }

        gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (engine));

        gtk_widget_show (GTK_WIDGET (engine));

        gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
        gtk_widget_show (window);

        gtk_main ();

        return 0;
}
