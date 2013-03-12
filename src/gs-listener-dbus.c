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

#ifdef WITH_SYSTEMD
        gboolean        have_systemd;
#endif
};

enum {
        LOCK,
        ACTIVE_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ACTIVE,
};

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSListener, gs_listener, G_TYPE_OBJECT)

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

                        return TRUE;
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
