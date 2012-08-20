/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2008 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008-2011 Red Hat, Inc.
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
 */

#include "config.h"

#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include <gdesktop-enums.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-wall-clock.h>

#include "gs-window.h"
#include "gs-marshal.h"
#include "subprocs.h"
#include "gs-debug.h"

#ifdef HAVE_SHAPE_EXT
#include <X11/extensions/shape.h>
#endif

static void gs_window_class_init (GSWindowClass *klass);
static void gs_window_init       (GSWindow      *window);
static void gs_window_finalize   (GObject       *object);

static gboolean popup_dialog_idle (GSWindow *window);
static void gs_window_dialog_finish (GSWindow *window);
static void remove_command_watches (GSWindow *window);

enum {
        DIALOG_RESPONSE_CANCEL,
        DIALOG_RESPONSE_OK
};

#define MAX_QUEUED_EVENTS 16
#define INFO_BAR_SECONDS 30

#define GS_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WINDOW, GSWindowPrivate))

struct GSWindowPrivate
{
        int        monitor;

        GdkRectangle geometry;
        guint      obscured : 1;
        guint      dialog_up : 1;

        guint      lock_enabled : 1;
        guint      user_switch_enabled : 1;
        guint      logout_enabled : 1;
        guint      keyboard_enabled : 1;

        guint64    logout_timeout;
        char      *logout_command;
        char      *keyboard_command;
        char      *status_message;

        GtkWidget *vbox;
        GtkWidget *panel;
        GtkWidget *clock;
        GtkWidget *name_label;
        GtkWidget *drawing_area;
        GtkWidget *lock_box;
        GtkWidget *lock_socket;
        GtkWidget *keyboard_socket;
        GtkWidget *info_bar;
        GtkWidget *info_content;

        cairo_surface_t *background_surface;

        guint      popup_dialog_idle_id;

        guint      dialog_map_signal_id;
        guint      dialog_unmap_signal_id;
        guint      dialog_response_signal_id;

        guint      watchdog_timer_id;
        guint      info_bar_timer_id;

        gint       lock_pid;
        gint       lock_watch_id;
        gint       dialog_response;
        gboolean   dialog_quit_requested;
        gboolean   dialog_shake_in_progress;

        gint       keyboard_pid;
        gint       keyboard_watch_id;

        GList     *key_events;

        gdouble    last_x;
        gdouble    last_y;

        GTimer    *timer;

        GnomeWallClock *clock_tracker;

#ifdef HAVE_SHAPE_EXT
        int        shape_event_base;
#endif
};

enum {
        ACTIVITY,
        DEACTIVATED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_OBSCURED,
        PROP_DIALOG_UP,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_KEYBOARD_ENABLED,
        PROP_KEYBOARD_COMMAND,
        PROP_LOGOUT_COMMAND,
        PROP_LOGOUT_TIMEOUT,
        PROP_MONITOR,
        PROP_STATUS_MESSAGE
};

static guint           signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSWindow, gs_window, GTK_TYPE_WINDOW)

static void
set_invisible_cursor (GdkWindow *window,
                      gboolean   invisible)
{
        GdkCursor *cursor = NULL;

        if (invisible) {
                cursor = gdk_cursor_new (GDK_BLANK_CURSOR);
        }

        gdk_window_set_cursor (window, cursor);

        if (cursor) {
                gdk_cursor_unref (cursor);
        }
}

/* derived from tomboy */
static void
gs_window_override_user_time (GSWindow *window)
{
        guint32 ev_time = gtk_get_current_event_time ();

        if (ev_time == 0) {
                gint ev_mask = gtk_widget_get_events (GTK_WIDGET (window));
                if (!(ev_mask & GDK_PROPERTY_CHANGE_MASK)) {
                        gtk_widget_add_events (GTK_WIDGET (window),
                                               GDK_PROPERTY_CHANGE_MASK);
                }

                /*
                 * NOTE: Last resort for D-BUS or other non-interactive
                 *       openings.  Causes roundtrip to server.  Lame.
                 */
                ev_time = gdk_x11_get_server_time (gtk_widget_get_window (GTK_WIDGET (window)));
        }

        gdk_x11_window_set_user_time (gtk_widget_get_window (GTK_WIDGET (window)), ev_time);
}

static void
gs_window_reset_background_surface (GSWindow *window)
{
        cairo_pattern_t *pattern;
        pattern = cairo_pattern_create_for_surface (window->priv->background_surface);
        gdk_window_set_background_pattern (gtk_widget_get_window (GTK_WIDGET (window)),
                                           pattern);
        cairo_pattern_destroy (pattern);
        gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gs_window_set_background_surface (GSWindow        *window,
                                  cairo_surface_t *surface)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->background_surface != NULL) {
                cairo_surface_destroy (window->priv->background_surface);
        }

        if (surface != NULL) {
                window->priv->background_surface = cairo_surface_reference (surface);
                gs_window_reset_background_surface (window);
        }
}

static void
gs_window_clear_to_background_surface (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (!gtk_widget_get_visible (GTK_WIDGET (window))) {
                return;
        }

        if (window->priv->background_surface == NULL) {
                return;
        }

        gs_debug ("Clearing window to background pixmap");
        gs_window_reset_background_surface (window);
}

static void
clear_widget (GtkWidget *widget)
{
        GdkRGBA rgba = { 0.0, 0.0, 0.0, 1.0 };

        if (!gtk_widget_get_realized (widget))
                return;

        gtk_widget_override_background_color (widget, GTK_STATE_FLAG_NORMAL, &rgba);
        gtk_widget_queue_draw (GTK_WIDGET (widget));
}

void
gs_window_clear (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        clear_widget (GTK_WIDGET (window));
        clear_widget (window->priv->drawing_area);
}

static cairo_region_t *
get_outside_region (GSWindow *window)
{
        int             i;
        cairo_region_t *region;

        region = cairo_region_create ();
        for (i = 0; i < window->priv->monitor; i++) {
                GdkRectangle geometry;
                cairo_rectangle_int_t rectangle;

                gdk_screen_get_monitor_geometry (gtk_window_get_screen (GTK_WINDOW (window)),
                                                   i, &geometry);
                rectangle.x = geometry.x;
                rectangle.y = geometry.y;
                rectangle.width = geometry.width;
                rectangle.height = geometry.height;
                cairo_region_union_rectangle (region, &rectangle);
        }

        return region;
}

static void
update_geometry (GSWindow *window)
{
        GdkRectangle    geometry;
        cairo_region_t *outside_region;
        cairo_region_t *monitor_region;

        outside_region = get_outside_region (window);

        gdk_screen_get_monitor_geometry (gtk_window_get_screen (GTK_WINDOW (window)),
                                         window->priv->monitor,
                                         &geometry);
        gs_debug ("got geometry for monitor %d: x=%d y=%d w=%d h=%d",
                  window->priv->monitor,
                  geometry.x,
                  geometry.y,
                  geometry.width,
                  geometry.height);
        monitor_region = cairo_region_create_rectangle ((const cairo_rectangle_int_t *)&geometry);
        cairo_region_subtract (monitor_region, outside_region);
        cairo_region_destroy (outside_region);

        cairo_region_get_extents (monitor_region, (cairo_rectangle_int_t *)&geometry);
        cairo_region_destroy (monitor_region);

        gs_debug ("using geometry for monitor %d: x=%d y=%d w=%d h=%d",
                  window->priv->monitor,
                  geometry.x,
                  geometry.y,
                  geometry.width,
                  geometry.height);

        window->priv->geometry.x = geometry.x;
        window->priv->geometry.y = geometry.y;
        window->priv->geometry.width = geometry.width;
        window->priv->geometry.height = geometry.height;
}

