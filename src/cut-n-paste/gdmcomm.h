/*
 *    GDMcommunication routines
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

#ifndef __GDMCOMM_H__
#define __GDMCOMM_H__ 1

#include <gdk/gdkscreen.h>

G_BEGIN_DECLS

/* ***************************************************************** *
 *  From gdm2/daemon/gdm.h                                           *
 *  Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>  *
 * ***************************************************************** */

#define GDM_SUP_SOCKET			"/tmp/.gdm_socket"
#define GDM_SUP_VERSION			"VERSION"			/* None */
#define GDM_SUP_AUTH_LOCAL		"AUTH_LOCAL"			/* <xauth cookie> */
#define GDM_SUP_FLEXI_XSERVER		"FLEXI_XSERVER"			/* <xserver type> */
#define GDM_SUP_FLEXI_XNEST		"FLEXI_XNEST"			/* <display> <uid> <xauth cookie> <xauth file> */
#define GDM_SUP_CONSOLE_SERVERS		"CONSOLE_SERVERS"		/* None */
#define GDM_SUP_GET_CONFIG_FILE		"GET_CONFIG_FILE"		/* None */
#define GDM_SUP_ALL_SERVERS		"ALL_SERVERS"			/* None */
#define GDM_SUP_UPDATE_CONFIG		"UPDATE_CONFIG"			/* <key> */
#define GDM_SUP_GREETERPIDS		"GREETERPIDS"			/* None */
#define GDM_SUP_QUERY_LOGOUT_ACTION	"QUERY_LOGOUT_ACTION"		/* None */
#define GDM_SUP_SET_LOGOUT_ACTION	"SET_LOGOUT_ACTION"		/* <action> */
#define GDM_SUP_SET_SAFE_LOGOUT_ACTION	"SET_SAFE_LOGOUT_ACTION"	/* <action> */
#define GDM_SUP_LOGOUT_ACTION_NONE	"NONE"				/* None */
#define GDM_SUP_LOGOUT_ACTION_HALT	"HALT"				/* None */
#define GDM_SUP_LOGOUT_ACTION_REBOOT	"REBOOT"			/* None */
#define GDM_SUP_LOGOUT_ACTION_SUSPEND	"SUSPEND"			/* None */
#define GDM_SUP_QUERY_VT       		"QUERY_VT"			/* None */
#define GDM_SUP_SET_VT			"SET_VT"			/* <vt> */
#define GDM_SUP_CLOSE        		"CLOSE"				/* None */


gchar       *gdmcomm_call_gdm          (const gchar *command,
					const gchar *auth_cookie,
					const gchar *min_version,
					gint         tries);
/* This just gets a cookie of MIT-MAGIC-COOKIE-1 type */
gchar       *gdmcomm_get_a_cookie      (GdkScreen   *screen,
					gboolean     binary);
/* get the gdm auth cookie */
const gchar *gdmcomm_get_auth_cookie   (GdkScreen   *screen);

gboolean     gdmcomm_check             (gboolean     gui_bitching);
const gchar *gdmcomm_get_error_message (const gchar *ret,
					gboolean     use_xnest);

G_END_DECLS

#endif /* !__GDMCOMM_H__ */
