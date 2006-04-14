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

#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "gs-window.h"
#include "gs-visual-gl.h"
#include "subprocs.h"
#include "gs-debug.h"

static void gs_window_class_init (GSWindowClass *klass);
static void gs_window_init       (GSWindow      *window);
static void gs_window_finalize   (GObject       *object);

static gboolean popup_dialog_idle (GSWindow *window);
static gboolean gs_window_request_unlock_idle (GSWindow *window);

enum {
        DIALOG_RESPONSE_CANCEL,
        DIALOG_RESPONSE_OK
};

#define GS_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WINDOW, GSWindowPrivate))

struct GSWindowPrivate
{
        int        monitor;

        GdkRectangle geometry;

        guint      lock_enabled : 1;
        guint      user_switch_enabled : 1;
        guint      logout_enabled : 1;
        guint64    logout_timeout;
        char      *logout_command;

        GtkWidget *box;
        GtkWidget *socket;

        guint      request_unlock_idle_id;
        guint      popup_dialog_idle_id;

        guint      dialog_map_signal_id;
        guint      dialog_unmap_signal_id;
        guint      dialog_response_signal_id;

        guint      watchdog_timer_id;

        gint       pid;
        gint       watch_id;
        gint       dialog_response;

        GList     *key_events;

        gdouble    last_x;
        gdouble    last_y;

        GTimer    *timer;
};

enum {
        DEACTIVATED,
        DIALOG_UP,
        DIALOG_DOWN,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_LOGOUT_COMMAND,
        PROP_LOGOUT_TIMEOUT,
        PROP_MONITOR
};

static GObjectClass   *parent_class = NULL;
static guint           signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSWindow, gs_window, GTK_TYPE_WINDOW)

static void
set_invisible_cursor (GdkWindow *window,
                      gboolean   invisible)
{
        GdkBitmap *empty_bitmap;
        GdkCursor *cursor = NULL;
        GdkColor   useless;
        char       invisible_cursor_bits [] = { 0x0 };

        if (invisible) {
                useless.red = useless.green = useless.blue = 0;
                useless.pixel = 0;

                empty_bitmap = gdk_bitmap_create_from_data (window,
                                                            invisible_cursor_bits,
                                                            1, 1);

                cursor = gdk_cursor_new_from_pixmap (empty_bitmap,
                                                     empty_bitmap,
                                                     &useless,
                                                     &useless, 0, 0);

                g_object_unref (empty_bitmap);
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
                ev_time = gdk_x11_get_server_time (GTK_WIDGET (window)->window);
        }

        gdk_x11_window_set_user_time (GTK_WIDGET (window)->window, ev_time);
}

static void
clear_children (Window window)
{
        Window            root;
        Window            parent;
        Window           *children;
        unsigned int      n_children;
        int               status;

        children = NULL;
        status = XQueryTree (GDK_DISPLAY (), window, &root, &parent, &children, &n_children);

        if (status == 0) {
                if (children) {
                        XFree (children);
                }
                return;
        }

        if (children) {
                while (n_children) {
                        Window child;

                        child = children [--n_children];

                        XClearWindow (GDK_DISPLAY (), child);
                        clear_children (child);
                }

                XFree (children);
        }
}

static void
clear_all_children (GSWindow *window)
{
        GdkWindow *w;

        gs_debug ("Clearing all child windows");

        gdk_error_trap_push ();

        w = GTK_WIDGET (window)->window;

        clear_children (GDK_WINDOW_XID (w));

        gdk_display_sync (gtk_widget_get_display (GTK_WIDGET (window)));
        gdk_flush ();
        gdk_error_trap_pop ();
}

void
gs_window_clear (GSWindow *window)
{
        GdkColor     color = { 0, 0x0000, 0x0000, 0x0000 };
        GdkColormap *colormap;
        GtkStateType state;

        gs_debug ("Clearing window");

        state = (GtkStateType) 0;
        while (state < (GtkStateType) G_N_ELEMENTS (GTK_WIDGET (window)->style->bg)) {
                gtk_widget_modify_bg (GTK_WIDGET (window), state, &color);
                state++;
        }

        colormap = gdk_drawable_get_colormap (GTK_WIDGET (window)->window);
        gdk_colormap_alloc_color (colormap, &color, FALSE, TRUE);
        gdk_window_set_background (GTK_WIDGET (window)->window, &color);
        gdk_window_clear (GTK_WIDGET (window)->window);

        /* If a screensaver theme adds child windows we need to clear them too */
        clear_all_children (window);

        gdk_flush ();
}

