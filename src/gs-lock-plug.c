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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#include <gtk/gtk.h>
#include <glade/glade-xml.h>
#include <gconf/gconf-client.h>

/* for fast user switching */
#include <libgnomevfs/gnome-vfs-init.h>

#include "gs-lock-plug.h"

#include "gs-debug.h"

#include "fusa-manager.h"

#define KEY_LOCK_DIALOG_THEME "/apps/gnome-screensaver/lock_dialog_theme"
#define KEY_TRY_AUTH_FIRST    "/apps/gnome-screensaver/try_auth_first"

enum {
        AUTH_PAGE = 0,
        SWITCH_PAGE
};

#define FACE_ICON_SIZE 48
#define DIALOG_TIMEOUT_MSEC 60000

static void gs_lock_plug_finalize   (GObject         *object);

#define GS_LOCK_PLUG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LOCK_PLUG, GSLockPlugPrivate))

struct GSLockPlugPrivate
{
        GtkWidget   *vbox;
        GtkWidget   *auth_action_area;
        GtkWidget   *switch_action_area;

        GtkWidget   *notebook;
        GtkWidget   *auth_face_image;
        GtkWidget   *auth_realname_label;
        GtkWidget   *auth_username_label;
        GtkWidget   *auth_prompt_label;
        GtkWidget   *auth_prompt_entry;
        GtkWidget   *auth_prompt_box;
        GtkWidget   *auth_capslock_label;
        GtkWidget   *auth_message_label;
        GtkWidget   *switch_user_treeview;

        GtkWidget   *auth_unlock_button;
        GtkWidget   *auth_switch_button;
        GtkWidget   *auth_cancel_button;
        GtkWidget   *auth_logout_button;
        GtkWidget   *switch_cancel_button;
        GtkWidget   *switch_switch_button;

        FusaManager *fusa_manager;

        gboolean     caps_lock_on;
        gboolean     switch_enabled;
        gboolean     logout_enabled;
        char        *logout_command;

        guint        timeout;

        guint        idle_id;
        guint        auth_check_idle_id;
        guint        response_idle_id;

        GTimeVal     start_time;
};

typedef struct _ResponseData ResponseData;

struct _ResponseData
{
        gint response_id;
};


enum {
        RESPONSE,
        CLOSE,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOGOUT_ENABLED,
        PROP_LOGOUT_COMMAND,
        PROP_SWITCH_ENABLED
};

static guint lock_plug_signals [LAST_SIGNAL];

G_DEFINE_TYPE (GSLockPlug, gs_lock_plug, GTK_TYPE_PLUG)

static void
gs_lock_plug_style_set (GtkWidget *widget,
                        GtkStyle  *previous_style)
{
        GSLockPlug *plug;

        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->style_set) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->style_set (widget, previous_style);
        }

        plug = GS_LOCK_PLUG (widget);

        if (! plug->priv->vbox) {
                return;
        }

        gtk_container_set_border_width (GTK_CONTAINER (plug->priv->vbox), 24);
        gtk_box_set_spacing (GTK_BOX (plug->priv->vbox), 12);

        gtk_container_set_border_width (GTK_CONTAINER (plug->priv->auth_action_area), 0);
        gtk_box_set_spacing (GTK_BOX (plug->priv->auth_action_area), 5);
}

static void
manager_new_console_cb (FusaManager  *manager,
			FusaDisplay  *display,
			const GError *error,
			gpointer      data)
{
        GSLockPlug *plug = data;
        g_signal_emit (plug,
                       lock_plug_signals [RESPONSE],
                       0,
                       GS_LOCK_PLUG_RESPONSE_CANCEL);
}

static void
do_user_switch (GSLockPlug  *plug,
                FusaDisplay *display)
{
        GdkScreen *screen;

        if (gtk_widget_has_screen (plug->priv->switch_user_treeview)) {
                screen = gtk_widget_get_screen (plug->priv->switch_user_treeview);
        } else {
                screen = gdk_screen_get_default ();
        }

        if (display) {
                fusa_manager_activate_display (plug->priv->fusa_manager, display, screen,
                                               manager_new_console_cb, plug, NULL);
                return;
        }

        fusa_manager_new_console (plug->priv->fusa_manager, screen,
                                  manager_new_console_cb, plug, NULL);
}

enum {
        REAL_NAME_COLUMN,
        USER_NAME_COLUMN,
        DISPLAY_LABEL_COLUMN,
        ACTIVE_COLUMN,
        PIXBUF_COLUMN,
        N_COLUMNS
};

static void
switch_user_response (GSLockPlug *plug)
{
        FusaDisplay      *display = NULL;
        FusaUser         *user;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;
        GSList           *displays;
        char             *name;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (plug->priv->switch_user_treeview));
        gtk_tree_selection_get_selected (selection,
                                         &model,
                                         &iter);
        gtk_tree_model_get (model, &iter, USER_NAME_COLUMN, &name, -1);
        if (name
            && strcmp (name, "__new_user") != 0
            && strcmp (name, "__separator") != 0) {
                user = fusa_manager_get_user (plug->priv->fusa_manager, name);
                displays = fusa_user_get_displays (user);
                if (displays) {
                        /* FIXME: just pick the first one for now */
                        display = displays->data;
                }
        }

        g_free (name);

        do_user_switch (plug, display);
}

static void
set_status_text (GSLockPlug *plug,
                 const char *text)
{
        if (plug->priv->auth_message_label != NULL) {
                gtk_label_set_text (GTK_LABEL (plug->priv->auth_message_label), text);
        }
}

void
gs_lock_plug_set_sensitive (GSLockPlug *plug,
                            gboolean    sensitive)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, sensitive);
        gtk_widget_set_sensitive (plug->priv->auth_action_area, sensitive);
}

static void
remove_monitor_idle (GSLockPlug *plug)
{
        if (plug->priv->idle_id > 0) {
                g_source_remove (plug->priv->idle_id);
                plug->priv->idle_id = 0;
        }
}

static void
remove_response_idle (GSLockPlug *plug)
{
        if (plug->priv->response_idle_id > 0) {
                g_source_remove (plug->priv->response_idle_id);
                plug->priv->response_idle_id = 0;
        }
}

