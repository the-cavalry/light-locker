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

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gconf/gconf-client.h>

#include "gs-prefs.h"

static void gs_prefs_class_init (GSPrefsClass *klass);
static void gs_prefs_init       (GSPrefs      *prefs);
static void gs_prefs_finalize   (GObject      *object);

#define GNOME_LOCKDOWN_DIR "/desktop/gnome/lockdown"
#define KEY_LOCK_DISABLE          GNOME_LOCKDOWN_DIR "/disable_lock_screen"
#define KEY_USER_SWITCH_DISABLE   GNOME_LOCKDOWN_DIR "/disable_user_switching"

#define KEY_DIR            "/apps/gnome-screensaver"
#define GNOME_SESSION_DIR  "/desktop/gnome/session"
#define KEY_IDLE_ACTIVATION_ENABLED         KEY_DIR "/idle_activation_enabled"
#define KEY_LOCK_ENABLED   KEY_DIR "/lock_enabled"
#define KEY_MODE           KEY_DIR "/mode"
#define KEY_ACTIVATE_DELAY GNOME_SESSION_DIR "/idle_delay"
#define KEY_POWER_DELAY    KEY_DIR "/power_management_delay"
#define KEY_LOCK_DELAY     KEY_DIR "/lock_delay"
#define KEY_CYCLE_DELAY    KEY_DIR "/cycle_delay"
#define KEY_THEMES         KEY_DIR "/themes"
#define KEY_USER_SWITCH_ENABLED KEY_DIR "/user_switch_enabled"
#define KEY_LOGOUT_ENABLED KEY_DIR "/logout_enabled"
#define KEY_LOGOUT_DELAY   KEY_DIR "/logout_delay"
#define KEY_LOGOUT_COMMAND KEY_DIR "/logout_command"
#define KEY_KEYBOARD_ENABLED KEY_DIR "/embedded_keyboard_enabled"
#define KEY_KEYBOARD_COMMAND KEY_DIR "/embedded_keyboard_command"
#define KEY_STATUS_MESSAGE_ENABLED   KEY_DIR "/status_message_enabled"

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
       { 0, NULL }
};

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSPrefs, gs_prefs, G_TYPE_OBJECT)

static void
gs_prefs_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
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
_gs_prefs_set_timeout (GSPrefs *prefs,
                       int      value)
{
        if (value < 1)
                value = 10;

        /* pick a reasonable large number for the
           upper bound */
        if (value > 480)
                value = 480;

        prefs->timeout = value * 60000;
}

static void
_gs_prefs_set_power_timeout (GSPrefs *prefs,
                             int      value)
{
        if (value < 1)
                value = 60;

        /* pick a reasonable large number for the
           upper bound */
        if (value > 480)
                value = 480;

        /* this value is in seconds - others are in minutes */
        prefs->power_timeout = value * 1000;
}

static void
_gs_prefs_set_lock_timeout (GSPrefs *prefs,
                            int      value)
{
        if (value < 0)
                value = 0;

        /* pick a reasonable large number for the
           upper bound */
        if (value > 480)
                value = 480;

        prefs->lock_timeout = value * 60000;
}

static void
_gs_prefs_set_cycle_timeout (GSPrefs *prefs,
                             int      value)
{
        if (value < 1)
                value = 1;

        /* pick a reasonable large number for the
           upper bound */
        if (value > 480)
                value = 480;

        prefs->cycle = value * 60000;
}

static void
_gs_prefs_set_mode (GSPrefs    *prefs,
                    const char *value)
{
        int mode;

        if (value && gconf_string_to_enum (mode_enum_map, value, &mode))
                prefs->mode = mode;
        else
                prefs->mode = GS_MODE_BLANK_ONLY;
}

static void
_gs_prefs_set_themes (GSPrefs *prefs,
                      GSList  *list)
{
        if (prefs->themes) {
                g_slist_foreach (prefs->themes, (GFunc)g_free, NULL);
                g_slist_free (prefs->themes);
        }

        /* take ownership of the list */
        prefs->themes = list;
}

