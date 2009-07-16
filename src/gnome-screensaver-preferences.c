/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>          /* For uid_t, gid_t */

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <gio/gio.h>

#include "copy-theme-dialog.h"

#include "gs-theme-manager.h"
#include "gs-job.h"
#include "gs-prefs.h" /* for GS_MODE enum */

#define GTK_BUILDER_FILE "gnome-screensaver-preferences.ui"

#define GNOME_LOCKDOWN_DIR  "/desktop/gnome/lockdown"
#define KEY_LOCK_DISABLE    GNOME_LOCKDOWN_DIR "/disable_lock_screen"

#define KEY_DIR             "/apps/gnome-screensaver"
#define GNOME_SESSION_DIR   "/desktop/gnome/session"
#define KEY_LOCK            KEY_DIR "/lock_enabled"
#define KEY_IDLE_ACTIVATION_ENABLED         KEY_DIR "/idle_activation_enabled"
#define KEY_MODE            KEY_DIR "/mode"
#define KEY_ACTIVATE_DELAY  GNOME_SESSION_DIR "/idle_delay"
#define KEY_LOCK_DELAY      KEY_DIR "/lock_delay"
#define KEY_CYCLE_DELAY     KEY_DIR "/cycle_delay"
#define KEY_THEMES          KEY_DIR "/themes"

#define GPM_COMMAND "gnome-power-preferences"

enum {
        NAME_COLUMN = 0,
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

static GtkBuilder       *builder = NULL;
static GSThemeManager *theme_manager = NULL;
static GSJob          *job = NULL;

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

        if (delay < 1) {
                delay = 1;
        }

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
        char        *name;
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

        name = NULL;
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
                if (list != NULL) {
                        name = g_strdup (list->data);
                } else {
                        /* TODO: handle error */
                        /* default to blank */
                        name = g_strdup ("__blank-only");
                }

                g_slist_foreach (list, (GFunc)g_free, NULL);
                g_slist_free (list);
        }

        g_object_unref (client);

        return name;
}

