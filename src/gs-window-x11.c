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

#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "gs-window.h"
#include "gs-marshal.h"
#include "subprocs.h"
#include "gs-debug.h"

static void gs_window_class_init (GSWindowClass *klass);
static void gs_window_init       (GSWindow      *window);
static void gs_window_finalize   (GObject       *object);

static gboolean popup_dialog_idle (GSWindow *window);

enum {
        DIALOG_RESPONSE_CANCEL,
        DIALOG_RESPONSE_OK
};

#define MAX_QUEUED_EVENTS 16

#define GS_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WINDOW, GSWindowPrivate))

struct GSWindowPrivate
{
        int        monitor;

        GdkRectangle geometry;
        guint      obscured : 1;

        guint      lock_enabled : 1;
        guint      user_switch_enabled : 1;
        guint      logout_enabled : 1;
        guint      keyboard_enabled : 1;

        guint64    logout_timeout;
        char      *logout_command;
        char      *keyboard_command;

        GtkWidget *vbox;
        GtkWidget *lock_box;
        GtkWidget *lock_socket;
        GtkWidget *keyboard_socket;

        guint      popup_dialog_idle_id;

        guint      dialog_map_signal_id;
        guint      dialog_unmap_signal_id;
        guint      dialog_response_signal_id;

        guint      watchdog_timer_id;

        gint       lock_pid;
        gint       lock_watch_id;
        gint       dialog_response;

        gint       keyboard_pid;
        gint       keyboard_watch_id;

        GList     *key_events;

        gdouble    last_x;
        gdouble    last_y;

        GTimer    *timer;
};

enum {
        ACTIVITY,
        DEACTIVATED,
        DIALOG_UP,
        DIALOG_DOWN,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_OBSCURED,
        PROP_LOCK_ENABLED,
        PROP_LOGOUT_ENABLED,
        PROP_KEYBOARD_ENABLED,
        PROP_KEYBOARD_COMMAND,
        PROP_LOGOUT_COMMAND,
        PROP_LOGOUT_TIMEOUT,
        PROP_MONITOR
};

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
force_no_pixmap_background (GtkWidget *widget)
{
        static gboolean first_time = TRUE;

        if (first_time) {
                gtk_rc_parse_string ("\n"
                                     "   style \"gs-theme-engine-style\"\n"
                                     "   {\n"
                                     "      bg_pixmap[NORMAL] = \"<none>\"\n"
                                     "      bg_pixmap[INSENSITIVE] = \"<none>\"\n"
                                     "      bg_pixmap[ACTIVE] = \"<none>\"\n"
                                     "      bg_pixmap[PRELIGHT] = \"<none>\"\n"
                                     "      bg[NORMAL] = { 0.0, 0.0, 0.0 }\n"
                                     "      bg[INSENSITIVE] = { 0.0, 0.0, 0.0 }\n"
                                     "      bg[ACTIVE] = { 0.0, 0.0, 0.0 }\n"
                                     "      bg[PRELIGHT] = { 0.0, 0.0, 0.0 }\n"
                                     "   }\n"
                                     "   widget \"gs-window*\" style : highest \"gs-theme-engine-style\"\n"
                                     "\n");
                first_time = FALSE;
        }

        gtk_widget_set_name (widget, "gs-window");
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
        gdk_error_trap_pop ();
}

