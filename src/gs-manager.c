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

#include <time.h>
#include <gdk/gdk.h>

#include "gs-prefs.h"        /* for GSSaverMode */

#include "gs-manager.h"
#include "gs-window.h"
#include "gs-job.h"
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
        GSList      *jobs;

        /* Policy */
        glong        lock_timeout;
        glong        cycle_timeout;
        glong        logout_timeout;

        guint        lock_enabled : 1;
        guint        logout_enabled : 1;
        guint        user_switch_enabled : 1;
        guint        throttle_enabled : 1;

        char        *logout_command;

        /* State */
        guint        active : 1;
        guint        lock_active : 1;

        guint        dialog_up : 1;

        guint        lock_timeout_id;
        guint        cycle_timeout_id;

        time_t       activate_time;

        GSList      *themes;
        GSSaverMode  saver_mode;
};

enum {
        ACTIVATED,
        DEACTIVATED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_USER_SWITCH_ENABLED,
        PROP_LOCK_TIMEOUT,
        PROP_CYCLE_TIMEOUT,
        PROP_LOGOUT_TIMEOUT,
        PROP_LOGOUT_COMMAND,
        PROP_ACTIVE,
        PROP_THROTTLE_ENABLED,
};

#define FADE_TIMEOUT 1000

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSManager, gs_manager, G_TYPE_OBJECT)

void
gs_manager_set_mode (GSManager  *manager,
                     GSSaverMode mode)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        manager->priv->saver_mode = mode;
}

void
gs_manager_set_themes (GSManager *manager,
                       GSList    *themes)
{
        GSList *l;

        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->themes) {
                g_slist_foreach (manager->priv->themes, (GFunc)g_free, NULL);
                g_slist_free (manager->priv->themes);
        }
        manager->priv->themes = NULL;

        for (l = themes; l; l = l->next) {
                manager->priv->themes = g_slist_append (manager->priv->themes, g_strdup (l->data));
        }
}

void
gs_manager_set_throttle_enabled (GSManager *manager,
                                 gboolean   throttle_enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->throttle_enabled != throttle_enabled) {
                GSList *l;

                manager->priv->throttle_enabled = throttle_enabled;

                if (! manager->priv->dialog_up) {

                        for (l = manager->priv->jobs; l; l = l->next) {
                                if (throttle_enabled) {
                                        gs_job_stop (GS_JOB (l->data));
                                } else {
                                        gs_job_start (GS_JOB (l->data));
                                }
                        }

                        for (l = manager->priv->windows; l; l = l->next) {
                                gs_window_clear (l->data);
                        }
                }
        }
}

void
gs_manager_set_lock_active (GSManager *manager,
                            gboolean   lock_active)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->lock_active != lock_active) {
                GSList *l;

                manager->priv->lock_active = lock_active;
                for (l = manager->priv->windows; l; l = l->next)
                        gs_window_set_lock_enabled (l->data, lock_active);
        }
}

void
gs_manager_set_lock_enabled (GSManager *manager,
                             gboolean   lock_enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->lock_enabled != lock_enabled) {
                manager->priv->lock_enabled = lock_enabled;
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
                for (l = manager->priv->windows; l; l = l->next)
                        gs_window_set_logout_enabled (l->data, logout_enabled);
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
                for (l = manager->priv->windows; l; l = l->next)
                        gs_window_set_user_switch_enabled (l->data, user_switch_enabled);
        }
}

static gboolean
activate_lock_timeout (GSManager *manager)
{
        if (manager->priv->lock_enabled)
                gs_manager_set_lock_active (manager, TRUE);

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
                for (l = manager->priv->windows; l; l = l->next)
                        gs_window_set_logout_timeout (l->data, logout_timeout);
        }
}

void
gs_manager_set_logout_command (GSManager  *manager,
                               const char *command)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        g_free (manager->priv->logout_command);

        if (command) {
                manager->priv->logout_command = g_strdup (command);
        } else {
                manager->priv->logout_command = NULL;
        }
}

static const char *
select_theme (GSManager *manager)
{
        const char *theme = NULL;

        g_return_val_if_fail (manager != NULL, NULL);
        g_return_val_if_fail (GS_IS_MANAGER (manager), NULL);

        if (manager->priv->saver_mode == GS_MODE_BLANK_ONLY)
                return NULL;

        if (manager->priv->themes) {
                int number = 0;

                if (manager->priv->saver_mode == GS_MODE_RANDOM) {
                        g_random_set_seed (time (NULL));
                        number = g_random_int_range (0, g_slist_length (manager->priv->themes));
                }
                theme = g_slist_nth_data (manager->priv->themes, number);
        }

        return theme;
}

