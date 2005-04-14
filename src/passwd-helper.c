/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * passwd-helper.c --- verifying typed passwords with external helper program
 *
 * written by Olaf Kirch <okir@suse.de>
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

/* The idea here is to be able to run xscreensaver without any setuid bits.
 * Password verification happens through an external program that you feed
 * your password to on stdin.  The external command is invoked with a user
 * name argument.
 *
 * The external helper does whatever authentication is necessary.  Currently,
 * SuSE uses "unix2_chkpwd", which is a variation of "unix_chkpwd" from the
 * PAM distribution.
 *
 * Normally, the password helper should just authenticate the calling user
 * (i.e. based on the caller's real uid).  This is in order to prevent
 * brute-forcing passwords in a shadow environment.  A less restrictive
 * approach would be to allow verifying other passwords as well, but always
 * with a 2 second delay or so.  (Not sure what SuSE's "unix2_chkpwd"
 * currently does.)
 *                         -- Olaf Kirch <okir@suse.de>, 16-Dec-2003
 */

#include "config.h"

#ifndef NO_LOCKING  /* whole file */

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <sys/wait.h>

#include <glib.h>

#include "passwd-helper.h"

extern void block_sigchld (void);
extern void unblock_sigchld (void);

static gboolean
ext_run (const char *user,
         const char *typed_passwd,
         gboolean    verbose)
{
        int   pfd[2], status;
        pid_t pid;

        if (pipe (pfd) < 0)
                return 0;

        if (verbose)
                g_message ("ext_run (%s, %s)",
                           PASSWD_HELPER_PROGRAM, user);

        block_sigchld ();

        if ((pid = fork ()) < 0) {
                close (pfd [0]);
                close (pfd [1]);
                return FALSE;
        }

        if (pid == 0) {
                close (pfd [1]);
                if (pfd [0] != 0)
                        dup2 (pfd [0], 0);

                /* Helper is invoked as helper service-name [user] */
                execlp (PASSWD_HELPER_PROGRAM, PASSWD_HELPER_PROGRAM, "xscreensaver", user, NULL);
                if (verbose)
                        g_message ("%s: %s", PASSWD_HELPER_PROGRAM, strerror (errno));
                exit (1);
        }

        close (pfd [0]);

        /* Write out password to helper process */
        if (!typed_passwd)
                typed_passwd = "";
        write (pfd [1], typed_passwd, strlen (typed_passwd));
        close (pfd [1]);

        while (waitpid (pid, &status, 0) < 0) {
                if (errno == EINTR)
                        continue;
                if (verbose)
                        g_message ("ext_run: waitpid failed: %s\n",
                                   strerror (errno));
                unblock_sigchld ();
                return FALSE;
        }

        unblock_sigchld ();

        if (! WIFEXITED (status) || WEXITSTATUS (status) != 0)
                return FALSE;

        return TRUE;
}

/* This can be called at any time, and says whether the typed password
   belongs to either the logged in user (real uid, not effective); or
   to root.
*/
gboolean
ext_passwd_valid (const char *typed_passwd,
                  gboolean    verbose)
{
        struct passwd *pw;
        gboolean       res = FALSE;

        if ((pw = getpwuid (getuid ())) != NULL)
                res = ext_run (pw->pw_name, typed_passwd, verbose);
        endpwent ();

        if (!res)
                res = ext_run ("root", typed_passwd, verbose);

        return res;
}

gboolean 
ext_priv_init (int      argc,
               char   **argv,
               gboolean verbose)
{
        /* Make sure the passwd helper exists */
        if (access (PASSWD_HELPER_PROGRAM, X_OK) < 0) {
                g_warning ("%s does not exist. "
                           "password authentication via "
                           "external helper will not work.",
                           PASSWD_HELPER_PROGRAM);
                return FALSE;
        }

        return TRUE;
}

#endif /* NO_LOCKING -- whole file */
