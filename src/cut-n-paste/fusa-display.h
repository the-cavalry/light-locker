/*
 * Fast User Switch Applet: fusa-display.h
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

/* 
 * Facade object used to represent an open VT or Xnest window, managed by FusaDisplayManager
 */

#ifndef __FUSA_DISPLAY__
#define __FUSA_DISPLAY__ 1

#include "fusa-user.h"

G_BEGIN_DECLS

#define FUSA_TYPE_DISPLAY \
  (fusa_display_get_type ())
#define FUSA_DISPLAY(object) \
  (G_TYPE_CHECK_INSTANCE_CAST ((object), FUSA_TYPE_DISPLAY, FusaDisplay))
#define FUSA_IS_DISPLAY(object) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((object), FUSA_TYPE_DISPLAY))

typedef struct _FusaDisplay FusaDisplay;

GType                 fusa_display_get_type      (void) G_GNUC_CONST;

G_CONST_RETURN gchar *fusa_display_get_name      (FusaDisplay *display);
FusaUser             *fusa_display_get_user      (FusaDisplay *display);
gint                  fusa_display_get_console   (FusaDisplay *display);
gboolean              fusa_display_get_nested    (FusaDisplay *display);

void                  fusa_display_switch_to     (FusaDisplay *display,
						  GdkScreen   *screen);

G_END_DECLS

#endif /* !__FUSA_DISPLAY__ */