gboolean
gs_manager_cycle (GSManager *manager)
{
        GSList     *l;
        const char *theme;
        gboolean    success = FALSE;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->active)
                return FALSE;

        if (manager->priv->dialog_up)
                return FALSE;

        if (manager->priv->throttle_enabled)
                return FALSE;

        theme = select_theme (manager);

        for (l = manager->priv->jobs; l; l = l->next) {
                gs_job_stop (GS_JOB (l->data));
                gs_job_set_theme (GS_JOB (l->data), theme, NULL);

                /* this success flag is kinda bogus */
                if (gs_job_start (GS_JOB (l->data)))
                        success = TRUE;
        }

        return success;
}

static gboolean
cycle_timeout (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->dialog_up)
                gs_manager_cycle (manager);

        return TRUE;
}

static void
remove_cycle_timer (GSManager *manager)
{
        if (manager->priv->cycle_timeout_id != 0) {
                g_source_remove (manager->priv->cycle_timeout_id);
                manager->priv->cycle_timeout_id = 0;
        }
}

static void
add_cycle_timer (GSManager *manager,
                 glong      timeout)
{
        manager->priv->cycle_timeout_id = g_timeout_add (timeout,
                                                         (GSourceFunc)cycle_timeout,
                                                         manager);
}

void
gs_manager_set_cycle_timeout (GSManager *manager,
                              glong      cycle_timeout)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->cycle_timeout != cycle_timeout) {

                manager->priv->cycle_timeout = cycle_timeout;

                if (manager->priv->active && (cycle_timeout >= 0)) {
                        glong timeout;
                        glong elapsed = (time (NULL) - manager->priv->activate_time) * 1000;

                        remove_cycle_timer (manager);

                        if (elapsed >= cycle_timeout) {
                                timeout = 0;
                        } else {
                                timeout = cycle_timeout - elapsed;
                        }

                        add_cycle_timer (manager, timeout);

                }
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
        case PROP_THROTTLE_ENABLED:
                gs_manager_set_throttle_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOCK_ENABLED:
                gs_manager_set_lock_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOCK_TIMEOUT:
                gs_manager_set_lock_timeout (self, g_value_get_long (value));
                break;
        case PROP_LOGOUT_ENABLED:
                gs_manager_set_logout_enabled (self, g_value_get_boolean (value));
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
        case PROP_CYCLE_TIMEOUT:
                gs_manager_set_cycle_timeout (self, g_value_get_long (value));
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
        case PROP_THROTTLE_ENABLED:
                g_value_set_boolean (value, self->priv->throttle_enabled);
                break;
        case PROP_LOCK_ENABLED:
                g_value_set_boolean (value, self->priv->lock_enabled);
                break;
        case PROP_LOCK_TIMEOUT:
                g_value_set_long (value, self->priv->lock_timeout);
                break;
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
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
        case PROP_CYCLE_TIMEOUT:
                g_value_set_long (value, self->priv->cycle_timeout);
                break;
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->active);
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_manager_class_init (GSManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

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
                                         PROP_LOGOUT_TIMEOUT,
                                         g_param_spec_string ("logout-command",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_CYCLE_TIMEOUT,
                                         g_param_spec_long ("cycle-timeout",
                                                            NULL,
                                                            NULL,
                                                            10000,
                                                            G_MAXLONG,
                                                            300000,
                                                            G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_THROTTLE_ENABLED,
                                         g_param_spec_boolean ("throttle-enabled",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (GSManagerPrivate));
}

static void
gs_manager_init (GSManager *manager)
{
        manager->priv = GS_MANAGER_GET_PRIVATE (manager);
}

static void
remove_timers (GSManager *manager)
{
        remove_lock_timer (manager);
        remove_cycle_timer (manager);
}

static void
gs_manager_finalize (GObject *object)
{
        GSManager *manager;
        GSList    *l;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_MANAGER (object));

        manager = GS_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_free (manager->priv->logout_command);

        remove_timers (manager);

        gs_grab_release_keyboard_and_mouse ();

        for (l = manager->priv->jobs; l; l = l->next) {
                gs_job_stop (GS_JOB (l->data));
                g_object_unref (l->data);
        }
        g_slist_free (manager->priv->jobs);
        manager->priv->jobs = NULL;

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_destroy (l->data);
        }
        g_slist_free (manager->priv->windows);
        manager->priv->windows = NULL;

        manager->priv->active = FALSE;
        manager->priv->activate_time = 0;
        manager->priv->lock_enabled = FALSE;

        G_OBJECT_CLASS (parent_class)->finalize (object);
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

static void
window_dialog_up_cb (GSWindow  *window,
                     GSManager *manager)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        gs_debug ("Handling dialog up");

        manager->priv->dialog_up = TRUE;

        /* Grab keyboard so dialog can be used */
        gs_grab_window (gs_window_get_gdk_window (window),
                        gs_window_get_screen (window),
                        FALSE);

        /* Make all other windows insensitive so we don't get events */
        for (l = manager->priv->windows; l; l = l->next) {
                if (l->data != window)
                        gtk_widget_set_sensitive (GTK_WIDGET (l->data), FALSE);
        }

        if (! manager->priv->throttle_enabled) {
                gs_debug ("Suspending jobs");

                for (l = manager->priv->jobs; l; l = l->next) {
                        gs_job_suspend (GS_JOB (l->data), TRUE);
                }
        }
}

