/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#ifdef HAVE_XF86MISCSETGRABKEYSSTATE
# include <X11/extensions/xf86misc.h>
#endif /* HAVE_XF86MISCSETGRABKEYSSTATE */

#include "gs-window.h"
#include "gs-grab.h"
#include "gs-debug.h"

static void     gs_grab_class_init (GSGrabClass *klass);
static void     gs_grab_init       (GSGrab      *grab);
static void     gs_grab_finalize   (GObject        *object);

#define GS_GRAB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_GRAB, GSGrabPrivate))

G_DEFINE_TYPE (GSGrab, gs_grab, G_TYPE_OBJECT)

static gpointer grab_object = NULL;

struct GSGrabPrivate
{
        GDBusConnection *session_bus;

        guint      mouse_hide_cursor : 1;
        GdkWindow *mouse_grab_window;
        GdkWindow *keyboard_grab_window;
        GdkScreen *mouse_grab_screen;
        GdkScreen *keyboard_grab_screen;

        GtkWidget *invisible;
};

static const char *
grab_string (int status)
{
        switch (status) {
        case GDK_GRAB_SUCCESS:          return "GrabSuccess";
        case GDK_GRAB_ALREADY_GRABBED:  return "AlreadyGrabbed";
        case GDK_GRAB_INVALID_TIME:     return "GrabInvalidTime";
        case GDK_GRAB_NOT_VIEWABLE:     return "GrabNotViewable";
        case GDK_GRAB_FROZEN:           return "GrabFrozen";
        default:
                {
                        static char foo [255];
                        sprintf (foo, "unknown status: %d", status);
                        return foo;
                }
        }
}

#ifdef HAVE_XF86MISCSETGRABKEYSSTATE
/* This function enables and disables the Ctrl-Alt-KP_star and 
   Ctrl-Alt-KP_slash hot-keys, which (in XFree86 4.2) break any
   grabs and/or kill the grabbing client.  That would effectively
   unlock the screen, so we don't like that.

   The Ctrl-Alt-KP_star and Ctrl-Alt-KP_slash hot-keys only exist
   if AllowDeactivateGrabs and/or AllowClosedownGrabs are turned on
   in XF86Config.  I believe they are disabled by default.

   This does not affect any other keys (specifically Ctrl-Alt-BS or
   Ctrl-Alt-F1) but I wish it did.  Maybe it will someday.
 */
static void
xorg_lock_smasher_set_active (GSGrab  *grab,
                              gboolean active)
{
        int status, event, error;

        if (!XF86MiscQueryExtension (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &event, &error)) {
                gs_debug ("No XFree86-Misc extension present");
                return;
        }

        if (active) {
                gs_debug ("Enabling the x.org grab smasher");
        } else {
                gs_debug ("Disabling the x.org grab smasher");
        }

        gdk_error_trap_push ();

        status = XF86MiscSetGrabKeysState (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), active);

        gdk_display_sync (gdk_display_get_default ());
        error = gdk_error_trap_pop ();

        if (active && status == MiscExtGrabStateAlready) {
                /* shut up, consider this success */
                status = MiscExtGrabStateSuccess;
        }

        if (error == Success) {
                gs_debug ("XF86MiscSetGrabKeysState(%s) returned %s\n",
                          active ? "on" : "off",
                          (status == MiscExtGrabStateSuccess ? "MiscExtGrabStateSuccess" :
                           status == MiscExtGrabStateLocked  ? "MiscExtGrabStateLocked"  :
                           status == MiscExtGrabStateAlready ? "MiscExtGrabStateAlready" :
                           "unknown value"));
        } else {
                gs_debug ("XF86MiscSetGrabKeysState(%s) failed with error code %d\n",
                          active ? "on" : "off", error);
        }
}
#else
static void
xorg_lock_smasher_set_active (GSGrab  *grab,
                              gboolean active)
{
}
#endif /* HAVE_XF86MISCSETGRABKEYSSTATE */

