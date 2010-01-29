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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-lock-plug.h"

#include "gs-auth.h"
#include "setuid.h"

#include "gs-debug.h"

#define MAX_FAILURES 5

static gboolean verbose        = FALSE;
static gboolean show_version   = FALSE;
static gboolean enable_logout  = FALSE;
static gboolean enable_switch  = FALSE;
static char    *logout_command = NULL;
static char    *status_message   = NULL;
static char    *away_message     = NULL;

static GOptionEntry entries [] = {
        { "verbose", 0, 0, G_OPTION_ARG_NONE, &verbose,
          N_("Show debugging output"), NULL },
        { "version", 0, 0, G_OPTION_ARG_NONE, &show_version,
          N_("Version of this application"), NULL },
        { "enable-logout", 0, 0, G_OPTION_ARG_NONE, &enable_logout,
          N_("Show the logout button"), NULL },
        { "logout-command", 0, 0, G_OPTION_ARG_STRING, &logout_command,
          N_("Command to invoke from the logout button"), NULL },
        { "enable-switch", 0, 0, G_OPTION_ARG_NONE, &enable_switch,
          N_("Show the switch user button"), NULL },
        { "status-message", 0, 0, G_OPTION_ARG_STRING, &status_message,
          N_("Message to show in the dialog"), N_("MESSAGE") },
        { "away-message", 0, 0, G_OPTION_ARG_STRING, &away_message,
          N_("Not used"), N_("MESSAGE") },
        { NULL }
};

static char *
get_id_string (GtkWidget *widget)
{
        char *id = NULL;

        g_return_val_if_fail (widget != NULL, NULL);
        g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

        id = g_strdup_printf ("%" G_GUINT32_FORMAT,
                              (guint32) GDK_WINDOW_XID (widget->window));
        return id;
}

static gboolean
print_id (GtkWidget *widget)
{
        char *id;

        gs_profile_start (NULL);

        id = get_id_string (widget);
        printf ("WINDOW ID=%s\n", id);
        fflush (stdout);

        gs_profile_end (NULL);

        g_free (id);

        return FALSE;
}

static void
response_cancel (void)
{
        printf ("RESPONSE=CANCEL\n");
        fflush (stdout);
}

static void
response_ok (void)
{
        printf ("RESPONSE=OK\n");
        fflush (stdout);
}

static gboolean
quit_response_ok (void)
{
        response_ok ();
        gtk_main_quit ();
        return FALSE;
}

static gboolean
quit_response_cancel (void)
{
        response_cancel ();
        gtk_main_quit ();
        return FALSE;
}

static void
response_lock_init_failed (void)
{
        /* if we fail to lock then we should drop the dialog */
        response_ok ();
}

static char *
request_response (GSLockPlug *plug,
                  const char *prompt,
                  gboolean    visible)
{
        int   response;
        char *text;

        gs_lock_plug_set_sensitive (plug, TRUE);
        gs_lock_plug_enable_prompt (plug, prompt, visible);
        response = gs_lock_plug_run (plug);

        gs_debug ("got response: %d", response);

        text = NULL;
        if (response == GS_LOCK_PLUG_RESPONSE_OK) {
                gs_lock_plug_get_text (plug, &text);
        }
        gs_lock_plug_disable_prompt (plug);

        return text;
}

