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

#ifdef HAVE_MIT_SAVER_EXTENSION
#include <gtk/gtk.h>
#if GTK_CHECK_VERSION(3, 0, 0)
#include <gtk/gtkx.h>
#else
#include <gdk/gdkx.h>
#endif
#include <X11/extensions/scrnsaver.h>
#endif

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#endif

#include "gs-listener-dbus.h"
#include "gs-marshal.h"
#include "gs-debug.h"
#include "gs-bus.h"

/* this is for dbus < 0.3 */
#if ((DBUS_VERSION_MAJOR == 0) && (DBUS_VERSION_MINOR < 30))
#define dbus_bus_name_has_owner(connection, name, err)      dbus_bus_service_exists(connection, name, err)
#define dbus_bus_request_name(connection, name, flags, err) dbus_bus_acquire_service(connection, name, flags, err)
#endif

static void              gs_listener_class_init         (GSListenerClass *klass);
static void              gs_listener_init               (GSListener      *listener);
static void              gs_listener_finalize           (GObject         *object);

static DBusHandlerResult gs_listener_message_handler    (DBusConnection  *connection,
                                                         DBusMessage     *message,
                                                         void            *user_data);

#define TYPE_MISMATCH_ERROR  GS_INTERFACE ".TypeMismatch"

#define GS_LISTENER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LISTENER, GSListenerPrivate))

struct GSListenerPrivate
{
        DBusConnection *connection;
        DBusConnection *system_connection;

        guint           active : 1;
        guint           lid_closed : 1;
        guint           blanked : 1;
        time_t          blanked_start;
        char           *session_id;
        char           *seat_path;

#ifdef WITH_SYSTEMD
        gboolean        have_systemd;
        char           *sd_session_id;
        int             delay_fd;
#endif

        dbus_uint32_t   inhibit_last_cookie;
        GHashTable     *inhibit_list;
};

enum {
        LOCK,
        LOCKED,
        SESSION_SWITCHED,
        ACTIVE_CHANGED,
        SUSPEND,
        RESUME,
        SIMULATE_USER_ACTIVITY,
        BLANKING,
        INHIBIT,
        IDLE_TIME,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ACTIVE,
        PROP_LID_CLOSED,
};

static DBusObjectPathVTable
gs_listener_vtable = { NULL,
                       &gs_listener_message_handler,
                       NULL,
                       NULL,
                       NULL,
                       NULL };

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSListener, gs_listener, G_TYPE_OBJECT)

gboolean
gs_listener_is_lid_closed (GSListener *listener)
{
        return listener->priv->lid_closed;
}

void
gs_listener_send_switch_greeter (GSListener *listener)
{
        dbus_bool_t sent;
        DBusMessage *message;

        gs_debug ("Send switch greeter");

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

        gs_debug ("Send lock session");

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

GQuark
gs_listener_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark) {
                quark = g_quark_from_static_string ("gs_listener_error");
        }

        return quark;
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
                                           GS_INTERFACE,
                                           name);

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &value);

        if (! send_dbus_message (listener->priv->connection, message)) {
                gs_debug ("Could not send %s signal", name);
        }

        dbus_message_unref (message);

        /* Emit the signal on the KDE path */
        message = dbus_message_new_signal (GS_PATH_KDE,
                                           GS_INTERFACE,
                                           name);

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &value);

        if (! send_dbus_message (listener->priv->connection, message)) {
                gs_debug ("Could not send %s signal", name);
        }

        dbus_message_unref (message);

        /* Emit the signal on the GNOME interface */
        message = dbus_message_new_signal (GS_PATH_GNOME,
                                           GS_INTERFACE_GNOME,
                                           name);

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &value);

        if (! send_dbus_message (listener->priv->connection, message)) {
                gs_debug ("Could not send %s signal", name);
        }

        dbus_message_unref (message);
}

