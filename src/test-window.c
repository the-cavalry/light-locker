/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-window.h"
#include "gs-grab.h"
#include "gs-debug.h"

static GSGrab *grab = NULL;

static void
window_deactivated_cb (GSWindow  *window,
                       gpointer   data)
{
        gs_window_destroy (window);
}

static void
window_dialog_up_changed_cb (GSWindow  *window,
                             gpointer   data)
{
}

static void
window_show_cb (GSWindow  *window,
                gpointer   data)
{
        /* Grab keyboard so dialog can be used */
        gs_grab_move_to_window (grab,
                                gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);

}

static gboolean
window_activity_cb (GSWindow  *window,
                    gpointer   data)
{
        gs_window_request_unlock (window);

        return TRUE;
}

static void
disconnect_window_signals (GSWindow *window)
{
        gpointer data;

        data = NULL;
        g_signal_handlers_disconnect_by_func (window, window_activity_cb, data);
        g_signal_handlers_disconnect_by_func (window, window_deactivated_cb, data);
        g_signal_handlers_disconnect_by_func (window, window_dialog_up_changed_cb, data);
        g_signal_handlers_disconnect_by_func (window, window_show_cb, data);
}

static void
window_destroyed_cb (GtkWindow *window,
                     gpointer   data)
{
        disconnect_window_signals (GS_WINDOW (window));
        gs_grab_release (grab);
        gtk_main_quit ();
}

static void
connect_window_signals (GSWindow *window)
{
        gpointer data;

        data = NULL;

        g_signal_connect_object (window, "activity",
                                 G_CALLBACK (window_activity_cb), data, 0);
        g_signal_connect_object (window, "destroy",
                                 G_CALLBACK (window_destroyed_cb), data, 0);
        g_signal_connect_object (window, "deactivated",
                                 G_CALLBACK (window_deactivated_cb), data, 0);
        g_signal_connect_object (window, "notify::dialog-up",
                                 G_CALLBACK (window_dialog_up_changed_cb), data, 0);
        g_signal_connect_object (window, "show",
                                 G_CALLBACK (window_show_cb), data, 0);
}

static void
test_window (void)
{
        GSWindow  *window;
        gboolean   lock_active;
        gboolean   user_switch_enabled;
        GdkScreen *screen;
        int        monitor;

        lock_active = TRUE;
        user_switch_enabled = TRUE;
        screen = gdk_screen_get_default ();
        monitor = 0;

        window = gs_window_new (screen, monitor, lock_active);

        gs_window_set_user_switch_enabled (window, user_switch_enabled);

        connect_window_signals (window);

        gs_window_show (window);
}

int
main (int    argc,
      char **argv)
{
        GError *error = NULL;

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif
        textdomain (GETTEXT_PACKAGE);
#endif

        if (! gtk_init_with_args (&argc, &argv, NULL, NULL, NULL, &error)) {
                fprintf (stderr, "%s", error->message);
                g_error_free (error);
                exit (1);
        }

        gs_debug_init (TRUE, FALSE);

        grab = gs_grab_new ();

        test_window ();

        /* safety valve in case we can't authenticate */
        g_timeout_add (30000, (GSourceFunc)gtk_main_quit, NULL);

        gtk_main ();

        g_object_unref (grab);

        gs_debug_shutdown ();

        return 0;
}
