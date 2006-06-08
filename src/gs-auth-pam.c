/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2003 Bill Nottingham <notting@redhat.com>
 * Copyright (c) 1993-2003 Jamie Zawinski <jwz@jwz.org>
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
 */

#include "config.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "gs-auth.h"

#include "subprocs.h"

/* Some time between Red Hat 4.2 and 7.0, the words were transposed
   in the various PAM_x_CRED macro names.  Yay!
*/
#ifndef  PAM_REFRESH_CRED
# define PAM_REFRESH_CRED PAM_CRED_REFRESH
#endif

#ifdef HAVE_PAM_FAIL_DELAY
/* We handle delays ourself.*/
/* Don't set this to 0 (Linux bug workaround.) */
# define PAM_NO_DELAY(pamh) pam_fail_delay ((pamh), 1)
#else  /* !HAVE_PAM_FAIL_DELAY */
# define PAM_NO_DELAY(pamh) /* */
#endif /* !HAVE_PAM_FAIL_DELAY */


/* On SunOS 5.6, and on Linux with PAM 0.64, pam_strerror() takes two args.
   On some other Linux systems with some other version of PAM (e.g.,
   whichever Debian release comes with a 2.2.5 kernel) it takes one arg.
   I can't tell which is more "recent" or "correct" behavior, so configure
   figures out which is in use for us.  Shoot me!
*/
#ifdef PAM_STRERROR_TWO_ARGS
# define PAM_STRERROR(pamh, status) pam_strerror((pamh), (status))
#else  /* !PAM_STRERROR_TWO_ARGS */
# define PAM_STRERROR(pamh, status) pam_strerror((status))
#endif /* !PAM_STRERROR_TWO_ARGS */

static gboolean verbose_enabled = FALSE;
static pam_handle_t *pam_handle = NULL;
static gboolean did_we_ask_for_password = FALSE;

struct pam_closure {
        const char       *username;
        GSAuthMessageFunc cb_func;
        gpointer          cb_data;
};

GQuark
gs_auth_error_quark (void)
{
        static GQuark quark = 0;
        if (! quark) {
                quark = g_quark_from_static_string ("gs_auth_error");
        }

        return quark;
}

void
gs_auth_set_verbose (gboolean enabled)
{
        verbose_enabled = enabled;
}

gboolean
gs_auth_get_verbose (void)
{
        return verbose_enabled;
}