static void
gs_listener_send_signal_active_changed (GSListener *listener,
                                        gboolean    active)
{
        g_return_if_fail (listener != NULL);

        gs_debug ("Sending the ActiveChanged(%s) signal on the session bus",
                  active ? "TRUE" : "FALSE");

        send_dbus_boolean_signal (listener, "ActiveChanged", active);
}

void
gs_listener_set_blanked (GSListener *listener,
                         gboolean    active)
{
        listener->priv->blanked = active;

        if (active) {
                listener->priv->blanked_start = time (NULL);
        } else {
                listener->priv->blanked_start = 0;
        }

        gs_listener_send_signal_active_changed (listener, active);
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

void
gs_listener_set_idle_hint (GSListener *listener, gboolean idle)
{
        dbus_bool_t sent;
        DBusMessage *message;

        gs_debug ("Send idle hint: %d", idle);

#ifdef WITH_SYSTEMD
        if (listener->priv->have_systemd) {

                if (listener->priv->system_connection == NULL) {
                        gs_debug ("No connection to the system bus");
                        return;
                }

                message = dbus_message_new_method_call (SYSTEMD_LOGIND_SERVICE,
                                                        listener->priv->session_id,
                                                        SYSTEMD_LOGIND_SESSION_INTERFACE,
                                                        "SetIdleHint");
                if (message == NULL) {
                        gs_debug ("Couldn't allocate the dbus message");
                        return;
                }

                if (dbus_message_append_args (message,
                                              DBUS_TYPE_BOOLEAN, &idle,
                                              DBUS_TYPE_INVALID) == FALSE) {
                        gs_debug ("Couldn't add args to the dbus message");
                        dbus_message_unref (message);
                        return;
                }

                sent = dbus_connection_send (listener->priv->system_connection, message, NULL);
                dbus_message_unref (message);

                if (sent == FALSE) {
                        gs_debug ("Couldn't send the dbus message");
                        return;
                }

                return;
        }
#endif

#ifdef WITH_CONSOLE_KIT
        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return;
        }

        message = dbus_message_new_method_call (CK_SERVICE,
                                                listener->priv->session_id,
                                                CK_SESSION_INTERFACE,
                                                "SetIdleHint");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return;
        }

        if (dbus_message_append_args (message,
                                      DBUS_TYPE_BOOLEAN, &idle,
                                      DBUS_TYPE_INVALID) == FALSE) {
                gs_debug ("Couldn't add args to the dbus message");
                dbus_message_unref (message);
                return;
        }

        sent = dbus_connection_send (listener->priv->system_connection, message, NULL);
        dbus_message_unref (message);

        if (sent == FALSE) {
                gs_debug ("Couldn't send the dbus message");
                return;
        }
#endif
}

void
gs_listener_delay_suspend (GSListener *listener)
{
#ifdef WITH_SYSTEMD
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       error;
        const char     *what;
        const char     *who;
        const char     *why;
        const char     *mode;
        int             fd;

        gs_debug ("Delay suspend");

        if (listener->priv->system_connection == NULL) {
                gs_debug ("No connection to the system bus");
                return;
        }

        dbus_error_init (&error);

        message = dbus_message_new_method_call (SYSTEMD_LOGIND_SERVICE, SYSTEMD_LOGIND_PATH, SYSTEMD_LOGIND_INTERFACE, "Inhibit");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return;
        }

        what = "sleep";
        who  = _("Screen Locker");
        why  = _("Lock the screen on suspend/resume");
        mode = "delay";

        if (dbus_message_append_args (message,
                                      DBUS_TYPE_STRING, &what,
                                      DBUS_TYPE_STRING, &who,
                                      DBUS_TYPE_STRING, &why,
                                      DBUS_TYPE_STRING, &mode,
                                      DBUS_TYPE_INVALID) == FALSE) {
                gs_debug ("Couldn't add args to the dbus message");
                dbus_message_unref (message);
                return;
        }

        reply = dbus_connection_send_with_reply_and_block (listener->priv->system_connection,
                                                           message,
                                                           -1, &error);
        dbus_message_unref (message);

        if (dbus_error_is_set (&error)) {
                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                dbus_error_free (&error);
                return;
        }

        if (dbus_message_get_args (reply, &error,
                                   DBUS_TYPE_UNIX_FD, &fd,
                                   DBUS_TYPE_INVALID) == FALSE) {
                fd = -1;
        }

        dbus_message_unref (reply);

        if (dbus_error_is_set (&error)) {
                gs_debug ("%s raised:\n %s\n\n", error.name, error.message);
                dbus_error_free (&error);
                return;
        }

        listener->priv->delay_fd = fd;
