/*
 *    GDMcommunication routines
 *    (c)2001 Queen of England, (c)2002,2003 George Lebl
 *    (c)2005 James M. Cape.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>

#include <X11/Xauth.h>

#include <gdk/gdkdisplay.h>

#include <glib/gi18n.h>

#include "gdmcomm.h"


/* ***************************************************************** *
 *  From gdm2/vicious-extensions/ve-misc.h                           *
 *  Copyright (c) 2000 Eazel, Inc., (c) 2001,2002 George Lebl        *
 * ***************************************************************** */

#define VE_IGNORE_EINTR(expr) \
  do {		\
    errno = 0;	\
    expr;		\
  } while G_UNLIKELY (errno == EINTR);


static const gboolean debug = FALSE;

static char *
do_command (int         fd,
	    const char *command,
	    gboolean    get_response)
{
	GString *str;
	char buf[1];
	char *cstr;
	int ret;
#ifndef MSG_NOSIGNAL
	void (*old_handler)(int);
#endif

	if (debug)
		g_print ("Sending command: '%s'\n", command);

	cstr = g_strdup_printf ("%s\n", command);

#ifdef MSG_NOSIGNAL
	ret = send (fd, cstr, strlen (cstr), MSG_NOSIGNAL);
#else
	old_handler = signal (SIGPIPE, SIG_IGN);
	ret = send (fd, cstr, strlen (cstr), 0);
	signal (SIGPIPE, old_handler);
#endif
	g_free (cstr);

	if (ret < 0)
		return NULL;

	if ( ! get_response)
		return NULL;

	str = g_string_sized_new (10);
	while (read (fd, buf, 1) == 1 &&
	       buf[0] != '\n') {
		g_string_append_c (str, buf[0]);
	}

	if (debug)
		g_print ("  Got response: '%s'\n", str->str);

	cstr = str->str;
	g_string_free (str, FALSE);
	return cstr;
}

static gboolean
version_ok_p (const char *version,
	      const char *min_version)
{
	int a = 0, b = 0, c = 0, d = 0;
	int mina = 0, minb = 0, minc = 0, mind = 0;

	/* Note that if some fields in the version don't exist, then
	 * we don't mind, they are zero */
	sscanf (version, "%d.%d.%d.%d", &a, &b, &c, &d);
	sscanf (min_version, "%d.%d.%d.%d", &mina, &minb, &minc, &mind);

	if ((a > mina) ||
	    (a == mina && b > minb) ||
	    (a == mina && b == minb && c > minc) ||
	    (a == mina && b == minb && c == minc && d >= mind))
		return TRUE;
	else
		return FALSE;
}

char *
gdmcomm_call_gdm (const char *command,
		  const char *auth_cookie,
		  const char *min_version,
		  int         tries)
{
	struct sockaddr_un addr;
	int fd;
	char *ret;

	if (tries <= 0)
		return NULL;

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return gdmcomm_call_gdm (command, auth_cookie, min_version, tries - 1);
	}

	strcpy (addr.sun_path, GDM_SUP_SOCKET);
	addr.sun_family = AF_UNIX;

	if (connect (fd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		VE_IGNORE_EINTR (close (fd));
		return gdmcomm_call_gdm (command, auth_cookie, min_version, tries - 1);
	}

	/* Version check first */
	ret = do_command (fd, GDM_SUP_VERSION, TRUE /* get_response */);
	if (ret == NULL) {
		VE_IGNORE_EINTR (close (fd));
		return gdmcomm_call_gdm (command, auth_cookie, min_version, tries - 1);
	}
	if (strncmp (ret, "GDM ", strlen ("GDM ")) != 0) {
		g_free (ret);
		VE_IGNORE_EINTR (close (fd));
		return NULL;
	}
	if ( ! version_ok_p (&ret[4], min_version)) {
		g_free (ret);
		do_command (fd, GDM_SUP_CLOSE, FALSE /* get_response */);
		VE_IGNORE_EINTR (close (fd));
		return NULL;
	}
	g_free (ret);

	/* require authentication */
	if (auth_cookie != NULL)  {
		char *auth_cmd = g_strdup_printf
			(GDM_SUP_AUTH_LOCAL " %s", auth_cookie);
		ret = do_command (fd, auth_cmd, TRUE /* get_response */);
		g_free (auth_cmd);
		if (ret == NULL) {
			VE_IGNORE_EINTR (close (fd));
			return gdmcomm_call_gdm (command, auth_cookie,
						 min_version, tries - 1);
		}
		/* not auth'ed */
		if (strcmp (ret, "OK") != 0) {
			do_command (fd, GDM_SUP_CLOSE,
				    FALSE /* get_response */);
			VE_IGNORE_EINTR (close (fd));
			/* returns the error */
			return ret;
		}
		g_free (ret);
	}

	ret = do_command (fd, command, TRUE /* get_response */);
	if (ret == NULL) {
		VE_IGNORE_EINTR (close (fd));
		return gdmcomm_call_gdm (command, auth_cookie, min_version, tries - 1);
	}

	do_command (fd, GDM_SUP_CLOSE, FALSE /* get_response */);

	VE_IGNORE_EINTR (close (fd));

	return ret;
}