static void
gs_lock_plug_response (GSLockPlug *plug,
                       gint        response_id)
{
        int new_response;

        new_response = response_id;

        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        /* Act only on response IDs we recognize */
        if (!(response_id == GS_LOCK_PLUG_RESPONSE_OK
              || response_id == GS_LOCK_PLUG_RESPONSE_CANCEL)) {
                return;
        }

        remove_monitor_idle (plug);
        remove_response_idle (plug);

        if (response_id == GS_LOCK_PLUG_RESPONSE_CANCEL) {
                gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), "");
        }

        if (response_id == GS_LOCK_PLUG_RESPONSE_OK) {
                gint current_page = AUTH_PAGE;

                if (plug->priv->notebook != NULL) {
                        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (plug->priv->notebook));
                }

                if (current_page != AUTH_PAGE) {
                        /* switch user */
                        switch_user_response (plug);
                        return;
                }
        }

        g_signal_emit (plug,
                       lock_plug_signals [RESPONSE],
                       0,
                       new_response);
}

static gboolean
response_cancel_idle_cb (GSLockPlug *plug)
{
        plug->priv->response_idle_id = 0;

        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);

        return FALSE;
}

static gboolean
monitor_progress (GSLockPlug *plug)
{
        GTimeVal now;
        glong    elapsed;
        glong    remaining;

        g_get_current_time (&now);

        elapsed = (now.tv_sec - plug->priv->start_time.tv_sec) * 1000
                + (now.tv_usec - plug->priv->start_time.tv_usec) / 1000;
        remaining = plug->priv->timeout - elapsed;

        if ((remaining <= 0) || (remaining > plug->priv->timeout)) {
                gs_lock_plug_set_sensitive (plug, FALSE);
                set_status_text (plug, _("Time has expired."));

                if (plug->priv->response_idle_id != 0) {
                        g_warning ("Response idle ID already set but shouldn't be");
                }

                remove_response_idle (plug);

                plug->priv->response_idle_id = g_timeout_add (2000,
                                                              (GSourceFunc)response_cancel_idle_cb,
                                                              plug);
                return FALSE;
        }
        return TRUE;
}


static void
capslock_update (GSLockPlug *plug,
                 gboolean    is_on)
{

        plug->priv->caps_lock_on = is_on;

        if (plug->priv->auth_capslock_label == NULL) {
                return;
        }

        if (is_on) {
                gtk_label_set_text (GTK_LABEL (plug->priv->auth_capslock_label),
                                    _("You have the Caps Lock key on."));
        } else {
                gtk_label_set_text (GTK_LABEL (plug->priv->auth_capslock_label),
                                    "");
        }
}

/* adapted from GDM2 */
static gboolean
is_capslock_on (void)
{
        unsigned int states;
        Display     *dsp;

        dsp = GDK_DISPLAY ();

        if (XkbGetIndicatorState (dsp, XkbUseCoreKbd, &states) != Success) {
                return FALSE;
        }

        return (states & ShiftMask) != 0;
}

static void
restart_monitor_progress (GSLockPlug *plug)
{
        remove_monitor_idle (plug);

        g_get_current_time (&plug->priv->start_time);
        plug->priv->idle_id = g_timeout_add (50,
                                             (GSourceFunc)monitor_progress,
                                             plug);
}

void
gs_lock_plug_get_text (GSLockPlug *plug,
                       char      **text)
{
        const char *typed_text;
        char       *null_text;
        char       *local_text;

        typed_text = gtk_entry_get_text (GTK_ENTRY (plug->priv->auth_prompt_entry));
        local_text = g_locale_from_utf8 (typed_text, strlen (typed_text), NULL, NULL, NULL);

        null_text = g_strnfill (strlen (typed_text) + 1, '\b');
        gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), null_text);
        gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), "");
        g_free (null_text);

        if (text != NULL) {
                *text = local_text;
        }
}

typedef struct
{
        GSLockPlug *plug;
        gint response_id;
        GMainLoop *loop;
        gboolean destroyed;
} RunInfo;

static void
shutdown_loop (RunInfo *ri)
{
        if (g_main_loop_is_running (ri->loop))
                g_main_loop_quit (ri->loop);
}

static void
run_unmap_handler (GSLockPlug *plug,
                   gpointer data)
{
        RunInfo *ri = data;

        shutdown_loop (ri);
}

static void
run_response_handler (GSLockPlug *plug,
                      gint response_id,
                      gpointer data)
{
        RunInfo *ri;

        ri = data;

        ri->response_id = response_id;

        shutdown_loop (ri);
}

static gint
run_delete_handler (GSLockPlug *plug,
                    GdkEventAny *event,
                    gpointer data)
{
        RunInfo *ri = data;

        shutdown_loop (ri);

        return TRUE; /* Do not destroy */
}

static void
run_destroy_handler (GSLockPlug *plug,
                     gpointer data)
{
        RunInfo *ri = data;

        /* shutdown_loop will be called by run_unmap_handler */
        ri->destroyed = TRUE;
}

/* adapted from GTK+ gtkdialog.c */
int
gs_lock_plug_run (GSLockPlug *plug)
{
        RunInfo ri = { NULL, GTK_RESPONSE_NONE, NULL, FALSE };
        gboolean was_modal;
        gulong response_handler;
        gulong unmap_handler;
        gulong destroy_handler;
        gulong delete_handler;

        g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), -1);

        g_object_ref (plug);

        was_modal = GTK_WINDOW (plug)->modal;
        if (!was_modal) {
                gtk_window_set_modal (GTK_WINDOW (plug), TRUE);
        }

        if (!GTK_WIDGET_VISIBLE (plug)) {
                gtk_widget_show (GTK_WIDGET (plug));
        }

        response_handler =
                g_signal_connect (plug,
                                  "response",
                                  G_CALLBACK (run_response_handler),
                                  &ri);

        unmap_handler =
                g_signal_connect (plug,
                                  "unmap",
                                  G_CALLBACK (run_unmap_handler),
                                  &ri);

        delete_handler =
                g_signal_connect (plug,
                                  "delete_event",
                                  G_CALLBACK (run_delete_handler),
                                  &ri);

        destroy_handler =
                g_signal_connect (plug,
                                  "destroy",
                                  G_CALLBACK (run_destroy_handler),
                                  &ri);

        ri.loop = g_main_loop_new (NULL, FALSE);

        GDK_THREADS_LEAVE ();
        g_main_loop_run (ri.loop);
        GDK_THREADS_ENTER ();

        g_main_loop_unref (ri.loop);

        ri.loop = NULL;

        if (!ri.destroyed) {
                if (! was_modal) {
                        gtk_window_set_modal (GTK_WINDOW (plug), FALSE);
                }

                g_signal_handler_disconnect (plug, response_handler);
                g_signal_handler_disconnect (plug, unmap_handler);
                g_signal_handler_disconnect (plug, delete_handler);
                g_signal_handler_disconnect (plug, destroy_handler);
        }

        g_object_unref (plug);

        return ri.response_id;
}

