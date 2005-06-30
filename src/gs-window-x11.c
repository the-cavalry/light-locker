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
#include <gtk/gtk.h>

#include "gs-window.h"
#include "subprocs.h"

static void gs_window_class_init (GSWindowClass *klass);
static void gs_window_init       (GSWindow      *window);
static void gs_window_finalize   (GObject       *object);

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
        guint      logout_enabled : 1;
        guint64    logout_timeout;

        GtkWidget *box;
        GtkWidget *socket;

        guint      request_unlock_idle_id;
        guint      popup_dialog_idle_id;

        guint      dialog_map_signal_id;
        guint      dialog_unmap_signal_id;
        guint      dialog_response_signal_id;

        gint       pid;
        gint       watch_id;
        gint       dialog_response;

        GTimer    *timer;
};

enum {
        UNBLANKED,
        DIALOG_UP,
        DIALOG_DOWN,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_LOGOUT_TIMEOUT,
        PROP_MONITOR
};

static GObjectClass   *parent_class = NULL;
static guint           signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GSWindow, gs_window, GTK_TYPE_WINDOW);

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

        if (cursor)
                gdk_cursor_unref (cursor);
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

void
gs_window_clear (GSWindow *window)
{
        GdkColor     color = { 0, 0, 0 };
        GdkColormap *colormap;

        gtk_widget_modify_bg (GTK_WIDGET (window), GTK_STATE_NORMAL, &color);
        colormap = gdk_drawable_get_colormap (GTK_WIDGET (window)->window);
        gdk_colormap_alloc_color (colormap, &color, FALSE, TRUE);
        gdk_window_set_background (GTK_WIDGET (window)->window, &color);
        gdk_window_clear (GTK_WIDGET (window)->window);
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

        if (move && resize)
                gdk_window_move_resize (widget->window,
                                        window->priv->geometry.x,
                                        window->priv->geometry.y,
                                        window->priv->geometry.width,
                                        window->priv->geometry.height);
        else if (move)
                gdk_window_move (widget->window,
                                 window->priv->geometry.x,
                                 window->priv->geometry.y);
        else if (resize)
                gdk_window_resize (widget->window,
                                   window->priv->geometry.width,
                                   window->priv->geometry.height);
}

static void
gs_window_real_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (parent_class)->realize)
                GTK_WIDGET_CLASS (parent_class)->realize (widget);

        gs_window_override_user_time (GS_WINDOW (widget));
        gs_window_clear (GS_WINDOW (widget));

        gs_window_move_resize_window (GS_WINDOW (widget), TRUE, TRUE);

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (screen_size_changed),
                          widget);
}

static void
gs_window_real_show (GtkWidget *widget)
{
        GSWindow *window;

        if (GTK_WIDGET_CLASS (parent_class)->show)
                GTK_WIDGET_CLASS (parent_class)->show (widget);

        set_invisible_cursor (widget->window, TRUE);

        window = GS_WINDOW (widget);
        if (window->priv->timer)
                g_timer_destroy (window->priv->timer);
        window->priv->timer = g_timer_new ();
}

