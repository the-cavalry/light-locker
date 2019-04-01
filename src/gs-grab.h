/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __GS_GRAB_H
#define __GS_GRAB_H

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define GS_TYPE_GRAB gs_grab_get_type ()
G_DECLARE_FINAL_TYPE (GSGrab, gs_grab, GS, GRAB, GObject)

GSGrab  * gs_grab_new              (void);

void      gs_grab_release          (GSGrab    *grab);
gboolean  gs_grab_release_mouse    (GSGrab    *grab);

gboolean  gs_grab_grab_window      (GSGrab    *grab,
                                    GdkWindow *window,
                                    GdkScreen *screen,
                                    gboolean   hide_cursor);

gboolean  gs_grab_grab_root        (GSGrab    *grab,
                                    gboolean   hide_cursor);
gboolean  gs_grab_grab_offscreen   (GSGrab    *grab,
                                    gboolean   hide_cursor);

void      gs_grab_move_to_window   (GSGrab    *grab,
                                    GdkWindow *window,
                                    GdkScreen *screen,
                                    gboolean   hide_cursor);

void      gs_grab_mouse_reset      (GSGrab    *grab);
void      gs_grab_keyboard_reset   (GSGrab    *grab);

G_END_DECLS

#endif /* __GS_GRAB_H */