/* Adapted from GDM2 daemon/verify-pam.c on 2006-06-13 */
static const char *
maybe_translate_message (const char *msg)
{
        char              *s;
        const char        *ret;
        static GHashTable *hash = NULL;

        if (hash == NULL) {
                /* Here we come with some fairly standard messages so that
                   we have as much as possible translated.  Should really be
                   translated in pam I suppose.  This way we can "change"
                   some of these messages to be more sane. */
                hash = g_hash_table_new (g_str_hash, g_str_equal);
                /* login: is whacked always translate to Username: */
                g_hash_table_insert (hash, "login:", _("Username:"));
                g_hash_table_insert (hash, "Username:", _("Username:"));
                g_hash_table_insert (hash, "username:", _("Username:"));
                g_hash_table_insert (hash, "Password:", _("Password:"));
                g_hash_table_insert (hash, "password:", _("Password:"));
                g_hash_table_insert (hash, "You are required to change your password immediately (password aged)", _("You are required to change your password immediately (password aged)"));
                g_hash_table_insert (hash, "You are required to change your password immediately (root enforced)", _("You are required to change your password immediately (root enforced)"));
                g_hash_table_insert (hash, "Your account has expired; please contact your system administrator", _("Your account has expired; please contact your system administrator"));
                g_hash_table_insert (hash, "No password supplied", _("No password supplied"));
                g_hash_table_insert (hash, "Password unchanged", _("Password unchanged"));
                g_hash_table_insert (hash, "Can not get username", _("Can not get username"));
                g_hash_table_insert (hash, "Retype new UNIX password:", _("Retype new UNIX password:"));
                g_hash_table_insert (hash, "Enter new UNIX password:", _("Enter new UNIX password:"));
                g_hash_table_insert (hash, "(current) UNIX password:", _("(current) UNIX password:"));
                g_hash_table_insert (hash, "Error while changing NIS password.", _("Error while changing NIS password."));
                g_hash_table_insert (hash, "You must choose a longer password", _("You must choose a longer password"));
                g_hash_table_insert (hash, "Password has been already used. Choose another.", _("Password has been already used. Choose another."));
                g_hash_table_insert (hash, "You must wait longer to change your password", _("You must wait longer to change your password"));
                g_hash_table_insert (hash, "Sorry, passwords do not match", _("Sorry, passwords do not match"));
                /* FIXME: what about messages which have some variables in them, perhaps try to do those as well */
        }

        s = g_strstrip (g_strdup (msg));
        ret = g_hash_table_lookup (hash, s);
        g_free (s);

        if (ret != NULL) {
                return ret;
        } else {
                return msg;
        }
}

static gboolean
auth_message_handler (GSAuthMessageStyle style,
                      const char        *msg,
                      char             **response,
                      gpointer           data)
{
        gboolean    ret;
        GSLockPlug *plug;
        const char *message;

        plug = GS_LOCK_PLUG (data);

        gs_profile_start (NULL);
        gs_debug ("Got message style %d: '%s'", style, msg);

        gtk_widget_show (GTK_WIDGET (plug));
        gs_lock_plug_set_ready (plug);

        ret = TRUE;
        *response = NULL;
        message = maybe_translate_message (msg);

        switch (style) {
        case GS_AUTH_MESSAGE_PROMPT_ECHO_ON:
                if (msg != NULL) {
                        char *resp;
                        resp = request_response (plug, message, TRUE);
                        *response = resp;
                }
                break;
        case GS_AUTH_MESSAGE_PROMPT_ECHO_OFF:
                if (msg != NULL) {
                        char *resp;
                        resp = request_response (plug, message, FALSE);
                        *response = resp;
                }
                break;
        case GS_AUTH_MESSAGE_ERROR_MSG:
                gs_lock_plug_show_message (plug, message);
                break;
        case GS_AUTH_MESSAGE_TEXT_INFO:
                gs_lock_plug_show_message (plug, message);
                break;
        default:
                g_assert_not_reached ();
        }

        if (*response == NULL) {
                gs_debug ("Got no response");
                ret = FALSE;
        } else {
                gs_lock_plug_show_message (plug, _("Checking..."));
                gs_lock_plug_set_sensitive (plug, FALSE);
        }

        /* we may have pending events that should be processed before continuing back into PAM */
        while (gtk_events_pending ()) {
                gtk_main_iteration ();
        }

        gs_lock_plug_set_busy (plug);
        gs_profile_end (NULL);

        return ret;
}

static gboolean
reset_idle_cb (GSLockPlug *plug)
{
        gs_lock_plug_set_sensitive (plug, TRUE);
        gs_lock_plug_show_message (plug, NULL);

        return FALSE;
}