void
gs_window_show (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gtk_widget_show (GTK_WIDGET (window));
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

static gboolean
emit_unblanked_idle (GSWindow *window)
{
        g_signal_emit (window, signals [UNBLANKED], 0);

        return FALSE;
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
        char       *envp [5];
        int         nenv = 0;
        int         i;
        gboolean    result;
        GIOChannel *channel;
        int         standard_output;
        int         child_pid;
        int         id;

        if (!g_shell_parse_argv (command, &argc, &argv, NULL))
                return FALSE;

        envp[nenv++] = g_strdup_printf ("DISPLAY=%s",
                                        gdk_display_get_name (gdk_display_get_default ()));
        envp[nenv++] = g_strdup_printf ("HOME=%s",
                                        g_get_home_dir ());
        envp[nenv++] = g_strdup_printf ("PATH=%s", g_getenv ("PATH"));
        envp[nenv++] = g_strdup_printf ("SESSION_MANAGER=%s", g_getenv ("SESSION_MANAGER"));
        envp[nenv++] = NULL;

        result = gdk_spawn_on_screen_with_pipes (GTK_WINDOW (window)->screen,
                                                 g_get_home_dir (),
                                                 argv,
                                                 envp,
                                                 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                                 NULL,
                                                 NULL,
                                                 &child_pid,
                                                 NULL,
                                                 &standard_output,
                                                 NULL,
                                                 NULL);

        if (!result)
                return FALSE;

        if (pid)
                *pid = child_pid;
        else
                g_spawn_close_pid (child_pid);

        channel = g_io_channel_unix_new (standard_output);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        id = g_io_add_watch (channel,
                             G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                             watch_func,
                             user_data);
        if (watch_id)
                *watch_id = id;

        g_io_channel_unref (channel);

        for (i = 0; i < nenv; i++)
                g_free (envp [i]);

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

        return TRUE;
}

static void
socket_show (GtkWidget *widget,
             GSWindow  *window)
{
        gtk_widget_child_focus (window->priv->socket, GTK_DIR_TAB_FORWARD);
}

static void
socket_destroyed (GtkWidget *widget,
                  GSWindow  *window)
{
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
                if (errno == EINTR)
                        goto wait_again;
                else if (errno == ECHILD)
                        ; /* do nothing, child already reaped */
                else
                        g_warning ("waitpid () should not fail in 'GSWindow'");
        }

        return status;
}

static void
gs_window_dialog_finish (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->pid > 0) {
                int exit_status;
                        
                exit_status = wait_on_child (window->priv->pid);
        }

        g_spawn_close_pid (window->priv->pid);
        window->priv->pid = 0;
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
                        /*g_message ("LINE: %s", line);*/

                        if (strstr (line, "WINDOW ID=")) {
                                guint32 id;
                                char    c;
                                if (1 == sscanf (line, " WINDOW ID= 0x%x %c", &id, &c)) {
                                        create_socket (window, id);
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

        } else if (condition & G_IO_HUP)
                finished = TRUE;

        if (finished) {
                gs_window_dialog_finish (window);

                if (window->priv->dialog_response == DIALOG_RESPONSE_OK) {
                        g_idle_add ((GSourceFunc)emit_unblanked_idle, window);
                }

                gs_window_clear (window);
                set_invisible_cursor (GTK_WIDGET (window)->window, TRUE);
                g_signal_emit (window, signals [DIALOG_DOWN], 0);

                window->priv->watch_id = 0;
                return FALSE;
        }

        return TRUE;
}

static gboolean
is_logout_enabled (GSWindow *window)
{
        double elapsed;

        if (! window->priv->logout_enabled)
                return FALSE;

        elapsed = g_timer_elapsed (window->priv->timer, NULL);

        if (window->priv->logout_timeout < (elapsed * 1000))
                return TRUE;

        return FALSE;
}

static gboolean
popup_dialog_idle (GSWindow *window)
{
        gboolean  result;
        char     *command;

        command = g_build_filename (LIBEXECDIR, "gnome-screensaver-dialog", NULL);

        if (is_logout_enabled (window)) {
                char *cmd;

                cmd = g_strdup_printf ("%s --enable-logout", command);
                g_free (command);
                command = cmd;
        }

        gs_window_clear (window);
        set_invisible_cursor (GTK_WIDGET (window)->window, FALSE);

        result = spawn_on_window (window,
                                  command,
                                  &window->priv->pid,
                                  (GIOFunc)command_watch,
                                  window,
                                  &window->priv->watch_id);
        if (!result)
                g_warning ("Could not start command: %s", command);

        g_free (command);

        window->priv->popup_dialog_idle_id = 0;

        return FALSE;
}

