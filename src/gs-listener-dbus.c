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

#include <glib/gi18n.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gs-listener-dbus.h"

/* this is for dbus < 0.3 */
#if ((DBUS_VERSION_MAJOR == 0) && (DBUS_VERSION_MINOR < 30))
#define dbus_bus_name_has_owner(connection, name, err)      dbus_bus_service_exists(connection, name, err)
#define dbus_bus_request_name(connection, name, flags, err) dbus_bus_acquire_service(connection, name, flags, err)
#endif

static void     gs_listener_class_init (GSListenerClass *klass);
static void     gs_listener_init       (GSListener      *listener);
static void     gs_listener_finalize   (GObject         *object);

static void              gs_listener_unregister_handler (DBusConnection *connection,
                                                         void           *data);

static DBusHandlerResult gs_listener_message_handler    (DBusConnection *connection,
                                                         DBusMessage    *message,
                                                         void           *user_data);

#define GS_LISTENER_SERVICE   "org.gnome.screensaver"
#define GS_LISTENER_PATH      "/org/gnome/screensaver"
#define GS_LISTENER_INTERFACE "org.gnome.screensaver"

#define GS_LISTENER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LISTENER, GSListenerPrivate))

struct GSListenerPrivate
{
        DBusConnection *connection;

        guint           active : 1;
        guint           throttle_enabled : 1;
};

enum {
        LOCK,
        CYCLE,
        QUIT,
        POKE,
        ACTIVE_CHANGED,
        THROTTLE_ENABLED_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ACTIVE,
        PROP_THROTTLE_ENABLED,
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

static void
gs_listener_send_signal_active_changed (GSListener *listener)
{
        DBusMessage    *message;
	DBusMessageIter iter;
        dbus_bool_t     active;

        g_return_if_fail (listener != NULL);

        message = dbus_message_new_signal (GS_LISTENER_PATH,
                                           GS_LISTENER_SERVICE,
                                           "ActiveChanged");

        active = listener->priv->active;
	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &active);

        if (! dbus_connection_send (listener->priv->connection, message, NULL)) {
                g_warning ("Could not send ActiveChanged signal");
        }

        dbus_message_unref (message);
}

static void
gs_listener_send_signal_throttle_enabled_changed (GSListener *listener)
{
        DBusMessage    *message;
	DBusMessageIter iter;
        dbus_bool_t     enabled;

        g_return_if_fail (listener != NULL);

        message = dbus_message_new_signal (GS_LISTENER_PATH,
                                           GS_LISTENER_SERVICE,
                                           "ThrottleEnabledChanged");

        enabled = listener->priv->throttle_enabled;
	dbus_message_iter_init_append (message, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &enabled);

        if (! dbus_connection_send (listener->priv->connection, message, NULL)) {
                g_warning ("Could not send ThrottleEnabledChanged signal");
        }

        dbus_message_unref (message);
}

void
gs_listener_set_active (GSListener *listener,
                        gboolean    active)
{
        g_return_if_fail (GS_IS_LISTENER (listener));

        if (listener->priv->active != active) {

                listener->priv->active = active;

                g_signal_emit (listener, signals [ACTIVE_CHANGED], 0, active);
                gs_listener_send_signal_active_changed (listener);
        }
}

void
gs_listener_set_throttle_enabled (GSListener *listener,
                                  gboolean    enabled)
{
        g_return_if_fail (GS_IS_LISTENER (listener));

        if (listener->priv->throttle_enabled != enabled) {

                listener->priv->throttle_enabled = enabled;

                g_signal_emit (listener, signals [THROTTLE_ENABLED_CHANGED], 0, enabled);
                gs_listener_send_signal_throttle_enabled_changed (listener);
        }
}

static dbus_bool_t
listener_property_set_bool (GSListener *listener,
                            guint       prop_id,
                            dbus_bool_t value)
{
        switch (prop_id) {
        case PROP_ACTIVE:
                gs_listener_set_active (listener, value);
                return TRUE;
                break;
        case PROP_THROTTLE_ENABLED:
                gs_listener_set_throttle_enabled (listener, value);
                return TRUE;
                break;
        default:
                break;
        }

        return FALSE;
}

static void
raise_property_type_error (DBusConnection *connection,
                           DBusMessage    *in_reply_to,
                           const char     *device_id)
{
        char         buf [512];
        DBusMessage *reply;

        snprintf (buf, 511,
                  "Type mismatch setting property with id %s",
                  device_id);
        g_warning (buf);

        reply = dbus_message_new_error (in_reply_to,
                                        "org.gnome.screensaver.TypeMismatch",
                                        buf);
        if (reply == NULL)
                g_error ("No memory");
        if (!dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);
}