static gchar *
get_dispnum (GdkScreen *screen)
{
  gchar *tmp, *retval;

  tmp = g_strdup (gdk_display_get_name (gdk_screen_get_display (screen)));
  if (tmp)
    {
      gchar *ptr;

      ptr = strchr (tmp, '.');
      if (ptr)
	*ptr = '\0';

      ptr = strchr (tmp, ':');
      if (ptr)
	{
	  while (*ptr == ':')
	    ptr++;

	  retval = g_strdup (ptr);
	}
      else
	retval = g_strdup (tmp);

      g_free (tmp);
    }
  else
    retval = g_strdup ("0");

  return retval;
}

/* This just gets a cookie of MIT-MAGIC-COOKIE-1 type */
gchar *
gdmcomm_get_a_cookie (GdkScreen *screen,
		      gboolean   binary)
{
	FILE *fp;
	char *number;
	char *cookie = NULL;
	Xauth *xau;

	VE_IGNORE_EINTR (fp = fopen (XauFileName (), "r"));
	if (fp == NULL) {
		return NULL;
	}

	number = get_dispnum (screen);

	cookie = NULL;

	while ((xau = XauReadAuth (fp)) != NULL) {
		/* Just find the FIRST magic cookie, that's what gdm uses */
		if (xau->number_length != strlen (number) ||
		    strncmp (xau->number, number, xau->number_length) != 0 ||
		    /* gdm sends MIT-MAGIC-COOKIE-1 cookies of length 16,
		     * so just do those */
		    xau->data_length != 16 ||
		    xau->name_length != strlen ("MIT-MAGIC-COOKIE-1") ||
		    strncmp (xau->name, "MIT-MAGIC-COOKIE-1",
			     xau->name_length) != 0) {
			XauDisposeAuth (xau);
			continue;
		}

		if (binary) {
			cookie = g_new0 (char, 16);
			memcpy (cookie, xau->data, 16);
		} else {
			int i;
			GString *str;

			str = g_string_new (NULL);

			for (i = 0; i < xau->data_length; i++) {
				g_string_append_printf
					(str, "%02x",
					 (guint)(guchar)xau->data[i]);
			}
			cookie = g_string_free (str, FALSE);
		}

		XauDisposeAuth (xau);

		break;
	}
	VE_IGNORE_EINTR (fclose (fp));

	return cookie;
}

const char *
gdmcomm_get_auth_cookie (GdkScreen *screen)
{
	FILE *fp;
	char *number;
	static gboolean tried = FALSE;
	static char *cookie = NULL;
	Xauth *xau;

	if (tried)
		return cookie;

	VE_IGNORE_EINTR (fp = fopen (XauFileName (), "r"));
	if (fp == NULL) {
		cookie = NULL;
		tried = TRUE;
		return NULL;
	}

	number = get_dispnum (screen);

	cookie = NULL;

	while ((xau = XauReadAuth (fp)) != NULL) {
		char *cmd;
		char *ret;
		int i;
		char buffer[40 /* 2*16 == 32, so 40 is enough */];

		/* Only Family local things are considered, all console
		 * logins DO have this family (and even some local xdmcp
		 * logins, though those will not pass by gdm itself of
		 * course) */
		if (xau->family != FamilyLocal ||
		    xau->number_length != strlen (number) ||
		    strncmp (xau->number, number, xau->number_length) != 0 ||
		    /* gdm sends MIT-MAGIC-COOKIE-1 cookies of length 16,
		     * so just do those */
		    xau->data_length != 16 ||
		    xau->name_length != strlen ("MIT-MAGIC-COOKIE-1") ||
		    strncmp (xau->name, "MIT-MAGIC-COOKIE-1",
			     xau->name_length) != 0 ||
		    xau->data_length != 16) {
			XauDisposeAuth (xau);
			continue;
		}

		buffer[0] = '\0';
		for (i = 0; i < 16; i++) {
			char sub[3];
			g_snprintf (sub, sizeof (sub), "%02x",
				    (guint)(guchar)xau->data[i]);
			strcat (buffer, sub);
		}

		XauDisposeAuth (xau);

		cmd = g_strdup_printf (GDM_SUP_AUTH_LOCAL " %s", buffer);
		ret = gdmcomm_call_gdm (cmd, NULL /* auth cookie */, "2.2.4.0", 5);
		if (ret != NULL &&
		    strcmp (ret, "OK") == 0) {
			g_free (ret);
			cookie = g_strdup (buffer);
			break;
		}
		g_free (ret);
	}
	VE_IGNORE_EINTR (fclose (fp));
      
	g_free (number);

	tried = TRUE;
	return cookie;
}

