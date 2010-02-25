/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#ifdef WITH_KBD_LAYOUT_INDICATOR
#include <libgnomekbd/gkbd-indicator.h>
#endif

#ifdef WITH_LIBNOTIFY
#include <libnotify/notify.h>
#endif

#include "gs-lock-plug.h"

#include "gs-debug.h"

#define KEY_LOCK_DIALOG_THEME "/apps/gnome-screensaver/lock_dialog_theme"
#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"

/* same as SMS ;) */
#define NOTE_BUFFER_MAX_CHARS 160

enum {
        AUTH_PAGE = 0,
};

#define FACE_ICON_SIZE 48
#define DIALOG_TIMEOUT_MSEC 60000

static void gs_lock_plug_finalize   (GObject         *object);

#define GS_LOCK_PLUG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LOCK_PLUG, GSLockPlugPrivate))

struct GSLockPlugPrivate
{
        GtkWidget   *vbox;
        GtkWidget   *auth_action_area;

        GtkWidget   *notebook;
        GtkWidget   *auth_face_image;
        GtkWidget   *auth_realname_label;
        GtkWidget   *auth_username_label;
        GtkWidget   *auth_prompt_label;
        GtkWidget   *auth_prompt_entry;
        GtkWidget   *auth_prompt_box;
        GtkWidget   *auth_capslock_label;
        GtkWidget   *auth_message_label;
        GtkWidget   *status_message_label;

        GtkWidget   *auth_unlock_button;
        GtkWidget   *auth_switch_button;
        GtkWidget   *auth_cancel_button;
        GtkWidget   *auth_logout_button;
        GtkWidget   *auth_note_button;
        GtkWidget   *note_tab;
        GtkWidget   *note_tab_label;
        GtkWidget   *note_text_view;
        GtkWidget   *note_ok_button;
        GtkWidget   *note_cancel_button;

        GtkWidget   *auth_prompt_kbd_layout_indicator;

        gboolean     caps_lock_on;
        gboolean     switch_enabled;
        gboolean     leave_note_enabled;
        gboolean     logout_enabled;
        char        *logout_command;
        char        *status_message;

        guint        timeout;

        guint        cancel_timeout_id;
        guint        auth_check_idle_id;
        guint        response_idle_id;

        GList       *key_events;
};

typedef struct _ResponseData ResponseData;

struct _ResponseData
{
        gint response_id;
};


enum {
        RESPONSE,
        CLOSE,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_LOGOUT_ENABLED,
        PROP_LOGOUT_COMMAND,
        PROP_SWITCH_ENABLED,
        PROP_STATUS_MESSAGE
};

static guint lock_plug_signals [LAST_SIGNAL];

G_DEFINE_TYPE (GSLockPlug, gs_lock_plug, GTK_TYPE_PLUG)

static void
gs_lock_plug_style_set (GtkWidget *widget,
                        GtkStyle  *previous_style)
{
        GSLockPlug *plug;

        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->style_set) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->style_set (widget, previous_style);
        }

        plug = GS_LOCK_PLUG (widget);

        if (! plug->priv->vbox) {
                return;
        }

        gtk_container_set_border_width (GTK_CONTAINER (plug->priv->vbox), 12);
        gtk_box_set_spacing (GTK_BOX (plug->priv->vbox), 12);

        gtk_container_set_border_width (GTK_CONTAINER (plug->priv->auth_action_area), 0);
        gtk_box_set_spacing (GTK_BOX (plug->priv->auth_action_area), 5);
}

static void
do_user_switch (GSLockPlug *plug)
{
        GError  *error;
        gboolean res;
        char    *command;

        command = g_strdup_printf ("%s %s",
                                   GDM_FLEXISERVER_COMMAND,
                                   GDM_FLEXISERVER_ARGS);

        error = NULL;
        res = gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
                                                command,
                                                &error);

        g_free (command);

        if (! res) {
                gs_debug ("Unable to start GDM greeter: %s", error->message);
                g_error_free (error);
        }
}

static void
set_status_text (GSLockPlug *plug,
                 const char *text)
{
        if (plug->priv->auth_message_label != NULL) {
                gtk_label_set_text (GTK_LABEL (plug->priv->auth_message_label), text);
        }
}

void
gs_lock_plug_set_sensitive (GSLockPlug *plug,
                            gboolean    sensitive)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, sensitive);
        gtk_widget_set_sensitive (plug->priv->auth_action_area, sensitive);
}

static void
remove_cancel_timeout (GSLockPlug *plug)
{
        if (plug->priv->cancel_timeout_id > 0) {
                g_source_remove (plug->priv->cancel_timeout_id);
                plug->priv->cancel_timeout_id = 0;
        }
}

static void
remove_response_idle (GSLockPlug *plug)
{
        if (plug->priv->response_idle_id > 0) {
                g_source_remove (plug->priv->response_idle_id);
                plug->priv->response_idle_id = 0;
        }
}

static void
gs_lock_plug_response (GSLockPlug *plug,
                       gint        response_id)
{
        int new_response;

        new_response = response_id;

        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        /* Act only on response IDs we recognize */
        if (!(response_id == GS_LOCK_PLUG_RESPONSE_OK
              || response_id == GS_LOCK_PLUG_RESPONSE_CANCEL)) {
                return;
        }

        remove_cancel_timeout (plug);
        remove_response_idle (plug);

        if (response_id == GS_LOCK_PLUG_RESPONSE_CANCEL) {
                gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), "");
        }

        g_signal_emit (plug,
                       lock_plug_signals [RESPONSE],
                       0,
                       new_response);
}

static gboolean
response_cancel_idle_cb (GSLockPlug *plug)
{
        plug->priv->response_idle_id = 0;

        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);

        return FALSE;
}

static gboolean
dialog_timed_out (GSLockPlug *plug)
{
        gs_lock_plug_set_sensitive (plug, FALSE);
        set_status_text (plug, _("Time has expired."));

        if (plug->priv->response_idle_id != 0) {
                g_warning ("Response idle ID already set but shouldn't be");
        }

        remove_response_idle (plug);

        plug->priv->response_idle_id = g_timeout_add (2000,
                                                      (GSourceFunc)response_cancel_idle_cb,
                                                      plug);
        return FALSE;
}


