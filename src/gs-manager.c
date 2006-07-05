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
        GHashTable  *jobs;

        /* Policy */
        glong        lock_timeout;
        glong        cycle_timeout;
        glong        logout_timeout;

        guint        lock_enabled : 1;
        guint        logout_enabled : 1;
        guint        user_switch_enabled : 1;
        guint        throttled : 1;

        char        *logout_command;

        /* State */
        guint        active : 1;
        guint        lock_active : 1;

        guint        fading : 1;
        guint        dialog_up : 1;

        guint        lock_timeout_id;
        guint        cycle_timeout_id;

        time_t       activate_time;

        GSList      *themes;
        GSSaverMode  saver_mode;
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
        PROP_LOCK_TIMEOUT,
        PROP_CYCLE_TIMEOUT,
        PROP_LOGOUT_TIMEOUT,
        PROP_LOGOUT_COMMAND,
        PROP_ACTIVE,
        PROP_THROTTLED,
};

#define FADE_TIMEOUT 1000

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSManager, gs_manager, G_TYPE_OBJECT)

static void
manager_add_job_for_window (GSManager *manager,
                            GSWindow  *window,
                            GSJob     *job)
{
        if (manager->priv->jobs == NULL) {
                return;
        }

        g_hash_table_insert (manager->priv->jobs, window, job);
}

static const char *
select_theme (GSManager *manager)
{
        const char *theme = NULL;

        g_return_val_if_fail (manager != NULL, NULL);
        g_return_val_if_fail (GS_IS_MANAGER (manager), NULL);

        if (manager->priv->saver_mode == GS_MODE_BLANK_ONLY) {
                return NULL;
        }

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

static void
cycle_job (GSWindow  *window,
           GSJob     *job,
           GSManager *manager)
{
        const char *theme;

        theme = select_theme (manager);

        gs_job_stop (job);
        gs_job_set_theme (job, theme, NULL);
        gs_job_start (job);
}

static void
manager_cycle_jobs (GSManager *manager)
{
        if (manager->priv->jobs != NULL) {
                g_hash_table_foreach (manager->priv->jobs, (GHFunc) cycle_job, manager);
        }
}

static void
throttle_job (GSWindow  *window,
              GSJob     *job,
              GSManager *manager)
{
        if (manager->priv->throttled) {
                gs_job_stop (job);
        } else {
                gs_job_start (job);
        }
}

static void
manager_throttle_jobs (GSManager *manager)
{
        if (manager->priv->jobs != NULL) {
                g_hash_table_foreach (manager->priv->jobs, (GHFunc) throttle_job, manager);
        }
}

static void
resume_job (GSWindow  *window,
            GSJob     *job,
            GSManager *manager)
{
        if (gs_job_is_running (job)) {
                gs_job_suspend (job, FALSE);
        } else {
                gs_job_start (job);
        }
}

static void
manager_resume_jobs (GSManager *manager)
{
        if (manager->priv->jobs != NULL) {
                g_hash_table_foreach (manager->priv->jobs, (GHFunc) resume_job, manager);
        }
}

static void
suspend_job (GSWindow  *window,
             GSJob     *job,
             GSManager *manager)
{
        gs_job_suspend (job, TRUE);
}

static void
manager_suspend_jobs (GSManager *manager)
{
        if (manager->priv->jobs != NULL) {
                g_hash_table_foreach (manager->priv->jobs, (GHFunc) suspend_job, manager);
        }
}

static void
manager_stop_jobs (GSManager *manager)
{
        if (manager->priv->jobs != NULL) {
                g_hash_table_destroy (manager->priv->jobs);

        }
        manager->priv->jobs = NULL;
}

void
gs_manager_set_mode (GSManager  *manager,
                     GSSaverMode mode)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        manager->priv->saver_mode = mode;
}

static void
free_themes (GSManager *manager)
{
        if (manager->priv->themes) {
                g_slist_foreach (manager->priv->themes, (GFunc)g_free, NULL);
                g_slist_free (manager->priv->themes);
        }
}

void
gs_manager_set_themes (GSManager *manager,
                       GSList    *themes)
{
        GSList *l;

        g_return_if_fail (GS_IS_MANAGER (manager));

        free_themes (manager);
        manager->priv->themes = NULL;

        for (l = themes; l; l = l->next) {
                manager->priv->themes = g_slist_append (manager->priv->themes, g_strdup (l->data));
        }
}