static void
gs_lock_plug_show (GtkWidget *widget)
{
        GSLockPlug *plug = GS_LOCK_PLUG (widget);

        gs_profile_start (NULL);

        gs_profile_start ("parent");
        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->show) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->show (widget);
        }

        gs_profile_end ("parent");

        capslock_update (plug, is_capslock_on ());

        restart_monitor_progress (plug);

        gs_profile_end (NULL);
}

static void
gs_lock_plug_hide (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->hide) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->hide (widget);
        }
}


static void
gs_lock_plug_size_request (GtkWidget      *widget,
                           GtkRequisition *requisition)
{
        GSLockPlug *plug = GS_LOCK_PLUG (widget);
        int mod_width;
        int mod_height;

        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->size_request) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->size_request (widget, requisition);
        }

        /* don't constrain size when themed */
        if (plug->priv->vbox == NULL) {
                return;
        }

        mod_width = requisition->height * 1.3;
        mod_height = requisition->width / 1.6;
        if (requisition->width < mod_width) {
                /* if the dialog is tall fill out the width */
                requisition->width = mod_width;
        } else if (requisition->height < mod_height) {
                /* if the dialog is wide fill out the height */
                requisition->height = mod_height;
        }
}

static void
gs_lock_plug_set_logout_enabled (GSLockPlug *plug,
                                 gboolean    logout_enabled)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        if (plug->priv->logout_enabled == logout_enabled) {
                return;
        }

        plug->priv->logout_enabled = logout_enabled;
        g_object_notify (G_OBJECT (plug), "logout-enabled");

        if (plug->priv->auth_logout_button == NULL) {
                return;
        }

        if (logout_enabled) {
                gtk_widget_show (plug->priv->auth_logout_button);
        } else {
                gtk_widget_hide (plug->priv->auth_logout_button);
        }
}

static void
gs_lock_plug_set_logout_command (GSLockPlug *plug,
                                 const char *command)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        g_free (plug->priv->logout_command);

        if (command) {
                plug->priv->logout_command = g_strdup (command);
        } else {
                plug->priv->logout_command = NULL;
        }
}

static void
gs_lock_plug_set_switch_enabled (GSLockPlug *plug,
                                 gboolean    switch_enabled)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        if (plug->priv->switch_enabled == switch_enabled) {
                return;
        }

        plug->priv->switch_enabled = switch_enabled;
        g_object_notify (G_OBJECT (plug), "switch-enabled");

        if (switch_enabled) {
                gtk_widget_show (plug->priv->auth_switch_button);
        } else {
                gtk_widget_hide (plug->priv->auth_switch_button);
        }
}

static void
gs_lock_plug_set_property (GObject            *object,
                           guint               prop_id,
                           const GValue       *value,
                           GParamSpec         *pspec)
{
        GSLockPlug *self;

        self = GS_LOCK_PLUG (object);

        switch (prop_id) {
        case PROP_LOGOUT_ENABLED:
                gs_lock_plug_set_logout_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOGOUT_COMMAND:
                gs_lock_plug_set_logout_command (self, g_value_get_string (value));
                break;
        case PROP_SWITCH_ENABLED:
                gs_lock_plug_set_switch_enabled (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_lock_plug_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        GSLockPlug *self;

        self = GS_LOCK_PLUG (object);

        switch (prop_id) {
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
                break;
        case PROP_LOGOUT_COMMAND:
                g_value_set_string (value, self->priv->logout_command);
                break;
        case PROP_SWITCH_ENABLED:
                g_value_set_boolean (value, self->priv->switch_enabled);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_lock_plug_close (GSLockPlug *plug)
{
        /* Synthesize delete_event to close dialog. */

        GtkWidget *widget = GTK_WIDGET (plug);
        GdkEvent  *event;

        event = gdk_event_new (GDK_DELETE);
        event->any.window = g_object_ref (widget->window);
        event->any.send_event = TRUE;

        gtk_main_do_event (event);
        gdk_event_free (event);
}

static void
gs_lock_plug_class_init (GSLockPlugClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
        GtkBindingSet  *binding_set;

        object_class->finalize     = gs_lock_plug_finalize;
        object_class->get_property = gs_lock_plug_get_property;
        object_class->set_property = gs_lock_plug_set_property;

        widget_class->style_set    = gs_lock_plug_style_set;
        widget_class->show         = gs_lock_plug_show;
        widget_class->hide         = gs_lock_plug_hide;
        widget_class->size_request = gs_lock_plug_size_request;

        klass->close = gs_lock_plug_close;

        g_type_class_add_private (klass, sizeof (GSLockPlugPrivate));

        lock_plug_signals [RESPONSE] = g_signal_new ("response",
                                                     G_OBJECT_CLASS_TYPE (klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GSLockPlugClass, response),
                                                     NULL, NULL,
                                                     g_cclosure_marshal_VOID__INT,
                                                     G_TYPE_NONE, 1,
                                                     G_TYPE_INT);
        lock_plug_signals [CLOSE] = g_signal_new ("close",
                                                  G_OBJECT_CLASS_TYPE (klass),
                                                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                  G_STRUCT_OFFSET (GSLockPlugClass, close),
                                                  NULL, NULL,
                                                  g_cclosure_marshal_VOID__VOID,
                                                  G_TYPE_NONE, 0);

        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_ENABLED,
                                         g_param_spec_boolean ("logout-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_COMMAND,
                                         g_param_spec_string ("logout-command",
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SWITCH_ENABLED,
                                         g_param_spec_boolean ("switch-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        binding_set = gtk_binding_set_by_class (klass);

        gtk_binding_entry_add_signal (binding_set, GDK_Escape, 0,
                                      "close", 0);
}

static void
switch_page (GSLockPlug *plug,
             GtkButton  *button)
{
        gint       current_page;
        gint       next_page;

        g_return_if_fail (plug != NULL);

        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (plug->priv->notebook));
        next_page = (current_page == AUTH_PAGE) ? SWITCH_PAGE : AUTH_PAGE;

        gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), next_page);

        /* this counts as activity so restart the timer */
        restart_monitor_progress (plug);
}

static void
logout_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        char   **argv  = NULL;
        GError  *error = NULL;
        gboolean res;

        if (! plug->priv->logout_command) {
                return;
        }

        res = g_shell_parse_argv (plug->priv->logout_command, NULL, &argv, &error);

        if (! res) {
                g_warning ("Could not parse logout command: %s", error->message);
                g_error_free (error);
                return;
        }

        g_spawn_async (g_get_home_dir (),
                       argv,
                       NULL,
                       G_SPAWN_SEARCH_PATH,
                       NULL,
                       NULL,
                       NULL,
                       &error);

        g_strfreev (argv);

        if (error) {
                g_warning ("Could not run logout command: %s", error->message);
                g_error_free (error);
        }
}

void
gs_lock_plug_show_prompt (GSLockPlug *plug,
                          const char *message,
                          gboolean    visible)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        gs_debug ("Setting prompt to: %s", message);

        gtk_label_set_text (GTK_LABEL (plug->priv->auth_prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (plug->priv->auth_prompt_entry), visible);
        gtk_widget_grab_focus (plug->priv->auth_prompt_entry);

        restart_monitor_progress (plug);
}

void
gs_lock_plug_show_message (GSLockPlug *plug,
                           const char *message)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        set_status_text (plug, message ? message : "");
}

/* button press handler used to inhibit popup menu */
static gint
entry_button_press (GtkWidget      *widget,
                    GdkEventButton *event)
{
        if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
                return TRUE;
        }

        return FALSE;
}

static gint
entry_key_press (GtkWidget   *widget,
                 GdkEventKey *event,
                 GSLockPlug  *plug)
{
        gboolean capslock_on;

        restart_monitor_progress (plug);

        capslock_on = is_capslock_on ();

        if (capslock_on != plug->priv->caps_lock_on) {
                capslock_update (plug, capslock_on);
        }

        return FALSE;
}

/* adapted from gtk_dialog_add_button */
static GtkWidget *
gs_lock_plug_add_button (GSLockPlug  *plug,
                         GtkWidget   *action_area,
                         const gchar *button_text)
{
        GtkWidget *button;

        g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), NULL);
        g_return_val_if_fail (button_text != NULL, NULL);

        button = gtk_button_new_from_stock (button_text);

        GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);

        gtk_widget_show (button);

        gtk_box_pack_end (GTK_BOX (action_area),
                          button,
                          FALSE, TRUE, 0);

        return button;
}