static void
window_dialog_down_cb (GSWindow  *window,
                       GSManager *manager)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        /* Make all windows sensitive so we get events */
        for (l = manager->priv->windows; l; l = l->next) {
                gtk_widget_set_sensitive (GTK_WIDGET (l->data), TRUE);
        }

        if (! manager->priv->throttle_enabled) {
                for (l = manager->priv->jobs; l; l = l->next) {
                        if (gs_job_is_running (GS_JOB (l->data))) {
                                gs_job_suspend (GS_JOB (l->data), FALSE);
                        } else {
                                gs_job_start (GS_JOB (l->data));
                        }
                }
        }

        manager->priv->dialog_up = FALSE;
}

static gboolean
window_map_event_cb (GSWindow  *window,
                     GdkEvent  *event,
                     GSManager *manager)
{
        GdkDisplay *display;
        GdkScreen  *screen;
        int         monitor;
        int         x, y;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (window != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, &x, &y, NULL);
        monitor = gdk_screen_get_monitor_at_point (screen, x, y);

        if (gs_window_get_screen (window) == screen
            && gs_window_get_monitor (window) == monitor) {
                gs_grab_window (gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);
        }

        return FALSE;
}

static void
window_show_cb (GSWindow  *window,
                GSManager *manager)
{
        GSJob      *job;
        const char *theme;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));
        g_return_if_fail (window != NULL);
        g_return_if_fail (GS_IS_WINDOW (window));

        job = gs_job_new_for_widget (GTK_WIDGET (window));

        theme = select_theme (manager);
        gs_job_set_theme (job, theme, NULL);

        if (! manager->priv->throttle_enabled) {
                gs_job_start (job);
        }

        manager->priv->jobs = g_slist_append (manager->priv->jobs, job);

        manager->priv->activate_time = time (NULL);

        /* Set lock state to off initially.  It may be enabled by activate_lock_timout() */
        gs_manager_set_lock_active (manager, FALSE);

        if (manager->priv->lock_timeout >= 0) {
                remove_lock_timer (manager);
                add_lock_timer (manager, manager->priv->lock_timeout);
        }

        if (manager->priv->cycle_timeout >= 10000) {
                remove_cycle_timer (manager);
                add_cycle_timer (manager, manager->priv->cycle_timeout);
        }

        /* FIXME: only emit signal once */
        g_signal_emit (manager, signals [ACTIVATED], 0);
}

static void
disconnect_window_signals (GSManager *manager,
                           GSWindow  *window)
{
        g_signal_handlers_disconnect_by_func (window, window_deactivated_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_dialog_up_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_dialog_down_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_show_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_map_event_cb, manager);
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
        g_signal_connect_object (window, "deactivated",
                                 G_CALLBACK (window_deactivated_cb), manager, 0);
        g_signal_connect_object (window, "dialog-up",
                                 G_CALLBACK (window_dialog_up_cb), manager, 0);
        g_signal_connect_object (window, "dialog-down",
                                 G_CALLBACK (window_dialog_down_cb), manager, 0);
        g_signal_connect_object (window, "show",
                                 G_CALLBACK (window_show_cb), manager, 0);
        g_signal_connect_object (window, "map_event",
                                 G_CALLBACK (window_map_event_cb), manager, 0);
}

