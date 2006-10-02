/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 1999, 2000, 2003 Jamie Zawinski <jwz@jwz.org>
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#ifdef HAVE_LIBGL
#include <GL/gl.h>
#include <GL/glx.h>
#endif /* HAVE_GL */

#include "gs-visual-gl.h"
#include "gs-debug.h"

GdkVisual *
gs_visual_gl_get_best_for_screen (GdkScreen *screen)
{
        GdkVisual  *visual;
#ifdef HAVE_LIBGL
        GdkDisplay *display;
        int         screen_num;
        int         i;

# define R GLX_RED_SIZE
# define G GLX_GREEN_SIZE
# define B GLX_BLUE_SIZE
# define D GLX_DEPTH_SIZE
# define I GLX_BUFFER_SIZE
# define DB GLX_DOUBLEBUFFER
# define ST GLX_STENCIL_SIZE

        static int attrs [][20] = {
                { GLX_RGBA, R, 8, G, 8, B, 8, D, 8, DB, ST,1, 0 }, /* rgb double, stencil */
                { GLX_RGBA, R, 4, G, 4, B, 4, D, 4, DB, ST,1, 0 },
                { GLX_RGBA, R, 2, G, 2, B, 2, D, 2, DB, ST,1, 0 },
                { GLX_RGBA, R, 8, G, 8, B, 8, D, 8, DB,       0 }, /* rgb double */
                { GLX_RGBA, R, 4, G, 4, B, 4, D, 4, DB,       0 },
                { GLX_RGBA, R, 2, G, 2, B, 2, D, 2, DB,       0 },
                { GLX_RGBA, R, 8, G, 8, B, 8, D, 8,           0 }, /* rgb single */
                { GLX_RGBA, R, 4, G, 4, B, 4, D, 4,           0 },
                { GLX_RGBA, R, 2, G, 2, B, 2, D, 2,           0 },
                { I, 8,                       D, 8, DB,       0 }, /* cmap double */
                { I, 4,                       D, 4, DB,       0 },
                { I, 8,                       D, 8,           0 }, /* cmap single */
                { I, 4,                       D, 4,           0 },
                { GLX_RGBA, R, 1, G, 1, B, 1, D, 1,           0 }  /* monochrome */
        };

        g_return_val_if_fail (screen != NULL, NULL);

        display = gdk_screen_get_display (screen);
        screen_num = gdk_screen_get_number (screen);

        gdk_error_trap_push ();

        visual = NULL;
        for (i = 0; i < G_N_ELEMENTS (attrs); i++) {
                XVisualInfo *vi;

                vi = glXChooseVisual (GDK_DISPLAY_XDISPLAY (display), screen_num, attrs [i]);

                if (vi != NULL) {
                        VisualID   vid;

                        vid = XVisualIDFromVisual (vi->visual);

                        visual = gdkx_visual_get (vid);

                        XFree (vi);

                        if (visual != NULL) {
                                break;
                        }
                }
        }

        gdk_display_sync (display);
        gdk_error_trap_pop ();

#else
        visual = NULL;

#endif /* HAVE_LIBGL */

        return visual;
}