#endif
}

void
gs_listener_resume_suspend (GSListener *listener)
{
#ifdef WITH_SYSTEMD
        gs_debug ("Resume suspend: fd=%d", listener->priv->delay_fd);

        if (listener->priv->delay_fd >= 0) {
                close (listener->priv->delay_fd);
                listener->priv->delay_fd = -1;
        }
#endif
}

static dbus_uint32_t
gs_listener_add_inhibit (GSListener *listener,
                         const char *owner)
{
        dbus_uint32_t *cookie;

        cookie = g_new (dbus_uint32_t, 1);
        if (cookie == NULL) {
                g_error ("No memory");
        }

        *cookie = ++listener->priv->inhibit_last_cookie;

        if (g_hash_table_size (listener->priv->inhibit_list) == 0) {
                g_signal_emit (listener, signals [INHIBIT], 0, TRUE);
        }

        g_hash_table_insert (listener->priv->inhibit_list, cookie, g_strdup (owner));

        return *cookie;
}

static void
gs_listener_remove_inhibit (GSListener    *listener,
                            dbus_uint32_t  cookie,
                            const char    *owner)
{
        const gchar *owned;

        owned = g_hash_table_lookup (listener->priv->inhibit_list, &cookie);

        if (owned == NULL) {
                return;
        }

        if (strcmp (owner, owned) != 0) {
                return;
        }

        g_hash_table_remove (listener->priv->inhibit_list, &cookie);

        if (g_hash_table_size (listener->priv->inhibit_list) == 0) {
                g_signal_emit (listener, signals [INHIBIT], 0, FALSE);
        }
}

static gboolean
compare_owner (gpointer key,
               gpointer value,
               gpointer user_data)
{
        const gchar *oa = value;
        const char *ob = user_data;

        return strcmp (oa, ob) == 0;;
}