static GSList *
get_all_theme_ids (GSThemeManager *theme_manager)
{
        GSList *ids = NULL;
        GSList *entries;
        GSList *l;

        entries = gs_theme_manager_get_info_list (theme_manager);
        for (l = entries; l; l = l->next) {
                GSThemeInfo *info = l->data;

                ids = g_slist_prepend (ids, g_strdup (gs_theme_info_get_id (info)));
                gs_theme_info_unref (info);
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
                list = get_all_theme_ids (theme_manager);
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

static gboolean
config_get_lock_disabled ()
{
        GConfClient *client;
        gboolean     lock;

        client = gconf_client_get_default ();

        lock = gconf_client_get_bool (client, KEY_LOCK_DISABLE, NULL);

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
job_set_theme (GSJob      *job,
               const char *theme)
{
        GSThemeInfo *info;
        const char  *command;

        command = NULL;

        info = gs_theme_manager_lookup_theme_info (theme_manager, theme);
        if (info != NULL) {
                command = gs_theme_info_get_exec (info);
        }

        gs_job_set_command (job, command);

        if (info != NULL) {
                gs_theme_info_unref (info);
        }
}

static void
preview_set_theme (GtkWidget  *widget,
                   const char *theme,
                   const char *name)
{
        GtkWidget *label;
        char      *markup;

        if (job != NULL) {
                gs_job_stop (job);
        }

        preview_clear (widget);

        label = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_theme_label"));
        markup = g_markup_printf_escaped ("<i>%s</i>", name);
        gtk_label_set_markup (GTK_LABEL (label), markup);
        g_free (markup);

        if ((theme && strcmp (theme, "__blank-only") == 0)) {

        } else if (theme && strcmp (theme, "__random") == 0) {
                GSList *themes;

                themes = get_all_theme_ids (theme_manager);
                if (themes != NULL) {
                        GSList *l;
                        gint32  i;

                        i = g_random_int_range (0, g_slist_length (themes));
                        l = g_slist_nth (themes, i);

                        job_set_theme (job, (const char *) l->data);
                        g_slist_foreach (themes, (GFunc) g_free, NULL);
                        g_slist_free (themes);

                        gs_job_start (job);
                }
        } else {
                job_set_theme (job, theme);
                gs_job_start (job);
        }
}

static void
help_display (void)
{
        GError     *error = NULL;
        char       *command;
        const char *lang;
        char       *uri = NULL;
        GdkScreen  *gscreen;
        int         i;

        const char * const * langs = g_get_language_names ();

        for (i = 0; langs[i] != NULL; i++) {
                lang = langs[i];
                if (strchr (lang, '.')) {
                        continue;
                }

                uri = g_build_filename (DATADIR,
                                        "/gnome/help/user-guide/",
                                        lang,
                                        "/user-guide.xml",
                                        NULL);

                if (g_file_test (uri, G_FILE_TEST_EXISTS)) {
                    break;
                }
        }

        command = g_strconcat ("gnome-open ghelp://",
                               uri,
                               "?prefs-screensaver",
                               NULL);
        gscreen = gdk_screen_get_default ();
        gdk_spawn_command_line_on_screen (gscreen, command, &error);

        if (error != NULL) {
                GtkWidget *d;

                d = gtk_message_dialog_new (NULL,
                                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                            "%s", error->message);
                gtk_dialog_run (GTK_DIALOG (d));
                gtk_widget_destroy (d);
                g_error_free (error);
        }

        g_free (command);
        g_free (uri);
}

static void
response_cb (GtkWidget *widget,
             int        response_id)
{

        if (response_id == GTK_RESPONSE_HELP) {
                help_display ();
        } else if (response_id == GTK_RESPONSE_REJECT) {
                GError  *error;
                gboolean res;

                error = NULL;

                res = gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
                                                        GPM_COMMAND,
                                                        &error);
                if (! res) {
                        g_warning ("Unable to start power management preferences: %s", error->message);
                        g_error_free (error);
                }
        } else {
                gtk_widget_destroy (widget);
                gtk_main_quit ();
        }
}

static GSList *
get_theme_info_list (void)
{
        return gs_theme_manager_get_info_list (theme_manager);
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

        if (themes == NULL) {
                return;
        }

        for (l = themes; l; l = l->next) {
                const char  *name;
                const char  *id;
                GSThemeInfo *info = l->data;

                if (info == NULL) {
                        continue;
                }

                name = gs_theme_info_get_name (info);
                id = gs_theme_info_get_id (info);

                gtk_tree_store_append (store, &iter, NULL);
                gtk_tree_store_set (store, &iter,
                                    NAME_COLUMN, name,
                                    ID_COLUMN, id,
                                    -1);

                gs_theme_info_unref (info);
        }

        g_slist_free (themes);
}

static void
tree_selection_previous (GtkTreeSelection *selection)
{
        GtkTreeIter   iter;
        GtkTreeModel *model;
        GtkTreePath  *path;

        if (! gtk_tree_selection_get_selected (selection, &model, &iter)) {
                return;
        }

        path = gtk_tree_model_get_path (model, &iter);
        if (gtk_tree_path_prev (path)) {
                gtk_tree_selection_select_path (selection, path);
        }
}

static void
tree_selection_next (GtkTreeSelection *selection)
{
        GtkTreeIter   iter;
        GtkTreeModel *model;
        GtkTreePath  *path;

        if (! gtk_tree_selection_get_selected (selection, &model, &iter)) {
                return;
        }

        path = gtk_tree_model_get_path (model, &iter);
        gtk_tree_path_next (path);
        gtk_tree_selection_select_path (selection, path);
}

static void
tree_selection_changed_cb (GtkTreeSelection *selection,
                           GtkWidget        *preview)
{
        GtkTreeIter   iter;
        GtkTreeModel *model;
        char         *theme;
        char         *name;

        if (! gtk_tree_selection_get_selected (selection, &model, &iter)) {
                return;
        }

        gtk_tree_model_get (model, &iter, ID_COLUMN, &theme, NAME_COLUMN, &name, -1);

        if (theme == NULL) {
                g_free (name);
                return;
        }

        preview_set_theme (preview, theme, name);
        config_set_theme (theme);

        g_free (theme);
        g_free (name);
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
compare_theme_names (char *name_a,
                     char *name_b,
                     char *id_a,
                     char *id_b)
{

        if (id_a == NULL) {
                return 1;
        } else if (id_b == NULL) {
                return -1;
        }

        if (strcmp (id_a, "__blank-only") == 0) {
                return -1;
        } else if (strcmp (id_b, "__blank-only") == 0) {
                return 1;
        } else if (strcmp (id_a, "__random") == 0) {
                return -1;
        } else if (strcmp (id_b, "__random") == 0) {
                return 1;
        } else if (strcmp (id_a, "__separator") == 0) {
                return -1;
        } else if (strcmp (id_b, "__separator") == 0) {
                return 1;
        }

        if (name_a == NULL) {
                return 1;
        } else if (name_b == NULL) {
                return -1;
        }

        return g_utf8_collate (name_a, name_b);
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

        result = compare_theme_names (name_a, name_b, id_a, id_b);

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

        if (text != NULL && strcmp (text, "__separator") == 0) {
                return TRUE;
        }

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

#if GTK_CHECK_VERSION(2,10,0)
        g_object_set (tree, "show-expanders", FALSE, NULL);
#endif

        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
                                                           "text", NAME_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

        gtk_tree_view_column_set_sort_column_id (column, NAME_COLUMN);
        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
                                         NAME_COLUMN,
                                         compare_theme,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                              NAME_COLUMN,
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

        if (! is_writable) {
                gtk_widget_set_sensitive (tree, FALSE);
        }

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

        if (theme && gtk_tree_model_get_iter_first (model, &iter)) {

                do {
                        char *id;
                        gboolean found;

                        gtk_tree_model_get (model, &iter,
                                            ID_COLUMN, &id, -1);
                        found = (id && strcmp (id, theme) == 0);
                        g_free (id);

                        if (found) {
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

        treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
        gtk_tree_store_clear (GTK_TREE_STORE (model));
        populate_model (GTK_TREE_STORE (model));

        gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
                                 GTK_TREE_MODEL (model));
}

static void
theme_copy_complete_cb (GtkWidget *dialog, gpointer user_data)
{
        reload_themes ();
        gtk_widget_destroy (dialog);
}

static void
theme_installer_run (GtkWidget *prefs_dialog, GList *files)
{
        GtkWidget *copy_dialog;

        copy_dialog = copy_theme_dialog_new (files);
        g_list_foreach (files, (GFunc) (g_object_unref), NULL);
        g_list_free (files);

        gtk_window_set_transient_for (GTK_WINDOW (copy_dialog),
                                        GTK_WINDOW (prefs_dialog));
        gtk_window_set_icon_name (GTK_WINDOW (copy_dialog),
                                        "preferences-desktop-screensaver");

        g_signal_connect (copy_dialog, "complete",
                          G_CALLBACK (theme_copy_complete_cb), NULL);

        copy_theme_dialog_begin (COPY_THEME_DIALOG (copy_dialog));
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

/* GIO has no version of gnome_vfs_uri_list_parse(), so copy from GnomeVFS
 * and re-work to create GFiles.
**/
static GList *
uri_list_parse (const gchar *uri_list)
{
        const gchar *p, *q;
        gchar *retval;
        GFile *file;
        GList *result = NULL;

        g_return_val_if_fail (uri_list != NULL, NULL);

        p = uri_list;

        /* We don't actually try to validate the URI according to RFC
         * 2396, or even check for allowed characters - we just ignore
         * comments and trim whitespace off the ends.  We also
         * allow LF delimination as well as the specified CRLF.
         */
        while (p != NULL) {
                if (*p != '#') {
                        while (g_ascii_isspace (*p))
                                p++;

                        q = p;
                        while ((*q != '\0')
                               && (*q != '\n')
                               && (*q != '\r'))
                                q++;

                        if (q > p) {
                                q--;
                                while (q > p
                                       && g_ascii_isspace (*q))
                                        q--;

                                retval = g_malloc (q - p + 2);
                                strncpy (retval, p, q - p + 1);
                                retval[q - p + 1] = '\0';

                                file = g_file_new_for_uri (retval);

                                g_free (retval);

                                if (file != NULL)
                                        result = g_list_prepend (result, file);
                        }
                }
                p = strchr (p, '\n');
                if (p != NULL)
                        p++;
        }

        return g_list_reverse (result);
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
        GList     *files;

        if (!(info == TARGET_URI_LIST || info == TARGET_NS_URL))
                return;

        files = uri_list_parse ((char *) selection_data->data);
        if (files != NULL) {
                GtkWidget *prefs_dialog;

                prefs_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
                theme_installer_run (prefs_dialog, files);
        }
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
        } else {
                /* seconds */
                string = g_strdup_printf (_("%s"), secs);
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
ui_disable_lock (gboolean disable)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
        gtk_widget_set_sensitive (widget, !disable);
        if (disable) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
        }
}

static void
ui_set_lock (gboolean enabled)
{
        GtkWidget *widget;
        gboolean   active;
        gboolean   lock_disabled;

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));

        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }
        lock_disabled = config_get_lock_disabled ();
        ui_disable_lock (lock_disabled);
}

static void
ui_set_enabled (gboolean enabled)
{
        GtkWidget *widget;
        gboolean   active;
        gboolean   is_writable;
        gboolean   lock_disabled;

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "enable_checkbox"));
        active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
        if (active != enabled) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
        }

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
        config_get_lock (&is_writable);
        if (is_writable) {
                gtk_widget_set_sensitive (widget, enabled);
        }
        lock_disabled = config_get_lock_disabled ();
        ui_disable_lock(lock_disabled);
}

