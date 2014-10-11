/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GS_LISTENER_X11_H
#define __GS_LISTENER_X11_H

G_BEGIN_DECLS

#define GS_TYPE_LISTENER_X11         (gs_listener_x11_get_type ())
#define GS_LISTENER_X11(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_LISTENER_X11, GSListenerX11))
#define GS_LISTENER_X11_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_LISTENER_X11, GSListenerX11Class))
#define GS_IS_LISTENER_X11(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_LISTENER_X11))
#define GS_IS_LISTENER_X11_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_LISTENER_X11))
#define GS_LISTENER_X11_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_LISTENER_X11, GSListenerX11Class))

typedef struct GSListenerX11Private GSListenerX11Private;

typedef struct
{
        GObject               parent;
        GSListenerX11Private *priv;
} GSListenerX11;

typedef struct
{
        GObjectClass       parent_class;

        void            (* blanking_changed)         (GSListenerX11 *listener, gboolean active);

} GSListenerX11Class;

GType          gs_listener_x11_get_type          (void);

GSListenerX11 *gs_listener_x11_new               (void);
gboolean       gs_listener_x11_acquire           (GSListenerX11 *listener);
void           gs_listener_x11_simulate_activity (GSListenerX11 *listener);
gboolean       gs_listener_x11_force_blanking    (GSListenerX11 *listener,
                                                  gboolean       active);
void           gs_listener_x11_inhibit           (GSListenerX11 *listener,
                                                  gboolean       active);
gulong         gs_listener_x11_idle_time         (GSListenerX11 *listener);

G_END_DECLS

#endif /* __GS_LISTENER_X11_H */
