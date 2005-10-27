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
#include "gs-marshal.h"

/* this is for dbus < 0.3 */
#if ((DBUS_VERSION_MAJOR == 0) && (DBUS_VERSION_MINOR < 30))
#define dbus_bus_name_has_owner(connection, name, err)      dbus_bus_service_exists(connection, name, err)
#define dbus_bus_request_name(connection, name, flags, err) dbus_bus_acquire_service(connection, name, flags, err)
#endif

static void              gs_listener_class_init         (GSListenerClass *klass);
static void              gs_listener_init               (GSListener      *listener);
static void              gs_listener_finalize           (GObject         *object);

static void              gs_listener_unregister_handler (DBusConnection  *connection,
                                                         void            *data);

static DBusHandlerResult gs_listener_message_handler    (DBusConnection  *connection,
                                                         DBusMessage     *message,
                                                         void            *user_data);

#define GS_LISTENER_SERVICE   "org.gnome.screensaver"
#define GS_LISTENER_PATH      "/org/gnome/screensaver"
#define GS_LISTENER_INTERFACE "org.gnome.screensaver"

#define GS_LISTENER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LISTENER, GSListenerPrivate))

struct GSListenerPrivate
{
        DBusConnection *connection;

        guint           idle : 1;
        guint           active : 1;
        guint           throttle_enabled : 1;
        GHashTable     *inhibitors;

        time_t          idle_start;
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
        PROP_IDLE,
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

G_DEFINE_TYPE (GSListener, gs_listener, G_TYPE_OBJECT)

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

static gboolean
send_dbus_message (DBusConnection *connection,
                   DBusMessage    *message)
{
        gboolean is_connected;
        gboolean sent;

        g_return_val_if_fail (message != NULL, FALSE);

        if (! connection) {
                g_warning ("There is no valid connection to the message bus");
                return FALSE;
        }

        is_connected = dbus_connection_get_is_connected (connection);
        if (! is_connected) {
                g_warning ("Not connected to the message bus");
                return FALSE;
        }

        sent = dbus_connection_send (connection, message, NULL);

        return sent;
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

        if (! send_dbus_message (listener->priv->connection, message)) {
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

        if (! send_dbus_message (listener->priv->connection, message)) {
                g_warning ("Could not send ThrottleEnabledChanged signal");
        }

        dbus_message_unref (message);
}

#ifdef DEBUG_INHIBITORS
static void
list_inhibitors (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
        g_message ("Inhibited by bus %s for reason: %s", (char *)key, (char *)value);
}
#endif

static void
listener_check_activation (GSListener *listener)
{
        guint n_inhibitors = 0;

        /* if we aren't inhibited then activate */
        if (listener->priv->inhibitors)
                n_inhibitors = g_hash_table_size (listener->priv->inhibitors);

#ifdef DEBUG_INHIBITORS
        g_hash_table_foreach (listener->priv->inhibitors, list_inhibitors, NULL);
#endif

        if (listener->priv->idle
            && n_inhibitors == 0) {
                gs_listener_set_active (listener, TRUE);
        }
}

gboolean
gs_listener_set_active (GSListener *listener,
                        gboolean    active)
{
        gboolean res;

        g_return_val_if_fail (GS_IS_LISTENER (listener), FALSE);

        if (listener->priv->active != active) {

                res = FALSE;
                g_signal_emit (listener, signals [ACTIVE_CHANGED], 0, active, &res);
                if (! res) {
                        /* if the signal is not handled then we haven't changed state */

                        /* clear the idle state */
                        if (active) {
                                gs_listener_set_idle (listener, FALSE);
                        }

                        return FALSE;
                }

                listener->priv->active = active;
                gs_listener_send_signal_active_changed (listener);

                if (! active) {
                        /* if we are deactivating then reset the throttle */
                        gs_listener_set_throttle_enabled (listener, FALSE);
                        /* if we are deactivating then reset the idle */
                        listener->priv->idle = active;
                        listener->priv->idle_start = 0;
                }
        }

        return TRUE;
}

gboolean
gs_listener_set_idle (GSListener *listener,
                      gboolean    idle)
{
        g_return_val_if_fail (GS_IS_LISTENER (listener), FALSE);

        if (listener->priv->idle == idle) {
                g_warning ("Trying to set idle when already idle");
                return FALSE;
        }

        if (idle) {
                guint n_inhibitors = 0;

                /* if we aren't inhibited then set idle */
                if (listener->priv->inhibitors)
                        n_inhibitors = g_hash_table_size (listener->priv->inhibitors);

                if (n_inhibitors != 0) {
                        return FALSE;
                }

                listener->priv->idle_start = time (NULL);
        } else {
                listener->priv->idle_start = 0;
        }

        listener->priv->idle = idle;

        listener_check_activation (listener);

        return TRUE;
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
        dbus_bool_t ret;

        ret = FALSE;

        switch (prop_id) {
        case PROP_ACTIVE:
                gs_listener_set_active (listener, value);
                ret = TRUE;
                break;
        case PROP_IDLE:
                gs_listener_set_idle (listener, value);
                ret = TRUE;
                break;
        case PROP_THROTTLE_ENABLED:
                gs_listener_set_throttle_enabled (listener, value);
                ret = TRUE;
                break;
        default:
                break;
        }

        return ret;
}

static void
raise_error (DBusConnection *connection,
             DBusMessage    *in_reply_to,
             const char     *error_name,
             char           *format, ...)
{
        char         buf[512];
        DBusMessage *reply;

        va_list args;
        va_start (args, format);
        vsnprintf (buf, sizeof (buf), format, args);
        va_end (args);

        g_warning (buf);
        reply = dbus_message_new_error (in_reply_to, error_name, buf);
        if (reply == NULL)
                g_error ("No memory");
        if (! dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);
}

static void
raise_syntax (DBusConnection *connection,
              DBusMessage    *in_reply_to,
              const char     *method_name)
{
        raise_error (connection, in_reply_to,
                     GS_LISTENER_SERVICE ".SyntaxError",
                     "There is a syntax error in the invocation of the method %s",
                     method_name);
}

static DBusHandlerResult
listener_add_inhibitor (GSListener     *listener,
                        DBusConnection *connection,
                        DBusMessage    *message)
{
        const char     *sender;
        DBusMessage    *reply;
        DBusError       error;
        char           *reason;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_STRING, &reason,
                                     DBUS_TYPE_INVALID)) {
                raise_syntax (connection, message, "InhibitActivation");

                return DBUS_HANDLER_RESULT_HANDLED;
        }

