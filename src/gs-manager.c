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
#include <gconf/gconf-client.h>
#include <gdk/gdk.h>

#include "gs-prefs.h"        /* for GSSaverMode */

#include "gs-manager.h"
#include "gs-window.h"
#include "gs-job.h"
#include "gs-grab.h"

static void gs_manager_class_init (GSManagerClass *klass);
static void gs_manager_init       (GSManager      *manager);
static void gs_manager_finalize   (GObject        *object);

#define GS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_MANAGER, GSManagerPrivate))

struct GSManagerPrivate
{
        GSList      *windows;
        GSList      *jobs;

        glong        lock_timeout;
        glong        cycle_timeout;
        glong        logout_timeout;

        guint        lock_enabled : 1;
        guint        logout_enabled : 1;
        guint        blank_active : 1;

        guint        dialog_up : 1;

        guint        lock_timeout_id;
        guint        cycle_timeout_id;

        time_t       blank_time;

        GSList      *themes;
        GSSaverMode  saver_mode;
        GConfClient *gconf_client;
};

enum {
        BLANKED,
        UNBLANKED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_LOCK_TIMEOUT,
        PROP_CYCLE_TIMEOUT,
        PROP_LOGOUT_TIMEOUT,
        PROP_BLANK_ACTIVE,
};

