/* copy-theme-dialog.c
 * Copyright (C) 2008 John Millikin <jmillikin@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
**/

#include "config.h"

#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "copy-theme-dialog.h"

static void
copy_theme_dialog_class_init (CopyThemeDialogClass *klass);
static void
copy_theme_dialog_init (CopyThemeDialog *dlg);
static void
add_file_to_dialog (gpointer data, gpointer user_data);
static void
single_copy_complete (GObject *source_object, GAsyncResult *res,
                      gpointer user_data);
static void
copy_theme_dialog_copy_next (CopyThemeDialog *dialog);
static void
copy_theme_dialog_cancel (CopyThemeDialog *dialog);
static void
copy_theme_dialog_finalize (GObject *obj);
static void
copy_theme_dialog_update_num_files (CopyThemeDialog *dlg);
static void
copy_theme_dialog_response (GtkDialog *dialog, gint response_id);
static void
eel_gtk_label_make_bold (GtkLabel *label);
static void
create_titled_label (GtkTable   *table,
                     int         row,
                     GtkWidget **title_widget,
                     GtkWidget **label_text_widget);

static GObjectClass *parent_class = NULL;

enum
{
	CANCELLED = 0,
	COMPLETE,
	SIGNAL_COUNT
};

struct _CopyThemeDialogPrivate
{
	GtkWidget *progress;
	GtkWidget *status;
	GtkWidget *current;
	GtkWidget *from;
	GtkWidget *to;

	GFile *theme_dir;
	GSList *all_files, *file;
	GSList *all_basenames, *basename;
	guint index;
	guint total_files;
	GCancellable *cancellable;
};

guint signals[SIGNAL_COUNT] = {0, 0};

GType
copy_theme_dialog_get_type (void)
{
	static GType copy_theme_dialog_type = 0;
	
	if (!copy_theme_dialog_type)
	{
		static GTypeInfo copy_theme_dialog_info =
		{
			sizeof (CopyThemeDialogClass),
			NULL, /* GBaseInitFunc */
			NULL, /* GBaseFinalizeFunc */
			(GClassInitFunc) copy_theme_dialog_class_init,
			NULL, /* GClassFinalizeFunc */
			NULL, /* data */
			sizeof (CopyThemeDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) copy_theme_dialog_init,
			NULL
		};
		
		copy_theme_dialog_type = g_type_register_static (GTK_TYPE_DIALOG,
		                                                 "CopyThemeDialog",
		                                                 &copy_theme_dialog_info,
		                                                 0);
	}
	
	return copy_theme_dialog_type;
}

static void
copy_theme_dialog_class_init (CopyThemeDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	g_type_class_add_private (klass, sizeof (CopyThemeDialogPrivate));
	
	klass->cancelled = copy_theme_dialog_cancel;
	object_class->finalize = copy_theme_dialog_finalize;
	
	GTK_DIALOG_CLASS (klass)->response = copy_theme_dialog_response;
	
	signals[CANCELLED] = g_signal_new ("cancelled",
	                                   G_TYPE_FROM_CLASS (object_class),
	                                   G_SIGNAL_RUN_FIRST,
	                                   G_STRUCT_OFFSET (CopyThemeDialogClass, cancelled),
	                                   NULL, NULL,
	                                   g_cclosure_marshal_VOID__VOID,
	                                   G_TYPE_NONE, 0);
	
	signals[COMPLETE] = g_signal_new ("complete",
	                                  G_TYPE_FROM_CLASS (object_class),
	                                  G_SIGNAL_RUN_LAST,
	                                  G_STRUCT_OFFSET (CopyThemeDialogClass, complete),
	                                  NULL, NULL,
	                                  g_cclosure_marshal_VOID__VOID,
	                                  G_TYPE_NONE, 0);
	
	parent_class = g_type_class_peek_parent (klass);
}

