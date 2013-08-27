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

#include "gs-bus.h"

static gboolean do_lock       = FALSE;
static gboolean do_version    = FALSE;

static GOptionEntry entries [] = {
        { "lock", 'l', 0, G_OPTION_ARG_NONE, &do_lock,
          N_("Tells the running display manager to lock the screen immediately"), NULL },
        { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version,
          N_("Version of this application"), NULL },
        { NULL }
};

static GMainLoop *loop = NULL;

static GDBusMessage *
displaymanager_send_message_void (GDBusConnection *connection,
                               const char      *name,
                               gboolean         expect_reply)
{
        GDBusMessage *message, *reply;
        GError       *error;

        g_return_val_if_fail (connection != NULL, NULL);
        g_return_val_if_fail (name != NULL, NULL);

        message = g_dbus_message_new_method_call (DM_SERVICE,
                                                  DM_SESSION_PATH,
                                                  DM_SESSION_INTERFACE,
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
do_command (GDBusConnection *connection)
{
        GDBusMessage *reply;

        if (do_lock) {
                reply = displaymanager_send_message_void (connection, "Lock", FALSE);
                g_assert (reply == NULL);
        }

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

        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (connection == NULL) {
                g_message ("Failed to get session bus: %s", error->message);
                g_error_free (error);
                return EXIT_FAILURE;
        }

        g_idle_add ((GSourceFunc) do_command, connection);

        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);

        g_object_unref (connection);

        return EXIT_SUCCESS;
}

