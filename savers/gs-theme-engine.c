/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "gs-theme-engine.h"
#include "gs-theme-engine-marshal.h"

static void     gs_theme_engine_class_init (GSThemeEngineClass *klass);
static void     gs_theme_engine_init       (GSThemeEngine      *engine);
static void     gs_theme_engine_finalize   (GObject            *object);

enum {
        SHOW,
        DELETE_EVENT,
        DESTROY_EVENT,
        MAP_EVENT,
        UNMAP_EVENT,
        EXPOSE_EVENT,
        CONFIGURE_EVENT,
        LAST_SIGNAL
};

struct GSThemeEnginePrivate
{
        GdkWindow    *window;
        GdkRectangle  geometry;

        guint         visible : 1;
};

#define GS_THEME_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_THEME_ENGINE, GSThemeEnginePrivate))

static GObjectClass *parent_class = NULL;
static guint         engine_signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSThemeEngine, gs_theme_engine, G_TYPE_OBJECT)

static void
gs_theme_engine_set_property (GObject            *object,
                              guint               prop_id,
                              const GValue       *value,
                              GParamSpec         *pspec)
{
        GSThemeEngine *self;

        self = GS_THEME_ENGINE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_theme_engine_get_property (GObject            *object,
                              guint               prop_id,
                              GValue             *value,
                              GParamSpec         *pspec)
{
        GSThemeEngine *self;

        self = GS_THEME_ENGINE (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
update_geometry (GSThemeEngine *engine)
{
        GdkRectangle rect;

        gdk_window_get_geometry (engine->priv->window,
                                 &rect.x,
                                 &rect.y,
                                 &rect.width,
                                 &rect.height,
                                 NULL);

        engine->priv->geometry.x = rect.x;
        engine->priv->geometry.y = rect.y;
        engine->priv->geometry.width = rect.width;
        engine->priv->geometry.height = rect.height;
}

static gboolean
gs_theme_engine_real_configure_event (GSThemeEngine     *engine,
                                      GdkEventConfigure *event)
{
        gboolean handled = FALSE;

        gdk_window_clear (engine->priv->window);
        update_geometry (engine);

        return handled;
}

static gboolean
gs_theme_engine_real_map_event (GSThemeEngine *engine,
                                GdkEventAny   *event)
{
        gboolean handled = FALSE;

        gdk_window_clear (engine->priv->window);
        update_geometry (engine);

        if (! engine->priv->visible) {
                g_object_ref (engine);
                g_signal_emit (engine, engine_signals [SHOW], 0);
                g_object_unref (engine);
        }
        engine->priv->visible = TRUE;

        return handled;
}

static void
gs_theme_engine_class_init (GSThemeEngineClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = gs_theme_engine_finalize;
        object_class->get_property = gs_theme_engine_get_property;
        object_class->set_property = gs_theme_engine_set_property;

        klass->configure_event = gs_theme_engine_real_configure_event;
        klass->map_event = gs_theme_engine_real_map_event;

        g_type_class_add_private (klass, sizeof (GSThemeEnginePrivate));

        engine_signals [SHOW] = g_signal_new ("show",
                                              G_TYPE_FROM_CLASS (object_class),
                                              G_SIGNAL_RUN_LAST,
                                              G_STRUCT_OFFSET (GSThemeEngineClass, show),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__VOID,
                                              G_TYPE_NONE,
                                              0, G_TYPE_NONE);

        engine_signals [EXPOSE_EVENT] = g_signal_new ("expose_event",
                                                      G_TYPE_FROM_CLASS (object_class),
                                                      G_SIGNAL_RUN_LAST,
                                                      G_STRUCT_OFFSET (GSThemeEngineClass, expose_event),
                                                      NULL,
                                                      NULL,
                                                      gs_theme_engine_marshal_BOOLEAN__BOXED,
                                                      G_TYPE_BOOLEAN, 1,
                                                      GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
        engine_signals [DELETE_EVENT] = g_signal_new ("delete_event",
                                                      G_TYPE_FROM_CLASS (object_class),
                                                      G_SIGNAL_RUN_LAST,
                                                      G_STRUCT_OFFSET (GSThemeEngineClass, delete_event),
                                                      NULL,
                                                      NULL,
                                                      gs_theme_engine_marshal_BOOLEAN__BOXED,
                                                      G_TYPE_BOOLEAN, 1,
                                                      GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
        engine_signals [DESTROY_EVENT] = g_signal_new ("destroy_event",
                                                      G_TYPE_FROM_CLASS (object_class),
                                                      G_SIGNAL_RUN_LAST,
                                                      G_STRUCT_OFFSET (GSThemeEngineClass, destroy_event),
                                                      NULL,
                                                      NULL,
                                                      gs_theme_engine_marshal_BOOLEAN__BOXED,
                                                      G_TYPE_BOOLEAN, 1,
                                                      GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
        engine_signals [CONFIGURE_EVENT] = g_signal_new ("configure_event",
                                                      G_TYPE_FROM_CLASS (object_class),
                                                      G_SIGNAL_RUN_LAST,
                                                      G_STRUCT_OFFSET (GSThemeEngineClass, configure_event),
                                                      NULL,
                                                      NULL,
                                                      gs_theme_engine_marshal_BOOLEAN__BOXED,
                                                      G_TYPE_BOOLEAN, 1,
                                                      GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
        engine_signals [MAP_EVENT] = g_signal_new ("map_event",
                                                      G_TYPE_FROM_CLASS (object_class),
                                                      G_SIGNAL_RUN_LAST,
                                                      G_STRUCT_OFFSET (GSThemeEngineClass, map_event),
                                                      NULL,
                                                      NULL,
                                                      gs_theme_engine_marshal_BOOLEAN__BOXED,
                                                      G_TYPE_BOOLEAN, 1,
                                                      GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
        engine_signals [UNMAP_EVENT] = g_signal_new ("unmap_event",
                                                      G_TYPE_FROM_CLASS (object_class),
                                                      G_SIGNAL_RUN_LAST,
                                                      G_STRUCT_OFFSET (GSThemeEngineClass, unmap_event),
                                                      NULL,
                                                      NULL,
                                                      gs_theme_engine_marshal_BOOLEAN__BOXED,
                                                      G_TYPE_BOOLEAN, 1,
                                                      GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

}

static void
gs_theme_engine_init (GSThemeEngine *engine)
{

        engine->priv = GS_THEME_ENGINE_GET_PRIVATE (engine);

}

static void
gs_theme_engine_finalize (GObject *object)
{
        GSThemeEngine *engine;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_THEME_ENGINE (object));

        engine = GS_THEME_ENGINE (object);

        g_return_if_fail (engine->priv != NULL);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Copied from gtkwidget.c */
static gboolean
event_window_is_still_viewable (GdkEvent *event)
{
  /* Some programs, such as gnome-theme-manager, fake widgets
   * into exposing onto a pixmap by sending expose events with
   * event->window pointing to a pixmap
   */
  if (GDK_IS_PIXMAP (event->any.window))
    return event->type == GDK_EXPOSE;
  
  /* Check that we think the event's window is viewable before
   * delivering the event, to prevent suprises. We do this here
   * at the last moment, since the event may have been queued
   * up behind other events, held over a recursive main loop, etc.
   */
  switch (event->type)
    {
    case GDK_EXPOSE:
    case GDK_MOTION_NOTIFY:
    case GDK_BUTTON_PRESS:
    case GDK_2BUTTON_PRESS:
    case GDK_3BUTTON_PRESS:
    case GDK_KEY_PRESS:
    case GDK_ENTER_NOTIFY:
    case GDK_PROXIMITY_IN:
    case GDK_SCROLL:
      return event->any.window && gdk_window_is_viewable (event->any.window);

#if 0
    /* The following events are the second half of paired events;
     * we always deliver them to deal with widgets that clean up
     * on the second half.
     */
    case GDK_BUTTON_RELEASE:
    case GDK_KEY_RELEASE:
    case GDK_LEAVE_NOTIFY:
    case GDK_PROXIMITY_OUT:
#endif      
      
    default:
      /* Remaining events would make sense on an not-viewable window,
       * or don't have an associated window.
       */
      return TRUE;
    }
}

static gboolean
gs_theme_engine_event_internal (GSThemeEngine *engine,
                                GdkEvent      *event)
{
        gboolean return_val = FALSE;
        int      signal_num;

        if (! event_window_is_still_viewable (event))
                return TRUE;

        g_object_ref (engine);

        switch (event->type) {
	case GDK_NOTHING:
	case GDK_BUTTON_PRESS:
	case GDK_2BUTTON_PRESS:
	case GDK_3BUTTON_PRESS:
	case GDK_SCROLL:
	case GDK_BUTTON_RELEASE:
	case GDK_MOTION_NOTIFY:
	case GDK_KEY_PRESS:
	case GDK_KEY_RELEASE:
	case GDK_ENTER_NOTIFY:
	case GDK_LEAVE_NOTIFY:
	case GDK_FOCUS_CHANGE:
	case GDK_WINDOW_STATE:
	case GDK_PROPERTY_NOTIFY:
	case GDK_SELECTION_CLEAR:
	case GDK_SELECTION_REQUEST:
	case GDK_SELECTION_NOTIFY:
	case GDK_PROXIMITY_IN:
	case GDK_PROXIMITY_OUT:
	case GDK_NO_EXPOSE:
	case GDK_CLIENT_EVENT:
	case GDK_VISIBILITY_NOTIFY:
	case GDK_GRAB_BROKEN:
                signal_num = -1;
                break;
	case GDK_DELETE:
                signal_num = DELETE_EVENT;
                break;
	case GDK_DESTROY:
                signal_num = DESTROY_EVENT;
                break;
	case GDK_CONFIGURE:
                signal_num = CONFIGURE_EVENT;
                break;
	case GDK_MAP:
                signal_num = MAP_EVENT;
                break;
	case GDK_UNMAP:
                signal_num = UNMAP_EVENT;
                break;
	case GDK_EXPOSE:
                signal_num = EXPOSE_EVENT;
                break;
	default:
                g_warning ("gs_theme_engine_event(): unhandled event type: %d", event->type);
                signal_num = -1;
                break;
	}

        if (signal_num != -1) {
                g_signal_emit (engine, engine_signals [signal_num], 0, event, &return_val);
        }

        g_object_unref (engine);
        
        return return_val;
}

gboolean
gs_theme_engine_event (GSThemeEngine *engine,
                       GdkEvent      *event)
{
        g_return_val_if_fail (GS_IS_THEME_ENGINE (engine), TRUE);

        return gs_theme_engine_event_internal (engine, event);
}

void
gs_theme_engine_queue_draw (GSThemeEngine *engine)
{
        g_return_if_fail (GS_IS_THEME_ENGINE (engine));

        gdk_window_invalidate_rect (engine->priv->window,
                                    &engine->priv->geometry,
                                    FALSE);
}

void
gs_theme_engine_queue_resize (GSThemeEngine *engine)
{
        g_return_if_fail (GS_IS_THEME_ENGINE (engine));

        /* FIXME */
}

void
gs_theme_engine_show (GSThemeEngine *engine)
{
        g_return_if_fail (GS_IS_THEME_ENGINE (engine));

        if (! engine->priv->window) {
                return;
        }

        engine->priv->visible = gdk_window_is_visible (engine->priv->window);

        if (! engine->priv->visible) {
                /* wait for map */
                gdk_window_show (engine->priv->window);
        } else {
                g_object_ref (engine);
                g_signal_emit (engine, engine_signals [SHOW], 0);
                g_object_unref (engine);
        }
}

void
gs_theme_engine_destroy (GSThemeEngine *engine)
{
        g_return_if_fail (GS_IS_THEME_ENGINE (engine));

        gdk_window_clear (engine->priv->window);
        engine->priv->visible = FALSE;

        g_object_unref (engine);
}

GdkWindow *
gs_theme_engine_get_window (GSThemeEngine *engine)
{
        g_return_val_if_fail (GS_IS_THEME_ENGINE (engine), NULL);

        return engine->priv->window;
}

void
gs_theme_engine_set_window (GSThemeEngine *engine,
                            GdkWindow     *window)
{
        g_return_if_fail (GS_IS_THEME_ENGINE (engine));

        engine->priv->window = window;

        update_geometry (engine);
}

void
gs_theme_engine_get_window_size (GSThemeEngine *engine,
                                 int           *width,
                                 int           *height)
{
        g_return_if_fail (GS_IS_THEME_ENGINE (engine));

        if (width) {
                *width = engine->priv->geometry.width;
        }

        if (height) {
                *height = engine->priv->geometry.height;
        }
}
