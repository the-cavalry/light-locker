/*
 *    gdm-queue.h
 *    (c) 2006 Thomas Thurman
 *    Based originally on GDMcommunication routines
 *    (c)2001 Queen of England, (c)2002 George Lebl
 *    
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *   
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *   
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#ifndef __GDM_QUEUE_H__
#define __GDM_QUEUE_H__ 1

#include <gdk/gdkscreen.h>

G_BEGIN_DECLS

/* ***************************************************************** *
 *  Based on gdm2/daemon/gdm.h                                       *
 *  Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>  *
 * ***************************************************************** */

#define GDM_CMD_VERSION			"VERSION"
#define GDM_CMD_AUTH_LOCAL		"AUTH_LOCAL %s"
#define GDM_CMD_FLEXI_XSERVER		"FLEXI_XSERVER"
#define GDM_CMD_FLEXI_XNEST		"FLEXI_XNEST %s %d %s %s"
#define GDM_CMD_CONSOLE_SERVERS		"CONSOLE_SERVERS"
#define GDM_CMD_GET_CONFIG       	"GET_CONFIG %s" 
#define GDM_CMD_GET_CONFIG_FILE		"GET_CONFIG_FILE"
#define GDM_CMD_ALL_SERVERS		"ALL_SERVERS"
#define GDM_CMD_UPDATE_CONFIG		"UPDATE_CONFIG %s"
#define GDM_CMD_GREETERPIDS		"GREETERPIDS"
#define GDM_CMD_QUERY_LOGOUT_ACTION	"QUERY_LOGOUT_ACTION"
#define GDM_CMD_SET_LOGOUT_ACTION	"SET_LOGOUT_ACTION"
#define GDM_CMD_SET_SAFE_LOGOUT_ACTION	"SET_SAFE_LOGOUT_ACTION"
#define GDM_CMD_LOGOUT_ACTION_NONE	"NONE"
#define GDM_CMD_LOGOUT_ACTION_HALT	"HALT"
#define GDM_CMD_LOGOUT_ACTION_REBOOT	"REBOOT"
#define GDM_CMD_LOGOUT_ACTION_SUSPEND	"SUSPEND"
#define GDM_CMD_QUERY_VT       		"QUERY_VT"
#define GDM_CMD_SET_VT			"SET_VT %d"
#define GDM_CMD_CLOSE        		"CLOSE"

typedef enum {
  GDM_RESULT_OK,
  GDM_RESULT_ERROR,
  GDM_RESULT_BIZARRE,
} GdmResultState;

typedef void (GdmMessageCallback)(GdmResultState is_ok,
    const gchar *answer, gpointer data);

/* This is the main external entrance to this part of the program.
 * 
 * It is an instruction to (at some future point) possibly send a certain
 * string to GDM, and when an answer is received, possibly send the answer
 * to a certain function.
 *
 * |callback| is a callback to send the answer to, when we get it.
 * If this is NULL, any answer will be thrown away.
 * Otherwise, the function is passed:
 *  1) A status flag, for "ok", "error" or (for error conditions) "bizarre".
 *  2) The string received from GDM. The leading "OK" or "ERROR" plus a
 *     space will be removed (and turned into the status flag just
 *     mentioned). If the result from GDM is *only* "OK" or "ERROR",
 *     or if there was actually no GDM request because the query was NULL,
 *     this will be NULL.
 *  3) An arbitrary pointer-- see below.
 *
 * |data| is arbitrary data to pass to the callback.
 * 
 * |query| is the query to send to GDM. You can use printf-style formatting.
 * If this is NULL, GDM won't hear anything about this step, but the
 * callback (if any) will be called anyway, just as if GDM had returned "OK".
 */
void ask_gdm (GdmMessageCallback *callback, gpointer data, gchar *query, ...);

/* This is the other main entrance to this part of the program, as well as
 * ask_gdm. It adds an authorisation step to the gdm_queue, which may involve
 * trying many keys and seeing which one works; this is not the concern of the
 * caller. Once a good key is found, it will be remembered for next time.
 *
 * Note that if you want to test error handling, commenting out the body of
 * this function is a good way to test it.
 */
void queue_authentication (GdkScreen *screen);

/* This just gets a cookie of MIT-MAGIC-COOKIE-1 type */
gchar *get_mit_magic_cookie (GdkScreen *screen, gboolean binary);

G_END_DECLS

#endif /* !__GDM_QUEUE_H__ */
