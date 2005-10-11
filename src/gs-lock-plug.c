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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#include <gtk/gtk.h>

/* for fast user switching */
#include <libgnomevfs/gnome-vfs-init.h>

#include "gs-lock-plug.h"

#include "passwd.h"

#include "fusa-manager.h"

/* Profiling stuff adapted from gtkfilechooserdefault */

#undef PROFILE_LOCK_DIALOG
#ifdef PROFILE_LOCK_DIALOG

#define PROFILE_INDENT 4
static int profile_indent;

static void
profile_add_indent (int indent)
{
        profile_indent += indent;
        if (profile_indent < 0)
                g_error ("You screwed up your indentation");
}

static void
_gs_lock_plug_profile_log (const char *func,
                           int         indent,
                           const char *msg1,
                           const char *msg2)
{
        char *str;

        if (indent < 0)
                profile_add_indent (indent);

        if (profile_indent == 0)
                str = g_strdup_printf ("MARK: %s: %s %s %s", G_STRLOC, func, msg1 ? msg1 : "", msg2 ? msg2 : "");
        else
                str = g_strdup_printf ("MARK: %s: %*c %s %s %s", G_STRLOC, profile_indent - 1, ' ', func, msg1 ? msg1 : "", msg2 ? msg2 : "");

        access (str, F_OK);

        g_free (str);

        if (indent > 0)
                profile_add_indent (indent);
}

#define profile_start(x, y) _gs_lock_plug_profile_log (G_STRFUNC, PROFILE_INDENT, x, y)
#define profile_end(x, y)   _gs_lock_plug_profile_log (G_STRFUNC, -PROFILE_INDENT, x, y)
#define profile_msg(x, y)   _gs_lock_plug_profile_log (NULL, 0, x, y)
#else
#define profile_start(x, y)
#define profile_end(x, y)
#define profile_msg(x, y)
#endif


enum { 
        AUTH_PAGE = 0,
        SWITCH_PAGE
};

#define FACE_ICON_SIZE 48
#define DIALOG_TIMEOUT_MSEC 60000

static void gs_lock_plug_class_init (GSLockPlugClass *klass);
static void gs_lock_plug_init       (GSLockPlug      *plug);
static void gs_lock_plug_finalize   (GObject         *object);

static gboolean password_check_idle_cb (GSLockPlug *plug);

#define GS_LOCK_PLUG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LOCK_PLUG, GSLockPlugPrivate))

struct GSLockPlugPrivate
{
        GtkWidget   *notebook;
        GtkWidget   *username_label;
        GtkWidget   *password_entry;
        GtkWidget   *capslock_label;
        GtkWidget   *status_label;
        GtkWidget   *user_treeview;

        GtkWidget   *ok_button;
        GtkWidget   *cancel_button;
        GtkWidget   *logout_button;
        GtkWidget   *switch_button;

        FusaManager *fusa_manager;

        gboolean     caps_lock_on;
        gboolean     switch_enabled;
        gboolean     logout_enabled;

        guint        timeout;

        guint        idle_id;
        guint        password_check_idle_id;
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
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOGOUT_ENABLED,
        PROP_SWITCH_ENABLED
};

static GObjectClass *parent_class = NULL;
static guint         lock_plug_signals [LAST_SIGNAL];

G_DEFINE_TYPE (GSLockPlug, gs_lock_plug, GTK_TYPE_PLUG);

