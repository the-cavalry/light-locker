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

#ifndef __GS_MANAGER_H
#define __GS_MANAGER_H

#include "gs-prefs.h"

G_BEGIN_DECLS

#define GS_TYPE_MANAGER         (gs_manager_get_type ())
#define GS_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_MANAGER, GSManager))
#define GS_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_MANAGER, GSManagerClass))
#define GS_IS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_MANAGER))
#define GS_IS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_MANAGER))
#define GS_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_MANAGER, GSManagerClass))

typedef struct GSManagerPrivate GSManagerPrivate;

typedef struct
{
        GObject          parent;
        GSManagerPrivate *priv;
} GSManager;

typedef struct
{
        GObjectClass     parent_class;

        void            (* activated)          (GSManager *manager);
        void            (* deactivated)        (GSManager *manager);
        void            (* auth_request_begin) (GSManager *manager);
        void            (* auth_request_end)   (GSManager *manager);

} GSManagerClass;

GType       gs_manager_get_type             (void);

GSManager * gs_manager_new                  (void);

gboolean    gs_manager_set_active           (GSManager  *manager,
                                             gboolean    active);
gboolean    gs_manager_get_active           (GSManager  *manager);

gboolean    gs_manager_cycle                (GSManager  *manager);

void        gs_manager_get_lock_active      (GSManager  *manager,
                                             gboolean   *lock_active);
void        gs_manager_set_lock_active      (GSManager  *manager,
                                             gboolean    lock_active);
void        gs_manager_set_keyboard_enabled (GSManager  *manager,
                                             gboolean    enabled);
void        gs_manager_set_keyboard_command (GSManager  *manager,
                                             const char *command);
void        gs_manager_set_status_message   (GSManager  *manager,
                                             const char *message);
void        gs_manager_get_lock_enabled     (GSManager  *manager,
                                             gboolean   *lock_enabled);
void        gs_manager_set_lock_enabled     (GSManager  *manager,
                                             gboolean    lock_enabled);
void        gs_manager_set_lock_timeout     (GSManager  *manager,
                                             glong       lock_timeout);
void        gs_manager_set_logout_enabled   (GSManager  *manager,
                                             gboolean    logout_enabled);
void        gs_manager_set_user_switch_enabled (GSManager  *manager,
                                                gboolean    user_switch_enabled);
void        gs_manager_set_logout_timeout   (GSManager  *manager,
                                             glong       logout_timeout);
void        gs_manager_set_logout_command   (GSManager  *manager,
                                             const char *command);
void        gs_manager_set_throttled        (GSManager  *manager,
                                             gboolean    lock_enabled);
void        gs_manager_set_cycle_timeout    (GSManager  *manager,
                                             glong       cycle_timeout);
void        gs_manager_set_themes           (GSManager  *manager,
                                             GSList     *themes);
void        gs_manager_set_mode             (GSManager  *manager,
                                             GSSaverMode mode);
void        gs_manager_show_message         (GSManager  *manager,
                                             const char *summary,
                                             const char *body,
                                             const char *icon);
gboolean    gs_manager_request_unlock       (GSManager  *manager);
void        gs_manager_cancel_unlock_request (GSManager *manager);

G_END_DECLS

#endif /* __GS_MANAGER_H */