static GObjectClass *parent_class = NULL;
static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSManager, gs_manager, G_TYPE_OBJECT);

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
gs_manager_set_lock_enabled (GSManager *manager,
                             gboolean   lock_enabled)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->lock_enabled != lock_enabled) {
                GSList *l;

                manager->priv->lock_enabled = lock_enabled;
                for (l = manager->priv->windows; l; l = l->next)
                        gs_window_set_lock_enabled (l->data, lock_enabled);
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

static gboolean
enable_lock_timeout (GSManager *manager)
{
        gs_manager_set_lock_enabled (manager, TRUE);

        manager->priv->lock_timeout_id = 0;

        return FALSE;
}

void
gs_manager_set_lock_timeout (GSManager *manager,
                             glong      lock_timeout)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->lock_timeout != lock_timeout) {

                manager->priv->lock_timeout = lock_timeout;

                if (manager->priv->blank_active
                    && !manager->priv->lock_enabled
                    && (lock_timeout >= 0)) {

                        glong elapsed = (time (NULL) - manager->priv->blank_time) * 1000;

                        if (manager->priv->lock_timeout_id) {
                                g_source_remove (manager->priv->lock_timeout_id);
                                manager->priv->lock_timeout_id = 0;
                        }

                        if (elapsed >= lock_timeout) {
                                gs_manager_set_lock_enabled (manager, TRUE);
                        } else {
                                manager->priv->lock_timeout_id = g_timeout_add (lock_timeout - elapsed,
                                                                                (GSourceFunc)enable_lock_timeout,
                                                                                manager);
                        }
                } else {
                        gs_manager_set_lock_enabled (manager, FALSE);
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

        if (! manager->priv->blank_active)
                return FALSE;

        if (manager->priv->dialog_up)
                return FALSE;

        theme = select_theme (manager);

        for (l = manager->priv->jobs; l; l = l->next) {
                gs_job_stop (GS_JOB (l->data));
                gs_job_set_theme (GS_JOB (l->data), theme);

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

void
gs_manager_set_cycle_timeout (GSManager *manager,
                              glong      cycle_timeout)
{
        g_return_if_fail (GS_IS_MANAGER (manager));

        if (manager->priv->cycle_timeout != cycle_timeout) {

                manager->priv->cycle_timeout = cycle_timeout;

                if (manager->priv->blank_active && (cycle_timeout >= 0)) {
                        glong timeout;
                        glong elapsed = (time (NULL) - manager->priv->blank_time) * 1000;

                        if (manager->priv->cycle_timeout_id) {
                                g_source_remove (manager->priv->cycle_timeout_id);
                                manager->priv->cycle_timeout_id = 0;
                        }

                        if (elapsed >= cycle_timeout) {
                                timeout = 0;
                        } else {
                                timeout = cycle_timeout - elapsed;
                        }
                        manager->priv->cycle_timeout_id = g_timeout_add (timeout,
                                                                         (GSourceFunc)cycle_timeout,
                                                                         manager);
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
        case PROP_LOCK_ENABLED:
                gs_manager_set_lock_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOCK_TIMEOUT:
                gs_manager_set_lock_timeout (self, g_value_get_long (value));
                break;
        case PROP_LOGOUT_ENABLED:
                gs_manager_set_logout_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOGOUT_TIMEOUT:
                gs_manager_set_logout_timeout (self, g_value_get_long (value));
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
        case PROP_LOCK_ENABLED:
                g_value_set_boolean (value, self->priv->lock_enabled);
                break;
        case PROP_LOCK_TIMEOUT:
                g_value_set_long (value, self->priv->lock_timeout);
                break;
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
                break;
        case PROP_LOGOUT_TIMEOUT:
                g_value_set_long (value, self->priv->logout_timeout);
                break;
        case PROP_CYCLE_TIMEOUT:
                g_value_set_long (value, self->priv->cycle_timeout);
                break;
        case PROP_BLANK_ACTIVE:
                g_value_set_boolean (value, self->priv->blank_active);
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

        signals [BLANKED] =
                g_signal_new ("blanked",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, blanked),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [UNBLANKED] =
                g_signal_new ("unblanked",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, unblanked),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_object_class_install_property (object_class,
                                         PROP_BLANK_ACTIVE,
                                         g_param_spec_boolean ("blank-active",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_LOCK_ENABLED,
                                         g_param_spec_boolean ("lock-enabled",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
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
                                         PROP_LOGOUT_TIMEOUT,
                                         g_param_spec_long ("logout-timeout",
                                                            NULL,
                                                            NULL,
                                                            -1,
                                                            G_MAXLONG,
                                                            0,
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

        g_type_class_add_private (klass, sizeof (GSManagerPrivate));
}

static void
gs_manager_init (GSManager *manager)
{
        manager->priv = GS_MANAGER_GET_PRIVATE (manager);
        manager->priv->gconf_client = gconf_client_get_default ();
}

static void
remove_blank_timers (GSManager *manager)
{

        if (manager->priv->lock_timeout_id) {
                g_source_remove (manager->priv->lock_timeout_id);
                manager->priv->lock_timeout_id = 0;
        }

        if (manager->priv->cycle_timeout_id) {
                g_source_remove (manager->priv->cycle_timeout_id);
                manager->priv->cycle_timeout_id = 0;
        }
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

        if (manager->priv->gconf_client)
                g_object_unref (manager->priv->gconf_client);

        remove_blank_timers (manager);

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

        manager->priv->blank_active = FALSE;
        manager->priv->blank_time = 0;
        manager->priv->lock_enabled = FALSE;

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
window_unblank_idle (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);
        
        gs_manager_unblank (manager);

        return FALSE;
}

static void
window_unblanked_cb (GSWindow  *window,
                     GSManager *manager)
{
        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));
        
        g_idle_add ((GSourceFunc)window_unblank_idle, manager);
}

static void
window_dialog_up_cb (GSWindow  *window,
                     GSManager *manager)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        manager->priv->dialog_up = TRUE;

        for (l = manager->priv->jobs; l; l = l->next) {
                gs_job_suspend (GS_JOB (l->data), TRUE);
        }

}

static void
window_dialog_down_cb (GSWindow  *window,
                       GSManager *manager)
{
        GSList *l;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));

        for (l = manager->priv->jobs; l; l = l->next) {
                gs_job_suspend (GS_JOB (l->data), FALSE);
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

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);
        g_return_val_if_fail (window != NULL, FALSE);
        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, NULL, NULL, NULL);

        if (gs_window_get_screen (window) == screen)
                gs_grab_window (gs_window_get_gdk_window (window),
                                gs_window_get_screen (window),
                                FALSE);
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

        theme = select_theme (manager);
        job = gs_job_new_for_widget (GTK_WIDGET (window), theme);

        g_object_ref (job);
        gs_job_start (job);

        manager->priv->jobs = g_slist_append (manager->priv->jobs, job);

        manager->priv->blank_time = time (NULL);

        if (manager->priv->lock_timeout >= 0) {
                if (manager->priv->lock_timeout_id)
                        g_source_remove (manager->priv->lock_timeout_id);

                manager->priv->lock_timeout_id = g_timeout_add (manager->priv->lock_timeout,
                                                                (GSourceFunc)enable_lock_timeout,
                                                                manager);
        }

        if (manager->priv->cycle_timeout >= 10000) {
                if (manager->priv->cycle_timeout_id)
                        g_source_remove (manager->priv->cycle_timeout_id);

                manager->priv->cycle_timeout_id = g_timeout_add (manager->priv->cycle_timeout,
                                                                 (GSourceFunc)cycle_timeout,
                                                                 manager);
        }

        g_signal_emit (manager, signals [BLANKED], 0);
}

static void
gs_manager_create_window (GSManager *manager,
                          GdkScreen *screen)
{
        GSWindow *window;

        g_return_if_fail (manager != NULL);
        g_return_if_fail (GS_IS_MANAGER (manager));
        g_return_if_fail (GDK_IS_SCREEN (screen));

        g_object_ref (manager);
        g_object_ref (screen);
        window = gs_window_new (screen, manager->priv->lock_enabled);
        gs_window_set_logout_enabled (window, manager->priv->logout_enabled);
        gs_window_set_logout_timeout (window, manager->priv->logout_timeout);

        g_signal_connect_object (window, "unblanked",
                                 G_CALLBACK (window_unblanked_cb), manager, 0);
        g_signal_connect_object (window, "dialog-up",
                                 G_CALLBACK (window_dialog_up_cb), manager, 0);
        g_signal_connect_object (window, "dialog-down",
                                 G_CALLBACK (window_dialog_down_cb), manager, 0);
        g_signal_connect_object (window, "show",
                                 G_CALLBACK (window_show_cb), manager, 0);
        g_signal_connect_object (window, "map_event",
                                 G_CALLBACK (window_map_event_cb), manager, 0);

        manager->priv->windows = g_slist_append (manager->priv->windows, window);
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
gs_manager_new (int lock_timeout,
                int cycle_timeout)
{
        GObject *manager;
        gboolean lock_enabled;

        lock_enabled = (lock_timeout == 0) ? TRUE : FALSE;

        manager = g_object_new (GS_TYPE_MANAGER,
                                "lock_enabled", lock_enabled,
                                "lock_timeout",   lock_timeout,
                                "cycle_timeout",  cycle_timeout,
                                NULL);

        return GS_MANAGER (manager);
}

gboolean
gs_manager_blank (GSManager *manager)
{
        GdkDisplay *display;
        GdkScreen  *screen;
        GSList     *l;
        GdkWindow  *root;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (manager->priv->blank_active)
                return FALSE;

        manager->priv->blank_active = TRUE;

        display = gdk_display_get_default ();
        gdk_display_get_pointer (display, &screen, NULL, NULL, NULL);
        root = gdk_screen_get_root_window (screen);

        if (!gs_grab_get_keyboard_and_mouse (root, screen))
                return FALSE;

        if (! manager->priv->windows)
                gs_manager_create (GS_MANAGER (manager));

        for (l = manager->priv->windows; l; l = l->next) {
                gs_window_show (GS_WINDOW (l->data));
        }

        return TRUE;
}

gboolean
gs_manager_unblank (GSManager *manager)
{
        GSList *l;

        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        if (!manager->priv->blank_active)
                return FALSE;

        remove_blank_timers (manager);

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

        manager->priv->blank_active = FALSE;
        manager->priv->blank_time = 0;
        manager->priv->lock_enabled = FALSE;

        g_signal_emit (manager, signals [UNBLANKED], 0);

        return TRUE;
}

gboolean
gs_manager_is_blanked (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        return manager->priv->blank_active;
}
