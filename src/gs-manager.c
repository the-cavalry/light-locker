/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2008 William Jon McCann <mccann@jhu.edu>
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

#include <time.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-bg.h>

#include "gs-prefs.h"        /* for GSSaverMode */

#include "gs-manager.h"
#include "gs-window.h"
#include "gs-grab.h"
#include "gs-fade.h"
#include "gs-debug.h"

static void gs_manager_class_init (GSManagerClass *klass);
static void gs_manager_init       (GSManager      *manager);
static void gs_manager_finalize   (GObject        *object);

#define GS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_MANAGER, GSManagerPrivate))

struct GSManagerPrivate
{
        GSList      *windows;
        GSettings      *settings;
        GnomeBG        *bg;

        /* Policy */
        glong        lock_timeout;
        glong        logout_timeout;

        guint        lock_enabled : 1;
        guint        logout_enabled : 1;
        guint        keyboard_enabled : 1;
        guint        user_switch_enabled : 1;

        char        *logout_command;
        char        *keyboard_command;

        char        *status_message;

        /* State */
        guint        active : 1;
        guint        lock_active : 1;

        guint        fading : 1;
        guint        dialog_up : 1;

        time_t       activate_time;

        guint        lock_timeout_id;

        GSGrab      *grab;
        GSFade      *fade;
        guint        unfade_idle_id;
};

enum {
        ACTIVATED,
        DEACTIVATED,
        AUTH_REQUEST_BEGIN,
        AUTH_REQUEST_END,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_USER_SWITCH_ENABLED,
        PROP_KEYBOARD_ENABLED,
        PROP_LOCK_TIMEOUT,
        PROP_LOGOUT_TIMEOUT,
        PROP_LOGOUT_COMMAND,
        PROP_KEYBOARD_COMMAND,
        PROP_STATUS_MESSAGE,
        PROP_ACTIVE,
};

#define FADE_TIMEOUT 250

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSManager, gs_manager, G_TYPE_OBJECT)

void
gs_manager_get_lock_active (GSManager *manager,
                            gboolean  *lock_active)
{
        if (lock_active != NULL) {
                *lock_active = FALSE;
        }

        g_return_if_fail (GS_IS_MANAGER (manager));

        *lock_active = manager->priv->lock_active;
}

void
gs_manager_set_lock_active (GSManager *manager,
                            gboolean   lock_active)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        gs_debug ("Setting lock active: %d", lock_active);

        if (manager->priv->lock_active != lock_active) {
                GSList *l;

                manager->priv->lock_active = lock_active;
                for (l = manager->priv->windows; l; l = l->next) {
                        gs_window_set_lock_enabled (l->data, lock_active);
                }
        }
}

void
gs_manager_get_lock_enabled (GSManager *manager,
                             gboolean  *lock_enabled)
{
        if (lock_enabled != NULL) {
                *lock_enabled = FALSE;
        }

        g_return_if_fail (GS_IS_MANAGER (manager));

        *lock_enabled = manager->priv->lock_enabled;
}

void
gs_manager_set_lock_enabled (GSManager *manager,
                             gboolean   lock_enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->lock_enabled != lock_enabled) {
                manager->priv->lock_enabled = lock_enabled;
                gs_debug ("GSManager: lock-enabled=%d", lock_enabled);
        }
}

void
gs_manager_set_logout_enabled (GSManager *manager,
                               gboolean   logout_enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->logout_enabled != logout_enabled) {
                GSList *l;

                manager->priv->logout_enabled = logout_enabled;
                for (l = manager->priv->windows; l; l = l->next) {
                        gs_window_set_logout_enabled (l->data, logout_enabled);
                }
        }
}

void
gs_manager_set_keyboard_enabled (GSManager *manager,
                                 gboolean   enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->keyboard_enabled != enabled) {
                GSList *l;

                manager->priv->keyboard_enabled = enabled;
                for (l = manager->priv->windows; l; l = l->next) {
                        gs_window_set_keyboard_enabled (l->data, enabled);
                }
        }
}

