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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __GS_MANAGER_H
#define __GS_MANAGER_H

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

} GSManagerClass;

GType       gs_manager_get_type             (void);

GSManager * gs_manager_new                  (void);

gboolean    gs_manager_set_active           (GSManager  *manager,
                                             gboolean    active);
gboolean    gs_manager_get_active           (GSManager  *manager);

gboolean    gs_manager_request_unlock       (GSManager  *manager);

G_END_DECLS

#endif /* __GS_MANAGER_H */
