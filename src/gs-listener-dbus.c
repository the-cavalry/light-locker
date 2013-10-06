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
#include <unistd.h>

#include <glib/gi18n.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#endif

#include "gs-listener-dbus.h"
#include "gs-marshal.h"
#include "gs-debug.h"
#include "gs-bus.h"

static void              gs_listener_class_init         (GSListenerClass *klass);
static void              gs_listener_init               (GSListener      *listener);
static void              gs_listener_finalize           (GObject         *object);

#define GS_LISTENER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LISTENER, GSListenerPrivate))

struct GSListenerPrivate
{
        DBusConnection *system_connection;

        guint           active : 1;
        char           *session_id;
        char           *seat_path;

#ifdef WITH_SYSTEMD
        gboolean        have_systemd;
        char           *sd_session_id;
#endif
};

enum {
        LOCK,
        SESSION_SWITCHED,
        ACTIVE_CHANGED,
        SUSPEND,
        RESUME,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ACTIVE,
};

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSListener, gs_listener, G_TYPE_OBJECT)

void
gs_listener_send_switch_greeter (GSListener *listener)
{
        dbus_bool_t sent;
        DBusMessage *message;

#ifdef WITH_SYSTEMD
        /* Compare with 0. On failure this will return < 0.
         * In the later case we probably aren't using systemd.
         */
        if (sd_session_is_active (listener->priv->sd_session_id) == 0) {
                gs_debug ("Refusing to switch to greeter");
                return;
        };
#endif

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return;
        }

        message = dbus_message_new_method_call (DM_SERVICE,
                                                listener->priv->seat_path,
                                                DM_SEAT_INTERFACE,
                                                "SwitchToGreeter");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return;
        }

        sent = dbus_connection_send (listener->priv->system_connection, message, NULL);
        dbus_message_unref (message);

        if (sent == FALSE) {
                gs_debug ("Couldn't send the dbus message");
                return;
        }
}

void
gs_listener_send_lock_session (GSListener *listener)
{
        dbus_bool_t sent;
        DBusMessage *message;

#ifdef WITH_SYSTEMD
        /* Compare with 0. On failure this will return < 0.
         * In the later case we probably aren't using systemd.
         */
        if (sd_session_is_active (listener->priv->sd_session_id) == 0) {
                gs_debug ("Refusing to lock session");
                return;
        };
#endif

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return;
        }

        message = dbus_message_new_method_call (DM_SERVICE,
                                                DM_SESSION_PATH,
                                                DM_SESSION_INTERFACE,
                                                "Lock");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return;
        }

        sent = dbus_connection_send (listener->priv->system_connection, message, NULL);
        dbus_message_unref (message);

        if (sent == FALSE) {
                gs_debug ("Couldn't send the dbus message");
                return;
        }
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

                return FALSE;
        }

        listener->priv->active = active;

        return TRUE;
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

        if (strcmp (ssid, listener->priv->session_id) == 0)
                return TRUE;

        return FALSE;
}