static void
gs_lock_plug_style_set (GtkWidget *widget,
                        GtkStyle  *previous_style)
{
        GSLockPlug *plug;

        if (GTK_WIDGET_CLASS (parent_class)->style_set)
                GTK_WIDGET_CLASS (parent_class)->style_set (widget, previous_style);

        plug = GS_LOCK_PLUG (widget);

        gtk_container_set_border_width (GTK_CONTAINER (plug->vbox), 24);
        gtk_box_set_spacing (GTK_BOX (plug->vbox), 12);

        gtk_container_set_border_width (GTK_CONTAINER (plug->action_area), 0);
        gtk_box_set_spacing (GTK_BOX (plug->action_area), 5);
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

        if (gtk_widget_has_screen (plug->priv->user_treeview)) {
                screen = gtk_widget_get_screen (plug->priv->user_treeview);
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
        DISPLAY_NAME_COLUMN,
        NAME_COLUMN,
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

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (plug->priv->user_treeview));
        gtk_tree_selection_get_selected (selection,
                                         &model,
                                         &iter);
        gtk_tree_model_get (model, &iter, NAME_COLUMN, &name, -1);
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
        gtk_label_set_text (GTK_LABEL (plug->priv->status_label), text);
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

        if (plug->priv->idle_id > 0) {
                g_source_remove (plug->priv->idle_id);
                plug->priv->idle_id = 0;
        }

        if (plug->priv->password_check_idle_id > 0) {
                g_source_remove (plug->priv->password_check_idle_id);
                plug->priv->password_check_idle_id = 0;
        }

        if (plug->priv->response_idle_id > 0) {
                g_source_remove (plug->priv->response_idle_id);
                plug->priv->response_idle_id = 0;
        }

        if (response_id == GS_LOCK_PLUG_RESPONSE_CANCEL) {
                gtk_entry_set_text (GTK_ENTRY (plug->priv->password_entry), "");
        }

        if (response_id == GS_LOCK_PLUG_RESPONSE_OK) {
                gint current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (plug->priv->notebook));

                if (current_page == 0) {
                        gtk_widget_set_sensitive (plug->priv->password_entry, FALSE);
                        set_status_text (plug, _("Checking password..."));

                        plug->priv->password_check_idle_id = g_idle_add ((GSourceFunc)password_check_idle_cb,
                                                                         plug);
                        return;
                } else {
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
response_idle_cb (GSLockPlug *plug)
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
        char    *message;

        g_get_current_time (&now);

        elapsed = (now.tv_sec - plug->priv->start_time.tv_sec) * 1000
                + (now.tv_usec - plug->priv->start_time.tv_usec) / 1000;
        remaining = plug->priv->timeout - elapsed;

        if ((remaining <= 0) || (remaining > plug->priv->timeout)) {
                message = g_strdup (_("Time has expired."));
                gtk_widget_set_sensitive (plug->priv->password_entry, FALSE);
                set_status_text (plug, message);
                g_free (message);

                if (plug->priv->response_idle_id == 0)
                        plug->priv->response_idle_id = g_timeout_add (1000,
                                                                      (GSourceFunc)response_idle_cb,
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

        if (is_on)
                gtk_label_set_text (GTK_LABEL (plug->priv->capslock_label),
                                    _("You have the Caps Lock key on."));
        else
                gtk_label_set_text (GTK_LABEL (plug->priv->capslock_label),
                                    "");
}

/* adapted from GDM2 */
static gboolean
is_capslock_on (void)
{
        unsigned int states;
        Display     *dsp;

        dsp = GDK_DISPLAY ();

        if (XkbGetIndicatorState (dsp, XkbUseCoreKbd, &states) != Success)
                return FALSE;

        return (states & ShiftMask) != 0;
}

static void
restart_monitor_progress (GSLockPlug *plug)
{
        if (plug->priv->idle_id > 0) {
                g_source_remove (plug->priv->idle_id);
                plug->priv->idle_id = 0;
        }

        g_get_current_time (&plug->priv->start_time);
        plug->priv->idle_id = g_timeout_add (50,
                                             (GSourceFunc)monitor_progress,
                                             plug);
}

static void
gs_lock_plug_show (GtkWidget *widget)
{
        profile_start ("start", NULL);

        profile_start ("start", "parent");
        if (GTK_WIDGET_CLASS (parent_class)->show)
                GTK_WIDGET_CLASS (parent_class)->show (widget);
        profile_end ("end", "parent");

        capslock_update (GS_LOCK_PLUG (widget), is_capslock_on ());

        restart_monitor_progress (GS_LOCK_PLUG (widget));

        profile_end ("end", NULL);
}

static void
gs_lock_plug_hide (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (parent_class)->hide)
                GTK_WIDGET_CLASS (parent_class)->hide (widget);
}


static void
gs_lock_plug_size_request (GtkWidget      *widget,
                           GtkRequisition *requisition)
{
        if (GTK_WIDGET_CLASS (parent_class)->size_request)
                GTK_WIDGET_CLASS (parent_class)->size_request (widget, requisition);

        /* Force width to be at least 1.2 times the height.
         * This ensures enough space for the text entry and
         * makes for a more pleasing aspect ratio */
        if (requisition->width < requisition->height) {
                requisition->width = requisition->height * 1.2;
        }
}

static void
gs_lock_plug_set_logout_enabled (GSLockPlug *plug,
                                 gboolean    logout_enabled)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        if (plug->priv->logout_enabled == logout_enabled)
                return;

        plug->priv->logout_enabled = logout_enabled;
        g_object_notify (G_OBJECT (plug), "logout-enabled");

        if (logout_enabled)
                gtk_widget_show (plug->priv->logout_button);
        else
                gtk_widget_hide (plug->priv->logout_button);
}

static void
gs_lock_plug_set_switch_enabled (GSLockPlug *plug,
                                 gboolean    switch_enabled)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        if (plug->priv->switch_enabled == switch_enabled)
                return;

        plug->priv->switch_enabled = switch_enabled;
        g_object_notify (G_OBJECT (plug), "switch-enabled");

        if (switch_enabled)
                gtk_widget_show (plug->priv->switch_button);
        else
                gtk_widget_hide (plug->priv->switch_button);
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
        case PROP_SWITCH_ENABLED:
                g_value_set_boolean (value, self->priv->switch_enabled);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_lock_plug_class_init (GSLockPlugClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize     = gs_lock_plug_finalize;
        object_class->get_property = gs_lock_plug_get_property;
        object_class->set_property = gs_lock_plug_set_property;

        widget_class->style_set    = gs_lock_plug_style_set;
        widget_class->show         = gs_lock_plug_show;
        widget_class->hide         = gs_lock_plug_hide;
        widget_class->size_request = gs_lock_plug_size_request;

        g_type_class_add_private (klass, sizeof (GSLockPlugPrivate));


        lock_plug_signals [RESPONSE] = g_signal_new ("response",
                                                     G_OBJECT_CLASS_TYPE (klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GSLockPlugClass, response),
                                                     NULL, NULL,
                                                     g_cclosure_marshal_VOID__INT,
                                                     G_TYPE_NONE, 1,
                                                     G_TYPE_INT);

        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_ENABLED,
                                         g_param_spec_boolean ("logout-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SWITCH_ENABLED,
                                         g_param_spec_boolean ("switch-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
}

static gboolean
password_check_idle_cb (GSLockPlug *plug)
{
        const char *typed_password;
        char       *null_password;
        char       *local_password;

        plug->priv->password_check_idle_id = 0;

        typed_password = gtk_entry_get_text (GTK_ENTRY (plug->priv->password_entry));
        local_password = g_locale_from_utf8 (typed_password, strlen (typed_password), NULL, NULL, NULL);

        null_password = g_strnfill (strlen (typed_password) + 1, '\b');
        gtk_entry_set_text (GTK_ENTRY (plug->priv->password_entry), null_password);
        gtk_entry_set_text (GTK_ENTRY (plug->priv->password_entry), "");
        g_free (null_password);

        if (validate_password (local_password, FALSE)) {
                g_signal_emit (plug,
                               lock_plug_signals [RESPONSE],
                               0,
                               GS_LOCK_PLUG_RESPONSE_OK);
        } else {
                if (plug->priv->response_idle_id == 0)
                        plug->priv->response_idle_id = g_timeout_add (2000,
                                                                      (GSourceFunc)response_idle_cb,
                                                                      plug);

                set_status_text (plug, _("That password was incorrect."));

                printf ("NOTICE=AUTH FAILED\n");
                fflush (stdout);
        }

        memset (local_password, '\b', strlen (local_password));
        g_free (local_password);

        return FALSE;
}

static GtkWidget *
get_ok_button_for_page (gint page)
{
        GtkWidget *align;
        GtkWidget *hbox;
        GtkWidget *widget;
        const char *label = NULL;

        align = gtk_alignment_new (0.5, 0.5, 0, 0);
        hbox = gtk_hbox_new (FALSE, 2);
        gtk_container_add (GTK_CONTAINER (align), hbox);

        switch (page) {
        case (AUTH_PAGE):
                label = _("_Unlock");
                break;
        case (SWITCH_PAGE):
                label = _("_Switch User...");
                break;
        default:
                g_assert ("Invalid notebook page");
                break;
        }

        widget = gtk_label_new_with_mnemonic (label);
        gtk_box_pack_end (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        return align;
}

static GtkWidget *
get_switch_button_for_page (gint page)
{
        GtkWidget *align;
        GtkWidget *hbox;
        GtkWidget *widget;
        const char *label = NULL;

        align = gtk_alignment_new (0.5, 0.5, 0, 0);
        hbox = gtk_hbox_new (FALSE, 2);
        gtk_container_add (GTK_CONTAINER (align), hbox);

        switch (page) {
        case (AUTH_PAGE):
                label = _("_Switch User...");
                break;
        case (SWITCH_PAGE):
                label = _("_Unlock");
                break;
        default:
                g_assert ("Invalid notebook page");
                break;
        }

        widget = gtk_label_new_with_mnemonic (label);
        gtk_box_pack_end (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        return align;
}

static void
switch_page (GSLockPlug *plug,
             GtkButton  *button)
{
        GtkWidget *ok_widget;
        GtkWidget *other_widget;
        gint       current_page;
        gint       next_page;

        g_return_if_fail (plug != NULL);

        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (plug->priv->notebook));
        next_page = (current_page == AUTH_PAGE) ? SWITCH_PAGE : AUTH_PAGE;

        other_widget = get_switch_button_for_page (next_page);
        ok_widget = get_ok_button_for_page (next_page);

        gtk_widget_destroy (GTK_BIN (plug->priv->switch_button)->child);
        gtk_widget_show_all (other_widget);
        gtk_container_add (GTK_CONTAINER (plug->priv->switch_button), other_widget);

        gtk_widget_destroy (GTK_BIN (plug->priv->ok_button)->child);
        gtk_widget_show_all (ok_widget);
        gtk_container_add (GTK_CONTAINER (plug->priv->ok_button), ok_widget);

        /* don't show the switch button on the switch page */
        if (next_page == SWITCH_PAGE) {
                gtk_widget_hide (plug->priv->switch_button);
        }

        gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), next_page);

        /* this counts as activity so restart the timer */
        restart_monitor_progress (plug);
}

static void
logout_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        char   *argv [4];
        GError *error = NULL;

        argv [0] = BINDIR "/gnome-session-save";
        argv [1] = "--kill";
        argv [2] = "--silent";
        argv [3] = NULL;

        g_spawn_async (g_get_home_dir (),
                       argv,
                       NULL,
                       0,
                       NULL,
                       NULL,
                       NULL,
                       &error);
        if (error) {
                g_warning ("Could not run logout command: %s", error->message);
                g_error_free (error);
        }
}

/* button press handler used to inhibit popup menu */
static gint
entry_button_press (GtkWidget      *widget,
                    GdkEventButton *event)
{
        if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
                return TRUE;

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

        if (capslock_on != plug->priv->caps_lock_on)
                capslock_update (plug, capslock_on);

        return FALSE;
}

static ResponseData*
get_response_data (GtkWidget *widget,
		   gboolean   create)
{
        ResponseData *ad = g_object_get_data (G_OBJECT (widget),
                                              "gs-lock-plug-response-data");

        if (ad == NULL && create) {
                ad = g_new (ResponseData, 1);
      
                g_object_set_data_full (G_OBJECT (widget),
                                        "gs-lock-plug-response-data",
                                        ad,
                                        g_free);
        }

        return ad;
}

/* adapted from gtkdialog */
static void
action_widget_activated (GtkWidget  *widget,
                         GSLockPlug *plug)
{
        ResponseData *ad;
        gint response_id;
  
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        response_id = GS_LOCK_PLUG_RESPONSE_NONE;
  
        ad = get_response_data (widget, TRUE);

        g_assert (ad != NULL);
  
        response_id = ad->response_id;

        gs_lock_plug_response (plug, response_id);
}

/* adapted from gtk_dialog_add_action_widget */
static void
gs_lock_plug_add_action_widget (GSLockPlug *plug,
                                GtkWidget  *child,
                                gint        response_id)
{
        ResponseData *ad;
        gint signal_id = 0;
  
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));
        g_return_if_fail (GTK_IS_WIDGET (child));

        ad = get_response_data (child, TRUE);

        ad->response_id = response_id;

        if (GTK_IS_BUTTON (child))
                signal_id = g_signal_lookup ("clicked", GTK_TYPE_BUTTON);
        else
                signal_id = GTK_WIDGET_GET_CLASS (child)->activate_signal != 0;

        if (signal_id) {
                GClosure *closure;

                closure = g_cclosure_new_object (G_CALLBACK (action_widget_activated),
                                                 G_OBJECT (plug));
                g_signal_connect_closure_by_id (child,
                                                signal_id,
                                                0,
                                                closure,
                                                FALSE);
        } else
                g_warning ("Only 'activatable' widgets can be packed into the action area of a GSLockPlug");

        gtk_box_pack_end (GTK_BOX (plug->action_area),
                          child,
                          FALSE, TRUE, 0);
  
        if (response_id == GTK_RESPONSE_HELP)
                gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (plug->action_area), child, TRUE);
}

