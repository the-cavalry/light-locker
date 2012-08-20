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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gdesktop-enums.h>

#include "gs-prefs.h"
#include "gs-debug.h"

static void gs_prefs_class_init (GSPrefsClass *klass);
static void gs_prefs_init       (GSPrefs      *prefs);
static void gs_prefs_finalize   (GObject      *object);

#define LOCKDOWN_SETTINGS_SCHEMA "org.gnome.desktop.lockdown"
#define KEY_LOCK_DISABLE          "disable-lock-screen"
#define KEY_USER_SWITCH_DISABLE   "disable-user-switching"

#define GS_SETTINGS_SCHEMA "org.gnome.desktop.screensaver"
#define KEY_IDLE_ACTIVATION_ENABLED         "idle-activation-enabled"
#define KEY_LOCK_ENABLED   "lock-enabled"
#define KEY_LOCK_DELAY     "lock-delay"
#define KEY_USER_SWITCH_ENABLED "user-switch-enabled"
#define KEY_LOGOUT_ENABLED "logout-enabled"
#define KEY_LOGOUT_DELAY   "logout-delay"
#define KEY_LOGOUT_COMMAND "logout-command"
#define KEY_KEYBOARD_ENABLED "embedded-keyboard-enabled"
#define KEY_KEYBOARD_COMMAND "embedded-keyboard-command"
#define KEY_STATUS_MESSAGE_ENABLED   "status-message-enabled"

#define GS_PREFS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_PREFS, GSPrefsPrivate))

struct GSPrefsPrivate
{
        GSettings *settings;
        GSettings *lockdown;
};

enum {
        CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0
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
_gs_prefs_set_lock_timeout (GSPrefs *prefs,
                            guint    value)
{
        /* prevent overflow when converting to milliseconds */
        if (value > G_MAXUINT / 1000) {
                value = G_MAXUINT / 1000;
        }

        prefs->lock_timeout = value * 1000;
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
                              guint    value)
{
        /* prevent overflow when converting to milliseconds */
        if (value > G_MAXUINT / 1000) {
                value = G_MAXUINT / 1000;
        }

        prefs->logout_timeout = value * 1000;
}

static void
_gs_prefs_set_user_switch_enabled (GSPrefs *prefs,
                                   gboolean value)
{
        prefs->user_switch_enabled = value;
}

static guint
_gs_settings_get_uint (GSettings  *settings,
                       const char *key)
{
  guint value;

  g_settings_get (settings, key, "u", &value);
  return value;
}

static void
gs_prefs_load_from_settings (GSPrefs *prefs)
{
        guint    uvalue;
        gboolean bvalue;
        char    *string;

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_IDLE_ACTIVATION_ENABLED);
        _gs_prefs_set_idle_activation_enabled (prefs, bvalue);

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_LOCK_ENABLED);
        _gs_prefs_set_lock_enabled (prefs, bvalue);

        uvalue = _gs_settings_get_uint (prefs->priv->settings, KEY_LOCK_DELAY);
        _gs_prefs_set_lock_timeout (prefs, uvalue);

        /* Embedded keyboard options */

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_KEYBOARD_ENABLED);
        _gs_prefs_set_keyboard_enabled (prefs, bvalue);

        string = g_settings_get_string (prefs->priv->settings, KEY_KEYBOARD_COMMAND);
        _gs_prefs_set_keyboard_command (prefs, string);
        g_free (string);

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_STATUS_MESSAGE_ENABLED);
        _gs_prefs_set_status_message_enabled (prefs, bvalue);

        /* Logout options */

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_LOGOUT_ENABLED);
        _gs_prefs_set_logout_enabled (prefs, bvalue);

        string = g_settings_get_string (prefs->priv->settings, KEY_LOGOUT_COMMAND);
        _gs_prefs_set_logout_command (prefs, string);
        g_free (string);

        uvalue = _gs_settings_get_uint (prefs->priv->settings, KEY_LOGOUT_DELAY);
        _gs_prefs_set_logout_timeout (prefs, uvalue);

        /* User switching options */

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_USER_SWITCH_ENABLED);
        _gs_prefs_set_user_switch_enabled (prefs, bvalue);

        /* Lockdown keys */

        bvalue = g_settings_get_boolean (prefs->priv->lockdown, KEY_LOCK_DISABLE);
        _gs_prefs_set_lock_disabled (prefs, bvalue);

        bvalue = g_settings_get_boolean (prefs->priv->lockdown, KEY_USER_SWITCH_DISABLE);
        _gs_prefs_set_user_switch_disabled (prefs, bvalue);

}