static void
_gs_prefs_set_idle_activation_enabled (GSPrefs *prefs,
                                       gboolean value)
{
        prefs->idle_activation_enabled = value;
}

static void
_gs_prefs_set_lock_enabled (GSPrefs *prefs,
                            gboolean value)
{
        prefs->lock_enabled = value;
}

static void
_gs_prefs_set_lock_disabled (GSPrefs *prefs,
                             gboolean value)
{
        prefs->lock_disabled = value;
}

static void
_gs_prefs_set_user_switch_disabled (GSPrefs *prefs,
                                    gboolean value)
{
        prefs->user_switch_disabled = value;
}

static void
_gs_prefs_set_keyboard_enabled (GSPrefs *prefs,
                                gboolean value)
{
        prefs->keyboard_enabled = value;
}

static void
_gs_prefs_set_keyboard_command (GSPrefs    *prefs,
                                const char *value)
{
        g_free (prefs->keyboard_command);
        prefs->keyboard_command = NULL;

       if (value) {
               /* FIXME: check command */

               prefs->keyboard_command = g_strdup (value);
        }
}

static void
_gs_prefs_set_status_message_enabled (GSPrefs  *prefs,
                                      gboolean  enabled)
{
        prefs->status_message_enabled = enabled;
}

static void
_gs_prefs_set_logout_enabled (GSPrefs *prefs,
                              gboolean value)
{
        prefs->logout_enabled = value;
}

static void
_gs_prefs_set_logout_command (GSPrefs    *prefs,
                              const char *value)
{
        g_free (prefs->logout_command);
        prefs->logout_command = NULL;

       if (value) {
               /* FIXME: check command */

               prefs->logout_command = g_strdup (value);
        }
}

static void
_gs_prefs_set_logout_timeout (GSPrefs *prefs,
                              int      value)
{
        if (value < 0)
                value = 0;

        /* pick a reasonable large number for the
           upper bound */
        if (value > 480)
                value = 480;

        prefs->logout_timeout = value * 60000;
}

static void
_gs_prefs_set_user_switch_enabled (GSPrefs *prefs,
                                   gboolean value)
{
        prefs->user_switch_enabled = value;
}

static void
key_error_and_free (const char *key,
                    GError     *error)
{
        g_warning ("Error retrieving configuration key '%s': %s", key, error->message);
        g_error_free (error);
        error = NULL;
}