void
gs_window_clear (GSWindow *window)
{
        GdkColor     color = { 0, 0x0000, 0x0000, 0x0000 };
        GdkColormap *colormap;
        GtkStateType state;

        g_return_if_fail (GS_IS_WINDOW (window));

        if (! GTK_WIDGET_VISIBLE (GTK_WIDGET (window))) {
                return;
        }

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

static gboolean
spawn_command_line_on_screen_sync (GdkScreen    *screen,
                                   const gchar  *command_line,
                                   char        **standard_output,
                                   char        **standard_error,
                                   int          *exit_status,
                                   GError      **error)
{
        char     **argv = NULL;
        char     **envp = NULL;
        gboolean   retval;

        g_return_val_if_fail (command_line != NULL, FALSE);

        if (! g_shell_parse_argv (command_line, NULL, &argv, error)) {
                return FALSE;
        }

        envp = spawn_make_environment_for_screen (screen, NULL);

        retval = g_spawn_sync (NULL,
                               argv,
                               envp,
                               G_SPAWN_SEARCH_PATH,
                               NULL,
                               NULL,
                               standard_output,
                               standard_error,
                               exit_status,
                               error);

        g_strfreev (argv);
        g_strfreev (envp);

        return retval;
}

static GdkVisual *
get_best_visual_for_screen (GdkScreen *screen)
{
        char         *command;
        char         *std_output;
        int           exit_status;
        GError       *error;
        unsigned long v;
        char          c;
        GdkVisual    *visual;
        gboolean      res;

        visual = NULL;

        command = g_build_filename (LIBEXECDIR, "gnome-screensaver-gl-helper", NULL);

        error = NULL;
        std_output = NULL;
        res = spawn_command_line_on_screen_sync (screen,
                                                 command,
                                                 &std_output,
                                                 NULL,
                                                 &exit_status,
                                                 &error);
        if (! res) {
                gs_debug ("Could not run command '%s': %s", command, error->message);
                g_error_free (error);
                goto out;
        }

        if (1 == sscanf (std_output, "0x%lx %c", &v, &c)) {
                if (v != 0) {
                        VisualID      visual_id;

                        visual_id = (VisualID) v;
                        visual = gdkx_visual_get (visual_id);

                        gs_debug ("Found best GL visual for screen %d: 0x%x",
                                  gdk_screen_get_number (screen),
                                  (unsigned int) visual_id);
                }
        }
 out:
        g_free (std_output);
        g_free (command);

        return visual;
}

static GdkColormap *
get_best_colormap_for_screen (GdkScreen *screen)
{
        GdkColormap *colormap;
        GdkVisual   *visual;

        g_return_val_if_fail (screen != NULL, NULL);

        visual = get_best_visual_for_screen (screen);

        colormap = NULL;
        if (visual != NULL) {
                colormap = gdk_colormap_new (visual, FALSE);
        }

        return colormap;
}

static void
widget_set_best_colormap (GtkWidget *widget)
{
        GdkColormap *colormap;

        g_return_if_fail (widget != NULL);

        colormap = get_best_colormap_for_screen (gtk_widget_get_screen (widget));
        if (colormap != NULL) {
                gtk_widget_set_colormap (widget, colormap);
                g_object_unref (colormap);
        }
}

static void
gs_window_real_realize (GtkWidget *widget)
{
        widget_set_best_colormap (widget);

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

        gdk_window_focus (widget->window, GDK_CURRENT_TIME);

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

        gdk_display_sync (gdk_display_get_default ());
        gdk_error_trap_pop ();
}

static void
gs_window_real_show (GtkWidget *widget)
{
        GSWindow *window;

        if (GTK_WIDGET_CLASS (gs_window_parent_class)->show) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->show (widget);
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

        if (GTK_WIDGET_CLASS (gs_window_parent_class)->hide) {
                GTK_WIDGET_CLASS (gs_window_parent_class)->hide (widget);
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
                "LD_LIBRARY_PATH",
                "SESSION_MANAGER",
                "XAUTHORITY",
                "XAUTHLOCALHOSTNAME",
                "KRB5CCNAME",
                "KRBTKFILE",
                "LANG",
                "LANGUAGE",
                "LC_ALL",
                "LC_CTYPE",
                "LC_MESSAGES",
                "LOGNAME",
                "RUNNING_UNDER_GDM",
                "GTK_DATA_PREFIX",
                "GTK_EXE_PREFIX",
                "GTK_IM_MODULE",
                "GTK_IM_MODULE_FILE",
                "GTK_MODULES",
                "GTK_PATH",
                "GTK2_RC_FILES",
                "XDG_CONFIG_DIRS",
                "XDG_DATA_DIRS",
                "XDG_CACHE_HOME",
                "XDG_CONFIG_HOME",
                "XDG_DATA_HOME",
                "MB_KBD_CONFIG",
                "MB_KBD_VARIANT",
                "MB_KBD_LANG"
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
                        char *str;
                        str = g_strdup_printf ("%s=%s", var, val);
                        g_ptr_array_add (env, str);
                        gs_debug ("adding environment: %s", str);
                }
        }

        g_ptr_array_add (env, NULL);

        return env;
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
        GPtrArray  *env;
        GError     *error;
        gboolean    result;
        GIOChannel *channel;
        int         standard_output;
        int         standard_error;
        int         child_pid;
        int         id;
        int         i;

        error = NULL;
        if (! g_shell_parse_argv (command, &argc, &argv, &error)) {
                gs_debug ("Could not parse command: %s", error->message);
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
                                                 &standard_error,
                                                 &error);

        for (i = 0; i < env->len; i++) {
                g_free (g_ptr_array_index (env, i));
        }
        g_ptr_array_free (env, TRUE);

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
keyboard_command_finish (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Keyboard finished");

        if (window->priv->keyboard_pid > 0) {
                int exit_status;

                exit_status = wait_on_child (window->priv->keyboard_pid);

                g_spawn_close_pid (window->priv->keyboard_pid);
                window->priv->keyboard_pid = 0;
        }
}

static void
kill_keyboard_command (GSWindow *window)
{
        if (window->priv->keyboard_pid > 0) {
                signal_pid (window->priv->keyboard_pid, SIGTERM);
        }
        keyboard_command_finish (window);
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
        kill_keyboard_command (window);

        if (window->priv->lock_pid > 0) {
                int exit_status;

                exit_status = wait_on_child (window->priv->lock_pid);

                g_spawn_close_pid (window->priv->lock_pid);
                window->priv->lock_pid = 0;
        }

        /* remove events for the case were we failed to show socket */
        remove_key_events (window);
}

static void
kill_dialog_command (GSWindow *window)
{
        /* If a dialog is up we need to signal it
           and wait on it */
        if (window->priv->lock_pid > 0) {
                signal_pid (window->priv->lock_pid, SIGTERM);
        }

        gs_window_dialog_finish (window);
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

                        if (strstr (line, "WINDOW ID=")) {
                                guint32 id;
                                char    c;
                                if (1 == sscanf (line, " WINDOW ID= %" G_GUINT32_FORMAT " %c", &id, &c)) {
                                        create_lock_socket (window, id);
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
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        gs_debug ("Error reading from child: %s\n", error->message);
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

        if (gs_debug_enabled ()) {
                command = g_string_append (command, " --verbose");
        }

        gs_window_clear (window);
        set_invisible_cursor (GTK_WIDGET (window)->window, FALSE);

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

        window->priv->popup_dialog_idle_id = 0;

        return FALSE;
}

void
gs_window_request_unlock (GSWindow *window)
{
        g_return_if_fail (GS_IS_WINDOW (window));

        gs_debug ("Requesting unlock");

        if (! GTK_WIDGET_VISIBLE (GTK_WIDGET (window))) {
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
        case PROP_LOGOUT_TIMEOUT:
                g_value_set_long (value, self->priv->logout_timeout);
                break;
        case PROP_MONITOR:
                g_value_set_int (value, self->priv->monitor);
                break;
        case PROP_OBSCURED:
                g_value_set_boolean (value, self->priv->obscured);
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
            && (event->keyval == GDK_Return
                || event->keyval == GDK_KP_Enter
                || event->keyval == GDK_Escape
                || event->keyval == GDK_space)) {
                return;
        }

        /* Only cache MAX_QUEUED_EVENTS key events.  If there are any more than this then
           something is wrong */
        /* Don't queue keys that may cause focus navigation in the dialog */
        if (g_list_length (window->priv->key_events) < MAX_QUEUED_EVENTS
            && event->keyval != GDK_Tab
            && event->keyval != GDK_Up
            && event->keyval != GDK_Down) {
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
            && GTK_WIDGET_IS_SENSITIVE (GTK_WIDGET (window))) {
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
        GSWindow *window;
        gdouble   distance;
        gdouble   min_distance = 50;

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

gboolean
gs_window_is_obscured (GSWindow *window)
{
        g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

        return window->priv->obscured;
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
        widget_class->size_request        = gs_window_real_size_request;
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
                                         PROP_OBSCURED,
                                         g_param_spec_boolean ("obscured",
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
        force_no_pixmap_background (GTK_WIDGET (window));

        window->priv->vbox = gtk_vbox_new (FALSE, 12);
        gtk_widget_show (window->priv->vbox);
        gtk_container_add (GTK_CONTAINER (window), window->priv->vbox);
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

        remove_watchdog_timer (window);
        remove_popup_dialog_idle (window);

        if (window->priv->timer) {
                g_timer_destroy (window->priv->timer);
        }

        remove_key_events (window);

        remove_command_watches (window);

        kill_keyboard_command (window);
        kill_dialog_command (window);

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
                               NULL);

        return GS_WINDOW (result);
}
