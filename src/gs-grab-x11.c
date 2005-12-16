/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
#include <unistd.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gs-window.h"
#include "gs-grab.h"
#include "gs-debug.h"

gboolean   mouse_hide_cursor = FALSE;
GdkWindow *mouse_grab_window = NULL;
GdkWindow *keyboard_grab_window = NULL;
GdkScreen *mouse_grab_screen = NULL;
GdkScreen *keyboard_grab_screen = NULL;

static GdkCursor *
get_cursor (void)
{
        GdkBitmap *empty_bitmap;
        GdkCursor *cursor;
        GdkColor   useless;
        char       invisible_cursor_bits [] = { 0x0 };

        useless.red = useless.green = useless.blue = 0;
        useless.pixel = 0;

        empty_bitmap = gdk_bitmap_create_from_data (NULL,
                                                    invisible_cursor_bits,
                                                    1, 1);

        cursor = gdk_cursor_new_from_pixmap (empty_bitmap,
                                             empty_bitmap,
                                             &useless,
                                             &useless, 0, 0);

        g_object_unref (empty_bitmap);

        return cursor;
}

static const char *
grab_string (int status)
{
        switch (status) {
        case GDK_GRAB_SUCCESS:          return "GrabSuccess";
        case GDK_GRAB_ALREADY_GRABBED:  return "AlreadyGrabbed";
        case GDK_GRAB_INVALID_TIME:     return "GrabInvalidTime";
        case GDK_GRAB_NOT_VIEWABLE:     return "GrabNotViewable";
        case GDK_GRAB_FROZEN:           return "GrabFrozen";
        default:
                {
                        static char foo [255];
                        sprintf (foo, "unknown status: %d", status);
                        return foo;
                }
        }
}

void
gs_grab_window (GdkWindow *window,
                GdkScreen *screen,
                gboolean   hide_cursor)
{
        gboolean result = FALSE;

        do
                result = gs_grab_move_keyboard (window, screen);
        while (!result);

        do
                result = gs_grab_move_mouse (window, screen, hide_cursor);
        while (!result);
}

static int
gs_grab_get_keyboard (GdkWindow *window,
                      GdkScreen *screen)
{
        GdkGrabStatus status;

        g_return_val_if_fail (window != NULL, FALSE);
        g_return_val_if_fail (screen != NULL, FALSE);

        gs_debug ("Grabbing keyboard widget=%X", (guint32) GDK_WINDOW_XID (window));
        status = gdk_keyboard_grab (window, FALSE, GDK_CURRENT_TIME);

        if (status == GDK_GRAB_SUCCESS) {
                keyboard_grab_window = window;
                keyboard_grab_screen = screen;
        }

        if (status != GDK_GRAB_SUCCESS) {
                g_warning ("Couldn't grab keyboard!  (%s)", grab_string (status));
        }

        return status;
}

static int
gs_grab_get_mouse (GdkWindow *window,
                   GdkScreen *screen,
                   gboolean   hide_cursor)
{
        GdkGrabStatus status;
        GdkCursor    *cursor = get_cursor ();

        g_return_val_if_fail (window != NULL, FALSE);
        g_return_val_if_fail (screen != NULL, FALSE);

        gs_debug ("Grabbing mouse widget=%X", (guint32) GDK_WINDOW_XID (window));
        status = gdk_pointer_grab (window, TRUE, 0, NULL,
                                   (hide_cursor ? cursor : NULL),
                                   GDK_CURRENT_TIME);

        if (status == GDK_GRAB_SUCCESS) {
                mouse_grab_window = window;
                mouse_grab_screen = screen;
                mouse_hide_cursor = hide_cursor;
        }

        gdk_cursor_unref (cursor);

        return status;
}

static gboolean
gs_grab_release_keyboard (void)
{
        gs_debug ("Ungrabbing keyboard");
        gdk_keyboard_ungrab (GDK_CURRENT_TIME);

        keyboard_grab_window = NULL;
        keyboard_grab_screen = NULL;

        return TRUE;
}

static gboolean
gs_grab_release_mouse (void)
{
        gs_debug ("Ungrabbing pointer");
        gdk_pointer_ungrab (GDK_CURRENT_TIME);

        mouse_grab_window = NULL;
        mouse_grab_screen = NULL;

        return TRUE;
}