static void
gs_manager_create_window (GSManager *manager,
                          GdkScreen *screen)
{
        GSWindow *window;
        int       n_monitors;
        int       i;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));
        g_return_if_fail (GDK_IS_SCREEN (screen));

        g_object_ref (manager);
        g_object_ref (screen);

        n_monitors = gdk_screen_get_n_monitors (screen);

        for (i = 0; i < n_monitors; i++) {
                window = gs_window_new (screen, i, manager->priv->lock_active);

                gs_window_set_user_switch_enabled (window, manager->priv->user_switch_enabled);
                gs_window_set_logout_enabled (window, manager->priv->logout_enabled);
                gs_window_set_logout_timeout (window, manager->priv->logout_timeout);
                gs_window_set_logout_command (window, manager->priv->logout_command);

                connect_window_signals (manager, window);

                manager->priv->windows = g_slist_append (manager->priv->windows, window);
        }

        g_object_unref (screen);
        g_object_unref (manager);
}

static void
gs_manager_create (GSManager *manager)
{
        GdkDisplay  *display;
        GSList      *l;
        int          n_screens, i;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        display = gdk_display_get_default ();

        n_screens = gdk_display_get_n_screens (display);

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_destroy (l->data);
        }
        g_slist_free (manager->priv->windows);

        for (i = 0; i < n_screens; i++)
                gs_manager_create_window (manager, gdk_display_get_screen (display, i));
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

static gboolean
gs_manager_activate (GSManager *manager)
{
        GdkDisplay *display;
        GdkScreen  *screen;
        GdkWindow  *root;
        gboolean    do_fade;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (manager->priv->active) {
                g_warning ("Trying to activate manager when already active");
                return FALSE;
        }

        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, NULL, NULL, NULL);
        root = gdk_screen_get_root_window (screen);

        if (! gs_grab_get_keyboard_and_mouse (root, screen)) {
                return FALSE;
        }

        if (! manager->priv->windows)
                gs_manager_create (GS_MANAGER (manager));

        manager->priv->active = TRUE;

        /* fade to black and show windows */
        do_fade = TRUE;
        if (do_fade) {
                GSFade *fade;

                fade = gs_fade_new ();
                gs_fade_set_timeout (fade, FADE_TIMEOUT);
                gs_fade_now (fade);
                show_windows (manager->priv->windows);
                g_usleep (500000);
                gs_fade_reset (fade);
                g_object_unref (fade);
        } else {
                show_windows (manager->priv->windows);
        }

        return TRUE;
}

static gboolean
gs_manager_deactivate (GSManager *manager)
{
        GSList *l;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->active) {
                g_warning ("Trying to deactivate a screensaver that is not active");
                return FALSE;
        }

        remove_timers (manager);

        gs_grab_release_keyboard_and_mouse ();

        for (l = manager->priv->jobs; l; l = l->next) {
                gs_job_stop (GS_JOB (l->data));
                g_object_unref (l->data);
        }
        g_slist_free (manager->priv->jobs);
        manager->priv->jobs = NULL;

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_destroy (l->data);
        }
        g_slist_free (manager->priv->windows);
        manager->priv->windows = NULL;

        manager->priv->active = FALSE;
        manager->priv->activate_time = 0;
        manager->priv->lock_active = FALSE;
        manager->priv->throttle_enabled = FALSE;

        return TRUE;
}

gboolean
gs_manager_set_active (GSManager *manager,
                       gboolean   active)
{
        gboolean res;

        if (active)
                res = gs_manager_activate (manager);
        else
                res = gs_manager_deactivate (manager);

        return res;
}

gboolean
gs_manager_get_active (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        return manager->priv->active;
}

void
gs_manager_request_unlock (GSManager *manager)
{
        GSList     *l;
        GdkDisplay *display;
        GdkScreen  *screen;
        int         screen_num;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (! manager->priv->active)
                return;

        /* Find the screen that contains the pointer */
        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, NULL, NULL, NULL);
        screen_num = gdk_screen_get_number (screen);

        /* Find the gs-window that is on that screen */
        for (l = manager->priv->windows; l; l = l->next) {
                int num = gdk_screen_get_number (GTK_WINDOW (l->data)->screen);
                if (num == screen_num) {
                        gs_window_request_unlock (GS_WINDOW (l->data));
                        break;
                }
        }
}