static void
ui_set_delay (int delay)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "activate_delay_hscale"));
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

        if (! g_str_has_prefix (key, KEY_DIR) && ! g_str_has_prefix (key, GNOME_LOCKDOWN_DIR)) {
                return;
        }

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
        } else if (strcmp (key, KEY_LOCK_DISABLE) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean disabled;

                        disabled = gconf_value_get_bool (value);

                        ui_disable_lock (disabled);
                } else {
                        invalid_type_warning (key);
                }
        } else if (strcmp (key, KEY_THEMES) == 0) {
                if (value->type == GCONF_VALUE_LIST) {
                        GtkWidget *treeview;

                        treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
                        setup_treeview_selection (treeview);
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
fullscreen_preview_previous_cb (GtkWidget *fullscreen_preview_window,
                                gpointer   user_data)
{
        GtkWidget        *treeview;
        GtkTreeSelection *selection;

        treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
        tree_selection_previous (selection);
}

static void
fullscreen_preview_next_cb (GtkWidget *fullscreen_preview_window,
                            gpointer   user_data)
{
        GtkWidget        *treeview;
        GtkTreeSelection *selection;

        treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
        tree_selection_next (selection);
}

static void
fullscreen_preview_cancelled_cb (GtkWidget *button,
                                 gpointer   user_data)
{

        GtkWidget *fullscreen_preview_area;
        GtkWidget *fullscreen_preview_window;
        GtkWidget *preview_area;
        GtkWidget *dialog;

        preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "preview_area"));
        gs_job_set_widget (job, preview_area);

        fullscreen_preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_area"));
        preview_clear (fullscreen_preview_area);

        fullscreen_preview_window = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_window"));
        gtk_widget_hide (fullscreen_preview_window);

        dialog = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
        gtk_widget_show (dialog);
        gtk_window_present (GTK_WINDOW (dialog));
}

