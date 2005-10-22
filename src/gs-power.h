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

#ifndef __GS_POWER_H
#define __GS_POWER_H

G_BEGIN_DECLS

#define GS_TYPE_POWER         (gs_power_get_type ())
#define GS_POWER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_POWER, GSPower))
#define GS_POWER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_POWER, GSPowerClass))
#define GS_IS_POWER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_POWER))
#define GS_IS_POWER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_POWER))
#define GS_POWER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_POWER, GSPowerClass))

typedef enum {
        GS_POWER_MODE_ON,
        GS_POWER_MODE_STANDBY,
        GS_POWER_MODE_SUSPEND,
        GS_POWER_MODE_OFF
} GSPowerMode;

typedef struct GSPowerPrivate GSPowerPrivate;

typedef struct
{
        GObject         parent;
        GSPowerPrivate *priv;
} GSPower;

typedef struct
{
        GObjectClass      parent_class;

        void              (* changed)        (GSPower    *power,
                                              GSPowerMode mode);
} GSPowerClass;

GType       gs_power_get_type (void);

GSPower   * gs_power_new              (void);

gboolean    gs_power_get_active       (GSPower    *power);
gboolean    gs_power_set_active       (GSPower    *power,
                                       gboolean    active);
gboolean    gs_power_get_enabled      (GSPower    *power);
void        gs_power_set_enabled      (GSPower    *power,
                                       gboolean    enabled);
void        gs_power_set_timeouts     (GSPower    *power,
                                       guint       standby,
                                       guint       suspend,
                                       guint       off);

/* Direct manipulation */
GSPowerMode gs_power_get_mode         (GSPower    *power);
void        gs_power_set_mode         (GSPower    *power,
                                       GSPowerMode mode);


#endif /* __GS_POWER_H */