static void
capslock_update (GSLockPlug *plug,
                 gboolean    is_on)
{

        plug->priv->caps_lock_on = is_on;

        if (plug->priv->auth_capslock_label == NULL) {
                return;
        }

        if (is_on) {
                gtk_label_set_text (GTK_LABEL (plug->priv->auth_capslock_label),
                                    _("You have the Caps Lock key on."));
        } else {
                gtk_label_set_text (GTK_LABEL (plug->priv->auth_capslock_label),
                                    "");
        }
}

static gboolean
is_capslock_on (void)
{
        XkbStateRec states;
        Display    *dsp;

        dsp = GDK_DISPLAY ();
        if (XkbGetState (dsp, XkbUseCoreKbd, &states) != Success) {
                return FALSE;
        }

        return (states.locked_mods & LockMask) != 0;
}

static void
restart_cancel_timeout (GSLockPlug *plug)
{
        remove_cancel_timeout (plug);

        plug->priv->cancel_timeout_id = g_timeout_add (plug->priv->timeout,
                                                       (GSourceFunc)dialog_timed_out,
                                                       plug);
}

void
gs_lock_plug_get_text (GSLockPlug *plug,
                       char      **text)
{
        const char *typed_text;
        char       *null_text;
        char       *local_text;

        typed_text = gtk_entry_get_text (GTK_ENTRY (plug->priv->auth_prompt_entry));
        local_text = g_locale_from_utf8 (typed_text, strlen (typed_text), NULL, NULL, NULL);

        null_text = g_strnfill (strlen (typed_text) + 1, '\b');
        gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), null_text);
        gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), "");
        g_free (null_text);

        if (text != NULL) {
                *text = local_text;
        }
}

typedef struct
{
        GSLockPlug *plug;
        gint response_id;
        GMainLoop *loop;
        gboolean destroyed;
} RunInfo;

static void
shutdown_loop (RunInfo *ri)
{
        if (g_main_loop_is_running (ri->loop))
                g_main_loop_quit (ri->loop);
}

static void
run_unmap_handler (GSLockPlug *plug,
                   gpointer data)
{
        RunInfo *ri = data;

        shutdown_loop (ri);
}

static void
run_response_handler (GSLockPlug *plug,
                      gint response_id,
                      gpointer data)
{
        RunInfo *ri;

        ri = data;

        ri->response_id = response_id;

        shutdown_loop (ri);
}

static gint
run_delete_handler (GSLockPlug *plug,
                    GdkEventAny *event,
                    gpointer data)
{
        RunInfo *ri = data;

        shutdown_loop (ri);

        return TRUE; /* Do not destroy */
}

static void
run_destroy_handler (GSLockPlug *plug,
                     gpointer data)
{
        RunInfo *ri = data;

        /* shutdown_loop will be called by run_unmap_handler */
        ri->destroyed = TRUE;
}

/* adapted from GTK+ gtkdialog.c */
int
gs_lock_plug_run (GSLockPlug *plug)
{
        RunInfo ri = { NULL, GTK_RESPONSE_NONE, NULL, FALSE };
        gboolean was_modal;
        gulong response_handler;
        gulong unmap_handler;
        gulong destroy_handler;
        gulong delete_handler;

        g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), -1);

        g_object_ref (plug);

        was_modal = GTK_WINDOW (plug)->modal;
        if (!was_modal) {
                gtk_window_set_modal (GTK_WINDOW (plug), TRUE);
        }

        if (!GTK_WIDGET_VISIBLE (plug)) {
                gtk_widget_show (GTK_WIDGET (plug));
        }

        response_handler =
                g_signal_connect (plug,
                                  "response",
                                  G_CALLBACK (run_response_handler),
                                  &ri);

        unmap_handler =
                g_signal_connect (plug,
                                  "unmap",
                                  G_CALLBACK (run_unmap_handler),
                                  &ri);

        delete_handler =
                g_signal_connect (plug,
                                  "delete_event",
                                  G_CALLBACK (run_delete_handler),
                                  &ri);

        destroy_handler =
                g_signal_connect (plug,
                                  "destroy",
                                  G_CALLBACK (run_destroy_handler),
                                  &ri);

        ri.loop = g_main_loop_new (NULL, FALSE);

        GDK_THREADS_LEAVE ();
        g_main_loop_run (ri.loop);
        GDK_THREADS_ENTER ();

        g_main_loop_unref (ri.loop);

        ri.loop = NULL;

        if (!ri.destroyed) {
                if (! was_modal) {
                        gtk_window_set_modal (GTK_WINDOW (plug), FALSE);
                }

                g_signal_handler_disconnect (plug, response_handler);
                g_signal_handler_disconnect (plug, unmap_handler);
                g_signal_handler_disconnect (plug, delete_handler);
                g_signal_handler_disconnect (plug, destroy_handler);
        }

        g_object_unref (plug);

        return ri.response_id;
}


static cairo_surface_t *
surface_from_pixbuf (GdkPixbuf *pixbuf)
{
        cairo_surface_t *surface;
        cairo_t         *cr;

        surface = cairo_image_surface_create (gdk_pixbuf_get_has_alpha (pixbuf) ?
                                              CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
                                              gdk_pixbuf_get_width (pixbuf),
                                              gdk_pixbuf_get_height (pixbuf));
        cr = cairo_create (surface);
        gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
        cairo_paint (cr);
        cairo_destroy (cr);

        return surface;
}

static void
rounded_rectangle (cairo_t *cr,
                   gdouble  aspect,
                   gdouble  x,
                   gdouble  y,
                   gdouble  corner_radius,
                   gdouble  width,
                   gdouble  height)
{
        gdouble radius;
        gdouble degrees;

        radius = corner_radius / aspect;
        degrees = G_PI / 180.0;

        cairo_new_sub_path (cr);
        cairo_arc (cr,
                   x + width - radius,
                   y + radius,
                   radius,
                   -90 * degrees,
                   0 * degrees);
        cairo_arc (cr,
                   x + width - radius,
                   y + height - radius,
                   radius,
                   0 * degrees,
                   90 * degrees);
        cairo_arc (cr,
                   x + radius,
                   y + height - radius,
                   radius,
                   90 * degrees,
                   180 * degrees);
        cairo_arc (cr,
                   x + radius,
                   y + radius,
                   radius,
                   180 * degrees,
                   270 * degrees);
        cairo_close_path (cr);
}