/* adapted from gtk_dialog_add_button */
static GtkWidget *
gs_lock_plug_add_button (GSLockPlug  *plug,
                         const gchar *button_text,
                         gint         response_id)
{
        GtkWidget *button;

        g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), NULL);
        g_return_val_if_fail (button_text != NULL, NULL);

        button = gtk_button_new_from_stock (button_text);

        GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);

        gtk_widget_show (button);

        gs_lock_plug_add_action_widget (plug,
                                        button,
                                        response_id);

        return button;
}

static void
gs_lock_plug_set_default_response (GSLockPlug *plug,
                                   gint        response_id)
{
        GList *children;
        GList *tmp_list;

        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        children = gtk_container_get_children (GTK_CONTAINER (plug->action_area));

        tmp_list = children;
        while (tmp_list != NULL) {
                GtkWidget *widget = tmp_list->data;
                ResponseData *rd = g_object_get_data (G_OBJECT (widget),
                                                      "gs-lock-plug-response-data");

                if (rd && rd->response_id == response_id)
                        gtk_widget_grab_default (widget);
	    
                tmp_list = g_list_next (tmp_list);
        }

        g_list_free (children);
}

typedef struct
{
        GtkTreeIter iter;
        GtkWidget  *tree;
} DisplayChangedData;

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

        name = fusa_user_get_user_name (user);
        n_displays = fusa_user_get_n_displays (user);
        is_active = n_displays > 0;
        pixbuf = fusa_user_render_icon (user, data->tree, icon_size, is_active);

        filter_model = gtk_tree_view_get_model (GTK_TREE_VIEW (data->tree));
        model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (filter_model));

        gtk_list_store_set (GTK_LIST_STORE (model), &data->iter,
                            NAME_COLUMN, name,
                            DISPLAY_NAME_COLUMN, fusa_user_get_display_name (user),
                            ACTIVE_COLUMN, is_active,
                            PIXBUF_COLUMN, pixbuf,
                            -1);
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

        profile_start ("start", NULL);

        if (gtk_widget_has_screen (plug->priv->user_treeview))
                theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (plug->priv->user_treeview));
        else
                theme = gtk_icon_theme_get_default ();
        
        pixbuf = gtk_icon_theme_load_icon (theme, "gdm", icon_size, 0, NULL);