void
gs_manager_set_user_switch_enabled (GSManager *manager,
                                    gboolean   user_switch_enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->user_switch_enabled != user_switch_enabled) {
                GSList *l;

                manager->priv->user_switch_enabled = user_switch_enabled;
                for (l = manager->priv->windows; l; l = l->next) {
                        gs_window_set_user_switch_enabled (l->data, user_switch_enabled);
                }
        }
}

static gboolean
activate_lock_timeout (GSManager *manager)
{
        if (manager->priv->lock_enabled) {
                gs_manager_set_lock_active (manager, TRUE);
        }

        manager->priv->lock_timeout_id = 0;

        return FALSE;
}

static void
remove_lock_timer (GSManager *manager)
{
        if (manager->priv->lock_timeout_id != 0) {
                g_source_remove (manager->priv->lock_timeout_id);
                manager->priv->lock_timeout_id = 0;
        }
}

static void
add_lock_timer (GSManager *manager,
                glong      timeout)
{
        manager->priv->lock_timeout_id = g_timeout_add (timeout,
                                                        (GSourceFunc)activate_lock_timeout,
                                                        manager);
}

void
gs_manager_set_lock_timeout (GSManager *manager,
                             glong      lock_timeout)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->lock_timeout != lock_timeout) {

                manager->priv->lock_timeout = lock_timeout;

                if (manager->priv->active
                    && ! manager->priv->lock_active
                    && (lock_timeout >= 0)) {

                        glong elapsed = (time (NULL) - manager->priv->activate_time) * 1000;

                        remove_lock_timer (manager);

                        if (elapsed >= lock_timeout) {
                                activate_lock_timeout (manager);
                        } else {
                                add_lock_timer (manager, lock_timeout - elapsed);
                        }
                }
        }
}

void
gs_manager_set_logout_timeout (GSManager *manager,
                               glong      logout_timeout)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->logout_timeout != logout_timeout) {
                GSList *l;

                manager->priv->logout_timeout = logout_timeout;
                for (l = manager->priv->windows; l; l = l->next) {
                        gs_window_set_logout_timeout (l->data, logout_timeout);
                }
        }
}

void
gs_manager_set_logout_command (GSManager  *manager,
                               const char *command)
{
        GSList *l;

        g_return_if_fail (GS_IS_MANAGER (manager));

        g_free (manager->priv->logout_command);

        if (command) {
                manager->priv->logout_command = g_strdup (command);
        } else {
                manager->priv->logout_command = NULL;
        }

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_set_logout_command (l->data, manager->priv->logout_command);
        }
}

void
gs_manager_set_keyboard_command (GSManager  *manager,
                                 const char *command)
{
        GSList *l;

        g_return_if_fail (GS_IS_MANAGER (manager));

        g_free (manager->priv->keyboard_command);

        if (command) {
                manager->priv->keyboard_command = g_strdup (command);
        } else {
                manager->priv->keyboard_command = NULL;
        }

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_set_keyboard_command (l->data, manager->priv->keyboard_command);
        }
}

void
gs_manager_set_status_message (GSManager  *manager,
                               const char *status_message)
{
        GSList *l;

        g_return_if_fail (GS_IS_MANAGER (manager));

        g_free (manager->priv->status_message);

        manager->priv->status_message = g_strdup (status_message);

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_set_status_message (l->data, manager->priv->status_message);
        }
}