static void
fullscreen_preview_start_cb (GtkWidget *widget,
                             gpointer   user_data)
{
        GtkWidget *fullscreen_preview_area;
        GtkWidget *fullscreen_preview_window;
        GtkWidget *dialog;

        dialog = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
        gtk_widget_hide (dialog);

        fullscreen_preview_window = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_window"));

        gtk_window_fullscreen (GTK_WINDOW (fullscreen_preview_window));
        gtk_window_set_keep_above (GTK_WINDOW (fullscreen_preview_window), TRUE);

        gtk_widget_show (fullscreen_preview_window);
        gtk_widget_grab_focus (fullscreen_preview_window);

        fullscreen_preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_area"));
        preview_clear (fullscreen_preview_area);
        gs_job_set_widget (job, fullscreen_preview_area);
}

static void
constrain_list_size (GtkWidget      *widget,
                     GtkRequisition *requisition,
                     GtkWidget      *to_size)
{
        GtkRequisition req;
        int            max_height;

        /* constrain height to be the tree height up to a max */
        max_height = (gdk_screen_get_height (gtk_widget_get_screen (widget))) / 4;

        gtk_widget_size_request (to_size, &req);

        requisition->height = MIN (req.height, max_height);
}

static void
setup_list_size_constraint (GtkWidget *widget,
                            GtkWidget *to_size)
{
        g_signal_connect (widget, "size-request",
                          G_CALLBACK (constrain_list_size), to_size);
}

