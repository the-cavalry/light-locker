/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#ifdef HAVE_DPMS_EXTENSION
#include <X11/Xproto.h>			/* for CARD16 */
#include <X11/extensions/dpms.h>
#include <X11/extensions/dpmsstr.h>
#endif

#include "gs-power.h"

static void     gs_power_class_init (GSPowerClass *klass);
static void     gs_power_init       (GSPower      *power);
static void     gs_power_finalize   (GObject        *object);

#define GS_POWER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_POWER, GSPowerPrivate))

struct GSPowerPrivate
{
        gboolean       verbose;

        gboolean       enabled;
        gboolean       active;

        guint          standby_timeout;
        guint          suspend_timeout;
        guint          off_timeout;

        GSPowerMode    mode;

        guint          timer_id;
};

enum {
        CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0
};

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSPower, gs_power, G_TYPE_OBJECT);

/* the following function is derived from
   xscreensaver Copyright (C) Jamie Zawinski
*/
static void
x11_sync_server_dpms_settings (Display *dpy,
                               gboolean enabled,
                               int      standby_secs,
                               int      suspend_secs,
                               int      off_secs,
                               gboolean verbose)
{
#ifdef HAVE_DPMS_EXTENSION

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

        if (! DPMSGetTimeouts (dpy, &o_standby, &o_suspend, &o_off)) {
                if (verbose)
                        g_message ("unable to get DPMS timeouts.");
                return;
        }

        if (o_standby != standby_secs ||
            o_suspend != suspend_secs ||
            o_off != off_secs) {
                if (! DPMSSetTimeouts (dpy, standby_secs, suspend_secs, off_secs)) {
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

#ifdef HAVE_DPMS_EXTENSION

static GSPowerMode
x11_get_mode (GSPower *power)
{
        GSPowerMode result;
        int         event_number, error_number;
        BOOL        onoff = FALSE;
        CARD16      state;

        if (! DPMSQueryExtension (GDK_DISPLAY (), &event_number, &error_number))
                /* Server doesn't know -- assume the monitor is on. */
                result = GS_POWER_MODE_ON;

        else if (! DPMSCapable (GDK_DISPLAY ()))
                /* Server says the monitor doesn't do power management -- so it's on. */
                result = GS_POWER_MODE_ON;

        else {
                DPMSInfo (GDK_DISPLAY (), &state, &onoff);
                if (! onoff)
                        /* Server says DPMS is disabled -- so the monitor is on. */
                        result = GS_POWER_MODE_ON;
                else
                        switch (state) {
                        case DPMSModeOn:
                                result = GS_POWER_MODE_ON;
                                break;
                        case DPMSModeStandby:
                                result = GS_POWER_MODE_STANDBY;
                                break;
                        case DPMSModeSuspend:
                                result = GS_POWER_MODE_SUSPEND;
                                break;
                        case DPMSModeOff:
                                result = GS_POWER_MODE_OFF;
                                break;
                        default:
                                result = GS_POWER_MODE_ON;
                                break;
                        }
        }

        return result;
}

static void
x11_set_mode (GSPower    *power,
              GSPowerMode mode)
{
        GSPowerMode current_mode;
        CARD16      state;

        current_mode = x11_get_mode (power);

        switch (current_mode) {
        case GS_POWER_MODE_ON:
                state = DPMSModeOn;
                break;
        case GS_POWER_MODE_STANDBY:
                state = DPMSModeStandby;
                break;
        case GS_POWER_MODE_SUSPEND:
                state = DPMSModeSuspend;
                break;
        case GS_POWER_MODE_OFF:
                state = DPMSModeOff;
                break;
        default:
                state = DPMSModeOn;
                break;
        }

        if (current_mode != GS_POWER_MODE_ON) {
                DPMSForceLevel (GDK_DISPLAY (), state);
                XSync (GDK_DISPLAY (), FALSE);
        }
}

#else  /* HAVE_DPMS_EXTENSION */

static GSPowerMode
x11_get_mode (GSPower *power) 
{
        return GS_POWER_MODE_ON; 
}

static void
x11_set_mode (GSPower     *power,
              GSPowerMode *mode)
{
        return; 
}

#endif /* !HAVE_DPMS_EXTENSION */

static void
sync_settings (GSPower *power)
{
        gboolean permitted;

        /* to use power management it has to be
           allowed by policy (ie. enabled) and
           be requested at this particular time (ie. active)
        */

        /* FIXME: should we require the session to be on console? */

        permitted = power->priv->enabled && power->priv->active;

        x11_sync_server_dpms_settings (GDK_DISPLAY (),
                                       permitted,
                                       power->priv->standby_timeout / 1000,
                                       power->priv->suspend_timeout / 1000,
                                       power->priv->off_timeout / 1000,
                                       power->priv->verbose);
}

gboolean
gs_power_get_enabled (GSPower *power)
{
        g_return_val_if_fail (GS_IS_POWER (power), FALSE);

        return power->priv->enabled;
}

void
gs_power_set_enabled (GSPower *power,
                      gboolean enabled)
{
        g_return_if_fail (GS_IS_POWER (power));

        if (power->priv->enabled != enabled) {
                power->priv->enabled = enabled;

                sync_settings (power);
        }
}

gboolean
gs_power_get_active (GSPower *power)
{
        g_return_val_if_fail (GS_IS_POWER (power), FALSE);

        return power->priv->active;
}

void
gs_power_set_active (GSPower *power,
                     gboolean active)
{
        g_return_if_fail (GS_IS_POWER (power));

        if (power->priv->active != active) {
                power->priv->active = active;

                sync_settings (power);
        }
}

void
gs_power_set_timeouts (GSPower   *power,
                       guint      standby,
                       guint      suspend,
                       guint      off)
{
        g_return_if_fail (GS_IS_POWER (power));

        power->priv->standby_timeout = standby;
        power->priv->suspend_timeout = suspend;
        power->priv->off_timeout     = off;

        sync_settings (power);
}

void
gs_power_set_mode (GSPower    *power,
                   GSPowerMode mode)
{
        g_return_if_fail (GS_IS_POWER (power));

        x11_set_mode (power, mode);
}

GSPowerMode
gs_power_get_mode (GSPower *power)
{
        GSPowerMode mode;

        mode = x11_get_mode (power);

        return mode;
}

static void
gs_power_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
        GSPower *self;

        self = GS_POWER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_power_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
        GSPower *self;

        self = GS_POWER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_power_class_init (GSPowerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize     = gs_power_finalize;
        object_class->get_property = gs_power_get_property;
        object_class->set_property = gs_power_set_property;

        signals [CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSPowerClass, changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1, G_TYPE_INT);

        g_type_class_add_private (klass, sizeof (GSPowerPrivate));
}

static gboolean
poll_power_mode (GSPower *power)
{
        GSPowerMode mode;

#ifndef HAVE_DPMS_EXTENSION
        return FALSE;
#endif

        mode = x11_get_mode (power);
        if (mode != power->priv->mode) {
                power->priv->mode = mode;

                g_signal_emit (power,
                               signals [CHANGED],
                               0,
                               mode);
        }

        /* FIXME: check that we are on console? */

        return TRUE;
}

static void
gs_power_init (GSPower *power)
{
        power->priv = GS_POWER_GET_PRIVATE (power);

        /* FIXME: for testing */
        power->priv->verbose = TRUE;

        power->priv->timer_id = g_timeout_add (500, (GSourceFunc)poll_power_mode, power);
}

static void
gs_power_finalize (GObject *object)
{
        GSPower *power;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_POWER (object));

        power = GS_POWER (object);

        g_return_if_fail (power->priv != NULL);

        if (power->priv->timer_id != 0) {
                g_source_remove (power->priv->timer_id);
                power->priv->timer_id = 0;
        }

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GSPower *
gs_power_new (void)
{
        GSPower *power;

        power = g_object_new (GS_TYPE_POWER, NULL);

        return GS_POWER (power);
}
