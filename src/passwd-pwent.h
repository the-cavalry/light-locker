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
 *
 */

#ifndef __GS_PASSWD_PWENT_H
#define __GS_PASSWD_PWENT_H

G_BEGIN_DECLS

gboolean pwent_lock_init         (int         argc,
                                  char      **argv,
                                  gboolean    verbose);
gboolean pwent_priv_init         (int         argc,
                                  char      **argv,
                                  gboolean    verbose);
gboolean pwent_passwd_valid      (const char *typed_passwd,
                                  gboolean    verbose);

G_END_DECLS

#endif /* __GS_PASSWD_PWENT_H */