static gboolean
check_is_root_user (void)
{
#ifndef G_OS_WIN32
  uid_t ruid, euid, suid; /* Real, effective and saved user ID's */
  gid_t rgid, egid, sgid; /* Real, effective and saved group ID's */

#ifdef HAVE_GETRESUID
  if (getresuid (&ruid, &euid, &suid) != 0 ||
      getresgid (&rgid, &egid, &sgid) != 0)
#endif /* HAVE_GETRESUID */
    {
      suid = ruid = getuid ();
      sgid = rgid = getgid ();
      euid = geteuid ();
      egid = getegid ();
    }

  if (ruid == 0) {
          return TRUE;
  }

#endif
  return FALSE;
}

static void
setup_for_root_user (void)
{
        GtkWidget *lock_checkbox;
        GtkWidget *label;

        lock_checkbox = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
        label = GTK_WIDGET (gtk_builder_get_object (builder, "root_warning_label"));

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lock_checkbox), FALSE);
        gtk_widget_set_sensitive (lock_checkbox, FALSE);

        gtk_widget_show (label);
}

static GdkVisual *
get_best_visual (void)
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
        res = g_spawn_command_line_sync (command,
                                         &std_output,
                                         NULL,
                                         &exit_status,
                                         &error);

        if (! res) {
                g_debug ("Could not run command '%s': %s", command, error->message);
                g_error_free (error);
                goto out;
        }

        if (1 == sscanf (std_output, "0x%lx %c", &v, &c)) {
                if (v != 0) {
                        VisualID      visual_id;

                        visual_id = (VisualID) v;
                        visual = gdkx_visual_get (visual_id);

                        g_debug ("Found best visual for GL: 0x%x",
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

        visual = get_best_visual ();

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

static gboolean
setup_treeview_idle (gpointer data)
{
        GtkWidget *preview;
        GtkWidget *treeview;

        preview  = GTK_WIDGET (gtk_builder_get_object (builder, "preview_area"));
        treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));

        setup_treeview (treeview, preview);
        setup_treeview_selection (treeview);

        return FALSE;
}

static gboolean
is_program_in_path (const char *program)
{
        char *tmp = g_find_program_in_path (program);
        if (tmp != NULL) {
                g_free (tmp);
                return TRUE;
        } else {
                return FALSE;
        }
}

static void
init_capplet (void)
{
        GtkWidget *dialog;
        GtkWidget *preview;
        GtkWidget *treeview;
        GtkWidget *list_scroller;
        GtkWidget *activate_delay_hscale;
        GtkWidget *activate_delay_hbox;
        GtkWidget *label;
        GtkWidget *enabled_checkbox;
        GtkWidget *lock_checkbox;
        GtkWidget *root_warning_label;
        GtkWidget *preview_button;
        GtkWidget *gpm_button;
        GtkWidget *fullscreen_preview_window;
        GtkWidget *fullscreen_preview_previous;
        GtkWidget *fullscreen_preview_next;
        GtkWidget *fullscreen_preview_area;
        GtkWidget *fullscreen_preview_close;
        char      *gtk_builder_file;
        char      *string;
        gdouble    activate_delay;
        gboolean   enabled;
        gboolean   is_writable;
        GConfClient *client;
        GError    *error=NULL;

        gtk_builder_file = g_build_filename (GTKBUILDERDIR, GTK_BUILDER_FILE, NULL);
        builder = gtk_builder_new();
        if (!gtk_builder_add_from_file(builder, gtk_builder_file, &error)) {
                g_warning("Couldn't load builder file: %s", error->message);
                g_error_free(error);
        }
        g_free (gtk_builder_file);

        if (builder == NULL) {

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

        preview            = GTK_WIDGET (gtk_builder_get_object (builder, "preview_area"));
        dialog             = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
        treeview           = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
        list_scroller      = GTK_WIDGET (gtk_builder_get_object (builder, "themes_scrolled_window"));
        activate_delay_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "activate_delay_hscale"));
        activate_delay_hbox   = GTK_WIDGET (gtk_builder_get_object (builder, "activate_delay_hbox"));
        enabled_checkbox   = GTK_WIDGET (gtk_builder_get_object (builder, "enable_checkbox"));
        lock_checkbox      = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
        root_warning_label = GTK_WIDGET (gtk_builder_get_object (builder, "root_warning_label"));
        preview_button     = GTK_WIDGET (gtk_builder_get_object (builder, "preview_button"));
        gpm_button         = GTK_WIDGET (gtk_builder_get_object (builder, "gpm_button"));
        fullscreen_preview_window = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_window"));
        fullscreen_preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_area"));
        fullscreen_preview_close = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_close"));
        fullscreen_preview_previous = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_previous_button"));
        fullscreen_preview_next = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_next_button"));

        label              = GTK_WIDGET (gtk_builder_get_object (builder, "activate_delay_label"));
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), activate_delay_hscale);
        label              = GTK_WIDGET (gtk_builder_get_object (builder, "savers_label"));
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), treeview);

        gtk_widget_set_no_show_all (root_warning_label, TRUE);
        widget_set_best_colormap (preview);

        if (! is_program_in_path (GPM_COMMAND)) {
                gtk_widget_set_no_show_all (gpm_button, TRUE);
                gtk_widget_hide (gpm_button);
        }

        activate_delay = config_get_activate_delay (&is_writable);
        ui_set_delay (activate_delay);
        if (! is_writable) {
                gtk_widget_set_sensitive (activate_delay_hbox, FALSE);
        }
        g_signal_connect (activate_delay_hscale, "format-value",
                          G_CALLBACK (format_value_callback_time), NULL);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lock_checkbox), config_get_lock (&is_writable));
        if (! is_writable) {
                gtk_widget_set_sensitive (lock_checkbox, FALSE);
        }
        g_signal_connect (lock_checkbox, "toggled",
                          G_CALLBACK (lock_checkbox_toggled), NULL);

        enabled = config_get_enabled (&is_writable);
        ui_set_enabled (enabled);
        if (! is_writable) {
                gtk_widget_set_sensitive (enabled_checkbox, FALSE);
        }
        g_signal_connect (enabled_checkbox, "toggled",
                          G_CALLBACK (enabled_checkbox_toggled), NULL);

        setup_list_size_constraint (list_scroller, treeview);
        gtk_widget_set_size_request (preview, 480, 300);
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-desktop-screensaver");
        gtk_window_set_icon_name (GTK_WINDOW (fullscreen_preview_window), "screensaver");

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
        if (string != NULL) {
                int mode;
                GSList *list;

                gconf_string_to_enum (mode_enum_map, string, &mode);
                g_free (string);

                if (mode == GS_MODE_RANDOM) {
                        list = get_all_theme_ids (theme_manager);
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
        gconf_client_add_dir (client, GNOME_LOCKDOWN_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        gconf_client_notify_add (client,
                                 GNOME_LOCKDOWN_DIR,
                                 key_changed_cb,
                                 NULL, NULL, NULL);

        g_object_unref (client);

        preview_clear (preview);
        gs_job_set_widget (job, preview);

        if (check_is_root_user ()) {
                setup_for_root_user ();
        }

        g_signal_connect (activate_delay_hscale, "value-changed",
                          G_CALLBACK (activate_delay_value_changed_cb), NULL);

        g_signal_connect (dialog, "response",
                          G_CALLBACK (response_cb), NULL);

        g_signal_connect (preview_button, "clicked",
                          G_CALLBACK (fullscreen_preview_start_cb),
                          treeview);

        g_signal_connect (fullscreen_preview_close, "clicked", 
                          G_CALLBACK (fullscreen_preview_cancelled_cb), NULL);
        g_signal_connect (fullscreen_preview_previous, "clicked", 
                          G_CALLBACK (fullscreen_preview_previous_cb), NULL);
        g_signal_connect (fullscreen_preview_next, "clicked", 
                          G_CALLBACK (fullscreen_preview_next_cb), NULL);

        g_idle_add ((GSourceFunc)setup_treeview_idle, NULL);
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

        gtk_init (&argc, &argv);

        job = gs_job_new ();
        theme_manager = gs_theme_manager_new ();

        init_capplet ();

        gtk_main ();

        g_object_unref (theme_manager);
        g_object_unref (job);

        return 0;
}
