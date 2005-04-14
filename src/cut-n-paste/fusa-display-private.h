/* 
 * Fast User Switch Applet: fusa-display-private.h
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
 * Private Interfaces to the FusaDisplay object
 */

#ifndef __FUSA_DISPLAY_PRIVATE_H__
#define __FUSA_DISPLAY_PRIVATE_H__ 1

#include "fusa-manager.h"

G_BEGIN_DECLS

FusaDisplay *_fusa_display_new      (FusaManager *manager,
				     const gchar *name,
				     FusaUser    *user,
				     gint         console,
				     gboolean     nested);
void         _fusa_display_set_user (FusaDisplay *display,
				     FusaUser    *user);

G_END_DECLS

#endif /* !__FUSA_DISPLAY_PRIVATE_H__ */
