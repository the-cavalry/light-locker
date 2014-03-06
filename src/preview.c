/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "light-locker.h"
#include "gs-content.h"
#include "gs-debug.h"

static void
preview_quit (void)
{
        gtk_main_quit ();
}

static void
content_draw_cb (GtkWidget *widget,
                 cairo_t   *cr,
                 gpointer   user_data)
{
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
        cairo_paint (cr);

        content_draw (widget, cr);
}

#if !GTK_CHECK_VERSION(3, 0, 0)
static void
content_expose_cb (GtkWidget *widget,
                  GdkEvent  *event,
                  gpointer   user_data)
{
        cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (widget));

        content_draw_cb (widget, cr, user_data);
        cairo_destroy (cr);
}
#endif

int
main (int    argc,
      char **argv)
{
        GtkWidget          *window;
        GtkWidget          *drawing_area;
        GError             *error = NULL;
        static gboolean     show_version = FALSE;
        static gboolean     debug        = FALSE;
	static gint         width        = 640;
	static gint         height       = 480;
        static GOptionEntry entries []   = {
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "width", 'w', 0, G_OPTION_ARG_INT, &width, N_("Preview screen width"), NULL },
                { "height", 'h', 0, G_OPTION_ARG_INT, &height, N_("Preview screen height"), NULL },
                { NULL }
        };

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif
        textdomain (GETTEXT_PACKAGE);
#endif

        if (! gtk_init_with_args (&argc, &argv, NULL, entries, NULL, &error)) {
                if (error) {
                        g_warning ("%s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to initialize GTK+");
                }
                exit (1);
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        gs_debug_init (debug, FALSE);
        gs_debug ("initializing preview %s", VERSION);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), width, height);

        g_signal_connect (window,
                          "destroy",
                          G_CALLBACK (preview_quit),
                          NULL);

	drawing_area = gtk_drawing_area_new ();
	gtk_widget_show (drawing_area);
	gtk_widget_set_app_paintable (drawing_area, TRUE);
	gtk_container_add (GTK_CONTAINER (window), drawing_area);

#if GTK_CHECK_VERSION(3, 0, 0)
        g_signal_connect_object (drawing_area, "draw",
                                 G_CALLBACK (content_draw_cb), NULL, 0);
#else
        g_signal_connect_object (drawing_area, "expose-event",
                                 G_CALLBACK (content_expose_cb), NULL, 0);
#endif

	gtk_widget_show (window);

        gtk_main ();

        gs_debug ("preview finished");

        return 0;
}
