/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GS_PREFS_H
#define __GS_PREFS_H

G_BEGIN_DECLS

#define GS_TYPE_PREFS         (gs_prefs_get_type ())
#define GS_PREFS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_PREFS, GSPrefs))
#define GS_PREFS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_PREFS, GSPrefsClass))
#define GS_IS_PREFS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_PREFS))
#define GS_IS_PREFS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_PREFS))
#define GS_PREFS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_PREFS, GSPrefsClass))

typedef enum {
        GS_MODE_BLANK_ONLY,
        GS_MODE_RANDOM,
        GS_MODE_SINGLE
} GSSaverMode;

typedef struct GSPrefsPrivate GSPrefsPrivate;

typedef struct
{
        GObject          parent;

        GSPrefsPrivate  *priv;

        guint            idle_activation_enabled : 1; /* whether to activate when idle */
        guint            lock_enabled : 1;              /* whether to lock when active */
        guint            logout_enabled : 1;    /* Whether to offer the logout option */
        guint            lock_disabled : 1;     /* Whether locking the system is disabled */
        guint            user_switch_disabled : 1;      /* Whether user switching is disabled */
        guint            user_switch_enabled : 1;       /* Whether to offer the user switch option */
        guint            keyboard_enabled : 1;  /* Whether to try to embed a keyboard */
        guint            status_message_enabled : 1; /* show the status message in the lock */

        guint            power_timeout;         /* how much idle time before power management */
        guint            timeout;               /* how much idle time before activation */
        guint            lock_timeout;          /* how long after activation locking starts */
        guint            logout_timeout;        /* how long until the logout option appears */
        guint            cycle;                 /* how long each theme should run */

        char            *logout_command;        /* command to use to logout */
        char            *keyboard_command;      /* command to use to embed a keyboard */

        GSList          *themes;                /* the screensaver themes to run */
        GSSaverMode      mode;                  /* theme selection mode */
} GSPrefs;

typedef struct
{
        GObjectClass     parent_class;

        void            (* changed)        (GSPrefs *prefs);
} GSPrefsClass;

GType       gs_prefs_get_type        (void);
GSPrefs   * gs_prefs_new             (void);
void        gs_prefs_load            (GSPrefs *prefs);

G_END_DECLS

#endif /* __GS_PREFS_H */
