/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *          Rodrigo Moya <rodrigo@novell.com>
 *
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>

#include <libgnomeui/gnome-ui-init.h>
#include <libgnomeui/gnome-help.h> /* for gnome_help_display */
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "file-transfer-dialog.h"

#include "gs-job.h"
#include "gs-prefs.h" /* for GS_MODE enum */

#define GLADE_XML_FILE "gnome-screensaver-preferences.glade"

#define KEY_DIR             "/apps/gnome-screensaver"
#define KEY_LOCK            KEY_DIR "/lock_enabled"
#define KEY_IDLE_ACTIVATION_ENABLED         KEY_DIR "/idle_activation_enabled"
#define KEY_MODE            KEY_DIR "/mode"
#define KEY_ACTIVATE_DELAY  KEY_DIR "/idle_delay"
#define KEY_LOCK_DELAY      KEY_DIR "/lock_delay"
#define KEY_CYCLE_DELAY     KEY_DIR "/cycle_delay"
#define KEY_DPMS_ENABLED    KEY_DIR "/dpms_enabled"
#define KEY_DPMS_STANDBY    KEY_DIR "/dpms_standby"
#define KEY_DPMS_SUSPEND    KEY_DIR "/dpms_suspend"
#define KEY_DPMS_OFF        KEY_DIR "/dpms_off"
#define KEY_THEMES          KEY_DIR "/themes"

enum {
        NAME_COLUMN,
        ID_COLUMN,
        N_COLUMNS
};

static GConfEnumStringPair mode_enum_map [] = {
       { GS_MODE_BLANK_ONLY,       "blank-only" },
       { GS_MODE_RANDOM,           "random"     },
       { GS_MODE_SINGLE,           "single"     },
       { 0, NULL }
};

/* Drag and drop info */
enum {
        TARGET_URI_LIST,
        TARGET_NS_URL
};

static GtkTargetEntry drop_types [] =
{
        { "text/uri-list", 0, TARGET_URI_LIST },
        { "_NETSCAPE_URL", 0, TARGET_NS_URL }
};

static GladeXML *xml = NULL;
static GSJob    *job = NULL;

static gint32
config_get_activate_delay (gboolean *is_writable)
{
        GConfClient *client;
        gint32       delay;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             KEY_ACTIVATE_DELAY,
                                                             NULL);
        }

        delay = gconf_client_get_int (client, KEY_ACTIVATE_DELAY, NULL);

        if (delay < 1)
                delay = 1;

        g_object_unref (client);

        return delay;
}

static void
config_set_activate_delay (gint32 timeout)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        gconf_client_set_int (client, KEY_ACTIVATE_DELAY, timeout, NULL);

        g_object_unref (client);
}

static int
config_get_mode (gboolean *is_writable)
{
        GConfClient *client;
        int          mode;
        char        *string;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             KEY_MODE,
                                                             NULL);
        }

        string = gconf_client_get_string (client, KEY_MODE, NULL);
        if (string) {
                gconf_string_to_enum (mode_enum_map, string, &mode);
                g_free (string);
        } else {
                mode = GS_MODE_BLANK_ONLY;
        }

        g_object_unref (client);

        return mode;
}

static void
config_set_mode (int mode)
{
        GConfClient *client;
        const char  *mode_string;

        client = gconf_client_get_default ();

        mode_string = gconf_enum_to_string (mode_enum_map, mode);
        gconf_client_set_string (client, KEY_MODE, mode_string, NULL);

        g_object_unref (client);
}

