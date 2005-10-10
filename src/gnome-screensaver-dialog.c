/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-lock-plug.h"

#include "passwd.h"
#include "setuid.h"


/* Profiling stuff adapted from gtkfilechooserdefault */
/* To use run:
 *  strace -ttt -f -o logfile.txt ./gnome-screensaver-dialog
 */

#undef PROFILE_LOCK_DIALOG
#ifdef PROFILE_LOCK_DIALOG

#define PROFILE_INDENT 4
static int profile_indent;

static void
profile_add_indent (int indent)
{
        profile_indent += indent;
        if (profile_indent < 0)
                g_error ("You screwed up your indentation");
}

static void
_gs_lock_plug_profile_log (const char *func,
                           int         indent,
                           const char *msg1,
                           const char *msg2)
{
        char *str;

        if (indent < 0)
                profile_add_indent (indent);

        if (profile_indent == 0)
                str = g_strdup_printf ("MARK: %s: %s %s %s", G_STRLOC, func, msg1 ? msg1 : "", msg2 ? msg2 : "");
        else
                str = g_strdup_printf ("MARK: %s: %*c %s %s %s", G_STRLOC, profile_indent - 1, ' ', func, msg1 ? msg1 : "", msg2 ? msg2 : "");

        access (str, F_OK);

        g_free (str);

        if (indent > 0)
                profile_add_indent (indent);
}

#define profile_start(x, y) _gs_lock_plug_profile_log (G_STRFUNC, PROFILE_INDENT, x, y)
#define profile_end(x, y)   _gs_lock_plug_profile_log (G_STRFUNC, -PROFILE_INDENT, x, y)
#define profile_msg(x, y)   _gs_lock_plug_profile_log (NULL, 0, x, y)
#else
#define profile_start(x, y)
#define profile_end(x, y)
#define profile_msg(x, y)
#endif


static gboolean verbose       = FALSE;
static gboolean show_version  = FALSE;
static gboolean enable_logout = FALSE;
static gboolean enable_switch = FALSE;

static GOptionEntry entries [] = {
        { "verbose", 0, 0, G_OPTION_ARG_NONE, &verbose,
          N_("Show debugging output"), NULL },
        { "version", 0, 0, G_OPTION_ARG_NONE, &show_version,
          N_("Version of this application"), NULL },
        { "enable-logout", 0, 0, G_OPTION_ARG_NONE, &enable_logout,
          N_("Show the logout button"), NULL },
        { "enable-switch", 0, 0, G_OPTION_ARG_NONE, &enable_switch,
          N_("Show the switch user button"), NULL },
        { NULL }
};

static char *
get_id_string (GtkWidget *widget)
{
        char *id = NULL;

        g_return_val_if_fail (widget != NULL, NULL);
        g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

        id = g_strdup_printf ("0x%X",
                              (guint32) GDK_WINDOW_XID (widget->window));
        return id;
}

static gboolean
print_id (GtkWidget *widget)
{
        profile_start ("start", NULL);

        printf ("WINDOW ID=%s\n", get_id_string (widget));
        fflush (stdout);

        profile_end ("end", NULL);

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

static void
response_lock_init_failed (void)
{
        /* if we fail to lock then we should drop the dialog */
        response_ok ();
}

static void
response_cb (GSLockPlug *plug,
             gint        response_id)
{
        switch (response_id) {
        case (GS_LOCK_PLUG_RESPONSE_OK):
                response_ok ();
                break;
        case (GS_LOCK_PLUG_RESPONSE_CANCEL):
        default:
                response_cancel ();
                break;
        }

        gtk_main_quit ();
}

static gboolean
popup_dialog_idle (void)
{
        GtkWidget *widget;

        profile_start ("start", NULL);

        widget = gs_lock_plug_new ();

        if (enable_logout) {
                g_object_set (widget, "logout-enabled", TRUE, NULL);
        }

        if (enable_switch) {
                g_object_set (widget, "switch-enabled", TRUE, NULL);
        }

        g_signal_connect (GS_LOCK_PLUG (widget), "response", G_CALLBACK (response_cb), NULL);

        gtk_widget_show (widget);

        print_id (widget);

        profile_end ("end", NULL);

        return FALSE;
}


/*
 * Copyright (c) 1991-2004 Jamie Zawinski <jwz@jwz.org>
 * Copyright (c) 2005 William Jon McCann <mccann@jhu.edu>
 *
 * Initializations that potentially take place as a priveleged user:
   If the xscreensaver executable is setuid root, then these initializations
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

        profile_start ("start", NULL);

#ifndef NO_LOCKING
        /* before hack_uid () for proper permissions */
        lock_priv_init (*argc, argv, verbose);
#endif /* NO_LOCKING */

        ret = hack_uid (&nolock_reason,
                        &orig_uid,
                        &uid_message);
        if (nolock_reason)
                g_warning ("Locking disabled: %s", nolock_reason);
        if (uid_message && verbose)
                g_print ("Modified UID: %s", uid_message);

        g_free (nolock_reason);
        g_free (orig_uid);
        g_free (uid_message);

        profile_end ("end", NULL);

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
        profile_start ("start", NULL);

        if (nolock_reason)
                *nolock_reason = NULL;

#ifdef NO_LOCKING
        if (nolock_reason)
                *nolock_reason = g_strdup ("not compiled with locking support");
        return FALSE;
#else /* !NO_LOCKING */

        /* Finish initializing locking, now that we're out of privileged code. */
        if (! lock_init (*argc, argv, verbose)) {
                if (nolock_reason)
                        *nolock_reason = g_strdup ("error getting password");
                return FALSE;
        }

        /* If locking is currently enabled, but the environment indicates that
           we have been launched as GDM's "Background" program, then disable
           locking just in case.
        */
        if (getenv ("RUNNING_UNDER_GDM")) {
                if (nolock_reason)
                        *nolock_reason = g_strdup ("running under GDM");
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
                        if (nolock_reason)
                                *nolock_reason = g_strdup ("Cannot lock securely on MacOS X");
                        return FALSE;
                }
        }

#endif /* NO_LOCKING */

        profile_end ("end", NULL);

        return TRUE;
}

int
main (int    argc,
      char **argv)
{
        GError         *error = NULL;
        char           *nolock_reason = NULL;

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif 
        textdomain (GETTEXT_PACKAGE);
#endif 

        g_type_init ();

        if (error) {
                fprintf (stderr, "%s\n", error->message);
                response_lock_init_failed ();
                exit (1);
        }

        profile_start ("start", NULL);

        if (! privileged_initialization (&argc, argv, verbose)) {
                response_lock_init_failed ();
                exit (1);
        }

        if (! gtk_init_with_args (&argc, &argv, NULL, entries, NULL, &error)) {
                fprintf (stderr, "%s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        if (! lock_initialization (&argc, argv, &nolock_reason, verbose)) {
                if (nolock_reason) {
                        g_warning ("Screen locking disabled: %s", nolock_reason);
                        g_free (nolock_reason);
                }
                response_lock_init_failed ();
                exit (1);
        }

        g_idle_add ((GSourceFunc)popup_dialog_idle, NULL);

        gtk_main ();

        profile_end ("end", NULL);

	return 0;
}