#if 0
gboolean
gdmcomm_check (gboolean gui_bitching)
{
	GtkWidget *dialog;
	FILE *fp = NULL;
	long pid;
	char *pidfile;
	struct stat s;
	int statret;

	pidfile = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					GDM_KEY_PIDFILE);

	pid = 0;
	if (pidfile != NULL)
		VE_IGNORE_EINTR (fp = fopen (pidfile, "r"));
	if (fp != NULL) {
		int r;
		VE_IGNORE_EINTR (r = fscanf (fp, "%ld", &pid));
		VE_IGNORE_EINTR (fclose (fp));
		if (r != 1)
			pid = 0;
	}

	g_free (pidfile);

	errno = 0;
	if (pid <= 1 ||
	    (kill (pid, 0) < 0 &&
	     errno != EPERM)) {
		if (gui_bitching) {
			dialog = ve_hig_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 FALSE /* markup */,
				 _("GDM (The GNOME Display Manager) "
				   "is not running."),
				 "%s\n%s",
				 _("You might in fact be using a different "
				   "display manager, such as KDM "
				   "(KDE Display Manager) or xdm."),
				 _("If you still wish to use this feature, "
				   "either start GDM yourself or ask your "
				   "system administrator to start GDM."));

			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
		return FALSE;
	}

	VE_IGNORE_EINTR (statret = stat (GDM_SUP_SOCKET, &s));
	if (statret < 0 ||
	    s.st_uid != 0 ||
	    access (GDM_SUP_SOCKET, R_OK|W_OK) != 0) {
		if (gui_bitching) {
			dialog = ve_hig_dialog_new
				(NULL /* parent */,
				 GTK_DIALOG_MODAL /* flags */,
				 GTK_MESSAGE_WARNING,
				 GTK_BUTTONS_OK,
				 FALSE /* markup */,
				 _("Cannot communicate with GDM "
				   "(The GNOME Display Manager)"),
				 "%s",
				 _("Perhaps you have an old version "
				   "of GDM running."));
			gtk_widget_show_all (dialog);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
		return FALSE;
	}

	return TRUE;
}
#endif /* 0 */

const char *
gdmcomm_get_error_message (const char *ret,
			   gboolean    use_xnest)
{
	/* These need a bit more refinement */
	if (ret == NULL) {
		return _("Cannot communicate with gdm, perhaps "
			 "you have an old version running.");
	} else if (strncmp (ret, "ERROR 0 ", strlen ("ERROR 0 ")) == 0) {
		return _("Cannot communicate with gdm, perhaps "
			 "you have an old version running.");
	} else if (strncmp (ret, "ERROR 1 ", strlen ("ERROR 1 ")) == 0) {
		return _("The allowed limit of flexible X servers reached.");
	} else if (strncmp (ret, "ERROR 2 ", strlen ("ERROR 2 ")) == 0) {
		return _("There were errors trying to start the X server.");
	} else if (strncmp (ret, "ERROR 3 ", strlen ("ERROR 3 ")) == 0) {
		return _("The X server failed.  Perhaps it is not "
			 "configured well.");
	} else if (strncmp (ret, "ERROR 4 ", strlen ("ERROR 4 ")) == 0) {
		return _("Too many X sessions running.");
	} else if (strncmp (ret, "ERROR 5 ", strlen ("ERROR 5 ")) == 0) {
		return _("The nested X server (Xnest) cannot connect to "
			 "your current X server.  You may be missing an "
			 "X authorization file.");
	} else if (strncmp (ret, "ERROR 6 ", strlen ("ERROR 6 ")) == 0) {
		if (use_xnest)
			return _("The nested X server (Xnest) is not "
				 "available, or gdm is badly configured.\n"
				 "Please install the Xnest package in "
				 "order to use the nested login.");
		else
			return _("The X server is not available, "
				 "it is likely that gdm is badly "
				 "configured.");
	} else if (strncmp (ret, "ERROR 7 ", strlen ("ERROR 7 ")) == 0) {
		return _("Trying to set an unknown logout action, or trying "
			 "to set a logout action which is not available.");
	} else if (strncmp (ret, "ERROR 8 ", strlen ("ERROR 8 ")) == 0) {
		return _("Virtual terminals not supported.");
	} else if (strncmp (ret, "ERROR 9 ", strlen ("ERROR 9 ")) == 0) {
		return _("Trying to change to an invalid virtual terminal number.");
	} else if (strncmp (ret, "ERROR 50 ", strlen ("ERROR 50 ")) == 0) {
		return _("Trying to update an unsupported configuration key.");
	} else if (strncmp (ret, "ERROR 100 ", strlen ("ERROR 100 ")) == 0) {
		return _("You do not seem to have authentication needed "
			 "be for this operation.  Perhaps your .Xauthority "
			 "file is not set up correctly.");
	} else if (strncmp (ret, "ERROR 200 ", strlen ("ERROR 200 ")) == 0) {
		return _("Too many messages were sent to gdm and it hung up"
			 "on us.");
	} else {
		return _("Unknown error occured.");
	}
}
