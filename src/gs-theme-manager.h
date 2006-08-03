/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GS_THEME_MANAGER_H
#define __GS_THEME_MANAGER_H

G_BEGIN_DECLS

#define GS_TYPE_THEME_MANAGER         (gs_theme_manager_get_type ())
#define GS_THEME_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_THEME_MANAGER, GSThemeManager))
#define GS_THEME_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_THEME_MANAGER, GSThemeManagerClass))
#define GS_IS_THEME_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_THEME_MANAGER))
#define GS_IS_THEME_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_THEME_MANAGER))
#define GS_THEME_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_THEME_MANAGER, GSThemeManagerClass))

typedef struct GSThemeManagerPrivate GSThemeManagerPrivate;

typedef struct
{
        GObject                parent;
        GSThemeManagerPrivate *priv;
} GSThemeManager;

typedef struct
{
        GObjectClass   parent_class;
} GSThemeManagerClass;

typedef struct _GSThemeInfo GSThemeInfo;

GType              gs_theme_manager_get_type          (void);

GSThemeManager    *gs_theme_manager_new               (void);

GSList            *gs_theme_manager_get_info_list     (GSThemeManager *manager);
GSThemeInfo       *gs_theme_manager_lookup_theme_info (GSThemeManager *manager,
                                                       const char     *theme);
GSThemeInfo       *gs_theme_info_ref                  (GSThemeInfo    *info);
void               gs_theme_info_unref                (GSThemeInfo    *info);
const char        *gs_theme_info_get_id               (GSThemeInfo    *info);
const char        *gs_theme_info_get_name             (GSThemeInfo    *info);
const char        *gs_theme_info_get_exec             (GSThemeInfo    *info);

G_END_DECLS

#endif /* __GS_THEME_MANAGER_H */