GtkWidget*
copy_theme_dialog_new (GList *files)
{
	GtkWidget *dialog;
	CopyThemeDialogPrivate *priv;
	
	dialog = GTK_WIDGET (g_object_new (COPY_THEME_DIALOG_TYPE, NULL));
	priv = COPY_THEME_DIALOG (dialog)->priv;
	priv->index = 0;
	priv->total_files = 0;
	priv->all_files = NULL;
	priv->all_basenames = NULL;
	
	g_list_foreach (files, add_file_to_dialog, dialog);
	
	priv->file = priv->all_files;
	priv->basename = priv->all_basenames;
	
	return dialog;
}

static gboolean
copy_finished (CopyThemeDialog *dialog)
{
	return (g_cancellable_is_cancelled (dialog->priv->cancellable) ||
	        dialog->priv->file == NULL);
}

static void
copy_theme_dialog_init (CopyThemeDialog *dlg)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *progress_vbox;
	GtkWidget *table;
	GtkWidget *label;
	char      *markup;
	gchar     *theme_dir_path;
	
	dlg->priv = G_TYPE_INSTANCE_GET_PRIVATE (dlg, COPY_THEME_DIALOG_TYPE,
	                                         CopyThemeDialogPrivate);
	
	/* Find and, if needed, create the directory for storing themes */
	theme_dir_path = g_build_filename (g_get_user_data_dir (),
	                                   "applications", "screensavers",
	                                   NULL);
	dlg->priv->theme_dir = g_file_new_for_path (theme_dir_path);
	g_mkdir_with_parents (theme_dir_path, S_IRWXU);
	g_free (theme_dir_path);
	
	/* For cancelling async I/O operations */
	dlg->priv->cancellable = g_cancellable_new ();
	
	/* GUI settings */
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox),
	                                4);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 4);
	
	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), vbox, TRUE, TRUE, 0);
	
	dlg->priv->status = gtk_label_new ("");
	markup = g_strdup_printf ("<big><b>%s</b></big>", _("Copying files"));
	gtk_label_set_markup (GTK_LABEL (dlg->priv->status), markup);
	g_free (markup);
	
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->status), 0.0, 0.0);
	
	gtk_box_pack_start (GTK_BOX (vbox), dlg->priv->status, FALSE, FALSE, 0);
	
	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
	
	table = gtk_table_new (2, 2, FALSE);
	gtk_table_set_row_spacings (GTK_TABLE (table), 4);
	gtk_table_set_col_spacings (GTK_TABLE (table), 4);
	
	create_titled_label (GTK_TABLE (table), 0,
	                     &label, 
	                     &dlg->priv->from);
	gtk_label_set_text (GTK_LABEL (label), _("From:"));
	create_titled_label (GTK_TABLE (table), 1,
	                     &label, 
	                     &dlg->priv->to);
	gtk_label_set_text (GTK_LABEL (label), _("To:"));
	
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (table), FALSE, FALSE, 0);
	
	progress_vbox = gtk_vbox_new (TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), progress_vbox, FALSE, FALSE, 0);
	
	dlg->priv->progress = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (progress_vbox),
	                    dlg->priv->progress, FALSE, FALSE, 0);
	
	dlg->priv->current = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (progress_vbox),
	                    dlg->priv->current, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->current), 0.0, 0.5);
	
	gtk_dialog_add_button (GTK_DIALOG (dlg),
	                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	
	gtk_window_set_title (GTK_WINDOW (dlg),
	                      _("Copying themes"));
	gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dlg), 6);
	
	gtk_widget_show_all (GTK_DIALOG (dlg)->vbox);
}

