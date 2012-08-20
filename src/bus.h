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

#ifndef bus_h
#define bus_h

/* systemd logind */
#define SYSTEMD_LOGIND_SERVICE          "org.freedesktop.login1"
#define SYSTEMD_LOGIND_PATH             "/org/freedesktop/login1"
#define SYSTEMD_LOGIND_INTERFACE        "org.freedesktop.login1.Manager"

#define SYSTEMD_LOGIND_SESSION_INTERFACE "org.freedesktop.login1.Session"
#define SYSTEMD_LOGIND_SESSION_PATH     "/org/freedesktop/login1/session"

/* ConsoleKit */
#define CK_SERVICE                      "org.freedesktop.ConsoleKit"
#define CK_PATH                         "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE                    "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH                 CK_PATH "/Manager"
#define CK_MANAGER_INTERFACE            CK_INTERFACE ".Manager"

#define CK_SESSION_PATH                 CK_PATH "/Session"
#define CK_SESSION_INTERFACE            CK_INTERFACE ".Session"

/* DBus */
#define DBUS_SERVICE                    "org.freedesktop.DBus"
#define DBUS_PATH                       "/org/freedesktop/DBus"
#define DBUS_INTERFACE                  "org.freedesktop.DBus"

/* Gnome Screensaver */
#define GS_SERVICE                      "org.gnome.ScreenSaver"
#define GS_PATH                         "/org/gnome/ScreenSaver"
#define GS_INTERFACE                    "org.gnome.ScreenSaver"

/* Gnome Session Manager */
#define GSM_SERVICE                     "org.gnome.SessionManager"
#define GSM_PATH                        "/org/gnome/SessionManager"
#define GSM_INTERFACE                   "org.gnome.SessionManager"

#define GSM_PRESENCE_PATH               GSM_PATH "/Presence"
#define GSM_PRESENCE_INTERFACE          GSM_INTERFACE ".Presence"

#endif