static char *
config_get_theme (gboolean *is_writable)
{
        GConfClient *client;
        char        *name = NULL;
        int          mode;

        client = gconf_client_get_default ();

        if (is_writable) {
                gboolean can_write_theme;
                gboolean can_write_mode;

                can_write_theme = gconf_client_key_is_writable (client,
                                                                KEY_THEMES,
                                                                NULL);
                can_write_mode = gconf_client_key_is_writable (client,
                                                               KEY_MODE,
                                                               NULL);
                *is_writable = can_write_theme && can_write_mode;
        }

        mode = config_get_mode (NULL);

        if (mode == GS_MODE_BLANK_ONLY) {
                name = g_strdup ("__blank-only");
        } else if (mode == GS_MODE_RANDOM) {
                name = g_strdup ("__random");
        } else {
                GSList *list;
                list = gconf_client_get_list (client,
                                              KEY_THEMES,
                                              GCONF_VALUE_STRING,
                                              NULL);
                if (list) {
                        name = g_strdup (list->data);
                } else {
                        /* TODO: handle error */
                }

                g_slist_foreach (list, (GFunc)g_free, NULL);
                g_slist_free (list);
        }

        g_object_unref (client);

        return name;
}

static GSList *
get_all_theme_ids (GSJob *job)
{
        GSList *ids = NULL;
        GSList *entries;
        GSList *l;

        entries = gs_job_get_theme_info_list (job);
        for (l = entries; l; l = l->next) {
                GSJobThemeInfo *info = l->data;

                ids = g_slist_prepend (ids, g_strdup (gs_job_theme_info_get_id (info)));
                gs_job_theme_info_unref (info);
        }
        g_slist_free (entries);

        return ids;
}

static void
config_set_theme (const char *theme_id)
{
        GConfClient *client;
        GSList      *list = NULL;
        int          mode;

        client = gconf_client_get_default ();

        if (theme_id && strcmp (theme_id, "__blank-only") == 0) {
                mode = GS_MODE_BLANK_ONLY;
        } else if (theme_id && strcmp (theme_id, "__random") == 0) {
                mode = GS_MODE_RANDOM;

                /* set the themes key to contain all available screensavers */
                list = get_all_theme_ids (job);
        } else {
                mode = GS_MODE_SINGLE;
                list = g_slist_append (list, g_strdup (theme_id));
        }

        config_set_mode (mode);

        gconf_client_set_list (client,
                               KEY_THEMES,
                               GCONF_VALUE_STRING,
                               list,
                               NULL);

        g_slist_foreach (list, (GFunc) g_free, NULL);
        g_slist_free (list);

        g_object_unref (client);
}

static gboolean
config_get_enabled (gboolean *is_writable)
{
        int          enabled;
        GConfClient *client;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             KEY_LOCK,
                                                             NULL);
        }

        enabled = gconf_client_get_bool (client, KEY_IDLE_ACTIVATION_ENABLED, NULL);

        g_object_unref (client);

        return enabled;
}

static void
config_set_enabled (gboolean enabled)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        gconf_client_set_bool (client, KEY_IDLE_ACTIVATION_ENABLED, enabled, NULL);

        g_object_unref (client);
}

static gboolean
config_get_lock (gboolean *is_writable)
{
        GConfClient *client;
        gboolean     lock;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             KEY_LOCK,
                                                             NULL);
        }

        lock = gconf_client_get_bool (client, KEY_LOCK, NULL);

        g_object_unref (client);

        return lock;
}

static void
config_set_lock (gboolean lock)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        gconf_client_set_bool (client, KEY_LOCK, lock, NULL);

        g_object_unref (client);
}

static void
preview_clear (GtkWidget *widget)
{
        GdkColor color = { 0, 0, 0 };

        gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, &color);
        gdk_window_clear (widget->window);
}

static void
preview_set_theme (GtkWidget  *widget,
                   const char *theme)
{
        GError *error = NULL;

        if (job) {
                gs_job_stop (job);
        }

        preview_clear (widget);

        if ((theme && strcmp (theme, "__blank-only") == 0)) {

        } else if (theme && strcmp (theme, "__random") == 0) {
                GSList *themes;

                themes = get_all_theme_ids (job);
                if (themes) {
                        if (! gs_job_set_theme (job, (const char *) themes->data, &error)) {
                                if (error) {
                                        g_warning ("Could not set theme: %s", error->message);
                                        g_error_free (error);
                                }
                                return;
                        }
                        g_slist_foreach (themes, (GFunc) g_free, NULL);
                        g_slist_free (themes);

                        gs_job_start (job);
                }
                
        } else {
                if (! gs_job_set_theme (job, theme, &error)) {
                        if (error) {
                                g_warning ("Could not set theme: %s", error->message);
                                g_error_free (error);
                        }
                        return;
                }
                gs_job_start (job);
        }
}