static GSAuthMessageStyle
pam_style_to_gs_style (int pam_style)
{
        GSAuthMessageStyle style;

        switch (pam_style) {
        case PAM_PROMPT_ECHO_ON:
                style = GS_AUTH_MESSAGE_PROMPT_ECHO_ON;
                break;
        case PAM_PROMPT_ECHO_OFF:
                style = GS_AUTH_MESSAGE_PROMPT_ECHO_OFF;
                break;
        case PAM_ERROR_MSG:
                style = GS_AUTH_MESSAGE_ERROR_MSG;
                break;
        case PAM_TEXT_INFO:
                style = GS_AUTH_MESSAGE_TEXT_INFO;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return style;
}

static gboolean
auth_message_handler (GSAuthMessageStyle style,
                      const char        *msg,
                      char             **response,
                      gpointer           data)
{
        gboolean ret;

        ret = TRUE;
        *response = NULL;

        switch (style) {
        case GS_AUTH_MESSAGE_PROMPT_ECHO_ON:
                break;
        case GS_AUTH_MESSAGE_PROMPT_ECHO_OFF:
                if (msg != NULL && g_str_has_prefix (msg, "Password:")) {
                        did_we_ask_for_password = TRUE;
                }
                break;
        case GS_AUTH_MESSAGE_ERROR_MSG:
                break;
        case GS_AUTH_MESSAGE_TEXT_INFO:
                break;
        default:
                g_assert_not_reached ();
        }

        return ret;
}

static int
pam_conversation (int                        nmsgs,
                  const struct pam_message **msg,
                  struct pam_response      **resp,
                  void                      *closure)
{
        int                  replies = 0;
        struct pam_response *reply = NULL;
        struct pam_closure  *c = (struct pam_closure *) closure;
        gboolean             res;
        int                  ret;

        reply = (struct pam_response *) calloc (nmsgs, sizeof (*reply));

        if (reply == NULL) {
                return PAM_CONV_ERR;
        }

        res = TRUE;
        ret = PAM_SUCCESS;

        for (replies = 0; replies < nmsgs; replies++) {
                GSAuthMessageStyle style;

                style = pam_style_to_gs_style (msg [replies]->msg_style);

                /* handle message locally first */
                auth_message_handler (style,
                                      msg [replies]->msg,
                                      &reply [replies].resp,
                                      NULL);

                if (c->cb_func != NULL) {
                        res = c->cb_func (style,
                                          msg [replies]->msg,
                                          &reply [replies].resp,
                                          c->cb_data);

                        /* If the handler returns FALSE - interrupt the PAM stack */
                        if (res) {
                                reply [replies].resp_retcode = PAM_SUCCESS;
                        } else {
                                reply [replies].resp_retcode = PAM_INCOMPLETE;
                        }
                }
        }

        *resp = reply;

        return ret;
}

static gboolean
close_pam_handle (int status)
{

        if (pam_handle != NULL) {
                int status2;

                status2 = pam_end (pam_handle, status);
                pam_handle = NULL;

                if (gs_auth_get_verbose ()) {
                        g_message (" pam_end (...) ==> %d (%s)",
                                   status2,
                                   (status2 == PAM_SUCCESS ? "Success" : "Failure"));
                }
        }

        return TRUE;
}

static gboolean
create_pam_handle (const char      *username,
                   const char      *display,
                   struct pam_conv *conv,
                   int             *status_code)
{
        int         status;
        const char *service = PAM_SERVICE_NAME;
        char       *disp;
        gboolean    ret;

	if (pam_handle != NULL) {
		g_warning ("create_pam_handle: Stale pam handle around, cleaning up");
                close_pam_handle (PAM_SUCCESS);
	}

	/* init things */
	pam_handle = NULL;
        status = -1;
        disp = NULL;
        ret = TRUE;

	/* Initialize a PAM session for the user */
	if ((status = pam_start (service, username, conv, &pam_handle)) != PAM_SUCCESS) {
		pam_handle = NULL;
                g_warning (_("Unable to establish service %s: %s\n"),
                           service,
                           PAM_STRERROR (NULL, status));

                if (status_code != NULL) {
                        *status_code = status;
                }

                ret = FALSE;
                goto out;
	}

        if (gs_auth_get_verbose ()) {
                g_message ("pam_start (\"%s\", \"%s\", ...) ==> %d (%s)",
                           service,
                           username,
                           status,
                           PAM_STRERROR (pam_handle, status));
        }

        disp = g_strdup (display);
        if (disp == NULL) {
                disp = g_strdup (":0.0");
        }

	if ((status = pam_set_item (pam_handle, PAM_TTY, disp)) != PAM_SUCCESS) {
                g_warning (_("Can't set PAM_TTY=%s"), display);

                if (status_code != NULL) {
                        *status_code = status;
                }

                ret = FALSE;
                goto out;
	}

        ret = TRUE;

 out:
        if (status_code != NULL) {
                *status_code = status;
        }

        g_free (disp);

        return ret;
}

static void
set_pam_error (GError **error,
               int      status)
{
        if (status == PAM_AUTH_ERR || status == PAM_USER_UNKNOWN) {
                char *msg;

                if (did_we_ask_for_password) {
                        msg = g_strdup (_("Incorrect password."));
                } else {
                        msg = g_strdup (_("Authentication failed."));
                }

                g_set_error (error,
                             GS_AUTH_ERROR,
                             GS_AUTH_ERROR_AUTH_ERROR,
                             "%s",
                             msg);
                g_free (msg);
        } else if (status == PAM_PERM_DENIED) {
                g_set_error (error,
                             GS_AUTH_ERROR,
                             GS_AUTH_ERROR_AUTH_DENIED,
                             "%s",
                             _("Not permitted to gain access at this time."));
        } else if (status == PAM_ACCT_EXPIRED) {
                g_set_error (error,
                             GS_AUTH_ERROR,
                             GS_AUTH_ERROR_AUTH_DENIED,
                             "%s",
                             _("No longer permitted to access the system."));
        }

}

gboolean
gs_auth_verify_user (const char       *username,
                     const char       *display,
                     GSAuthMessageFunc func,
                     gpointer          data,
                     GError          **error)
{
        int                status = -1;
        int                status2;
        struct pam_conv    conv;
        struct pam_closure c;
        sigset_t           set;
        struct timespec    timeout;
        struct passwd     *pwent;
        int                null_tok = 0;
        const void        *p;

        pwent = getpwnam (username);
        if (pwent == NULL) {
                return FALSE;
        }

        c.username = username;
        c.cb_func = func;
        c.cb_data = data;

        conv.conv = &pam_conversation;
        conv.appdata_ptr = (void *) &c;

        /* Initialize PAM. */
        create_pam_handle (username, display, &conv, &status);
        if (status != PAM_SUCCESS) {
                goto DONE;
        }

        pam_set_item (pam_handle, PAM_USER_PROMPT, _("Username:"));

        PAM_NO_DELAY(pam_handle);

        timeout.tv_sec = 0;
        timeout.tv_nsec = 1;
        set = block_sigchld ();

        did_we_ask_for_password = FALSE;
        status = pam_authenticate (pam_handle, null_tok);

        sigtimedwait (&set, NULL, &timeout);
        unblock_sigchld ();

        if (gs_auth_get_verbose ()) {
                g_message ("   pam_authenticate (...) ==> %d (%s)",
                           status,
                           PAM_STRERROR (pam_handle, status));
        }

        if (status != PAM_SUCCESS) {
                goto DONE;
        }

        if ((status = pam_get_item (pam_handle, PAM_USER, &p)) != PAM_SUCCESS) {
                /* is not really an auth problem, but it will
                   pretty much look as such, it shouldn't really
                   happen */
                goto DONE;
        }

        /* We don't actually care if the account modules fail or succeed,
         * but we need to run them anyway because certain pam modules
         * depend on side effects of the account modules getting run.
         */
        status2 = pam_acct_mgmt (pam_handle, null_tok);

        if (gs_auth_get_verbose ()) {
                g_message ("pam_acct_mgmt (...) ==> %d (%s)\n",
                           status2,
                           PAM_STRERROR (pam_handle, status2));
        }

        /* FIXME: should we handle these? */
        switch (status2) {
        case PAM_SUCCESS:
                break;
        case PAM_NEW_AUTHTOK_REQD:
                break;
        case PAM_AUTHINFO_UNAVAIL:
                break;
        case PAM_ACCT_EXPIRED:
                break;
        case PAM_PERM_DENIED:
                break;
        default :
                break;
        }

        /* Each time we successfully authenticate, refresh credentials,
           for Kerberos/AFS/DCE/etc.  If this fails, just ignore that
           failure and blunder along; it shouldn't matter.

           Note: this used to be PAM_REFRESH_CRED instead of
           PAM_REINITIALIZE_CRED, but Jason Heiss <jheiss@ee.washington.edu>
           says that the Linux PAM library ignores that one, and only refreshes
           credentials when using PAM_REINITIALIZE_CRED.
        */
        status2 = pam_setcred (pam_handle, PAM_REINITIALIZE_CRED);
        if (gs_auth_get_verbose ()) {
                g_message ("   pam_setcred (...) ==> %d (%s)",
                           status2,
                           PAM_STRERROR (pam_handle, status2));
        }

 DONE:
        if (status != PAM_SUCCESS) {
                set_pam_error (error, status);
        }

        close_pam_handle (status);

        return (status == PAM_SUCCESS ? TRUE : FALSE);
}

gboolean
gs_auth_init (void)
{
        return TRUE;
}

gboolean
gs_auth_priv_init (void)
{
        /* We have nothing to do at init-time.
           However, we might as well do some error checking.
           If "/etc/pam.d" exists and is a directory, but "/etc/pam.d/xlock"
           does not exist, warn that PAM probably isn't going to work.

           This is a priv-init instead of a non-priv init in case the directory
           is unreadable or something (don't know if that actually happens.)
        */
        const char   dir [] = "/etc/pam.d";
        const char  file [] = "/etc/pam.d/" PAM_SERVICE_NAME;
        const char file2 [] = "/etc/pam.conf";
        struct stat st;

        if (g_stat (dir, &st) == 0 && st.st_mode & S_IFDIR) {
                if (g_stat (file, &st) != 0) {
                        g_warning ("%s does not exist.\n"
                                   "Authentication via PAM is unlikely to work.",
                                   file);
                }
        } else if (g_stat (file2, &st) == 0) {
                FILE *f = g_fopen (file2, "r");
                if (f) {
                        gboolean ok = FALSE;
                        char buf[255];
                        while (fgets (buf, sizeof(buf), f)) {
                                if (strstr (buf, PAM_SERVICE_NAME)) {
                                        ok = TRUE;
                                        break;
                                }
                        }

                        fclose (f);
                        if (!ok) {
                                g_warning ("%s does not list the `%s' service.\n"
                                           "Authentication via PAM is unlikely to work.",
                                           file2, PAM_SERVICE_NAME);
                        }
                }
                /* else warn about file2 existing but being unreadable? */
        } else {
                g_warning ("Neither %s nor %s exist.\n"
                           "Authentication via PAM is unlikely to work.",
                           file2, file);
        }

        /* Return true anyway, just in case. */
        return TRUE;
}
