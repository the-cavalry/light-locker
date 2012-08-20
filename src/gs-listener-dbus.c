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

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <glib/gi18n.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
#endif

#include "gs-listener-dbus.h"
#include "gs-marshal.h"
#include "gs-debug.h"

#include "bus.h"

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

#define TYPE_MISMATCH_ERROR  GS_INTERFACE ".TypeMismatch"

#define GS_LISTENER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LISTENER, GSListenerPrivate))

struct GSListenerPrivate
{
        DBusConnection *connection;
        DBusConnection *system_connection;

        guint           session_idle : 1;
        guint           active : 1;
        guint           activation_enabled : 1;
        time_t          active_start;
        time_t          session_idle_start;
        char           *session_id;

#ifdef WITH_SYSTEMD
        gboolean        have_systemd;
#endif
};

enum {
        LOCK,
        QUIT,
        SIMULATE_USER_ACTIVITY,
        ACTIVE_CHANGED,
        SHOW_MESSAGE,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ACTIVE,
        PROP_SESSION_IDLE,
        PROP_ACTIVATION_ENABLED,
};

static DBusObjectPathVTable
gs_listener_vtable = { &gs_listener_unregister_handler,
                       &gs_listener_message_handler,
                       NULL,
                       NULL,
                       NULL,
                       NULL };

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSListener, gs_listener, G_TYPE_OBJECT)

GQuark
gs_listener_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark) {
                quark = g_quark_from_static_string ("gs_listener_error");
        }

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
                gs_debug ("There is no valid connection to the message bus");
                return FALSE;
        }

        is_connected = dbus_connection_get_is_connected (connection);
        if (! is_connected) {
                gs_debug ("Not connected to the message bus");
                return FALSE;
        }

        sent = dbus_connection_send (connection, message, NULL);

        return sent;
}

static void
send_dbus_boolean_signal (GSListener *listener,
                          const char *name,
                          gboolean    value)
{
        DBusMessage    *message;
        DBusMessageIter iter;

        g_return_if_fail (listener != NULL);

        message = dbus_message_new_signal (GS_PATH,
                                           GS_SERVICE,
                                           name);

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &value);

        if (! send_dbus_message (listener->priv->connection, message)) {
                gs_debug ("Could not send %s signal", name);
        }

        dbus_message_unref (message);
}

static void
gs_listener_send_signal_active_changed (GSListener *listener)
{
        g_return_if_fail (listener != NULL);

        gs_debug ("Sending the ActiveChanged(%s) signal on the session bus",
                  listener->priv->active ? "TRUE" : "FALSE");

        send_dbus_boolean_signal (listener, "ActiveChanged", listener->priv->active);
}

static gboolean
listener_check_activation (GSListener *listener)
{
        gboolean res;

        gs_debug ("Checking for activation");

        if (! listener->priv->activation_enabled) {
                return TRUE;
        }

        if (! listener->priv->session_idle) {
                return TRUE;
        }

        gs_debug ("Trying to activate");
        res = gs_listener_set_active (listener, TRUE);

        return res;
}

static gboolean
listener_set_session_idle_internal (GSListener *listener,
                                    gboolean    idle)
{
        listener->priv->session_idle = idle;

        if (idle) {
                listener->priv->session_idle_start = time (NULL);
        } else {
                listener->priv->session_idle_start = 0;
        }

        return TRUE;
}

static gboolean
listener_set_active_internal (GSListener *listener,
                              gboolean    active)
{
        listener->priv->active = active;

        /* if idle not in sync with active, change it */
        if (listener->priv->session_idle != active) {
                listener_set_session_idle_internal (listener, active);
        }

        if (active) {
                listener->priv->active_start = time (NULL);
        } else {
                listener->priv->active_start = 0;
        }

        gs_listener_send_signal_active_changed (listener);

        return TRUE;
}

