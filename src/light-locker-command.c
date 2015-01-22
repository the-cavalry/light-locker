/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib-unix.h>

#include "gs-bus.h"

static gboolean do_lock       = FALSE;
static gboolean do_activate   = FALSE;
static gboolean do_deactivate = FALSE;
static gboolean do_version    = FALSE;
static gboolean do_poke       = FALSE;
static gboolean do_inhibit    = FALSE;
static gboolean do_uninhibit  = FALSE;

static gboolean do_query      = FALSE;
static gboolean do_time       = FALSE;

static char    *inhibit_reason      = NULL;
static char    *inhibit_application = NULL;

static gint32   uninhibit_cookie = 0;

static GOptionEntry entries [] = {
        { "query", 'q', 0, G_OPTION_ARG_NONE, &do_query,
          N_("Query the state of the locker"), NULL },
        { "time", 't', 0, G_OPTION_ARG_NONE, &do_time,
          N_("Query the length of time the locker has been active"), NULL },
        { "lock", 'l', 0, G_OPTION_ARG_NONE, &do_lock,
          N_("Tells the running locker process to lock the screen immediately"), NULL },
        { "activate", 'a', 0, G_OPTION_ARG_NONE, &do_activate,
          N_("Turn the screensaver on (blank the screen)"), NULL },
        { "deactivate", 'd', 0, G_OPTION_ARG_NONE, &do_deactivate,
          N_("If the screensaver is active then deactivate it (un-blank the screen)"), NULL },
        { "poke", 'p', 0, G_OPTION_ARG_NONE, &do_poke,
          N_("Poke the running locker to simulate user activity"), NULL },
        { "inhibit", 'i', 0, G_OPTION_ARG_NONE, &do_inhibit,
          N_("Inhibit the screensaver from activating. Terminate the light-locker-command process to end inhibition."), NULL },
        { "application-name", 'n', 0, G_OPTION_ARG_STRING, &inhibit_application,
          N_("The calling application that is inhibiting the screensaver"), NULL },
        { "reason", 'r', 0, G_OPTION_ARG_STRING, &inhibit_reason,
          N_("The reason for inhibiting the screensaver"), NULL },
        { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version,
          N_("Version of this application"), NULL },
        { NULL }
};

static GMainLoop *loop = NULL;

static GDBusMessage *
screensaver_send_message_inhibit (GDBusConnection *connection,
                                  const char      *application,
                                  const char      *reason)
{
        GDBusMessage *message, *reply;
        GError       *error;

        g_return_val_if_fail (connection != NULL, NULL);
        g_return_val_if_fail (application != NULL, NULL);
        g_return_val_if_fail (reason != NULL, NULL);

        message = g_dbus_message_new_method_call (GS_SERVICE,
                                                  GS_PATH,
                                                  GS_INTERFACE,
                                                  "Inhibit");
        if (message == NULL) {
                g_warning ("Couldn't allocate the dbus message");
                return NULL;
        }

        g_dbus_message_set_body (message, g_variant_new ("(ss)", application, reason));

        error = NULL;
        reply = g_dbus_connection_send_message_with_reply_sync (connection,
                                                                message,
                                                                G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                                                -1,
                                                                NULL,
                                                                NULL,
                                                                &error);
        if (error != NULL) {
                g_warning ("unable to send message: %s", error->message);
                g_clear_error (&error);
        }

        g_dbus_connection_flush_sync (connection, NULL, &error);
        if (error != NULL) {
                g_warning ("unable to flush message queue: %s", error->message);
                g_clear_error (&error);
        }

        g_object_unref (message);

        return reply;
}

static void
screensaver_send_message_uninhibit (GDBusConnection *connection,
                                    gint32 cookie)
{
        GDBusMessage *message;
        GError       *error;

        g_return_val_if_fail (connection != NULL, NULL);

        message = g_dbus_message_new_method_call (GS_SERVICE,
                                                  GS_PATH,
                                                  GS_INTERFACE,
                                                  "UnInhibit");
        if (message == NULL) {
                g_warning ("Couldn't allocate the dbus message");
                return;
        }

        g_dbus_message_set_body (message, g_variant_new ("(u)", cookie));

        error = NULL;
        g_dbus_connection_send_message (connection,
                                        message,
                                        G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                        NULL,
                                        &error);
        if (error != NULL) {
                g_warning ("unable to send message: %s", error->message);
                g_clear_error (&error);
        }

        g_dbus_connection_flush_sync (connection, NULL, &error);
        if (error != NULL) {
                g_warning ("unable to flush message queue: %s", error->message);
                g_clear_error (&error);
        }

        g_object_unref (message);
}

