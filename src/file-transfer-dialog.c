/* -*- mode: c; style: linux -*- */

/* file-transfer-dialog.c
 * Copyright (C) 2002 Ximian, Inc.
 *
 * Written by Rachel Hestilow <hestilow@ximian.com> 
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
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "file-transfer-dialog.h"
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnome/libgnome.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtkstock.h>
#include <limits.h>

enum
{
	PROP_0,
	PROP_FROM_URI,
	PROP_TO_URI,
	PROP_FRACTION_COMPLETE,
	PROP_NTH_URI,
	PROP_TOTAL_URIS
};

enum
{
	CANCEL,
	DONE,
	LAST_SIGNAL
};

guint file_transfer_dialog_signals[LAST_SIGNAL] = {0, };

struct _FileTransferDialogPrivate
{
	GtkWidget *progress; 
	GtkWidget *status;
	GtkWidget *num_files;
	GtkWidget *current;
	GtkWidget *from;
	GtkWidget *to;
	guint nth;
	guint total;
	GnomeVFSAsyncHandle *handle;
};

static GObjectClass *parent_class;

static void
file_transfer_dialog_cancel (FileTransferDialog *dlg)
{
	if (dlg->priv->handle)
	{
		gnome_vfs_async_cancel (dlg->priv->handle);
		dlg->priv->handle = NULL;
	}
}

static void
file_transfer_dialog_finalize (GObject *obj)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (obj);
		
	g_free (dlg->priv);

	if (parent_class->finalize)
		parent_class->finalize (G_OBJECT (dlg));
}

static void
file_transfer_dialog_update_num_files (FileTransferDialog *dlg)
{
	gchar *str = g_strdup_printf (_("%i of %i"),
				      dlg->priv->nth, dlg->priv->total);
	gtk_label_set_text (GTK_LABEL (dlg->priv->num_files), str);
	g_free (str);
}

static void
file_transfer_dialog_response (GtkDialog *dlg, gint response_id)
{
	g_signal_emit (G_OBJECT (dlg),
		       file_transfer_dialog_signals[CANCEL], 0, NULL); 
}

static void
file_transfer_dialog_set_prop (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (object);
	gchar *str;
	gchar *base;

	switch (prop_id)
	{
	case PROP_FROM_URI:
		base = g_path_get_basename (g_value_get_string (value));
		str = g_strdup_printf (_("Transferring: %s"), base);
		gtk_label_set_text (GTK_LABEL (dlg->priv->current),
				    str);
		g_free (base);
		g_free (str);
		
		base = g_path_get_dirname (g_value_get_string (value));
		str = g_strdup_printf (_("From: %s"), base);
		gtk_label_set_text (GTK_LABEL (dlg->priv->from),
				    str);
		g_free (base);
		g_free (str);
		break;
	case PROP_TO_URI:
		base = g_path_get_dirname (g_value_get_string (value));
		str = g_strdup_printf (_("To: %s"), base);
		gtk_label_set_text (GTK_LABEL (dlg->priv->to),
				    str);
		g_free (base);
		g_free (str);
		break;
	case PROP_FRACTION_COMPLETE:
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (dlg->priv->progress), g_value_get_double (value));
		break;
	case PROP_NTH_URI:
		dlg->priv->nth = g_value_get_uint (value);
		file_transfer_dialog_update_num_files (dlg);
		break;
	case PROP_TOTAL_URIS:
		dlg->priv->total = g_value_get_uint (value);
		file_transfer_dialog_update_num_files (dlg);
		break;
	}
}
	
static void
file_transfer_dialog_get_prop (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (object);
	
	switch (prop_id)
	{
	case PROP_NTH_URI:
		g_value_set_uint (value, dlg->priv->nth);
		break;
	case PROP_TOTAL_URIS:
		g_value_set_uint (value, dlg->priv->total);
		break;
	}
}

static void
file_transfer_dialog_class_init (FileTransferDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	klass->cancel = file_transfer_dialog_cancel;
	object_class->finalize = file_transfer_dialog_finalize;
	object_class->get_property = file_transfer_dialog_get_prop;
	object_class->set_property = file_transfer_dialog_set_prop;

	GTK_DIALOG_CLASS (klass)->response = file_transfer_dialog_response;

	g_object_class_install_property
		(object_class, PROP_FROM_URI,
		 g_param_spec_string ("from_uri",
				      _("From URI"),
				      _("URI currently transferring from"),
				      NULL,
				      G_PARAM_READWRITE));
	
	g_object_class_install_property
		(object_class, PROP_TO_URI,
		 g_param_spec_string ("to_uri",
				      _("To URI"),
				      _("URI currently transferring to"),
				      NULL,
				      G_PARAM_WRITABLE));

	g_object_class_install_property
		(object_class, PROP_FRACTION_COMPLETE,
		 g_param_spec_double ("fraction_complete",
				      _("Fraction completed"),
				      _("Fraction of transfer currently completed"),
				      0, 1, 0,
				      G_PARAM_WRITABLE));

	g_object_class_install_property
		(object_class, PROP_NTH_URI,
		 g_param_spec_uint ("nth_uri",
				    _("Current URI index"),
				    _("Current URI index - starts from 1"),
				    1, INT_MAX, 1,
				    G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_TOTAL_URIS,
		 g_param_spec_uint ("total_uris",
				    _("Total URIs"),
				    _("Total number of URIs"),
				    1, INT_MAX, 1,
				    G_PARAM_READWRITE));

	file_transfer_dialog_signals[CANCEL] =
		g_signal_new ("cancel",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (FileTransferDialogClass, cancel),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	file_transfer_dialog_signals[DONE] =
		g_signal_new ("done",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FileTransferDialogClass, done),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	parent_class = 
		G_OBJECT_CLASS (g_type_class_ref (GTK_TYPE_DIALOG));
}

static void
file_transfer_dialog_init (FileTransferDialog *dlg)
{
	GtkWidget *hbox;

	dlg->priv = g_new0 (FileTransferDialogPrivate, 1);
	
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox),
					4);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 4);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
			    hbox, FALSE, FALSE, 0);
	
	dlg->priv->status = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (dlg->priv->status),
			       GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->status), 0.0, 0.5);
	
	gtk_box_pack_start (GTK_BOX (hbox),
			    dlg->priv->status, TRUE, TRUE, 0);
	
	dlg->priv->num_files = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (dlg->priv->num_files),
			       GTK_JUSTIFY_RIGHT);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->num_files), 1.0, 0.5);

	gtk_box_pack_start (GTK_BOX (hbox),
			    dlg->priv->num_files, TRUE, TRUE, 0);

	dlg->priv->progress = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
			    dlg->priv->progress, FALSE, FALSE, 0);

	dlg->priv->current = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
			    dlg->priv->current, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->current), 0.0, 0.5);

	dlg->priv->from = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
			    dlg->priv->from, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->from), 0.0, 0.5);

	dlg->priv->to = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox),
			    dlg->priv->to, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->to), 0.0, 0.5);

	gtk_dialog_add_button (GTK_DIALOG (dlg),
			       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	
	gtk_widget_show_all (GTK_DIALOG (dlg)->vbox);
}
	
GType
file_transfer_dialog_get_type (void)
{
	static GType file_transfer_dialog_type = 0;

	if (!file_transfer_dialog_type)
	{
		static GTypeInfo file_transfer_dialog_info =
		{
			sizeof (FileTransferDialogClass),
			NULL, /* GBaseInitFunc */
			NULL, /* GBaseFinalizeFunc */
			(GClassInitFunc) file_transfer_dialog_class_init,
			NULL, /* GClassFinalizeFunc */
			NULL, /* data */
			sizeof (FileTransferDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) file_transfer_dialog_init,
			NULL
		};

		file_transfer_dialog_type =
			g_type_register_static (GTK_TYPE_DIALOG,
					 	"FileTransferDialog",
						&file_transfer_dialog_info,
						0);
	}

	return file_transfer_dialog_type;
}

