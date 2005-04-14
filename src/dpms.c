/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * dpms.c --- syncing the X Display Power Management values
 *
 * xscreensaver, Copyright (c) 2001 Jamie Zawinski <jwz@jwz.org>
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

#include <stdio.h>
#include <glib.h>
#include <gdk/gdkx.h>

#include "dpms.h"

#ifdef HAVE_DPMS_EXTENSION

# include <X11/Xproto.h>
# include <X11/extensions/dpms.h>
# include <X11/extensions/dpmsstr.h>

/* Why this crap is not in a header file somewhere, I have no idea.  Losers!
 */
extern Bool   DPMSQueryExtension (Display *dpy,
                                  int     *event_ret,
                                  int     *err_ret);
extern Bool   DPMSCapable        (Display *dpy);
extern Status DPMSInfo           (Display *dpy,
                                  CARD16  *power_level,
                                  BOOL    *state);
extern Status DPMSSetTimeouts    (Display *dpy,
                                  CARD16   standby,
                                  CARD16   suspend,
                                  CARD16   off);
extern Bool   DPMSGetTimeouts    (Display *dpy,
                                  CARD16  *standby,
                                  CARD16  *suspend,
                                  CARD16  *off);
extern Status DPMSEnable         (Display *dpy);
extern Status DPMSDisable        (Display *dpy);

#endif /* HAVE_DPMS_EXTENSION */


/* This file doesn't need the Xt headers, so stub these types out... */
#undef XtPointer
#define XtAppContext void*
#define XrmDatabase  void*
#define XtIntervalId void*
#define XtPointer    void*
#define Widget       void*

void
sync_server_dpms_settings (Display *dpy,
                           gboolean enabled,
                           int      standby_secs,
                           int      suspend_secs,
                           int      off_secs,
                           gboolean verbose)
{
# ifdef HAVE_DPMS_EXTENSION

        int      event = 0, error = 0;
        BOOL     o_enabled = FALSE;
        CARD16   o_power = 0;
        CARD16   o_standby = 0, o_suspend = 0, o_off = 0;
        gboolean bogus = FALSE;

        if (standby_secs == 0 && suspend_secs == 0 && off_secs == 0)
                /* all zero implies "DPMS disabled" */
                enabled = FALSE;

        else if ((standby_secs != 0 && standby_secs < 10) ||
                 (suspend_secs != 0 && suspend_secs < 10) ||
                 (off_secs     != 0 && off_secs     < 10))
                /* any negative, or any positive-and-less-than-10-seconds, is crazy. */
                bogus = TRUE;

        if (bogus)
                enabled = FALSE;

        if (! DPMSQueryExtension (dpy, &event, &error)) {
                if (verbose)
                        g_message ("XDPMS extension not supported.");
                return;
        }

        if (! DPMSCapable (dpy)) {
                if (verbose)
                        g_message ("DPMS not supported.");
                return;
        }

        if (! DPMSInfo (dpy, &o_power, &o_enabled)) {
                if (verbose)
                        g_message ("unable to get DPMS state.");
                return;
        }

        if (o_enabled != enabled) {
                if (! (enabled ? DPMSEnable (dpy) : DPMSDisable (dpy))) {
                        if (verbose)
                                g_message ("unable to set DPMS state.");
                        return;
                }
                else if (verbose)
                        g_message ("turned DPMS %s.", enabled ? "on" : "off");
        }

        if (bogus) {
                if (verbose)
                        g_message ("not setting bogus DPMS timeouts: %d %d %d.",
                                   standby_secs, suspend_secs, off_secs);
                return;
        }

        if (!DPMSGetTimeouts (dpy, &o_standby, &o_suspend, &o_off)) {
                if (verbose)
                        g_message ("unable to get DPMS timeouts.");
                return;
        }

        if (o_standby != standby_secs ||
            o_suspend != suspend_secs ||
            o_off != off_secs) {
                if (!DPMSSetTimeouts (dpy, standby_secs, suspend_secs, off_secs)) {
                        if (verbose)
                                g_message ("unable to set DPMS timeouts.");
                        return;
                }
                else if (verbose)
                        g_message ("set DPMS timeouts: %d %d %d.", 
                                   standby_secs, suspend_secs, off_secs);
        }

# else  /* !HAVE_DPMS_EXTENSION */

        if (verbose)
                g_message ("DPMS support not compiled in.");

# endif /* HAVE_DPMS_EXTENSION */
}
