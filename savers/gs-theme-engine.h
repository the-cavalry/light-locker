/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GS_THEME_ENGINE_H
#define __GS_THEME_ENGINE_H

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define GS_TYPE_THEME_ENGINE         (gs_theme_engine_get_type ())
#define GS_THEME_ENGINE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_THEME_ENGINE, GSThemeEngine))
#define GS_THEME_ENGINE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_THEME_ENGINE, GSThemeEngineClass))
#define GS_IS_THEME_ENGINE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_THEME_ENGINE))
#define GS_IS_THEME_ENGINE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_THEME_ENGINE))
#define GS_THEME_ENGINE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_THEME_ENGINE, GSThemeEngineClass))

typedef struct GSThemeEnginePrivate GSThemeEnginePrivate;

typedef struct
{
        GtkDrawingArea        parent;
        GSThemeEnginePrivate *priv;
} GSThemeEngine;

typedef struct
{
        GtkDrawingAreaClass parent_class;

        /* for signals later if needed */
        gpointer reserved_1;
        gpointer reserved_2;
        gpointer reserved_3;
        gpointer reserved_4;
} GSThemeEngineClass;

GType           gs_theme_engine_get_type         (void);

void            gs_theme_engine_get_window_size (GSThemeEngine *engine,
                                                 int           *width,
                                                 int           *height);
GdkWindow      *gs_theme_engine_get_window      (GSThemeEngine *engine);

#define ENABLE_PROFILING 1
#ifdef ENABLE_PROFILING
#ifdef G_HAVE_ISO_VARARGS
#define gs_theme_engine_profile_start(...) _gs_theme_engine_profile_log (G_STRFUNC, "start", __VA_ARGS__)
#define gs_theme_engine_profile_end(...)   _gs_theme_engine_profile_log (G_STRFUNC, "end", __VA_ARGS__)
#define gs_theme_engine_profile_msg(...)   _gs_theme_engine_profile_log (NULL, NULL, __VA_ARGS__)
#elif defined(G_HAVE_GNUC_VARARGS)
#define gs_theme_engine_profile_start(format...) _gs_theme_engine_profile_log (G_STRFUNC, "start", format)
#define gs_theme_engine_profile_end(format...)   _gs_theme_engine_profile_log (G_STRFUNC, "end", format)
#define gs_theme_engine_profile_msg(format...)   _gs_theme_engine_profile_log (NULL, NULL, format)
#endif
#else
#define gs_theme_engine_profile_start(...)
#define gs_theme_engine_profile_end(...)
#define gs_theme_engine_profile_msg(...)
#endif

void            _gs_theme_engine_profile_log    (const char *func,
                                                 const char *note,
                                                 const char *format,
                                                 ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS

#endif /* __GS_THEME_ENGINE_H */