static int
gs_grab_get_keyboard (GSGrab    *grab,
                      GdkWindow *window,
                      GdkScreen *screen)
{
        GdkGrabStatus status = 0;

        g_return_val_if_fail (window != NULL, FALSE);
        g_return_val_if_fail (screen != NULL, FALSE);

        gs_debug ("Grabbing keyboard widget=%X", (guint32) GDK_WINDOW_XID (window));

#if GTK_CHECK_VERSION(3, 0, 0)
        GList *list, *link;
        GdkDisplay *display = gdk_window_get_display (window);
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

        for (link = list; link != NULL; link = g_list_next (link)) {
                GdkDevice *device = GDK_DEVICE (link->data);

                if (gdk_device_get_source (device) != GDK_SOURCE_KEYBOARD)
                        continue;

                status = gdk_device_grab (device,
                                          window,
                                          GDK_OWNERSHIP_NONE,
                                          FALSE,
                                          GDK_KEY_PRESS_MASK |
                                          GDK_KEY_RELEASE_MASK,
                                          NULL,
                                          GDK_CURRENT_TIME);
        }
        g_list_free(list);
#else
        status = gdk_keyboard_grab (window, FALSE, GDK_CURRENT_TIME);
#endif

        if (status == GDK_GRAB_SUCCESS) {
                if (grab->priv->keyboard_grab_window != NULL) {
                        g_object_remove_weak_pointer (G_OBJECT (grab->priv->keyboard_grab_window),
                                                      (gpointer *) &grab->priv->keyboard_grab_window);
                }
                grab->priv->keyboard_grab_window = window;

                g_object_add_weak_pointer (G_OBJECT (grab->priv->keyboard_grab_window),
                                           (gpointer *) &grab->priv->keyboard_grab_window);

                grab->priv->keyboard_grab_screen = screen;
        } else {
                gs_debug ("Couldn't grab keyboard!  (%s)", grab_string (status));
        }

        return status;
}

static int
gs_grab_get_mouse (GSGrab    *grab,
                   GdkWindow *window,
                   GdkScreen *screen,
                   gboolean   hide_cursor)
{
        GdkGrabStatus status = 0;
        GdkCursor    *cursor;

        g_return_val_if_fail (window != NULL, FALSE);
        g_return_val_if_fail (screen != NULL, FALSE);

        cursor = gdk_cursor_new (GDK_BLANK_CURSOR);

        gs_debug ("Grabbing mouse widget=%X", (guint32) GDK_WINDOW_XID (window));
#if GTK_CHECK_VERSION(3, 0, 0)
        GList *list, *link;
        GdkDisplay *display = gdk_window_get_display (window);
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

        for (link = list; link != NULL; link = g_list_next (link)) {
                GdkDevice *device = GDK_DEVICE (link->data);

                if (gdk_device_get_source (device) != GDK_SOURCE_MOUSE)
                        continue;

                status = gdk_device_grab (device,
                                          window,
                                          GDK_OWNERSHIP_NONE,
                                          FALSE,
                                          GDK_KEY_PRESS_MASK |
                                          GDK_KEY_RELEASE_MASK,
                                          (hide_cursor ? cursor : NULL),
                                          GDK_CURRENT_TIME);
        }
        g_list_free(list);
#else
        status = gdk_pointer_grab (window, TRUE, 0, NULL,
                                   (hide_cursor ? cursor : NULL),
                                   GDK_CURRENT_TIME);
#endif

        if (status == GDK_GRAB_SUCCESS) {
                if (grab->priv->mouse_grab_window != NULL) {
                        g_object_remove_weak_pointer (G_OBJECT (grab->priv->mouse_grab_window),
                                                      (gpointer *) &grab->priv->mouse_grab_window);
                }
                grab->priv->mouse_grab_window = window;

                g_object_add_weak_pointer (G_OBJECT (grab->priv->mouse_grab_window),
                                           (gpointer *) &grab->priv->mouse_grab_window);

                grab->priv->mouse_grab_screen = screen;
                grab->priv->mouse_hide_cursor = hide_cursor;
        }

#if GTK_CHECK_VERSION(3, 0, 0)
        g_object_unref (cursor);
#else
        gdk_cursor_unref (cursor);
#endif

        return status;
}

void
gs_grab_keyboard_reset (GSGrab *grab)
{
        if (grab->priv->keyboard_grab_window != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (grab->priv->keyboard_grab_window),
                                              (gpointer *) &grab->priv->keyboard_grab_window);
        }
        grab->priv->keyboard_grab_window = NULL;
        grab->priv->keyboard_grab_screen = NULL;
}

static gboolean
gs_grab_release_keyboard (GSGrab *grab)
{
        gs_debug ("Ungrabbing keyboard");

#if GTK_CHECK_VERSION(3, 0, 0)
        GList *list, *link;
        GdkDisplay *display = gdk_display_get_default ();
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

        for (link = list; link != NULL; link = g_list_next (link)) {
                GdkDevice *device = GDK_DEVICE (link->data);

                if (gdk_device_get_source (device) != GDK_SOURCE_KEYBOARD)
                        continue;

                gdk_device_ungrab(device, GDK_CURRENT_TIME);
        }
        g_list_free(list);
#else
        gdk_keyboard_ungrab (GDK_CURRENT_TIME);
#endif

        gs_grab_keyboard_reset (grab);

        return TRUE;
}