static void
image_set_from_pixbuf (GtkImage  *image,
                       GdkPixbuf *source)
{
        cairo_t         *cr;
        cairo_t         *cr_mask;
        cairo_surface_t *surface;
        GdkPixmap       *pixmap;
        GdkPixmap       *bitmask;
        int              w;
        int              h;
        int              frame_width;
        double           radius;
        GdkColor         color;
        double           r;
        double           g;
        double           b;

        frame_width = 5;

        w = gdk_pixbuf_get_width (source) + frame_width * 2;
        h = gdk_pixbuf_get_height (source) + frame_width * 2;

        radius = w / 10;

        pixmap = gdk_pixmap_new (GTK_WIDGET (image)->window, w, h, -1);
        bitmask = gdk_pixmap_new (GTK_WIDGET (image)->window, w, h, 1);

        cr = gdk_cairo_create (pixmap);
        cr_mask = gdk_cairo_create (bitmask);

        /* setup mask */
        cairo_rectangle (cr_mask, 0, 0, w, h);
        cairo_set_operator (cr_mask, CAIRO_OPERATOR_CLEAR);
        cairo_fill (cr_mask);

        rounded_rectangle (cr_mask, 1.0, 0.5, 0.5, radius, w - 1, h - 1);
        cairo_set_operator (cr_mask, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgb (cr_mask, 1, 1, 1);
        cairo_fill (cr_mask);

        color = GTK_WIDGET (image)->style->bg [GTK_STATE_NORMAL];
        r = (float)color.red / 65535.0;
        g = (float)color.green / 65535.0;
        b = (float)color.blue / 65535.0;

        /* set up image */
        cairo_rectangle (cr, 0, 0, w, h);
        cairo_set_source_rgb (cr, r, g, b);
        cairo_fill (cr);

        rounded_rectangle (cr,
                           1.0,
                           frame_width + 0.5,
                           frame_width + 0.5,
                           radius,
                           w - frame_width * 2 - 1,
                           h - frame_width * 2 - 1);
        cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.3);
        cairo_fill_preserve (cr);

        surface = surface_from_pixbuf (source);
        cairo_set_source_surface (cr, surface, frame_width, frame_width);
        cairo_fill (cr);

        gtk_image_set_from_pixmap (image, pixmap, bitmask);

        cairo_surface_destroy (surface);

        g_object_unref (bitmask);
        g_object_unref (pixmap);

        cairo_destroy (cr_mask);
        cairo_destroy (cr);
}


static gboolean
check_user_file (const gchar *filename,
                 uid_t        user,
                 gssize       max_file_size,
                 gboolean     relax_group,
                 gboolean     relax_other)
{
        struct stat fileinfo;

        if (max_file_size < 0) {
                max_file_size = G_MAXSIZE;
        }

        /* Exists/Readable? */
        if (g_stat (filename, &fileinfo) < 0) {
                return FALSE;
        }

        /* Is a regular file */
        if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode))) {
                return FALSE;
        }

        /* Owned by user? */
        if (G_UNLIKELY (fileinfo.st_uid != user)) {
                return FALSE;
        }

        /* Group not writable or relax_group? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWGRP) == S_IWGRP && !relax_group)) {
                return FALSE;
        }

        /* Other not writable or relax_other? */
        if (G_UNLIKELY ((fileinfo.st_mode & S_IWOTH) == S_IWOTH && !relax_other)) {
                return FALSE;
        }

        /* Size is kosher? */
        if (G_UNLIKELY (fileinfo.st_size > max_file_size)) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
set_face_image (GSLockPlug *plug)
{
        GdkPixbuf    *pixbuf;
        const char   *homedir;
        char         *path;
        int           icon_size = 96;
        gsize         user_max_file = 65536;
        uid_t         uid;

        homedir = g_get_home_dir ();
        uid = getuid ();

        path = g_build_filename (homedir, ".face", NULL);

        pixbuf = NULL;
        if (check_user_file (path, uid, user_max_file, 0, 0)) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        }

        g_free (path);

        if (pixbuf == NULL) {
                return FALSE;
        }

        image_set_from_pixbuf (GTK_IMAGE (plug->priv->auth_face_image), pixbuf);

        g_object_unref (pixbuf);

        return TRUE;
}

static void
gs_lock_plug_show (GtkWidget *widget)
{
        GSLockPlug *plug = GS_LOCK_PLUG (widget);

        gs_profile_start (NULL);

        gs_profile_start ("parent");
        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->show) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->show (widget);
        }

        gs_profile_end ("parent");


        if (plug->priv->auth_face_image) {
                set_face_image (plug);
        }

        capslock_update (plug, is_capslock_on ());

        restart_cancel_timeout (plug);

        gs_profile_end (NULL);
}

static void
gs_lock_plug_hide (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->hide) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->hide (widget);
        }
}

static void
queue_key_event (GSLockPlug  *plug,
                 GdkEventKey *event)
{
        GdkEvent *saved_event;

        saved_event = gdk_event_copy ((GdkEvent *)event);
        plug->priv->key_events = g_list_prepend (plug->priv->key_events,
                                                 saved_event);
}

static void
forward_key_events (GSLockPlug *plug)
{
        plug->priv->key_events = g_list_reverse (plug->priv->key_events);
        while (plug->priv->key_events != NULL) {
                GdkEventKey *event = plug->priv->key_events->data;

                gtk_window_propagate_key_event (GTK_WINDOW (plug), event);

                gdk_event_free ((GdkEvent *)event);

                plug->priv->key_events = g_list_delete_link (plug->priv->key_events,
                                                             plug->priv->key_events);
        }
}

static void
gs_lock_plug_size_request (GtkWidget      *widget,
                           GtkRequisition *requisition)
{
        GSLockPlug *plug = GS_LOCK_PLUG (widget);
        int mod_width;
        int mod_height;

        if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->size_request) {
                GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->size_request (widget, requisition);
        }

        /* don't constrain size when themed */
        if (plug->priv->vbox == NULL) {
                return;
        }

        mod_width = requisition->height * 1.3;
        mod_height = requisition->width / 1.6;
        if (requisition->width < mod_width) {
                /* if the dialog is tall fill out the width */
                requisition->width = mod_width;
        } else if (requisition->height < mod_height) {
                /* if the dialog is wide fill out the height */
                requisition->height = mod_height;
        }
}

static void
gs_lock_plug_set_logout_enabled (GSLockPlug *plug,
                                 gboolean    logout_enabled)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        if (plug->priv->logout_enabled == logout_enabled) {
                return;
        }

        plug->priv->logout_enabled = logout_enabled;
        g_object_notify (G_OBJECT (plug), "logout-enabled");

        if (plug->priv->auth_logout_button == NULL) {
                return;
        }

        if (logout_enabled) {
                gtk_widget_show (plug->priv->auth_logout_button);
        } else {
                gtk_widget_hide (plug->priv->auth_logout_button);
        }
}