static void
gs_manager_set_property (GObject            *object,
                         guint               prop_id,
                         const GValue       *value,
                         GParamSpec         *pspec)
{
        GSManager *self;

        self = GS_MANAGER (object);

        switch (prop_id) {
        case PROP_LOCK_ENABLED:
                gs_manager_set_lock_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOCK_TIMEOUT:
                gs_manager_set_lock_timeout (self, g_value_get_long (value));
                break;
        case PROP_LOGOUT_ENABLED:
                gs_manager_set_logout_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_KEYBOARD_ENABLED:
                gs_manager_set_keyboard_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_USER_SWITCH_ENABLED:
                gs_manager_set_user_switch_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOGOUT_TIMEOUT:
                gs_manager_set_logout_timeout (self, g_value_get_long (value));
                break;
        case PROP_LOGOUT_COMMAND:
                gs_manager_set_logout_command (self, g_value_get_string (value));
                break;
        case PROP_KEYBOARD_COMMAND:
                gs_manager_set_keyboard_command (self, g_value_get_string (value));
                break;
        case PROP_STATUS_MESSAGE:
                gs_manager_set_status_message (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_manager_get_property (GObject            *object,
                         guint               prop_id,
                         GValue             *value,
                         GParamSpec         *pspec)
{
        GSManager *self;

        self = GS_MANAGER (object);

        switch (prop_id) {
        case PROP_LOCK_ENABLED:
                g_value_set_boolean (value, self->priv->lock_enabled);
                break;
        case PROP_LOCK_TIMEOUT:
                g_value_set_long (value, self->priv->lock_timeout);
                break;
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
                break;
        case PROP_KEYBOARD_ENABLED:
                g_value_set_boolean (value, self->priv->keyboard_enabled);
                break;
        case PROP_USER_SWITCH_ENABLED:
                g_value_set_boolean (value, self->priv->user_switch_enabled);
                break;
        case PROP_LOGOUT_TIMEOUT:
                g_value_set_long (value, self->priv->logout_timeout);
                break;
        case PROP_LOGOUT_COMMAND:
                g_value_set_string (value, self->priv->logout_command);
                break;
        case PROP_KEYBOARD_COMMAND:
                g_value_set_string (value, self->priv->keyboard_command);
                break;
        case PROP_STATUS_MESSAGE:
                g_value_set_string (value, self->priv->status_message);
                break;
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->active);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_manager_class_init (GSManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = gs_manager_finalize;
        object_class->get_property = gs_manager_get_property;
        object_class->set_property = gs_manager_set_property;

        signals [ACTIVATED] =
                g_signal_new ("activated",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, activated),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DEACTIVATED] =
                g_signal_new ("deactivated",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, deactivated),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [AUTH_REQUEST_BEGIN] =
                g_signal_new ("auth-request-begin",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, auth_request_begin),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [AUTH_REQUEST_END] =
                g_signal_new ("auth-request-end",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, auth_request_end),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_LOCK_ENABLED,
                                         g_param_spec_boolean ("lock-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LOCK_TIMEOUT,
                                         g_param_spec_long ("lock-timeout",
                                                            NULL,
                                                            NULL,
                                                            -1,
                                                            G_MAXLONG,
                                                            0,
                                                            G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_ENABLED,
                                         g_param_spec_boolean ("logout-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_USER_SWITCH_ENABLED,
                                         g_param_spec_boolean ("user-switch-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_TIMEOUT,
                                         g_param_spec_long ("logout-timeout",
                                                            NULL,
                                                            NULL,
                                                            -1,
                                                            G_MAXLONG,
                                                            0,
                                                            G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_COMMAND,
                                         g_param_spec_string ("logout-command",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (GSManagerPrivate));
}

static void
on_bg_changed (GnomeBG   *bg,
               GSManager *manager)
{
        gs_debug ("background changed");
}

static gboolean
background_settings_change_event_cb (GSettings *settings,
                                     gpointer   keys,
                                     gint       n_keys,
                                     GSManager   *manager)
{
#if 0
        /* FIXME: since we bind user settings instead of system ones,
         *        watching for changes is no longer valid.
         */
        gnome_bg_load_from_preferences (manager->priv->bg,
                                        manager->priv->settings);
#endif

        return FALSE;
}

static GSettings *
get_system_settings (void)
{
        GSettings *settings;
        gchar **keys;
        gchar **k;

        /* FIXME: we need to bind system settings instead of user but
         *        that's currently impossible, not implemented yet.
         *        Hence, reset to system default values.
         */
        /* TODO: Ideally we would like to bind some other key, screensaver-specific. */
        settings = g_settings_new ("org.gnome.desktop.background");

        g_settings_delay (settings);

        keys = g_settings_list_keys (settings);
        for (k = keys; *k; k++) {
                g_settings_reset (settings, *k);
        }
        g_strfreev (keys);

        return settings;
}

static void
gs_manager_init (GSManager *manager)
{
        manager->priv = GS_MANAGER_GET_PRIVATE (manager);

        manager->priv->fade = gs_fade_new ();
        manager->priv->grab = gs_grab_new ();

        manager->priv->settings = get_system_settings ();
        manager->priv->bg = gnome_bg_new ();

        g_signal_connect (manager->priv->bg,
                          "changed",
                          G_CALLBACK (on_bg_changed),
                          manager);
        g_signal_connect (manager->priv->settings,
                          "change-event",
                          G_CALLBACK (background_settings_change_event_cb),
                          manager);

        gnome_bg_load_from_preferences (manager->priv->bg,
                                        manager->priv->settings);
}

static void
remove_timers (GSManager *manager)
{
        remove_lock_timer (manager);
}

static void
remove_unfade_idle (GSManager *manager)
{
        if (manager->priv->unfade_idle_id > 0) {
                g_source_remove (manager->priv->unfade_idle_id);
                manager->priv->unfade_idle_id = 0;
        }
}


static gboolean
window_deactivated_idle (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        /* don't deactivate directly but only emit a signal
           so that we let the parent deactivate */
        g_signal_emit (manager, signals [DEACTIVATED], 0);

        return FALSE;
}

static void
window_deactivated_cb (GSWindow  *window,
                       GSManager *manager)
{
        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        g_idle_add ((GSourceFunc)window_deactivated_idle, manager);
}

static GSWindow *
find_window_at_pointer (GSManager *manager)
{
        GdkDisplay *display;
        GdkScreen  *screen;
        int         monitor;
        int         x, y;
        GSWindow   *window;
        int         screen_num;
        GSList     *l;

        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, &x, &y, NULL);
        monitor = gdk_screen_get_monitor_at_point (screen, x, y);
        screen_num = gdk_screen_get_number (screen);

        /* Find the gs-window that is on that screen */
        window = NULL;
        for (l = manager->priv->windows; l; l = l->next) {
                GSWindow *win = GS_WINDOW (l->data);
                if (gs_window_get_screen (win) == screen
                    && gs_window_get_monitor (win) == monitor) {
                        window = win;
                }
        }

        if (window == NULL) {
                gs_debug ("WARNING: Could not find the GSWindow for screen %d", screen_num);
                /* take the first one */
                window = manager->priv->windows->data;
        } else {
                gs_debug ("Requesting unlock for screen %d", screen_num);
        }

        return window;
}

void
gs_manager_show_message (GSManager  *manager,
                         const char *summary,
                         const char *body,
                         const char *icon)
{
        GSWindow *window;

        g_return_if_fail (GS_IS_MANAGER (manager));

        /* Find the GSWindow that contains the pointer */
        window = find_window_at_pointer (manager);
        gs_window_show_message (window, summary, body, icon);

        gs_manager_request_unlock (manager);
}

static gboolean
manager_maybe_grab_window (GSManager *manager,
                           GSWindow  *window)
{
        GdkDisplay *display;
        GdkScreen  *screen;
        int         monitor;
        int         x, y;
        gboolean    grabbed;

        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, &x, &y, NULL);
        monitor = gdk_screen_get_monitor_at_point (screen, x, y);

        gdk_flush ();
        grabbed = FALSE;
        if (gs_window_get_screen (window) == screen
            && gs_window_get_monitor (window) == monitor) {
                gs_debug ("Moving grab to %p", window);
                gs_grab_move_to_window (manager->priv->grab,
                                        gs_window_get_gdk_window (window),
                                        gs_window_get_screen (window),
                                        FALSE);
                grabbed = TRUE;
        }

        return grabbed;
}

static void
window_grab_broken_cb (GSWindow           *window,
                       GdkEventGrabBroken *event,
                       GSManager          *manager)
{
        gs_debug ("GRAB BROKEN!");
        if (event->keyboard) {
                gs_grab_keyboard_reset (manager->priv->grab);
        } else {
                gs_grab_mouse_reset (manager->priv->grab);
        }
}

static gboolean
unfade_idle (GSManager *manager)
{
        gs_debug ("resetting fade");
        gs_fade_reset (manager->priv->fade);
        manager->priv->unfade_idle_id = 0;
        return FALSE;
}


static void
add_unfade_idle (GSManager *manager)
{
        remove_unfade_idle (manager);
        manager->priv->unfade_idle_id = g_timeout_add (500, (GSourceFunc)unfade_idle, manager);
}

static gboolean
window_map_event_cb (GSWindow  *window,
                     GdkEvent  *event,
                     GSManager *manager)
{
        gs_debug ("Handling window map_event event");

        manager_maybe_grab_window (manager, window);

        return FALSE;
}

static void
window_map_cb (GSWindow  *window,
               GSManager *manager)
{
        gs_debug ("Handling window map event");
}

static void
window_unmap_cb (GSWindow  *window,
                 GSManager *manager)
{
        gs_debug ("window unmapped!");
}

static void
apply_background_to_window (GSManager *manager,
                            GSWindow  *window)
{
        cairo_surface_t *surface;
        GdkScreen       *screen;
        GdkWindow       *gdk_window;
        gint             monitor;
        GdkRectangle     monitor_geometry;
        int              width;
        int              height;

        if (manager->priv->bg == NULL) {
                gs_debug ("No background available");
                gs_window_set_background_surface (window, NULL);
        }

        screen = gs_window_get_screen (window);
        gdk_window = gs_window_get_gdk_window (window);
        monitor = gdk_screen_get_monitor_at_window (screen, gdk_window);
        gdk_screen_get_monitor_geometry (screen, monitor, &monitor_geometry);
        width = monitor_geometry.width;
        height = monitor_geometry.height;
        gs_debug ("Creating background w:%d h:%d", width, height);
        surface = gnome_bg_create_surface (manager->priv->bg,
                                           gdk_window,
                                           width,
                                           height,
                                           FALSE);
        gs_window_set_background_surface (window, surface);
        cairo_surface_destroy (surface);
}

static void
manager_show_window (GSManager *manager,
                     GSWindow  *window)
{
        apply_background_to_window (manager, window);

        manager->priv->activate_time = time (NULL);

        if (manager->priv->lock_timeout >= 0) {
                remove_lock_timer (manager);
                add_lock_timer (manager, manager->priv->lock_timeout);
        }

        add_unfade_idle (manager);

        /* FIXME: only emit signal once */
        g_signal_emit (manager, signals [ACTIVATED], 0);
}

static void
window_show_cb (GSWindow  *window,
                GSManager *manager)
{

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));
        g_return_if_fail (window != NULL);
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Handling window show");
        manager_show_window (manager, window);
}

static void
handle_window_dialog_up (GSManager *manager,
                         GSWindow  *window)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        gs_debug ("Handling dialog up");

        g_signal_emit (manager, signals [AUTH_REQUEST_BEGIN], 0);

        manager->priv->dialog_up = TRUE;
        /* Make all other windows insensitive so we don't get events */
        for (l = manager->priv->windows; l; l = l->next) {
                if (l->data != window) {
                        gtk_widget_set_sensitive (GTK_WIDGET (l->data), FALSE);
                }
        }

        /* Move keyboard and mouse grabs so dialog can be used */
        gs_grab_move_to_window (manager->priv->grab,
                                gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);

        /* Release the pointer grab while dialog is up so that
           the dialog can be used.  We'll regrab it when the dialog goes down. */
        gs_grab_release_mouse (manager->priv->grab);
}

static void
handle_window_dialog_down (GSManager *manager,
                           GSWindow  *window)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        gs_debug ("Handling dialog down");

        /* Regrab the mouse */
        gs_grab_move_to_window (manager->priv->grab,
                                gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);

        /* Make all windows sensitive so we get events */
        for (l = manager->priv->windows; l; l = l->next) {
                gtk_widget_set_sensitive (GTK_WIDGET (l->data), TRUE);
        }

        manager->priv->dialog_up = FALSE;

        g_signal_emit (manager, signals [AUTH_REQUEST_END], 0);
}

static void
window_dialog_up_changed_cb (GSWindow   *window,
                             GParamSpec *pspec,
                             GSManager  *manager)
{
        gboolean up;

        up = gs_window_is_dialog_up (window);
        gs_debug ("Handling window dialog up changed: %s", up ? "up" : "down");
        if (up) {
                handle_window_dialog_up (manager, window);
        } else {
                handle_window_dialog_down (manager, window);
        }
}

static gboolean
window_activity_cb (GSWindow  *window,
                    GSManager *manager)
{
        gboolean handled;

        handled = gs_manager_request_unlock (manager);

        return handled;
}

static void
disconnect_window_signals (GSManager *manager,
                           GSWindow  *window)
{
        g_signal_handlers_disconnect_by_func (window, window_deactivated_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_activity_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_show_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_map_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_map_event_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_dialog_up_changed_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_unmap_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_grab_broken_cb, manager);
}

static void
window_destroyed_cb (GtkWindow *window,
                     GSManager *manager)
{
        disconnect_window_signals (manager, GS_WINDOW (window));
}

static void
connect_window_signals (GSManager *manager,
                        GSWindow  *window)
{
        g_signal_connect_object (window, "destroy",
                                 G_CALLBACK (window_destroyed_cb), manager, 0);
        g_signal_connect_object (window, "activity",
                                 G_CALLBACK (window_activity_cb), manager, 0);
        g_signal_connect_object (window, "deactivated",
                                 G_CALLBACK (window_deactivated_cb), manager, 0);
        g_signal_connect_object (window, "show",
                                 G_CALLBACK (window_show_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "map",
                                 G_CALLBACK (window_map_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "map_event",
                                 G_CALLBACK (window_map_event_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "notify::dialog-up",
                                 G_CALLBACK (window_dialog_up_changed_cb), manager, 0);
        g_signal_connect_object (window, "unmap",
                                 G_CALLBACK (window_unmap_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "grab_broken_event",
                                 G_CALLBACK (window_grab_broken_cb), manager, G_CONNECT_AFTER);
}

static void
gs_manager_create_window_for_monitor (GSManager *manager,
                                      GdkScreen *screen,
                                      int        monitor)
{
        GSWindow    *window;
        GdkRectangle rect;

        gdk_screen_get_monitor_geometry (screen, monitor, &rect);

        gs_debug ("Creating window for monitor %d [%d,%d] (%dx%d)",
                  monitor, rect.x, rect.y, rect.width, rect.height);

        window = gs_window_new (screen, monitor, manager->priv->lock_active);

        gs_window_set_user_switch_enabled (window, manager->priv->user_switch_enabled);
        gs_window_set_logout_enabled (window, manager->priv->logout_enabled);
        gs_window_set_logout_timeout (window, manager->priv->logout_timeout);
        gs_window_set_logout_command (window, manager->priv->logout_command);
        gs_window_set_keyboard_enabled (window, manager->priv->keyboard_enabled);
        gs_window_set_keyboard_command (window, manager->priv->keyboard_command);
        gs_window_set_status_message (window, manager->priv->status_message);

        connect_window_signals (manager, window);

        manager->priv->windows = g_slist_append (manager->priv->windows, window);

        if (manager->priv->active && !manager->priv->fading) {
                gtk_widget_show (GTK_WIDGET (window));
        }
}

static void
on_screen_monitors_changed (GdkScreen *screen,
                            GSManager *manager)
{
        GSList *l;
        int     n_monitors;
        int     n_windows;
        int     i;

        n_monitors = gdk_screen_get_n_monitors (screen);
        n_windows = g_slist_length (manager->priv->windows);

        gs_debug ("Monitors changed for screen %d: num=%d",
                  gdk_screen_get_number (screen),
                  n_monitors);

        if (n_monitors > n_windows) {

                /* Tear down unlock dialog in case we want to move it
                 * to a new monitor
                 */
                l = manager->priv->windows;
                while (l != NULL) {
                        gs_window_cancel_unlock_request (GS_WINDOW (l->data));
                        l = l->next;
                }

                /* add more windows */
                for (i = n_windows; i < n_monitors; i++) {
                        gs_manager_create_window_for_monitor (manager, screen, i);
                }

                /* And put unlock dialog up where ever it's supposed to be
                 */
                gs_manager_request_unlock (manager);
        } else {

                gdk_x11_grab_server ();

                /* remove the extra windows */
                l = manager->priv->windows;
                while (l != NULL) {
                        GdkScreen *this_screen;
                        int        this_monitor;
                        GSList    *next = l->next;

                        this_screen = gs_window_get_screen (GS_WINDOW (l->data));
                        this_monitor = gs_window_get_monitor (GS_WINDOW (l->data));
                        if (this_screen == screen && this_monitor >= n_monitors) {
                                gs_window_destroy (GS_WINDOW (l->data));
                                manager->priv->windows = g_slist_delete_link (manager->priv->windows, l);
                        }
                        l = next;
                }

                /* make sure there is a lock dialog on a connected monitor,
                 * and that the keyboard is still properly grabbed after all
                 * the windows above got destroyed*/
                if (n_windows > n_monitors) {
                        gs_manager_request_unlock (manager);
                }

                gdk_flush ();
                gdk_x11_ungrab_server ();
        }
}

static void
gs_manager_destroy_windows (GSManager *manager)
{
        GdkDisplay  *display;
        GSList      *l;
        int          n_screens;
        int          i;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->windows == NULL) {
                return;
        }

        display = gdk_display_get_default ();

        n_screens = gdk_display_get_n_screens (display);

        for (i = 0; i < n_screens; i++) {
                g_signal_handlers_disconnect_by_func (gdk_display_get_screen (display, i),
                                                      on_screen_monitors_changed,
                                                      manager);
        }

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_destroy (l->data);
        }
        g_slist_free (manager->priv->windows);
        manager->priv->windows = NULL;
}

static void
gs_manager_finalize (GObject *object)
{
        GSManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_MANAGER (object));

        manager = GS_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->bg != NULL) {
                g_object_unref (manager->priv->bg);
        }
        if (manager->priv->settings != NULL) {
                g_settings_revert (manager->priv->settings);
                g_object_unref (manager->priv->settings);
        }

        g_free (manager->priv->logout_command);
        g_free (manager->priv->keyboard_command);
        g_free (manager->priv->status_message);

        remove_unfade_idle (manager);
        remove_timers (manager);

        gs_grab_release (manager->priv->grab);

        gs_manager_destroy_windows (manager);

        manager->priv->active = FALSE;
        manager->priv->activate_time = 0;
        manager->priv->lock_enabled = FALSE;

        g_object_unref (manager->priv->fade);
        g_object_unref (manager->priv->grab);

        G_OBJECT_CLASS (gs_manager_parent_class)->finalize (object);
}

static void
gs_manager_create_windows_for_screen (GSManager *manager,
                                      GdkScreen *screen)
{
        int       n_monitors;
        int       i;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));
        g_return_if_fail (GDK_IS_SCREEN (screen));

        g_object_ref (manager);
        g_object_ref (screen);

        n_monitors = gdk_screen_get_n_monitors (screen);

        gs_debug ("Creating %d windows for screen %d", n_monitors, gdk_screen_get_number (screen));

        for (i = 0; i < n_monitors; i++) {
                gs_manager_create_window_for_monitor (manager, screen, i);
        }

        g_object_unref (screen);
        g_object_unref (manager);
}

