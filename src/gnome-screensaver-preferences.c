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
#include <libgnome/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "file-transfer-dialog.h"

#include "gs-job.h"
#include "gs-prefs.h" /* for GS_MODE enum */

#define GLADE_XML_FILE "gnome-screensaver-preferences.glade"

#define KEY_DIR          "/apps/gnome-screensaver"
#define KEY_LOCK         KEY_DIR "/lock"
#define KEY_MODE         KEY_DIR "/mode"
#define KEY_BLANK_DELAY  KEY_DIR "/blank_delay"
#define KEY_LOCK_DELAY   KEY_DIR "/lock_delay"
#define KEY_CYCLE_DELAY  KEY_DIR "/cycle_delay"
#define KEY_DPMS_ENABLED KEY_DIR "/dpms_enabled"
#define KEY_DPMS_STANDBY KEY_DIR "/dpms_standby"
#define KEY_DPMS_SUSPEND KEY_DIR "/dpms_suspend"
#define KEY_DPMS_OFF     KEY_DIR "/dpms_off"
#define KEY_THEMES       KEY_DIR "/themes"

enum {
        NAME_COLUMN,
        LABEL_COLUMN,
        N_COLUMNS
};

static GConfEnumStringPair mode_enum_map [] = {
       { GS_MODE_BLANK_ONLY,       "blank-only" },
       { GS_MODE_RANDOM,           "random"     },
       { GS_MODE_SINGLE,           "single"     },
       { GS_MODE_DONT_BLANK,       "disabled"   },
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
config_get_blank_delay (gboolean *is_writable)
{
        GConfClient *client;
        gint32       delay;

        client = gconf_client_get_default ();

        if (is_writable) {
                *is_writable = gconf_client_key_is_writable (client,
                                                             KEY_BLANK_DELAY,
                                                             NULL);
        }

        delay = gconf_client_get_int (client, KEY_BLANK_DELAY, NULL);

        if (delay < 1)
                delay = 1;

        g_object_unref (client);

        return delay;
}

static void
config_set_blank_delay (gint32 timeout)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        gconf_client_set_int (client, KEY_BLANK_DELAY, timeout, NULL);

        g_object_unref (client);
}