static GDBusMessage *
screensaver_send_message_bool (GDBusConnection *connection,
                               const char      *name,
                               gboolean         value)
{
        GDBusMessage *message, *reply;
        GError       *error;

        g_return_val_if_fail (connection != NULL, NULL);
        g_return_val_if_fail (name != NULL, NULL);

        message = g_dbus_message_new_method_call (GS_SERVICE,
                                                  GS_PATH,
                                                  GS_INTERFACE,
                                                  name);
        if (message == NULL) {
                g_warning ("Couldn't allocate the dbus message");
                return NULL;
        }

        g_dbus_message_set_body (message, g_variant_new ("(b)", value));

        error = NULL;
        reply = g_dbus_connection_send_message_with_reply_sync (connection,
                                                                message,
                                                                G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                                                -1,
                                                                NULL,
                                                                NULL,
                                                                &error);
        if (error != NULL) {
                g_warning ("unable to send message: %s", error->message);
                g_clear_error (&error);
        }

        g_dbus_connection_flush_sync (connection, NULL, &error);
        if (error != NULL) {
                g_warning ("unable to flush message queue: %s", error->message);
                g_clear_error (&error);
        }

        g_object_unref (message);

        return reply;
}

static GDBusMessage *
screensaver_send_message_void (GDBusConnection *connection,
                               const char      *name,
                               gboolean         expect_reply)
{
        GDBusMessage *message, *reply;
        GError       *error;

        g_return_val_if_fail (connection != NULL, NULL);
        g_return_val_if_fail (name != NULL, NULL);

        message = g_dbus_message_new_method_call (GS_SERVICE,
                                                  GS_PATH,
                                                  GS_INTERFACE,
                                                  name);
        if (message == NULL) {
                g_warning ("Couldn't allocate the dbus message");
                return NULL;
        }

        error = NULL;

        if (! expect_reply) {
                reply = NULL;

                g_dbus_connection_send_message (connection,
                                                message,
                                                G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                                NULL,
                                                &error);

        } else {
                reply = g_dbus_connection_send_message_with_reply_sync (connection,
                                                                        message,
                                                                        G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                                                        -1,
                                                                        NULL,
                                                                        NULL,
                                                                        &error);
        }

        if (error != NULL) {
                g_warning ("unable to send message: %s", error->message);
                g_clear_error (&error);
        }

        g_dbus_connection_flush_sync (connection, NULL, &error);
        if (error != NULL) {
                g_warning ("unable to flush message queue: %s", error->message);
                g_clear_error (&error);
        }

        g_object_unref (message);

        return reply;
}

static gboolean
screensaver_is_running (GDBusConnection *connection)
{
        GVariant *reply;
        gboolean exists = FALSE;

        g_return_val_if_fail (connection != NULL, FALSE);

        reply = g_dbus_connection_call_sync (connection,
                                             DBUS_SERVICE,
                                             DBUS_PATH,
                                             DBUS_INTERFACE,
                                             "GetNameOwner",
                                             g_variant_new ("(s)", GS_SERVICE),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                             -1,
                                             NULL,
                                             NULL);
        if (reply != NULL) {
                exists = TRUE;
                g_variant_unref (reply);
        }

        return exists;
}

static gboolean
handle_term (gpointer user_data)
{
        g_main_loop_quit (loop);
        return FALSE;
}

