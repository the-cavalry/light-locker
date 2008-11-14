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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-idle-monitor.h"
#include "gs-debug.h"

static gboolean
on_less_idle (GSIdleMonitor *monitor,
              guint          id,
              gboolean       condition,
              gpointer       data)
{
        g_debug ("Less idle callback condition=%d", condition);

        /* return TRUE so that the idle monitor is not reset */
        return TRUE;
}


static gboolean
on_idle (GSIdleMonitor *monitor,
         guint          id,
         gboolean       condition,
         gpointer       data)
{
        g_debug ("Idle callback condition=%d", condition);

        /* return FALSE to reset monitor
         * means that very idle callback sound never fire */

        /* this is how we implement inhibit */
        return FALSE;
}

static gboolean
on_very_idle (GSIdleMonitor *monitor,
              guint          id,
              gboolean       condition,
              gpointer       data)
{
        g_debug ("Very idle callback");

        /* return TRUE so that the idle monitor is not reset */
        return TRUE;
}

static void
test_idle_monitor (void)
{
        GSIdleMonitor *monitor;
        guint          timeout;
        guint          id;

        timeout = 5000;

        monitor = gs_idle_monitor_new ();
        id = gs_idle_monitor_add_watch (monitor,
                                        timeout / 2,
                                        on_less_idle,
                                        NULL);
        id = gs_idle_monitor_add_watch (monitor,
                                        timeout,
                                        on_idle,
                                        NULL);
        id = gs_idle_monitor_add_watch (monitor,
                                        timeout * 2,
                                        on_very_idle,
                                        NULL);
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

        test_idle_monitor ();

        gtk_main ();

        gs_debug_shutdown ();

        return 0;
}