static void
gs_listener_disonnect_inhibit (GSListener  *listener,
                               const char  *owner)
{
        guint count;

        count = g_hash_table_foreach_remove (listener->priv->inhibit_list, compare_owner, (gpointer)owner);

        gs_debug ("Inhibitor disconnected: %s (%u)", owner, count);

        if (count > 0 && g_hash_table_size (listener->priv->inhibit_list) == 0) {
                g_signal_emit (listener, signals [INHIBIT], 0, FALSE);
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
listener_set_active (GSListener     *listener,
                     DBusConnection *connection,
                     DBusMessage    *message,
                     gboolean        send_reply)
{
        const char     *path;
        int             type;
        DBusMessageIter iter;
        DBusMessage    *reply;
        gboolean        new_state, res;
        dbus_bool_t     v;

        path = dbus_message_get_path (message);

        dbus_message_iter_init (message, &iter);
        type = dbus_message_iter_get_arg_type (&iter);

        if (type != DBUS_TYPE_BOOLEAN) {
                gs_debug ("Unsupported property type %d", type);
                raise_property_type_error (connection, message, path);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        dbus_message_iter_get_basic (&iter, &v);
        new_state = v;
        g_signal_emit (listener, signals [BLANKING], 0, new_state, &res);
        v = res;

        if (send_reply == FALSE) {
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        reply = dbus_message_new_method_return (message);

        if (reply == NULL) {
                g_error ("No memory");
        }

        dbus_message_iter_init_append (reply, &iter);

        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &v);

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_get_bool (GSListener     *listener,
                   DBusConnection *connection,
                   DBusMessage    *message,
                   dbus_bool_t     v)
{
        DBusMessageIter iter;
        DBusMessage    *reply;

        reply = dbus_message_new_method_return (message);

        if (reply == NULL) {
                g_error ("No memory");
        }

        dbus_message_iter_init_append (reply, &iter);

        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &v);

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_get_time (GSListener     *listener,
                   DBusConnection *connection,
                   DBusMessage    *message,
                   time_t          t)
{
        DBusMessageIter iter;
        DBusMessage    *reply;
        dbus_uint32_t   secs = 0;
        time_t          now;

        reply = dbus_message_new_method_return (message);

        if (reply == NULL) {
                g_error ("No memory");
        }

        dbus_message_iter_init_append (reply, &iter);

        now = time (NULL);
        if (G_UNLIKELY (now < t)) {
                gs_debug ("Time is in the future");
        } else if (G_UNLIKELY (t<= 0)) {
                //gs_debug ("Time was not set");
        } else {
                secs = now - t;
        }
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &secs);

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_get_info (GSListener     *listener,
                   DBusConnection *connection,
                   DBusMessage    *message,
                   int             signal)
{
        DBusMessageIter iter;
        DBusMessage    *reply;

        reply = dbus_message_new_method_return (message);

        if (reply == NULL) {
                g_error ("No memory");
        }

        dbus_message_iter_init_append (reply, &iter);

        switch (signal) {
        case IDLE_TIME:
                {
                        dbus_uint32_t v;
                        gulong        res;
                        g_signal_emit (listener, signals [signal], 0, &res);
                        v = res;
                        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &v);
                        break;
                }

        default:
                gs_debug ("Unsupported signal id %d", signal);
                break;
        }

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_inhibit (GSListener     *listener,
                  DBusConnection *connection,
                  DBusMessage    *message)
{
        const char     *path;
        int             type;
        DBusMessageIter iter;
        DBusMessage    *reply;
        dbus_uint32_t   cookie;
        const char     *application;
        const char     *reason;

        path = dbus_message_get_path (message);

        dbus_message_iter_init (message, &iter);
        type = dbus_message_iter_get_arg_type (&iter);

        if (type != DBUS_TYPE_STRING) {
                gs_debug ("Unsupported property type %d", type);
                raise_property_type_error (connection, message, path);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        dbus_message_iter_get_basic (&iter, &application);

        dbus_message_iter_next (&iter);
        type = dbus_message_iter_get_arg_type (&iter);

        if (type != DBUS_TYPE_STRING) {
                gs_debug ("Unsupported property type %d", type);
                raise_property_type_error (connection, message, path);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        dbus_message_iter_get_basic (&iter, &reason);

        gs_debug ("Inhibit requested: %s '%s'", application, reason);

        cookie = gs_listener_add_inhibit (listener, dbus_message_get_sender (message));

        reply = dbus_message_new_method_return (message);

        dbus_message_iter_init_append (reply, &iter);

        if (reply == NULL) {
                g_error ("No memory");
        }

        gs_debug ("Returning inhibit cookie %u", cookie);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &cookie);

        if (! dbus_connection_send (connection, reply, NULL)) {
                g_error ("No memory");
        }

        dbus_message_unref (reply);

        return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
listener_uninhibit (GSListener     *listener,
                    DBusConnection *connection,
                    DBusMessage    *message)
{
        const char     *path;
        int             type;
        DBusMessageIter iter;
        dbus_uint32_t   cookie = 0;

        path = dbus_message_get_path (message);

        dbus_message_iter_init (message, &iter);
        type = dbus_message_iter_get_arg_type (&iter);

        if (type != DBUS_TYPE_UINT32) {
                gs_debug ("Unsupported property type %d", type);
                raise_property_type_error (connection, message, path);
                return DBUS_HANDLER_RESULT_HANDLED;
        }

        dbus_message_iter_get_basic (&iter, &cookie);

        gs_debug ("Uninhibit requested: %u", cookie);

        gs_listener_remove_inhibit (listener, cookie, dbus_message_get_sender (message));

        return send_success_reply (connection, message);
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
                               "      <arg direction=\"out\" type=\"b\"/>\n"
                               "    </method>\n"
                               "    <method name=\"GetActiveTime\">\n"
                               "      <arg name=\"seconds\" direction=\"out\" type=\"u\"/>\n"
                               "    </method>\n"
                               "    <method name=\"GetSessionIdleTime\">\n"
                               "      <arg name=\"seconds\" direction=\"out\" type=\"u\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SetActive\">\n"
                               "      <arg direction=\"out\" type=\"b\"/>\n"
                               "      <arg name=\"e\" direction=\"in\" type=\"b\"/>\n"
                               "    </method>\n"
                               "    <method name=\"Inhibit\">\n"
                               "      <arg direction=\"out\" type=\"u\"/>\n"
                               "      <arg name=\"application_name\" direction=\"in\" type=\"s\"/>\n"
                               "      <arg name=\"reason_for_inhibit\" direction=\"in\" type=\"s\"/>\n"
                               "    </method>\n"
                               "    <method name=\"UnInhibit\">\n"
                               "      <arg name=\"cookie\" direction=\"in\" type=\"u\"/>\n"
                               "    </method>\n"
                               "    <signal name=\"ActiveChanged\">\n"
                               "      <arg type=\"b\"/>\n"
                               "    </signal>\n"
                               "  </interface>\n");

        /* ScreenSaver interface GNOME */
        xml = g_string_append (xml,
                               "  <interface name=\""GS_INTERFACE_GNOME"\">\n"
                               "    <method name=\"Lock\">\n"
                               "    </method>\n"
                               "    <method name=\"SimulateUserActivity\">\n"
                               "    </method>\n"
                               "    <method name=\"GetActive\">\n"
                               "      <arg direction=\"out\" type=\"b\"/>\n"
                               "    </method>\n"
                               "    <method name=\"GetActiveTime\">\n"
                               "      <arg name=\"seconds\" direction=\"out\" type=\"u\"/>\n"
                               "    </method>\n"
                               "    <method name=\"SetActive\">\n"
                               "      <arg name=\"e\" direction=\"in\" type=\"b\"/>\n"
                               "    </method>\n"
                               "    <signal name=\"ActiveChanged\">\n"
                               "      <arg type=\"b\"/>\n"
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
listener_dbus_handle_session_message (GSListener     *listener,
                                      DBusConnection *connection,
                                      DBusMessage    *message,
                                      dbus_bool_t     local_interface)
{
#if 0
        g_message ("obj_path=%s interface=%s method=%s destination=%s",
                   dbus_message_get_path (message),
                   dbus_message_get_interface (message),
                   dbus_message_get_member (message),
                   dbus_message_get_destination (message));
#endif

        g_return_val_if_fail (connection != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
        g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

        if (dbus_message_is_method_call (message, GS_INTERFACE, "Lock")) {
                gs_debug ("Received Lock request");
                g_signal_emit (listener, signals [LOCK], 0);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "SetActive")) {
                gs_debug ("Received SetActive request");
                return listener_set_active (listener, connection, message, TRUE);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "GetActive")) {
                gs_debug ("Received GetActive request: %d", listener->priv->blanked);
                return listener_get_bool (listener, connection, message, listener->priv->blanked);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "GetActiveTime")) {
                gs_debug ("Received GetActiveTime request: %d", listener->priv->blanked_start);
                return listener_get_time (listener, connection, message, listener->priv->blanked_start);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "GetSessionIdleTime")) {
                gs_debug ("Received GetSessionIdleTime request");
                return listener_get_info (listener, connection, message, IDLE_TIME);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "SimulateUserActivity")) {
                gs_debug ("Received SimulateUserActivity request");
                g_signal_emit (listener, signals [SIMULATE_USER_ACTIVITY], 0);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "Inhibit")) {
                gs_debug ("Received Inhibit request");
                return listener_inhibit (listener, connection, message);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE, "UnInhibit")) {
                gs_debug ("Received UnInhibit request");
                return listener_uninhibit (listener, connection, message);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE_GNOME, "Lock")) {
                gs_debug ("Received Lock request");
                g_signal_emit (listener, signals [LOCK], 0);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE_GNOME, "SetActive")) {
                gs_debug ("Received SetActive request");
                listener_set_active (listener, connection, message, FALSE);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE_GNOME, "GetActive")) {
                gs_debug ("Received GetActive request: %d", listener->priv->blanked);
                return listener_get_bool (listener, connection, message, listener->priv->blanked);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE_GNOME, "GetActiveTime")) {
                gs_debug ("Received GetActiveTime request: %d", listener->priv->blanked_start);
                return listener_get_time (listener, connection, message, listener->priv->blanked_start);
        }
        if (dbus_message_is_method_call (message, GS_INTERFACE_GNOME, "SimulateUserActivity")) {
                gs_debug ("Received SimulateUserActivity request");
                g_signal_emit (listener, signals [SIMULATE_USER_ACTIVITY], 0);
                return send_success_reply (connection, message);
        }
        if (dbus_message_is_method_call (message, DBUS_INTROSPECTABLE_INTERFACE, "Introspect")) {
                return do_introspect (connection, message, local_interface);
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
listener_dbus_handle_owner_changed (GSListener     *listener,
                                    DBusConnection *connection,
                                    DBusMessage    *message)
{
        DBusMessageIter  iter;
        int              type;
        const char      *old_owner;

        dbus_message_iter_init (message, &iter);
        dbus_message_iter_next (&iter);
        type = dbus_message_iter_get_arg_type (&iter);

        if (type != DBUS_TYPE_STRING) {
                gs_debug ("Invalid NameOwnerChanged message");
                return;
        }

        dbus_message_iter_get_basic (&iter, &old_owner);

        gs_listener_disonnect_inhibit (listener, old_owner);
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

#ifdef WITH_UPOWER
#ifdef WITH_LOCK_ON_LID
static gboolean
query_lid_closed (GSListener *listener)
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

        message = dbus_message_new_method_call (UP_SERVICE, UP_PATH, DBUS_PROPERTIES_INTERFACE, "Get");
        if (message == NULL) {
                gs_debug ("Couldn't allocate the dbus message");
                return FALSE;
        }

        interface = UP_INTERFACE;
        property = "LidIsClosed";

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
#endif

#if defined(WITH_SYSTEMD) || (defined(WITH_UPOWER) && defined(WITH_LOCK_ON_LID))
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
                                g_signal_emit (listener, signals [LOCKED], 0);
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
                                gs_debug ("systemd notified ActiveSession %d", new_active);
                                g_signal_emit (listener, signals [SESSION_SWITCHED], 0, new_active);
                        }

#ifdef WITH_UPOWER
#ifdef WITH_LOCK_ON_LID
                        if (properties_changed_match (message, "LidIsClosed")) {
                                listener->priv->lid_closed = query_lid_closed (listener);
                                gs_debug ("UPower notified LidIsClosed %d", (int)listener->priv->lid_closed);
                                g_object_notify (G_OBJECT (listener), "lid-closed");
                        }
#endif
#endif

                        return DBUS_HANDLER_RESULT_HANDLED;
                }

