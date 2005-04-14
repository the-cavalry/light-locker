/* 
 * Fast User-Switching Applet: fusa-manager-private.h
 * 
 * Copyright (C) 2004 James M. Cape <jcape@ignore-your.tv>.
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
 * Private interfaces to the FusaManager object
 */

#ifndef __FUSA_MANAGER_PRIVATE_H__
#define __FUSA_MANAGER_PRIVATE_H__ 1

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "fusa-manager.h"

G_BEGIN_DECLS

void       _fusa_manager_set_display (FusaManager *manager,
				      FusaDisplay *display,
				      GdkScreen   *screen);
GdkPixbuf *_fusa_manager_render_icon (FusaManager *manager,
				      FusaUser    *user,
				      GtkWidget   *widget,
				      gint         icon_size);

G_END_DECLS

#endif /* !__FUSA_MANAGER_PRIVATE_H__ */