#if 0
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            DISPLAY_NAME_COLUMN, _("Log in as a new user"),
                            NAME_COLUMN, "__new_user",
                            PIXBUF_COLUMN, pixbuf,
                            -1);
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            DISPLAY_NAME_COLUMN, NULL,
                            NAME_COLUMN, "__separator",
                            -1);
#endif

        profile_start ("start", "FUSA list users");
        if (! plug->priv->fusa_manager) {
        /* for fast user switching */
                profile_start ("start", "g_thread_init");
                g_thread_init (NULL);
                profile_end ("end", "g_thread_init");
                profile_start ("start", "gnome_vfs_init");
                gnome_vfs_init ();
                profile_end ("end", "gnome_vfs_init");

                profile_start ("start", "fusa_manager_ref_default");
                plug->priv->fusa_manager = fusa_manager_ref_default ();
                profile_end ("end", "fusa_manager_ref_default");
        }

        users = fusa_manager_list_users (plug->priv->fusa_manager);
        profile_end ("end", "FUSA list users");

        while (users) {
                FusaUser           *user;
                gboolean            is_active;
                guint               n_displays;
                DisplayChangedData *ddata;

                user = users->data;

                /* skip the current user */
                if (fusa_user_get_uid (user) == getuid ()) {
                        users = g_slist_delete_link (users, users);
                        continue;
                }

                n_displays = fusa_user_get_n_displays (user);
                is_active = n_displays > 0;

                /* this requires the following to scale well:
                   http://bugzilla.gnome.org/show_bug.cgi?id=310418 */
                pixbuf = fusa_user_render_icon (user, plug->priv->user_treeview, icon_size, is_active);

                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    NAME_COLUMN, fusa_user_get_user_name (user),
                                    DISPLAY_NAME_COLUMN, fusa_user_get_display_name (user),
                                    ACTIVE_COLUMN, is_active,
                                    PIXBUF_COLUMN, pixbuf,
                                    -1);

                ddata = g_new0 (DisplayChangedData, 1);
                ddata->iter = iter;
                ddata->tree = plug->priv->user_treeview;

                g_signal_connect_data (user, "displays-changed",
                                       G_CALLBACK (user_displays_changed_cb), ddata,
                                       (GClosureNotify) g_free, 0);

                users = g_slist_delete_link (users, users);
        }

        profile_end ("end", NULL);
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

        gtk_tree_model_get (model, a, NAME_COLUMN, &name_a, -1);
        gtk_tree_model_get (model, b, NAME_COLUMN, &name_b, -1);
        gtk_tree_model_get (model, a, DISPLAY_NAME_COLUMN, &label_a, -1);
        gtk_tree_model_get (model, b, DISPLAY_NAME_COLUMN, &label_b, -1);

        if (! name_a)
                return 1;
        else if (! name_b)
                return -1;

        if (strcmp (name_a, "__new_user") == 0)
                return -1;
        else if (strcmp (name_b, "__new_user") == 0)
                return 1;
        else if (strcmp (name_a, "__separator") == 0)
                return -1;
        else if (strcmp (name_b, "__separator") == 0)
                return 1;

        if (! label_a)
                return 1;
        else if (! label_b)
                return -1;

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
        int   column = GPOINTER_TO_INT (data);
        char *text;
        
        gtk_tree_model_get (model, iter, column, &text, -1);
        
        if (text && strcmp (text, "__separator") == 0)
                return TRUE;
        
        g_free (text);

        return FALSE;
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
                            NAME_COLUMN, &name,
                            ACTIVE_COLUMN, &is_active,
                            -1);
        if (! name)
                return FALSE;

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

