/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 1993-2004 Jamie Zawinski <jwz@jwz.org>
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
 */

#ifndef __GS_FADE_H
#define __GS_FADE_H

#include <gdk/gdkx.h>

G_BEGIN_DECLS

void fade_screens_and_show (int       seconds,
                            int       ticks,
                            gboolean  out,
                            GSList   *windows);

G_END_DECLS

#endif /* __GS_FADE_H */
