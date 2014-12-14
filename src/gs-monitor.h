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

#ifndef __GS_MONITOR_H
#define __GS_MONITOR_H

G_BEGIN_DECLS

#define GS_TYPE_MONITOR         (gs_monitor_get_type ())
#define GS_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_MONITOR, GSMonitor))
#define GS_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_MONITOR, GSMonitorClass))
#define GS_IS_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_MONITOR))
#define GS_IS_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_MONITOR))
#define GS_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_MONITOR, GSMonitorClass))

typedef struct GSMonitorPrivate GSMonitorPrivate;

typedef struct
{
        GObject          parent;
        GSMonitorPrivate *priv;
} GSMonitor;

typedef struct
{
        GObjectClass     parent_class;
} GSMonitorClass;

GType       gs_monitor_get_type (void);

GSMonitor * gs_monitor_new              (LLConfig  *config);
gboolean    gs_monitor_start            (GSMonitor *monitor,
                                         GError   **error);
G_END_DECLS

#endif /* __GS_MONITOR_H */