static void
setup_treeview (GSLockPlug *plug)
{
        GtkListStore      *store;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        GtkTreeModel      *filter;

        /* if user switching is not enabled then do nothing */
        if (! plug->priv->switch_enabled)
                return;

        profile_start ("start", NULL);

        store = gtk_list_store_new (N_COLUMNS,
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

        gtk_tree_view_set_model (GTK_TREE_VIEW (plug->priv->user_treeview),
                                 filter);

        g_object_unref (store);
        g_object_unref (filter);

        renderer = gtk_cell_renderer_pixbuf_new ();
        column = gtk_tree_view_column_new_with_attributes ("Image", renderer,
                                                           "pixbuf", PIXBUF_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (plug->priv->user_treeview), column);

        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
                                                           "text", DISPLAY_NAME_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (plug->priv->user_treeview), column);

        gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (plug->priv->user_treeview),
                                              separator_func,
                                              GINT_TO_POINTER (NAME_COLUMN),
                                              NULL);

        gtk_tree_view_column_set_sort_column_id (column, 0);
        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
                                         0,
                                         compare_users,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                              0,
                                              GTK_SORT_ASCENDING);
        profile_end ("end", NULL);
}

static gboolean
setup_treeview_idle (GSLockPlug *plug)
{
        setup_treeview (plug);

        return FALSE;
}

