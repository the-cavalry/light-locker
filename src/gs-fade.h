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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __GS_FADE_H
#define __GS_FADE_H

G_BEGIN_DECLS

#define GS_TYPE_FADE         (gs_fade_get_type ())
#define GS_FADE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_FADE, GSFade))
#define GS_FADE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_FADE, GSFadeClass))
#define GS_IS_FADE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_FADE))
#define GS_IS_FADE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_FADE))
#define GS_FADE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_FADE, GSFadeClass))


typedef struct GSFadePrivate GSFadePrivate;

typedef struct
{
        GObject        parent;
        GSFadePrivate *priv;
} GSFade;

typedef struct
{
        GObjectClass   parent_class;

        void          (* faded)        (GSFade *fade);
} GSFadeClass;

typedef void  (* GSFadeDoneFunc) (GSFade       *fade,
                                  gpointer      data);


GType       gs_fade_get_type         (void);

GSFade    * gs_fade_new              (void);

void        gs_fade_async            (GSFade        *fade,
                                      guint          timeout,
                                      GSFadeDoneFunc done_cb,
                                      gpointer       data);
void        gs_fade_sync             (GSFade        *fade,
                                      guint          timeout);

void        gs_fade_finish           (GSFade    *fade);
void        gs_fade_reset            (GSFade    *fade);

gboolean    gs_fade_get_active       (GSFade    *fade);

gboolean    gs_fade_get_enabled      (GSFade    *fade);
void        gs_fade_set_enabled      (GSFade    *fade,
                                      gboolean   enabled);

G_END_DECLS

#endif /* __GS_FADE_H */