typedef struct
{
        GtkTreeIter iter;
        GtkWidget  *tree;
} DisplayChangedData;

static char *
get_user_display_label (FusaUser *user)
{
        char *label;

        label = g_strdup_printf ("<big>%s</big>\n<small>%s</small>",
                                 fusa_user_get_display_name (user),
                                 fusa_user_get_user_name (user));

        return label;
}

static void
user_displays_changed_cb (FusaUser           *user,
                          DisplayChangedData *data)
{
        const char   *name;
        gboolean      is_active;
        int           n_displays;
        GdkPixbuf    *pixbuf;
        int           icon_size = FACE_ICON_SIZE;
        GtkTreeModel *filter_model;
        GtkTreeModel *model;
        char         *label;

        name = fusa_user_get_user_name (user);
        n_displays = fusa_user_get_n_displays (user);
        is_active = n_displays > 0;
        pixbuf = fusa_user_render_icon (user, data->tree, icon_size);
        label = get_user_display_label (user);

        filter_model = gtk_tree_view_get_model (GTK_TREE_VIEW (data->tree));
        model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (filter_model));

        gtk_list_store_set (GTK_LIST_STORE (model), &data->iter,
                            USER_NAME_COLUMN, name,
                            REAL_NAME_COLUMN, fusa_user_get_display_name (user),
                            DISPLAY_LABEL_COLUMN, label,
                            ACTIVE_COLUMN, is_active,
                            PIXBUF_COLUMN, pixbuf,
                            -1);
        g_free (label);
}

static void
populate_model (GSLockPlug   *plug,
                GtkListStore *store)
{
        GtkTreeIter   iter;
        GSList       *users;
        GdkPixbuf    *pixbuf;
        int           icon_size = FACE_ICON_SIZE;
        GtkIconTheme *theme;

        gs_profile_start (NULL);

        if (gtk_widget_has_screen (plug->priv->switch_user_treeview)) {
                theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (plug->priv->switch_user_treeview));
        } else {
                theme = gtk_icon_theme_get_default ();
        }

        pixbuf = gtk_icon_theme_load_icon (theme, "gdm", icon_size, 0, NULL);

        gs_profile_start ("FUSA list users");
        if (! plug->priv->fusa_manager) {
                if (! g_thread_supported ()) {
                        gs_profile_start ("g_thread_init");
                        g_thread_init (NULL);
                        gs_profile_end ("g_thread_init");
                }
                gs_profile_start ("gnome_vfs_init");
                gnome_vfs_init ();
                gs_profile_end ("gnome_vfs_init");

                gs_profile_start ("fusa_manager_ref_default");
                plug->priv->fusa_manager = fusa_manager_ref_default ();
                gs_profile_end ("fusa_manager_ref_default");
        }

        users = fusa_manager_list_users (plug->priv->fusa_manager);
        gs_profile_end ("FUSA list users");

        while (users) {
                FusaUser           *user;
                gboolean            is_active;
                guint               n_displays;
                DisplayChangedData *ddata;
                char               *label;

                user = users->data;

                /* skip the current user */
                if (fusa_user_get_uid (user) == getuid ()) {
                        users = g_slist_delete_link (users, users);
                        continue;
                }

                n_displays = fusa_user_get_n_displays (user);
                is_active = n_displays > 0;

                pixbuf = fusa_user_render_icon (user, plug->priv->switch_user_treeview, icon_size);

                label = get_user_display_label (user);

                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    USER_NAME_COLUMN, fusa_user_get_user_name (user),
                                    REAL_NAME_COLUMN, fusa_user_get_display_name (user),
                                    DISPLAY_LABEL_COLUMN, label,
                                    ACTIVE_COLUMN, is_active,
                                    PIXBUF_COLUMN, pixbuf,
                                    -1);

                g_free (label);

                ddata = g_new0 (DisplayChangedData, 1);
                ddata->iter = iter;
                ddata->tree = plug->priv->switch_user_treeview;

                g_signal_connect_data (user, "displays-changed",
                                       G_CALLBACK (user_displays_changed_cb), ddata,
                                       (GClosureNotify) g_free, 0);

                users = g_slist_delete_link (users, users);
        }

        gs_profile_end (NULL);
}

