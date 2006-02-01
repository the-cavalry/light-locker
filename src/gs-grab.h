/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __GS_GRAB_H
#define __GS_GRAB_H

G_BEGIN_DECLS

void      gs_grab_release_keyboard_and_mouse (void);
gboolean  gs_grab_release_mouse              (void);
gboolean  gs_grab_get_keyboard_and_mouse     (GdkWindow *window,
                                              GdkScreen *screen);
void      gs_grab_window                     (GdkWindow *window,
                                              GdkScreen *screen,
                                              gboolean   hide_cursor);
gboolean  gs_grab_move_mouse                 (GdkWindow *window,
                                              GdkScreen *screen,
                                              gboolean   hide_cursor);
gboolean  gs_grab_move_keyboard              (GdkWindow *window,
                                              GdkScreen *screen);

G_END_DECLS

#endif /* __GS_GRAB_H */