static void
screen_size_changed (GdkScreen *screen,
                     GSWindow  *window)
{
        gs_debug ("Got screen size changed signal");
        gtk_widget_queue_resize (GTK_WIDGET (window));
}

/* copied from panel-toplevel.c */
static void
gs_window_move_resize_window (GSWindow *window,
                              gboolean  move,
                              gboolean  resize)
{
        GtkWidget *widget;
        GdkScreen *screen;
        int        monitor;
        int        primary_monitor;

        widget = GTK_WIDGET (window);

        g_assert (gtk_widget_get_realized (widget));

        gs_debug ("Move and/or resize window on monitor %d: x=%d y=%d w=%d h=%d",
                  window->priv->monitor,
                  window->priv->geometry.x,
                  window->priv->geometry.y,
                  window->priv->geometry.width,
                  window->priv->geometry.height);

        if (move && resize) {
                gdk_window_move_resize (gtk_widget_get_window (widget),
                                        window->priv->geometry.x,
                                        window->priv->geometry.y,
                                        window->priv->geometry.width,
                                        window->priv->geometry.height);
        } else if (move) {
                gdk_window_move (gtk_widget_get_window (widget),
                                 window->priv->geometry.x,
                                 window->priv->geometry.y);
        } else if (resize) {
                gdk_window_resize (gtk_widget_get_window (widget),
                                   window->priv->geometry.width,
                                   window->priv->geometry.height);
        }

        screen = gtk_widget_get_screen (widget);
        monitor = gdk_screen_get_monitor_at_window (screen,
                                                    gtk_widget_get_window (widget));
        primary_monitor = gdk_screen_get_primary_monitor (screen);
        gtk_widget_set_visible (window->priv->panel, monitor == primary_monitor);
}

static void
gs_window_real_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (gs_window_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->unrealize (widget);
        }
}

/* copied from gdk */
extern char **environ;

static gchar **
spawn_make_environment_for_screen (GdkScreen  *screen,
                                   gchar     **envp)
{
        gchar **retval = NULL;
        gchar  *display_name;
        gint    display_index = -1;
        gint    i, env_len;

        g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

        if (envp == NULL)
                envp = environ;

        for (env_len = 0; envp[env_len]; env_len++)
                if (strncmp (envp[env_len], "DISPLAY", strlen ("DISPLAY")) == 0)
                        display_index = env_len;

        retval = g_new (char *, env_len + 1);
        retval[env_len] = NULL;

        display_name = gdk_screen_make_display_name (screen);

        for (i = 0; i < env_len; i++)
                if (i == display_index)
                        retval[i] = g_strconcat ("DISPLAY=", display_name, NULL);
                else
                        retval[i] = g_strdup (envp[i]);

        g_assert (i == env_len);

        g_free (display_name);

        return retval;
}

static void
gs_window_real_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gs_window_parent_class)->realize) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->realize (widget);
        }

        gs_window_override_user_time (GS_WINDOW (widget));

        gs_window_move_resize_window (GS_WINDOW (widget), TRUE, TRUE);

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (screen_size_changed),
                          widget);
}

/* every so often we should raise the window in case
   another window has somehow gotten on top */
static gboolean
watchdog_timer (GSWindow *window)
{
        GtkWidget *widget = GTK_WIDGET (window);

        gdk_window_focus (gtk_widget_get_window (widget), GDK_CURRENT_TIME);

        return TRUE;
}

static void
remove_watchdog_timer (GSWindow *window)
{
        if (window->priv->watchdog_timer_id != 0) {
                g_source_remove (window->priv->watchdog_timer_id);
                window->priv->watchdog_timer_id = 0;
        }
}

static void
add_watchdog_timer (GSWindow *window,
                    glong     timeout)
{
        window->priv->watchdog_timer_id = g_timeout_add_seconds (timeout,
                                                                 (GSourceFunc)watchdog_timer,
                                                                 window);
}

static void
remove_popup_dialog_idle (GSWindow *window)
{
        if (window->priv->popup_dialog_idle_id != 0) {
                g_source_remove (window->priv->popup_dialog_idle_id);
                window->priv->popup_dialog_idle_id = 0;
        }
}

static void
add_popup_dialog_idle (GSWindow *window)
{
        window->priv->popup_dialog_idle_id = g_idle_add ((GSourceFunc)popup_dialog_idle, window);
}

static gboolean
emit_deactivated_idle (GSWindow *window)
{
        g_signal_emit (window, signals [DEACTIVATED], 0);

        return FALSE;
}

static void
add_emit_deactivated_idle (GSWindow *window)
{
        g_idle_add ((GSourceFunc)emit_deactivated_idle, window);
}

static void
gs_window_raise (GSWindow *window)
{
        GdkWindow *win;

        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Raising screensaver window");

        win = gtk_widget_get_window (GTK_WIDGET (window));

        gdk_window_raise (win);
}

static gboolean
x11_window_is_ours (Window window)
{
        GdkWindow *gwindow;
        gboolean   ret;

        ret = FALSE;

        gwindow = gdk_x11_window_lookup_for_display (gdk_display_get_default (), window);
        if (gwindow && (window != GDK_ROOT_WINDOW ())) {
                ret = TRUE;
        }

        return ret;
}

#ifdef HAVE_SHAPE_EXT
static void
unshape_window (GSWindow *window)
{
        gdk_window_shape_combine_region (gtk_widget_get_window (GTK_WIDGET (window)),
                                         NULL,
                                         0,
                                         0);
}
#endif

static void
gs_window_xevent (GSWindow  *window,
                  GdkXEvent *xevent)
{
        XEvent *ev;

        ev = xevent;

        /* MapNotify is used to tell us when new windows are mapped.
           ConfigureNofify is used to tell us when windows are raised. */
        switch (ev->xany.type) {
        case MapNotify:
                {
                        XMapEvent *xme = &ev->xmap;

                        if (! x11_window_is_ours (xme->window)) {
                                gs_window_raise (window);
                        } else {
                                gs_debug ("not raising our windows");
                        }

                        break;
                }
        case ConfigureNotify:
                {
                        XConfigureEvent *xce = &ev->xconfigure;

                        if (! x11_window_is_ours (xce->window)) {
                                gs_window_raise (window);
                        } else {
                                gs_debug ("not raising our windows");
                        }

                        break;
                }
        default:
                /* extension events */
#ifdef HAVE_SHAPE_EXT
                if (ev->xany.type == (window->priv->shape_event_base + ShapeNotify)) {
                        /*XShapeEvent *xse = (XShapeEvent *) ev;*/
                        unshape_window (window);
                        gs_debug ("Window was reshaped!");
                }
#endif

                break;
        }

}

static GdkFilterReturn
xevent_filter (GdkXEvent *xevent,
               GdkEvent  *event,
               GSWindow  *window)
{
        gs_window_xevent (window, xevent);

        return GDK_FILTER_CONTINUE;
}

static void
select_popup_events (void)
{
        XWindowAttributes attr;
        unsigned long     events;

        gdk_error_trap_push ();

        memset (&attr, 0, sizeof (attr));
        XGetWindowAttributes (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_ROOT_WINDOW (), &attr);

        events = SubstructureNotifyMask | attr.your_event_mask;
        XSelectInput (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_ROOT_WINDOW (), events);

        gdk_error_trap_pop_ignored ();
}

