/* vim: set noet ts=8 sts=8 sw=8 :
 *
 * Copyright © 2010 Saleem Abdulrasool <compnerd@compnerd.org>
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
 */

#ifndef __GS_BUS_H
#define __GS_BUS_H

/* systemd logind */
#define SYSTEMD_LOGIND_SERVICE          "org.freedesktop.login1"
#define SYSTEMD_LOGIND_PATH             "/org/freedesktop/login1"
#define SYSTEMD_LOGIND_INTERFACE        "org.freedesktop.login1.Manager"

#define SYSTEMD_LOGIND_SESSION_INTERFACE "org.freedesktop.login1.Session"

/* ConsoleKit */
#define CK_SERVICE                      "org.freedesktop.ConsoleKit"
#define CK_PATH                         "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE                    "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH                 CK_PATH "/Manager"
#define CK_MANAGER_INTERFACE            CK_INTERFACE ".Manager"

#define CK_SESSION_PATH                 CK_PATH "/Session"
#define CK_SESSION_INTERFACE            CK_INTERFACE ".Session"

/* UPower */
#define UP_SERVICE                      "org.freedesktop.UPower"
#define UP_PATH                         "/org/freedesktop/UPower"
#define UP_INTERFACE                    "org.freedesktop.UPower"

/* lightDM */
#define DM_SERVICE                      "org.freedesktop.DisplayManager"

#define DM_SEAT_INTERFACE               "org.freedesktop.DisplayManager.Seat"

#define DM_SESSION_PATH                 getenv("XDG_SESSION_PATH")
#define DM_SESSION_INTERFACE            "org.freedesktop.DisplayManager.Session"

#define DBUS_PROPERTIES_INTERFACE       "org.freedesktop.DBus.Properties"

/* DBus */
#define DBUS_SERVICE                    "org.freedesktop.DBus"
#define DBUS_PATH                       "/org/freedesktop/DBus"
#define DBUS_INTERFACE                  "org.freedesktop.DBus"
#define DBUS_INTROSPECTABLE_INTERFACE   "org.freedesktop.DBus.Introspectable"

/* Screensaver */
#define GS_SERVICE                      "org.freedesktop.ScreenSaver"
#define GS_PATH                         "/org/freedesktop/ScreenSaver"
#define GS_PATH_KDE                     "/ScreenSaver"
#define GS_INTERFACE                    "org.freedesktop.ScreenSaver"

#define GS_SERVICE_GNOME                "org.gnome.ScreenSaver"
#define GS_PATH_GNOME                   "/org/gnome/ScreenSaver"
#define GS_INTERFACE_GNOME              "org.gnome.ScreenSaver"

#endif