static int
compare_users (GtkTreeModel *model,
               GtkTreeIter  *a,
               GtkTreeIter  *b,
               gpointer      user_data)
{
        char *name_a;
        char *name_b;
        char *label_a;
        char *label_b;
        int   result;

        gtk_tree_model_get (model, a, USER_NAME_COLUMN, &name_a, -1);
        gtk_tree_model_get (model, b, USER_NAME_COLUMN, &name_b, -1);
        gtk_tree_model_get (model, a, REAL_NAME_COLUMN, &label_a, -1);
        gtk_tree_model_get (model, b, REAL_NAME_COLUMN, &label_b, -1);

        if (! name_a) {
                return 1;
        } else if (! name_b) {
                return -1;
        }

        if (strcmp (name_a, "__new_user") == 0) {
                return -1;
        } else if (strcmp (name_b, "__new_user") == 0) {
                return 1;
        } else if (strcmp (name_a, "__separator") == 0) {
                return -1;
        } else if (strcmp (name_b, "__separator") == 0) {
                return 1;
        }

        if (! label_a) {
                return 1;
        } else if (! label_b) {
                return -1;
        }

        result = strcmp (label_a, label_b);

        g_free (label_a);
        g_free (label_b);
        g_free (name_a);
        g_free (name_b);

        return result;
}

static gboolean
separator_func (GtkTreeModel *model,
                GtkTreeIter  *iter,
                gpointer      data)
{
        int      column = GPOINTER_TO_INT (data);
        char    *text;
        gboolean is_separator;

        gtk_tree_model_get (model, iter, column, &text, -1);

        if (text && strcmp (text, "__separator") == 0) {
                is_separator = TRUE;
        } else {
                is_separator = FALSE;
        }

        g_free (text);

        return is_separator;
}

static gboolean
filter_out_users (GtkTreeModel *model,
                  GtkTreeIter  *iter,
                  GSLockPlug   *plug)
{
        gboolean is_active;
        gboolean visible;
        char    *name;

        gtk_tree_model_get (model, iter,
                            USER_NAME_COLUMN, &name,
                            ACTIVE_COLUMN, &is_active,
                            -1);

        if (name == NULL) {
                return FALSE;
        }

        if (strcmp (name, "__new_user") == 0
            || strcmp (name, "__separator") == 0) {
                visible = TRUE;
        } else {
                /* FIXME: do we show all users or only active ones? */
                visible = TRUE;
                /*visible = is_active;*/
        }

        g_free (name);

        return visible;
}

static gboolean
row_activated_cb (GtkTreeView       *view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  GSLockPlug        *plug)
{
        GtkTreeModel *filter_model;
        GtkTreeModel *model;
        GtkTreeIter   iter;
        GtkTreePath  *child_path;
        char         *username;

        filter_model = gtk_tree_view_get_model (view);
        model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (filter_model));

        child_path = gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (filter_model), path);

        gtk_tree_model_get_iter (model, &iter, child_path);
        gtk_tree_model_get (model, &iter, USER_NAME_COLUMN, &username, -1);

        /* FIXME: do something with username? */

        switch_user_response (plug);

        g_free (username);
        gtk_tree_path_free (child_path);

        return TRUE;
}

static void
setup_treeview (GSLockPlug *plug)
{
        GtkListStore      *store;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        GtkTreeModel      *filter;

        /* if user switching is not enabled then do nothing */
        if (! plug->priv->switch_enabled) {
                return;
        }

        gs_profile_start (NULL);

        store = gtk_list_store_new (N_COLUMNS,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_BOOLEAN,
                                    GDK_TYPE_PIXBUF);
        populate_model (plug, store);

        filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (store), NULL);

        gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (filter),
                                                (GtkTreeModelFilterVisibleFunc) filter_out_users,
                                                plug,
                                                NULL);

        gtk_tree_view_set_model (GTK_TREE_VIEW (plug->priv->switch_user_treeview),
                                 filter);

        g_object_unref (store);
        g_object_unref (filter);

        renderer = gtk_cell_renderer_pixbuf_new ();
        column = gtk_tree_view_column_new_with_attributes ("Image", renderer,
                                                           "pixbuf", PIXBUF_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (plug->priv->switch_user_treeview), column);

        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
                                                           "markup", DISPLAY_LABEL_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (plug->priv->switch_user_treeview), column);

        gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (plug->priv->switch_user_treeview),
                                              separator_func,
                                              GINT_TO_POINTER (USER_NAME_COLUMN),
                                              NULL);

        gtk_tree_view_column_set_sort_column_id (column, 0);
        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
                                         0,
                                         compare_users,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                              0,
                                              GTK_SORT_ASCENDING);

        g_signal_connect (GTK_TREE_VIEW (plug->priv->switch_user_treeview),
                          "row-activated",
                          G_CALLBACK (row_activated_cb), plug);

        gs_profile_end (NULL);
}

static gboolean
setup_treeview_idle (GSLockPlug *plug)
{
        setup_treeview (plug);

        return FALSE;
}

static char *
get_user_display_name (void)
{
        const char *name;
        char       *utf8_name;

        name = g_get_real_name ();

        if (name == NULL || strcmp (name, "Unknown") == 0) {
                name = g_get_user_name ();
        }

        utf8_name = NULL;

        if (name != NULL) {
                utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
        }

        return utf8_name;
}

static char *
get_user_name (void)
{
        const char *name;
        char       *utf8_name;

        name = g_get_user_name ();

        utf8_name = NULL;
        if (name != NULL) {
                utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
        }

        return utf8_name;
}

static gboolean
check_user_file (const gchar *filename,
		 uid_t        user,
		 gssize       max_file_size,
		 gboolean     relax_group,
		 gboolean     relax_other)
{
        struct stat fileinfo;

        if (max_file_size < 0) {
                max_file_size = G_MAXSIZE;
        }

        /* Exists/Readable? */
        if (g_stat (filename, &fileinfo) < 0) {
                return FALSE;
        }

        /* Is a regular file */
        if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode))) {
                return FALSE;
        }

        /* Owned by user? */
        if (G_UNLIKELY (fileinfo.st_uid != user)) {
                return FALSE;
        }

        /* Group not writable or relax_group? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWGRP) == S_IWGRP && !relax_group)) {
                return FALSE;
        }

        /* Other not writable or relax_other? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWOTH) == S_IWOTH && !relax_other)) {
                return FALSE;
        }

        /* Size is kosher? */
        if (G_UNLIKELY (fileinfo.st_size > max_file_size)) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
