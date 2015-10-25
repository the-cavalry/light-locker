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

#include "gs-manager.h"
#include "gs-window.h"
#include "gs-grab.h"
#include "gs-content.h"
#include "gs-debug.h"

static void gs_manager_class_init (GSManagerClass *klass);
static void gs_manager_init       (GSManager      *manager);
static void gs_manager_finalize   (GObject        *object);

#define GS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_MANAGER, GSManagerPrivate))

struct GSManagerPrivate
{
        GSList      *windows;

        /* Configuration */
        guint        lock_after;

        /* State */
        guint        active : 1;
        guint        visible : 1;
        guint        blank : 1;
        guint        closed : 1;
        guint        show_content : 1;

        guint        greeter_timeout_id;
        guint        lock_timeout_id;

        GSGrab      *grab;
};

enum {
        ACTIVATED,
        SWITCH_GREETER,
        LOCK,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_STATUS_MESSAGE,
        PROP_ACTIVE,
};

#define FADE_TIMEOUT 250

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSManager, gs_manager, G_TYPE_OBJECT)

static void
gs_manager_set_property (GObject            *object,
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
gs_manager_get_property (GObject            *object,
                         guint               prop_id,
                         GValue             *value,
                         GParamSpec         *pspec)
{
        GSManager *self;

        self = GS_MANAGER (object);

        switch (prop_id) {
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

        signals [SWITCH_GREETER] =
                g_signal_new ("switch-greeter",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, switch_greeter),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [LOCK] =
                g_signal_new ("lock",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSManagerClass, lock),
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

        g_type_class_add_private (klass, sizeof (GSManagerPrivate));
}

static void
gs_manager_init (GSManager *manager)
{
        manager->priv = GS_MANAGER_GET_PRIVATE (manager);

        manager->priv->grab = gs_grab_new ();

        /* Assume we are the visible session on start. */
        manager->priv->visible = TRUE;

        manager->priv->lock_after = 5;
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
#if GTK_CHECK_VERSION(3, 0, 0)
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        GdkDevice *pointer = gdk_device_manager_get_client_pointer (device_manager);
        gdk_device_get_position (pointer, &screen, &x, &y);
#else
        gdk_display_get_pointer (display, &screen, &x, &y, NULL);
#endif
        monitor = gdk_screen_get_monitor_at_point (screen, x, y);

        gdk_flush ();
        grabbed = FALSE;
        if (gs_window_get_screen (window) == screen
            && gs_window_get_monitor (window) == monitor) {
                gs_debug ("Moving grab to %p", window);
                gs_grab_move_to_window (manager->priv->grab,
                                        gs_window_get_gdk_window (window),
                                        gs_window_get_screen (window),
                                        TRUE);
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
window_map_event_cb (GSWindow  *window,
                     GdkEvent  *event,
                     GSManager *manager)
{
        gs_debug ("Handling window map_event event");

        /* FIXME: only emit signal once */
        g_signal_emit (manager, signals [ACTIVATED], 0);

        manager_maybe_grab_window (manager, window);

        return FALSE;
}

static void
content_draw_cb (GtkWidget *widget,
                 cairo_t   *cr,
                 GSManager *manager)
{
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
        cairo_paint (cr);

        if (manager->priv->show_content)
                content_draw (widget, cr);
}

#if !GTK_CHECK_VERSION(3, 0, 0)
static void
content_expose_cb (GtkWidget *widget,
                  GdkEvent  *event,
                  GSManager *manager)
{
        cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (widget));
        content_draw_cb (widget, cr, manager);
        cairo_destroy (cr);
}
#endif

static void
disconnect_window_signals (GSManager *manager,
                           GSWindow  *window)
{
        GtkWidget *drawing_area = gs_window_get_drawing_area (window);

#if GTK_CHECK_VERSION(3, 0, 0)
        g_signal_handlers_disconnect_by_func (drawing_area, content_draw_cb, manager);
#else
        g_signal_handlers_disconnect_by_func (drawing_area, content_expose_cb, manager);
#endif

        g_signal_handlers_disconnect_by_func (window, window_map_event_cb, manager);
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
        GtkWidget *drawing_area = gs_window_get_drawing_area (window);

#if GTK_CHECK_VERSION(3, 0, 0)
        g_signal_connect_object (drawing_area, "draw",
                                 G_CALLBACK (content_draw_cb), manager, 0);
#else
        g_signal_connect_object (drawing_area, "expose-event",
                                 G_CALLBACK (content_expose_cb), manager, 0);
#endif

        g_signal_connect_object (window, "destroy",
                                 G_CALLBACK (window_destroyed_cb), manager, 0);
        g_signal_connect_object (window, "map_event",
                                 G_CALLBACK (window_map_event_cb), manager, G_CONNECT_AFTER);
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

        window = gs_window_new (screen, monitor);

        connect_window_signals (manager, window);

        manager->priv->windows = g_slist_append (manager->priv->windows, window);

        if (manager->priv->active) {
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

                /* add more windows */
                for (i = n_windows; i < n_monitors; i++) {
                        gs_manager_create_window_for_monitor (manager, screen, i);
                }
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

                gdk_flush ();
                gdk_x11_ungrab_server ();
        }

	for (l = manager->priv->windows; l != NULL; l = l->next) {
		GdkScreen *this_screen;

		this_screen = gs_window_get_screen (GS_WINDOW (l->data));
		if (this_screen == screen) {
			gtk_widget_queue_resize (GTK_WIDGET (l->data));
		}
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

static gboolean
switch_greeter_timeout (GSManager *manager)
{
        manager->priv->greeter_timeout_id = 0;

        gs_debug ("Switch to greeter timeout");

        g_signal_emit (manager, signals [SWITCH_GREETER], 0);

        return FALSE;
}

static void
gs_manager_timed_switch (GSManager *manager)
{
        if (manager->priv->greeter_timeout_id != 0) {
                gs_debug ("Trying to start an active switch to greeter timer");
                return;
        }

        gs_debug ("Start switch to greeter timer");

        manager->priv->greeter_timeout_id = g_timeout_add_seconds (10,
                                                                   (GSourceFunc)switch_greeter_timeout,
                                                                   manager);
}

static void
gs_manager_stop_switch (GSManager *manager)
{
        if (manager->priv->greeter_timeout_id != 0) {
                gs_debug ("Stop switch to greeter timer");

                g_source_remove (manager->priv->greeter_timeout_id);
                manager->priv->greeter_timeout_id = 0;
        }
}

static gboolean
lock_timeout (GSManager *manager)
{
        manager->priv->lock_timeout_id = 0;

        gs_debug ("Lock timeout");

        g_signal_emit (manager, signals [LOCK], 0);

        return FALSE;
}

static void
gs_manager_timed_lock (GSManager *manager)
{
        if (manager->priv->lock_after == 0) {
                gs_debug ("Lock after disabled");
                return;
        }

        if (manager->priv->lock_timeout_id != 0) {
                gs_debug ("Trying to start an active lock timer");
                return;
        }

        gs_debug ("Start lock timer");

        manager->priv->lock_timeout_id = g_timeout_add_seconds (manager->priv->lock_after,
                                                                (GSourceFunc)lock_timeout,
                                                                manager);
}

static void
gs_manager_stop_lock (GSManager *manager)
{
        if (manager->priv->lock_timeout_id != 0) {
                gs_debug ("Stop lock timer");

                g_source_remove (manager->priv->lock_timeout_id);
                manager->priv->lock_timeout_id = 0;
        }
}

static void
gs_manager_finalize (GObject *object)
{
        GSManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_MANAGER (object));

        manager = GS_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        gs_grab_release (manager->priv->grab);

        gs_manager_destroy_windows (manager);

        manager->priv->active = FALSE;

        gs_manager_stop_switch (manager);
        gs_manager_stop_lock (manager);

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

static gboolean
gs_manager_activate (GSManager *manager)
{
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

        show_windows (manager->priv->windows);

        if (manager->priv->visible && !manager->priv->blank && !manager->priv->closed) {
                gs_manager_timed_switch (manager);
        }

        gs_manager_stop_lock (manager);

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

        gs_grab_release (manager->priv->grab);

        gs_manager_destroy_windows (manager);

        gs_manager_stop_switch (manager);

        if (manager->priv->blank) {
                gs_manager_timed_lock (manager);
        }

        /* reset state */
        manager->priv->active = FALSE;
        manager->priv->show_content = FALSE;

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
gs_manager_set_session_visible (GSManager *manager,
                                gboolean   visible)
{
        manager->priv->visible = visible;

        if (manager->priv->active && visible && !manager->priv->blank && !manager->priv->closed) {
                gs_manager_timed_switch (manager);
        } else {
                gs_manager_stop_switch (manager);
        }
}

gboolean
gs_manager_get_session_visible (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        return manager->priv->visible;
}

void
gs_manager_set_blank_screen (GSManager *manager,
                             gboolean   blank)
{
        manager->priv->blank = blank;

        if (!manager->priv->active && blank) {
                gs_manager_timed_lock (manager);
        } else {
                gs_manager_stop_lock (manager);
                if (manager->priv->active && manager->priv->visible && !manager->priv->closed) {
                        gs_manager_timed_switch (manager);
                }
        }
}

gboolean
gs_manager_get_blank_screen (GSManager *manager)
{
        g_return_val_if_fail (manager != NULL, FALSE);
        g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

        return manager->priv->blank;
}

void
gs_manager_set_lid_closed (GSManager *manager,
                           gboolean   closed)
{
        manager->priv->closed = closed;

        if (manager->priv->active && manager->priv->visible && !manager->priv->blank && !closed) {
                gs_manager_timed_switch (manager);
        } else {
                gs_manager_stop_switch (manager);
        }
}

void
gs_manager_set_lock_after (GSManager *manager,
                           guint      lock_after)
{
        manager->priv->lock_after = lock_after;
}

void
gs_manager_show_content (GSManager *manager)
{
        GSList *l;

        if (manager->priv->show_content)
                return;

        manager->priv->show_content = TRUE;

        for (l = manager->priv->windows; l; l = l->next) {
                gtk_widget_queue_draw (gs_window_get_drawing_area (l->data));
        }
}
