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

#include "gs-watcher.h"
#include "gs-debug.h"

static gboolean
watcher_idle_cb (GSWatcher *watcher,
                 gboolean   is_idle,
                 gpointer   data)
{
        g_message ("Idle status changed: %s", is_idle ? "idle" : "not idle");

        /* return FALSE so that the idle watcher continues */
        return FALSE;
}

static gboolean
watcher_idle_notice_cb (GSWatcher *watcher,
                        gboolean   is_idle,
                        gpointer   data)
{
        g_message ("Idle notice status changed: %s", is_idle ? "idle" : "not idle");

        return TRUE;
}

static void
connect_watcher_signals (GSWatcher *watcher)
{
        g_signal_connect (watcher, "idle-changed",
                          G_CALLBACK (watcher_idle_cb), NULL);
        g_signal_connect (watcher, "idle-notice-changed",
                          G_CALLBACK (watcher_idle_notice_cb), NULL);
}

static void
test_watcher (void)
{
        GSWatcher *watcher;

        watcher = gs_watcher_new ();
        gs_watcher_set_enabled (watcher, TRUE);
        gs_watcher_set_active (watcher, TRUE);

        connect_watcher_signals (watcher);
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

        test_watcher ();

        gtk_main ();

        gs_debug_shutdown ();

        return 0;
}