static gboolean
do_auth_check (GSLockPlug *plug)
{
        GError *error;
        gboolean res;

        error = NULL;

        gs_lock_plug_disable_prompt (plug);
        gs_lock_plug_set_busy (plug);
        res = gs_auth_verify_user (g_get_user_name (), g_getenv ("DISPLAY"), auth_message_handler, plug, &error);

        gs_debug ("Verify user returned: %s", res ? "TRUE" : "FALSE");
        if (! res) {
                if (error != NULL) {
                        gs_debug ("Verify user returned error: %s", error->message);
                        gs_lock_plug_show_message (plug, error->message);
                } else {
                        gs_lock_plug_show_message (plug, _("Authentication failed."));
                }

                printf ("NOTICE=AUTH FAILED\n");
                fflush (stdout);

                if (error != NULL) {
                        g_error_free (error);
                }
        }

        return res;
}

static void
response_cb (GSLockPlug *plug,
             gint        response_id)
{
        if ((response_id == GS_LOCK_PLUG_RESPONSE_CANCEL) ||
            (response_id == GTK_RESPONSE_DELETE_EVENT)) {
                quit_response_cancel ();
        }
}

static gboolean
response_request_quit (void)
{
        printf ("REQUEST QUIT\n");
        fflush (stdout);
        return FALSE;
}

static gboolean
quit_timeout_cb (gpointer data)
{
        gtk_main_quit ();
        return FALSE;
}

static gboolean
auth_check_idle (GSLockPlug *plug)
{
        gboolean     res;
        gboolean     again;
        static guint loop_counter = 0;

        again = TRUE;
        res = do_auth_check (plug);

        if (res) {
                again = FALSE;
                g_idle_add ((GSourceFunc)quit_response_ok, NULL);
        } else {
                loop_counter++;

                if (loop_counter < MAX_FAILURES) {
                        gs_debug ("Authentication failed, retrying (%u)", loop_counter);
                        g_timeout_add (3000, (GSourceFunc)reset_idle_cb, plug);
                } else {
                        gs_debug ("Authentication failed, quitting (max failures)");
                        again = FALSE;
                        /* Don't quit immediately, but rather request that gnome-screensaver
                         * terminates us after it has finished the dialog shake. Time out
                         * after 5 seconds and quit anyway if this doesn't happen though */
                        g_idle_add ((GSourceFunc)response_request_quit, NULL);
                        g_timeout_add (5000, (GSourceFunc)quit_timeout_cb, NULL);
                }
        }

        return again;
}

static void
show_cb (GtkWidget *widget,
         gpointer   data)
{
        print_id (widget);
}

static gboolean
popup_dialog_idle (void)
{
        GtkWidget *widget;

        gs_profile_start (NULL);

        widget = gs_lock_plug_new ();

        if (enable_logout) {
                g_object_set (widget, "logout-enabled", TRUE, NULL);
        }

        if (logout_command) {
                g_object_set (widget, "logout-command", logout_command, NULL);
        }

        if (enable_switch) {
                g_object_set (widget, "switch-enabled", TRUE, NULL);
        }

        if (status_message) {
                g_object_set (widget, "status-message", status_message, NULL);
        }

        g_signal_connect (GS_LOCK_PLUG (widget), "response", G_CALLBACK (response_cb), NULL);
        g_signal_connect (widget, "show", G_CALLBACK (show_cb), NULL);

        gtk_widget_realize (widget);

        g_idle_add ((GSourceFunc)auth_check_idle, widget);

        gs_profile_end (NULL);

        return FALSE;
}


