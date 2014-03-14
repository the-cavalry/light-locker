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

static gboolean
screensaver_is_running (void)
{
#if __linux__
    /* Return TRUE if there is a running instance of light-locker */
    gboolean exists = FALSE;

    GDir* proc = NULL;
    GError* error = NULL;
    gchar* subdir = "a";
    gchar* dir_path = NULL;
    gchar* file_path = NULL;
    gchar* contents = NULL;
    gchar** paths = NULL;
    gchar* path = NULL;
    gsize length;
    guint i = 0;

    /* Check the /proc directory for pids */
    proc = g_dir_open("/proc", 0, &error);
    if (error)
    {
        g_critical("%s", error->message);
        g_error_free(error);
        return FALSE;
    }

    /* Iterate through each file, the PIDs are directories */
    while (subdir != NULL)
    {
        subdir = g_strdup(g_dir_read_name(proc));
        dir_path = g_build_filename("/proc", subdir, (gchar*)NULL);

        /* If its a directory, check it out. */
        if (g_file_test(dir_path, G_FILE_TEST_IS_DIR))
        {
            /* Check for the cmdline file, which has the command. */
            file_path = g_build_filename(dir_path, "cmdline", (gchar*)NULL);
            if (g_file_test(file_path, G_FILE_TEST_EXISTS)) {
                if (g_file_get_contents(g_strdup(file_path), &contents, &length, &error))
                {
                    /* Check if light-locker is running */
                    if (g_str_has_suffix(contents, "light-locker"))
                    {
                        /* Check for just "light-locker" */
                        if (g_strcmp0(contents, "light-locker") == 0)
                        {
                            exists = TRUE;
                        }
                        if (!exists)
                        {
                            /* Check if executable in path */
                            paths = g_strsplit(g_getenv("PATH"), ":", 0);
                            for (i = 0; i < g_strv_length(paths); i++) {
                                path = g_strdup(g_build_filename(paths[i], "light-locker", NULL));
                                if (g_strcmp0(contents, path) == 0)
                                {
                                    exists = TRUE;
                                    g_free(path);
                                    break;
                                }
                                g_free(path);
                            }
                            g_strfreev(paths);
                        }
                    }
                }
                g_free(contents);
                if (error)
                {
                    g_error_free(error);
                    error = NULL;
                }
            }
            g_free(file_path);
        }
        g_free(dir_path);

        /* If found, stop the loop */
        if (exists)
        {
            g_free(subdir);
            subdir = NULL;
        }
    }
    g_dir_close(proc);

    return exists;
#else
    return TRUE;
#endif
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

        if (!screensaver_is_running()) {
            g_message ("light-locker is not running");
            return EXIT_FAILURE;
        }

        g_idle_add ((GSourceFunc) do_command, connection);

        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);

        g_object_unref (connection);

        return EXIT_SUCCESS;
}