static void
window_select_shape_events (GSWindow *window)
{
#ifdef HAVE_SHAPE_EXT
        unsigned long events;
        int           shape_error_base;

        gdk_error_trap_push ();

        if (XShapeQueryExtension (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &window->priv->shape_event_base, &shape_error_base)) {
                events = ShapeNotifyMask;
                XShapeSelectInput (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_WINDOW_XID (gtk_widget_get_window (GTK_WIDGET (window))), events);
        }

        gdk_error_trap_pop_ignored ();
#endif
}

static void
gs_window_real_show (GtkWidget *widget)
{
        GSWindow *window;

        if (GTK_WIDGET_CLASS (gs_window_parent_class)->show) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->show (widget);
        }

        gs_window_clear (GS_WINDOW (widget));

        set_invisible_cursor (gtk_widget_get_window (widget), TRUE);

        window = GS_WINDOW (widget);
        if (window->priv->timer) {
                g_timer_destroy (window->priv->timer);
        }
        window->priv->timer = g_timer_new ();

        remove_watchdog_timer (window);
        add_watchdog_timer (window, 30);

        select_popup_events ();
        window_select_shape_events (window);
        gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, window);
}

static void
set_info_text_and_icon (GSWindow   *window,
                        const char *icon_stock_id,
                        const char *primary_text,
                        const char *secondary_text)
{
        GtkWidget *content_area;
        GtkWidget *hbox_content;
        GtkWidget *image;
        GtkWidget *vbox;
        gchar *primary_markup;
        gchar *secondary_markup;
        GtkWidget *primary_label;
        GtkWidget *secondary_label;

        hbox_content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_show (hbox_content);

        image = gtk_image_new_from_stock (icon_stock_id, GTK_ICON_SIZE_DIALOG);
        gtk_widget_show (image);
        gtk_box_pack_start (GTK_BOX (hbox_content), image, FALSE, FALSE, 0);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0);

        vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_show (vbox);
        gtk_box_pack_start (GTK_BOX (hbox_content), vbox, FALSE, FALSE, 0);

        primary_markup = g_strdup_printf ("<b>%s</b>", primary_text);
        primary_label = gtk_label_new (primary_markup);
        g_free (primary_markup);
        gtk_widget_show (primary_label);
        gtk_box_pack_start (GTK_BOX (vbox), primary_label, TRUE, TRUE, 0);
        gtk_label_set_use_markup (GTK_LABEL (primary_label), TRUE);
        gtk_label_set_line_wrap (GTK_LABEL (primary_label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (primary_label), 0, 0.5);

        if (secondary_text != NULL) {
                secondary_markup = g_strdup_printf ("<small>%s</small>",
                                                    secondary_text);
                secondary_label = gtk_label_new (secondary_markup);
                g_free (secondary_markup);
                gtk_widget_show (secondary_label);
                gtk_box_pack_start (GTK_BOX (vbox), secondary_label, TRUE, TRUE, 0);
                gtk_label_set_use_markup (GTK_LABEL (secondary_label), TRUE);
                gtk_label_set_line_wrap (GTK_LABEL (secondary_label), TRUE);
                gtk_misc_set_alignment (GTK_MISC (secondary_label), 0, 0.5);
        }

        /* remove old content */
        content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (window->priv->info_bar));
        if (window->priv->info_content != NULL) {
                gtk_container_remove (GTK_CONTAINER (content_area), window->priv->info_content);
        }
        gtk_box_pack_start (GTK_BOX (content_area),
                            hbox_content,
                            TRUE, FALSE, 0);
        window->priv->info_content = hbox_content;
}

static gboolean
info_bar_timeout (GSWindow *window)
{
        window->priv->info_bar_timer_id = 0;
        gtk_widget_hide (window->priv->info_bar);
        return FALSE;
}

void
gs_window_show_message (GSWindow   *window,
                        const char *summary,
                        const char *body,
                        const char *icon)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        set_info_text_and_icon (window,
                                icon,
                                summary,
                                body);
        gtk_widget_show (window->priv->info_bar);

        if (window->priv->info_bar_timer_id > 0) {
                g_source_remove (window->priv->info_bar_timer_id);
        }

        window->priv->info_bar_timer_id = g_timeout_add_seconds (INFO_BAR_SECONDS,
                                                                 (GSourceFunc)info_bar_timeout,
                                                                 window);
}

void
gs_window_show (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gtk_widget_show (GTK_WIDGET (window));
}

static void
gs_window_real_hide (GtkWidget *widget)
{
        GSWindow *window;

        window = GS_WINDOW (widget);

        gdk_window_remove_filter (NULL, (GdkFilterFunc)xevent_filter, window);

        remove_watchdog_timer (window);

        if (GTK_WIDGET_CLASS (gs_window_parent_class)->hide) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->hide (widget);
        }
}

void
gs_window_destroy (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_window_cancel_unlock_request (window);

        gtk_widget_destroy (GTK_WIDGET (window));
}

GdkWindow *
gs_window_get_gdk_window (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

        return gtk_widget_get_window (GTK_WIDGET (window));
}

GtkWidget *
gs_window_get_drawing_area (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

        return window->priv->drawing_area;
}

