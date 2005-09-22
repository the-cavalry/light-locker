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

#ifndef __GS_JOB_H
#define __GS_JOB_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_JOB         (gs_job_get_type ())
#define GS_JOB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_JOB, GSJob))
#define GS_JOB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_JOB, GSJobClass))
#define GS_IS_JOB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_JOB))
#define GS_IS_JOB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_JOB))
#define GS_JOB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_JOB, GSJobClass))

typedef struct GSJobPrivate GSJobPrivate;

typedef struct
{
        GObject       parent;
        GSJobPrivate *priv;
} GSJob;

typedef struct
{
        GObjectClass  parent_class;
} GSJobClass;

typedef struct {
        char  *name;
        char  *comment;
        char  *exec;
        char  *icon;
        char  *path;
        char  *file_id;
        guint  refcount;
} GSJobThemeInfo;

GType           gs_job_get_type                  (void);

GSJob          *gs_job_new                       (void);
GSJob          *gs_job_new_for_widget            (GtkWidget  *widget);
gboolean        gs_job_start                     (GSJob      *job);
gboolean        gs_job_stop                      (GSJob      *job);
gboolean        gs_job_suspend                   (GSJob      *job,
                                                  gboolean    suspend);

void            gs_job_set_widget                (GSJob      *job,
                                                  GtkWidget  *widget);

gboolean        gs_job_set_theme                 (GSJob          *job,
                                                  const char     *theme,
                                                  GError        **error);

void            gs_job_set_theme_path            (GSJob          *job,
                                                  const char     *path[],
                                                  int             n_elements);
void            gs_job_get_theme_path            (GSJob          *job,
                                                  char          **path[],
                                                  int            *n_elements);
void            gs_job_append_theme_path         (GSJob          *job,
                                                  const char     *path);
void            gs_job_prepend_theme_path        (GSJob          *job,
                                                  const char     *path);

GSList         *gs_job_get_theme_info_list       (GSJob          *job);
GSJobThemeInfo *gs_job_lookup_theme_info         (GSJob          *job,
                                                  const char     *theme);
GSJobThemeInfo *gs_job_theme_info_ref            (GSJobThemeInfo *info);
void            gs_job_theme_info_unref          (GSJobThemeInfo *info);
const char     *gs_job_theme_info_get_id         (GSJobThemeInfo *info);
const char     *gs_job_theme_info_get_name       (GSJobThemeInfo *info);

G_END_DECLS

#endif /* __GS_JOB_H */