static void
update_geometry (GSWindow *window)
{
        GdkRectangle geometry;

        gdk_screen_get_monitor_geometry (GTK_WINDOW (window)->screen,
                                         window->priv->monitor,
                                         &geometry);

        window->priv->geometry.x = geometry.x;
        window->priv->geometry.y = geometry.y;
        window->priv->geometry.width = geometry.width;
        window->priv->geometry.height = geometry.height;
}

static void
screen_size_changed (GdkScreen *screen,
                     GSWindow  *window)
{
        gtk_widget_queue_resize (GTK_WIDGET (window));
}

/* copied from panel-toplevel.c */
static void
gs_window_move_resize_window (GSWindow *window,
                              gboolean  move,
                              gboolean  resize)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (window);

        g_assert (GTK_WIDGET_REALIZED (widget));

        if (move && resize) {
                gdk_window_move_resize (widget->window,
                                        window->priv->geometry.x,
                                        window->priv->geometry.y,
                                        window->priv->geometry.width,
                                        window->priv->geometry.height);
        } else if (move) {
                gdk_window_move (widget->window,
                                 window->priv->geometry.x,
                                 window->priv->geometry.y);
        } else if (resize) {
                gdk_window_resize (widget->window,
                                   window->priv->geometry.width,
                                   window->priv->geometry.height);
        }
}

static void
gs_window_real_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (parent_class)->unrealize) {
                GTK_WIDGET_CLASS (parent_class)->unrealize (widget);
        }
}