static void
response_cb (GtkWidget *widget,
             int        response_id)
{

        if (response_id == GTK_RESPONSE_HELP) {
                GError *error;
                error = NULL;
                gnome_help_display_desktop (NULL,
                                            "user-guide",
                                            "user-guide.xml",
                                            "prefs-screensaver",
                                            &error);
                if (error != NULL) {
                        g_warning ("%s", error->message);
                        g_error_free (error);
                }

                return;
        }

        gtk_widget_destroy (widget);
        gtk_main_quit ();
}

static GSList *
get_theme_info_list (void)
{
        return gs_job_get_theme_info_list (job);
}

static void
populate_model (GtkTreeStore *store)
{
        GtkTreeIter iter;
        GSList     *themes        = NULL;
        GSList     *l;

        gtk_tree_store_append (store, &iter, NULL);
        gtk_tree_store_set (store, &iter,
                            NAME_COLUMN, _("Blank screen"),
                            ID_COLUMN, "__blank-only",
                            -1);

        gtk_tree_store_append (store, &iter, NULL);
        gtk_tree_store_set (store, &iter,
                            NAME_COLUMN, _("Random"),
                            ID_COLUMN, "__random",
                            -1);

        gtk_tree_store_append (store, &iter, NULL);
        gtk_tree_store_set (store, &iter,
                            NAME_COLUMN, NULL,
                            ID_COLUMN, "__separator",
                            -1);

        themes = get_theme_info_list ();

        if (! themes)
                return;
        
        for (l = themes; l; l = l->next) {
                GSJobThemeInfo *info = l->data;

                if (! info)
                        continue;

                gtk_tree_store_append (store, &iter, NULL);
                gtk_tree_store_set (store, &iter,
                                    NAME_COLUMN, gs_job_theme_info_get_name (info),
                                    ID_COLUMN, gs_job_theme_info_get_id (info),
                                    -1);

                gs_job_theme_info_unref (info);
        }

        g_slist_free (themes);
}

static void
tree_selection_changed_cb (GtkTreeSelection *selection,
                           GtkWidget        *preview)
{
        GtkTreeIter   iter;
        GtkTreeModel *model;
        char         *theme;

        if (! gtk_tree_selection_get_selected (selection, &model, &iter))
                return;

        gtk_tree_model_get (model, &iter, ID_COLUMN, &theme, -1);

        if (! theme)
                return;

        preview_set_theme (preview, theme);
        config_set_theme (theme);

        g_free (theme);
}

static void
activate_delay_value_changed_cb (GtkRange *range,
                                 gpointer  user_data)
{
        gdouble value;

        value = gtk_range_get_value (range);
        config_set_activate_delay ((gint32)value);
}

