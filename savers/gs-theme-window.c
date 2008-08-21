/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * gs-theme-window.c - special toplevel for screensavers
 *
 * Copyright (C) 2005 Ray Strode <rstrode@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Originally written by: Ray Strode <rstrode@redhat.com>
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gs-theme-window.h"

static void gs_theme_window_finalize     (GObject *object);
static void gs_theme_window_real_realize (GtkWidget *widget);

static GObjectClass   *parent_class = NULL;

G_DEFINE_TYPE (GSThemeWindow, gs_theme_window, GTK_TYPE_WINDOW)

#define MIN_SIZE 10

static void
gs_theme_window_class_init (GSThemeWindowClass *klass)
{
        GObjectClass   *object_class;
        GtkWidgetClass *widget_class;

        object_class = G_OBJECT_CLASS (klass);
        widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize = gs_theme_window_finalize;

        widget_class->realize = gs_theme_window_real_realize;
}

static void
force_no_pixmap_background (GtkWidget *widget)
{
        static gboolean first_time = TRUE;

        if (first_time) {
                gtk_rc_parse_string ("\n"
                                     "   style \"gs-theme-engine-style\"\n"
                                     "   {\n"
                                     "      bg_pixmap[NORMAL] = \"<none>\"\n"
                                     "      bg_pixmap[ACTIVE] = \"<none>\"\n"
                                     "      bg_pixmap[PRELIGHT] = \"<none>\"\n"
                                     "      bg_pixmap[SELECTED] = \"<none>\"\n"
                                     "      bg_pixmap[INSENSITIVE] = \"<none>\"\n"
                                     "      bg[NORMAL] = \"#000000\"\n"
                                     "      bg[ACTIVE] = \"#000000\"\n"
                                     "      bg[PRELIGHT] = \"#000000\"\n"
                                     "      bg[SELECTED] = \"#000000\"\n"
                                     "      bg[INSENSITIVE] = \"#000000\"\n"
                                     "   }\n"
                                     "   widget \"gs-window*\" style : highest \"gs-theme-engine-style\"\n"
                                     "\n");
                first_time = FALSE;
        }

        gtk_widget_set_name (widget, "gs-window");
}

static void
gs_theme_window_init (GSThemeWindow *window)
{
        force_no_pixmap_background (GTK_WIDGET (window));
}

static void
gs_theme_window_finalize (GObject *object)
{
        GSThemeWindow *window;
        GObjectClass  *parent_class;

        window = GS_THEME_WINDOW (object);

        parent_class = G_OBJECT_CLASS (gs_theme_window_parent_class);

        if (parent_class->finalize != NULL)
                parent_class->finalize (object);
}

static void
gs_theme_window_real_realize (GtkWidget *widget)
{
        GdkWindow     *window;
        Window         remote_xwindow;
        GtkRequisition requisition;
        GtkAllocation  allocation;
        const char    *preview_xid;
        int            x;
        int            y;
        int            width;
        int            height;
        int            event_mask;

        event_mask = 0;
        window = NULL;
        preview_xid = g_getenv ("XSCREENSAVER_WINDOW");

        if (preview_xid != NULL) {
                char *end;

                remote_xwindow = (Window) strtoul (preview_xid, &end, 0);

                if ((remote_xwindow != 0) && (end != NULL) &&
                    ((*end == ' ') || (*end == '\0')) &&
                    ((remote_xwindow < G_MAXULONG) || (errno != ERANGE))) {
                        window = gdk_window_foreign_new (remote_xwindow);

                        if (window != NULL) {
                                /* This is a kludge; we need to set the same
                                 * flags gs-window-x11.c does, to ensure they
                                 * don't get unset by gtk_window_map() later.
                                 */
                                gtk_window_set_decorated (GTK_WINDOW (widget), FALSE);

                                gtk_window_set_skip_taskbar_hint (GTK_WINDOW (widget), TRUE);
                                gtk_window_set_skip_pager_hint (GTK_WINDOW (widget), TRUE);

                                gtk_window_set_keep_above (GTK_WINDOW (widget), TRUE);

                                gtk_window_fullscreen (GTK_WINDOW (widget));

                                event_mask = GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK;
                                gtk_widget_set_events (widget, gtk_widget_get_events (widget) | event_mask);
                        }
                }
        }

        if (window == NULL) {
                GtkWidgetClass *parent_class;

                parent_class = GTK_WIDGET_CLASS (gs_theme_window_parent_class);

                if (parent_class->realize != NULL)
                        parent_class->realize (widget);

                return;
        }

        gtk_style_set_background (widget->style,
                                  window,
                                  GTK_STATE_NORMAL);
        gdk_window_set_decorations (window, (GdkWMDecoration) 0);
        gdk_window_set_events (window, gdk_window_get_events (window) | event_mask);

        widget->window = window;
        gdk_window_set_user_data (window, widget);
        GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

        gdk_window_get_geometry (window, &x, &y, &width, &height, NULL);

        if (width < MIN_SIZE || height < MIN_SIZE) {
                g_critical ("This window is way too small to use");
                exit (1);
        }

        gtk_widget_size_request (widget, &requisition);
        allocation.x = x;
        allocation.y = y;
        allocation.width = width;
        allocation.height = height;
        gtk_widget_size_allocate (widget, &allocation);
        gtk_window_resize (GTK_WINDOW (widget), width, height);
}

GtkWidget *
gs_theme_window_new (void)
{
        GSThemeWindow *window;

        window = g_object_new (GS_TYPE_THEME_WINDOW,
                               "type", GTK_WINDOW_TOPLEVEL,
                               NULL);

        return GTK_WIDGET (window);
}