gboolean
gs_listener_set_active (GSListener *listener,
                        gboolean    active)
{
        gboolean res;

        g_return_val_if_fail (GS_IS_LISTENER (listener), FALSE);

        if (listener->priv->active == active) {
                gs_debug ("Trying to set active state when already: %s",
                          active ? "active" : "inactive");
                return FALSE;
        }

        res = FALSE;
        g_signal_emit (listener, signals [ACTIVE_CHANGED], 0, active, &res);
        if (! res) {
                /* if the signal is not handled then we haven't changed state */
                gs_debug ("Active-changed signal not handled");

                /* clear the idle state */
                if (active) {
                        listener_set_session_idle_internal (listener, FALSE);
                }

                return FALSE;
        }

        listener_set_active_internal (listener, active);

        return TRUE;
}

gboolean
gs_listener_set_session_idle (GSListener *listener,
                              gboolean    idle)
{
        gboolean res;

        g_return_val_if_fail (GS_IS_LISTENER (listener), FALSE);

        gs_debug ("Setting session idle: %d", idle);

        if (listener->priv->session_idle == idle) {
                gs_debug ("Trying to set idle state when already %s",
                          idle ? "idle" : "not idle");
                return FALSE;
        }

        listener->priv->session_idle = idle;
        res = listener_check_activation (listener);

        /* if activation fails then don't set idle */
        if (res) {
                listener_set_session_idle_internal (listener, idle);
        } else {
                gs_debug ("Idle activation failed");
                listener->priv->session_idle = !idle;
        }

        return res;
}

gboolean
gs_listener_get_activation_enabled (GSListener *listener)
{
        g_return_val_if_fail (GS_IS_LISTENER (listener), FALSE);

        return listener->priv->activation_enabled;
}

void
gs_listener_set_activation_enabled (GSListener *listener,
                                    gboolean    enabled)
{
        g_return_if_fail (GS_IS_LISTENER (listener));

        if (listener->priv->activation_enabled != enabled) {
                listener->priv->activation_enabled = enabled;
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

        gs_debug (buf);
        reply = dbus_message_new_error (in_reply_to, error_name, buf);
        if (reply == NULL) {
                g_error ("No memory");
        }
        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);
}

