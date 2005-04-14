/* 
 * Fast User Switch Applet: fusa-manager.h
 * 
 * Copyright (C) 2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Singleton object to manage FusaDisplay and FusaUser objects, and handle GDM
 * interaction/configs.
 */

#ifndef __FUSA_MANAGER_H__
#define __FUSA_MANAGER_H__ 1

#include <gdk/gdkscreen.h>

#include "fusa-display.h"
#include "fusa-user.h"

G_BEGIN_DECLS

#define FUSA_TYPE_MANAGER \
  (fusa_manager_get_type ())
#define FUSA_MANAGER(object) \
  (G_TYPE_CHECK_INSTANCE_CAST ((object), FUSA_TYPE_MANAGER, FusaManager))
#define FUSA_IS_MANAGER(object) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((object), FUSA_TYPE_MANAGER))

#define FUSA_MANAGER_DM_ERROR \
  (fusa_manager_dm_error_get_quark ())

#define FUSA_MANAGER_SIGNAL_USER_ADDED \
  "user-added"
#define FUSA_MANAGER_SIGNAL_USER_REMOVED \
  "user-removed"
#define FUSA_MANAGER_SIGNAL_DISPLAY_ADDED \
  "display-added"
#define FUSA_MANAGER_SIGNAL_DISPLAY_REMOVED \
  "display-removed"


typedef enum
{
  FUSA_MANAGER_DM_ERROR_UNKNOWN               = -1,
  FUSA_MANAGER_DM_ERROR_NOT_RUNNING           =  0,
  FUSA_MANAGER_DM_ERROR_FLEXI_LIMIT_REACHED   =  1,
  FUSA_MANAGER_DM_ERROR_X_ERROR               =  2,
  FUSA_MANAGER_DM_ERROR_X_FAILED              =  3,
  FUSA_MANAGER_DM_ERROR_X_LIMIT_REACHED       =  4,
  FUSA_MANAGER_DM_ERROR_XNEST_AUTH_ERROR      =  5,
  FUSA_MANAGER_DM_ERROR_X_NOT_FOUND           =  6,
  FUSA_MANAGER_DM_ERROR_LOGOUT_ACTION         =  7,
  FUSA_MANAGER_DM_ERROR_CONSOLES_UNSUPPORTED  =  8,
  FUSA_MANAGER_DM_ERROR_INVALID_CONSOLE       =  9,
  FUSA_MANAGER_DM_ERROR_UNKNOWN_CONFIG        =  50,
  FUSA_MANAGER_DM_ERROR_PERMISSIONS           =  100,
  FUSA_MANAGER_DM_ERROR_TOO_MANY_MESSAGES     =  200,
  FUSA_MANAGER_DM_ERROR_OTHER                 = -2
}
FusaManagerDmError;

typedef struct _FusaManager FusaManager;
  
typedef void (*FusaManagerDisplayCallback)      (FusaManager  *manager,
						 FusaDisplay  *display,
						 const GError *error,
						 gpointer      data);

GType        fusa_manager_get_type              (void) G_GNUC_CONST;

FusaManager *fusa_manager_ref_default           (void);

/* Users */
GSList      *fusa_manager_list_users            (FusaManager *manager);
FusaUser    *fusa_manager_get_user              (FusaManager *manager,
						 const gchar *name);
FusaUser    *fusa_manager_get_user_by_uid       (FusaManager *manager,
						 uid_t        uid);

/* Displays */
GSList      *fusa_manager_list_displays         (FusaManager *manager);
FusaDisplay *fusa_manager_get_display           (FusaManager *manager,
						 const gchar *name);
FusaDisplay *fusa_manager_get_display_by_screen (FusaManager *manager,
						 GdkScreen   *screen);

/* Methods */
void         fusa_manager_activate_display      (FusaManager                *manager,
						 FusaDisplay                *display,
						 GdkScreen                  *screen,
						 FusaManagerDisplayCallback  func,
						 gpointer                    data,
						 GDestroyNotify              notify);
void         fusa_manager_new_console           (FusaManager                *manager,
						 GdkScreen                  *screen,
						 FusaManagerDisplayCallback  func,
						 gpointer                    data,
						 GDestroyNotify              notify);
void         fusa_manager_new_xnest             (FusaManager                *manager,
						 GdkScreen                  *screen,
						 FusaManagerDisplayCallback  func,
						 gpointer                    data,
						 GDestroyNotify              notify);

/* Errors */
GQuark       fusa_manager_dm_error_get_quark    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* !__FUSA_MANAGER_H__ */