set_face_image (GSLockPlug *plug)
{
        GdkPixbuf    *pixbuf;
        const char   *homedir;
        char         *path;
        int           icon_size = 96;
        gsize         user_max_file = 65536;
        uid_t         uid;

        homedir = g_get_home_dir ();
        uid = getuid ();

        path = g_build_filename (homedir, ".face", NULL);

        pixbuf = NULL;
        if (check_user_file (path, uid, user_max_file, 0, 0)) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        }

        g_free (path);

        if (pixbuf == NULL) {
                return FALSE;
        }

        gtk_image_set_from_pixbuf (GTK_IMAGE (plug->priv->auth_face_image), pixbuf);

        g_object_unref (pixbuf);

        return TRUE;
}

static void
create_page_one_buttons (GSLockPlug *plug)
{

        gs_profile_start ("page one buttons");

        plug->priv->auth_switch_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   _("S_witch User..."));
        gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (plug->priv->auth_action_area),
                                            plug->priv->auth_switch_button,
                                            TRUE);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_switch_button), FALSE);

        plug->priv->auth_logout_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                              plug->priv->auth_action_area,
                                                              _("Log _Out"));
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_logout_button), FALSE);

        plug->priv->auth_cancel_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   GTK_STOCK_CANCEL);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_cancel_button), FALSE);

        plug->priv->auth_unlock_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   _("_Unlock"));
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_unlock_button), FALSE);

        gtk_window_set_default (GTK_WINDOW (plug), plug->priv->auth_unlock_button);

        gs_profile_end ("page one buttons");
}

static void
create_page_two_buttons (GSLockPlug *plug)
{
        gs_profile_start ("page two buttons");
        plug->priv->switch_cancel_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                     plug->priv->switch_action_area,
                                                                     GTK_STOCK_CANCEL);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->switch_cancel_button), FALSE);

        plug->priv->switch_switch_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                     plug->priv->switch_action_area,
                                                                     _("S_witch User..."));

        gs_profile_end ("page two buttons");
}

/* adapted from GDM */
static char *
expand_string (const char *text)
{
        GString       *str;
        const char    *p;
        char          *username;
        int            i;
        int            n_chars;
        struct utsname name;

        str = g_string_sized_new (strlen (text));

        p = text;
        n_chars = g_utf8_strlen (text, -1);
        i = 0;

        while (i < n_chars) {
                gunichar ch;

                ch = g_utf8_get_char (p);

                /* Backslash commands */
                if (ch == '\\') {
                        p = g_utf8_next_char (p);
                        i++;
                        ch = g_utf8_get_char (p);

                        if (i >= n_chars || ch == '\0') {
                                g_warning ("Unescaped \\ at end of text\n");
                                goto bail;
                        } else if (ch == 'n') {
                                g_string_append_unichar (str, '\n');
                        } else {
                                g_string_append_unichar (str, ch);
                        }
                } else if (ch == '%') {
                        p = g_utf8_next_char (p);
                        i++;
                        ch = g_utf8_get_char (p);

                        if (i >= n_chars || ch == '\0') {
                                g_warning ("Unescaped %% at end of text\n");
                                goto bail;
                        }

                        switch (ch) {
                        case '%':
                                g_string_append (str, "%");
                                break;
                        case 'c':
                                /* clock */
                                break;
                        case 'd':
                                /* display */
                                g_string_append (str, g_getenv ("DISPLAY"));
                                break;
                        case 'h':
                                /* hostname */
                                g_string_append (str, g_get_host_name ());
                                break;
                        case 'm':
                                /* machine name */
                                uname (&name);
                                g_string_append (str, name.machine);
                                break;
                        case 'n':
                                /* nodename */
                                uname (&name);
                                g_string_append (str, name.nodename);
                                break;
                        case 'r':
                                /* release */
                                uname (&name);
                                g_string_append (str, name.release);
                                break;
                        case 'R':
                                /* Real name */
                                username = get_user_display_name ();
                                g_string_append (str, username);
                                g_free (username);
                                break;
                        case 's':
                                /* system name */
                                uname (&name);
                                g_string_append (str, name.sysname);
                                break;
                        case 'U':
                                /* Username */
                                username = get_user_name ();
                                g_string_append (str, username);
                                g_free (username);
                                break;
                        default:
                                if (ch < 127) {
                                        g_warning ("unknown escape code %%%c in text\n", (char)ch);
                                } else {
                                        g_warning ("unknown escape code %%(U%x) in text\n", (int)ch);
                                }
                        }
                } else {
                        g_string_append_unichar (str, ch);
                }
                p = g_utf8_next_char (p);
                i++;
        }

 bail:

        return g_string_free (str, FALSE);
}

static void
expand_string_for_label (GtkWidget *label)
{
        const char *template;
        char       *str;

        template = gtk_label_get_label (GTK_LABEL (label));
        str = expand_string (template);
        gtk_label_set_label (GTK_LABEL (label), str);
        g_free (str);
}