static DBusHandlerResult
listener_set_property (GSListener     *listener,
                       DBusConnection *connection,
                       DBusMessage    *message,
                       guint           prop_id)
{
        const char     *path;
        int             type;
        gboolean        rc;
        DBusMessageIter iter;
        DBusMessage    *reply;

        path = dbus_message_get_path (message);

        dbus_message_iter_init (message, &iter);
        type = dbus_message_iter_get_arg_type (&iter);
        rc = FALSE;

        switch (type) {
        case DBUS_TYPE_BOOLEAN:
                {
                        dbus_bool_t v;
                        dbus_message_iter_get_basic (&iter, &v);
                        rc = listener_property_set_bool (listener, prop_id, v);
                        break;
                }
        default:
                g_warning ("Unsupported property type %d", type);
                break;
        }

        if (! rc) {
                raise_property_type_error (connection, message, path);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        reply = dbus_message_new_method_return (message);

        if (reply == NULL)
                g_error ("No memory");

        if (!dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_get_property (GSListener     *listener,
                       DBusConnection *connection,
                       DBusMessage    *message,
                       guint           prop_id)
{
        const char     *path;
        DBusMessageIter iter;
        DBusMessage    *reply;

        path = dbus_message_get_path (message);

        reply = dbus_message_new_method_return (message);

	dbus_message_iter_init_append (reply, &iter);

        if (reply == NULL)
                g_error ("No memory");

	switch (prop_id) {
	case PROP_ACTIVE:
                {
                        dbus_bool_t b;
                        b = listener->priv->active;
                        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &b);
                }
                break;
	case PROP_THROTTLE_ENABLED:
                {
                        dbus_bool_t b;
                        b = listener->priv->throttle_enabled;
                        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &b);
                }
                break;
        default:
                g_warning ("Unsupported property id %d", prop_id);
                break;
        }

        if (!dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
gs_listener_message_handler (DBusConnection *connection,
                             DBusMessage    *message,
                             void           *user_data)
{
        GSListener *listener = GS_LISTENER (user_data);

	g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
	g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

        /*
        g_message ("obj_path=%s interface=%s method=%s destination=%s", 
                   dbus_message_get_path (message), 
                   dbus_message_get_interface (message),
                   dbus_message_get_member (message),
                   dbus_message_get_destination (message));
        */

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
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "setActive")) {
                return listener_set_property (listener, connection, message, PROP_ACTIVE);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "getActive")) {
                return listener_get_property (listener, connection, message, PROP_ACTIVE);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "setThrottleEnabled")) {
                return listener_set_property (listener, connection, message, PROP_THROTTLE_ENABLED);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "getThrottleEnabled")) {
                return listener_get_property (listener, connection, message, PROP_THROTTLE_ENABLED);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "poke")) {
                g_signal_emit (listener, signals [POKE], 0);
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
        case PROP_ACTIVE:
                gs_listener_set_active (self, g_value_get_boolean (value));
                break;
        case PROP_THROTTLE_ENABLED:
                gs_listener_set_throttle_enabled (self, g_value_get_boolean (value));
                break;
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
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->active);
                break;
        case PROP_THROTTLE_ENABLED:
                g_value_set_boolean (value, self->priv->throttle_enabled);
                break;
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
        signals [POKE] =
                g_signal_new ("poke",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, poke),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [ACTIVE_CHANGED] =
                g_signal_new ("active-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, active_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);
        signals [THROTTLE_ENABLED_CHANGED] =
                g_signal_new ("throttle-enabled-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, throttle_enabled_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_THROTTLE_ENABLED,
                                         g_param_spec_boolean ("throttle-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (GSListenerPrivate));
}


static gboolean
screensaver_is_running (DBusConnection *connection)
{
        DBusError               error;
        gboolean                exists;

        g_return_val_if_fail (connection != NULL, FALSE);

        dbus_error_init (&error);
        exists = dbus_bus_name_has_owner (connection, GS_LISTENER_SERVICE, &error);
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

        g_return_val_if_fail (listener != NULL, FALSE);

        if (!listener->priv->connection) {
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             _("failed to register with the message bus"));
                return FALSE;
        }

        if (screensaver_is_running (listener->priv->connection)) {
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             _("screensaver already running in this session"));
                return FALSE;
        }

        dbus_error_init (&buserror);

        if (dbus_connection_register_object_path (listener->priv->connection,
                                                  GS_LISTENER_PATH,
                                                  &gs_listener_vtable,
                                                  listener) == FALSE)
                g_critical ("out of memory registering object path");

        acquired = dbus_bus_request_name (listener->priv->connection,
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