static void
gs_lock_plug_set_logout_command (GSLockPlug *plug,
                                 const char *command)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        g_free (plug->priv->logout_command);

        if (command) {
                plug->priv->logout_command = g_strdup (command);
        } else {
                plug->priv->logout_command = NULL;
        }
}

static gboolean
is_program_in_path (const char *program)
{
        char *tmp = g_find_program_in_path (program);
        if (tmp != NULL) {
                g_free (tmp);
                return TRUE;
        } else {
                return FALSE;
        }
}

static void
gs_lock_plug_set_status_message (GSLockPlug *plug,
                                 const char *status_message)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        g_free (plug->priv->status_message);
        plug->priv->status_message = g_strdup (status_message);

        if (plug->priv->status_message_label) {
                if (plug->priv->status_message) {
                        gtk_label_set_text (GTK_LABEL (plug->priv->status_message_label),
                                            plug->priv->status_message);
                        gtk_widget_show (plug->priv->status_message_label);
                }
                else {
                        gtk_widget_hide (plug->priv->status_message_label);
                }
        }
}

static void
gs_lock_plug_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
        GSLockPlug *self;

        self = GS_LOCK_PLUG (object);

        switch (prop_id) {
        case PROP_LOGOUT_ENABLED:
                g_value_set_boolean (value, self->priv->logout_enabled);
                break;
        case PROP_LOGOUT_COMMAND:
                g_value_set_string (value, self->priv->logout_command);
                break;
        case PROP_SWITCH_ENABLED:
                g_value_set_boolean (value, self->priv->switch_enabled);
                break;
        case PROP_STATUS_MESSAGE:
                g_value_set_string (value, self->priv->status_message);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_lock_plug_set_switch_enabled (GSLockPlug *plug,
                                 gboolean    switch_enabled)
{
        g_return_if_fail (GS_LOCK_PLUG (plug));

        if (plug->priv->switch_enabled == switch_enabled) {
                return;
        }

        plug->priv->switch_enabled = switch_enabled;
        g_object_notify (G_OBJECT (plug), "switch-enabled");

        if (plug->priv->auth_switch_button == NULL) {
                return;
        }

        if (switch_enabled) {
                gboolean found;
                found = is_program_in_path (GDM_FLEXISERVER_COMMAND);
                if (found) {
                        gtk_widget_show (plug->priv->auth_switch_button);
                } else {
                        gs_debug ("Waring: GDM flexiserver command not found: %s", GDM_FLEXISERVER_COMMAND);
                        gtk_widget_hide (plug->priv->auth_switch_button);
                }
        } else {
                gtk_widget_hide (plug->priv->auth_switch_button);
        }
}

static void
gs_lock_plug_set_property (GObject            *object,
                           guint               prop_id,
                           const GValue       *value,
                           GParamSpec         *pspec)
{
        GSLockPlug *self;

        self = GS_LOCK_PLUG (object);

        switch (prop_id) {
        case PROP_LOGOUT_ENABLED:
                gs_lock_plug_set_logout_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_LOGOUT_COMMAND:
                gs_lock_plug_set_logout_command (self, g_value_get_string (value));
                break;
        case PROP_STATUS_MESSAGE:
                gs_lock_plug_set_status_message (self, g_value_get_string (value));
                break;
        case PROP_SWITCH_ENABLED:
                gs_lock_plug_set_switch_enabled (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gs_lock_plug_close (GSLockPlug *plug)
{
        /* Synthesize delete_event to close dialog. */

        GtkWidget *widget = GTK_WIDGET (plug);
        GdkEvent  *event;

        event = gdk_event_new (GDK_DELETE);
        event->any.window = g_object_ref (widget->window);
        event->any.send_event = TRUE;

        gtk_main_do_event (event);
        gdk_event_free (event);
}

static void
gs_lock_plug_class_init (GSLockPlugClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
        GtkBindingSet  *binding_set;

        object_class->finalize     = gs_lock_plug_finalize;
        object_class->get_property = gs_lock_plug_get_property;
        object_class->set_property = gs_lock_plug_set_property;

        widget_class->style_set    = gs_lock_plug_style_set;
        widget_class->show         = gs_lock_plug_show;
        widget_class->hide         = gs_lock_plug_hide;
        widget_class->size_request = gs_lock_plug_size_request;

        klass->close = gs_lock_plug_close;

        g_type_class_add_private (klass, sizeof (GSLockPlugPrivate));

        lock_plug_signals [RESPONSE] = g_signal_new ("response",
                                                     G_OBJECT_CLASS_TYPE (klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GSLockPlugClass, response),
                                                     NULL, NULL,
                                                     g_cclosure_marshal_VOID__INT,
                                                     G_TYPE_NONE, 1,
                                                     G_TYPE_INT);
        lock_plug_signals [CLOSE] = g_signal_new ("close",
                                                  G_OBJECT_CLASS_TYPE (klass),
                                                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                  G_STRUCT_OFFSET (GSLockPlugClass, close),
                                                  NULL, NULL,
                                                  g_cclosure_marshal_VOID__VOID,
                                                  G_TYPE_NONE, 0);

        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_ENABLED,
                                         g_param_spec_boolean ("logout-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LOGOUT_COMMAND,
                                         g_param_spec_string ("logout-command",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_STATUS_MESSAGE,
                                         g_param_spec_string ("status-message",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SWITCH_ENABLED,
                                         g_param_spec_boolean ("switch-enabled",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        binding_set = gtk_binding_set_by_class (klass);

        gtk_binding_entry_add_signal (binding_set, GDK_Escape, 0,
                                      "close", 0);
}

static void
clear_clipboards (GSLockPlug *plug)
{
        GtkClipboard *clipboard;

        clipboard = gtk_widget_get_clipboard (GTK_WIDGET (plug), GDK_SELECTION_PRIMARY);
        gtk_clipboard_clear (clipboard);
        gtk_clipboard_set_text (clipboard, "", -1);
        clipboard = gtk_widget_get_clipboard (GTK_WIDGET (plug), GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_clear (clipboard);
        gtk_clipboard_set_text (clipboard, "", -1);
}

static void
take_note (GtkButton  *button,
           GSLockPlug *plug)
{
        int page;

        page = gtk_notebook_page_num (GTK_NOTEBOOK (plug->priv->notebook), plug->priv->note_tab);
        gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), page);
        /* this counts as activity so restart the timer */
        restart_cancel_timeout (plug);
}

static void
submit_note (GtkButton  *button,
             GSLockPlug *plug)
{
#ifdef WITH_LIBNOTIFY
        char               *text;
        char                summary[128];
        char               *escaped_text;
        GtkTextBuffer      *buffer;
        GtkTextIter         start, end;
        time_t              t;
        struct tm          *tmp;
        NotifyNotification *note;

        gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), AUTH_PAGE);
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (plug->priv->note_text_view));
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        gtk_text_buffer_set_text (buffer, "", 0);
        escaped_text = g_markup_escape_text (text, -1);

        t = time (NULL);
        tmp = localtime (&t);
        strftime (summary, 128, "%X", tmp);

        notify_init ("gnome-screensaver-dialog");
        note = notify_notification_new (summary, escaped_text, NULL, NULL);
        notify_notification_set_timeout (note, NOTIFY_EXPIRES_NEVER);
        notify_notification_show (note, NULL);
        g_object_unref (note);

        g_free (text);
        g_free (escaped_text);

        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);
#endif /* WITH_LIBNOTIFY */
}

static void
cancel_note (GtkButton  *button,
             GSLockPlug *plug)
{
        GtkTextBuffer *buffer;

        gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), AUTH_PAGE);
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (plug->priv->note_text_view));
        gtk_text_buffer_set_text (buffer, "", 0);

        /* this counts as activity so restart the timer */
        restart_cancel_timeout (plug);

        gtk_window_set_default (GTK_WINDOW (plug), plug->priv->auth_unlock_button);

        clear_clipboards (plug);
}

