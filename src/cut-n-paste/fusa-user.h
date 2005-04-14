/* 
 * Fast User Switch Applet: fusa-user.h
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
 * Facade object for user data, owned by FusaUserManager and updated by FusaVt
 */

#ifndef __FUSA_USER__
#define __FUSA_USER__ 1

#include <sys/types.h>
#include <gtk/gtkwidget.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

#define FUSA_TYPE_USER \
  (fusa_user_get_type ())
#define FUSA_USER(object) \
  (G_TYPE_CHECK_INSTANCE_CAST ((object), FUSA_TYPE_USER, FusaUser))
#define FUSA_IS_USER(object) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((object), FUSA_TYPE_USER))

typedef struct _FusaUser FusaUser;

GType                 fusa_user_get_type         (void) G_GNUC_CONST;

uid_t		      fusa_user_get_uid            (FusaUser   *user);
G_CONST_RETURN gchar *fusa_user_get_user_name      (FusaUser   *user);
G_CONST_RETURN gchar *fusa_user_get_display_name   (FusaUser   *user);
G_CONST_RETURN gchar *fusa_user_get_home_directory (FusaUser   *user);
G_CONST_RETURN gchar *fusa_user_get_shell          (FusaUser   *user);

GSList               *fusa_user_get_displays       (FusaUser   *user);
guint                 fusa_user_get_n_displays     (FusaUser   *user);

GdkPixbuf            *fusa_user_render_icon        (FusaUser   *user,
						    GtkWidget  *widget,
						    gint        icon_size,
						    gboolean    desktop_overlay);

gint                  fusa_user_collate            (FusaUser   *user1,
						    FusaUser   *user2);

G_END_DECLS

#endif /* !__FUSA_USER_MENU_ITEM__ */
