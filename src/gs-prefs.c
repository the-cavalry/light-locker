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

#include "gs-prefs.h"
#include "gs-debug.h"

static void gs_prefs_class_init (GSPrefsClass *klass);
static void gs_prefs_init       (GSPrefs      *prefs);
static void gs_prefs_finalize   (GObject      *object);

#define LOCKDOWN_SETTINGS_SCHEMA "org.gnome.desktop.lockdown"

#define GS_SETTINGS_SCHEMA "org.gnome.desktop.screensaver"
#define KEY_IDLE_ACTIVATION_ENABLED         "idle-activation-enabled"
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
_gs_prefs_set_idle_activation_enabled (GSPrefs *prefs,
                                       gboolean value)
{
        prefs->idle_activation_enabled = value;
}

static void
_gs_prefs_set_status_message_enabled (GSPrefs  *prefs,
                                      gboolean  enabled)
{
        prefs->status_message_enabled = enabled;
}

static void
gs_prefs_load_from_settings (GSPrefs *prefs)
{
        gboolean bvalue;

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_IDLE_ACTIVATION_ENABLED);
        _gs_prefs_set_idle_activation_enabled (prefs, bvalue);

        bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_STATUS_MESSAGE_ENABLED);
        _gs_prefs_set_status_message_enabled (prefs, bvalue);

        /* Lockdown keys */

}

static void
key_changed_cb (GSettings   *settings,
                const gchar *key,
                GSPrefs     *prefs)
{
        if (strcmp (key, KEY_IDLE_ACTIVATION_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_idle_activation_enabled (prefs, enabled);

        } else if (strcmp (key, KEY_STATUS_MESSAGE_ENABLED) == 0) {

                gboolean enabled;

                enabled = g_settings_get_boolean (settings, key);
                _gs_prefs_set_status_message_enabled (prefs, enabled);

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

        G_OBJECT_CLASS (gs_prefs_parent_class)->finalize (object);
}

GSPrefs *
gs_prefs_new (void)
{
        GObject *prefs;

        prefs = g_object_new (GS_TYPE_PREFS, NULL);

        return GS_PREFS (prefs);
}
