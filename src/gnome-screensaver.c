/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Portions derived from xscreensaver,
 * Copyright (c) 1991-2004 Jamie Zawinski <jwz@jwz.org>
 * 
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib/gi18n.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>

#include "gnome-screensaver.h"
#include "gs-monitor.h"

static gboolean
check_dbus ()
{
#define GS_LISTENER_SERVICE   "org.gnome.ScreenSaver"
        DBusConnection *connection;
        DBusError       error;
        gboolean        already_running;

        dbus_error_init (&error);
        connection = dbus_bus_get (DBUS_BUS_SESSION, &error);

        if (!connection) {
                if (dbus_error_is_set (&error))
                        dbus_error_free (&error);                
                g_warning ("Could not connect to the session message bus");
                return FALSE;
        }

        dbus_error_init (&error);
        already_running = dbus_bus_service_exists (connection, GS_LISTENER_SERVICE, &error);
        if (dbus_error_is_set (&error))
                dbus_error_free (&error);
        if (already_running)
                g_warning ("Screensaver is already running in this session");

        return !already_running;
}

void
gnome_screensaver_quit (void)
{
        gtk_main_quit ();
}

int
main (int    argc,
      char **argv)
{
        GSMonitor          *monitor;
        GError             *error = NULL;
        static gboolean     show_version = FALSE;
        static GOptionEntry entries []   = {
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version,
                  N_("Version of this application"), NULL },
                { NULL }
        };

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif 
        textdomain (GETTEXT_PACKAGE);
#endif 

        if (!check_dbus ()) {
                exit (1);
        }

        if (! gtk_init_with_args (&argc, &argv, NULL, entries, NULL, &error)) {
                g_warning ("%s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        monitor = gs_monitor_new ();

        if (!monitor)
                exit (1);

        gtk_main ();

        g_object_unref (monitor);

	return 0;
}