static const char *
get_user_display_name (void)
{
        const char *name;

        name = g_get_real_name ();

        if (name == NULL || strcmp (name, "Unknown") == 0) {
                name = g_get_user_name ();
        }

        return name;
}

static void
label_set_big_bold (GtkLabel *label)
{
        PangoAttrList        *pattrlist;
        PangoAttribute       *attr;

        pattrlist = pango_attr_list_new ();

        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        attr = pango_attr_scale_new (1.2);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (label, pattrlist);

        pango_attr_list_unref (pattrlist);
}

static void
label_set_bigger (GtkLabel *label)
{
        PangoAttrList        *pattrlist;
        PangoAttribute       *attr;

        pattrlist = pango_attr_list_new ();

        attr = pango_attr_scale_new (1.4);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (label, pattrlist);

        pango_attr_list_unref (pattrlist);
}

static void
label_set_big (GtkLabel *label)
{
        PangoAttrList        *pattrlist;
        PangoAttribute       *attr;

        pattrlist = pango_attr_list_new ();

        attr = pango_attr_scale_new (1.2);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (label, pattrlist);

        pango_attr_list_unref (pattrlist);
}

static gboolean
check_user_file (const gchar *filename,
		 uid_t        user,
		 gssize       max_file_size,
		 gboolean     relax_group,
		 gboolean     relax_other)
{
        struct stat fileinfo;

        if (max_file_size < 0)
                max_file_size = G_MAXSIZE;

        /* Exists/Readable? */
        if (g_stat (filename, &fileinfo) < 0)
                return FALSE;

        /* Is a regular file */
        if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode)))
                return FALSE;

        /* Owned by user? */
        if (G_UNLIKELY (fileinfo.st_uid != user))
                return FALSE;

        /* Group not writable or relax_group? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWGRP) == S_IWGRP && !relax_group))
                return FALSE;

        /* Other not writable or relax_other? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWOTH) == S_IWOTH && !relax_other))
                return FALSE;

        /* Size is kosher? */
        if (G_UNLIKELY (fileinfo.st_size > max_file_size))
                return FALSE;

        return TRUE;
}

