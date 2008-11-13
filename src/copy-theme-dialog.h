/* copy-theme-dialog.h
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

#ifndef __COPY_THEME_DIALOG_H__
#define __COPY_THEME_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define COPY_THEME_DIALOG_TYPE copy_theme_dialog_get_type ()
#define COPY_THEME_DIALOG(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, COPY_THEME_DIALOG_TYPE, CopyThemeDialog)
#define COPY_THEME_DIALOG_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, COPY_THEME_DIALOG_TYPE, CopyThemeDialogClass)
#define IS_COPY_THEME_DIALOG(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, COPY_THEME_DIALOG_TYPE)

typedef struct _CopyThemeDialog CopyThemeDialog;
typedef struct _CopyThemeDialogClass CopyThemeDialogClass;
typedef struct _CopyThemeDialogPrivate CopyThemeDialogPrivate;

struct _CopyThemeDialog
{
	GtkDialog dialog;
	CopyThemeDialogPrivate *priv;
};

struct _CopyThemeDialogClass
{
	GtkDialogClass parent_class;
	
	void (*cancelled) (CopyThemeDialog *dialog);
	void (*complete) (CopyThemeDialog *dialog);
};

GType copy_theme_dialog_get_type (void);
GtkWidget *copy_theme_dialog_new (GList *files);
void copy_theme_dialog_begin (CopyThemeDialog *dialog);

G_END_DECLS

#endif /* __COPY_THEME_DIALOG_H__ */