static int
compare_theme  (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      user_data)
{
        char *name_a;
        char *name_b;
        char *id_a;
        char *id_b;
        int   result;

        gtk_tree_model_get (model, a, NAME_COLUMN, &name_a, -1);
        gtk_tree_model_get (model, b, NAME_COLUMN, &name_b, -1);
        gtk_tree_model_get (model, a, ID_COLUMN, &id_a, -1);
        gtk_tree_model_get (model, b, ID_COLUMN, &id_b, -1);

        if (! id_a)
                return 1;
        else if (! id_b)
                return -1;

        if (strcmp (id_a, "__blank-only") == 0)
                return -1;
        else if (strcmp (id_b, "__blank-only") == 0)
                return 1;
        else if (strcmp (id_a, "__random") == 0)
                return -1;
        else if (strcmp (id_b, "__random") == 0)
                return 1;
        else if (strcmp (id_a, "__separator") == 0)
                return -1;
        else if (strcmp (id_b, "__separator") == 0)
                return 1;

        if (! name_a)
                return 1;
        else if (! name_b)
                return -1;

        result = strcmp (name_a, name_b);

        g_free (name_a);
        g_free (name_b);
        g_free (id_a);
        g_free (id_b);

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

static void
setup_treeview (GtkWidget *tree,
                GtkWidget *preview)
{
        GtkTreeStore      *store;
        GtkTreeViewColumn *column;
        GtkCellRenderer   *renderer;
        GtkTreeSelection  *select;

        store = gtk_tree_store_new (N_COLUMNS,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING);
        populate_model (store);

        gtk_tree_view_set_model (GTK_TREE_VIEW (tree),
                                 GTK_TREE_MODEL (store));

        g_object_unref (store);

        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
                                                           "text", NAME_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

        gtk_tree_view_column_set_sort_column_id (column, 0);
        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
                                         0,
                                         compare_theme,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                              0,
                                              GTK_SORT_ASCENDING);

        gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (tree),
                                              separator_func,
                                              GINT_TO_POINTER (ID_COLUMN),
                                              NULL);

        select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
        gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
        g_signal_connect (G_OBJECT (select), "changed",
                          G_CALLBACK (tree_selection_changed_cb),
                          preview);

}

static void
setup_treeview_selection (GtkWidget *tree)
{
        char         *theme;
        GtkTreeModel *model;
        GtkTreeIter   iter;
        GtkTreePath  *path = NULL;
        gboolean      is_writable;

        theme = config_get_theme (&is_writable);

        if (! is_writable)
                gtk_widget_set_sensitive (tree, FALSE);

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

        if (theme && gtk_tree_model_get_iter_first (model, &iter)) {
                char *id;

                do {
                        gtk_tree_model_get (model, &iter,
                                            ID_COLUMN, &id, -1);
                        if (id && strcmp (id, theme) == 0) {
                                path = gtk_tree_model_get_path (model, &iter);
                                break;
                        }

                } while (gtk_tree_model_iter_next (model, &iter));
        }

        if (path) {
                gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree),
                                          path,
                                          NULL,
                                          FALSE);

                gtk_tree_path_free (path);
        }

        g_free (theme);
}

static void
reload_themes (void)
{
        GtkWidget    *treeview;
        GtkTreeModel *model;

        treeview = glade_xml_get_widget (xml, "savers_treeview");
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
        gtk_tree_store_clear (GTK_TREE_STORE (model));
        populate_model (GTK_TREE_STORE (model));

        gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
                                 GTK_TREE_MODEL (model));
}

static void
transfer_done_cb (GtkWidget *dialog,
                  char      *path)
{

        g_free (path);

        gtk_widget_destroy (dialog);

        reload_themes ();
}

static void
transfer_cancel_cb (GtkWidget *dialog,
                    char      *path)
{
        gnome_vfs_unlink (path);
        g_free (path);

        gtk_widget_destroy (dialog);
}