void
gs_manager_set_throttled (GSManager *manager,
                          gboolean   throttled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->throttled != throttled) {
                GSList *l;

                manager->priv->throttled = throttled;

                if (! manager->priv->dialog_up) {

                        manager_throttle_jobs (manager);

                        for (l = manager->priv->windows; l; l = l->next) {
                                gs_window_clear (l->data);
                        }
                }
        }
}

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
                for (l = manager->priv->windows; l; l = l->next) {
                        gs_window_set_logout_enabled (l->data, logout_enabled);
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
        g_return_if_fail (GS_IS_MANAGER (manager));

        g_free (manager->priv->logout_command);

        if (command) {
                manager->priv->logout_command = g_strdup (command);
        } else {
                manager->priv->logout_command = NULL;
        }
}

gboolean
gs_manager_cycle (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->active) {
                return FALSE;
        }

        if (manager->priv->dialog_up) {
                return FALSE;
        }

        if (manager->priv->throttled) {
                return FALSE;
        }

        manager_cycle_jobs (manager);

        return TRUE;
}

static gboolean
cycle_timeout (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->dialog_up) {
                gs_manager_cycle (manager);
        }

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
        case PROP_THROTTLED:
                gs_manager_set_throttled (self, g_value_get_boolean (value));
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
        case PROP_THROTTLED:
                g_value_set_boolean (value, self->priv->throttled);
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
                                         PROP_THROTTLED,
                                         g_param_spec_boolean ("throttled",
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

        manager->priv->fade = gs_fade_new ();
        manager->priv->grab = gs_grab_new ();
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

        free_themes (manager);
        g_free (manager->priv->logout_command);

        remove_timers (manager);

        gs_grab_release (manager->priv->grab);

        manager_stop_jobs (manager);

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_destroy (l->data);
        }
        g_slist_free (manager->priv->windows);
        manager->priv->windows = NULL;

        manager->priv->active = FALSE;
        manager->priv->activate_time = 0;
        manager->priv->lock_enabled = FALSE;

        g_object_unref (manager->priv->fade);
        g_object_unref (manager->priv->grab);

        G_OBJECT_CLASS (gs_manager_parent_class)->finalize (object);
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

        g_signal_emit (manager, signals [AUTH_REQUEST_BEGIN], 0);

        manager->priv->dialog_up = TRUE;

        /* Move keyboard and mouse grabs so dialog can be used */
        gs_grab_move_to_window (manager->priv->grab,
                                gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);

        /* Release the pointer grab while dialog is up so that
           the dialog can be used.  We'll regrab it when the dialog goes down. */
        gs_grab_release_mouse (manager->priv->grab);

        /* Make all other windows insensitive so we don't get events */
        for (l = manager->priv->windows; l; l = l->next) {
                if (l->data != window) {
                        gtk_widget_set_sensitive (GTK_WIDGET (l->data), FALSE);
                }
        }

        if (! manager->priv->throttled) {
                gs_debug ("Suspending jobs");

                manager_suspend_jobs (manager);
        }
}

static void
window_dialog_down_cb (GSWindow  *window,
                       GSManager *manager)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        /* Regrab the mouse */
        gs_grab_move_to_window (manager->priv->grab,
                                gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);

        /* Make all windows sensitive so we get events */
        for (l = manager->priv->windows; l; l = l->next) {
                gtk_widget_set_sensitive (GTK_WIDGET (l->data), TRUE);
        }

        if (! manager->priv->throttled) {
                manager_resume_jobs (manager);
        }

        manager->priv->dialog_up = FALSE;

        g_signal_emit (manager, signals [AUTH_REQUEST_END], 0);
}