static void
add_file_to_dialog (gpointer data, gpointer user_data)
{
	CopyThemeDialogPrivate *priv;
	GFile *file;
	gchar *basename = NULL, *raw_basename;
	
	priv = COPY_THEME_DIALOG (user_data)->priv;
	file = G_FILE (data);
	
	raw_basename = g_file_get_basename (file);
	if (g_str_has_suffix (raw_basename, ".desktop"))
	{
		/* FIXME: validate key file? */
		basename = g_strndup (raw_basename,
		                      /* 8 = strlen (".desktop") */
		                      strlen (raw_basename) - 8);
	}
	g_free (raw_basename);
	
	if (basename)
	{
		g_object_ref (file);
		priv->all_files = g_slist_append (priv->all_files, file);
		priv->all_basenames = g_slist_append (priv->all_basenames, basename);
		priv->total_files++;
	}
	
	else
	{
		GtkWidget *dialog;
		gchar *uri;
		
		dialog = gtk_message_dialog_new (GTK_WINDOW (user_data),
		                                 GTK_DIALOG_MODAL,
		                                 GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_OK,
		                                 _("Invalid screensaver theme"));
		uri = g_file_get_uri (file);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		                                          _("%s does not appear to be a valid screensaver theme."),
		                                          uri);
		g_free (uri);
		gtk_window_set_title (GTK_WINDOW (dialog), "");
		gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-desktop-screensaver");
		
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
}

static void
single_copy_complete (GObject *source_object, GAsyncResult *res,
                      gpointer user_data)
{
	GError *error = NULL;
	gboolean should_continue = FALSE;
	CopyThemeDialog *dialog = COPY_THEME_DIALOG (user_data);
	
	if (g_file_copy_finish (G_FILE (source_object), res, &error))
	{
		should_continue = TRUE;
	}
	
	else
	{
		/* If the file already exists, generate a new random name
		 * and try again.
		**/
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
		{
			GFile *file, *destination;
			gchar *basename, *full_basename;
			g_error_free (error);
			
			file = G_FILE (dialog->priv->file->data);
			basename = (gchar *) (dialog->priv->basename->data);
			
			g_return_if_fail (file != NULL);
			g_return_if_fail (basename != NULL);
			
			full_basename = g_strdup_printf ("%s-%u.desktop",
			                                 basename,
			                                 g_random_int ());
			destination = g_file_get_child (dialog->priv->theme_dir,
			                                full_basename);
			g_free (full_basename);
			
			g_file_copy_async (file, destination, G_FILE_COPY_NONE,
			                   G_PRIORITY_DEFAULT,
			                   dialog->priv->cancellable,
			                   NULL, NULL,
			                   single_copy_complete, dialog);
		}
		
		else
		{
			if (g_error_matches (error, G_IO_ERROR,
			                          G_IO_ERROR_CANCELLED))
			{
				/* User has cancelled the theme copy */
				g_signal_emit (G_OBJECT (dialog),
				               signals[CANCELLED],
				               0, NULL);
			}
			
			else
			{
				/* Some other error occurred, ignore and 
				 * try to copy remaining files
				**/
				should_continue = TRUE;
			}
			
			g_error_free (error);
		}
	}
	
	/* Update informational widgets and, if needed, signal
	 * copy completion.
	**/
	if (should_continue)
	{
		dialog->priv->index++;
		dialog->priv->file = dialog->priv->file->next;
		dialog->priv->basename = dialog->priv->basename->next;
		copy_theme_dialog_update_num_files (dialog);
		copy_theme_dialog_copy_next (dialog);
	}
}

/* Try to copy the theme file to the user's screensaver directory.
 * If a file with the given name already exists, the error will be
 * caught later and the copy re-attempted with a random value
 * appended to the filename.
**/
static void
copy_theme_dialog_copy_next (CopyThemeDialog *dialog)
{
	GFile *file, *destination;
	gchar *basename, *full_basename;
	
	if (copy_finished (dialog))
	{
		g_signal_emit (G_OBJECT (dialog), signals[COMPLETE],
		               0, NULL);
		return;
	}
	
	file = G_FILE (dialog->priv->file->data);
	basename = (gchar *) (dialog->priv->basename->data);
	
	g_return_if_fail (file != NULL);
	g_return_if_fail (basename != NULL);
	
	full_basename = g_strdup_printf ("%s.desktop", basename);
	destination = g_file_get_child (dialog->priv->theme_dir, full_basename);
	g_free (full_basename);
	
	g_file_copy_async (file, destination, G_FILE_COPY_NONE,
	                   G_PRIORITY_DEFAULT, dialog->priv->cancellable,
	                   NULL, NULL, single_copy_complete, dialog);
}