static void
gs_prefs_load_from_gconf (GSPrefs *prefs)
{
        glong    value;
        gboolean bvalue;
        char    *string;
        GSList  *list;
        GError  *error;

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_IDLE_ACTIVATION_ENABLED, &error);
        if (! error) {
                _gs_prefs_set_idle_activation_enabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_IDLE_ACTIVATION_ENABLED, error);
        }

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_LOCK_ENABLED, &error);
        if (! error) {
                _gs_prefs_set_lock_enabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_LOCK_ENABLED, error);
        }

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_LOCK_DISABLE, &error);
        if (! error) {
                _gs_prefs_set_lock_disabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_LOCK_DISABLE, error);
        }

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_USER_SWITCH_DISABLE, &error);
        if (! error) {
                _gs_prefs_set_user_switch_disabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_USER_SWITCH_DISABLE, error);
        }

        error = NULL;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_ACTIVATE_DELAY, &error);
        if (! error) {
                _gs_prefs_set_timeout (prefs, value);
        } else {
                key_error_and_free (KEY_ACTIVATE_DELAY, error);
        }

        error = NULL;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_POWER_DELAY, &error);
        if (! error) {
                _gs_prefs_set_power_timeout (prefs, value);
        } else {
                key_error_and_free (KEY_POWER_DELAY, error);
        }

        error = NULL;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_LOCK_DELAY, &error);
        if (! error) {
                _gs_prefs_set_lock_timeout (prefs, value);
        } else {
                key_error_and_free (KEY_LOCK_DELAY, error);
        }

        error = NULL;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_CYCLE_DELAY, &error);
        if (! error) {
                _gs_prefs_set_cycle_timeout (prefs, value);
        } else {
                key_error_and_free (KEY_CYCLE_DELAY, error);
        }

        error = NULL;
        string = gconf_client_get_string (prefs->priv->gconf_client, KEY_MODE, &error);
        if (! error) {
                _gs_prefs_set_mode (prefs, string);
        } else {
                key_error_and_free (KEY_MODE, error);
        }
        g_free (string);

        error = NULL;
        list = gconf_client_get_list (prefs->priv->gconf_client,
                                      KEY_THEMES,
                                      GCONF_VALUE_STRING,
                                      &error);
        if (! error) {
                _gs_prefs_set_themes (prefs, list);
        } else {
                key_error_and_free (KEY_THEMES, error);
        }

        /* Embedded keyboard options */

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_KEYBOARD_ENABLED, &error);
        if (! error) {
                _gs_prefs_set_keyboard_enabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_KEYBOARD_ENABLED, error);
        }

        error = NULL;
        string = gconf_client_get_string (prefs->priv->gconf_client, KEY_KEYBOARD_COMMAND, &error);
        if (! error) {
                _gs_prefs_set_keyboard_command (prefs, string);
        } else {
                key_error_and_free (KEY_KEYBOARD_COMMAND, error);
        }
        g_free (string);

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_STATUS_MESSAGE_ENABLED, &error);
        if (! error) {
                _gs_prefs_set_status_message_enabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_STATUS_MESSAGE_ENABLED, error);
        }

        /* Logout options */

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_LOGOUT_ENABLED, &error);
        if (! error) {
                _gs_prefs_set_logout_enabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_LOGOUT_ENABLED, error);
        }

        error = NULL;
        string = gconf_client_get_string (prefs->priv->gconf_client, KEY_LOGOUT_COMMAND, &error);
        if (! error) {
                _gs_prefs_set_logout_command (prefs, string);
        } else {
                key_error_and_free (KEY_LOGOUT_COMMAND, error);
        }
        g_free (string);

        error = NULL;
        value = gconf_client_get_int (prefs->priv->gconf_client, KEY_LOGOUT_DELAY, &error);
        if (! error) {
                _gs_prefs_set_logout_timeout (prefs, value);
        } else {
                key_error_and_free (KEY_LOGOUT_DELAY, error);
        }

        /* User switching options */

        error = NULL;
        bvalue = gconf_client_get_bool (prefs->priv->gconf_client, KEY_USER_SWITCH_ENABLED, &error);
        if (! error) {
                _gs_prefs_set_user_switch_enabled (prefs, bvalue);
        } else {
                key_error_and_free (KEY_USER_SWITCH_ENABLED, error);
        }
}

