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
#include <glib/gprintf.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#include <gtk/gtk.h>

#include "gs-lock-plug.h"

#include "passwd.h"
#include "fusa-manager.h"

enum { 
        AUTH_PAGE = 0,
        SWITCH_PAGE
};

static void gs_lock_plug_class_init (GSLockPlugClass *klass);
static void gs_lock_plug_init       (GSLockPlug      *plug);
static void gs_lock_plug_finalize   (GObject         *object);

static gboolean password_check_idle_cb (GSLockPlug *plug);

#define GS_LOCK_PLUG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LOCK_PLUG, GSLockPlugPrivate))

struct GSLockPlugPrivate
{
        GtkWidget   *notebook;
        GtkWidget   *username_entry;
        GtkWidget   *password_entry;
        GtkWidget   *capslock_label;
        GtkWidget   *progress_bar;

        GtkWidget   *ok_button;
        GtkWidget   *cancel_button;
        GtkWidget   *switch_button;

        gboolean     caps_lock_on;

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
        gtk_box_set_spacing (GTK_BOX (plug->vbox), 24);

        gtk_container_set_border_width (GTK_CONTAINER (plug->action_area), 0);
        gtk_box_set_spacing (GTK_BOX (plug->action_area), 5);
}

static void
manager_new_console_cb (FusaManager  *manager,
			FusaDisplay  *display,
			const GError *error,
			GSLockPlug   *plug)
{
        g_signal_emit (plug,
                       lock_plug_signals [RESPONSE],
                       0,
                       GS_LOCK_PLUG_RESPONSE_CANCEL);
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

                gtk_progress_bar_set_fraction  (GTK_PROGRESS_BAR (plug->priv->progress_bar), 0);
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (plug->priv->progress_bar), " ");

                if (current_page == 0) {
                        gtk_widget_set_sensitive (plug->priv->password_entry, FALSE);
                        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (plug->priv->progress_bar), _("Verifying password..."));

                        plug->priv->password_check_idle_id = g_idle_add ((GSourceFunc)password_check_idle_cb,
                                                                         plug);
                        return;
                } else {
                        GdkScreen   *screen;
                        FusaManager *dm;

                        if (gtk_widget_has_screen (GTK_WIDGET (plug)))
                                screen = gtk_widget_get_screen (GTK_WIDGET (plug));
                        else
                                screen = gdk_screen_get_default ();

                        dm = fusa_manager_ref_default ();

                        /* this is async */
                        fusa_manager_new_console (dm,
                                                  screen,
                                                  (FusaManagerDisplayCallback)manager_new_console_cb,
                                                  plug,
                                                  NULL);
                        g_object_unref (dm);

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
        gdouble  fraction;
        char    *message;

        g_get_current_time (&now);

        elapsed = (now.tv_sec - plug->priv->start_time.tv_sec) * 1000
                + (now.tv_usec - plug->priv->start_time.tv_usec) / 1000;
        remaining = plug->priv->timeout - elapsed;

        fraction = CLAMP ((gdouble)remaining / plug->priv->timeout, 0, 1);

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (plug->priv->progress_bar), fraction);

        message = g_strdup_printf (_("About %ld seconds left"),
                                   remaining / 1000 + 1);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (plug->priv->progress_bar),
                                   message);
        g_free (message);

        if ((remaining <= 0) || (remaining > plug->priv->timeout)) {
                message = g_strdup (_("Time expired!"));
                gtk_widget_set_sensitive (plug->priv->password_entry, FALSE);
                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (plug->priv->progress_bar),
                                           message);
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
gs_lock_plug_show (GtkWidget *widget)
{

        if (GTK_WIDGET_CLASS (parent_class)->show)
                GTK_WIDGET_CLASS (parent_class)->show (widget);

        capslock_update (GS_LOCK_PLUG (widget), is_capslock_on ());

        g_get_current_time (&(GS_LOCK_PLUG (widget)->priv->start_time));
        GS_LOCK_PLUG (widget)->priv->idle_id = g_timeout_add (50,
                                                              (GSourceFunc)monitor_progress,
                                                              GS_LOCK_PLUG (widget));
}

static void
gs_lock_plug_hide (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (parent_class)->hide)
                GTK_WIDGET_CLASS (parent_class)->hide (widget);
}

static void
gs_lock_plug_class_init (GSLockPlugClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize  = gs_lock_plug_finalize;

        widget_class->style_set  = gs_lock_plug_style_set;
        widget_class->show       = gs_lock_plug_show;
        widget_class->hide       = gs_lock_plug_hide;

        g_type_class_add_private (klass, sizeof (GSLockPlugPrivate));


        lock_plug_signals [RESPONSE] = g_signal_new ("response",
                                                     G_OBJECT_CLASS_TYPE (klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GSLockPlugClass, response),
                                                     NULL, NULL,
                                                     g_cclosure_marshal_VOID__INT,
                                                     G_TYPE_NONE, 1,
                                                     G_TYPE_INT);
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
                        plug->priv->response_idle_id = g_timeout_add (1000,
                                                                      (GSourceFunc)response_idle_cb,
                                                                      plug);

                gtk_progress_bar_set_text (GTK_PROGRESS_BAR (plug->priv->progress_bar), _("Password verification failed!"));
        }

        memset (local_password, '\b', strlen (local_password));
        g_free (local_password);

        return FALSE;
}