static void
gs_manager_create_windows (GSManager *manager)
{
        GdkDisplay  *display;
        int          n_screens;
        int          i;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        g_assert (manager->priv->windows == NULL);

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);

        for (i = 0; i < n_screens; i++) {
                g_signal_connect (gdk_display_get_screen (display, i),
                                  "monitors-changed",
                                  G_CALLBACK (on_screen_monitors_changed),
                                  manager);

                gs_manager_create_windows_for_screen (manager, gdk_display_get_screen (display, i));
        }
}

GSManager *
gs_manager_new (void)
{
        GObject *manager;

        manager = g_object_new (GS_TYPE_MANAGER, NULL);

        return GS_MANAGER (manager);
}

static void
show_windows (GSList *windows)
{
        GSList *l;

        for (l = windows; l; l = l->next) {
                gtk_widget_show (GTK_WIDGET (l->data));
        }
}

static void
fade_done_cb (GSFade    *fade,
              GSManager *manager)
{
        gs_debug ("fade completed, showing windows");
        show_windows (manager->priv->windows);
        manager->priv->fading = FALSE;
}

static gboolean
gs_manager_activate (GSManager *manager)
{
        gboolean    do_fade;
        gboolean    res;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (manager->priv->active) {
                gs_debug ("Trying to activate manager when already active");
                return FALSE;
        }

        res = gs_grab_grab_root (manager->priv->grab, FALSE);
        if (! res) {
                return FALSE;
        }

        if (manager->priv->windows == NULL) {
                gs_manager_create_windows (GS_MANAGER (manager));
        }

        manager->priv->active = TRUE;

        /* fade to black and show windows */
        do_fade = TRUE;
        if (do_fade) {
                manager->priv->fading = TRUE;
                gs_debug ("fading out");
                gs_fade_async (manager->priv->fade,
                               FADE_TIMEOUT,
                               (GSFadeDoneFunc)fade_done_cb,
                               manager);

                while (manager->priv->fading) {
                        gtk_main_iteration ();
                }
        } else {
                show_windows (manager->priv->windows);
        }

        return TRUE;
}