        reply = dbus_message_new_method_return (message);
        if (reply == NULL)
                g_error ("No memory");

        sender = dbus_message_get_sender (message);

        if (listener->priv->inhibitors == NULL) {
                listener->priv->inhibitors =
                        g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               g_free,
                                               g_free);
        }

        g_hash_table_insert (listener->priv->inhibitors, g_strdup (sender), g_strdup (reason));

        if (! dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_remove_inhibitor (GSListener     *listener,
                           DBusConnection *connection,
                           DBusMessage    *message)
{
        DBusMessage *reply;
        DBusError    error;
        const char  *sender;

        dbus_error_init (&error);
        if (! dbus_message_get_args (message, &error,
                                     DBUS_TYPE_INVALID)) {
                raise_syntax (connection, message, "AllowActivation");

                return DBUS_HANDLER_RESULT_HANDLED;
        }

        reply = dbus_message_new_method_return (message);
        if (reply == NULL)
                g_error ("No memory");

        sender = dbus_message_get_sender (message);

        if (g_hash_table_lookup (listener->priv->inhibitors, sender)) {
                g_hash_table_remove (listener->priv->inhibitors, sender);
                listener_check_activation (listener);
        } else {
                g_warning ("Service '%s' was not in the list of inhibitors!", sender);
        }

        /* FIXME?  Pointless? */
        if (! dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static void
listener_service_deleted (GSListener  *listener,
                          DBusMessage *message)
{
        char *old_service_name;
        char *new_service_name;
        char *reason;

        if (! dbus_message_get_args (message, NULL,
                                     DBUS_TYPE_STRING, &old_service_name,
                                     DBUS_TYPE_STRING, &new_service_name,
                                     DBUS_TYPE_INVALID)) {
                g_error ("Invalid NameOwnerChanged signal from bus!");
                return;
        }

        reason = g_hash_table_lookup (listener->priv->inhibitors, new_service_name);

        if (reason != NULL) {
                g_hash_table_remove (listener->priv->inhibitors, new_service_name);
                listener_check_activation (listener);
        }
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
        if (! dbus_connection_send (connection, reply, NULL))
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

        if (! dbus_connection_send (connection, reply, NULL))
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
        DBusMessageIter iter;
        DBusMessage    *reply;

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
        case PROP_IDLE:
                {
                        dbus_bool_t b;
                        b = listener->priv->idle;
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
                g_warning ("Unsupported property id %u", prop_id);
                break;
        }

        if (! dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_get_idle_time (GSListener     *listener,
                        DBusConnection *connection,
                        DBusMessage    *message)
{
        DBusMessageIter iter;
        DBusMessage    *reply;
        dbus_uint32_t    secs;

        reply = dbus_message_new_method_return (message);

        dbus_message_iter_init_append (reply, &iter);

        if (reply == NULL)
                g_error ("No memory");

        if (listener->priv->idle) {
                time_t now = time (NULL);

                if (now < listener->priv->idle_start) {
                        /* should't happen */
                        g_warning ("Idle start time is in the future");
                        secs = 0;
                } else {
                        secs = listener->priv->idle_start - now;
                }
        } else {
                secs = 0;
        }
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &secs);

        if (! dbus_connection_send (connection, reply, NULL))
                g_error ("No memory");

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_dbus_filter_handle_methods (DBusConnection *connection,
                                     DBusMessage    *message, 
                                     void           *user_data,
                                     dbus_bool_t     local_interface)
{
        GSListener *listener = GS_LISTENER (user_data);

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
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "InhibitActivation")) {
                return listener_add_inhibitor (listener, connection, message);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "AllowActivation")) {
                return listener_remove_inhibitor (listener, connection, message);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "setActive")) {
                return listener_set_property (listener, connection, message, PROP_ACTIVE);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "getActive")) {
                return listener_get_property (listener, connection, message, PROP_ACTIVE);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "getIdle")) {
                return listener_get_property (listener, connection, message, PROP_IDLE);
        }
        if (dbus_message_is_method_call (message, GS_LISTENER_SERVICE, "getIdleTime")) {
                return listener_get_idle_time (listener, connection, message);
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

static DBusHandlerResult
gs_listener_message_handler (DBusConnection *connection,
                             DBusMessage    *message,
                             void           *user_data)
{
        g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
        g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

        /*
        g_message ("obj_path=%s interface=%s method=%s destination=%s", 
                   dbus_message_get_path (message), 
                   dbus_message_get_interface (message),
                   dbus_message_get_member (message),
                   dbus_message_get_destination (message));
        */

        if (dbus_message_is_method_call (message, "org.freedesktop.DBus", "AddMatch")) {
                DBusMessage *reply;

                /* cheat, and handle AddMatch since libhal will try to invoke this method */
                reply = dbus_message_new_method_return (message);

                if (reply == NULL)
                        g_error ("No memory");

                if (! dbus_connection_send (connection, reply, NULL))
                        g_error ("No memory");

                dbus_message_unref (reply);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
                   strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                
                dbus_connection_unref (connection);

                return DBUS_HANDLER_RESULT_HANDLED;
        } 
        else return listener_dbus_filter_handle_methods (connection, message, user_data, TRUE);
}

static gboolean
gs_listener_dbus_init (GSListener *listener)
{
        DBusError error;

        dbus_error_init (&error);
        listener->priv->connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
        if (listener->priv->connection == NULL) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("couldn't connect to session bus: %s",
                                   error.message);
                        dbus_error_free (&error);
                }
                return FALSE;
        }

        dbus_connection_setup_with_g_main (listener->priv->connection, NULL);
	dbus_connection_set_exit_on_disconnect (listener->priv->connection, FALSE);

        return TRUE;
}

