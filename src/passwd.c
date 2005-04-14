/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * passwd.c --- verifying typed passwords with the OS.
 *
 * xscreensaver, Copyright (c) 1993-2004 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#include "config.h"

#ifndef NO_LOCKING  /* whole file */

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <glib.h>

#include "passwd.h"

struct auth_methods {
        const char *name;
        gboolean (*initialize)      (int argc, char **argv, gboolean verbose);
        gboolean (*priv_initialize) (int argc, char **argv, gboolean verbose);
        gboolean (*validate)        (const char *typed_passwd, gboolean verbose);
        gboolean initialized;
        gboolean priv_initialized;
};


#ifdef HAVE_KERBEROS
extern gboolean kerberos_lock_init      (int argc, char **argv, gboolean verbose);
extern gboolean kerberos_passwd_valid_p (const char *typed_passwd, gboolean verbose);
#endif
#ifdef HAVE_PAM
# include "passwd-pam.h"
#endif
#ifdef PASSWD_HELPER_PROGRAM
extern gboolean ext_priv_init           (int argc, char **argv, gboolean verbose);
extern gboolean ext_passwd_valid        (const char *typed_passwd, gboolean verbose);
#endif
#include "passwd-pwent.h"

/* The authorization methods to try, in order.
   Note that the last one (the pwent version) is actually two auth methods,
   since that code tries shadow passwords, and then non-shadow passwords.
   (It's all in the same file since the APIs are randomly nearly-identical.)
*/

static struct auth_methods methods[] = {
# ifdef HAVE_KERBEROS
        { "Kerberos",         kerberos_lock_init, NULL, kerberos_passwd_valid, FALSE, FALSE },
# endif
# ifdef HAVE_PAM
        { "PAM",              NULL, pam_priv_init, pam_passwd_valid, FALSE, FALSE },
# endif
# ifdef PASSWD_HELPER_PROGRAM
        { "external",	      NULL, ext_priv_init, ext_passwd_valid, FALSE, FALSE },
#endif
        { "normal",           pwent_lock_init, pwent_priv_init, pwent_passwd_valid, FALSE, FALSE }
};


gboolean
lock_priv_init (int      argc,
                char   **argv,
                gboolean verbose)
{
        int i;
        gboolean any_ok = FALSE;

        for (i = 0; i < G_N_ELEMENTS (methods); i++) {
                if (verbose)
                        g_message ("priv initializing %s passwords", methods [i].name);

                if (!methods [i].priv_initialize)
                        methods [i].priv_initialized = TRUE;
                else
                        methods [i].priv_initialized
                                = methods [i].priv_initialize (argc, argv, verbose);

                if (methods [i].priv_initialized)
                        any_ok = TRUE;
                else if (verbose)
                        g_warning ("priv initialization of %s passwords failed.", methods [i].name);
        }

        return any_ok;
}


gboolean
lock_init (int      argc,
           char   **argv,
           gboolean verbose)
{
        int i;
        gboolean any_ok = FALSE;

        for (i = 0; i < G_N_ELEMENTS (methods); i++) {
                if (!methods[i].priv_initialized)	/* Bail if lock_priv_init failed. */
                        continue;

                if (verbose)
                        g_message ("initializing %s passwords", methods [i].name);

                if (!methods[i].initialize)
                        methods[i].initialized = TRUE;
                else
                        methods[i].initialized = methods [i].initialize (argc, argv, verbose);

                if (methods[i].initialized)
                        any_ok = TRUE;
                else if (verbose)
                        g_warning ("initialization of %s passwords failed.", methods [i].name);
        }

        return any_ok;
}


gboolean
validate_password (const char *typed_passwd,
                   gboolean    verbose)
{
        int i, j;
        for (i = 0; i < G_N_ELEMENTS (methods); i++) {
                int ok_p = (methods [i].initialized &&
                            methods [i].validate (typed_passwd, verbose));

                if (ok_p) {
                        /* If we successfully authenticated by method N, but attempting
                           to authenticate by method N-1 failed, mention that (since if
                           an earlier authentication method fails and a later one succeeds,
                           something screwy is probably going on.)
                        */
                        if (verbose && i > 0) {
                                for (j = 0; j < i; j++)
                                        if (methods [j].initialized)
                                                g_warning ("authentication via %s passwords failed.",
                                                           methods [j].name);
                                g_message ("authentication via %s passwords succeeded.",
                                           methods [i].name);
                        }

                        return TRUE;		/* Successfully authenticated! */
                }
        }

        return FALSE;			/* Authentication failure. */
}

#endif /* NO_LOCKING -- whole file */
