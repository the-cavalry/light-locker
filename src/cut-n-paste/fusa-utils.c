/* 
 * Fast User Switch Applet: fusa-vt-manager.h
 * 
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <glib/gi18n.h>

#include <gtk/gtkalignment.h>
#include <gtk/gtkexpander.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>

#include "fusa-utils.h"


void
fusa_dialog_hig_fix (GtkWidget *dialog,
		     GtkStyle  *old_style,
		     gpointer   is_message_esque)
{
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area),
				  0);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);
  gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 0);

  if (is_message_esque)
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 24);
  else
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 12);
}

void
fusa_die_an_ignominious_death (const gchar *primary_markup,
			       const gchar *secondary_markup,
			       const gchar *details_text)
{
  GtkWidget *dialog;

  GDK_THREADS_ENTER ();

  dialog = gtk_message_dialog_new (NULL, 0,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_NONE,
				   primary_markup);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
					      secondary_markup);
  gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_QUIT,
				     GTK_RESPONSE_CLOSE);

  if (details_text)
    {
      GtkWidget *expander, *alignment, *label;
    
      alignment = gtk_alignment_new (0.0, 0.0, 1.0, 1.0);
      gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 12, 12, 0, 12);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			  alignment, TRUE, TRUE, 0);
      gtk_widget_show (alignment);

      expander = gtk_expander_new (_("Show Details"));
      gtk_container_add (GTK_CONTAINER (alignment), expander);
      gtk_widget_show (expander);

      alignment = gtk_alignment_new (0.0, 0.0, 1.0, 1.0);
      gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 6, 0, 12, 0);
      gtk_container_add (GTK_CONTAINER (expander), alignment);
      gtk_widget_show (alignment);

      label = gtk_label_new (details_text);
      gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
      gtk_container_add (GTK_CONTAINER (alignment), label);
      gtk_widget_show (label);
    }

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_main_quit ();

  GDK_THREADS_LEAVE ();
}