void
gs_grab_mouse_reset (GSGrab *grab)
{
        if (grab->priv->mouse_grab_window != NULL) {
                g_object_remove_weak_pointer (G_OBJECT (grab->priv->mouse_grab_window),
                                              (gpointer *) &grab->priv->mouse_grab_window);
        }

        grab->priv->mouse_grab_window = NULL;
        grab->priv->mouse_grab_screen = NULL;
}

gboolean
gs_grab_release_mouse (GSGrab *grab)
{
        gs_debug ("Ungrabbing pointer");

#if GTK_CHECK_VERSION(3, 0, 0)
        GList *list, *link;
        GdkDisplay *display = gdk_display_get_default ();
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

        for (link = list; link != NULL; link = g_list_next (link)) {
                GdkDevice *device = GDK_DEVICE (link->data);

                if (gdk_device_get_source (device) != GDK_SOURCE_MOUSE)
                        continue;

                gdk_device_ungrab(device, GDK_CURRENT_TIME);
        }
        g_list_free(list);
#else
        gdk_pointer_ungrab (GDK_CURRENT_TIME);
#endif

        gs_grab_mouse_reset (grab);

        return TRUE;
}

static gboolean
gs_grab_move_mouse (GSGrab    *grab,
                    GdkWindow *window,
                    GdkScreen *screen,
                    gboolean   hide_cursor)
{
        gboolean   result;
        GdkWindow *old_window;
        GdkScreen *old_screen;
        gboolean   old_hide_cursor;

        /* if the pointer is not grabbed and we have a
           mouse_grab_window defined then we lost the grab */
#if GTK_CHECK_VERSION(3, 0, 0)
        GList *list, *link;
        GdkDisplay *display = gdk_display_get_default ();
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        list = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);
        for (link = list; link != NULL; link = g_list_next (link)) {
                GdkDevice *device = GDK_DEVICE (link->data);

                if (gdk_device_get_source (device) != GDK_SOURCE_MOUSE)
                        continue;

                if (! gdk_display_device_is_grabbed(display, device)) {
                        gs_grab_mouse_reset (grab);
                }
        }
        g_list_free(list);
#else
        if (! gdk_pointer_is_grabbed ()) {
                gs_grab_mouse_reset (grab);
        }
#endif

        if (grab->priv->mouse_grab_window == window) {
                gs_debug ("Window %X is already grabbed, skipping",
                          (guint32) GDK_WINDOW_XID (grab->priv->mouse_grab_window));
                return TRUE;
        }

#if 0
        gs_debug ("Intentionally skipping move pointer grabs");
        /* FIXME: GTK doesn't like having the pointer grabbed */
        return TRUE;
#else
        if (grab->priv->mouse_grab_window) {
                gs_debug ("Moving pointer grab from %X to %X",
                          (guint32) GDK_WINDOW_XID (grab->priv->mouse_grab_window),
                          (guint32) GDK_WINDOW_XID (window));
        } else {
                gs_debug ("Getting pointer grab on %X",
                          (guint32) GDK_WINDOW_XID (window));
        }
#endif

        gs_debug ("*** doing X server grab");
        gdk_x11_grab_server ();

        old_window = grab->priv->mouse_grab_window;
        old_screen = grab->priv->mouse_grab_screen;
        old_hide_cursor = grab->priv->mouse_hide_cursor;

        if (old_window) {
                gs_grab_release_mouse (grab);
        }

        result = gs_grab_get_mouse (grab, window, screen, hide_cursor);

        if (result != GDK_GRAB_SUCCESS) {
                sleep (1);
                result = gs_grab_get_mouse (grab, window, screen, hide_cursor);
        }

        if ((result != GDK_GRAB_SUCCESS) && old_window) {
                gs_debug ("Could not grab mouse for new window.  Resuming previous grab.");
                gs_grab_get_mouse (grab, old_window, old_screen, old_hide_cursor);
        }

        gs_debug ("*** releasing X server grab");
        gdk_x11_ungrab_server ();
        gdk_flush ();

        return (result == GDK_GRAB_SUCCESS);
}