static void
logout_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        char   **argv  = NULL;
        GError  *error = NULL;
        gboolean res;

        if (! plug->priv->logout_command) {
                return;
        }

        res = g_shell_parse_argv (plug->priv->logout_command, NULL, &argv, &error);

        if (! res) {
                g_warning ("Could not parse logout command: %s", error->message);
                g_error_free (error);
                return;
        }

        g_spawn_async (g_get_home_dir (),
                       argv,
                       NULL,
                       G_SPAWN_SEARCH_PATH,
                       NULL,
                       NULL,
                       NULL,
                       &error);

        g_strfreev (argv);

        if (error) {
                g_warning ("Could not run logout command: %s", error->message);
                g_error_free (error);
        }
}

void
gs_lock_plug_set_busy (GSLockPlug *plug)
{
        GdkCursor *cursor;
        GtkWidget *top_level;

        top_level = gtk_widget_get_toplevel (GTK_WIDGET (plug));

        cursor = gdk_cursor_new (GDK_WATCH);
        gdk_window_set_cursor (top_level->window, cursor);
        gdk_cursor_unref (cursor);
}

void
gs_lock_plug_set_ready (GSLockPlug *plug)
{
        GdkCursor *cursor;
        GtkWidget *top_level;

        top_level = gtk_widget_get_toplevel (GTK_WIDGET (plug));

        cursor = gdk_cursor_new (GDK_LEFT_PTR);
        gdk_window_set_cursor (top_level->window, cursor);
        gdk_cursor_unref (cursor);
}

void
gs_lock_plug_enable_prompt (GSLockPlug *plug,
                            const char *message,
                            gboolean    visible)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        gs_debug ("Setting prompt to: %s", message);

        gtk_widget_set_sensitive (plug->priv->auth_unlock_button, TRUE);
        gtk_widget_show (plug->priv->auth_unlock_button);
        gtk_widget_grab_default (plug->priv->auth_unlock_button);
        gtk_label_set_text (GTK_LABEL (plug->priv->auth_prompt_label), message);
        gtk_widget_show (plug->priv->auth_prompt_label);
        gtk_entry_set_visibility (GTK_ENTRY (plug->priv->auth_prompt_entry), visible);
        gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, TRUE);
        gtk_widget_show (plug->priv->auth_prompt_entry);

        if (! GTK_WIDGET_HAS_FOCUS (plug->priv->auth_prompt_entry)) {
                gtk_widget_grab_focus (plug->priv->auth_prompt_entry);
        }

        /* were there any key events sent to the plug while the
         * entry wasnt ready? If so, forward them along
         */
        forward_key_events (plug);

        restart_cancel_timeout (plug);
}

void
gs_lock_plug_disable_prompt (GSLockPlug *plug)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        /* gtk_widget_hide (plug->priv->auth_prompt_entry); */
        /* gtk_widget_hide (plug->priv->auth_prompt_label); */
        gtk_widget_set_sensitive (plug->priv->auth_unlock_button, FALSE);
        gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, FALSE);
        /* gtk_widget_hide (plug->priv->auth_unlock_button); */

        gtk_widget_grab_default (plug->priv->auth_cancel_button);
}

void
gs_lock_plug_show_message (GSLockPlug *plug,
                           const char *message)
{
        g_return_if_fail (GS_IS_LOCK_PLUG (plug));

        set_status_text (plug, message ? message : "");
}

/* button press handler used to inhibit popup menu */
static gint
entry_button_press (GtkWidget      *widget,
                    GdkEventButton *event)
{
        if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
                return TRUE;
        }

        return FALSE;
}

static gint
entry_key_press (GtkWidget   *widget,
                 GdkEventKey *event,
                 GSLockPlug  *plug)
{
        gboolean capslock_on;

        restart_cancel_timeout (plug);

        capslock_on = is_capslock_on ();

        if (capslock_on != plug->priv->caps_lock_on) {
                capslock_update (plug, capslock_on);
        }

        /* if the input widget is visible and ready for input
         * then just carry on as usual
         */
        if (GTK_WIDGET_VISIBLE (plug->priv->auth_prompt_entry) &&
            GTK_WIDGET_IS_SENSITIVE (plug->priv->auth_prompt_entry)) {
                return FALSE;
        }

        if (strcmp (event->string, "") == 0) {
                return FALSE;
        }

        queue_key_event (plug, event);

        return TRUE;
}

/* adapted from gtk_dialog_add_button */
static GtkWidget *
gs_lock_plug_add_button (GSLockPlug  *plug,
                         GtkWidget   *action_area,
                         const gchar *button_text)
{
        GtkWidget *button;

        g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), NULL);
        g_return_val_if_fail (button_text != NULL, NULL);

        button = gtk_button_new_from_stock (button_text);

        GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);

        gtk_widget_show (button);

        gtk_box_pack_end (GTK_BOX (action_area),
                          button,
                          FALSE, TRUE, 0);

        return button;
}