GtkWidget*
file_transfer_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (file_transfer_dialog_get_type (),
					 NULL));
}

static int
file_transfer_dialog_update_cb (GnomeVFSAsyncHandle *handle,
				GnomeVFSXferProgressInfo *info,
				gpointer data)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (data);

	if (info->status == GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR)
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
		
	if (info->source_name)
		g_object_set (G_OBJECT (dlg),
			      "from_uri", info->source_name,
			      NULL);
	if (info->target_name)
		g_object_set (G_OBJECT (dlg),
			      "to_uri", info->target_name,
			      NULL);

	if (info->bytes_total)
		g_object_set (G_OBJECT (dlg),
			      "fraction_complete", (double) info->total_bytes_copied / (double) info->bytes_total,
			      NULL);

	if (info->file_index && info->files_total)
		g_object_set (G_OBJECT (dlg),
			      "nth_uri", info->file_index,
			      "total_uris", info->files_total,
			      NULL);
	
	switch (info->phase)
	{
	case GNOME_VFS_XFER_PHASE_INITIAL:
		gtk_label_set_text (GTK_LABEL (dlg->priv->status),
				    _("Connecting..."));
		gtk_window_set_title (GTK_WINDOW (dlg),
				    _("Connecting..."));
		break;
	case GNOME_VFS_XFER_PHASE_READYTOGO:
	case GNOME_VFS_XFER_PHASE_OPENSOURCE:
		gtk_label_set_text (GTK_LABEL (dlg->priv->status),
				    _("Downloading..."));
		gtk_window_set_title (GTK_WINDOW (dlg),
				    _("Downloading..."));
		break;
	case GNOME_VFS_XFER_PHASE_COMPLETED:
		g_signal_emit (G_OBJECT (dlg),
			       file_transfer_dialog_signals[DONE],
			       0, NULL);
		return 0;
	default:
		break;
	}

	return 1;
}

GnomeVFSResult
file_transfer_dialog_wrap_async_xfer (FileTransferDialog *dlg,
				      GList *source_uri_list,
				      GList *target_uri_list,
				      GnomeVFSXferOptions xfer_options,
				      GnomeVFSXferErrorMode error_mode,
				      GnomeVFSXferOverwriteMode overwrite_mode,
				      int priority)
{
	g_return_val_if_fail (IS_FILE_TRANSFER_DIALOG (dlg),
			      GNOME_VFS_ERROR_BAD_PARAMETERS);

	return gnome_vfs_async_xfer (&dlg->priv->handle,
				     source_uri_list,
				     target_uri_list,
				     xfer_options,
				     error_mode,
				     overwrite_mode,
				     priority,
				     file_transfer_dialog_update_cb,
				     dlg,
				     NULL,
				     NULL
				   );
}