static void
theme_installer_run (GtkWidget *parent,
                     char      *filename)
{
        GtkWidget    *dialog;
        GnomeVFSURI  *src_uri;
        GList        *src, *target;
        char         *target_path;
        char         *user_dir;
        char         *base;

        src_uri = gnome_vfs_uri_new (filename);
        src = g_list_append (NULL, src_uri);

        user_dir = g_build_filename (g_get_user_data_dir (), "gnome-screensaver", "themes", NULL);
        base = gnome_vfs_uri_extract_short_name (src_uri);

        target_path = NULL;

        while (TRUE) {
                char      *file_tmp;
                int        len = strlen (base);

                if (base && len > 4 && (!strcmp (base + len - 4, ".xml"))) {
                        file_tmp = g_strdup_printf ("screensaver-theme-%d.xml", rand ());
                } else {
                        dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_OK,
                                                         _("Invalid screensaver theme"));
                        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                                  "%s",
                                                                  _("This file does not appear to be a valid screensaver theme."));
                        gtk_window_set_title (GTK_WINDOW (dialog), "");
                        gtk_window_set_icon_name (GTK_WINDOW (dialog), "screensaver");

                        gtk_dialog_run (GTK_DIALOG (dialog));
                        gtk_widget_destroy (dialog);
                        g_free (target_path);
                        return;
                }

                target_path = g_build_filename (user_dir, file_tmp, NULL);

                g_free (file_tmp);
                if (! gnome_vfs_uri_exists (gnome_vfs_uri_new (target_path)))
                        break;
        }
                
        g_free (user_dir);

        target = g_list_append (NULL, gnome_vfs_uri_new (target_path));

        dialog = file_transfer_dialog_new ();
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "screensaver");

        file_transfer_dialog_wrap_async_xfer (FILE_TRANSFER_DIALOG (dialog),
                                              src, target,
                                              GNOME_VFS_XFER_RECURSIVE,
                                              GNOME_VFS_XFER_ERROR_MODE_QUERY,
                                              GNOME_VFS_XFER_OVERWRITE_MODE_QUERY,
                                              GNOME_VFS_PRIORITY_DEFAULT);
        gnome_vfs_uri_list_unref (src);
        gnome_vfs_uri_list_unref (target);

        g_signal_connect (dialog, "cancel",
                          G_CALLBACK (transfer_cancel_cb), target_path);
        g_signal_connect (dialog, "done",
                          G_CALLBACK (transfer_done_cb), target_path);

        gtk_widget_show (dialog);
}

/* Callback issued during drag movements */
static gboolean
drag_motion_cb (GtkWidget      *widget,
                GdkDragContext *context,
                int             x,
                int             y,
                guint           time,
                gpointer        data)
{
        return FALSE;
}

/* Callback issued during drag leaves */
static void
drag_leave_cb (GtkWidget      *widget,
               GdkDragContext *context,
	       guint           time,
               gpointer        data)
{
        gtk_widget_queue_draw (widget);
}

/* Callback issued on actual drops. Attempts to load the file dropped. */
static void
drag_data_received_cb (GtkWidget        *widget,
                       GdkDragContext   *context,
                       int               x,
                       int               y,
                       GtkSelectionData *selection_data,
                       guint             info,
                       guint             time,
                       gpointer          data)
{
        GtkWidget *dialog;
        GList     *uris;
        char      *filename = NULL;

        if (!(info == TARGET_URI_LIST || info == TARGET_NS_URL))
                return;

        uris = gnome_vfs_uri_list_parse ((char *) selection_data->data);
        if (uris != NULL && uris->data != NULL) {
                GnomeVFSURI *uri = (GnomeVFSURI *) uris->data;

                if (gnome_vfs_uri_is_local (uri))
                        filename = gnome_vfs_unescape_string (
                                        gnome_vfs_uri_get_path (uri),
                                        G_DIR_SEPARATOR_S);
                else
                        filename = gnome_vfs_unescape_string (
                                        gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE),
                                        G_DIR_SEPARATOR_S);

                gnome_vfs_uri_list_unref (uris);
        }

        dialog = glade_xml_get_widget (xml, "prefs_dialog");
        theme_installer_run (dialog, filename);

        g_free (filename);
}

/* Adapted from totem_time_to_string_text */
static char *
time_to_string_text (long time)
{
        char *secs, *mins, *hours, *string;
        int   sec, min, hour;

        sec = time % 60;
        time = time - sec;
        min = (time % (60 * 60)) / 60;
        time = time - (min * 60);
        hour = time / (60 * 60);

        hours = g_strdup_printf (ngettext ("%d hour",
                                           "%d hours", hour), hour);

        mins = g_strdup_printf (ngettext ("%d minute",
                                          "%d minutes", min), min);

        secs = g_strdup_printf (ngettext ("%d second",
                                          "%d seconds", sec), sec);

        if (hour > 0) {
                if (sec > 0) {
                        /* hour:minutes:seconds */
                        string = g_strdup_printf (_("%s %s %s"), hours, mins, secs);
                } else if (min > 0) {
                        /* hour:minutes */
                        string = g_strdup_printf (_("%s %s"), hours, mins);
                } else {
                        /* hour */
                        string = g_strdup_printf (_("%s"), hours);
                }
        } else if (min > 0) {
                if (sec > 0) {
                        /* minutes:seconds */
                        string = g_strdup_printf (_("%s %s"), mins, secs);
                } else {
                        /* minutes */
                        string = g_strdup_printf (_("%s"), mins);
                }
        } else if (sec > 0) {
                /* seconds */
                string = g_strdup_printf (_("%s"), secs);
        } else {
                /* 0 seconds */
                string = g_strdup (_("0 seconds"));
        }

        g_free (hours);
        g_free (mins);
        g_free (secs);

        return string;
}

