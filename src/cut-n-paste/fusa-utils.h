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

#ifndef __FUSA_UTILS_H__
#define __FUSA_UTILS_H__ 1

#include <gtk/gtkwidget.h>

G_BEGIN_DECLS

void fusa_dialog_hig_fix           (GtkWidget   *dialog,
				    GtkStyle    *old_style,
				    gpointer     is_message_esque);
void fusa_die_an_ignominious_death (const gchar *primary_markup,
				    const gchar *secondary_markup,
				    const gchar *details_text);

G_END_DECLS

#endif /* !__FUSA_UTILS_H__ */