static GtkWidget *
get_face_image ()
{
        GtkWidget    *image;
        GdkPixbuf    *pixbuf;
        GtkIconTheme *theme;
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

	theme = gtk_icon_theme_get_default ();

        if (! pixbuf) {
                pixbuf = gtk_icon_theme_load_icon (theme,
                                                   "stock_person",
                                                   icon_size,
                                                   0,
                                                   NULL);
        }

        if (! pixbuf) {
                pixbuf = gtk_icon_theme_load_icon (theme,
                                                   GTK_STOCK_MISSING_IMAGE,
                                                   icon_size,
                                                   0,
                                                   NULL);
        }

        image = gtk_image_new_from_pixbuf (pixbuf);

        g_object_unref (pixbuf);

        return image;
}

static void
create_page_one (GSLockPlug *plug)
{
        GtkWidget            *widget;
        GtkWidget            *password_label;
        GtkWidget            *align;
        GtkWidget            *vbox;
        GtkWidget            *vbox2;
        GtkWidget            *hbox;
        char                 *str;

        profile_start ("start", "page one");

        align = gtk_alignment_new (0.5, 0.5, 1, 1);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), align, NULL);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        /* should we make this a gconf preference? */
        if (0) {
                char *str;

                /* translators: %s is a computer hostname */
                str = g_strdup_printf (_("Welcome to %s"), g_get_host_name ());
                widget = gtk_label_new (str);
                gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);
                gtk_misc_set_alignment (GTK_MISC (widget), 0.5, 0.5);
                g_free (str);

                label_set_big_bold (GTK_LABEL (widget));
        }

        widget = get_face_image ();
        gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

        plug->priv->username_label = gtk_label_new (get_user_display_name ());
        gtk_misc_set_alignment (GTK_MISC (plug->priv->username_label), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->username_label, FALSE, FALSE, 0);

        label_set_bigger (GTK_LABEL (plug->priv->username_label));

        str = g_strdup_printf ("(%s)", g_get_user_name ());
        widget = gtk_label_new (str);
        g_free (str);
        gtk_misc_set_alignment (GTK_MISC (widget), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), widget, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

        password_label = gtk_label_new_with_mnemonic (_("_Password:"));
        gtk_misc_set_alignment (GTK_MISC (password_label), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), password_label, FALSE, FALSE, 0);

        plug->priv->password_entry = gtk_entry_new ();
        gtk_box_pack_start (GTK_BOX (hbox), plug->priv->password_entry, TRUE, TRUE, 0);

        /* button press handler used to inhibit popup menu */
        g_signal_connect (plug->priv->password_entry, "button_press_event",
                          G_CALLBACK (entry_button_press), NULL);
        g_signal_connect (plug->priv->password_entry, "key_press_event",
                          G_CALLBACK (entry_key_press), plug);
        gtk_entry_set_activates_default (GTK_ENTRY (plug->priv->password_entry), TRUE);
        gtk_entry_set_visibility (GTK_ENTRY (plug->priv->password_entry), FALSE);

        gtk_label_set_mnemonic_widget (GTK_LABEL (password_label),
                                       plug->priv->password_entry);

        plug->priv->capslock_label = gtk_label_new ("");
        gtk_misc_set_alignment (GTK_MISC (plug->priv->capslock_label), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->capslock_label, FALSE, FALSE, 0);

        profile_end ("end", "page one");
}

static void
create_page_two (GSLockPlug *plug)
{
        GtkWidget            *widget;
        GtkWidget            *vbox;

        profile_start ("start", "page two");

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), vbox, NULL);

        widget = gtk_label_new (_("Switch to user:"));
        gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

        label_set_big (GTK_LABEL (widget));

        widget = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (widget),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (widget),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (vbox), widget, TRUE, TRUE, 0);

        plug->priv->user_treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (plug->priv->user_treeview), FALSE);
        gtk_container_add (GTK_CONTAINER (widget), plug->priv->user_treeview);

        g_idle_add ((GSourceFunc)setup_treeview_idle, plug);

        profile_end ("end", "page two");
}