static void
raise_syntax (DBusConnection *connection,
              DBusMessage    *in_reply_to,
              const char     *method_name)
{
        raise_error (connection, in_reply_to,
                     GS_SERVICE ".SyntaxError",
                     "There is a syntax error in the invocation of the method %s",
                     method_name);
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
        gs_debug (buf);

        reply = dbus_message_new_error (in_reply_to,
                                        TYPE_MISMATCH_ERROR,
                                        buf);
        if (reply == NULL) {
                g_error ("No memory");
        }
        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

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
                gs_debug ("Unsupported property type %d", type);
                break;
        }

        if (! rc) {
                raise_property_type_error (connection, message, path);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        reply = dbus_message_new_method_return (message);

        if (reply == NULL) {
                g_error ("No memory");
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

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
        default:
                gs_debug ("Unsupported property id %u", prop_id);
                break;
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_get_active_time (GSListener     *listener,
                          DBusConnection *connection,
                          DBusMessage    *message)
{
        DBusMessageIter iter;
        DBusMessage    *reply;
        dbus_uint32_t    secs;

        reply = dbus_message_new_method_return (message);

        dbus_message_iter_init_append (reply, &iter);

        if (reply == NULL) {
                g_error ("No memory");
        }

        if (listener->priv->active) {
                time_t now = time (NULL);

                if (now < listener->priv->active_start) {
                        /* shouldn't happen */
                        gs_debug ("Active start time is in the future");
                        secs = 0;
                } else if (listener->priv->active_start <= 0) {
                        /* shouldn't happen */
                        gs_debug ("Active start time was not set");
                        secs = 0;
                } else {
                        secs = now - listener->priv->active_start;
                }
        } else {
                secs = 0;
        }

        gs_debug ("Returning screensaver active for %u seconds", secs);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &secs);

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_show_message (GSListener     *listener,
                       DBusConnection *connection,
                       DBusMessage    *message)
{
        DBusMessageIter iter;
        DBusMessage    *reply;
        DBusError       error;

        reply = dbus_message_new_method_return (message);

        dbus_message_iter_init_append (reply, &iter);

        if (reply == NULL) {
                g_error ("No memory");
        }

        if (listener->priv->active) {
                char *summary;
                char *body;
                char *icon;

                /* if we're not active we ignore the request */

                dbus_error_init (&error);
                if (! dbus_message_get_args (message, &error,
                                             DBUS_TYPE_STRING, &summary,
                                             DBUS_TYPE_STRING, &body,
                                             DBUS_TYPE_STRING, &icon,
                                             DBUS_TYPE_INVALID)) {
                        raise_syntax (connection, message, "ShowMessage");
                        return DBUS_HANDLER_RESULT_HANDLED;
                }

                g_signal_emit (listener, signals [SHOW_MESSAGE], 0, summary, body, icon);
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
do_introspect (DBusConnection *connection,
               DBusMessage    *message,
               dbus_bool_t     local_interface)
{
        DBusMessage *reply;
        GString     *xml;
        char        *xml_string;

        /* standard header */
        xml = g_string_new ("<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
                            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
                            "<node>\n"
                            "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
                            "    <method name=\"Introspect\">\n"
                            "      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
                            "    </method>\n"
                            "  </interface>\n");

        /* ScreenSaver interface */
        xml = g_string_append (xml,
                               "  <interface name=\""GS_INTERFACE"\">\n"
                               "    <method name=\"Lock\">\n"
                               "    </method>\n"
                               "    <method name=\"SimulateUserActivity\">\n"
                               "    </method>\n"
                               "    <method name=\"GetActive\">\n"
                               "      <arg name=\"value\" direction=\"out\" type=\"b\"/>\n"
                               "    </method>\n"
                               "    <method name=\"GetActiveTime\">\n"
                               "      <arg name=\"seconds\" direction=\"out\" type=\"u\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SetActive\">\n"
                               "      <arg name=\"value\" direction=\"in\" type=\"b\"/>\n"
                               "    </method>\n"
                               "    <method name=\"ShowMessage\">\n"
                               "      <arg name=\"summary\" direction=\"in\" type=\"s\"/>\n"
                               "      <arg name=\"body\" direction=\"in\" type=\"s\"/>\n"
                               "      <arg name=\"icon\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <signal name=\"ActiveChanged\">\n"
                               "      <arg name=\"new_value\" type=\"b\"/>\n"
                               "    </signal>\n"
                               "  </interface>\n");

        reply = dbus_message_new_method_return (message);

        xml = g_string_append (xml, "</node>\n");
        xml_string = g_string_free (xml, FALSE);

        dbus_message_append_args (reply,
                                  DBUS_TYPE_STRING, &xml_string,
                                  DBUS_TYPE_INVALID);

        g_free (xml_string);

        if (reply == NULL) {
                g_error ("No memory");
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
send_success_reply (DBusConnection  *connection,
                    DBusMessage     *message)
{
        DBusMessage *reply;

        if (dbus_message_get_no_reply (message)) {
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        reply = dbus_message_new_method_return (message);
        if (reply == NULL) {
                g_error ("No memory");
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_dbus_handle_session_message (DBusConnection *connection,
                                      DBusMessage    *message,
                                      void           *user_data,
                                      dbus_bool_t     local_interface)
{
        GSListener *listener = GS_LISTENER (user_data);

#if 0
        g_message ("obj_path=%s interface=%s method=%s destination=%s",
                   dbus_message_get_path (message),
                   dbus_message_get_interface (message),
                   dbus_message_get_member (message),
                   dbus_message_get_destination (message));
#endif

        g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
        g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

        if (dbus_message_is_method_call (message, GS_SERVICE, "Lock")) {
                g_signal_emit (listener, signals [LOCK], 0);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, GS_SERVICE, "Quit")) {
                g_signal_emit (listener, signals [QUIT], 0);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, GS_SERVICE, "SetActive")) {
                return listener_set_property (listener, connection, message, PROP_ACTIVE);
        }
        if (dbus_message_is_method_call (message, GS_SERVICE, "GetActive")) {
                return listener_get_property (listener, connection, message, PROP_ACTIVE);
        }
        if (dbus_message_is_method_call (message, GS_SERVICE, "GetActiveTime")) {
                return listener_get_active_time (listener, connection, message);
        }
        if (dbus_message_is_method_call (message, GS_SERVICE, "ShowMessage")) {
                return listener_show_message (listener, connection, message);
        }
        if (dbus_message_is_method_call (message, GS_SERVICE, "SimulateUserActivity")) {
                g_signal_emit (listener, signals [SIMULATE_USER_ACTIVITY], 0);
                return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call (message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
                return do_introspect (connection, message, local_interface);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
_listener_message_path_is_our_session (GSListener  *listener,
                                       DBusMessage *message)
{
        const char *ssid;

        ssid = dbus_message_get_path (message);
        if (ssid == NULL)
                return FALSE;

        if (listener->priv->session_id == NULL)
                return FALSE;

#ifdef WITH_SYSTEMD
        /* The bus object path is simply the actual session ID
         * prefixed to make it a bus path */
        if (listener->priv->have_systemd)
                return g_str_has_prefix (ssid, SYSTEMD_LOGIND_SESSION_PATH "/")
                        && strcmp (ssid + sizeof (SYSTEMD_LOGIND_SESSION_PATH),
                                   listener->priv->session_id) == 0;
#endif

#ifdef WITH_CONSOLE_KIT
        if (strcmp (ssid, listener->priv->session_id) == 0)
                return TRUE;
#endif

        return FALSE;
}

#ifdef WITH_SYSTEMD
static gboolean
properties_changed_match (DBusMessage *message,
                          const char  *property)
{
        DBusMessageIter iter, sub, sub2;

        /* Checks whether a certain property is listed in the
         * specified PropertiesChanged message */

        if (!dbus_message_iter_init (message, &iter))
                goto failure;

        /* Jump over interface name */
        if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
                goto failure;

        dbus_message_iter_next (&iter);

        /* First, iterate through the changed properties array */
        if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type (&iter) != DBUS_TYPE_DICT_ENTRY)
                goto failure;

        dbus_message_iter_recurse (&iter, &sub);
        while (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_INVALID) {
                const char *name;

                if (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_DICT_ENTRY)
                        goto failure;

                dbus_message_iter_recurse (&sub, &sub2);
                dbus_message_iter_get_basic (&sub2, &name);

                if (strcmp (name, property) == 0)
                        return TRUE;

                dbus_message_iter_next (&sub);
        }

        dbus_message_iter_next (&iter);

        /* Second, iterate through the invalidated properties array */
        if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type (&iter) != DBUS_TYPE_STRING)
                goto failure;

        dbus_message_iter_recurse (&iter, &sub);
        while (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_INVALID) {
                const char *name;

                if (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_STRING)
                        goto failure;

                dbus_message_iter_get_basic (&sub, &name);

                if (strcmp (name, property) == 0)
                        return TRUE;

                dbus_message_iter_next (&sub);
        }

        return FALSE;

failure:
        gs_debug ("Failed to decode PropertiesChanged message.");
        return FALSE;
}
#endif

static DBusHandlerResult
listener_dbus_handle_system_message (DBusConnection *connection,
                                     DBusMessage    *message,
                                     void           *user_data,
                                     dbus_bool_t     local_interface)
{
        GSListener *listener = GS_LISTENER (user_data);

        g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
        g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

#if 1
        gs_debug ("obj_path=%s interface=%s method=%s destination=%s",
                  dbus_message_get_path (message),
                  dbus_message_get_interface (message),
                  dbus_message_get_member (message),
                  dbus_message_get_destination (message));
#endif

#ifdef WITH_SYSTEMD

        if (listener->priv->have_systemd) {

                if (dbus_message_is_signal (message, SYSTEMD_LOGIND_SESSION_INTERFACE, "Unlock")) {
                        if (_listener_message_path_is_our_session (listener, message)) {
                                gs_debug ("systemd requested session unlock");
                                gs_listener_set_active (listener, FALSE);
                        }

                        return DBUS_HANDLER_RESULT_HANDLED;
                } else if (dbus_message_is_signal (message, SYSTEMD_LOGIND_SESSION_INTERFACE, "Lock")) {
                        if (_listener_message_path_is_our_session (listener, message)) {
                                gs_debug ("systemd requested session lock");
                                g_signal_emit (listener, signals [LOCK], 0);
                        }

                        return DBUS_HANDLER_RESULT_HANDLED;
                } else if (dbus_message_is_signal (message, DBUS_INTERFACE_PROPERTIES, "PropertiesChanged")) {

                        if (_listener_message_path_is_our_session (listener, message)) {

                                if (properties_changed_match (message, "Active")) {
                                        gboolean new_active;

                                        /* Instead of going via the
                                         * bus to read the new
                                         * property state, let's
                                         * shortcut this and ask
                                         * directly the low-level
                                         * information */

                                        new_active = sd_session_is_active (listener->priv->session_id) != 0;
                                        if (new_active)
                                                g_signal_emit (listener, signals [SIMULATE_USER_ACTIVITY], 0);
                                }
                        }

                        return DBUS_HANDLER_RESULT_HANDLED;
                }

                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
#endif

#ifdef WITH_CONSOLE_KIT
        if (dbus_message_is_signal (message, CK_SESSION_INTERFACE, "Unlock")) {
                if (_listener_message_path_is_our_session (listener, message)) {
                        gs_debug ("Console kit requested session unlock");
                        gs_listener_set_active (listener, FALSE);
                }

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, CK_SESSION_INTERFACE, "Lock")) {
                if (_listener_message_path_is_our_session (listener, message)) {
                        gs_debug ("ConsoleKit requested session lock");
                        g_signal_emit (listener, signals [LOCK], 0);
                }

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, CK_SESSION_INTERFACE, "ActiveChanged")) {
                /* NB that `ActiveChanged' refers to the active
                 * session in ConsoleKit terminology - ie which
                 * session is currently displayed on the screen.
                 * gnome-screensaver uses `active' to mean `is the
                 * screensaver active' (ie, is the screen locked) but
                 * that's not what we're referring to here.
                 */

                if (_listener_message_path_is_our_session (listener, message)) {
                        DBusError   error;
                        dbus_bool_t new_active;

                        dbus_error_init (&error);
                        if (dbus_message_get_args (message, &error,
                                                   DBUS_TYPE_BOOLEAN, &new_active,
                                                   DBUS_TYPE_INVALID)) {
                                gs_debug ("ConsoleKit notified ActiveChanged %d", new_active);

                                /* when we become active poke the lock */
                                if (new_active) {
                                        g_signal_emit (listener, signals [SIMULATE_USER_ACTIVITY], 0);
                                }
                        }

                        if (dbus_error_is_set (&error)) {
                                dbus_error_free (&error);
                        }
                }

                return DBUS_HANDLER_RESULT_HANDLED;
        }
#endif

       return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult
gs_listener_message_handler (DBusConnection *connection,
                             DBusMessage    *message,
                             void           *user_data)
{
        g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
        g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

#if 0
        g_message ("obj_path=%s interface=%s method=%s destination=%s",
                   dbus_message_get_path (message),
                   dbus_message_get_interface (message),
                   dbus_message_get_member (message),
                   dbus_message_get_destination (message));
#endif

        if (dbus_message_is_method_call (message, "org.freedesktop.DBus", "AddMatch")) {
                DBusMessage *reply;

                reply = dbus_message_new_method_return (message);

                if (reply == NULL) {
                        g_error ("No memory");
                }

                if (! dbus_connection_send (connection, reply, NULL)) {
                        g_error ("No memory");
                }

                dbus_message_unref (reply);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
                   strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                dbus_connection_unref (connection);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else {
                return listener_dbus_handle_session_message (connection, message, user_data, TRUE);
        }
}

static gboolean
gs_listener_dbus_init (GSListener *listener)
{
        DBusError error;

        dbus_error_init (&error);

        if (listener->priv->connection == NULL) {
                listener->priv->connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
                if (listener->priv->connection == NULL) {
                        if (dbus_error_is_set (&error)) {
                                gs_debug ("couldn't connect to session bus: %s",
                                          error.message);
                                dbus_error_free (&error);
                        }
                        return FALSE;
                }

                dbus_connection_setup_with_g_main (listener->priv->connection, NULL);
                dbus_connection_set_exit_on_disconnect (listener->priv->connection, FALSE);
        }

        if (listener->priv->system_connection == NULL) {
                listener->priv->system_connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
                if (listener->priv->system_connection == NULL) {
                        if (dbus_error_is_set (&error)) {
                                gs_debug ("couldn't connect to system bus: %s",
                                          error.message);
                                dbus_error_free (&error);
                        }
                        return FALSE;
                }

                dbus_connection_setup_with_g_main (listener->priv->system_connection, NULL);
                dbus_connection_set_exit_on_disconnect (listener->priv->system_connection, FALSE);
        }

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

        if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected")
            && strcmp (path, DBUS_PATH_LOCAL) == 0) {

                g_message ("Got disconnected from the session message bus; "
                           "retrying to reconnect every 10 seconds");

                dbus_connection_unref (connection);
                listener->priv->connection = NULL;

                g_timeout_add (10000, (GSourceFunc)reinit_dbus, listener);
        } else if (dbus_message_is_signal (message,
                                           DBUS_INTERFACE_DBUS,
                                           "NameOwnerChanged")) {
        } else {
                return listener_dbus_handle_session_message (connection, message, user_data, FALSE);
        }

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_dbus_system_filter_function (DBusConnection *connection,
                                      DBusMessage    *message,
                                      void           *user_data)
{
        GSListener *listener = GS_LISTENER (user_data);
        const char *path;

        path = dbus_message_get_path (message);

        if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected")
            && strcmp (path, DBUS_PATH_LOCAL) == 0) {

                g_message ("Got disconnected from the system message bus; "
                           "retrying to reconnect every 10 seconds");

                dbus_connection_unref (connection);
                listener->priv->system_connection = NULL;

                g_timeout_add (10000, (GSourceFunc)reinit_dbus, listener);
        } else {
                return listener_dbus_handle_system_message (connection, message, user_data, FALSE);
        }

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
        case PROP_SESSION_IDLE:
                gs_listener_set_session_idle (self, g_value_get_boolean (value));
                break;
        case PROP_ACTIVATION_ENABLED:
                gs_listener_set_activation_enabled (self, g_value_get_boolean (value));
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
        case PROP_SESSION_IDLE:
                g_value_set_boolean (value, self->priv->session_idle);
                break;
        case PROP_ACTIVATION_ENABLED:
                g_value_set_boolean (value, self->priv->activation_enabled);
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
        signals [SIMULATE_USER_ACTIVITY] =
                g_signal_new ("simulate-user-activity",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, simulate_user_activity),
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
        signals [SHOW_MESSAGE] =
                g_signal_new ("show-message",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, show_message),
                              NULL,
                              NULL,
                              gs_marshal_VOID__STRING_STRING_STRING,
                              G_TYPE_NONE,
                              3,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_ACTIVATION_ENABLED,
                                         g_param_spec_boolean ("activation-enabled",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
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
        exists = dbus_bus_name_has_owner (connection, GS_SERVICE, &error);
        if (dbus_error_is_set (&error)) {
                dbus_error_free (&error);
        }

        return exists;
}

gboolean
gs_listener_acquire (GSListener *listener,
                     GError    **error)
{
        int       res;
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
                                                  GS_PATH,
                                                  &gs_listener_vtable,
                                                  listener) == FALSE) {
                g_critical ("out of memory registering object path");
                return FALSE;
        }

        res = dbus_bus_request_name (listener->priv->connection,
                                     GS_SERVICE,
                                     DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                     &buserror);
        if (dbus_error_is_set (&buserror)) {
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             buserror.message);
        }
        if (res == DBUS_REQUEST_NAME_REPLY_EXISTS) {
                g_set_error (error,
                             GS_LISTENER_ERROR,
                             GS_LISTENER_ERROR_ACQUISITION_FAILURE,
                             "%s",
                             _("screensaver already running in this session"));
                return FALSE;
        }

        dbus_error_free (&buserror);

        dbus_connection_add_filter (listener->priv->connection, listener_dbus_filter_function, listener, NULL);

        dbus_bus_add_match (listener->priv->connection,
                            "type='signal'"
                            ",interface='"DBUS_INTERFACE_DBUS"'"
                            ",sender='"DBUS_SERVICE_DBUS"'"
                            ",member='NameOwnerChanged'",
                            NULL);

        if (listener->priv->system_connection != NULL) {
                dbus_connection_add_filter (listener->priv->system_connection,
                                            listener_dbus_system_filter_function,
                                            listener,
                                            NULL);
#ifdef WITH_SYSTEMD
                if (listener->priv->have_systemd) {
                        dbus_bus_add_match (listener->priv->system_connection,
                                            "type='signal'"
                                            ",sender='"SYSTEMD_LOGIND_SERVICE"'"
                                            ",interface='"SYSTEMD_LOGIND_SESSION_INTERFACE"'"
                                            ",member='Unlock'",
                                            NULL);
                        dbus_bus_add_match (listener->priv->system_connection,
                                            "type='signal'"
                                            ",sender='"SYSTEMD_LOGIND_SERVICE"'"
                                            ",interface='"SYSTEMD_LOGIND_SESSION_INTERFACE"'"
                                            ",member='Lock'",
                                            NULL);
                        dbus_bus_add_match (listener->priv->system_connection,
                                            "type='signal'"
                                            ",sender='"SYSTEMD_LOGIND_SERVICE"'"
                                            ",interface='"DBUS_INTERFACE_PROPERTIES"'"
                                            ",member='PropertiesChanged'",
                                            NULL);

                        return (res != -1);
                }
#endif

#ifdef WITH_CONSOLE_KIT
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",interface='"CK_SESSION_INTERFACE"'"
                                    ",member='Unlock'",
                                    NULL);
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",interface='"CK_SESSION_INTERFACE"'"
                                    ",member='Lock'",
                                    NULL);
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",interface='"CK_SESSION_INTERFACE"'"
                                    ",member='ActiveChanged'",
                                    NULL);
#endif
        }

        return (res != -1);
}

static char *
query_session_id (GSListener *listener)
{
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       error;
        DBusMessageIter reply_iter;
        char           *ssid;

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return NULL;
        }

        ssid = NULL;

        dbus_error_init (&error);

#ifdef WITH_SYSTEMD
        if (listener->priv->have_systemd) {
                char *t;
                int r;

                r = sd_pid_get_session (0, &t);
                if (r < 0) {
                        gs_debug ("Couldn't determine our own session id: %s", strerror (-r));
                        return NULL;
                }

                /* t is allocated with malloc(), we need it with g_malloc() */
                ssid = g_strdup(t);
                free (t);

                return ssid;
        }
#endif

#ifdef WITH_CONSOLE_KIT
        message = dbus_message_new_method_call (CK_SERVICE, CK_MANAGER_PATH, CK_MANAGER_INTERFACE, "GetCurrentSession");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return NULL;
        }

        /* FIXME: use async? */
        reply = dbus_connection_send_with_reply_and_block (listener->priv->system_connection,
                                                           message,
                                                           -1, &error);
        dbus_message_unref (message);

        if (dbus_error_is_set (&error)) {
                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                dbus_error_free (&error);
                return NULL;
        }

        dbus_message_iter_init (reply, &reply_iter);
        dbus_message_iter_get_basic (&reply_iter, &ssid);

        dbus_message_unref (reply);

        return g_strdup (ssid);
#else
        return NULL;
#endif
}

static void
init_session_id (GSListener *listener)
{
        g_free (listener->priv->session_id);
        listener->priv->session_id = query_session_id (listener);
        gs_debug ("Got session-id: %s", listener->priv->session_id);
}

static void
gs_listener_init (GSListener *listener)
{
        listener->priv = GS_LISTENER_GET_PRIVATE (listener);

#ifdef WITH_SYSTEMD
        listener->priv->have_systemd = sd_booted () > 0;
#endif

        gs_listener_dbus_init (listener);

        init_session_id (listener);
}

static void
gs_listener_finalize (GObject *object)
{
        GSListener *listener;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LISTENER (object));

        listener = GS_LISTENER (object);

        g_return_if_fail (listener->priv != NULL);

        g_free (listener->priv->session_id);

        G_OBJECT_CLASS (gs_listener_parent_class)->finalize (object);
}

GSListener *
gs_listener_new (void)
{
        GSListener *listener;

        listener = g_object_new (GS_TYPE_LISTENER, NULL);

        return GS_LISTENER (listener);
}