static void
gs_window_real_realize (GtkWidget *widget)
{
        gs_visual_gl_widget_set_best_colormap (widget);

        if (GTK_WIDGET_CLASS (parent_class)->realize) {
                GTK_WIDGET_CLASS (parent_class)->realize (widget);
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
        gtk_window_present (GTK_WINDOW (window));

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
        window->priv->watchdog_timer_id = g_timeout_add (timeout,
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

static void
remove_request_unlock_idle (GSWindow *window)
{
        if (window->priv->request_unlock_idle_id != 0) {
                g_source_remove (window->priv->request_unlock_idle_id);
                window->priv->request_unlock_idle_id = 0;
        }
}

static void
add_request_unlock_idle (GSWindow *window)
{
        window->priv->request_unlock_idle_id = g_idle_add ((GSourceFunc)gs_window_request_unlock_idle, window);
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

        win = GTK_WIDGET (window)->window;

        gdk_window_raise (win);
}

static gboolean
x11_window_is_ours (Window window)
{
        GdkWindow *gwindow;
        gboolean   ret;

        ret = FALSE;

        gwindow = gdk_window_lookup (window);
        if (gwindow && (window != GDK_ROOT_WINDOW ())) {
                ret = TRUE;
        }

        return ret;
}

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

        XGetWindowAttributes (GDK_DISPLAY (), GDK_ROOT_WINDOW (), &attr);

        events = SubstructureNotifyMask | attr.your_event_mask;
        XSelectInput (GDK_DISPLAY (), GDK_ROOT_WINDOW (), events);
        gdk_flush ();
        gdk_error_trap_pop ();
}

static void
gs_window_real_show (GtkWidget *widget)
{
        GSWindow *window;

        if (GTK_WIDGET_CLASS (parent_class)->show) {
                GTK_WIDGET_CLASS (parent_class)->show (widget);
        }

        gs_window_clear (GS_WINDOW (widget));

        set_invisible_cursor (widget->window, TRUE);

        window = GS_WINDOW (widget);
        if (window->priv->timer) {
                g_timer_destroy (window->priv->timer);
        }
        window->priv->timer = g_timer_new ();

        remove_watchdog_timer (window);
        add_watchdog_timer (window, 30000);

        select_popup_events ();
        gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, window);
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

        if (GTK_WIDGET_CLASS (parent_class)->hide) {
                GTK_WIDGET_CLASS (parent_class)->hide (widget);
        }
}

void
gs_window_destroy (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gtk_widget_destroy (GTK_WIDGET (window));
}

GdkWindow *
gs_window_get_gdk_window (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

        return GTK_WIDGET (window)->window;
}

static GPtrArray *
get_env_vars (GtkWidget *widget)
{
        GPtrArray *env;
        char      *str;
        int        i;
        static const char *allowed_env_vars [] = {
                "PATH",
                "SESSION_MANAGER",
                "XAUTHORITY",
                "XAUTHLOCALHOSTNAME",
                "KRB5CCNAME",
                "KRBTKFILE",
                "LANG",
                "LANGUAGE"
        };

        env = g_ptr_array_new ();

        str = gdk_screen_make_display_name (gtk_widget_get_screen (widget));
        g_ptr_array_add (env, g_strdup_printf ("DISPLAY=%s", str));
        g_free (str);

        g_ptr_array_add (env, g_strdup_printf ("HOME=%s",
                                               g_get_home_dir ()));

        for (i = 0; i < G_N_ELEMENTS (allowed_env_vars); i++) {
                const char *var;
                const char *val;
                var = allowed_env_vars [i];
                val = g_getenv (var);
                if (val != NULL) {
                        g_ptr_array_add (env, g_strdup_printf ("%s=%s",
                                                               var,
                                                               val));
                }
        }

        g_ptr_array_add (env, NULL);

        return env;
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
        GPtrArray  *env;
        GError     *error;
        gboolean    result;
        GIOChannel *channel;
        int         standard_output;
        int         child_pid;
        int         id;
        int         i;

        error = NULL;
        if (! g_shell_parse_argv (command, &argc, &argv, &error)) {
                g_warning ("Could not parse command: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        env = get_env_vars (GTK_WIDGET (window));

        error = NULL;
        result = gdk_spawn_on_screen_with_pipes (GTK_WINDOW (window)->screen,
                                                 NULL,
                                                 argv,
                                                 (char **)env->pdata,
                                                 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                                 NULL,
                                                 NULL,
                                                 &child_pid,
                                                 NULL,
                                                 &standard_output,
                                                 NULL,
                                                 &error);

        for (i = 0; i < env->len; i++) {
                g_free (g_ptr_array_index (env, i));
        }
        g_ptr_array_free (env, TRUE);

        if (! result) {
                g_warning ("Could not start command '%s': %s", command, error->message);
                g_error_free (error);
                g_strfreev (argv);
                return FALSE;
        }

        if (pid) {
                *pid = child_pid;
        } else {
                g_spawn_close_pid (child_pid);
        }

        channel = g_io_channel_unix_new (standard_output);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        id = g_io_add_watch (channel,
                             G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                             watch_func,
                             user_data);
        if (watch_id) {
                *watch_id = id;
        }

        g_io_channel_unref (channel);

        g_strfreev (argv);

        return result;
}

static void
plug_added (GtkWidget *widget,
            GSWindow  *window)
{
        gtk_widget_show (window->priv->socket);
}

static gboolean
plug_removed (GtkWidget *widget,
              GSWindow  *window)
{
        gtk_widget_hide (window->priv->socket);
        gtk_container_remove (GTK_CONTAINER (window), GTK_WIDGET (window->priv->box));
        window->priv->box = NULL;

        return TRUE;
}

static void
forward_key_events (GSWindow *window)
{
        window->priv->key_events = g_list_reverse (window->priv->key_events);

        while (window->priv->key_events) {
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
socket_show (GtkWidget *widget,
             GSWindow  *window)
{
        gtk_widget_child_focus (window->priv->socket, GTK_DIR_TAB_FORWARD);

        /* send queued events to the dialog */
        forward_key_events (window);
}

static void
socket_destroyed (GtkWidget *widget,
                  GSWindow  *window)
{
        g_signal_handlers_disconnect_by_func (window->priv->socket, socket_show, window);
        g_signal_handlers_disconnect_by_func (window->priv->socket, socket_destroyed, window);
        g_signal_handlers_disconnect_by_func (window->priv->socket, plug_added, window);
        g_signal_handlers_disconnect_by_func (window->priv->socket, plug_removed, window);

        window->priv->socket = NULL;
}

static void
create_socket (GSWindow *window,
               guint32   id)
{
        window->priv->socket = gtk_socket_new ();
        window->priv->box = gtk_alignment_new (0.5, 0.5, 0, 0);
        gtk_widget_show (window->priv->box);

        gtk_container_add (GTK_CONTAINER (window), window->priv->box);

        gtk_container_add (GTK_CONTAINER (window->priv->box), window->priv->socket);

        g_signal_connect (window->priv->socket, "show",
                          G_CALLBACK (socket_show), window);
        g_signal_connect (window->priv->socket, "destroy",
                          G_CALLBACK (socket_destroyed), window);
        g_signal_connect (window->priv->socket, "plug_added",
                          G_CALLBACK (plug_added), window);
        g_signal_connect (window->priv->socket, "plug_removed",
                          G_CALLBACK (plug_removed), window);

        gtk_socket_add_id (GTK_SOCKET (window->priv->socket), id);
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
                        g_warning ("waitpid () should not fail in 'GSWindow'");
                }
        }

        return status;
}

static void
gs_window_dialog_finish (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Dialog finished");

        if (window->priv->pid > 0) {
                int exit_status;
                        
                exit_status = wait_on_child (window->priv->pid);

                g_spawn_close_pid (window->priv->pid);
                window->priv->pid = 0;
        }

        /* remove events for the case were we failed to show socket */
        remove_key_events (window);
}

/* very rudimentary animation for indicating an auth failure */
static void
shake_dialog (GSWindow *window)
{
        int   i;
        guint left;
        guint right;

        for (i = 0; i < 9; i++) {
                if (i % 2 == 0) {
                        left = 30;
                        right = 0;
                } else {
                        left = 0;
                        right = 30;
                }

                if (! window->priv->box) {
                        break;
                }

                gtk_alignment_set_padding (GTK_ALIGNMENT (window->priv->box),
                                           0, 0,
                                           left,
                                           right);

                while (gtk_events_pending ()) {
                        gtk_main_iteration ();
                }

                g_usleep (10000);
        }
}

static gboolean
command_watch (GIOChannel   *source,
               GIOCondition  condition,
               GSWindow     *window)
{
        gboolean finished = FALSE;

        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        if (condition & G_IO_IN) {
                GIOStatus status;
                GError   *error = NULL;
                char     *line;

                status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

                switch (status) {
                case G_IO_STATUS_NORMAL:
                        gs_debug ("command output: %s", line);

                        if (strstr (line, "WINDOW ID=")) {
                                guint32 id;
                                char    c;
                                if (1 == sscanf (line, " WINDOW ID= %" G_GUINT32_FORMAT " %c", &id, &c)) {
                                        create_socket (window, id);
                                }
                        } else if (strstr (line, "NOTICE=")) {
                                if (strstr (line, "NOTICE=AUTH FAILED")) {
                                        shake_dialog (window);
                                }
                        } else if (strstr (line, "RESPONSE=")) {
                                if (strstr (line, "RESPONSE=OK")) {
                                        window->priv->dialog_response = DIALOG_RESPONSE_OK;
                                } else {
                                        window->priv->dialog_response = DIALOG_RESPONSE_CANCEL;
                                }
                                finished = TRUE;
                        }

                        g_free (line);
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        fprintf (stderr, "Error reading fd from child: %s\n", error->message);
                        return FALSE;
                case G_IO_STATUS_AGAIN:
                default:
                        break;
                }

        } else if (condition & G_IO_HUP) {
                finished = TRUE;
        }

        if (finished) {
                gs_window_dialog_finish (window);

                if (window->priv->dialog_response == DIALOG_RESPONSE_OK) {
                        add_emit_deactivated_idle (window);
                }

                gs_window_clear (window);
                set_invisible_cursor (GTK_WIDGET (window)->window, TRUE);
                g_signal_emit (window, signals [DIALOG_DOWN], 0);

                /* reset the pointer positions */
                window->priv->last_x = -1;
                window->priv->last_y = -1;

                window->priv->watch_id = 0;

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

static gboolean
popup_dialog_idle (GSWindow *window)
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

        if (is_user_switch_enabled (window)) {
                command = g_string_append (command, " --enable-switch");
        }

        gs_window_clear (window);
        set_invisible_cursor (GTK_WIDGET (window)->window, FALSE);

        result = spawn_on_window (window,
                                  command->str,
                                  &window->priv->pid,
                                  (GIOFunc)command_watch,
                                  window,
                                  &window->priv->watch_id);
        if (! result) {
                g_warning ("Could not start command: %s", command->str);
        }

        g_string_free (command, TRUE);

        window->priv->popup_dialog_idle_id = 0;

        return FALSE;
}

void
gs_window_request_unlock (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Requesting unlock");

        if (window->priv->watch_id > 0) {
                return;
        }

        if (! window->priv->lock_enabled) {
                add_emit_deactivated_idle (window);

                return;
        }

        if (window->priv->popup_dialog_idle_id == 0) {
                add_popup_dialog_idle (window);
        }

        g_signal_emit (window, signals [DIALOG_UP], 0);
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

        return GTK_WINDOW (window)->screen;
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
        case PROP_LOGOUT_ENABLED:
                gs_window_set_logout_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOGOUT_COMMAND:
                gs_window_set_logout_command (self, g_value_get_string (value));
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
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
                break;
        case PROP_LOGOUT_COMMAND:
                g_value_set_string (value, self->priv->logout_command);
                break;
        case PROP_LOGOUT_TIMEOUT:
                g_value_set_long (value, self->priv->logout_timeout);
                break;
        case PROP_MONITOR:
                g_value_set_int (value, self->priv->monitor);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
gs_window_request_unlock_idle (GSWindow *window)
{
        gs_window_request_unlock (window);

        window->priv->request_unlock_idle_id = 0;

        return FALSE;
}

static void
queue_key_event (GSWindow    *window,
                 GdkEventKey *event)
{
        /* Eat the first return, enter, or space */
        if (window->priv->key_events == NULL
            && (event->keyval == GDK_Return
                || event->keyval == GDK_KP_Enter
                || event->keyval == GDK_space)) {
                return;
        }

        window->priv->key_events = g_list_prepend (window->priv->key_events,
                                                   gdk_event_copy ((GdkEvent *)event));
}

static gboolean
gs_window_real_key_press_event (GtkWidget   *widget,
                                GdkEventKey *event)
{
        gboolean catch_events = FALSE;

        /*g_message ("KEY PRESS state: %u keyval %u", event->state, event->keyval);*/

        /* if we don't already have a socket then request an unlock */
        if (! GS_WINDOW (widget)->priv->socket) {
                if (GS_WINDOW (widget)->priv->request_unlock_idle_id == 0) {
                        add_request_unlock_idle (GS_WINDOW (widget));
                }

                catch_events = TRUE;
        } else {
                if (! GTK_WIDGET_VISIBLE (GS_WINDOW (widget)->priv->socket)) {
                        catch_events = TRUE;
                }
        }

        /* Catch all keypresses up until the lock dialog is shown */
        if (catch_events) {
                queue_key_event (GS_WINDOW (widget), event);
        }

        if (GTK_WIDGET_CLASS (parent_class)->key_press_event) {
                GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event);
        }

        return TRUE;
}

static gboolean
gs_window_real_motion_notify_event (GtkWidget      *widget,
                                    GdkEventMotion *event)
{
        GSWindow *window;
        gdouble   distance;
        gdouble   min_distance = 10;

        window = GS_WINDOW (widget);

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
                /* if we don't already have a socket then request an unlock */
                if (! window->priv->socket
                    && (window->priv->request_unlock_idle_id == 0)) {
                        add_request_unlock_idle (window);
                }
                window->priv->last_x = -1;
                window->priv->last_y = -1;
        }

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

        if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
                gtk_widget_size_request (bin->child, requisition);
        }

	old_geometry = window->priv->geometry;

        update_geometry (window);

        requisition->width  = window->priv->geometry.width;
        requisition->height = window->priv->geometry.height;

        if (! GTK_WIDGET_REALIZED (widget)) {
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

static void
gs_window_class_init (GSWindowClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize     = gs_window_finalize;
        object_class->get_property = gs_window_get_property;
        object_class->set_property = gs_window_set_property;

        widget_class->show                = gs_window_real_show;
        widget_class->hide                = gs_window_real_hide;
        widget_class->realize             = gs_window_real_realize;
        widget_class->unrealize           = gs_window_real_unrealize;
        widget_class->key_press_event     = gs_window_real_key_press_event;
        widget_class->motion_notify_event = gs_window_real_motion_notify_event;
        widget_class->size_request        = gs_window_real_size_request;
        widget_class->grab_broken_event   = gs_window_real_grab_broken;

        g_type_class_add_private (klass, sizeof (GSWindowPrivate));

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
        signals [DIALOG_UP] =
                g_signal_new ("dialog-up",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWindowClass, dialog_up),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DIALOG_DOWN] =
                g_signal_new ("dialog-down",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWindowClass, dialog_down),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

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
                                         PROP_LOGOUT_TIMEOUT,
                                         g_param_spec_string ("logout-command",
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
                               | GDK_ENTER_NOTIFY_MASK
                               | GDK_LEAVE_NOTIFY_MASK);
}

static void
remove_command_watch (GSWindow *window)
{
        if (window->priv->watch_id != 0) {
                g_source_remove (window->priv->watch_id);
                window->priv->watch_id = 0;
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

        remove_watchdog_timer (window);
        remove_request_unlock_idle (window);
        remove_popup_dialog_idle (window);

        if (window->priv->timer) {
                g_timer_destroy (window->priv->timer);
        }

        remove_key_events (window);

        remove_command_watch (window);

        /* If a dialog is up we need to signal it
           and wait on it */
        if (window->priv->pid > 0) {
                signal_pid (window->priv->pid, SIGTERM);
        }
        gs_window_dialog_finish (window);

        G_OBJECT_CLASS (parent_class)->finalize (object);
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
                               NULL);

        return GS_WINDOW (result);
}