void
gs_window_request_unlock (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->watch_id)
                return;

        if (! window->priv->lock_enabled) {
                g_idle_add ((GSourceFunc)emit_unblanked_idle, window);
                return;
        }

        if (window->priv->popup_dialog_idle_id == 0) {
                window->priv->popup_dialog_idle_id = g_idle_add ((GSourceFunc)popup_dialog_idle, window);
        }

        g_signal_emit (window, signals [DIALOG_UP], 0);
}

void
gs_window_set_lock_enabled (GSWindow *window,
                            gboolean  lock_enabled)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->lock_enabled == lock_enabled)
                return;

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
gs_window_set_logout_timeout (GSWindow *window,
                              glong     logout_timeout)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (logout_timeout < 0)
                window->priv->logout_timeout = 0;
        else
                window->priv->logout_timeout = logout_timeout;
}

void
gs_window_set_monitor (GSWindow *window,
                       int       monitor)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        if (window->priv->monitor == monitor)
                return;

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

static gboolean
gs_window_real_key_press_event (GtkWidget   *widget,
                                GdkEventKey *event)
{
        /*g_message ("KEY PRESS state: %u keyval %u", event->state, event->keyval);*/

        if (GS_WINDOW (widget)->priv->request_unlock_idle_id == 0) {
                GS_WINDOW (widget)->priv->request_unlock_idle_id = g_idle_add ((GSourceFunc)gs_window_request_unlock_idle, widget);
        }

        if (GTK_WIDGET_CLASS (parent_class)->key_press_event)
                GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event);

        return TRUE;
}

static gboolean
gs_window_real_motion_notify_event (GtkWidget      *widget,
                                    GdkEventMotion *event)
{
        if (GS_WINDOW (widget)->priv->request_unlock_idle_id == 0) {
                GS_WINDOW (widget)->priv->request_unlock_idle_id = g_idle_add ((GSourceFunc)gs_window_request_unlock_idle, widget);
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

        if (bin->child && GTK_WIDGET_VISIBLE (bin->child))
                gtk_widget_size_request (bin->child, requisition);

	old_geometry = window->priv->geometry;

        update_geometry (window);

        requisition->width  = window->priv->geometry.width;
        requisition->height = window->priv->geometry.height;

        if (! GTK_WIDGET_REALIZED (widget))
                return;

        if (old_geometry.width  != window->priv->geometry.width ||
            old_geometry.height != window->priv->geometry.height)
                size_changed = TRUE;

        if (old_geometry.x != window->priv->geometry.x ||
            old_geometry.y != window->priv->geometry.y)
                position_changed = TRUE;

        gs_window_move_resize_window (window, position_changed, size_changed);
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
        widget_class->realize             = gs_window_real_realize;
        widget_class->key_press_event     = gs_window_real_key_press_event;
        widget_class->motion_notify_event = gs_window_real_motion_notify_event;
        widget_class->size_request        = gs_window_real_size_request;

        g_type_class_add_private (klass, sizeof (GSWindowPrivate));

        signals [UNBLANKED] =
                g_signal_new ("unblanked",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GSWindowClass, unblanked),
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
                                                               TRUE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_MONITOR,
                                         g_param_spec_int ("monitor",
                                                           "Xinerama monitor",
                                                           "The monitor (in terms of Xinerama) which the panel is on",
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
gs_window_finalize (GObject *object)
{
        GSWindow *window;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_WINDOW (object));

        window = GS_WINDOW (object);

        g_return_if_fail (window->priv != NULL);

        if (window->priv->request_unlock_idle_id)
                g_source_remove (window->priv->request_unlock_idle_id);
        if (window->priv->popup_dialog_idle_id)
                g_source_remove (window->priv->popup_dialog_idle_id);

        if (window->priv->timer)
                g_timer_destroy (window->priv->timer);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GSWindow *
gs_window_new (GdkScreen *screen,
               int        monitor,
               gboolean   lock_enabled)
{
        GObject     *result;

        result = g_object_new (GS_TYPE_WINDOW,
                               "screen", screen,
                               "monitor", monitor,
                               "lock-enabled", lock_enabled,
                               NULL);

        return GS_WINDOW (result);
}