static gboolean
reinit_dbus (GSListener *listener)
{
        gboolean initialized;
        gboolean try_again;

        initialized = gs_listener_dbus_init (listener);

        /* if we didn't initialize then try again */
        /* FIXME: Should we keep trying forever?  If we fail more than
           once or twice then the session bus may have died.  The
           problem is that if it is restarted it will likely have a
           different bus address and we won't be able to find it */
        try_again = !initialized;

        return try_again;
}

static DBusHandlerResult
listener_dbus_filter_function (DBusConnection *connection,
                               DBusMessage    *message,
                               void           *user_data)
{
        GSListener *listener = GS_LISTENER (user_data);
        const char *path;

        path = dbus_message_get_path (message);

        /*
        g_message ("obj_path=%s interface=%s method=%s", 
                   dbus_message_get_path (message), 
                   dbus_message_get_interface (message),
                   dbus_message_get_member (message));
        */

        if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (path, DBUS_PATH_LOCAL) == 0) {

                g_message ("Got disconnected from the system message bus; "
                           "retrying to reconnect every 10 seconds");

                dbus_connection_unref (connection);
                connection = NULL;

                g_timeout_add (10000, (GSourceFunc)reinit_dbus, listener);
        } else if (dbus_message_is_signal (message,
                                           DBUS_INTERFACE_DBUS,
                                           "NameOwnerChanged")) {

                if (listener->priv->inhibitors != NULL)
                        listener_service_deleted (listener, message);
        } else 
                return listener_dbus_filter_handle_methods (connection, message, user_data, FALSE);

        return DBUS_HANDLER_RESULT_HANDLED;
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
        case PROP_IDLE:
                gs_listener_set_idle (self, g_value_get_boolean (value));
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
        case PROP_IDLE:
                g_value_set_boolean (value, self->priv->idle);
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
                              gs_marshal_BOOLEAN__BOOLEAN,
                              G_TYPE_BOOLEAN,
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
        DBusError error;
        gboolean  exists;
              
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
        gboolean  acquired;
        DBusError buserror;
        gboolean  is_connected;

        g_return_val_if_fail (listener != NULL, FALSE);

        if (! listener->priv->connection) {
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             _("failed to register with the message bus"));
                return FALSE;
        }

        is_connected = dbus_connection_get_is_connected (listener->priv->connection);
        if (! is_connected) {
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             _("not connected to the message bus"));
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
                                                  listener) == FALSE) {
                g_critical ("out of memory registering object path");
                return FALSE;
        }

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

        dbus_connection_add_filter (listener->priv->connection, listener_dbus_filter_function, listener, NULL);

        dbus_bus_add_match (listener->priv->connection,
                            "type='signal'"
                            ",interface='"DBUS_INTERFACE_DBUS"'"
                            ",sender='"DBUS_SERVICE_DBUS"'"
                            ",member='NameOwnerChanged'",
                            NULL);

        return acquired;
}

static void
gs_listener_init (GSListener *listener)
{
        listener->priv = GS_LISTENER_GET_PRIVATE (listener);

        gs_listener_dbus_init (listener);
}

static void
gs_listener_finalize (GObject *object)
{
        GSListener *listener;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LISTENER (object));

        listener = GS_LISTENER (object);

        g_return_if_fail (listener->priv != NULL);

        if (listener->priv->connection)
                dbus_connection_unref (listener->priv->connection);

        if (listener->priv->inhibitors)
                g_hash_table_destroy (listener->priv->inhibitors);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GSListener *
gs_listener_new (void)
{
        GSListener *listener;

        listener = g_object_new (GS_TYPE_LISTENER, NULL);

        return GS_LISTENER (listener);
}
