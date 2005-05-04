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

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gs-listener-dbus.h"

static void     gs_listener_class_init (GSListenerClass *klass);
static void     gs_listener_init       (GSListener      *listener);
static void     gs_listener_finalize   (GObject         *object);

static void              gs_listener_unregister_handler (DBusConnection *connection,
                                                         void           *data);

static DBusHandlerResult gs_listener_message_handler    (DBusConnection *connection,
                                                         DBusMessage    *message,
                                                         void           *user_data);

#define GS_LISTENER_SERVICE   "org.gnome.ScreenSaver"
#define GS_LISTENER_PATH      "/org/gnome/ScreenSaver"
#define GS_LISTENER_INTERFACE "org.gnome.ScreenSaver"

#define GS_LISTENER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LISTENER, GSListenerPrivate))

struct GSListenerPrivate
{
        DBusConnection *connection;
};

enum {
        LOCK,
        ACTIVATE,
        DEACTIVATE,
        CYCLE,
        QUIT,
        PING,
        LAST_SIGNAL
};

enum {
        PROP_0,
};

static DBusObjectPathVTable
gs_listener_vtable = { &gs_listener_unregister_handler,
                       &gs_listener_message_handler,
                       NULL,
                       NULL,
                       NULL,
                       NULL };

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSListener, gs_listener, G_TYPE_OBJECT);

GQuark
gs_listener_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("gs_listener_error");

        return quark;
}

static void
gs_listener_unregister_handler (DBusConnection *connection,
                                void           *data)
{
}

static DBusHandlerResult
gs_listener_message_handler (DBusConnection *connection,
                             DBusMessage    *message,
                             void           *user_data)
{
        GSListener  *listener = GS_LISTENER (user_data);

	g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
	g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "lock")) {
                g_signal_emit (listener, signals [LOCK], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "quit")) {
                g_signal_emit (listener, signals [QUIT], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "cycle")) {
                g_signal_emit (listener, signals [CYCLE], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "activate")) {
                g_signal_emit (listener, signals [ACTIVATE], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "deactivate")) {
                g_signal_emit (listener, signals [DEACTIVATE], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "ping")) {
                g_signal_emit (listener, signals [PING], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
gs_listener_set_property (GObject            *object,
                         guint               prop_id,
                         const GValue       *value,
                         GParamSpec         *pspec)
{
        GSListener *self;

        self = GS_LISTENER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_listener_get_property (GObject            *object,
                         guint               prop_id,
                         GValue             *value,
                         GParamSpec         *pspec)
{
        GSListener *self;

        self = GS_LISTENER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_listener_class_init (GSListenerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize     = gs_listener_finalize;
        object_class->get_property = gs_listener_get_property;
        object_class->set_property = gs_listener_set_property;

        signals [LOCK] =
                g_signal_new ("lock",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, lock),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [QUIT] =
                g_signal_new ("quit",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, quit),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [CYCLE] =
                g_signal_new ("cycle",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, cycle),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [ACTIVATE] =
                g_signal_new ("activate",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, activate),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DEACTIVATE] =
                g_signal_new ("deactivate",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, deactivate),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [PING] =
                g_signal_new ("ping",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, ping),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_type_class_add_private (klass, sizeof (GSListenerPrivate));
}


static gboolean
screensaver_is_running (DBusConnection *connection)
{
        DBusError               error;
        gboolean                exists;

        g_return_val_if_fail (connection != NULL, FALSE);

        dbus_error_init (&error);
        exists = dbus_bus_service_exists (connection, GS_LISTENER_SERVICE, &error);
        if (dbus_error_is_set (&error))
                dbus_error_free (&error);

        return exists;
}

gboolean
gs_listener_acquire (GSListener *listener,
                     GError    **error)
{
        gboolean acquired;
        DBusError buserror;

        if (!listener->priv->connection) {
                g_warning ("failed to register, we're not on DBus...");
                return FALSE;
        }

        if (screensaver_is_running (listener->priv->connection)) {
                g_error ("Screensaver already running in this session");
                return FALSE;
        }

        dbus_error_init (&buserror);

        if (dbus_connection_register_object_path (listener->priv->connection,
                                                  GS_LISTENER_PATH,
                                                  &gs_listener_vtable,
                                                  listener) == FALSE)
                g_critical ("out of memory registering object path");

        acquired = dbus_bus_acquire_service (listener->priv->connection,
                                             GS_LISTENER_SERVICE,
                                             0, &buserror) != -1;
        if (dbus_error_is_set (&buserror))
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             buserror.message);

        dbus_error_free (&buserror);

        return acquired;
}

static void
gs_listener_init (GSListener *listener)
{
        DBusError error;

        listener->priv = GS_LISTENER_GET_PRIVATE (listener);

        dbus_error_init (&error);
        listener->priv->connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
        if (listener->priv->connection == NULL) {
                g_critical ("couldn't connect to session bus: %s",
                            error.message);
        } else {
                dbus_connection_setup_with_g_main (listener->priv->connection, NULL);
        }

        dbus_error_free (&error);
}

static void
gs_listener_finalize (GObject *object)
{
        GSListener *listener;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LISTENER (object));

        listener = GS_LISTENER (object);

        g_return_if_fail (listener->priv != NULL);

        /*dbus_connection_unref (listener->priv->connection);*/

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GSListener *
gs_listener_new (void)
{
        GSListener *listener;

        listener = g_object_new (GS_TYPE_LISTENER, NULL);

        return GS_LISTENER (listener);
}