static char *
format_value_callback_time (GtkScale *scale,
                            gdouble   value)
{
        if (value == 0)
                return g_strdup_printf (_("Never"));

        return time_to_string_text (value * 60.0);
}

static void
lock_checkbox_toggled (GtkToggleButton *button, gpointer user_data)
{
        config_set_lock (gtk_toggle_button_get_active (button));
}

static void
enabled_checkbox_toggled (GtkToggleButton *button, gpointer user_data)
{
        config_set_enabled (gtk_toggle_button_get_active (button));
}

static void
invalid_type_warning (const char *type)
{
        g_warning ("Error retrieving configuration key '%s': Invalid type",
                   type);
}

static void
ui_set_lock (gboolean enabled)
{
        GtkWidget *widget;
        gboolean   active;

        widget = glade_xml_get_widget (xml, "lock_checkbox");

        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
}

static void
ui_set_enabled (gboolean enabled)
{
        GtkWidget *widget;
        gboolean   active;
        gboolean   is_writable;

        widget = glade_xml_get_widget (xml, "enable_checkbox");
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }

        widget = glade_xml_get_widget (xml, "lock_checkbox");
        config_get_lock (&is_writable);
        if (is_writable) {
                gtk_widget_set_sensitive (widget, enabled);
        }
}

static void
ui_set_delay (int delay)
{
        GtkWidget *widget;

        widget = glade_xml_get_widget (xml, "activate_delay_hscale");
        gtk_range_set_value (GTK_RANGE (widget), delay);
}

static void
key_changed_cb (GConfClient *client,
                guint        cnxn_id,
                GConfEntry  *entry,
                gpointer     data)
{
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);

        if (! g_str_has_prefix (key, KEY_DIR))
                return;

        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_IDLE_ACTIVATION_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);

                        ui_set_enabled (enabled);
                } else {
                        invalid_type_warning (key);
                }
        } else if (strcmp (key, KEY_LOCK) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);

                        ui_set_lock (enabled);
                } else {
                        invalid_type_warning (key);
                }
        } else if (strcmp (key, KEY_ACTIVATE_DELAY) == 0) {

                if (value->type == GCONF_VALUE_INT) {
                        int delay;

                        delay = gconf_value_get_int (value);
                        ui_set_delay (delay);
                } else {
                        invalid_type_warning (key);
                }

        } else {
                /*g_warning ("Config key not handled: %s", key);*/
        }
}