static char *
config_get_theme (gboolean *is_writable)
{
        GConfClient *client;
        char        *string;
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

        string = gconf_client_get_string (client, KEY_MODE, NULL);
        if (string) {
                gconf_string_to_enum (mode_enum_map, string, &mode);
                g_free (string);
        } else
                mode = GS_MODE_BLANK_ONLY;

        if (mode == GS_MODE_BLANK_ONLY) {
                name = g_strdup ("__blank-only");
        } else if (mode == GS_MODE_DONT_BLANK) {
                name = g_strdup ("__disabled");
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

static void
config_set_theme (const char *name)
{
        GConfClient *client;
        GSList      *list = NULL;
        const char  *mode_string;
        int          mode;

        client = gconf_client_get_default ();

        if (name && strcmp (name, "__disabled") == 0) {
                mode = GS_MODE_DONT_BLANK;
        } else if (name && strcmp (name, "__blank-only") == 0) {
                mode = GS_MODE_BLANK_ONLY;
        } else {
                mode = GS_MODE_SINGLE;
                list = g_slist_append (list, g_strdup (name));
        }

        mode_string = gconf_enum_to_string (mode_enum_map, mode);
        gconf_client_set_string (client, KEY_MODE, mode_string, NULL);

        gconf_client_set_list (client,
                               KEY_THEMES,
                               GCONF_VALUE_STRING,
                               list,
                               NULL);

        g_slist_foreach (list, (GFunc)g_free, NULL);
        g_slist_free (list);

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
        GtkWidget *box   = glade_xml_get_widget (xml, "blank_delay_hbox");
        GError    *error = NULL;

        if (job) {
                gs_job_stop (job);
        }

        preview_clear (widget);

        if (theme && strcmp (theme, "__disabled") == 0) {
                gtk_widget_set_sensitive (box, FALSE);
        } else if (theme && strcmp (theme, "__blank-only") == 0) {
                gtk_widget_set_sensitive (box, TRUE);
        } else {
                gtk_widget_set_sensitive (box, TRUE);
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

        if (response_id == GTK_RESPONSE_HELP)
                return;

        gtk_widget_destroy (widget);
        gtk_main_quit ();
}

static GSList *
get_theme_list (void)
{
        return gs_job_get_theme_list (job);
}

static void
populate_model (GtkTreeStore *store)
{
        GtkTreeIter iter;
        gboolean    show_disabled = TRUE;
        GSList     *themes        = NULL;
        GSList     *l;

        if (show_disabled) {
                gtk_tree_store_append (store, &iter, NULL);
                gtk_tree_store_set (store, &iter,
                                    LABEL_COLUMN, "Disabled",
                                    NAME_COLUMN, "__disabled",
                                    -1);
        }

        gtk_tree_store_append (store, &iter, NULL);
        gtk_tree_store_set (store, &iter,
                            LABEL_COLUMN, "Blank screen",
                            NAME_COLUMN, "__blank-only",
                            -1);

        themes = get_theme_list ();

        if (! themes)
                return;
        
        for (l = themes; l; l = l->next) {
                GSJobThemeInfo *info;

                info = gs_job_lookup_theme_info (job,
                                                 (const char *) l->data);

                if (! info)
                        continue;

                gtk_tree_store_append (store, &iter, NULL);
                gtk_tree_store_set (store, &iter,
                                    NAME_COLUMN, info->name,
                                    LABEL_COLUMN, info->title,
                                    -1);

                gs_job_theme_info_free (info);
        }

        g_slist_foreach (themes, (GFunc)g_free, NULL);
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

        gtk_tree_model_get (model, &iter, NAME_COLUMN, &theme, -1);

        if (! theme)
                return;

        preview_set_theme (preview, theme);
        config_set_theme (theme);

        g_free (theme);
}

static void
blank_delay_value_changed_cb (GtkRange *range,
                              gpointer  user_data)
{
        gdouble value;

        value = gtk_range_get_value (range);
        config_set_blank_delay ((gint32)value);
}

static int
compare_theme  (GtkTreeModel *model,
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
        gtk_tree_model_get (model, a, LABEL_COLUMN, &label_a, -1);
        gtk_tree_model_get (model, b, LABEL_COLUMN, &label_b, -1);

        if (strcmp (name_a, "__disabled") == 0)
                return -1;
        else if (strcmp (name_b, "__disabled") == 0)
                return 1;
        else if (strcmp (name_a, "__blank-only") == 0)
                return -1;
        else if (strcmp (name_b, "__blank-only") == 0)
                return 1;

        result = strcmp (label_a, label_b);

        g_free (label_a);
        g_free (label_b);
        g_free (name_a);
        g_free (name_b);

        return result;
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
                                                           "text", LABEL_COLUMN,
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
                char *name;

                do {
                        gtk_tree_model_get (model, &iter,
                                            NAME_COLUMN, &name, -1);
                        if (name && strcmp (name, theme) == 0) {
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

        g_message ("Installing theme %s", filename);

        src_uri = gnome_vfs_uri_new (filename);
        src = g_list_append (NULL, src_uri);

        user_dir = g_build_filename (g_get_user_data_dir (), "gnome-screensaver", "themes", NULL);
        base = gnome_vfs_uri_extract_short_name (src_uri);

        g_message ("base: %s", base);

        target_path = NULL;

        while (TRUE) {
                char      *file_tmp;
                GtkWidget *dialog;
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
                g_message ("trying %s", target_path);

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

static void
init_capplet (void)
{
        GtkWidget *dialog;
        GtkWidget *preview;
        GtkWidget *treeview;
        GtkWidget *blank_delay_hscale;
        GtkWidget *blank_delay_hbox;
        GtkWidget *label;
        char      *glade_file;
        char      *path;
        gdouble    blank_delay;
        gboolean   is_writable;

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
        blank_delay_hscale = glade_xml_get_widget (xml, "blank_delay_hscale");
        blank_delay_hbox   = glade_xml_get_widget (xml, "blank_delay_hbox");

        label              = glade_xml_get_widget (xml, "blank_delay_label");
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), blank_delay_hscale);
        label              = glade_xml_get_widget (xml, "savers_label");
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), treeview);

        blank_delay = config_get_blank_delay (&is_writable);
        gtk_range_set_value (GTK_RANGE (blank_delay_hscale), blank_delay);
        if (! is_writable)
                gtk_widget_set_sensitive (blank_delay_hbox, FALSE);

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

        preview_clear (preview);
        gs_job_set_widget (job, preview);

        /* Add user configuration path */
        /* FIXME: disable this if locked down */
        path = g_build_filename (g_get_user_data_dir (), "gnome-screensaver", "themes", NULL);
        g_message ("Adding theme path: %s", path);
        gs_job_prepend_theme_path (job, path);
        g_free (path);

        setup_treeview (treeview, preview);
        setup_treeview_selection (treeview);

        g_signal_connect (blank_delay_hscale, "value-changed",
                          G_CALLBACK (blank_delay_value_changed_cb), NULL);

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

        gnome_program_init (PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
                            GNOME_PARAM_NONE);

        job = gs_job_new ();

        init_capplet ();

        gtk_main ();

        g_object_unref (job);

	return 0;
}