static GtkWidget *
get_ok_button_for_page (gint page)
{
        GtkWidget *align;
        GtkWidget *widget;
        GtkWidget *hbox;
        const char *stock_id = NULL;
        const char *label    = NULL;

        align = gtk_alignment_new (0.5, 0.5, 0, 0);
        hbox = gtk_hbox_new (FALSE, 2);
        gtk_container_add (GTK_CONTAINER (align), hbox);

        switch (page) {
        case (AUTH_PAGE):
                stock_id = GTK_STOCK_DIALOG_AUTHENTICATION;
                label = N_("_Unlock");
                break;
        case (SWITCH_PAGE):
                stock_id = GTK_STOCK_REFRESH;
                label = N_("_Login Screen");
                break;
        default:
                g_assert ("Invalid notebook page");
                break;
        }

        widget = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        widget = gtk_label_new_with_mnemonic (label);
        gtk_box_pack_end (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        return align;
}

static GtkWidget *
get_switch_button_for_page (gint page)
{
        GtkWidget *align;
        GtkWidget *widget;
        GtkWidget *hbox;
        const char *stock_id = NULL;
        const char *label    = NULL;

        align = gtk_alignment_new (0.5, 0.5, 0, 0);
        hbox = gtk_hbox_new (FALSE, 2);
        gtk_container_add (GTK_CONTAINER (align), hbox);

        switch (page) {
        case (AUTH_PAGE):
                stock_id = GTK_STOCK_REFRESH;
                label = N_("_Login Screen");
                break;
        case (SWITCH_PAGE):
                stock_id = GTK_STOCK_DIALOG_AUTHENTICATION;
                label = N_("_Unlock");
                break;
        default:
                g_assert ("Invalid notebook page");
                break;
        }

        widget = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        widget = gtk_label_new_with_mnemonic (label);
        gtk_box_pack_end (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        return align;
}

static void
switch_page (GtkButton  *button,
             GSLockPlug *plug)
{
        GtkWidget *ok_widget;
        GtkWidget *other_widget;
        gint       current_page;
        gint       next_page;

        g_return_if_fail (plug != NULL);
        g_return_if_fail (GTK_IS_WIDGET (button));

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

        gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), next_page);
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

        capslock_on = is_capslock_on ();

        if (capslock_on != plug->priv->caps_lock_on)
                capslock_update (plug, capslock_on);

        switch (event->keyval) {
        case GDK_Return:
        case GDK_KP_Enter:
                gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_OK);
                break;
        default:
                break;
        }
        
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

