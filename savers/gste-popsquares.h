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

#ifndef __GSTE_POPSQUARES_H
#define __GSTE_POPSQUARES_H

#include <glib.h>
#include <gdk/gdk.h>
#include "gs-theme-engine.h"

G_BEGIN_DECLS

#define GSTE_TYPE_POPSQUARES         (gste_popsquares_get_type ())
#define GSTE_POPSQUARES(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSTE_TYPE_POPSQUARES, GSTEPopsquares))
#define GSTE_POPSQUARES_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSTE_TYPE_POPSQUARES, GSTEPopsquaresClass))
#define GSTE_IS_POPSQUARES(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSTE_TYPE_POPSQUARES))
#define GSTE_IS_POPSQUARES_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSTE_TYPE_POPSQUARES))
#define GSTE_POPSQUARES_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSTE_TYPE_POPSQUARES, GSTEPopsquaresClass))

typedef struct GSTEPopsquaresPrivate GSTEPopsquaresPrivate;

typedef struct
{
        GSThemeEngine          parent;
        GSTEPopsquaresPrivate *priv;
} GSTEPopsquares;

typedef struct
{
        GSThemeEngineClass     parent_class;
} GSTEPopsquaresClass;

GType           gste_popsquares_get_type         (void);
GSThemeEngine  *gste_popsquares_new              (void);

G_END_DECLS

#endif /* __GSTE_POPSQUARES_H */
