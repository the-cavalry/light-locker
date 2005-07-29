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

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gconf/gconf-client.h>

#include "gs-prefs.h"

static void gs_prefs_class_init (GSPrefsClass *klass);
static void gs_prefs_init       (GSPrefs      *prefs);
static void gs_prefs_finalize   (GObject      *object);

#define KEY_DIR            "/apps/gnome-screensaver"
#define KEY_LOCK           KEY_DIR "/lock"
#define KEY_MODE           KEY_DIR "/mode"
#define KEY_BLANK_DELAY    KEY_DIR "/blank_delay"
#define KEY_LOCK_DELAY     KEY_DIR "/lock_delay"
#define KEY_CYCLE_DELAY    KEY_DIR "/cycle_delay"
#define KEY_DPMS_ENABLED   KEY_DIR "/dpms_enabled"
#define KEY_DPMS_STANDBY   KEY_DIR "/dpms_standby"
#define KEY_DPMS_SUSPEND   KEY_DIR "/dpms_suspend"
#define KEY_DPMS_OFF       KEY_DIR "/dpms_off"
#define KEY_THEMES         KEY_DIR "/themes"
#define KEY_LOGOUT_ENABLED KEY_DIR "/logout_enabled"
#define KEY_LOGOUT_DELAY   KEY_DIR "/logout_delay"

#define GS_PREFS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_PREFS, GSPrefsPrivate))

struct GSPrefsPrivate
{
        GConfClient *gconf_client;
};

enum {
        CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0
};

static GConfEnumStringPair mode_enum_map [] = {
       { GS_MODE_BLANK_ONLY,       "blank-only" },
       { GS_MODE_RANDOM,           "random"     },
       { GS_MODE_SINGLE,           "single"     },
       { GS_MODE_DONT_BLANK,       "disabled"   },
       { 0, NULL }
};

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSPrefs, gs_prefs, G_TYPE_OBJECT);

static void
gs_prefs_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
        GSPrefs *self;

        self = GS_PREFS (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_prefs_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
        GSPrefs *self;

        self = GS_PREFS (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_prefs_class_init (GSPrefsClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize     = gs_prefs_finalize;
        object_class->get_property = gs_prefs_get_property;
        object_class->set_property = gs_prefs_set_property;


        signals [CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSPrefsClass, changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_type_class_add_private (klass, sizeof (GSPrefsPrivate));

}

static void
gs_prefs_load_from_gconf (GSPrefs *prefs)
{
        glong value;
        char *string;
        int   mode;

        prefs->lock = gconf_client_get_bool (prefs->priv->gconf_client, KEY_LOCK, NULL);

        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_BLANK_DELAY, NULL);
        if (value < 1)
                value = 10;
        prefs->timeout = value * 60000;

        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_LOCK_DELAY, NULL);
        if (value < 0)
                value = 0;
        prefs->lock_timeout = value * 60000;

        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_CYCLE_DELAY, NULL);
        if (value < 1)
                value = 60;
        prefs->cycle = value * 60000;

        string = gconf_client_get_string (prefs->priv->gconf_client, KEY_MODE, NULL);
	if (string && gconf_string_to_enum (mode_enum_map, string, &mode))
                prefs->mode = mode;
        else
                prefs->mode = GS_MODE_BLANK_ONLY;
        g_free (string);

        if (prefs->themes) {
                g_slist_foreach (prefs->themes, (GFunc)g_free, NULL);
                g_slist_free (prefs->themes);
        }

        prefs->themes = gconf_client_get_list (prefs->priv->gconf_client,
                                               KEY_THEMES, GCONF_VALUE_STRING, NULL);

        /* DPMS options */

        prefs->dpms_enabled = gconf_client_get_bool (prefs->priv->gconf_client, KEY_DPMS_ENABLED, NULL);
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_DPMS_STANDBY, NULL);
        if (value < 0)
                value = 0;
        prefs->dpms_standby = value * 60000;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_DPMS_SUSPEND, NULL);
        if (value < 0)
                value = 0;
        prefs->dpms_suspend = value * 60000;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_DPMS_OFF, NULL);
        if (value < 0)
                value = 0;
        prefs->dpms_off = value * 60000;


        /* Logout options */

        prefs->logout_enabled = gconf_client_get_bool (prefs->priv->gconf_client, KEY_LOGOUT_ENABLED, NULL);
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_LOGOUT_DELAY, NULL);
        if (value < 0)
                value = 0;
        prefs->logout_timeout = value * 60000;
}