#ifdef WITH_SYSTEMD
static gboolean
query_session_active (GSListener *listener)
{
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       error;
        DBusMessageIter reply_iter;
        DBusMessageIter sub_iter;
        dbus_bool_t     active = FALSE;
        const char     *interface;
        const char     *property;

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return FALSE;
        }

        dbus_error_init (&error);

        message = dbus_message_new_method_call (SYSTEMD_LOGIND_SERVICE, listener->priv->session_id, DBUS_PROPERTIES_INTERFACE, "Get");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return FALSE;
        }

        interface = SYSTEMD_LOGIND_SESSION_INTERFACE;
        property = "Active";

        if (dbus_message_append_args (message, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID) == FALSE) {
                gs_debug ("Couldn't add args to the dbus message");
                dbus_message_unref (message);
                return FALSE;
        }

        /* FIXME: use async? */
        reply = dbus_connection_send_with_reply_and_block (listener->priv->system_connection,
                                                           message,
                                                           -1, &error);
        dbus_message_unref (message);

        if (dbus_error_is_set (&error)) {
                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                dbus_error_free (&error);
                return FALSE;
        }

        if (dbus_message_iter_init (reply, &reply_iter) == TRUE
            && dbus_message_iter_get_arg_type (&reply_iter) == DBUS_TYPE_VARIANT) {

                dbus_message_iter_recurse (&reply_iter, &sub_iter);

                if (dbus_message_iter_get_arg_type (&sub_iter) == DBUS_TYPE_BOOLEAN) {
                        dbus_message_iter_get_basic (&sub_iter, &active);
                } else {
                        gs_debug ("Unexpected return type");
                }
        } else {
                gs_debug ("Unexpected return type");
        }

        dbus_message_unref (reply);

        return active;
}
#endif

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

        if (dbus_message_iter_next (&iter) == FALSE)
                goto failure;

        /* First, iterate through the changed properties array */
        if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type (&iter) != DBUS_TYPE_DICT_ENTRY)
                goto failure;

        dbus_message_iter_recurse (&iter, &sub);
        while (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_INVALID) {
                const char *name = NULL;

                if (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_DICT_ENTRY)
                        goto failure;

                dbus_message_iter_recurse (&sub, &sub2);

                if (dbus_message_iter_get_arg_type (&sub2) != DBUS_TYPE_STRING)
                        goto failure;

                dbus_message_iter_get_basic (&sub2, &name);

                if (g_strcmp0 (name, property) == 0)
                        return TRUE;

                dbus_message_iter_next (&sub);
        }

        if (dbus_message_iter_next (&iter) == FALSE)
                goto failure;

        /* Second, iterate through the invalidated properties array */
        if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type (&iter) != DBUS_TYPE_STRING)
                goto failure;

        dbus_message_iter_recurse (&iter, &sub);
        while (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_INVALID) {
                const char *name = NULL;

                if (dbus_message_iter_get_arg_type (&sub) != DBUS_TYPE_STRING)
                        goto failure;

                dbus_message_iter_get_basic (&sub, &name);

                if (g_strcmp0 (name, property) == 0)
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

                        /* Use the seat property ActiveSession.
                         * The session property Active only seems to be signalled when it becomes active.
                         */
                        if (properties_changed_match (message, "ActiveSession")) {
                                gboolean new_active;

                                /* Do a DBus query, since the sd_session_is_active isn't up to date. */
                                new_active = query_session_active (listener);
                                g_signal_emit (listener, signals [SESSION_SWITCHED], 0, new_active);
                        }

                        return DBUS_HANDLER_RESULT_HANDLED;
                }

                if (dbus_message_is_signal (message, SYSTEMD_LOGIND_INTERFACE, "PrepareForSleep")) {
                        DBusError   error;
                        dbus_bool_t new_active;

                        dbus_error_init (&error);
                        if (dbus_message_get_args (message, &error,
                                                   DBUS_TYPE_BOOLEAN, &new_active,
                                                   DBUS_TYPE_INVALID)) {
                                gs_debug ("systemd initiating %s", new_active ? "sleep" : "resume");

                                g_signal_emit (listener, signals [new_active ? SUSPEND : RESUME], 0);
                        }

                        if (dbus_error_is_set (&error)) {
                                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                                dbus_error_free (&error);
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
                 * light-locker uses `active' to mean `is the
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

                                g_signal_emit (listener, signals [SESSION_SWITCHED], 0, new_active);
                        }

                        if (dbus_error_is_set (&error)) {
                                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                                dbus_error_free (&error);
                        }
                }

                return DBUS_HANDLER_RESULT_HANDLED;
        }
#endif

#ifdef WITH_UPOWER
        if (dbus_message_is_signal (message, UP_INTERFACE, "Sleeping")) {
                gs_debug ("UPower initiating sleep");
                g_signal_emit (listener, signals [SUSPEND], 0);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_message_is_signal (message, UP_INTERFACE, "Resuming")) {
                gs_debug ("UPower initiating resume");
                g_signal_emit (listener, signals [RESUME], 0);

                return DBUS_HANDLER_RESULT_HANDLED;
        }
#endif

       return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
gs_listener_dbus_init (GSListener *listener)
{
        DBusError error;

        dbus_error_init (&error);

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
listener_dbus_system_filter_function (DBusConnection *connection,
                                      DBusMessage    *message,
                                      void           *user_data)
{
        GSListener *listener = GS_LISTENER (user_data);
        const char *path;

        path = dbus_message_get_path (message);

        if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") == TRUE
            && g_strcmp0 (path, DBUS_PATH_LOCAL) == 0) {

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
        signals [SESSION_SWITCHED] =
                g_signal_new ("session-switched",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, session_switched),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);
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
        signals [SUSPEND] =
                g_signal_new ("suspend",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, suspend),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [RESUME] =
                g_signal_new ("resume",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, resume),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (GSListenerPrivate));
}


gboolean
gs_listener_acquire (GSListener *listener)
{
        g_return_val_if_fail (listener != NULL, FALSE);

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

                        dbus_bus_add_match (listener->priv->system_connection,
                                            "type='signal'"
                                            ",sender='"SYSTEMD_LOGIND_SERVICE"'"
                                            ",interface='"SYSTEMD_LOGIND_INTERFACE"'"
                                            ",member='PrepareForSleep'",
                                            NULL);

                        return TRUE;
                }
#endif

#ifdef WITH_CONSOLE_KIT
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",sender='"CK_SERVICE"'"
                                    ",interface='"CK_SESSION_INTERFACE"'"
                                    ",member='Unlock'",
                                    NULL);
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",sender='"CK_SERVICE"'"
                                    ",interface='"CK_SESSION_INTERFACE"'"
                                    ",member='Lock'",
                                    NULL);
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",sender='"CK_SERVICE"'"
                                    ",interface='"CK_SESSION_INTERFACE"'"
                                    ",member='ActiveChanged'",
                                    NULL);
#endif

#ifdef WITH_UPOWER
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",sender='"UP_SERVICE"'"
                                    ",interface='"UP_INTERFACE"'"
                                    ",member='Sleeping'",
                                    NULL);
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",sender='"UP_SERVICE"'"
                                    ",interface='"UP_INTERFACE"'"
                                    ",member='Resuming'",
                                    NULL);