/* just for debugging */
static gboolean
error_watch (GIOChannel   *source,
             GIOCondition  condition,
             gpointer      data)
{
        gboolean finished = FALSE;

        if (condition & G_IO_IN) {
                GIOStatus status;
                GError   *error = NULL;
                char     *line;

                line = NULL;
                status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

                switch (status) {
                case G_IO_STATUS_NORMAL:
                        gs_debug ("command error output: %s", line);
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        gs_debug ("Error reading from child: %s\n", error->message);
                        g_error_free (error);
                        return FALSE;
                case G_IO_STATUS_AGAIN:
                default:
                        break;
                }
                g_free (line);
        } else if (condition & G_IO_HUP) {
                finished = TRUE;
        }

        if (finished) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
spawn_on_window (GSWindow *window,
                 char     *command,
                 int      *pid,
                 GIOFunc   watch_func,
                 gpointer  user_data,
                 gint     *watch_id)
{
        int         argc;
        char      **argv;
        char      **envp;
        GError     *error;
        gboolean    result;
        GIOChannel *channel;
        int         standard_output;
        int         standard_error;
        int         child_pid;
        int         id;

        error = NULL;
        if (! g_shell_parse_argv (command, &argc, &argv, &error)) {
                gs_debug ("Could not parse command: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        envp = spawn_make_environment_for_screen (gtk_window_get_screen (GTK_WINDOW (window)), NULL);

        error = NULL;
        result = g_spawn_async_with_pipes (NULL,
                                           argv,
                                           envp,
                                           G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                           NULL,
                                           NULL,
                                           &child_pid,
                                           NULL,
                                           &standard_output,
                                           &standard_error,
                                           &error);

        if (! result) {
                gs_debug ("Could not start command '%s': %s", command, error->message);
                g_error_free (error);
                g_strfreev (argv);
                return FALSE;
        }

        if (pid != NULL) {
                *pid = child_pid;
        } else {
                g_spawn_close_pid (child_pid);
        }

        /* output channel */
        channel = g_io_channel_unix_new (standard_output);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        id = g_io_add_watch (channel,
                             G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                             watch_func,
                             user_data);
        if (watch_id != NULL) {
                *watch_id = id;
        }
        g_io_channel_unref (channel);

        /* error channel */
        channel = g_io_channel_unix_new (standard_error);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        id = g_io_add_watch (channel,
                             G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                             error_watch,
                             NULL);
        g_io_channel_unref (channel);

        g_strfreev (argv);
        g_strfreev (envp);

        return result;
}

static void
lock_plug_added (GtkWidget *widget,
                 GSWindow  *window)
{
        gtk_widget_show (widget);
}

static gboolean
lock_plug_removed (GtkWidget *widget,
                   GSWindow  *window)
{
        gtk_widget_hide (widget);
        gtk_container_remove (GTK_CONTAINER (window->priv->vbox), GTK_WIDGET (window->priv->lock_box));
        window->priv->lock_box = NULL;

        return TRUE;
}

static void
keyboard_plug_added (GtkWidget *widget,
                     GSWindow  *window)
{
        gtk_widget_show (widget);
}

static gboolean
keyboard_plug_removed (GtkWidget *widget,
                       GSWindow  *window)
{
        gtk_widget_hide (widget);
        gtk_container_remove (GTK_CONTAINER (window->priv->vbox), GTK_WIDGET (window->priv->keyboard_socket));

        return TRUE;
}

static void
keyboard_socket_destroyed (GtkWidget *widget,
                           GSWindow  *window)
{
        g_signal_handlers_disconnect_by_func (widget, keyboard_socket_destroyed, window);
        g_signal_handlers_disconnect_by_func (widget, keyboard_plug_added, window);
        g_signal_handlers_disconnect_by_func (widget, keyboard_plug_removed, window);

        window->priv->keyboard_socket = NULL;
}

static void
forward_key_events (GSWindow *window)
{
        window->priv->key_events = g_list_reverse (window->priv->key_events);

        while (window->priv->key_events != NULL) {
                GdkEventKey *event = window->priv->key_events->data;

                gtk_window_propagate_key_event (GTK_WINDOW (window), event);

                gdk_event_free ((GdkEvent *)event);
                window->priv->key_events = g_list_delete_link (window->priv->key_events,
                                                               window->priv->key_events);
        }
}

static void
remove_key_events (GSWindow *window)
{
        window->priv->key_events = g_list_reverse (window->priv->key_events);

        while (window->priv->key_events) {
                GdkEventKey *event = window->priv->key_events->data;

                gdk_event_free ((GdkEvent *)event);
                window->priv->key_events = g_list_delete_link (window->priv->key_events,
                                                               window->priv->key_events);
        }
}

static void
lock_socket_show (GtkWidget *widget,
                  GSWindow  *window)
{
        gtk_widget_child_focus (window->priv->lock_socket, GTK_DIR_TAB_FORWARD);

        /* send queued events to the dialog */
        forward_key_events (window);
}

static void
lock_socket_destroyed (GtkWidget *widget,
                       GSWindow  *window)
{
        g_signal_handlers_disconnect_by_func (widget, lock_socket_show, window);
        g_signal_handlers_disconnect_by_func (widget, lock_socket_destroyed, window);
        g_signal_handlers_disconnect_by_func (widget, lock_plug_added, window);
        g_signal_handlers_disconnect_by_func (widget, lock_plug_removed, window);

        window->priv->lock_socket = NULL;
}

static void
create_keyboard_socket (GSWindow *window,
                        guint32   id)
{
        int height;

        height = (gdk_screen_get_height (gtk_widget_get_screen (GTK_WIDGET (window)))) / 4;

        window->priv->keyboard_socket = gtk_socket_new ();
        gtk_widget_set_size_request (window->priv->keyboard_socket, -1, height);

        g_signal_connect (window->priv->keyboard_socket, "destroy",
                          G_CALLBACK (keyboard_socket_destroyed), window);
        g_signal_connect (window->priv->keyboard_socket, "plug_added",
                          G_CALLBACK (keyboard_plug_added), window);
        g_signal_connect (window->priv->keyboard_socket, "plug_removed",
                          G_CALLBACK (keyboard_plug_removed), window);
        gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->keyboard_socket, FALSE, FALSE, 0);
        gtk_socket_add_id (GTK_SOCKET (window->priv->keyboard_socket), id);
}

/* adapted from gspawn.c */
static int
wait_on_child (int pid)
{
        int status;

 wait_again:
        if (waitpid (pid, &status, 0) < 0) {
                if (errno == EINTR) {
                        goto wait_again;
                } else if (errno == ECHILD) {
                        ; /* do nothing, child already reaped */
                } else {
                        gs_debug ("waitpid () should not fail in 'GSWindow'");
                }
        }

        return status;
}

static void
kill_keyboard_command (GSWindow *window)
{
        if (window->priv->keyboard_pid > 0) {
                signal_pid (window->priv->keyboard_pid, SIGTERM);
        }
}

static void
kill_dialog_command (GSWindow *window)
{
        /* If a dialog is up we need to signal it
           and wait on it */
        if (window->priv->lock_pid > 0) {
                signal_pid (window->priv->lock_pid, SIGTERM);
        }
}

static void
keyboard_command_finish (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        /* send a signal just in case */
        kill_keyboard_command (window);

        gs_debug ("Keyboard finished");

        if (window->priv->keyboard_pid > 0) {
                wait_on_child (window->priv->keyboard_pid);

                g_spawn_close_pid (window->priv->keyboard_pid);
                window->priv->keyboard_pid = 0;
        }
}

static gboolean
keyboard_command_watch (GIOChannel   *source,
                        GIOCondition  condition,
                        GSWindow     *window)
{
        gboolean finished = FALSE;

        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        if (condition & G_IO_IN) {
                GIOStatus status;
                GError   *error = NULL;
                char     *line;

                line = NULL;
                status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

                switch (status) {
                case G_IO_STATUS_NORMAL:
                        {
                                guint32 id;
                                char    c;
                                gs_debug ("keyboard command output: %s", line);
                                if (1 == sscanf (line, " %" G_GUINT32_FORMAT " %c", &id, &c)) {
                                        create_keyboard_socket (window, id);
                                }
                        }
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        gs_debug ("Error reading from child: %s\n", error->message);
                        g_error_free (error);
                        return FALSE;
                case G_IO_STATUS_AGAIN:
                default:
                        break;
                }

                g_free (line);
        } else if (condition & G_IO_HUP) {
                finished = TRUE;
        }

        if (finished) {
                window->priv->keyboard_watch_id = 0;
                keyboard_command_finish (window);
                return FALSE;
        }

        return TRUE;
}

static void
embed_keyboard (GSWindow *window)
{
        gboolean res;

        if (! window->priv->keyboard_enabled
            || window->priv->keyboard_command == NULL)
                return;

        gs_debug ("Adding embedded keyboard widget");

        /* FIXME: verify command is safe */

        gs_debug ("Running command: %s", window->priv->keyboard_command);

        res = spawn_on_window (window,
                               window->priv->keyboard_command,
                               &window->priv->keyboard_pid,
                               (GIOFunc)keyboard_command_watch,
                               window,
                               &window->priv->keyboard_watch_id);
        if (! res) {
                gs_debug ("Could not start command: %s", window->priv->keyboard_command);
        }
}

static void
create_lock_socket (GSWindow *window,
                    guint32   id)
{
        window->priv->lock_socket = gtk_socket_new ();
        window->priv->lock_box = gtk_alignment_new (0.5, 0.5, 0, 0);
        gtk_widget_show (window->priv->lock_box);
        gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->lock_box, TRUE, TRUE, 0);

        gtk_container_add (GTK_CONTAINER (window->priv->lock_box), window->priv->lock_socket);

        g_signal_connect (window->priv->lock_socket, "show",
                          G_CALLBACK (lock_socket_show), window);
        g_signal_connect (window->priv->lock_socket, "destroy",
                          G_CALLBACK (lock_socket_destroyed), window);
        g_signal_connect (window->priv->lock_socket, "plug_added",
                          G_CALLBACK (lock_plug_added), window);
        g_signal_connect (window->priv->lock_socket, "plug_removed",
                          G_CALLBACK (lock_plug_removed), window);

        gtk_socket_add_id (GTK_SOCKET (window->priv->lock_socket), id);

        if (window->priv->keyboard_enabled) {
                embed_keyboard (window);
        }
}

static void
gs_window_dialog_finish (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Dialog finished");

        /* make sure we finish the keyboard thing too */
        keyboard_command_finish (window);

        /* send a signal just in case */
        kill_dialog_command (window);

        if (window->priv->lock_pid > 0) {
                wait_on_child (window->priv->lock_pid);

                g_spawn_close_pid (window->priv->lock_pid);
                window->priv->lock_pid = 0;
        }

        /* remove events for the case were we failed to show socket */
        remove_key_events (window);
}

static void
maybe_kill_dialog (GSWindow *window)
{
        if (!window->priv->dialog_shake_in_progress
            && window->priv->dialog_quit_requested
            && window->priv->lock_pid > 0) {
                kill (window->priv->lock_pid, SIGTERM);
        }
}

/* very rudimentary animation for indicating an auth failure */
static void
shake_dialog (GSWindow *window)
{
        int   i;
        guint left;
        guint right;

        window->priv->dialog_shake_in_progress = TRUE;

        for (i = 0; i < 9; i++) {
                if (i % 2 == 0) {
                        left = 30;
                        right = 0;
                } else {
                        left = 0;
                        right = 30;
                }

                if (! window->priv->lock_box) {
                        break;
                }

                gtk_alignment_set_padding (GTK_ALIGNMENT (window->priv->lock_box),
                                           0, 0,
                                           left,
                                           right);

                while (gtk_events_pending ()) {
                        gtk_main_iteration ();
                }

                g_usleep (10000);
        }

        window->priv->dialog_shake_in_progress = FALSE;
        maybe_kill_dialog (window);
}

static void
window_set_dialog_up (GSWindow *window,
                      gboolean  dialog_up)
{
        if (window->priv->dialog_up == dialog_up) {
                return;
        }

        window->priv->dialog_up = dialog_up;
        g_object_notify (G_OBJECT (window), "dialog-up");
}

static void
popdown_dialog (GSWindow *window)
{
        gs_window_dialog_finish (window);

        gtk_widget_show (window->priv->drawing_area);

        gs_window_clear (window);
        set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), TRUE);

        window_set_dialog_up (window, FALSE);

        /* reset the pointer positions */
        window->priv->last_x = -1;
        window->priv->last_y = -1;

        if (window->priv->lock_box != NULL) {
                gtk_container_remove (GTK_CONTAINER (window->priv->vbox), GTK_WIDGET (window->priv->lock_box));
                window->priv->lock_box = NULL;
        }

        remove_popup_dialog_idle (window);
        remove_command_watches (window);
}