static void
create_buttons (GSLockPlug *plug)
{
        GtkWidget            *widget;

        profile_start ("start", "buttons");

        plug->priv->switch_button = gtk_button_new ();
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->switch_button), FALSE);

        widget = get_switch_button_for_page (AUTH_PAGE);
        gtk_container_add (GTK_CONTAINER (plug->priv->switch_button), widget);

        GTK_WIDGET_SET_FLAGS (plug->priv->switch_button, GTK_CAN_DEFAULT);
        gtk_widget_show_all (plug->priv->switch_button);
        gtk_box_pack_start (GTK_BOX (plug->action_area), plug->priv->switch_button,
                            TRUE, TRUE, 0);
        gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (plug->action_area),
                                            plug->priv->switch_button,
                                            TRUE);

        plug->priv->logout_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                              _("Log _Out"),
                                                              GS_LOCK_PLUG_RESPONSE_CANCEL);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->logout_button), FALSE);

        plug->priv->cancel_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                              GTK_STOCK_CANCEL,
                                                              GS_LOCK_PLUG_RESPONSE_CANCEL);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->cancel_button), FALSE);

        plug->priv->ok_button = gtk_button_new ();
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->ok_button), FALSE);

        widget = get_ok_button_for_page (AUTH_PAGE);
        gtk_container_add (GTK_CONTAINER (plug->priv->ok_button), widget);
        GTK_WIDGET_SET_FLAGS (plug->priv->ok_button, GTK_CAN_DEFAULT);
        gtk_window_set_default (GTK_WINDOW (plug), plug->priv->ok_button);
        gtk_widget_show_all (plug->priv->ok_button);
        gs_lock_plug_add_action_widget (GS_LOCK_PLUG (plug), plug->priv->ok_button,
                                        GS_LOCK_PLUG_RESPONSE_OK);

        gs_lock_plug_set_default_response (plug, GS_LOCK_PLUG_RESPONSE_OK);

        profile_end ("end", "buttons");
}

static void
gs_lock_plug_init (GSLockPlug *plug)
{
        profile_start ("start", NULL);

        plug->priv = GS_LOCK_PLUG_GET_PRIVATE (plug);

        plug->priv->fusa_manager = NULL;

        /* Dialog emulation */

        plug->vbox = gtk_vbox_new (FALSE, 0);

        gtk_container_add (GTK_CONTAINER (plug), plug->vbox);

        plug->action_area = gtk_hbutton_box_new ();

        gtk_button_box_set_layout (GTK_BUTTON_BOX (plug->action_area),
                                   GTK_BUTTONBOX_END);

        gtk_box_pack_end (GTK_BOX (plug->vbox), plug->action_area,
                          FALSE, TRUE, 0);
        gtk_widget_show (plug->action_area);

        /* Notebook */

        plug->priv->notebook = gtk_notebook_new ();
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
        gtk_notebook_set_show_border (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
        gtk_box_pack_start (GTK_BOX (plug->vbox), plug->priv->notebook, FALSE, FALSE, 0);

        /* Page 1 */

        create_page_one (plug);

        /* Page 2 */

        create_page_two (plug);

        /* Status text */

        plug->priv->status_label = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX (plug->vbox), plug->priv->status_label,
                            FALSE, FALSE, 0);

        /* Buttons */

        create_buttons (plug);

        gtk_widget_show_all (plug->vbox);

        if (! plug->priv->logout_enabled)
                gtk_widget_hide (plug->priv->logout_button);
        if (! plug->priv->switch_enabled)
                gtk_widget_hide (plug->priv->switch_button);

        plug->priv->timeout = DIALOG_TIMEOUT_MSEC;

        g_signal_connect_swapped (plug->priv->switch_button, "clicked",
                                  G_CALLBACK (switch_page), plug);

        g_signal_connect (plug->priv->logout_button, "clicked",
                          G_CALLBACK (logout_button_clicked), plug);

        profile_end ("end", NULL);
}

static void
gs_lock_plug_finalize (GObject *object)
{
        GSLockPlug *plug;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LOCK_PLUG (object));

        plug = GS_LOCK_PLUG (object);

        g_return_if_fail (plug->priv != NULL);

        if (plug->priv->fusa_manager)
                g_object_unref (plug->priv->fusa_manager);

        if (plug->priv->password_check_idle_id > 0) {
                g_source_remove (plug->priv->password_check_idle_id);
                plug->priv->password_check_idle_id = 0;
        }

        if (plug->priv->response_idle_id > 0) {
                g_source_remove (plug->priv->response_idle_id);
                plug->priv->response_idle_id = 0;
        }

        if (plug->priv->idle_id > 0) {
                g_source_remove (plug->priv->idle_id);
                plug->priv->idle_id = 0;
        }

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget *
gs_lock_plug_new (void)
{
        GtkWidget *result;

        result = g_object_new (GS_TYPE_LOCK_PLUG, NULL);

        gtk_window_set_focus_on_map (GTK_WINDOW (result), TRUE);

        return result;
}
