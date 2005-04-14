/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * passwd-pwent.c --- verifying typed passwords with the OS.
 *
 * xscreensaver, Copyright (c) 1993-1998 Jamie Zawinski <jwz@jwz.org>
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

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_CRYPT_H
# include <crypt.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef VMS
# include <pwd.h>
# include <grp.h>
#else /* VMS */
# include "vms-pwd.h"
#endif /* VMS */

#ifdef __bsdi__
# include <sys/param.h>
# if _BSDI_VERSION >= 199608
#  define BSD_AUTH
# endif
#endif /* __bsdi__ */

#include <glib.h>

#if defined(HAVE_SHADOW_PASSWD)	      /* passwds live in /etc/shadow */

#   include <shadow.h>
#   define PWTYPE   struct spwd *
#   define PWPSLOT  sp_pwdp
#   define GETPW    getspnam

#elif defined(HAVE_ENHANCED_PASSWD)      /* passwds live in /tcb/files/auth/ */
/* M.Matsumoto <matsu@yao.sharp.co.jp> */
#   include <sys/security.h>
#   include <prot.h>

#   define PWTYPE   struct pr_passwd *
#   define PWPSLOT  ufld.fd_encrypt
#   define GETPW    getprpwnam

#elif defined(HAVE_ADJUNCT_PASSWD)

#   include <sys/label.h>
#   include <sys/audit.h>
#   include <pwdadj.h>

#   define PWTYPE   struct passwd_adjunct *
#   define PWPSLOT  pwa_passwd
#   define GETPW    getpwanam

#elif defined(HAVE_HPUX_PASSWD)

#   include <hpsecurity.h>
#   include <prot.h>

#   define PWTYPE   struct s_passwd *
#   define PWPSLOT  pw_passwd
#   define GETPW    getspwnam

#   define HAVE_BIGCRYPT

#endif

#include "passwd-pwent.h"

static char *encrypted_root_passwd = NULL;
static char *encrypted_user_passwd = NULL;

#ifdef VMS
# define ROOT "SYSTEM"
#else
# define ROOT "root"
#endif

static gboolean
passwd_known (const char *pw)
{
        return (pw &&
                pw[0] != '*' &&	/* This would be sensible...         */
                strlen (pw) > 4);	/* ...but this is what Solaris does. */
}

static char *
get_encrypted_passwd (const char *user)
{
        char *result = NULL;

#ifdef PWTYPE
        if (user && *user && !result) {
                /* First check the shadow passwords. */
                PWTYPE p = GETPW ((char *) user);
                if (p && passwd_known (p->PWPSLOT))
                        result = g_strdup (p->PWPSLOT);
        }
#endif /* PWTYPE */

        if (user && *user && !result) {
                /* Check non-shadow passwords too. */
                struct passwd *p = getpwnam (user);
                if (p && passwd_known (p->pw_passwd))
                        result = g_strdup (p->pw_passwd);
        }

        /* The manual for passwd(4) on HPUX 10.10 says:

        Password aging is put in effect for a particular user if his
        encrypted password in the password file is followed by a comma and
        a nonnull string of characters from the above alphabet.  This
        string defines the "age" needed to implement password aging.

        So this means that passwd->pw_passwd isn't simply a string of cyphertext,
        it might have trailing junk.  So, if there is a comma in the string, and
        that comma is beyond position 13, terminate the string before the comma.
        */
        if (result && strlen (result) > 13) {
                char *s = strchr (result + 13, ',');
                if (s)
                        *s = 0;
        }

#ifndef HAVE_PAM
        /* We only issue this warning if not compiled with support for PAM.
           If we're using PAM, it's not unheard of that normal pwent passwords
           would be unavailable. */

        if (!result)
                g_warning ("Couldn't get password of \"%s\"",
                           (user ? user : "(null)"));
#endif /* !HAVE_PAM */

        return result;
}

/* This has to be called before we've changed our effective user ID,
   because it might need privileges to get at the encrypted passwords.
   Returns false if we weren't able to get any passwords, and therefore,
   locking isn't possible.  (It will also have written to stderr.)
*/

#ifndef VMS

gboolean
pwent_priv_init (int      argc,
                 char   **argv,
                 gboolean verbose)
{
        const char *u;

#ifdef HAVE_ENHANCED_PASSWD
        set_auth_parameters (argc, argv);
        check_auth_parameters ();
#endif /* HAVE_DEC_ENHANCED */

        u = g_get_user_name ();

        encrypted_user_passwd = get_encrypted_passwd (u);
        encrypted_root_passwd = get_encrypted_passwd (ROOT);

        if (encrypted_user_passwd)
                return TRUE;
        else
                return FALSE;
}


gboolean
pwent_lock_init (int      argc,
                 char   **argv,
                 gboolean verbose)
{
        if (encrypted_user_passwd)
                return TRUE;
        else
                return FALSE;
}



static gboolean
passwds_match (const char *cleartext,
               const char *ciphertext)
{
        char *s = NULL;  /* note that on some systems, crypt() may return null */

        s = (char *) crypt (cleartext, ciphertext);
        if (s && !strcmp (s, ciphertext))
                return TRUE;

#ifdef HAVE_BIGCRYPT
        /* There seems to be no way to tell at runtime if an HP machine is in
           "trusted" mode, and thereby, which of crypt() or bigcrypt() we should
           be calling to compare passwords.  So call them both, and see which
           one works. */

        s = (char *) bigcrypt (cleartext, ciphertext);
        if (s && !strcmp (s, ciphertext))
                return TRUE;

#endif /* HAVE_BIGCRYPT */
  
        return FALSE;
}



/* This can be called at any time, and says whether the typed password
   belongs to either the logged in user (real uid, not effective); or
   to root.
*/

gboolean
pwent_passwd_valid (const char *typed_passwd,
                    gboolean    verbose)
{
        if (encrypted_user_passwd &&
            passwds_match (typed_passwd, encrypted_user_passwd))
                return TRUE;

        /* do not allow root to have a null password. */
        else if (typed_passwd [0] &&
                 encrypted_root_passwd &&
                 passwds_match (typed_passwd, encrypted_root_passwd))
                return TRUE;

        else
                return FALSE;
}

#else  /* VMS */

gboolean
pwent_lock_init (int      argc,
                 char   **argv,
                 gboolean verbose_p)
{
        return TRUE;
}

#endif /* VMS */

#endif /* NO_LOCKING -- whole file */