static gboolean
lock_command_watch (GIOChannel   *source,
                    GIOCondition  condition,
                    GSWindow     *window)
{
        gboolean finished = FALSE;

        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        if (condition & G_IO_IN) {
                GIOStatus status;
                GError   *error = NULL;
                char     *line;

                line = NULL;
                status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

                switch (status) {
                case G_IO_STATUS_NORMAL:
                        gs_debug ("command output: %s", line);

                        if (strstr (line, "WINDOW ID=") != NULL) {
                                guint32 id;
                                char    c;
                                if (1 == sscanf (line, " WINDOW ID= %" G_GUINT32_FORMAT " %c", &id, &c)) {
                                        create_lock_socket (window, id);
                                }
                        } else if (strstr (line, "NOTICE=") != NULL) {
                                if (strstr (line, "NOTICE=AUTH FAILED") != NULL) {
                                        shake_dialog (window);
                                }
                        } else if (strstr (line, "RESPONSE=") != NULL) {
                                if (strstr (line, "RESPONSE=OK") != NULL) {
                                        gs_debug ("Got OK response");
                                        window->priv->dialog_response = DIALOG_RESPONSE_OK;
                                } else {
                                        gs_debug ("Got CANCEL response");
                                        window->priv->dialog_response = DIALOG_RESPONSE_CANCEL;
                                }
                                finished = TRUE;
                        } else if (strstr (line, "REQUEST QUIT") != NULL) {
                                gs_debug ("Got request for quit");
                                window->priv->dialog_quit_requested = TRUE;
                                maybe_kill_dialog (window);
                        }
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        gs_debug ("Error reading from child: %s\n", error->message);
                        g_error_free (error);
                        return FALSE;
                case G_IO_STATUS_AGAIN:
                default:
                        break;
                }

                g_free (line);
        } else if (condition & G_IO_HUP) {
                finished = TRUE;
        }

        if (finished) {
                popdown_dialog (window);

                if (window->priv->dialog_response == DIALOG_RESPONSE_OK) {
                        add_emit_deactivated_idle (window);
                }

                window->priv->lock_watch_id = 0;

                return FALSE;
        }

        return TRUE;
}

static gboolean
is_logout_enabled (GSWindow *window)
{
        double elapsed;

        if (! window->priv->logout_enabled) {
                return FALSE;
        }

        if (! window->priv->logout_command) {
                return FALSE;
        }

        elapsed = g_timer_elapsed (window->priv->timer, NULL);

        if (window->priv->logout_timeout < (elapsed * 1000)) {
                return TRUE;
        }

        return FALSE;
}

static gboolean
is_user_switch_enabled (GSWindow *window)
{
        return window->priv->user_switch_enabled;
}

static void
popup_dialog (GSWindow *window)
{
        gboolean  result;
        char     *tmp;
        GString  *command;

        gs_debug ("Popping up dialog");

        tmp = g_build_filename (LIBEXECDIR, "gnome-screensaver-dialog", NULL);
        command = g_string_new (tmp);
        g_free (tmp);

        if (is_logout_enabled (window)) {
                command = g_string_append (command, " --enable-logout");
                g_string_append_printf (command, " --logout-command='%s'", window->priv->logout_command);
        }

        if (window->priv->status_message) {
                char *quoted;

                quoted = g_shell_quote (window->priv->status_message);
                g_string_append_printf (command, " --status-message=%s", quoted);
                g_free (quoted);
        }

        if (is_user_switch_enabled (window)) {
                command = g_string_append (command, " --enable-switch");
        }

        if (gs_debug_enabled ()) {
                command = g_string_append (command, " --verbose");
        }

        gtk_widget_hide (window->priv->drawing_area);

        gs_window_clear_to_background_surface (window);

        set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), FALSE);

        window->priv->dialog_quit_requested = FALSE;
        window->priv->dialog_shake_in_progress = FALSE;

        result = spawn_on_window (window,
                                  command->str,
                                  &window->priv->lock_pid,
                                  (GIOFunc)lock_command_watch,
                                  window,
                                  &window->priv->lock_watch_id);
        if (! result) {
                gs_debug ("Could not start command: %s", command->str);
        }

        g_string_free (command, TRUE);
}

static gboolean
popup_dialog_idle (GSWindow *window)
{
        popup_dialog (window);

        window->priv->popup_dialog_idle_id = 0;

        return FALSE;
}

void
gs_window_request_unlock (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Requesting unlock");

        if (! gtk_widget_get_visible (GTK_WIDGET (window))) {
                gs_debug ("Request unlock but window is not visible!");
                return;
        }

        if (window->priv->lock_watch_id > 0) {
                return;
        }

        if (! window->priv->lock_enabled) {
                add_emit_deactivated_idle (window);

                return;
        }

        if (window->priv->popup_dialog_idle_id == 0) {
                add_popup_dialog_idle (window);
        }

        window_set_dialog_up (window, TRUE);
}