#ifdef WITH_LOCK_ON_SUSPEND
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
#endif

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
                        g_signal_emit (listener, signals [LOCKED], 0);
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
#ifdef WITH_LOCK_ON_SUSPEND
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
#ifdef WITH_LOCK_ON_LID
        if (dbus_message_is_signal (message, DBUS_INTERFACE_PROPERTIES, "PropertiesChanged")) {

                if (properties_changed_match (message, "LidIsClosed")) {
                        listener->priv->lid_closed = query_lid_closed (listener);
                        gs_debug ("UPower notified LidIsClosed %d", (int)listener->priv->lid_closed);
                        g_object_notify (G_OBJECT (listener), "lid-closed");
                }

                return DBUS_HANDLER_RESULT_HANDLED;
        }
#endif
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

        if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
                   strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                dbus_connection_unref (connection);

                return DBUS_HANDLER_RESULT_HANDLED;
        } else {
                return listener_dbus_handle_session_message (GS_LISTENER (user_data), connection, message, TRUE);
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
                if (g_hash_table_size (listener->priv->inhibit_list) > 0)
                        listener_dbus_handle_owner_changed (listener, connection, message);
        } else {
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
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
        case PROP_LID_CLOSED:
                g_value_set_boolean (value, self->priv->lid_closed);
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
        signals [LOCKED] =
                g_signal_new ("locked",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, locked),
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
        signals [BLANKING] =
                g_signal_new ("blanking",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, blanking),
                              NULL,
                              NULL,
                              gs_marshal_BOOLEAN__BOOLEAN,
                              G_TYPE_BOOLEAN,
                              1,
                              G_TYPE_BOOLEAN);
        signals [INHIBIT] =
                g_signal_new ("inhibit",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, inhibit),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_BOOLEAN);
        signals [IDLE_TIME] =
                g_signal_new ("idle-time",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSListenerClass, idle_time),
                              NULL,
                              NULL,
                              gs_marshal_ULONG__VOID,
                              G_TYPE_ULONG,
                              0);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LID_CLOSED,
                                         g_param_spec_boolean ("lid-closed",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));

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

        if (exists == FALSE) {
                /* Also check for the GNOME screensaver */
                dbus_error_init (&error);
                exists = dbus_bus_name_has_owner (connection, GS_SERVICE_GNOME, &error);
                if (dbus_error_is_set (&error)) {
                        dbus_error_free (&error);
                }
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

        /* Register KDE path */
        if (dbus_connection_register_object_path (listener->priv->connection,
                                                  GS_PATH_KDE,
                                                  &gs_listener_vtable,
                                                  listener) == FALSE) {
                g_critical ("out of memory registering object path");
                return FALSE;
        }

        /* Register GNOME interface */
        if (dbus_connection_register_object_path (listener->priv->connection,
                                                  GS_PATH_GNOME,
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

        res = dbus_bus_request_name (listener->priv->connection,
                                     GS_SERVICE_GNOME,
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
                             _("GNOME screensaver already running in this session"));
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

#ifdef WITH_LOCK_ON_SUSPEND
                        dbus_bus_add_match (listener->priv->system_connection,
                                            "type='signal'"
                                            ",sender='"SYSTEMD_LOGIND_SERVICE"'"
                                            ",interface='"SYSTEMD_LOGIND_INTERFACE"'"
                                            ",member='PrepareForSleep'",
                                            NULL);
#endif

#ifdef WITH_UPOWER
#ifdef WITH_LOCK_ON_LID
                        dbus_bus_add_match (listener->priv->system_connection,
                                            "type='signal'"
                                            ",sender='"UP_SERVICE"'"
                                            ",interface='"DBUS_INTERFACE_PROPERTIES"'"
                                            ",member='PropertiesChanged'",
                                            NULL);
#endif
#endif

                        return (res != -1);
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
#ifdef WITH_LOCK_ON_SUSPEND
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
#ifdef WITH_LOCK_ON_LID
                dbus_bus_add_match (listener->priv->system_connection,
                                    "type='signal'"
                                    ",sender='"UP_SERVICE"'"
                                    ",interface='"DBUS_INTERFACE_PROPERTIES"'"
                                    ",member='PropertiesChanged'",
                                    NULL);
#endif
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

        if (DM_SESSION_PATH == NULL) {
                g_error ("Environment variable XDG_SESSION_PATH not set. Is LightDM running?");
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
        listener->priv->delay_fd = -1;
#endif

        gs_listener_dbus_init (listener);

        init_session_id (listener);
        init_seat_path (listener);

        listener->priv->inhibit_list = g_hash_table_new_full (g_int_hash, g_int_equal, g_free, g_free);
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
