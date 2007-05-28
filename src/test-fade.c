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
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-fade.h"
#include "gs-debug.h"

#ifdef HAVE_XF86VMODE_GAMMA
# include <X11/extensions/xf86vmode.h>
#endif

#define XF86_VIDMODE_NAME "XFree86-VidModeExtension"

static void
test_fade (void)
{
        GSFade *fade;
        int     reps = 2;
        int     delay = 2;

        fade = gs_fade_new ();

        while (reps-- > 0) {

                g_print ("fading out...");
                gs_fade_sync (fade, 1000);
                g_print ("done.\n");

                g_print ("fading in...");
                gs_fade_reset (fade);
                g_print ("done.\n");

                if (delay) {
                        sleep (delay);
                }
        }

        g_object_unref (fade);
}

int
main (int    argc,
      char **argv)
{
        GError *error = NULL;
        int     op, event, err;

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif
        textdomain (GETTEXT_PACKAGE);
#endif

        if (error) {
                fprintf (stderr, "%s\n", error->message);
                exit (1);
        }

        if (! gtk_init_with_args (&argc, &argv, NULL, NULL, NULL, &error)) {
                fprintf (stderr, "%s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (! XQueryExtension (GDK_DISPLAY (), XF86_VIDMODE_NAME, &op, &event, &err)) {
                g_message ("no " XF86_VIDMODE_NAME " extension");
        } else {
# ifdef HAVE_XF86VMODE_GAMMA
                int major;
                int minor;

                if (! XF86VidModeQueryVersion (GDK_DISPLAY (), &major, &minor)) {
                        g_message ("unable to get " XF86_VIDMODE_NAME " version");
                } else {
                        g_message (XF86_VIDMODE_NAME " version %d.%d", major, minor);
                }
# else /* !HAVE_XF86VMODE_GAMMA */
                g_message ("no support for display's " XF86_VIDMODE_NAME " extension");
# endif /* !HAVE_XF86VMODE_GAMMA */
        }

        gs_debug_init (TRUE, FALSE);

        test_fade ();

        gs_debug_shutdown ();

        return 0;
}