static void
key_changed_cb (GConfClient *client,
                guint        cnxn_id,
                GConfEntry  *entry,
                GSPrefs     *prefs)
{
        gboolean    changed = FALSE;
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);

        if (! g_str_has_prefix (key, KEY_DIR))
                return;

        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_MODE) == 0) {
                const char *str;
                int         mode;

                str = gconf_value_get_string (value);

                if (str && gconf_string_to_enum (mode_enum_map, str, &mode))
                        prefs->mode = mode;
                else
                        prefs->mode = GS_MODE_BLANK_ONLY;

                changed = TRUE;

        } else if (strcmp (key, KEY_THEMES) == 0) {
                GSList *list = NULL;

                if (value == NULL
                    || value->type != GCONF_VALUE_LIST) {
                        return;
                }
                            
                list = gconf_value_get_list (value);

                if (list
                    && gconf_value_get_list_type (value) == GCONF_VALUE_STRING) {
                        GSList *l;

                        changed = TRUE;

                        if (prefs->themes) {
                                g_slist_foreach (prefs->themes, (GFunc)g_free, NULL);
                                g_slist_free (prefs->themes);
                                prefs->themes = NULL;
                        }

                        for (l = list; l; l = l->next) {
                                char *s;

                                s = gconf_value_to_string (l->data);

                                prefs->themes = g_slist_append (prefs->themes, g_strdup (s));

                                g_free (s);
                        }
                }
        } else if (strcmp (key, KEY_BLANK_DELAY) == 0) {
                int delay;

                delay = gconf_value_get_int (value);
                if (delay < 1)
                        delay = 1;
                prefs->timeout = delay * 60000;
                changed = TRUE;
        } else if (strcmp (key, KEY_DPMS_ENABLED) == 0) {
                gboolean enabled;

                enabled = gconf_value_get_bool (value);
                prefs->dpms_enabled = enabled;
                changed = TRUE;
        } else if (strcmp (key, KEY_DPMS_STANDBY) == 0) {
                int timeout;

                timeout = gconf_value_get_int (value);
                if (timeout < 1)
                        timeout = 1;
                prefs->dpms_standby = timeout * 60000;
                changed = TRUE;
        } else if (strcmp (key, KEY_DPMS_SUSPEND) == 0) {
                int timeout;

                timeout = gconf_value_get_int (value);
                if (timeout < 1)
                        timeout = 1;
                prefs->dpms_suspend = timeout * 60000;
                changed = TRUE;
        } else if (strcmp (key, KEY_DPMS_OFF) == 0) {
                int timeout;

                timeout = gconf_value_get_int (value);
                if (timeout < 1)
                        timeout = 1;
                prefs->dpms_off = timeout * 60000;
                changed = TRUE;
        } else if (strcmp (key, KEY_LOGOUT_ENABLED) == 0) {
                gboolean enabled;

                enabled = gconf_value_get_bool (value);
                prefs->logout_enabled = enabled;
                changed = TRUE;
        } else if (strcmp (key, KEY_LOGOUT_DELAY) == 0) {
                int delay;

                delay = gconf_value_get_int (value);
                if (delay < 1)
                        delay = 1;
                prefs->logout_timeout = delay * 60000;
                changed = TRUE;
        } else {
                g_message ("Config key not handled: %s", key);
        }

        if (changed && prefs) {
                g_signal_emit (prefs, signals [CHANGED], 0);
        }
}

static void
gs_prefs_init (GSPrefs *prefs)
{
        prefs->priv = GS_PREFS_GET_PRIVATE (prefs);

        prefs->priv->gconf_client      = gconf_client_get_default ();

        prefs->use_sgi_saver_extension = FALSE;
        prefs->use_mit_saver_extension = FALSE;
        prefs->use_xidle_extension     = FALSE;
        prefs->use_proc_interrupts     = FALSE;

        prefs->pointer_timeout         = 5000;

        prefs->lock                    = TRUE;

        prefs->verbose                 = FALSE;
        prefs->debug                   = FALSE;

        prefs->timeout                 = 600000;
        prefs->lock_timeout            = 0;
        prefs->cycle                   = 600000;

        prefs->dpms_enabled            = TRUE;
        prefs->dpms_standby            = 7200000;
        prefs->dpms_suspend            = 7200000;
        prefs->dpms_off                = 14400000;

        prefs->mode                    = GS_MODE_SINGLE;

        /* FIXME: for testing only */
        prefs->themes = g_slist_append (prefs->themes, g_strdup ("popsquares"));

        /* GConf setup */
        gconf_client_add_dir (prefs->priv->gconf_client,
                              KEY_DIR,
                              GCONF_CLIENT_PRELOAD_NONE, NULL);

        gconf_client_notify_add (prefs->priv->gconf_client,
                                 KEY_DIR,
                                 (GConfClientNotifyFunc)key_changed_cb,
                                 prefs,
                                 NULL, NULL);

        gs_prefs_load_from_gconf (prefs);
}

static void
gs_prefs_finalize (GObject *object)
{
        GSPrefs *prefs;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_PREFS (object));

        prefs = GS_PREFS (object);

        g_return_if_fail (prefs->priv != NULL);

        if (prefs->priv->gconf_client) {
                g_object_unref (prefs->priv->gconf_client);
                prefs->priv->gconf_client = NULL;
        }

        if (prefs->themes) {
                g_slist_foreach (prefs->themes, (GFunc)g_free, NULL);
                g_slist_free (prefs->themes);
        }

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GSPrefs *
gs_prefs_new (void)
{
        GObject *prefs;

        prefs = g_object_new (GS_TYPE_PREFS, NULL);

        return GS_PREFS (prefs);
}