void
gs_window_cancel_unlock_request (GSWindow  *window)
{
        /* FIXME: This is a bit of a hammer approach...
         * Maybe we should send a delete-event to
         * the plug?
         */
        g_return_if_fail (GS_IS_WINDOW (window));

        popdown_dialog (window);
}

void
gs_window_set_lock_enabled (GSWindow *window,
                            gboolean  lock_enabled)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->lock_enabled == lock_enabled) {
                return;
        }

        window->priv->lock_enabled = lock_enabled;
        g_object_notify (G_OBJECT (window), "lock-enabled");
}

void
gs_window_set_screen (GSWindow  *window,
                      GdkScreen *screen)
{

        g_return_if_fail (GS_IS_WINDOW (window));
        g_return_if_fail (GDK_IS_SCREEN (screen));

        gtk_window_set_screen (GTK_WINDOW (window), screen);
}

GdkScreen *
gs_window_get_screen (GSWindow  *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

        return gtk_window_get_screen (GTK_WINDOW (window));
}

void
gs_window_set_keyboard_enabled (GSWindow *window,
                                gboolean  enabled)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        window->priv->keyboard_enabled = enabled;
}

void
gs_window_set_keyboard_command (GSWindow   *window,
                                const char *command)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        g_free (window->priv->keyboard_command);

        if (command != NULL) {
                window->priv->keyboard_command = g_strdup (command);
        } else {
                window->priv->keyboard_command = NULL;
        }
}

void
gs_window_set_logout_enabled (GSWindow *window,
                              gboolean  logout_enabled)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        window->priv->logout_enabled = logout_enabled;
}

void
gs_window_set_user_switch_enabled (GSWindow *window,
                                   gboolean  user_switch_enabled)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        window->priv->user_switch_enabled = user_switch_enabled;
}

void
gs_window_set_logout_timeout (GSWindow *window,
                              glong     logout_timeout)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (logout_timeout < 0) {
                window->priv->logout_timeout = 0;
        } else {
                window->priv->logout_timeout = logout_timeout;
        }
}

void
gs_window_set_logout_command (GSWindow   *window,
                              const char *command)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        g_free (window->priv->logout_command);

        if (command) {
                window->priv->logout_command = g_strdup (command);
        } else {
                window->priv->logout_command = NULL;
        }
}

void
gs_window_set_status_message (GSWindow   *window,
                            const char *status_message)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        g_free (window->priv->status_message);
        window->priv->status_message = g_strdup (status_message);
}

void
gs_window_set_monitor (GSWindow *window,
                       int       monitor)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->monitor == monitor) {
                return;
        }

        window->priv->monitor = monitor;

        gtk_widget_queue_resize (GTK_WIDGET (window));

        g_object_notify (G_OBJECT (window), "monitor");
}

int
gs_window_get_monitor (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), -1);

        return window->priv->monitor;
}