gboolean
gs_grab_move_mouse (GdkWindow *window,
                    GdkScreen *screen,
                    gboolean   hide_cursor)
{
        gboolean   result;
        GdkWindow *old_window;
        GdkScreen *old_screen;
        gboolean   old_hide_cursor;

        /* FIXME: GTK doesn't like having the pointer grabbed */
        return TRUE;

        gdk_x11_grab_server ();

        old_window = mouse_grab_window;
        old_screen = mouse_grab_screen;
        old_hide_cursor = mouse_hide_cursor;

        if (old_window)
                gs_grab_release_mouse ();

        result = gs_grab_get_mouse (window, screen, hide_cursor);

        if (result != GDK_GRAB_SUCCESS) {
                sleep (1);
                result = gs_grab_get_mouse (window, screen, hide_cursor);
        }

        if ((result != GDK_GRAB_SUCCESS) && old_window) {
                g_warning ("Could not grab mouse for new window.  Resuming previous grab.");
                gs_grab_get_mouse (old_window, old_screen, old_hide_cursor);
        }
                
        gdk_x11_ungrab_server ();
        gdk_flush ();

        return (result == GDK_GRAB_SUCCESS);
}

gboolean
gs_grab_move_keyboard (GdkWindow *window,
                       GdkScreen *screen)
{
        gboolean   result;
        GdkWindow *old_window;
        GdkScreen *old_screen;

        gdk_x11_grab_server ();

        old_window = keyboard_grab_window;
        old_screen = keyboard_grab_screen;

        if (old_window)
                gs_grab_release_keyboard ();

        result = gs_grab_get_keyboard (window, screen);

        if (result != GDK_GRAB_SUCCESS) {
                sleep (1);
                result = gs_grab_get_keyboard (window, screen);
        }

        if ((result != GDK_GRAB_SUCCESS) && old_window) {
                g_warning ("Could not grab keyboard for new window.  Resuming previous grab.");
                gs_grab_get_keyboard (old_window, old_screen);
        }

        gdk_x11_ungrab_server ();
        gdk_flush ();

        return (result == GDK_GRAB_SUCCESS);
}

static void
gs_grab_nuke_focus (void)
{
        Window focus = 0;
        int    rev = 0;

        gs_debug ("Nuking focus");

        gdk_error_trap_push ();

        XGetInputFocus (GDK_DISPLAY (), &focus, &rev);

        XSetInputFocus (GDK_DISPLAY (), None, RevertToNone, CurrentTime);

        gdk_error_trap_pop ();
}

void
gs_grab_release_keyboard_and_mouse (void)
{
        gs_grab_release_mouse ();
        gs_grab_release_keyboard ();
}

gboolean
gs_grab_get_keyboard_and_mouse (GdkWindow *window,
                                GdkScreen *screen)
{
        gboolean mstatus = FALSE;
        gboolean kstatus = FALSE;
        int      i;
        int      retries = 4;
        gboolean focus_fuckus = FALSE;

 AGAIN:

        for (i = 0; i < retries; i++) {
                kstatus = gs_grab_get_keyboard (window, screen);
                if (kstatus == GDK_GRAB_SUCCESS)
                        break;

                /* else, wait a second and try to grab again. */
                sleep (1);
        }

        if (kstatus != GDK_GRAB_SUCCESS) {
                if (!focus_fuckus) {
                        focus_fuckus = TRUE;
                        gs_grab_nuke_focus ();
                        goto AGAIN;
                }
        }

        for (i = 0; i < retries; i++) {
                mstatus = gs_grab_get_mouse (window, screen, FALSE);
                if (mstatus == GDK_GRAB_SUCCESS)
                        break;

                /* else, wait a second and try to grab again. */
                sleep (1);
        }

        if (mstatus != GDK_GRAB_SUCCESS)
                g_warning ("Couldn't grab pointer!  (%s)",
                           grab_string (mstatus));

        /* FIXME: release the pointer grab so GTK will work */
        gs_grab_release_mouse ();

        /* When should we allow blanking to proceed?  The current theory
           is that a keyboard grab is manditory; a mouse grab is optional.

           - If we don't have a keyboard grab, then we won't be able to
           read a password to unlock, so the kbd grab is manditory.

           - If we don't have a mouse grab, then we might not see mouse
           clicks as a signal to unblank -- but we will still see kbd
           activity, so that's not a disaster.
        */

        if (kstatus != GDK_GRAB_SUCCESS)	/* Do not blank without a kbd grab.   */
                return FALSE;

        return TRUE;			/* Grab is good, go ahead and blank.  */
}

