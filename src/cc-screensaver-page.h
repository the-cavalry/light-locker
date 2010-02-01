/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 */

#ifndef __CC_SCREENSAVER_PAGE_H
#define __CC_SCREENSAVER_PAGE_H

#include <gtk/gtk.h>
#include <libgnome-control-center-extension/cc-page.h>

G_BEGIN_DECLS

#define CC_TYPE_SCREENSAVER_PAGE         (cc_screensaver_page_get_type ())
#define CC_SCREENSAVER_PAGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CC_TYPE_SCREENSAVER_PAGE, CcScreensaverPage))
#define CC_SCREENSAVER_PAGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CC_TYPE_SCREENSAVER_PAGE, CcScreensaverPageClass))
#define CC_IS_SCREENSAVER_PAGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CC_TYPE_SCREENSAVER_PAGE))
#define CC_IS_SCREENSAVER_PAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CC_TYPE_SCREENSAVER_PAGE))
#define CC_SCREENSAVER_PAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CC_TYPE_SCREENSAVER_PAGE, CcScreensaverPageClass))

typedef struct CcScreensaverPagePrivate CcScreensaverPagePrivate;

typedef struct
{
        CcPage                    parent;
        CcScreensaverPagePrivate *priv;
} CcScreensaverPage;

typedef struct
{
        CcPageClass   parent_class;
} CcScreensaverPageClass;

GType              cc_screensaver_page_get_type   (void);

CcPage *           cc_screensaver_page_new        (void);

G_END_DECLS

#endif /* __CC_SCREENSAVER_PAGE_H */