static gboolean
gs_manager_deactivate (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->active) {
                gs_debug ("Trying to deactivate a screensaver that is not active");
                return FALSE;
        }

        remove_unfade_idle (manager);
        gs_fade_reset (manager->priv->fade);
        remove_timers (manager);

        gs_grab_release (manager->priv->grab);

        gs_manager_destroy_windows (manager);

        /* reset state */
        manager->priv->active = FALSE;
        manager->priv->activate_time = 0;
        manager->priv->lock_active = FALSE;
        manager->priv->dialog_up = FALSE;
        manager->priv->fading = FALSE;

        return TRUE;
}

gboolean
gs_manager_set_active (GSManager *manager,
                       gboolean   active)
{
        gboolean res;

        if (active) {
                res = gs_manager_activate (manager);
        } else {
                res = gs_manager_deactivate (manager);
        }

        return res;
}

gboolean
gs_manager_get_active (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        return manager->priv->active;
}

gboolean
gs_manager_request_unlock (GSManager *manager)
{
        GSWindow *window;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->active) {
                gs_debug ("Request unlock but manager is not active");
                return FALSE;
        }

        if (manager->priv->dialog_up) {
                gs_debug ("Request unlock but dialog is already up");
                return FALSE;
        }

        if (manager->priv->fading) {
                gs_debug ("Request unlock so finishing fade");
                gs_fade_finish (manager->priv->fade);
        }

        if (manager->priv->windows == NULL) {
                gs_debug ("We don't have any windows!");
                return FALSE;
        }

        /* Find the GSWindow that contains the pointer */
        window = find_window_at_pointer (manager);
        gs_window_request_unlock (window);

        return TRUE;
}

void
gs_manager_cancel_unlock_request (GSManager *manager)
{
        GSList *l;
        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_cancel_unlock_request (l->data);
        }
}
