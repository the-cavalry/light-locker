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
#include "gs-monitor.h"
#include "gs-debug.h"

void
light_locker_quit (void)
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
        static gboolean     debug        = FALSE;
        static gint         lock_after_screensaver = 5;
        static gboolean     late_locking = WITH_LATE_LOCKING;
        static GOptionEntry entries []   = {
                { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
#ifdef HAVE_MIT_SAVER_EXTENSION /* Remove the flag if this feature is not supported */
                { "lock-after-screensaver", 0, 0, G_OPTION_ARG_INT, &lock_after_screensaver, N_("Lock the screen S seconds after the screensaver started"), "S" },
#ifdef WITH_LATE_LOCKING
                { "late-locking", 0, 0, G_OPTION_ARG_NONE, &late_locking, N_("Lock the screen on screensaver deactivation"), NULL },
                { "no-late-locking", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &late_locking, N_("Lock the screen on screensaver activation"), NULL },
#endif
#endif
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
        gs_debug ("initializing light-locker %s", VERSION);

        monitor = gs_monitor_new (lock_after_screensaver, late_locking);

        if (monitor == NULL) {
                exit (1);
        }

        error = NULL;
        if (! gs_monitor_start (monitor, &error)) {
                if (error) {
                        g_warning ("%s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to start screensaver");
                }
                exit (1);
        }

        gtk_main ();

        g_object_unref (monitor);

        gs_debug ("light-locker finished");

        gs_debug_shutdown ();

        return 0;
}