static gboolean
gs_grab_move_keyboard (GSGrab    *grab,
                       GdkWindow *window,
                       GdkScreen *screen)
{
        gboolean   result;
        GdkWindow *old_window;
        GdkScreen *old_screen;

        if (grab->priv->keyboard_grab_window == window) {
                gs_debug ("Window %X is already grabbed, skipping",
                          (guint32) GDK_WINDOW_XID (grab->priv->keyboard_grab_window));
                return TRUE;
        }

        if (grab->priv->keyboard_grab_window != NULL) {
                gs_debug ("Moving keyboard grab from %X to %X",
                          (guint32) GDK_WINDOW_XID (grab->priv->keyboard_grab_window),
                          (guint32) GDK_WINDOW_XID (window));
        } else {
                gs_debug ("Getting keyboard grab on %X",
                          (guint32) GDK_WINDOW_XID (window));

        }

        gs_debug ("*** doing X server grab");
        gdk_x11_grab_server ();

        old_window = grab->priv->keyboard_grab_window;
        old_screen = grab->priv->keyboard_grab_screen;

        if (old_window) {
                gs_grab_release_keyboard (grab);
        }

        result = gs_grab_get_keyboard (grab, window, screen);

        if (result != GDK_GRAB_SUCCESS) {
                sleep (1);
                result = gs_grab_get_keyboard (grab, window, screen);
        }

        if ((result != GDK_GRAB_SUCCESS) && old_window) {
                gs_debug ("Could not grab keyboard for new window.  Resuming previous grab.");
                gs_grab_get_keyboard (grab, old_window, old_screen);
        }

        gs_debug ("*** releasing X server grab");
        gdk_x11_ungrab_server ();
        gdk_flush ();

        return (result == GDK_GRAB_SUCCESS);
}

static void
gs_grab_nuke_focus (void)
{
        Window focus = 0;
        int    rev = 0;

        gs_debug ("Nuking focus");

        gdk_error_trap_push ();

        XGetInputFocus (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &focus, &rev);

        XSetInputFocus (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), None, RevertToNone, CurrentTime);

#if GTK_CHECK_VERSION(3, 0, 0)
        gdk_error_trap_pop_ignored ();
#else
        gdk_error_trap_pop ();
#endif
}

void
gs_grab_release (GSGrab *grab)
{
        gs_debug ("Releasing all grabs");

        gs_grab_release_mouse (grab);
        gs_grab_release_keyboard (grab);

        /* FIXME: is it right to enable this ? */
        xorg_lock_smasher_set_active (grab, TRUE);

        gdk_display_sync (gdk_display_get_default ());
        gdk_flush ();
}

/* The GNOME 3 Shell holds an X grab when we're in the overview;
 * ask it to bounce out before we try locking the screen.
 */
static void
request_shell_exit_overview (GSGrab *grab)
{
        GDBusMessage *message;

        /* Shouldn't happen, but... */
        if (!grab->priv->session_bus)
                return;

        message = g_dbus_message_new_method_call ("org.gnome.Shell",
                                                  "/org/gnome/Shell",
                                                  "org.freedesktop.DBus.Properties",
                                                  "Set");
        g_dbus_message_set_body (message,
                                 g_variant_new ("(ssv)",
                                                "org.gnome.Shell",
                                                "OverviewActive",
                                                g_variant_new ("b",
                                                               FALSE)));

        g_dbus_connection_send_message (grab->priv->session_bus,
                                        message,
                                        G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                        NULL,
                                        NULL);
        g_object_unref (message);
}