static gboolean
timeout_display_dialog (gpointer data)
{
	if (IS_COPY_THEME_DIALOG (data))
	{
		CopyThemeDialog *dialog = COPY_THEME_DIALOG (data);
		if (!copy_finished (dialog))
		{
			gtk_widget_show (GTK_WIDGET (dialog));
			
			g_signal_connect (dialog, "response",
			                  G_CALLBACK (copy_theme_dialog_response),
			                  dialog);
		}
	}
	return FALSE;
}

void
copy_theme_dialog_begin (CopyThemeDialog *dialog)
{
	gtk_widget_hide (GTK_WIDGET (dialog));
	
	/* If the copy operation takes more than half a second to
	 * complete, display the dialog.
	**/
	g_timeout_add (500, timeout_display_dialog, dialog);
	
	copy_theme_dialog_copy_next (dialog);
}

static void
copy_theme_dialog_cancel (CopyThemeDialog *dialog)
{
	g_cancellable_cancel (dialog->priv->cancellable);
}

static void
copy_theme_dialog_finalize (GObject *obj)
{
	CopyThemeDialog *dlg = COPY_THEME_DIALOG (obj);
	
	g_object_unref (dlg->priv->theme_dir);
	g_slist_foreach (dlg->priv->all_files, (GFunc) (g_object_unref), NULL);
	g_slist_free (dlg->priv->all_files);
	g_slist_foreach (dlg->priv->all_basenames, (GFunc) (g_free), NULL);
	g_slist_free (dlg->priv->all_basenames);
	g_object_unref (dlg->priv->cancellable);
	
	if (parent_class->finalize)
		parent_class->finalize (G_OBJECT (dlg));
}

static void
copy_theme_dialog_update_num_files (CopyThemeDialog *dlg)
{
	gchar *str = g_strdup_printf (_("Copying file: %u of %u"),
				      dlg->priv->index, dlg->priv->total_files);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (dlg->priv->progress), str);
	g_free (str);
}

static void
copy_theme_dialog_response (GtkDialog *dialog, gint response_id)
{
	g_cancellable_cancel (COPY_THEME_DIALOG (dialog)->priv->cancellable);
}

/**
 * eel_gtk_label_make_bold.
 *
 * Switches the font of label to a bold equivalent.
 * @label: The label.
 **/
static void
eel_gtk_label_make_bold (GtkLabel *label)
{
	PangoFontDescription *font_desc;
	
	font_desc = pango_font_description_new ();
	
	pango_font_description_set_weight (font_desc,
	                                   PANGO_WEIGHT_BOLD);
	
	/* This will only affect the weight of the font, the rest is
	 * from the current state of the widget, which comes from the
	 * theme or user prefs, since the font desc only has the
	 * weight flag turned on.
	 */
	gtk_widget_modify_font (GTK_WIDGET (label), font_desc);
	
	pango_font_description_free (font_desc);
}

/* from nautilus */
static void
create_titled_label (GtkTable   *table,
                     int         row,
                     GtkWidget **title_widget,
                     GtkWidget **label_text_widget)
{
	*title_widget = gtk_label_new ("");
	eel_gtk_label_make_bold (GTK_LABEL (*title_widget));
	gtk_misc_set_alignment (GTK_MISC (*title_widget), 1, 0);
	gtk_table_attach (table, *title_widget,
	                  0, 1,
	                  row, row + 1,
	                  GTK_FILL, 0,
	                  0, 0);
	gtk_widget_show (*title_widget);
	
	*label_text_widget = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (*label_text_widget), PANGO_ELLIPSIZE_END);
	gtk_table_attach (table, *label_text_widget,
	                  1, 2,
	                  row, row + 1,
	                  GTK_FILL | GTK_EXPAND, 0,
	                  0, 0);
	gtk_widget_show (*label_text_widget);
	gtk_misc_set_alignment (GTK_MISC (*label_text_widget), 0, 0);
}