static char *
get_user_display_name (void)
{
        const char *name;
        char       *utf8_name;

        name = g_get_real_name ();

        if (name == NULL || strcmp (name, "Unknown") == 0) {
                name = g_get_user_name ();
        }

        utf8_name = NULL;

        if (name != NULL) {
                utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
        }

        return utf8_name;
}

static char *
get_user_name (void)
{
        const char *name;
        char       *utf8_name;

        name = g_get_user_name ();

        utf8_name = NULL;
        if (name != NULL) {
                utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
        }

        return utf8_name;
}

static void
create_page_one_buttons (GSLockPlug *plug)
{

        gs_profile_start ("page one buttons");

        plug->priv->auth_switch_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   _("S_witch User..."));
        gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (plug->priv->auth_action_area),
                                            plug->priv->auth_switch_button,
                                            TRUE);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_switch_button), FALSE);
        gtk_widget_set_no_show_all (plug->priv->auth_switch_button, TRUE);

        plug->priv->auth_logout_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   _("Log _Out"));
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_logout_button), FALSE);
        gtk_widget_set_no_show_all (plug->priv->auth_logout_button, TRUE);

        plug->priv->auth_cancel_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   GTK_STOCK_CANCEL);
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_cancel_button), FALSE);

        plug->priv->auth_unlock_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
                                                                   plug->priv->auth_action_area,
                                                                   _("_Unlock"));
        gtk_button_set_focus_on_click (GTK_BUTTON (plug->priv->auth_unlock_button), FALSE);

        gtk_window_set_default (GTK_WINDOW (plug), plug->priv->auth_unlock_button);

        gs_profile_end ("page one buttons");
}

/* adapted from GDM */
static char *
expand_string (const char *text)
{
        GString       *str;
        const char    *p;
        char          *username;
        int            i;
        int            n_chars;
        struct utsname name;

        str = g_string_sized_new (strlen (text));

        p = text;
        n_chars = g_utf8_strlen (text, -1);
        i = 0;

        while (i < n_chars) {
                gunichar ch;

                ch = g_utf8_get_char (p);

                /* Backslash commands */
                if (ch == '\\') {
                        p = g_utf8_next_char (p);
                        i++;
                        ch = g_utf8_get_char (p);

                        if (i >= n_chars || ch == '\0') {
                                g_warning ("Unescaped \\ at end of text\n");
                                goto bail;
                        } else if (ch == 'n') {
                                g_string_append_unichar (str, '\n');
                        } else {
                                g_string_append_unichar (str, ch);
                        }
                } else if (ch == '%') {
                        p = g_utf8_next_char (p);
                        i++;
                        ch = g_utf8_get_char (p);

                        if (i >= n_chars || ch == '\0') {
                                g_warning ("Unescaped %% at end of text\n");
                                goto bail;
                        }

                        switch (ch) {
                        case '%':
                                g_string_append (str, "%");
                                break;
                        case 'c':
                                /* clock */
                                break;
                        case 'd':
                                /* display */
                                g_string_append (str, g_getenv ("DISPLAY"));
                                break;
                        case 'h':
                                /* hostname */
                                g_string_append (str, g_get_host_name ());
                                break;
                        case 'm':
                                /* machine name */
                                uname (&name);
                                g_string_append (str, name.machine);
                                break;
                        case 'n':
                                /* nodename */
                                uname (&name);
                                g_string_append (str, name.nodename);
                                break;
                        case 'r':
                                /* release */
                                uname (&name);
                                g_string_append (str, name.release);
                                break;
                        case 'R':
                                /* Real name */
                                username = get_user_display_name ();
                                g_string_append (str, username);
                                g_free (username);
                                break;
                        case 's':
                                /* system name */
                                uname (&name);
                                g_string_append (str, name.sysname);
                                break;
                        case 'U':
                                /* Username */
                                username = get_user_name ();
                                g_string_append (str, username);
                                g_free (username);
                                break;
                        default:
                                if (ch < 127) {
                                        g_warning ("unknown escape code %%%c in text\n", (char)ch);
                                } else {
                                        g_warning ("unknown escape code %%(U%x) in text\n", (int)ch);
                                }
                        }
                } else {
                        g_string_append_unichar (str, ch);
                }
                p = g_utf8_next_char (p);
                i++;
        }

 bail:

        return g_string_free (str, FALSE);
}

static void
expand_string_for_label (GtkWidget *label)
{
        const char *template;
        char       *str;

        template = gtk_label_get_label (GTK_LABEL (label));
        str = expand_string (template);
        gtk_label_set_label (GTK_LABEL (label), str);
        g_free (str);
}

static void
create_page_one (GSLockPlug *plug)
{
        GtkWidget            *align;
        GtkWidget            *vbox;
        GtkWidget            *vbox2;
        GtkWidget            *hbox;
        char                 *str;

        gs_profile_start ("page one");

        align = gtk_alignment_new (0.5, 0.5, 1, 1);
        gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), align, NULL);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        plug->priv->auth_face_image = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (vbox), plug->priv->auth_face_image, TRUE, TRUE, 0);
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_face_image), 0.5, 1.0);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

        str = g_strdup ("<span size=\"x-large\">%R</span>");
        plug->priv->auth_realname_label = gtk_label_new (str);
        g_free (str);
        expand_string_for_label (plug->priv->auth_realname_label);
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_realname_label), 0.5, 0.5);
        gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_realname_label), TRUE);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_realname_label, FALSE, FALSE, 0);

        /* To translators: This expands to USERNAME on HOSTNAME */
        str = g_strdup_printf ("<span size=\"small\">%s</span>", _("%U on %h"));
        plug->priv->auth_username_label = gtk_label_new (str);
        g_free (str);
        expand_string_for_label (plug->priv->auth_username_label);
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_username_label), 0.5, 0.5);
        gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_username_label), TRUE);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_username_label, FALSE, FALSE, 0);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox2, TRUE, TRUE, 0);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

        plug->priv->auth_prompt_label = gtk_label_new_with_mnemonic (_("_Password:"));
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_prompt_label), 0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), plug->priv->auth_prompt_label, FALSE, FALSE, 0);

        plug->priv->auth_prompt_entry = gtk_entry_new ();
        gtk_box_pack_start (GTK_BOX (hbox), plug->priv->auth_prompt_entry, TRUE, TRUE, 0);

        gtk_label_set_mnemonic_widget (GTK_LABEL (plug->priv->auth_prompt_label),
                                       plug->priv->auth_prompt_entry);

        plug->priv->auth_capslock_label = gtk_label_new ("");
        gtk_misc_set_alignment (GTK_MISC (plug->priv->auth_capslock_label), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_capslock_label, FALSE, FALSE, 0);

        /* Status text */

        plug->priv->auth_message_label = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX (vbox), plug->priv->auth_message_label,
                            FALSE, FALSE, 0);
        /* Buttons */
        plug->priv->auth_action_area = gtk_hbutton_box_new ();

        gtk_button_box_set_layout (GTK_BUTTON_BOX (plug->priv->auth_action_area),
                                   GTK_BUTTONBOX_END);

        gtk_box_pack_end (GTK_BOX (vbox), plug->priv->auth_action_area,
                          FALSE, TRUE, 0);
        gtk_widget_show (plug->priv->auth_action_area);

        create_page_one_buttons (plug);

        gs_profile_end ("page one");
}

