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

#ifndef __GS_WINDOW_H
#define __GS_WINDOW_H

#include <gdk/gdk.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_WINDOW         (gs_window_get_type ())
#define GS_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_WINDOW, GSWindow))
#define GS_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_WINDOW, GSWindowClass))
#define GS_IS_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_WINDOW))
#define GS_IS_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_WINDOW))
#define GS_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_WINDOW, GSWindowClass))

typedef struct GSWindowPrivate GSWindowPrivate;

typedef struct
{
        GtkWindow        window;

        GSWindowPrivate *priv;
} GSWindow;

typedef struct
{
        GtkWindowClass   parent_class;

        gboolean        (* activity)            (GSWindow *window);
        void            (* deactivated)         (GSWindow *window);
        void            (* dialog_up)           (GSWindow *window);
        void            (* dialog_down)         (GSWindow *window);
} GSWindowClass;

GType       gs_window_get_type           (void);

gboolean    gs_window_is_obscured        (GSWindow  *window);
gboolean    gs_window_is_dialog_up       (GSWindow  *window);

void        gs_window_set_screen         (GSWindow  *window,
                                          GdkScreen *screen);
GdkScreen * gs_window_get_screen         (GSWindow  *window);
void        gs_window_set_monitor        (GSWindow  *window,
                                          int        monitor);
int         gs_window_get_monitor        (GSWindow  *window);

void        gs_window_set_background_pixmap (GSWindow  *window,
                                             GdkPixmap *pixmap);
void        gs_window_set_lock_enabled   (GSWindow  *window,
                                          gboolean   lock_enabled);
void        gs_window_set_logout_enabled (GSWindow  *window,
                                          gboolean   logout_enabled);
void        gs_window_set_keyboard_enabled (GSWindow  *window,
                                            gboolean   enabled);
void        gs_window_set_keyboard_command (GSWindow   *window,
                                            const char *command);
void        gs_window_set_user_switch_enabled (GSWindow  *window,
                                               gboolean   user_switch_enabled);
void        gs_window_set_logout_timeout (GSWindow  *window,
                                          glong      timeout);
void        gs_window_set_logout_command (GSWindow   *window,
                                          const char *command);
void        gs_window_set_status_message   (GSWindow   *window,
                                            const char *status_message);
void        gs_window_show_message         (GSWindow   *window,
                                            const char *summary,
                                            const char *body,
                                            const char *icon);

void        gs_window_request_unlock     (GSWindow  *window);
void        gs_window_cancel_unlock_request (GSWindow  *window);

GSWindow  * gs_window_new                (GdkScreen *screen,
                                          int        monitor,
                                          gboolean   lock_enabled);
void        gs_window_show               (GSWindow  *window);
void        gs_window_destroy            (GSWindow  *window);
GdkWindow * gs_window_get_gdk_window     (GSWindow  *window);
GtkWidget * gs_window_get_drawing_area   (GSWindow  *window);
void        gs_window_clear              (GSWindow  *window);

G_END_DECLS

#endif /* __GS_WINDOW_H */