static void
create_page_one (GSLockPlug *plug)
{
        GtkWidget            *password_label;
        GtkWidget            *align;
        GtkWidget            *vbox;
        GtkWidget            *vbox2;
        GtkWidget            *hbox;
        char                 *str;

        gs_profile_start ("page one");

        align = gtk_alignment_new (0.5, 0.5, 1, 1);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), align, NULL);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        plug->priv->auth_face_image = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (vbox), plug->priv->auth_face_image, TRUE, TRUE, 0);
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_face_image), 0.5, 1.0);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

        str = g_strdup ("<span size=\"x-large\">%R</span>");
        plug->priv->auth_realname_label = gtk_label_new (str);
        g_free (str);
        expand_string_for_label (plug->priv->auth_realname_label);
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_realname_label), 0.5, 0.5);
        gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_realname_label), TRUE);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_realname_label, FALSE, FALSE, 0);

        str = g_strdup_printf ("<span size=\"small\">%s</span>", _("%U on %h"));
        plug->priv->auth_username_label = gtk_label_new (str);
        g_free (str);
        expand_string_for_label (plug->priv->auth_username_label);
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_username_label), 0.5, 0.5);
        gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_username_label), TRUE);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_username_label, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox2, TRUE, TRUE, 0);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

        password_label = gtk_label_new_with_mnemonic (_("_Password:"));
        gtk_misc_set_alignment (GTK_MISC (password_label), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), password_label, FALSE, FALSE, 0);

        plug->priv->auth_prompt_entry = gtk_entry_new ();
        gtk_box_pack_start (GTK_BOX (hbox), plug->priv->auth_prompt_entry, TRUE, TRUE, 0);

        gtk_label_set_mnemonic_widget (GTK_LABEL (password_label),
                                       plug->priv->auth_prompt_entry);

        plug->priv->auth_capslock_label = gtk_label_new ("");
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_capslock_label), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_capslock_label, FALSE, FALSE, 0);

        /* Status text */

        plug->priv->auth_message_label = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX (vbox), plug->priv->auth_message_label,
                            FALSE, FALSE, 0);
        /* Buttons */
        plug->priv->auth_action_area = gtk_hbutton_box_new ();

        gtk_button_box_set_layout (GTK_BUTTON_BOX (plug->priv->auth_action_area),
                                   GTK_BUTTONBOX_END);

        gtk_box_pack_end (GTK_BOX (vbox), plug->priv->auth_action_area,
                          FALSE, TRUE, 0);
        gtk_widget_show (plug->priv->auth_action_area);

        create_page_one_buttons (plug);

        gs_profile_end ("page one");
}

static void
constrain_list_size (GtkWidget      *widget,
                     GtkRequisition *requisition,
                     GSLockPlug     *plug)
{
        GtkRequisition req;
        int            max_height;
        int            page;

        /* don't do anything if we are on the auth page */
        page = gtk_notebook_get_current_page (GTK_NOTEBOOK (plug->priv->notebook));
        if (page == AUTH_PAGE) {
                return;
        }

        /* constrain height to be the tree height up to a max */
        max_height = (gdk_screen_get_height (gtk_widget_get_screen (widget))) / 4;
        gtk_widget_size_request (plug->priv->switch_user_treeview, &req);

        requisition->height = MIN (req.height, max_height);
}

static void
setup_list_size_constraint (GtkWidget  *widget,
                            GSLockPlug *plug)
{
        g_signal_connect (widget, "size-request",
                          G_CALLBACK (constrain_list_size), plug);
}

static void
create_page_two (GSLockPlug *plug)
{
        GtkWidget            *header_label;
        GtkWidget            *userlist_scroller;
        GtkWidget            *vbox;

        gs_profile_start ("page two");

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), vbox, NULL);

        header_label = gtk_label_new_with_mnemonic (_("S_witch to user:"));
        gtk_misc_set_alignment (GTK_MISC (header_label), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), header_label, FALSE, FALSE, 0);

        userlist_scroller = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (userlist_scroller),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (userlist_scroller),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (vbox), userlist_scroller, TRUE, TRUE, 0);

        setup_list_size_constraint (userlist_scroller, plug);

        plug->priv->switch_user_treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (plug->priv->switch_user_treeview), FALSE);
        gtk_container_add (GTK_CONTAINER (userlist_scroller), plug->priv->switch_user_treeview);

        gtk_label_set_mnemonic_widget (GTK_LABEL (header_label), plug->priv->switch_user_treeview);

        /* Buttons */
        plug->priv->switch_action_area = gtk_hbutton_box_new ();

        gtk_button_box_set_layout (GTK_BUTTON_BOX (plug->priv->switch_action_area),
                                   GTK_BUTTONBOX_END);

        gtk_box_pack_end (GTK_BOX (vbox), plug->priv->switch_action_area,
                          FALSE, TRUE, 0);
        gtk_widget_show (plug->priv->switch_action_area);

        create_page_two_buttons (plug);

        gs_profile_end ("page two");
}

static void
unlock_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_OK);
}

static void
cancel_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);
}

static void
switch_user_button_clicked (GtkButton  *button,
                            GSLockPlug *plug)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_OK);
}

static char *
get_dialog_theme_name (GSLockPlug *plug)
{
        char        *name;
        GConfClient *client;

        client = gconf_client_get_default ();
        name = gconf_client_get_string (client, KEY_LOCK_DIALOG_THEME, NULL);
        g_object_unref (client);

        return name;
}

static gboolean
load_theme (GSLockPlug *plug)
{
        char       *theme;
        char       *filename;
        char       *glade;
        char       *rc;
        GladeXML   *xml;
        GtkWidget  *lock_dialog;

        theme = get_dialog_theme_name (plug);
        if (theme == NULL) {
                return FALSE;
        }

        filename = g_strdup_printf ("lock-dialog-%s.glade", theme);
        glade = g_build_filename (GLADEDIR, filename, NULL);
        g_free (filename);
        if (! g_file_test (glade, G_FILE_TEST_IS_REGULAR)) {
                g_free (glade);
                g_free (theme);
                return FALSE;
        }

        filename = g_strdup_printf ("lock-dialog-%s.gtkrc", theme);
        g_free (theme);

        rc = g_build_filename (GLADEDIR, filename, NULL);
        g_free (filename);
        if (g_file_test (rc, G_FILE_TEST_IS_REGULAR)) {
                gtk_rc_parse (rc);
        }
        g_free (rc);

        xml = glade_xml_new (glade, "lock-dialog", NULL);

        if (xml == NULL) {
                g_warning ("Failed to load '%s'\n", glade);
                g_free (glade);
                return FALSE;
        }
        g_free (glade);

        lock_dialog = glade_xml_get_widget (xml, "lock-dialog");
        gtk_container_add (GTK_CONTAINER (plug), lock_dialog);
        gtk_widget_show_all (lock_dialog);

        plug->priv->vbox = NULL;
        plug->priv->notebook = glade_xml_get_widget (xml, "notebook");

        plug->priv->auth_face_image = glade_xml_get_widget (xml, "auth-face-image");
        plug->priv->auth_action_area = glade_xml_get_widget (xml, "auth-action-area");
        plug->priv->auth_realname_label = glade_xml_get_widget (xml, "auth-realname-label");
        plug->priv->auth_username_label = glade_xml_get_widget (xml, "auth-username-label");
        plug->priv->auth_prompt_label = glade_xml_get_widget (xml, "auth-prompt-label");
        plug->priv->auth_prompt_entry = glade_xml_get_widget (xml, "auth-prompt-entry");
        plug->priv->auth_prompt_box = glade_xml_get_widget (xml, "auth-prompt-box");
        plug->priv->auth_capslock_label = glade_xml_get_widget (xml, "auth-capslock-label");
        plug->priv->auth_message_label = glade_xml_get_widget (xml, "auth-status-label");
        plug->priv->auth_unlock_button = glade_xml_get_widget (xml, "auth-unlock-button");
        plug->priv->auth_cancel_button = glade_xml_get_widget (xml, "auth-cancel-button");
        plug->priv->auth_logout_button = glade_xml_get_widget (xml, "auth-logout-button");
        plug->priv->auth_switch_button = glade_xml_get_widget (xml, "auth-switch-button");

        plug->priv->switch_action_area = glade_xml_get_widget (xml, "switch-action-area");
        plug->priv->switch_user_treeview = glade_xml_get_widget (xml, "switch-user-treeview");
        plug->priv->switch_cancel_button = glade_xml_get_widget (xml, "switch-cancel-button");
        plug->priv->switch_switch_button = glade_xml_get_widget (xml, "switch-switch-button");

        return TRUE;
}