static gboolean
do_command (GDBusConnection *connection)
{
        GDBusMessage *reply;

        if (do_query) {
                GVariant     *body;
                gboolean      v;

                if (! screensaver_is_running (connection)) {
                        g_message ("Locker is not running!");
                        goto done;
                }

                reply = screensaver_send_message_void (connection, "GetActive", TRUE);
                if (reply == NULL) {
                        g_message ("Did not receive a reply from the locker.");
                        goto done;
                }

                body = g_dbus_message_get_body (reply);
                g_variant_get (body, "(b)", &v);
                g_object_unref (reply);

                if (v)  {
                        g_print (_("The screensaver is active\n"));
                } else {
                        g_print (_("The screensaver is inactive\n"));
                }
        }

        if (do_time) {
                GVariant *body;
                gboolean  v;
                gint32    t;

                reply = screensaver_send_message_void (connection, "GetActive", TRUE);
                if (reply == NULL) {
                        g_message ("Did not receive a reply from the locker.");
                        goto done;
                }

                body = g_dbus_message_get_body (reply);
                g_variant_get (body, "(b)", &v);
                g_object_unref (reply);

                if (v) {
                        reply = screensaver_send_message_void (connection, "GetActiveTime", TRUE);
                        if (reply == NULL) {
                                g_message ("Did not receive a reply from the locker.");
                                goto done;
                        }

                        body = g_dbus_message_get_body (reply);
                        g_variant_get (body, "(u)", &t);
                        g_object_unref (reply);

                        g_print (ngettext ("The screensaver has been active for %d second.\n", "The screensaver has been active for %d seconds.\n", t), t);
                } else {
                        reply = screensaver_send_message_void (connection, "GetSessionIdleTime", TRUE);
                        if (reply == NULL) {
                                g_message ("Did not receive a reply from the locker.");
                                goto done;
                        }

                        body = g_dbus_message_get_body (reply);
                        g_variant_get (body, "(u)", &t);
                        g_object_unref (reply);

                        g_print (_("The screensaver is not currently active.\n"));
                        g_print (ngettext ("The session has been idle for %d second.\n", "The session has been idle for %d seconds.\n", t), t);
                }
        }

        if (do_lock) {
                reply = screensaver_send_message_void (connection, "Lock", FALSE);
                g_assert (reply == NULL);
        }

        if (do_activate) {
                GVariant     *body;
                gboolean      v;

                reply = screensaver_send_message_bool (connection, "SetActive", TRUE);
                if (reply == NULL) {
                        g_message ("Did not receive a reply from the locker.");
                        goto done;
                }
                body = g_dbus_message_get_body (reply);
                g_variant_get (body, "(b)", &v);
                g_object_unref (reply);

                if (!v)  {
                        g_message ("The screensaver failed to activate.");
                        goto done;
                }
        }

        if (do_deactivate) {
                GVariant     *body;
                gboolean      v;

                reply = screensaver_send_message_bool (connection, "SetActive", FALSE);
                if (reply == NULL) {
                        g_message ("Did not receive a reply from the locker.");
                        goto done;
                }
                body = g_dbus_message_get_body (reply);
                g_variant_get (body, "(b)", &v);
                g_object_unref (reply);

                if (!v)  {
                        g_message ("The screensaver failed to deactivate.");
                        goto done;
                }
        }

        if (do_poke) {
                reply = screensaver_send_message_void (connection, "SimulateUserActivity", FALSE);
                g_assert (reply == NULL);
        }

        if (do_inhibit) {
                GVariant *body;

                reply = screensaver_send_message_inhibit (connection,
                                                          inhibit_application ? inhibit_application : "Unknown",
                                                          inhibit_reason ? inhibit_reason : "Unknown");
                if (! reply) {
                        g_message ("Did not receive a reply from the locker.");
                        goto done;
                }

                body = g_dbus_message_get_body (reply);
                g_variant_get (body, "(u)", &uninhibit_cookie);
                g_object_unref (reply);

                do_uninhibit = TRUE;

                g_print (_("The screensaver has been inhibited with cookie %d\n"), uninhibit_cookie);

                g_unix_signal_add (SIGTERM, handle_term, NULL);
                g_unix_signal_add (SIGINT, handle_term, NULL);
                g_unix_signal_add (SIGHUP, handle_term, NULL);
                return FALSE;
        }

 done:
        g_main_loop_quit (loop);
        return FALSE;
}

int
main (int    argc,
      char **argv)
{
        GDBusConnection *connection;
        GOptionContext  *context;
        gboolean         retval;
        GError          *error;

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif
        textdomain (GETTEXT_PACKAGE);
#endif

#if !GLIB_CHECK_VERSION(2,35,1)
        g_type_init ();
#endif

        g_set_prgname (argv[0]);

        if (setlocale (LC_ALL, "") == NULL) {
                g_warning ("Locale not understood by C library, internationalization will not work\n");
        }

        error = NULL;
        context = g_option_context_new (NULL);
        g_option_context_add_main_entries (context, entries, NULL);
        retval = g_option_context_parse (context, &argc, &argv, &error);

        g_option_context_free (context);

        if (! retval) {
                g_warning ("%s", error->message);
                g_error_free (error);
                return EXIT_FAILURE;
        }

        if (do_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                return EXIT_SUCCESS;
        }

        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (connection == NULL) {
                g_message ("Failed to get session bus: %s", error->message);
                g_error_free (error);
                return EXIT_FAILURE;
        }

        g_idle_add ((GSourceFunc) do_command, connection);

        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);

        if (do_uninhibit) {
                screensaver_send_message_uninhibit (connection, uninhibit_cookie);

                g_print (_("Send uninhibit to the screensaver with cookie %d\n"), uninhibit_cookie);
        }

        g_object_unref (connection);

        return EXIT_SUCCESS;
}