static void
manager_maybe_start_job_for_window (GSManager *manager,
                                    GSWindow  *window)
{
        GSJob *job;

        if (manager->priv->jobs == NULL) {
                return;
        }

        job = g_hash_table_lookup (manager->priv->jobs, window);

        if (job == NULL) {
                gs_debug ("Job not found for window");
                return;
        }

        if (! manager->priv->dialog_up) {
                if (! manager->priv->throttled) {
                        if (! gs_job_is_running (job)) {
                                gs_debug ("Starting job for window");
                                gs_job_start (job);
                        }
                }
        }
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
remove_unfade_idle (GSManager *manager)
{
        if (manager->priv->unfade_idle_id > 0) {
                g_source_remove (manager->priv->unfade_idle_id);
                manager->priv->unfade_idle_id = 0;
        }
}

static void
add_unfade_idle (GSManager *manager)
{
        remove_unfade_idle (manager);
        manager->priv->unfade_idle_id = g_idle_add ((GSourceFunc)unfade_idle, manager);
}

static gboolean
window_map_event_cb (GSWindow  *window,
                     GdkEvent  *event,
                     GSManager *manager)
{
        gs_debug ("Handling window map_event event");

        manager_maybe_grab_window (manager, window);

        manager_maybe_start_job_for_window (manager, window);

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
manager_show_window (GSManager *manager,
                     GSWindow  *window)
{
        GSJob      *job;
        const char *theme;

        job = gs_job_new_for_widget (GTK_WIDGET (window));

        theme = select_theme (manager);
        gs_job_set_theme (job, theme, NULL);

        manager_add_job_for_window (manager, window, job);

        manager->priv->activate_time = time (NULL);

        if (manager->priv->lock_timeout >= 0) {
                remove_lock_timer (manager);
                add_lock_timer (manager, manager->priv->lock_timeout);
        }

        if (manager->priv->cycle_timeout >= 10000) {
                remove_cycle_timer (manager);
                add_cycle_timer (manager, manager->priv->cycle_timeout);
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
disconnect_window_signals (GSManager *manager,
                           GSWindow  *window)
{
        g_signal_handlers_disconnect_by_func (window, window_deactivated_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_dialog_up_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_dialog_down_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_show_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_map_cb, manager);
        g_signal_handlers_disconnect_by_func (window, window_map_event_cb, manager);
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
        g_signal_connect_object (window, "deactivated",
                                 G_CALLBACK (window_deactivated_cb), manager, 0);
        g_signal_connect_object (window, "dialog-up",
                                 G_CALLBACK (window_dialog_up_cb), manager, 0);
        g_signal_connect_object (window, "dialog-down",
                                 G_CALLBACK (window_dialog_down_cb), manager, 0);
        g_signal_connect_object (window, "show",
                                 G_CALLBACK (window_show_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "map",
                                 G_CALLBACK (window_map_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "map_event",
                                 G_CALLBACK (window_map_event_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "unmap",
                                 G_CALLBACK (window_unmap_cb), manager, G_CONNECT_AFTER);
        g_signal_connect_object (window, "grab_broken_event",
                                 G_CALLBACK (window_grab_broken_cb), manager, G_CONNECT_AFTER);
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

        for (i = 0; i < n_screens; i++) {
                gs_manager_create_window (manager, gdk_display_get_screen (display, i));
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
remove_job (GSJob *job)
{
        if (job == NULL) {
                return;
        }

        gs_job_stop (job);
        g_object_unref (job);
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
                g_warning ("Trying to activate manager when already active");
                return FALSE;
        }

        res = gs_grab_grab_root (manager->priv->grab, FALSE);
        if (! res) {
                return FALSE;
        }

        if (! manager->priv->windows) {
                gs_manager_create (GS_MANAGER (manager));
        }

        manager->priv->jobs = g_hash_table_new_full (g_direct_hash,
                                                     g_direct_equal,
                                                     NULL,
                                                     (GDestroyNotify)remove_job);

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
        GSList *l;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (! manager->priv->active) {
                g_warning ("Trying to deactivate a screensaver that is not active");
                return FALSE;
        }

        remove_timers (manager);

        gs_grab_release (manager->priv->grab);

        manager_stop_jobs (manager);

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_destroy (l->data);
        }
        g_slist_free (manager->priv->windows);
        manager->priv->windows = NULL;

        manager->priv->active = FALSE;
        manager->priv->activate_time = 0;
        manager->priv->lock_active = FALSE;

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

void
gs_manager_request_unlock (GSManager *manager)
{
        GSList     *l;
        GdkDisplay *display;
        GdkScreen  *screen;
        int         screen_num;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (! manager->priv->active) {
                gs_debug ("Request unlock but manager is not active");
                return;
        }

        if (manager->priv->fading) {
                gs_debug ("Request unlock so finishing fade");
                gs_fade_finish (manager->priv->fade);
        }

        /* Find the screen that contains the pointer */
        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, NULL, NULL, NULL);
        screen_num = gdk_screen_get_number (screen);

        /* Find the gs-window that is on that screen */
        for (l = manager->priv->windows; l; l = l->next) {
                int num;

                num = gdk_screen_get_number (GTK_WINDOW (l->data)->screen);
                if (num == screen_num) {
                        gs_window_request_unlock (GS_WINDOW (l->data));
                        break;
                }
        }
}
