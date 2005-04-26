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

        void            (* blanked)          (GSManager *manager);
        void            (* unblanked)        (GSManager *manager);

} GSManagerClass;

GType       gs_manager_get_type (void);

GSManager * gs_manager_new              (gint       lock_delay,
                                         gint       cycle_delay);

gboolean    gs_manager_blank            (GSManager  *manager);
gboolean    gs_manager_unblank          (GSManager  *manager);
gboolean    gs_manager_cycle            (GSManager  *manager);

gboolean    gs_manager_is_blanked       (GSManager  *manager);
void        gs_manager_set_lock_enabled (GSManager  *manager,
                                         gboolean    lock_enabled);
void        gs_manager_set_lock_delay   (GSManager  *manager,
                                         glong       lock_delay);
void        gs_manager_set_cycle_delay  (GSManager  *manager,
                                         glong       cycle_delay);
void        gs_manager_set_themes       (GSManager  *manager,
                                         GSList     *themes);
void        gs_manager_set_mode         (GSManager  *manager,
                                         GSSaverMode mode);

G_END_DECLS

#endif /* __GS_MANAGER_H */