static void
gs_window_set_property (GObject            *object,
                        guint               prop_id,
                        const GValue       *value,
                        GParamSpec         *pspec)
{
        GSWindow *self;

        self = GS_WINDOW (object);

        switch (prop_id) {
        case PROP_LOCK_ENABLED:
                gs_window_set_lock_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_KEYBOARD_ENABLED:
                gs_window_set_keyboard_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_KEYBOARD_COMMAND:
                gs_window_set_keyboard_command (self, g_value_get_string (value));
                break;
        case PROP_LOGOUT_ENABLED:
                gs_window_set_logout_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOGOUT_COMMAND:
                gs_window_set_logout_command (self, g_value_get_string (value));
                break;
        case PROP_STATUS_MESSAGE:
                gs_window_set_status_message (self, g_value_get_string (value));
                break;
        case PROP_LOGOUT_TIMEOUT:
                gs_window_set_logout_timeout (self, g_value_get_long (value));
                break;
        case PROP_MONITOR:
                gs_window_set_monitor (self, g_value_get_int (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_window_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
        GSWindow *self;

        self = GS_WINDOW (object);

        switch (prop_id) {
        case PROP_LOCK_ENABLED:
                g_value_set_boolean (value, self->priv->lock_enabled);
                break;
        case PROP_KEYBOARD_ENABLED:
                g_value_set_boolean (value, self->priv->keyboard_enabled);
                break;
        case PROP_KEYBOARD_COMMAND:
                g_value_set_string (value, self->priv->keyboard_command);
                break;
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
                break;
        case PROP_LOGOUT_COMMAND:
                g_value_set_string (value, self->priv->logout_command);
                break;
        case PROP_STATUS_MESSAGE:
                g_value_set_string (value, self->priv->status_message);
                break;
        case PROP_LOGOUT_TIMEOUT:
                g_value_set_long (value, self->priv->logout_timeout);
                break;
        case PROP_MONITOR:
                g_value_set_int (value, self->priv->monitor);
                break;
        case PROP_OBSCURED:
                g_value_set_boolean (value, self->priv->obscured);
                break;
        case PROP_DIALOG_UP:
                g_value_set_boolean (value, self->priv->dialog_up);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
queue_key_event (GSWindow    *window,
                 GdkEventKey *event)
{
        /* Eat the first return, enter, escape, or space */
        if (window->priv->key_events == NULL
            && (event->keyval == GDK_KEY_Return
                || event->keyval == GDK_KEY_KP_Enter
                || event->keyval == GDK_KEY_Escape
                || event->keyval == GDK_KEY_space)) {
                return;
        }

        /* Only cache MAX_QUEUED_EVENTS key events.  If there are any more than this then
           something is wrong */
        /* Don't queue keys that may cause focus navigation in the dialog */
        if (g_list_length (window->priv->key_events) < MAX_QUEUED_EVENTS
            && event->keyval != GDK_KEY_Tab
            && event->keyval != GDK_KEY_Up
            && event->keyval != GDK_KEY_Down) {
                window->priv->key_events = g_list_prepend (window->priv->key_events,
                                                           gdk_event_copy ((GdkEvent *)event));
        }
}

static gboolean
maybe_handle_activity (GSWindow *window)
{
        gboolean handled;

        handled = FALSE;

        /* if we already have a socket then don't bother */
        if (! window->priv->lock_socket
            && gtk_widget_get_sensitive (GTK_WIDGET (window))) {
                g_signal_emit (window, signals [ACTIVITY], 0, &handled);
        }

        return handled;
}

static gboolean
gs_window_real_key_press_event (GtkWidget   *widget,
                                GdkEventKey *event)
{
        /*g_message ("KEY PRESS state: %u keyval %u", event->state, event->keyval);*/

        /* Ignore brightness keys */
        if (event->hardware_keycode == 101 || event->hardware_keycode == 212) {
                gs_debug ("Ignoring brightness keys");
                return TRUE;
        }

        maybe_handle_activity (GS_WINDOW (widget));

        queue_key_event (GS_WINDOW (widget), event);

        if (GTK_WIDGET_CLASS (gs_window_parent_class)->key_press_event) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->key_press_event (widget, event);
        }

        return TRUE;
}

static gboolean
gs_window_real_motion_notify_event (GtkWidget      *widget,
                                    GdkEventMotion *event)
{
        GSWindow  *window;
        gdouble    distance;
        gdouble    min_distance;
        gdouble    min_percentage = 0.1;
        GdkScreen *screen;

        window = GS_WINDOW (widget);

        screen = gs_window_get_screen (window);
        min_distance = gdk_screen_get_width (screen) * min_percentage;

        /* if the last position was not set then don't detect motion */
        if (window->priv->last_x < 0 || window->priv->last_y < 0) {
                window->priv->last_x = event->x;
                window->priv->last_y = event->y;

                return FALSE;
        }

        /* just an approximate distance */
        distance = MAX (ABS (window->priv->last_x - event->x),
                        ABS (window->priv->last_y - event->y));

        if (distance > min_distance) {
                maybe_handle_activity (window);

                window->priv->last_x = -1;
                window->priv->last_y = -1;
        }

        return FALSE;
}

static gboolean
gs_window_real_button_press_event (GtkWidget      *widget,
                                   GdkEventButton *event)
{
        GSWindow *window;

        window = GS_WINDOW (widget);
        maybe_handle_activity (window);

        return FALSE;
}

static gboolean
gs_window_real_scroll_event (GtkWidget      *widget,
                             GdkEventScroll *event)
{
        GSWindow *window;

        window = GS_WINDOW (widget);
        maybe_handle_activity (window);

        return FALSE;
}

static void
gs_window_real_size_request (GtkWidget      *widget,
                             GtkRequisition *requisition)
{
        GSWindow      *window;
        GtkBin        *bin;
        GdkRectangle   old_geometry;
        int            position_changed = FALSE;
        int            size_changed = FALSE;

        window = GS_WINDOW (widget);
        bin = GTK_BIN (widget);

        if (gtk_bin_get_child (bin) && gtk_widget_get_visible (gtk_bin_get_child (bin))) {
                gtk_widget_size_request (gtk_bin_get_child (bin), requisition);
        }

        old_geometry = window->priv->geometry;

        update_geometry (window);

        requisition->width  = window->priv->geometry.width;
        requisition->height = window->priv->geometry.height;

        if (!gtk_widget_get_realized (widget)) {
                return;
        }

        if (old_geometry.width  != window->priv->geometry.width ||
            old_geometry.height != window->priv->geometry.height) {
                size_changed = TRUE;
        }

        if (old_geometry.x != window->priv->geometry.x ||
            old_geometry.y != window->priv->geometry.y) {
                position_changed = TRUE;
        }

        gs_window_move_resize_window (window, position_changed, size_changed);
}

static gboolean
gs_window_real_grab_broken (GtkWidget          *widget,
                            GdkEventGrabBroken *event)
{
        if (event->grab_window != NULL) {
                gs_debug ("Grab broken on window %X %s, new grab on window %X",
                          (guint32) GDK_WINDOW_XID (event->window),
                          event->keyboard ? "keyboard" : "pointer",
                          (guint32) GDK_WINDOW_XID (event->grab_window));
        } else {
                gs_debug ("Grab broken on window %X %s, new grab is outside application",
                          (guint32) GDK_WINDOW_XID (event->window),
                          event->keyboard ? "keyboard" : "pointer");
        }

        return FALSE;
}

gboolean
gs_window_is_obscured (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        return window->priv->obscured;
}

gboolean
gs_window_is_dialog_up (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        return window->priv->dialog_up;
}

static void
window_set_obscured (GSWindow *window,
                     gboolean  obscured)
{
        if (window->priv->obscured == obscured) {
                return;
        }

        window->priv->obscured = obscured;
        g_object_notify (G_OBJECT (window), "obscured");
}

static gboolean
gs_window_real_visibility_notify_event (GtkWidget          *widget,
                                        GdkEventVisibility *event)
{
        switch (event->state) {
        case GDK_VISIBILITY_FULLY_OBSCURED:
                window_set_obscured (GS_WINDOW (widget), TRUE);
                break;
        case GDK_VISIBILITY_PARTIAL:
                break;
        case GDK_VISIBILITY_UNOBSCURED:
                window_set_obscured (GS_WINDOW (widget), FALSE);
                break;
        default:
                break;
        }

        return FALSE;
}

static void
gs_window_real_get_preferred_width (GtkWidget *widget,
                               gint      *minimal_width,
                               gint      *natural_width)
{
        GtkRequisition requisition;

        gs_window_real_size_request (widget, &requisition);

        *minimal_width = *natural_width = requisition.width;
}

static void
gs_window_real_get_preferred_height (GtkWidget *widget,
                                gint      *minimal_height,
                                gint      *natural_height)
{
        GtkRequisition requisition;

        gs_window_real_size_request (widget, &requisition);

        *minimal_height = *natural_height = requisition.height;
}

static void
gs_window_class_init (GSWindowClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize     = gs_window_finalize;
        object_class->get_property = gs_window_get_property;
        object_class->set_property = gs_window_set_property;

        widget_class->show                = gs_window_real_show;
        widget_class->hide                = gs_window_real_hide;
        widget_class->realize             = gs_window_real_realize;
        widget_class->unrealize           = gs_window_real_unrealize;
        widget_class->key_press_event     = gs_window_real_key_press_event;
        widget_class->motion_notify_event = gs_window_real_motion_notify_event;
        widget_class->button_press_event  = gs_window_real_button_press_event;
        widget_class->scroll_event        = gs_window_real_scroll_event;
        widget_class->get_preferred_width        = gs_window_real_get_preferred_width;
        widget_class->get_preferred_height       = gs_window_real_get_preferred_height;
        widget_class->grab_broken_event   = gs_window_real_grab_broken;
        widget_class->visibility_notify_event = gs_window_real_visibility_notify_event;

        g_type_class_add_private (klass, sizeof (GSWindowPrivate));

        signals [ACTIVITY] =
                g_signal_new ("activity",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWindowClass, activity),
                              NULL,
                              NULL,
                              gs_marshal_BOOLEAN__VOID,
                              G_TYPE_BOOLEAN,
                              0);
        signals [DEACTIVATED] =
                g_signal_new ("deactivated",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWindowClass, deactivated),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        g_object_class_install_property (object_class,
                                         PROP_OBSCURED,
                                         g_param_spec_boolean ("obscured",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DIALOG_UP,
                                         g_param_spec_boolean ("dialog-up",
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
                                         PROP_LOGOUT_COMMAND,
                                         g_param_spec_string ("logout-command",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_STATUS_MESSAGE,
                                         g_param_spec_string ("status-message",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_KEYBOARD_ENABLED,
                                         g_param_spec_boolean ("keyboard-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_KEYBOARD_COMMAND,
                                         g_param_spec_string ("keyboard-command",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_MONITOR,
                                         g_param_spec_int ("monitor",
                                                           "Xinerama monitor",
                                                           "The monitor (in terms of Xinerama) which the window is on",
                                                           0,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

}

static void
create_info_bar (GSWindow *window)
{
        window->priv->info_bar = gtk_info_bar_new ();
        gtk_widget_set_no_show_all (window->priv->info_bar, TRUE);
        gtk_box_pack_end (GTK_BOX (window->priv->vbox), window->priv->info_bar, FALSE, FALSE, 0);
}

static gboolean
on_panel_draw (GtkWidget    *widget,
               cairo_t      *cr,
               GSWindow     *window)
{
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
        cairo_paint (cr);

        return FALSE;
}

static void
update_clock (GSWindow *window)
{
        char *markup;

        markup = g_strdup_printf ("<b><span foreground=\"#ccc\">%s</span></b>", gnome_wall_clock_get_clock (window->priv->clock_tracker));
        gtk_label_set_markup (GTK_LABEL (window->priv->clock), markup);
        g_free (markup);
}

static void
on_clock_changed (GnomeWallClock *clock,
                  GParamSpec     *pspec,
                  gpointer        user_data)
{
        update_clock (GS_WINDOW (user_data));
}

static char *
get_user_display_name (void)
{
        const char *name;
        char       *utf8_name;

        name = g_get_real_name ();

        if (name == NULL || name[0] == '\0' || strcmp (name, "Unknown") == 0) {
                name = g_get_user_name ();
        }

        utf8_name = NULL;

        if (name != NULL) {
                utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
        }

        return utf8_name;
}

static void
update_name_label (GSWindow *window)
{
        char *name;
        char *markup;
        name = get_user_display_name ();
        markup = g_strdup_printf ("<b><span foreground=\"#ccc\">%s</span></b>", name);
        gtk_label_set_markup (GTK_LABEL (window->priv->name_label), markup);
        g_free (markup);
        g_free (name);
}

static void
create_panel (GSWindow *window)
{
        GtkWidget    *left_hbox;
        GtkWidget    *right_hbox;
        GtkWidget    *alignment;
        GtkWidget    *right_alignment;
        GtkWidget    *image;
        GtkSizeGroup *sg;
        GdkRGBA       bg;
        GdkRGBA       fg;
        int           all_states;
        GIcon        *gicon;

        bg.red = 0;
        bg.green = 0;
        bg.blue = 0;
        bg.alpha = 1.0;

        fg.red = 0.8;
        fg.green = 0.8;
        fg.blue = 0.8;
        fg.alpha = 1.0;

        all_states = 0;

        gtk_widget_override_background_color (window->priv->panel, all_states, &bg);
        gtk_widget_override_color (window->priv->panel, all_states, &fg);
        gtk_container_set_border_width (GTK_CONTAINER (window->priv->panel), 0);

        g_signal_connect (window->priv->panel, "draw", G_CALLBACK (on_panel_draw), window);

        left_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start (GTK_BOX (window->priv->panel), left_hbox, TRUE, TRUE, 0);

        alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
        gtk_box_pack_start (GTK_BOX (window->priv->panel), alignment, FALSE, FALSE, 0);
        window->priv->clock = gtk_label_new (NULL);
        gtk_misc_set_padding (GTK_MISC (window->priv->clock), 4, 4);
        gtk_container_add (GTK_CONTAINER (alignment), window->priv->clock);

        right_alignment = gtk_alignment_new (1, 0.5, 1, 1);
        gtk_box_pack_end (GTK_BOX (window->priv->panel), right_alignment, TRUE, TRUE, 0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (right_alignment),
                                   0, 0, 10, 10);

        right_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_container_add (GTK_CONTAINER (right_alignment), right_hbox);

        window->priv->name_label = gtk_label_new (NULL);
        update_name_label (window);
        gtk_box_pack_end (GTK_BOX (right_hbox), window->priv->name_label, FALSE, FALSE, 0);

        gicon = g_themed_icon_new_with_default_fallbacks ("changes-prevent-symbolic");
        image = gtk_image_new_from_gicon (gicon, GTK_ICON_SIZE_MENU);
        gtk_widget_override_color (image, all_states, &fg);
        g_object_unref (gicon);
        gtk_box_pack_end (GTK_BOX (right_hbox), image, FALSE, FALSE, 0);

        sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
        gtk_size_group_add_widget (sg, left_hbox);
        gtk_size_group_add_widget (sg, right_hbox);

        gtk_widget_show_all (window->priv->panel);
}

static void
on_drawing_area_realized (GtkWidget *drawing_area)
{
        GdkRGBA black = { 0.0, 0.0, 0.0, 1.0 };

        gdk_window_set_background_rgba (gtk_widget_get_window (drawing_area),
                                        &black);
}

static void
gs_window_init (GSWindow *window)
{
        window->priv = GS_WINDOW_GET_PRIVATE (window);

        window->priv->geometry.x      = -1;
        window->priv->geometry.y      = -1;
        window->priv->geometry.width  = -1;
        window->priv->geometry.height = -1;

        window->priv->last_x = -1;
        window->priv->last_y = -1;

        gtk_window_set_decorated (GTK_WINDOW (window), FALSE);

        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);

        gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);

        gtk_window_fullscreen (GTK_WINDOW (window));

        gtk_widget_set_events (GTK_WIDGET (window),
                               gtk_widget_get_events (GTK_WIDGET (window))
                               | GDK_POINTER_MOTION_MASK
                               | GDK_BUTTON_PRESS_MASK
                               | GDK_BUTTON_RELEASE_MASK
                               | GDK_KEY_PRESS_MASK
                               | GDK_KEY_RELEASE_MASK
                               | GDK_EXPOSURE_MASK
                               | GDK_VISIBILITY_NOTIFY_MASK
                               | GDK_ENTER_NOTIFY_MASK
                               | GDK_LEAVE_NOTIFY_MASK);

        window->priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_show (window->priv->vbox);
        gtk_container_add (GTK_CONTAINER (window), window->priv->vbox);

        window->priv->panel = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_show (window->priv->panel);
        gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->panel, FALSE, FALSE, 0);
        create_panel (window);

        window->priv->drawing_area = gtk_drawing_area_new ();
        gtk_widget_show (window->priv->drawing_area);
        gtk_widget_set_app_paintable (window->priv->drawing_area, TRUE);
        gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->drawing_area, TRUE, TRUE, 0);
        g_signal_connect (window->priv->drawing_area,
                          "realize",
                          G_CALLBACK (on_drawing_area_realized),
                          NULL);

        create_info_bar (window);

        window->priv->clock_tracker = g_object_new (GNOME_TYPE_WALL_CLOCK, NULL);
        g_signal_connect (window->priv->clock_tracker, "notify::clock", G_CALLBACK (on_clock_changed), window);
        update_clock (window);
}

static void
remove_command_watches (GSWindow *window)
{
        if (window->priv->lock_watch_id != 0) {
                g_source_remove (window->priv->lock_watch_id);
                window->priv->lock_watch_id = 0;
        }
        if (window->priv->keyboard_watch_id != 0) {
                g_source_remove (window->priv->keyboard_watch_id);
                window->priv->keyboard_watch_id = 0;
        }
}

static void
gs_window_finalize (GObject *object)
{
        GSWindow *window;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_WINDOW (object));

        window = GS_WINDOW (object);

        g_return_if_fail (window->priv != NULL);

        g_free (window->priv->logout_command);
        g_free (window->priv->keyboard_command);

        if (window->priv->clock_tracker) {
                g_object_unref (window->priv->clock_tracker);
        }

        if (window->priv->info_bar_timer_id > 0) {
                g_source_remove (window->priv->info_bar_timer_id);
        }

        remove_watchdog_timer (window);
        remove_popup_dialog_idle (window);

        if (window->priv->timer) {
                g_timer_destroy (window->priv->timer);
        }

        remove_key_events (window);

        remove_command_watches (window);

        gs_window_dialog_finish (window);

        if (window->priv->background_surface) {
               cairo_surface_destroy (window->priv->background_surface);
        }

        G_OBJECT_CLASS (gs_window_parent_class)->finalize (object);
}

GSWindow *
gs_window_new (GdkScreen *screen,
               int        monitor,
               gboolean   lock_enabled)
{
        GObject     *result;

        result = g_object_new (GS_TYPE_WINDOW,
                               "type", GTK_WINDOW_POPUP,
                               "screen", screen,
                               "monitor", monitor,
                               "lock-enabled", lock_enabled,
                               "app-paintable", TRUE,
                               NULL);

        return GS_WINDOW (result);
}