static void
invalid_type_warning (const char *type)
{
        g_warning ("Error retrieving configuration key '%s': Invalid type",
                   type);
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

        if (! g_str_has_prefix (key, KEY_DIR) && ! g_str_has_prefix (key, GNOME_LOCKDOWN_DIR))
                return;

        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_MODE) == 0) {

                if (value->type == GCONF_VALUE_STRING) {
                        const char *str;

                        str = gconf_value_get_string (value);
                        _gs_prefs_set_mode (prefs, str);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

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
                        GSList *new_list;

                        changed = TRUE;

                        new_list = NULL;
                        for (l = list; l; l = l->next) {
                                char *s;

                                s = gconf_value_to_string (l->data);

                                new_list = g_slist_prepend (new_list, g_strdup (s));

                                g_free (s);
                        }

                        new_list = g_slist_reverse (new_list);

                        _gs_prefs_set_themes (prefs, new_list);

                }

        } else if (strcmp (key, KEY_ACTIVATE_DELAY) == 0) {

                if (value->type == GCONF_VALUE_INT) {
                        int delay;

                        delay = gconf_value_get_int (value);
                        _gs_prefs_set_timeout (prefs, delay);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_POWER_DELAY) == 0) {

                if (value->type == GCONF_VALUE_INT) {
                        int delay;

                        delay = gconf_value_get_int (value);
                        _gs_prefs_set_power_timeout (prefs, delay);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_LOCK_DELAY) == 0) {

                if (value->type == GCONF_VALUE_INT) {
                        int delay;

                        delay = gconf_value_get_int (value);
                        _gs_prefs_set_lock_timeout (prefs, delay);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_IDLE_ACTIVATION_ENABLED) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        _gs_prefs_set_idle_activation_enabled (prefs, enabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_LOCK_ENABLED) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        _gs_prefs_set_lock_enabled (prefs, enabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_LOCK_DISABLE) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean disabled;

                        disabled = gconf_value_get_bool (value);
                        _gs_prefs_set_lock_disabled (prefs, disabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_USER_SWITCH_DISABLE) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean disabled;

                        disabled = gconf_value_get_bool (value);
                        _gs_prefs_set_user_switch_disabled (prefs, disabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_CYCLE_DELAY) == 0) {

                if (value->type == GCONF_VALUE_INT) {
                        int delay;

                        delay = gconf_value_get_int (value);
                        _gs_prefs_set_cycle_timeout (prefs, delay);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_KEYBOARD_ENABLED) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        _gs_prefs_set_keyboard_enabled (prefs, enabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_KEYBOARD_COMMAND) == 0) {

                if (value->type == GCONF_VALUE_STRING) {
                        const char *command;

                        command = gconf_value_get_string (value);
                        _gs_prefs_set_keyboard_command (prefs, command);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_STATUS_MESSAGE_ENABLED) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        _gs_prefs_set_status_message_enabled (prefs, enabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_LOGOUT_ENABLED) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        _gs_prefs_set_logout_enabled (prefs, enabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_LOGOUT_DELAY) == 0) {

                if (value->type == GCONF_VALUE_INT) {
                        int delay;

                        delay = gconf_value_get_int (value);
                        _gs_prefs_set_logout_timeout (prefs, delay);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_LOGOUT_COMMAND) == 0) {

                if (value->type == GCONF_VALUE_STRING) {
                        const char *command;

                        command = gconf_value_get_string (value);
                        _gs_prefs_set_logout_command (prefs, command);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else if (strcmp (key, KEY_USER_SWITCH_ENABLED) == 0) {

                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        _gs_prefs_set_user_switch_enabled (prefs, enabled);

                        changed = TRUE;
                } else {
                        invalid_type_warning (key);
                }

        } else {
                g_warning ("Config key not handled: %s", key);
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

        prefs->idle_activation_enabled = TRUE;
        prefs->lock_enabled            = TRUE;
        prefs->lock_disabled           = FALSE;
        prefs->logout_enabled          = FALSE;
        prefs->user_switch_enabled     = FALSE;

        prefs->timeout                 = 600000;
        prefs->power_timeout           = 60000;
        prefs->lock_timeout            = 0;
        prefs->logout_timeout          = 14400000;
        prefs->cycle                   = 600000;

        prefs->mode                    = GS_MODE_SINGLE;

        /* GConf setup */
        gconf_client_add_dir (prefs->priv->gconf_client,
                              KEY_DIR,
                              GCONF_CLIENT_PRELOAD_NONE, NULL);
        gconf_client_add_dir (prefs->priv->gconf_client,
                              GNOME_LOCKDOWN_DIR,
                              GCONF_CLIENT_PRELOAD_NONE, NULL);
        gconf_client_add_dir (prefs->priv->gconf_client,
                              GNOME_SESSION_DIR,
                              GCONF_CLIENT_PRELOAD_NONE, NULL);


        gconf_client_notify_add (prefs->priv->gconf_client,
                                 KEY_DIR,
                                 (GConfClientNotifyFunc)key_changed_cb,
                                 prefs,
                                 NULL, NULL);
        gconf_client_notify_add (prefs->priv->gconf_client,
                                 GNOME_LOCKDOWN_DIR,
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

        g_free (prefs->logout_command);
        g_free (prefs->keyboard_command);

        G_OBJECT_CLASS (gs_prefs_parent_class)->finalize (object);
}

GSPrefs *
gs_prefs_new (void)
{
        GObject *prefs;

        prefs = g_object_new (GS_TYPE_PREFS, NULL);

        return GS_PREFS (prefs);
}