gboolean
gs_grab_grab_window (GSGrab    *grab,
                     GdkWindow *window,
                     GdkScreen *screen,
                     gboolean   hide_cursor)
{
        gboolean mstatus = FALSE;
        gboolean kstatus = FALSE;
        int      i;
        int      retries = 4;
        gboolean focus_fuckus = FALSE;

        /* First, have stuff we control in GNOME un-grab */
        request_shell_exit_overview (grab);

 AGAIN:

        for (i = 0; i < retries; i++) {
                kstatus = gs_grab_get_keyboard (grab, window, screen);
                if (kstatus == GDK_GRAB_SUCCESS) {
                        break;
                }

                /* else, wait a second and try to grab again. */
                sleep (1);
        }

        if (kstatus != GDK_GRAB_SUCCESS) {
                if (!focus_fuckus) {
                        focus_fuckus = TRUE;
                        gs_grab_nuke_focus ();
                        goto AGAIN;
                }
        }

        for (i = 0; i < retries; i++) {
                mstatus = gs_grab_get_mouse (grab, window, screen, hide_cursor);
                if (mstatus == GDK_GRAB_SUCCESS) {
                        break;
                }

                /* else, wait a second and try to grab again. */
                sleep (1);
        }

        if (mstatus != GDK_GRAB_SUCCESS) {
                gs_debug ("Couldn't grab pointer!  (%s)",
                          grab_string (mstatus));
        }

#if 0
        /* FIXME: release the pointer grab so GTK will work */
        gs_grab_release_mouse (grab);
#endif

        /* When should we allow blanking to proceed?  The current theory
           is that both a keyboard grab and a mouse grab are mandatory

           - If we don't have a keyboard grab, then we won't be able to
           read a password to unlock, so the kbd grab is manditory.

           - If we don't have a mouse grab, then we might not see mouse
           clicks as a signal to unblank, on-screen widgets won't work ideally,
           and gs_grab_move_to_window() will spin forever when it gets called.
        */

        if (kstatus != GDK_GRAB_SUCCESS || mstatus != GDK_GRAB_SUCCESS) {
                /* Do not blank without a keyboard and mouse grabs. */

                /* Release keyboard or mouse which was grabbed. */
                if (kstatus == GDK_GRAB_SUCCESS) {
                        gs_grab_release_keyboard (grab);
                }
                if (mstatus == GDK_GRAB_SUCCESS) {
                        gs_grab_release_mouse (grab);
                }

                return FALSE;
        }

        /* Grab is good, go ahead and blank.  */
        return TRUE;
}

/* this is used to grab the keyboard and mouse to the root */
gboolean
gs_grab_grab_root (GSGrab  *grab,
                   gboolean hide_cursor)
{
        GdkDisplay *display;
        GdkWindow  *root;
        GdkScreen  *screen;
        gboolean    res;

        gs_debug ("Grabbing the root window");

        display = gdk_display_get_default ();
#if GTK_CHECK_VERSION(3, 0, 0)
        GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
        GdkDevice *pointer = gdk_device_manager_get_client_pointer (device_manager);
        gint x = -1, y = -1;
        gdk_device_get_position (pointer, &screen, &x, &y);
#else
        gdk_display_get_pointer (display, &screen, NULL, NULL, NULL);
#endif
        root = gdk_screen_get_root_window (screen);

        res = gs_grab_grab_window (grab, root, screen, hide_cursor);

        return res;
}

/* this is used to grab the keyboard and mouse to an offscreen window */
gboolean
gs_grab_grab_offscreen (GSGrab *grab,
                        gboolean hide_cursor)
{
        GdkScreen *screen;
        gboolean   res;

        gs_debug ("Grabbing an offscreen window");

        screen = gtk_invisible_get_screen (GTK_INVISIBLE (grab->priv->invisible));
        res = gs_grab_grab_window (grab, gtk_widget_get_window (grab->priv->invisible), screen, hide_cursor);

        return res;
}

/* This is similar to gs_grab_grab_window but doesn't fail */
void
gs_grab_move_to_window (GSGrab    *grab,
                        GdkWindow *window,
                        GdkScreen *screen,
                        gboolean   hide_cursor)
{
        gboolean result = FALSE;

        g_return_if_fail (GS_IS_GRAB (grab));

        xorg_lock_smasher_set_active (grab, FALSE);

        do {
                result = gs_grab_move_keyboard (grab, window, screen);
                gdk_flush ();
        } while (!result);

        do {
                result = gs_grab_move_mouse (grab, window, screen, hide_cursor);
                gdk_flush ();
        } while (!result);
}

static void
gs_grab_class_init (GSGrabClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gs_grab_finalize;

        g_type_class_add_private (klass, sizeof (GSGrabPrivate));
}

static void
gs_grab_init (GSGrab *grab)
{
        grab->priv = GS_GRAB_GET_PRIVATE (grab);

        grab->priv->session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

        grab->priv->mouse_hide_cursor = FALSE;
        grab->priv->invisible = gtk_invisible_new ();
        gtk_widget_show (grab->priv->invisible);
}

static void
gs_grab_finalize (GObject *object)
{
        GSGrab *grab;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_GRAB (object));

        grab = GS_GRAB (object);

        g_object_unref (grab->priv->session_bus);

        g_return_if_fail (grab->priv != NULL);

        gtk_widget_destroy (grab->priv->invisible);

        G_OBJECT_CLASS (gs_grab_parent_class)->finalize (object);
}

GSGrab *
gs_grab_new (void)
{
        if (grab_object) {
                g_object_ref (grab_object);
        } else {
                grab_object = g_object_new (GS_TYPE_GRAB, NULL);
                g_object_add_weak_pointer (grab_object,
                                           (gpointer *) &grab_object);
        }

        return GS_GRAB (grab_object);
}