/*
 * Copyright (c) 1991-2004 Jamie Zawinski <jwz@jwz.org>
 * Copyright (c) 2005 William Jon McCann <mccann@jhu.edu>
 *
 * Initializations that potentially take place as a priveleged user:
   If the executable is setuid root, then these initializations
   are run as root, before discarding privileges.
*/
static gboolean
privileged_initialization (int     *argc,
                           char   **argv,
                           gboolean verbose)
{
        gboolean ret;
        char    *nolock_reason;
        char    *orig_uid;
        char    *uid_message;

        gs_profile_start (NULL);

#ifndef NO_LOCKING
        /* before hack_uid () for proper permissions */
        gs_auth_priv_init ();
#endif /* NO_LOCKING */

        ret = hack_uid (&nolock_reason,
                        &orig_uid,
                        &uid_message);

        if (nolock_reason) {
                g_debug ("Locking disabled: %s", nolock_reason);
        }

        if (uid_message && verbose) {
                g_print ("Modified UID: %s", uid_message);
        }

        g_free (nolock_reason);
        g_free (orig_uid);
        g_free (uid_message);

        gs_profile_end (NULL);

        return ret;
}


/*
 * Copyright (c) 1991-2004 Jamie Zawinski <jwz@jwz.org>
 * Copyright (c) 2005 William Jon McCann <mccann@jhu.edu>
 *
 * Figure out what locking mechanisms are supported.
 */
static gboolean
lock_initialization (int     *argc,
                     char   **argv,
                     char   **nolock_reason,
                     gboolean verbose)
{
        if (nolock_reason != NULL) {
                *nolock_reason = NULL;
        }

#ifdef NO_LOCKING
        if (nolock_reason != NULL) {
                *nolock_reason = g_strdup ("not compiled with locking support");
        }

        return FALSE;
#else /* !NO_LOCKING */

        /* Finish initializing locking, now that we're out of privileged code. */
        if (! gs_auth_init ()) {
                if (nolock_reason != NULL) {
                        *nolock_reason = g_strdup ("error getting password");
                }

                return FALSE;
        }

        /* If locking is currently enabled, but the environment indicates that
           we have been launched as GDM's "Background" program, then disable
           locking just in case.
        */
        if (getenv ("RUNNING_UNDER_GDM")) {
                if (nolock_reason != NULL) {
                        *nolock_reason = g_strdup ("running under GDM");
                }

                return FALSE;
        }

        /* If the server is XDarwin (MacOS X) then disable locking.
           (X grabs only affect X programs, so you can use Command-Tab
           to bring any other Mac program to the front, e.g., Terminal.)
        */
        {
                gboolean macos = FALSE;

#ifdef __APPLE__
                /* Disable locking if *running* on Apple hardware, since we have no
                   reliable way to determine whether the server is running on MacOS.
                   Hopefully __APPLE__ means "MacOS" and not "Linux on Mac hardware"
                   but I'm not really sure about that.
                */
                macos = TRUE;
#endif

                if (macos) {
                        if (nolock_reason != NULL) {
                                *nolock_reason = g_strdup ("Cannot lock securely on MacOS X");
                        }

                        return FALSE;
                }
        }

#endif /* NO_LOCKING */

        return TRUE;
}

int
main (int    argc,
      char **argv)
{
        GError *error = NULL;
        char   *nolock_reason = NULL;

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif
        textdomain (GETTEXT_PACKAGE);
#endif

        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }

        g_type_init ();

        gs_profile_start (NULL);

        if (! privileged_initialization (&argc, argv, verbose)) {
                response_lock_init_failed ();
                exit (1);
        }

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, NULL, entries, NULL, &error)) {
                if (error != NULL) {
                        fprintf (stderr, "%s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        if (! lock_initialization (&argc, argv, &nolock_reason, verbose)) {
                if (nolock_reason != NULL) {
                        g_debug ("Screen locking disabled: %s", nolock_reason);
                        g_free (nolock_reason);
                }
                response_lock_init_failed ();
                exit (1);
        }

        gs_debug_init (verbose, FALSE);

        g_idle_add ((GSourceFunc)popup_dialog_idle, NULL);

        gtk_main ();

        gs_profile_end (NULL);
        gs_debug_shutdown ();

        return 0;
}