static void
key_changed_cb (GSettings   *settings,
                const gchar *key,
                GSPrefs     *prefs)
{
        if (strcmp (key, KEY_LOCK_DELAY) == 0) {

                guint delay;

                delay = _gs_settings_get_uint (settings, key);
                _gs_prefs_set_lock_timeout (prefs, delay);

        } else if (strcmp (key, KEY_IDLE_ACTIVATION_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_idle_activation_enabled (prefs, enabled);

        } else if (strcmp (key, KEY_LOCK_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_lock_enabled (prefs, enabled);
                gs_debug ("lock-enabled=%d",enabled);
        } else if (strcmp (key, KEY_LOCK_DISABLE) == 0) {

                gboolean disabled;

                disabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_lock_disabled (prefs, disabled);

        } else if (strcmp (key, KEY_USER_SWITCH_DISABLE) == 0) {

                gboolean disabled;

                disabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_user_switch_disabled (prefs, disabled);

        } else if (strcmp (key, KEY_KEYBOARD_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_keyboard_enabled (prefs, enabled);

        } else if (strcmp (key, KEY_KEYBOARD_COMMAND) == 0) {

                const char *command;

                command = g_settings_get_string (settings, key);
                _gs_prefs_set_keyboard_command (prefs, command);

        } else if (strcmp (key, KEY_STATUS_MESSAGE_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_status_message_enabled (prefs, enabled);

        } else if (strcmp (key, KEY_LOGOUT_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_logout_enabled (prefs, enabled);

        } else if (strcmp (key, KEY_LOGOUT_DELAY) == 0) {

                guint delay;

                delay = _gs_settings_get_uint (settings, key);
                _gs_prefs_set_logout_timeout (prefs, delay);

        } else if (strcmp (key, KEY_LOGOUT_COMMAND) == 0) {

                const char *command;

                command = g_settings_get_string (settings, key);
                _gs_prefs_set_logout_command (prefs, command);

        } else if (strcmp (key, KEY_USER_SWITCH_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_user_switch_enabled (prefs, enabled);

        } else {
                g_warning ("Config key not handled: %s", key);
                return;
        }

        g_signal_emit (prefs, signals [CHANGED], 0);
}

static void
gs_prefs_init (GSPrefs *prefs)
{
        prefs->priv = GS_PREFS_GET_PRIVATE (prefs);

        prefs->priv->settings          = g_settings_new (GS_SETTINGS_SCHEMA);
        g_signal_connect (prefs->priv->settings,
                          "changed",
                          G_CALLBACK (key_changed_cb),
                          prefs);
        prefs->priv->lockdown          = g_settings_new (LOCKDOWN_SETTINGS_SCHEMA);
        g_signal_connect (prefs->priv->lockdown,
                          "changed",
                          G_CALLBACK (key_changed_cb),
                          prefs);

        prefs->idle_activation_enabled = TRUE;
        prefs->lock_enabled            = TRUE;
        prefs->lock_disabled           = FALSE;
        prefs->logout_enabled          = FALSE;
        prefs->user_switch_enabled     = FALSE;

        prefs->lock_timeout            = 0;
        prefs->logout_timeout          = 14400000;

        gs_prefs_load_from_settings (prefs);
}

static void
gs_prefs_finalize (GObject *object)
{
        GSPrefs *prefs;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_PREFS (object));

        prefs = GS_PREFS (object);

        g_return_if_fail (prefs->priv != NULL);

        if (prefs->priv->settings) {
                g_object_unref (prefs->priv->settings);
                prefs->priv->settings = NULL;
        }
        if (prefs->priv->lockdown) {
                g_object_unref (prefs->priv->lockdown);
                prefs->priv->lockdown = NULL;
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
