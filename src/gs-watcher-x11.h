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

#ifndef __GS_WATCHER_H
#define __GS_WATCHER_H

G_BEGIN_DECLS

#define GS_TYPE_WATCHER         (gs_watcher_get_type ())
#define GS_WATCHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_WATCHER, GSWatcher))
#define GS_WATCHER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_WATCHER, GSWatcherClass))
#define GS_IS_WATCHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_WATCHER))
#define GS_IS_WATCHER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_WATCHER))
#define GS_WATCHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_WATCHER, GSWatcherClass))

typedef struct GSWatcherPrivate GSWatcherPrivate;

typedef struct
{
        GObject           parent;
        GSWatcherPrivate *priv;
} GSWatcher;

typedef struct
{
        GObjectClass      parent_class;

        gboolean          (* idle)        (GSWatcher *watcher,
                                           int        reserved);
} GSWatcherClass;

GType       gs_watcher_get_type (void);

GSWatcher * gs_watcher_new              (guint      timeout);
void        gs_watcher_reset            (GSWatcher *watcher);
void        gs_watcher_set_active       (GSWatcher *watcher,
                                         gboolean   active);
void        gs_watcher_set_timeout      (GSWatcher *watcher,
                                         guint      timeout);

#endif /* __GS_WATCHER_H */