static void
unlock_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_OK);
}

static void
cancel_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);
}

static void
switch_user_button_clicked (GtkButton  *button,
                            GSLockPlug *plug)
{

        remove_response_idle (plug);

        gs_lock_plug_set_sensitive (plug, FALSE);

        plug->priv->response_idle_id = g_timeout_add (2000,
                                                      (GSourceFunc)response_cancel_idle_cb,
                                                      plug);

        gs_lock_plug_set_busy (plug);
        do_user_switch (plug);
}

static char *
get_dialog_theme_name (GSLockPlug *plug)
{
        char        *name;
        GConfClient *client;

        client = gconf_client_get_default ();
        name = gconf_client_get_string (client, KEY_LOCK_DIALOG_THEME, NULL);
        g_object_unref (client);

        return name;
}

static gboolean
load_theme (GSLockPlug *plug)
{
        char       *theme;
        char       *filename;
        char       *gtkbuilder;
        char       *rc;
        GtkBuilder *builder;
        GtkWidget  *lock_dialog;
        GError     *error=NULL;

        theme = get_dialog_theme_name (plug);
        if (theme == NULL) {
                return FALSE;
        }

        filename = g_strdup_printf ("lock-dialog-%s.ui", theme);
        gtkbuilder = g_build_filename (GTKBUILDERDIR, filename, NULL);
        g_free (filename);
        if (! g_file_test (gtkbuilder, G_FILE_TEST_IS_REGULAR)) {
                g_free (gtkbuilder);
                g_free (theme);
                return FALSE;
        }

        filename = g_strdup_printf ("lock-dialog-%s.gtkrc", theme);
        g_free (theme);

        rc = g_build_filename (GTKBUILDERDIR, filename, NULL);
        g_free (filename);
        if (g_file_test (rc, G_FILE_TEST_IS_REGULAR)) {
                gtk_rc_parse (rc);
        }
        g_free (rc);

        builder = gtk_builder_new();

        if (!gtk_builder_add_from_file (builder,gtkbuilder,&error)) {
                g_warning ("Couldn't load builder file '%s': %s", gtkbuilder, error->message);
                g_error_free(error);
                g_free (gtkbuilder);
                return FALSE;
        }
        g_free (gtkbuilder);

        lock_dialog = GTK_WIDGET (gtk_builder_get_object(builder, "lock-dialog"));
        gtk_container_add (GTK_CONTAINER (plug), lock_dialog);

        plug->priv->vbox = NULL;
        plug->priv->notebook = GTK_WIDGET (gtk_builder_get_object(builder, "notebook"));

        plug->priv->auth_face_image = GTK_WIDGET (gtk_builder_get_object(builder, "auth-face-image"));
        plug->priv->auth_action_area = GTK_WIDGET (gtk_builder_get_object(builder, "auth-action-area"));
        plug->priv->auth_realname_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-realname-label"));
        plug->priv->auth_username_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-username-label"));
        plug->priv->auth_prompt_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-label"));
        plug->priv->auth_prompt_entry = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-entry"));
        plug->priv->auth_prompt_box = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-box"));
        plug->priv->auth_capslock_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-capslock-label"));
        plug->priv->auth_message_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-status-label"));
        plug->priv->auth_unlock_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-unlock-button"));
        plug->priv->auth_cancel_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-cancel-button"));
        plug->priv->auth_logout_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-logout-button"));
        plug->priv->auth_switch_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-switch-button"));
        plug->priv->auth_note_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-note-button"));
        plug->priv->note_tab = GTK_WIDGET (gtk_builder_get_object(builder, "note-tab"));
        plug->priv->note_tab_label = GTK_WIDGET (gtk_builder_get_object(builder, "note-tab-label"));
        plug->priv->note_ok_button = GTK_WIDGET (gtk_builder_get_object(builder, "note-ok-button"));
        plug->priv->note_text_view = GTK_WIDGET (gtk_builder_get_object(builder, "note-text-view"));
        plug->priv->note_cancel_button = GTK_WIDGET (gtk_builder_get_object(builder, "note-cancel-button"));

        /* Placeholder for the keyboard indicator */
        plug->priv->auth_prompt_kbd_layout_indicator = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-kbd-layout-indicator"));
        if (plug->priv->auth_logout_button != NULL) {
                gtk_widget_set_no_show_all (plug->priv->auth_logout_button, TRUE);
        }
        if (plug->priv->auth_switch_button != NULL) {
                gtk_widget_set_no_show_all (plug->priv->auth_switch_button, TRUE);
        }
        if (plug->priv->auth_note_button != NULL) {
                gtk_widget_set_no_show_all (plug->priv->auth_note_button, TRUE);
        }

        gtk_widget_show_all (lock_dialog);

        plug->priv->status_message_label = GTK_WIDGET (gtk_builder_get_object(builder, "status-message-label"));

        return TRUE;
}

static int
delete_handler (GSLockPlug  *plug,
                GdkEventAny *event,
                gpointer     data)
{
        gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);

        return TRUE; /* Do not destroy */
}

static void
on_note_text_buffer_changed (GtkTextBuffer *buffer,
                             GSLockPlug    *plug)
{
        int len;

        len = gtk_text_buffer_get_char_count (buffer);
        if (len > NOTE_BUFFER_MAX_CHARS) {
                gtk_widget_set_sensitive (plug->priv->note_text_view, FALSE);
        }
}

