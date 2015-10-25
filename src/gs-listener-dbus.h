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

#ifndef __GS_LISTENER_H
#define __GS_LISTENER_H

G_BEGIN_DECLS

#define GS_TYPE_LISTENER         (gs_listener_get_type ())
#define GS_LISTENER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_LISTENER, GSListener))
#define GS_LISTENER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_LISTENER, GSListenerClass))
#define GS_IS_LISTENER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_LISTENER))
#define GS_IS_LISTENER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_LISTENER))
#define GS_LISTENER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_LISTENER, GSListenerClass))

typedef struct GSListenerPrivate GSListenerPrivate;

typedef struct
{
        GObject            parent;
        GSListenerPrivate *priv;
} GSListener;

typedef struct
{
        GObjectClass       parent_class;

        void            (* lock)                     (GSListener *listener);
        void            (* locked)                   (GSListener *listener);
        void            (* session_switched)         (GSListener *listener,
                                                      gboolean    active);
        gboolean        (* active_changed)           (GSListener *listener,
                                                      gboolean    active);
        void            (* suspend)                  (GSListener *listener);
        void            (* resume)                   (GSListener *listener);
        void            (* simulate_user_activity)   (GSListener *listener);
        gboolean        (* blanking)                 (GSListener *listener,
                                                      gboolean    active);
        void            (* inhibit)                  (GSListener *listener,
                                                      gboolean    active);
        gboolean        (* is_blanked)               (GSListener *listener);
        gulong          (* blanked_time)             (GSListener *listener);
        gulong          (* idle_time)                (GSListener *listener);

} GSListenerClass;

typedef enum
{
        GS_LISTENER_ERROR_ACQUISITION_FAILURE,
} GSListenerError;

#define GS_LISTENER_ERROR gs_listener_error_quark ()

GQuark      gs_listener_error_quark             (void);

GType       gs_listener_get_type                (void);

GSListener *gs_listener_new                     (void);
gboolean    gs_listener_acquire                 (GSListener *listener,
                                                 GError    **error);
void        gs_listener_set_blanked             (GSListener *listener,
                                                 gboolean    active);
gboolean    gs_listener_set_active              (GSListener *listener,
                                                 gboolean    active);
void        gs_listener_send_switch_greeter     (GSListener *listener);
void        gs_listener_send_lock_session       (GSListener *listener);
gboolean    gs_listener_is_lid_closed           (GSListener *listener);

void        gs_listener_delay_suspend           (GSListener *listener);
void        gs_listener_resume_suspend          (GSListener *listener);

void        gs_listener_set_idle_hint           (GSListener *listener,
                                                 gboolean    idle);

G_END_DECLS

#endif /* __GS_LISTENER_H */