static void
init_capplet (void)
{
        GtkWidget *dialog;
        GtkWidget *preview;
        GtkWidget *treeview;
        GtkWidget *activate_delay_hscale;
        GtkWidget *activate_delay_hbox;
        GtkWidget *label;
        GtkWidget *enabled_checkbox;
        GtkWidget *lock_checkbox;
        char      *glade_file;
        char      *string;
        gdouble    activate_delay;
        gboolean   enabled;
        gboolean   is_writable;
        GConfClient *client;

        glade_file = g_build_filename (GLADEDIR, GLADE_XML_FILE, NULL);
        xml = glade_xml_new (glade_file, NULL, PACKAGE);
        g_free (glade_file);

        if (xml == NULL) {

                dialog = gtk_message_dialog_new (NULL,
                                                 0, GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK,
                                                 _("Could not load the main interface"));
                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                          _("Please make sure that the screensaver is properly installed"));

                gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                                 GTK_RESPONSE_OK);
                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
                exit (1);
        }

        preview            = glade_xml_get_widget (xml, "preview_area");
        dialog             = glade_xml_get_widget (xml, "prefs_dialog");
        treeview           = glade_xml_get_widget (xml, "savers_treeview");
        activate_delay_hscale = glade_xml_get_widget (xml, "activate_delay_hscale");
        activate_delay_hbox   = glade_xml_get_widget (xml, "activate_delay_hbox");
        enabled_checkbox   = glade_xml_get_widget (xml, "enable_checkbox");
        lock_checkbox      = glade_xml_get_widget (xml, "lock_checkbox");

        label              = glade_xml_get_widget (xml, "activate_delay_label");
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), activate_delay_hscale);
        label              = glade_xml_get_widget (xml, "savers_label");
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), treeview);

        activate_delay = config_get_activate_delay (&is_writable);
        ui_set_delay (activate_delay);
        if (! is_writable)
                gtk_widget_set_sensitive (activate_delay_hbox, FALSE);
        g_signal_connect (activate_delay_hscale, "format-value",
                          G_CALLBACK (format_value_callback_time), NULL);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lock_checkbox), config_get_lock (&is_writable));
        if (! is_writable)
                gtk_widget_set_sensitive (lock_checkbox, FALSE);
        g_signal_connect (lock_checkbox, "toggled",
                          G_CALLBACK (lock_checkbox_toggled), NULL);

        enabled = config_get_enabled (&is_writable);
        ui_set_enabled (enabled);
        if (! is_writable)
                gtk_widget_set_sensitive (enabled_checkbox, FALSE);
        g_signal_connect (enabled_checkbox, "toggled",
                          G_CALLBACK (enabled_checkbox_toggled), NULL);

            
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "screensaver");


        gtk_drag_dest_set (dialog, GTK_DEST_DEFAULT_ALL,
                           drop_types, G_N_ELEMENTS (drop_types),
                           GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE);

        g_signal_connect (dialog, "drag-motion",
                          G_CALLBACK (drag_motion_cb), NULL);
        g_signal_connect (dialog, "drag-leave",
                          G_CALLBACK (drag_leave_cb), NULL);
        g_signal_connect (dialog, "drag-data-received",
                          G_CALLBACK (drag_data_received_cb), NULL);

        gtk_widget_show_all (dialog);

        /* Update list of themes if using random screensaver */
        client = gconf_client_get_default ();
        string = gconf_client_get_string (client, KEY_MODE, NULL);
        if (string) {
                int mode;
                GSList *list;

                gconf_string_to_enum (mode_enum_map, string, &mode);
                g_free (string);

                if (mode == GS_MODE_RANDOM) {
                        list = get_all_theme_ids (job);
                        gconf_client_set_list (client, KEY_THEMES, GCONF_VALUE_STRING, list, NULL);

                        g_slist_foreach (list, (GFunc) g_free, NULL);
                        g_slist_free (list);
                }
        }

	gconf_client_add_dir (client, KEY_DIR,
			      GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
	gconf_client_notify_add (client,
                                 KEY_DIR,
				 key_changed_cb,
                                 NULL, NULL, NULL);

        g_object_unref (client);

        preview_clear (preview);
        gs_job_set_widget (job, preview);

        setup_treeview (treeview, preview);
        setup_treeview_selection (treeview);

        g_signal_connect (activate_delay_hscale, "value-changed",
                          G_CALLBACK (activate_delay_value_changed_cb), NULL);

        g_signal_connect (dialog, "response",
                          G_CALLBACK (response_cb), NULL);
}

int
main (int    argc,
      char **argv)
{

#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif 
        textdomain (GETTEXT_PACKAGE);
#endif 

        gnome_program_init (PACKAGE, VERSION,
                            LIBGNOMEUI_MODULE,
                            argc, argv,
                            GNOME_PROGRAM_STANDARD_PROPERTIES,
                            NULL);

        job = gs_job_new ();

        init_capplet ();

        gtk_main ();

        g_object_unref (job);

	return 0;
}