#endif
        }

        return TRUE;
}

static char *
query_session_id (GSListener *listener)
{
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       error;
        char           *ssid;

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return NULL;
        }

        ssid = NULL;

        dbus_error_init (&error);

#ifdef WITH_SYSTEMD
        if (listener->priv->have_systemd) {
                dbus_uint32_t pid = getpid();

                message = dbus_message_new_method_call (SYSTEMD_LOGIND_SERVICE, SYSTEMD_LOGIND_PATH, SYSTEMD_LOGIND_INTERFACE, "GetSessionByPID");
                if (message == NULL) {
                        gs_debug ("Couldn't allocate the dbus message");
                        return NULL;
                }

                if (dbus_message_append_args (message, DBUS_TYPE_UINT32, &pid, DBUS_TYPE_INVALID) == FALSE) {
                        gs_debug ("Couldn't add args to the dbus message");
                        dbus_message_unref (message);
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

                if (dbus_message_get_args (reply, &error, 
                                           DBUS_TYPE_OBJECT_PATH, &ssid,
                                           DBUS_TYPE_INVALID)) {
                        ssid = g_strdup (ssid);
                } else {
                        ssid = NULL;
                }

                dbus_message_unref (reply);

                if (dbus_error_is_set (&error)) {
                        gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                        dbus_error_free (&error);
                        return NULL;
                }

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

        if (dbus_message_get_args (reply, &error, 
                                   DBUS_TYPE_OBJECT_PATH, &ssid,
                                   DBUS_TYPE_INVALID)) {
                ssid = g_strdup (ssid);
        } else {
                ssid = NULL;
        }

        dbus_message_unref (reply);

        if (dbus_error_is_set (&error)) {
                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                dbus_error_free (&error);
                return NULL;
        }

        return ssid;
#else
        return NULL;
#endif
}

#ifdef WITH_SYSTEMD
static char *
query_sd_session_id (GSListener *listener)
{
      char *ssid;
      char *t;
      int r;

      r = sd_pid_get_session (0, &t);
      if (r < 0) {
              gs_debug ("Couldn't determine our own sd session id: %s", strerror (-r));
              return NULL;
      }

      ssid = g_strdup (t);
      free (t);

      return ssid;
}
#endif

static void
init_session_id (GSListener *listener)
{
        g_free (listener->priv->session_id);
        listener->priv->session_id = query_session_id (listener);
        gs_debug ("Got session-id: %s", listener->priv->session_id);

#ifdef WITH_SYSTEMD
        g_free (listener->priv->sd_session_id);
        listener->priv->sd_session_id = query_sd_session_id (listener);
        gs_debug ("Got sd-session-id: %s", listener->priv->sd_session_id);
#endif
}

static char *
query_seat_path (GSListener *listener)
{
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       error;
        DBusMessageIter reply_iter;
        DBusMessageIter sub_iter;
        char           *seat;
        const char     *interface;
        const char     *property;

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return NULL;
        }

        seat = NULL;

        dbus_error_init (&error);

        message = dbus_message_new_method_call (DM_SERVICE, DM_SESSION_PATH, DBUS_PROPERTIES_INTERFACE, "Get");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return NULL;
        }

        interface = DM_SESSION_INTERFACE;
        property = "Seat";

        if (dbus_message_append_args (message, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID) == FALSE) {
                gs_debug ("Couldn't add args to the dbus message");
                dbus_message_unref (message);
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

        if (dbus_message_iter_init (reply, &reply_iter) == TRUE
            && dbus_message_iter_get_arg_type (&reply_iter) == DBUS_TYPE_VARIANT) {

                dbus_message_iter_recurse (&reply_iter, &sub_iter);

                if (dbus_message_iter_get_arg_type (&sub_iter) == DBUS_TYPE_OBJECT_PATH) {
                        dbus_message_iter_get_basic (&sub_iter, &seat);
                        seat = g_strdup (seat);
                } else {
                        gs_debug ("Unexpected return type");
                }
        } else {
                gs_debug ("Unexpected return type");
        }

        dbus_message_unref (reply);

        return seat;
}

static void
init_seat_path (GSListener *listener)
{
        g_free (listener->priv->seat_path);
        listener->priv->seat_path = query_seat_path (listener);
        gs_debug ("Got seat: %s", listener->priv->seat_path);
}

static void
gs_listener_init (GSListener *listener)
{
        listener->priv = GS_LISTENER_GET_PRIVATE (listener);

#ifdef WITH_SYSTEMD
        /* check if logind is running */
        listener->priv->have_systemd = (access("/run/systemd/seats/", F_OK) >= 0);
#endif

        gs_listener_dbus_init (listener);

        init_session_id (listener);
        init_seat_path (listener);
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
        g_free (listener->priv->seat_path);

#ifdef WITH_SYSTEMD
        g_free (listener->priv->sd_session_id);
#endif

        G_OBJECT_CLASS (gs_listener_parent_class)->finalize (object);
}

GSListener *
gs_listener_new (void)
{
        GSListener *listener;

        listener = g_object_new (GS_TYPE_LISTENER, NULL);

        return GS_LISTENER (listener);
}
