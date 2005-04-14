/* 
 * Fast User Switch Applet: fusa-user-private.h
 * 
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Private interfaces to the FusaUser object
 */

#ifndef __FUSA_USER_PRIVATE__
#define __FUSA_USER_PRIVATE__ 1

#include <pwd.h>

#include "fusa-display.h"
#include "fusa-user.h"

G_BEGIN_DECLS

void _fusa_user_update         (FusaUser            *user,
				const struct passwd *pwent);

void _fusa_user_add_display    (FusaUser            *user,
				FusaDisplay         *display);
void _fusa_user_remove_display (FusaUser            *user,
				FusaDisplay         *display);

void _fusa_user_icon_changed   (FusaUser            *user);

G_END_DECLS

#endif /* !__FUSA_USER_PRIVATE__ */