static int
delete_handler (GSLockPlug  *plug,
                GdkEventAny *event,
                gpointer     data)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);

        return TRUE; /* Do not destroy */
}

#define INVISIBLE_CHAR_DEFAULT       '*'
#define INVISIBLE_CHAR_BLACK_CIRCLE  0x25cf
#define INVISIBLE_CHAR_WHITE_BULLET  0x25e6
#define INVISIBLE_CHAR_BULLET        0x2022
#define INVISIBLE_CHAR_NONE          0

static void
gs_lock_plug_init (GSLockPlug *plug)
{
        gunichar              invisible_char;

        gs_profile_start (NULL);

        plug->priv = GS_LOCK_PLUG_GET_PRIVATE (plug);

        plug->priv->fusa_manager = NULL;

        if (! load_theme (plug)) {
                gs_debug ("Unable to load theme!");

                plug->priv->vbox = gtk_vbox_new (FALSE, 0);

                gtk_container_add (GTK_CONTAINER (plug), plug->priv->vbox);

                /* Notebook */

                plug->priv->notebook = gtk_notebook_new ();
                gtk_notebook_set_show_tabs (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
                gtk_notebook_set_show_border (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
                gtk_box_pack_start (GTK_BOX (plug->priv->vbox), plug->priv->notebook, TRUE, TRUE, 0);

                /* Page 1 */

                create_page_one (plug);

                /* Page 2 */

                create_page_two (plug);

                gtk_widget_show_all (plug->priv->vbox);
        }

        gtk_widget_grab_default (plug->priv->auth_unlock_button);

        if (plug->priv->auth_username_label != NULL) {
                expand_string_for_label (plug->priv->auth_username_label);
        }

        if (plug->priv->auth_realname_label != NULL) {
                expand_string_for_label (plug->priv->auth_realname_label);
        }

        if (! plug->priv->logout_enabled
            || ! plug->priv->logout_command) {
                gtk_widget_hide (plug->priv->auth_logout_button);
        }

        if (! plug->priv->switch_enabled) {
                gtk_widget_hide (plug->priv->auth_switch_button);
        }

        plug->priv->timeout = DIALOG_TIMEOUT_MSEC;

        g_signal_connect (plug, "key_press_event",
                          G_CALLBACK (entry_key_press), plug);

        /* button press handler used to inhibit popup menu */
        g_signal_connect (plug->priv->auth_prompt_entry, "button_press_event",
                          G_CALLBACK (entry_button_press), NULL);
        gtk_entry_set_activates_default (GTK_ENTRY (plug->priv->auth_prompt_entry), TRUE);
        gtk_entry_set_visibility (GTK_ENTRY (plug->priv->auth_prompt_entry), FALSE);

        /* Only change the invisible character if it '*' otherwise assume it is OK */
        if ('*' == gtk_entry_get_invisible_char (GTK_ENTRY (plug->priv->auth_prompt_entry))) {
                invisible_char = INVISIBLE_CHAR_BLACK_CIRCLE;
                gtk_entry_set_invisible_char (GTK_ENTRY (plug->priv->auth_prompt_entry), invisible_char);
        }

        g_signal_connect (plug->priv->auth_unlock_button, "clicked",
                          G_CALLBACK (unlock_button_clicked), plug);

        g_signal_connect (plug->priv->auth_cancel_button, "clicked",
                          G_CALLBACK (cancel_button_clicked), plug);

        if (plug->priv->auth_switch_button != NULL) {
                g_signal_connect_swapped (plug->priv->auth_switch_button, "clicked",
                                          G_CALLBACK (switch_page), plug);
        }

        if (plug->priv->auth_logout_button != NULL) {
                g_signal_connect (plug->priv->auth_logout_button, "clicked",
                                  G_CALLBACK (logout_button_clicked), plug);
        }

        if (plug->priv->switch_cancel_button != NULL) {
                g_signal_connect (plug->priv->switch_cancel_button, "clicked",
                                  G_CALLBACK (cancel_button_clicked), plug);
        }
        if (plug->priv->switch_switch_button != NULL) {
                g_signal_connect (plug->priv->switch_switch_button, "clicked",
                                  G_CALLBACK (switch_user_button_clicked), plug);
        }

        if (plug->priv->auth_face_image) {
                set_face_image (plug);
        }

        g_signal_connect (plug, "delete_event", G_CALLBACK (delete_handler), NULL);

        g_idle_add ((GSourceFunc)setup_treeview_idle, plug);

        gs_profile_end (NULL);
}

static void
gs_lock_plug_finalize (GObject *object)
{
        GSLockPlug *plug;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LOCK_PLUG (object));

        plug = GS_LOCK_PLUG (object);

        g_return_if_fail (plug->priv != NULL);

        g_free (plug->priv->logout_command);

        if (plug->priv->fusa_manager) {
                g_object_unref (plug->priv->fusa_manager);
        }

        remove_response_idle (plug);
        remove_monitor_idle (plug);

        G_OBJECT_CLASS (gs_lock_plug_parent_class)->finalize (object);
}

GtkWidget *
gs_lock_plug_new (void)
{
        GtkWidget *result;

        result = g_object_new (GS_TYPE_LOCK_PLUG, NULL);

        gtk_window_set_focus_on_map (GTK_WINDOW (result), TRUE);

        return result;
}