static void
gs_lock_plug_init (GSLockPlug *plug)
{
        gs_profile_start (NULL);

        plug->priv = GS_LOCK_PLUG_GET_PRIVATE (plug);

        clear_clipboards (plug);

#ifdef WITH_LIBNOTIFY
        plug->priv->leave_note_enabled = TRUE;
#else
        plug->priv->leave_note_enabled = FALSE;
#endif

        if (! load_theme (plug)) {
                gs_debug ("Unable to load theme!");

                plug->priv->vbox = gtk_vbox_new (FALSE, 0);

                gtk_container_add (GTK_CONTAINER (plug), plug->priv->vbox);

                /* Notebook */

                plug->priv->notebook = gtk_notebook_new ();
                gtk_notebook_set_show_tabs (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
                gtk_notebook_set_show_border (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
                gtk_box_pack_start (GTK_BOX (plug->priv->vbox), plug->priv->notebook, TRUE, TRUE, 0);

                /* Page 1 */

                create_page_one (plug);

                gtk_widget_show_all (plug->priv->vbox);
        }

        if (plug->priv->note_text_view != NULL) {
                GtkTextBuffer *buffer;
                buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (plug->priv->note_text_view));
                g_signal_connect (buffer, "changed", G_CALLBACK (on_note_text_buffer_changed), plug);
        }

        /* Layout indicator */
#ifdef WITH_KBD_LAYOUT_INDICATOR
        if (plug->priv->auth_prompt_kbd_layout_indicator != NULL) {
                XklEngine *engine;

                engine = xkl_engine_get_instance (GDK_DISPLAY ());
                if (xkl_engine_get_num_groups (engine) > 1) {
                        GtkWidget *layout_indicator;

                        layout_indicator = gkbd_indicator_new ();
                        gkbd_indicator_set_parent_tooltips (GKBD_INDICATOR (layout_indicator), TRUE);
                        gtk_box_pack_start (GTK_BOX (plug->priv->auth_prompt_kbd_layout_indicator),
                                            layout_indicator,
                                            FALSE,
                                            FALSE,
                                            6);

                        gtk_widget_show_all (layout_indicator);
                        gtk_widget_show (plug->priv->auth_prompt_kbd_layout_indicator);
                } else {
                        gtk_widget_hide (plug->priv->auth_prompt_kbd_layout_indicator);
                }

                g_object_unref (engine);
        }
#endif

        if (plug->priv->auth_note_button != NULL) {
                if (plug->priv->leave_note_enabled) {
                        gtk_widget_show_all (plug->priv->auth_note_button);
                } else {
                        gtk_widget_hide (plug->priv->auth_note_button);
                }
        }
        if (plug->priv->auth_switch_button != NULL) {
                if (plug->priv->switch_enabled) {
                        gtk_widget_show_all (plug->priv->auth_switch_button);
                } else {
                        gtk_widget_hide (plug->priv->auth_switch_button);
                }
        }

        gtk_widget_grab_default (plug->priv->auth_unlock_button);

        if (plug->priv->auth_username_label != NULL) {
                expand_string_for_label (plug->priv->auth_username_label);
        }

        if (plug->priv->auth_realname_label != NULL) {
                expand_string_for_label (plug->priv->auth_realname_label);
        }

        if (! plug->priv->logout_enabled || ! plug->priv->logout_command) {
                if (plug->priv->auth_logout_button != NULL) {
                        gtk_widget_hide (plug->priv->auth_logout_button);
                }
        }

        plug->priv->timeout = DIALOG_TIMEOUT_MSEC;

        g_signal_connect (plug, "key_press_event",
                          G_CALLBACK (entry_key_press), plug);

        /* button press handler used to inhibit popup menu */
        g_signal_connect (plug->priv->auth_prompt_entry, "button_press_event",
                          G_CALLBACK (entry_button_press), NULL);
        gtk_entry_set_activates_default (GTK_ENTRY (plug->priv->auth_prompt_entry), TRUE);
        gtk_entry_set_visibility (GTK_ENTRY (plug->priv->auth_prompt_entry), FALSE);

        g_signal_connect (plug->priv->auth_unlock_button, "clicked",
                          G_CALLBACK (unlock_button_clicked), plug);

        g_signal_connect (plug->priv->auth_cancel_button, "clicked",
                          G_CALLBACK (cancel_button_clicked), plug);

        if (plug->priv->status_message_label) {
                if (plug->priv->status_message) {
                        gtk_label_set_text (GTK_LABEL (plug->priv->status_message_label),
                                            plug->priv->status_message);
                }
                else {
                        gtk_widget_hide (plug->priv->status_message_label);
                }
        }

        if (plug->priv->auth_switch_button != NULL) {
                g_signal_connect (plug->priv->auth_switch_button, "clicked",
                                  G_CALLBACK (switch_user_button_clicked), plug);
        }

        if (plug->priv->auth_note_button != NULL) {
                g_signal_connect (plug->priv->auth_note_button, "clicked",
                                  G_CALLBACK (take_note), plug);
                g_signal_connect (plug->priv->note_ok_button, "clicked",
                                  G_CALLBACK (submit_note), plug);
                g_signal_connect (plug->priv->note_cancel_button, "clicked",
                                  G_CALLBACK (cancel_note), plug);
        }

        if (plug->priv->note_tab_label != NULL) {
                expand_string_for_label (plug->priv->note_tab_label);
        }

        if (plug->priv->auth_logout_button != NULL) {
                g_signal_connect (plug->priv->auth_logout_button, "clicked",
                                  G_CALLBACK (logout_button_clicked), plug);
        }

        g_signal_connect (plug, "delete_event", G_CALLBACK (delete_handler), NULL);

        gs_profile_end (NULL);
}

static void
gs_lock_plug_finalize (GObject *object)
{
        GSLockPlug *plug;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_LOCK_PLUG (object));

        plug = GS_LOCK_PLUG (object);

        g_return_if_fail (plug->priv != NULL);

        g_free (plug->priv->logout_command);

        remove_response_idle (plug);
        remove_cancel_timeout (plug);

        G_OBJECT_CLASS (gs_lock_plug_parent_class)->finalize (object);
}

GtkWidget *
gs_lock_plug_new (void)
{
        GtkWidget *result;

        result = g_object_new (GS_TYPE_LOCK_PLUG, NULL);

        gtk_window_set_focus_on_map (GTK_WINDOW (result), TRUE);

        return result;
}