static void
gs_lock_plug_init (GSLockPlug *plug)
{
        GtkWidget      *widget;
        GtkWidget      *password_label;
        GtkWidget      *hbox;
        GtkWidget      *vbox;
        GtkWidget      *table;
        int             font_size;
	PangoAttrList  *pattrlist;
	PangoAttribute *attr;

        plug->priv = GS_LOCK_PLUG_GET_PRIVATE (plug);

        /* Dialog emulation */

        plug->vbox = gtk_vbox_new (FALSE, 0);

        gtk_container_add (GTK_CONTAINER (plug), plug->vbox);
        gtk_widget_show (plug->vbox);

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

        hbox = gtk_hbox_new (FALSE, 12);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), hbox, NULL);

        widget = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION, GTK_ICON_SIZE_DIALOG);
        gtk_misc_set_alignment (GTK_MISC (widget), 0.5, 0);
        gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

        widget = gtk_label_new (_("Enter password to unlock screen"));
        gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

        pattrlist = pango_attr_list_new ();
        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        font_size = pango_font_description_get_size (widget->style->font_desc);
        attr = pango_attr_size_new (font_size * 1.2);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);

        hbox = gtk_hbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

        widget = gtk_label_new ("    ");
        gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        table = gtk_table_new (3, 2, FALSE);
        gtk_table_set_row_spacings (GTK_TABLE (table), 12);
        gtk_table_set_col_spacings (GTK_TABLE (table), 6);
        gtk_box_pack_start (GTK_BOX (hbox), table, TRUE, TRUE, 0);

        widget = gtk_label_new (_("Name:"));
        gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
        gtk_table_attach (GTK_TABLE (table), widget, 0, 1, 0, 1,
                          GTK_FILL, 0, 0, 0);
        password_label = gtk_label_new_with_mnemonic (_("_Password:"));
        gtk_misc_set_alignment (GTK_MISC (password_label), 0, 0.5);
        gtk_table_attach (GTK_TABLE (table), password_label, 0, 1, 1, 2,
                          GTK_FILL, 0, 0, 0);

        plug->priv->capslock_label = gtk_label_new ("");
        gtk_misc_set_alignment (GTK_MISC (plug->priv->capslock_label), 0, 0.5);
        gtk_table_attach (GTK_TABLE (table), plug->priv->capslock_label, 1, 2, 2, 3,
                          GTK_FILL, 0, 0, 0);

        plug->priv->username_entry = gtk_entry_new ();
        /* button press handler used to inhibit popup menu */
        g_signal_connect (plug->priv->username_entry, "button_press_event",
                          G_CALLBACK (entry_button_press), NULL);

        gtk_widget_set_sensitive (plug->priv->username_entry, FALSE);
        gtk_editable_set_editable (GTK_EDITABLE (plug->priv->username_entry), FALSE);
        gtk_table_attach (GTK_TABLE (table), plug->priv->username_entry, 1, 2, 0, 1,
                          GTK_EXPAND | GTK_FILL, 0, 0, 0);
        gtk_entry_set_text (GTK_ENTRY (plug->priv->username_entry), g_get_real_name ());

        plug->priv->password_entry = gtk_entry_new ();

        /* button press handler used to inhibit popup menu */
        g_signal_connect (plug->priv->password_entry, "button_press_event",
                          G_CALLBACK (entry_button_press), NULL);
        g_signal_connect (plug->priv->password_entry, "key_press_event",
                          G_CALLBACK (entry_key_press), plug);
        gtk_entry_set_activates_default (GTK_ENTRY (plug->priv->password_entry), TRUE);
        gtk_entry_set_visibility (GTK_ENTRY (plug->priv->password_entry), FALSE);
        gtk_table_attach (GTK_TABLE (table), plug->priv->password_entry, 1, 2, 1, 2,
                          GTK_EXPAND | GTK_FILL, 0, 0, 0);
        gtk_label_set_mnemonic_widget (GTK_LABEL (password_label),
                                       plug->priv->password_entry);

        /* Page 2 */

        hbox = gtk_hbox_new (FALSE, 12);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), hbox, NULL);

        widget = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_DIALOG);
        gtk_misc_set_alignment (GTK_MISC (widget), 0.5, 0);
        gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

        widget = gtk_label_new (_("Log in as another user?"));
        gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

        pattrlist = pango_attr_list_new ();
        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        font_size = pango_font_description_get_size (widget->style->font_desc);
        attr = pango_attr_size_new (font_size * 1.2);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);

        widget = gtk_label_new (_("This option will bring you to the Login Screen.\n"
                                  "From the Login Screen you may log in to this system\n"
                                  "as another user or select 'Quit' to return to this screen."));
        gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

        /* Progress bar */

        plug->priv->progress_bar = gtk_progress_bar_new ();
        gtk_progress_bar_set_fraction  (GTK_PROGRESS_BAR (plug->priv->progress_bar), 1.0);
        gtk_progress_bar_set_orientation (GTK_PROGRESS_BAR (plug->priv->progress_bar),
                                          GTK_PROGRESS_RIGHT_TO_LEFT);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (plug->priv->progress_bar), " ");

        gtk_box_pack_start (GTK_BOX (plug->vbox), plug->priv->progress_bar,
                            FALSE, FALSE, 0);

        /* Buttons */

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

        plug->priv->cancel_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                              GTK_STOCK_CANCEL,
                                                              GS_LOCK_PLUG_RESPONSE_CANCEL);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->cancel_button), FALSE);

        plug->priv->ok_button =  gtk_button_new ();
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->ok_button), FALSE);

        widget = get_ok_button_for_page (AUTH_PAGE);
        gtk_container_add (GTK_CONTAINER (plug->priv->ok_button), widget);
        GTK_WIDGET_SET_FLAGS (plug->priv->ok_button, GTK_CAN_DEFAULT);
        gtk_window_set_default (GTK_WINDOW (plug), plug->priv->ok_button);
        gtk_widget_show_all (plug->priv->ok_button);
        gs_lock_plug_add_action_widget (GS_LOCK_PLUG (plug), plug->priv->ok_button,
                                        GS_LOCK_PLUG_RESPONSE_OK);

        gs_lock_plug_set_default_response (plug, GS_LOCK_PLUG_RESPONSE_OK);

        gtk_widget_show_all (plug->vbox);

        plug->priv->timeout = 30000;

        g_signal_connect (plug->priv->switch_button, "clicked",
                          G_CALLBACK (switch_page), plug);

}

static void
gs_lock_plug_finalize (GObject *object)
{
        GSLockPlug *plug;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LOCK_PLUG (object));

        plug = GS_LOCK_PLUG (object);

        g_return_if_fail (plug->priv != NULL);

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
