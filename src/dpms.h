/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __GS_DPMS_H
#define __GS_DPMS_H

#include <gdk/gdkx.h>

G_BEGIN_DECLS

void
sync_server_dpms_settings (Display *dpy,
                           gboolean enabled,
                           int      standby_secs,
                           int      suspend_secs,
                           int      off_secs,
                           gboolean verbose);

G_END_DECLS

#endif /* __GS_DPMS_H */
