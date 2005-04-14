/*
 * Fast User Switch Applet: fusa-user-menu-item.h
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
 * A menu item bound to a FusaUser.
 */

#ifndef __FUSA_USER_MENU_ITEM__
#define __FUSA_USER_MENU_ITEM__ 1

#include <gtk/gtkimagemenuitem.h>

#include "fusa-user.h"

G_BEGIN_DECLS

#define FUSA_TYPE_USER_MENU_ITEM \
  (fusa_user_menu_item_get_type ())
#define FUSA_USER_MENU_ITEM(object) \
  (G_TYPE_CHECK_INSTANCE_CAST ((object), FUSA_TYPE_USER_MENU_ITEM, FusaUserMenuItem))
#define FUSA_USER_MENU_ITEM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), FUSA_TYPE_USER_MENU_ITEM, FusaUserMenuItemClass))
#define FUSA_IS_USER_MENU_ITEM(object) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((object), FUSA_TYPE_USER_MENU_ITEM))
#define FUSA_IS_USER_MENU_ITEM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), FUSA_TYPE_USER_MENU_ITEM))
#define FUSA_USER_MENU_ITEM_GET_CLASS(object) \
  (G_TYPE_INSTANCE_GET_CLASS ((object), FUSA_TYPE_USER_MENU_ITEM, FusaUserMenuItemClass))

typedef struct _FusaUserMenuItem FusaUserMenuItem;
typedef struct _FusaUserMenuItemClass FusaUserMenuItemClass;

GType      fusa_user_menu_item_get_type              (void) G_GNUC_CONST;

GtkWidget *fusa_user_menu_item_new                   (FusaUser         *user);

FusaUser  *fusa_user_menu_item_get_user              (FusaUserMenuItem *item);

gint       fusa_user_menu_item_get_icon_size         (FusaUserMenuItem *item);
void       fusa_user_menu_item_set_icon_size         (FusaUserMenuItem *item,
						      gint              pixel_size);

G_END_DECLS

#endif /* !__FUSA_USER_MENU_ITEM__ */
