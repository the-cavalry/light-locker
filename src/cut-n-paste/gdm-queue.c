/*
 *    gdm-queue.h
 *    (c) 2006 Thomas Thurman
 *    Based originally on GDMcommunication routines
 *    (c)2001 Queen of England, (c)2002 George Lebl
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

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>

#include "gdm-queue.h"

#include <X11/Xauth.h>

/* You might have thought that we communicated with GDM using dbus or
 * bonobo or something spiffy like that. You'd be wrong! Instead, we
 * use a text-based command-response protocol over a Unix domain socket
 * which lives at a well-known position in the /tmp directory.
 * At some point, it will make sense to switch to something dbus-based.
 */
#define GDM_SOCKET_FILENAME "/tmp/.gdm_socket"

/* The old gdmcomm routines used to have individual minimum versions
 * for every particular GDM command, but then you have to deal with
 * the problem of what to do if you're running a version earlier than
 * the one you wanted. So we just have a blanket minimum for everything.
 */
#define MINIMAL_GDM_VERSION      "2.8.0"

/* Here's a useful macro to keep doing something even if we get EINTR
 * back (because the read or whatever was interrupted).
 *
 * From gdm2/vicious-extensions/ve-misc.h
 * Copyright (c) 2000 Eazel, Inc., (c) 2001,2002 George Lebl
 */
#define VE_IGNORE_EINTR(expr) \
  do {          \
    errno = 0;  \
    expr;               \
  } while G_UNLIKELY (errno == EINTR);

/* This is rather a one-off enum. ask_gdm() needs to keep track of
 * its current state, and these are the states it can be in:
 */
typedef enum {
  /* The socket is closed. */
  GDM_SOCKET_CLOSED,
  /* The socket is open, and we just sent the VERSION command
   * (so we are expecting to get "GDM xxxx" back).
   */
  GDM_SOCKET_WAITING_FOR_VERSION,
  /* The socket is open, and we just sent anything else. */
  GDM_SOCKET_RUNNING_QUEUE,
} GdmSocketStatus;

/* This is one entry in the queue. */
typedef struct {
  /* The string to send to GDM.
   * If this is NULL, GDM won't hear anything about this step, but the
   * callback will be called anyway, just as if GDM had returned "OK"
   * to something.
   */
  gchar *query;
  /* The function to call with the result.
   * If this is NULL, the result will be thrown away.
  */
  GdmMessageCallback *callback;
  /* Arbitrary data to pass to the callback. */
  gpointer data;
} GdmRequest;

/* The actual queue of instructions. */
static GQueue *gdm_queue = NULL;
/* The file descriptor of socket open on GDM_SOCKET_FILENAME. */
static int gdm_socket = 0;
/* The status, as described above. */
static GdmSocketStatus gdm_socket_status = GDM_SOCKET_CLOSED;

static void gdm_request_free (GdmRequest *victim);
static gboolean gdm_run_queue (void *dummy);
static int gdm_send_command (const gchar *command);
static gboolean gdm_send_next_command (void);

/* True if gdm_run_queue is set to be called by glib.
 * FIXME: Actually, isn't this pretty similar to GDM_SOCKET_CLOSED?
 */
static gboolean queue_runner_is_running = FALSE;

/* See the description in the header file. */
void
ask_gdm(GdmMessageCallback *callback, gpointer data, gchar *query, ...)
{
  GdmRequest *new_request;
  gboolean queue_was_empty;
  va_list args;

  queue_was_empty = gdm_queue==NULL || g_queue_is_empty (gdm_queue);

  va_start (args, query);

  new_request = g_malloc(sizeof(GdmRequest));
  new_request->callback = callback;
  new_request->data = data;
  
  if (query==NULL)
    new_request->query = NULL;
  else
    new_request->query = g_strdup_vprintf (query, args);

  va_end (args);

  if (gdm_queue == NULL)
    gdm_queue = g_queue_new();

  g_queue_push_tail(gdm_queue, new_request);

  if (!queue_runner_is_running) {
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, gdm_run_queue, NULL, NULL);
    queue_runner_is_running = TRUE;
  }
}

/* Cleans up a GdmRequest*. */

void
gdm_request_free (GdmRequest *victim)
{
  g_free (victim->query);
  /* Don't free victim->data: that's the callback's job */
  g_free (victim);
}

/* Returns TRUE iff the version represented by |version| is greater or equal
 * to the version represented by |min_version|.
 */
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

/* Closes the GDM socket gracefully, and sets the state accordingly. */
static void
close_gdm_socket ()
{
  gdm_send_command("CLOSE");
  VE_IGNORE_EINTR (close (gdm_socket));
  gdm_socket_status = GDM_SOCKET_CLOSED;
}

/* This is the function which gets called repeatedly while we're idle
 * to remove messages from the queue and send them to GDM.
 */
gboolean gdm_run_queue (void *dummy)
{
  static GString *incoming = NULL;

  /* Firstly, unless the socket is closed, we left
   * because we were waiting for a response from
   * GDM. Let's see what it's sent us.
   */
  if (gdm_socket_status != GDM_SOCKET_CLOSED) {
    char buf[1] = "";

    if (incoming == NULL)
      incoming = g_string_sized_new (10);

    while (read (gdm_socket, buf, 1) == 1 &&
        buf[0] != '\n') {
      g_string_append_c (incoming, buf[0]);
    }

    if (buf[0] != '\n') {
      /* We haven't received the CR yet, so let's go around
       * again.
       */

      return TRUE;
    }
  }

  /* So if we get here, either the socket is closed and we want to
   * begin a conversation, or we've just received an answer that we
   * were expecting.
   */

  switch (gdm_socket_status) {
    case GDM_SOCKET_CLOSED:
        {
        struct sockaddr_un addr;

        /* Okay, so open the socket and let's try talking to gdm. */
        gdm_socket = socket (PF_LOCAL, SOCK_STREAM, 0);
        strcpy (addr.sun_path, GDM_SOCKET_FILENAME);
        addr.sun_family = AF_UNIX;

        if (fcntl (gdm_socket, F_SETFL, O_NONBLOCK) < 0) {
          close_gdm_socket ();
          return TRUE;
        }

        if (connect (gdm_socket, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
          close_gdm_socket ();
          return TRUE;
        }

        gdm_send_command("VERSION");

        if (incoming) {
          g_string_free (incoming, TRUE);
          incoming = NULL;
        }
        gdm_socket_status = GDM_SOCKET_WAITING_FOR_VERSION;

        return TRUE;
        }             
      break;
    case GDM_SOCKET_WAITING_FOR_VERSION:
        {
        if (strncmp (incoming->str, "GDM ", 4) != 0) {
          g_string_free (incoming, TRUE);
          incoming = NULL;
          close_gdm_socket ();
          return TRUE;
        }
        if ( ! version_ok_p (&incoming->str[4], MINIMAL_GDM_VERSION)) {
          g_string_free (incoming, TRUE);
          incoming = NULL;
          close_gdm_socket ();
          return TRUE;
        }

        g_string_free (incoming, TRUE);
        incoming = NULL;

        gdm_send_next_command ();
        gdm_socket_status = GDM_SOCKET_RUNNING_QUEUE;        

        return TRUE;
        }
      break;
    case GDM_SOCKET_RUNNING_QUEUE:
        {
        GdmRequest *request;

        if (g_queue_is_empty (gdm_queue)) {
          queue_runner_is_running = FALSE;
          return FALSE;
        }

        request = (GdmRequest*) g_queue_pop_head (gdm_queue);

#if 0
        /* Uncomment this if you want to see who's saying what to whom. */
        g_warning ("%s -> %s", request->query, incoming->str);
#endif
        
        if (request->callback) {
          if (strncmp (incoming->str, "OK", 2) == 0) {
            if (incoming->str[2] == ' ')
              request->callback (GDM_RESULT_OK, incoming->str+3, request->data);
            else
              request->callback (GDM_RESULT_OK, NULL, request->data);
          } else if (strncmp (incoming->str, "ERROR", 5) == 0) {
            if (incoming->str[5] == ' ')
              request->callback (GDM_RESULT_ERROR, incoming->str+6, request->data);
            else
              request->callback (GDM_RESULT_ERROR, NULL, request->data);
          } else {
            g_warning("Bizarre message from GDM: %s", incoming->str);
            request->callback (GDM_RESULT_BIZARRE, incoming->str, request->data);
          }
        }

        gdm_request_free (request);

        g_string_free (incoming, TRUE);
        incoming = NULL;

        if (gdm_send_next_command ()) {
          return TRUE;
        } else {
          /* Queue is empty, so close the socket... */
          close_gdm_socket ();
          /* ...and don't go round again. */
          queue_runner_is_running = FALSE;
          return FALSE;
        }
        }
  }

  g_assert_not_reached ();
}

/* This sends the query given by the next command, i.e. the one on the
 * top of the queue. While there are entries on the top of the queue
 * with *no* commands to send, this function will pop them and execute
 * their callbacks. However, that is the only time when this function
 * modifies the queue.
 *
 * The function returns FALSE if the queue is (now) empty and TRUE
 * otherwise.
 */
static gboolean gdm_send_next_command ()
{
  GdmRequest *request;

  if (g_queue_is_empty (gdm_queue)) {
    return FALSE;
  } else {
    request  = (GdmRequest*) g_queue_peek_head (gdm_queue);

    while (!g_queue_is_empty (gdm_queue) && request->query == NULL) {
      /* The query can be NULL, in which case we just call the
       * callback and pop it.
       * 
       * (This pops the request that "request" points at
       * off the top of the stack.)
       */
      g_queue_pop_head (gdm_queue);

      /* Check that nobody has been daft enough to ask for no query
       * and no callback. Why anyone would do that, I don't know.
       */
      if (request->callback)
        /* So we call it... */
        request->callback (GDM_RESULT_OK, NULL, request->data);

      if (!g_queue_is_empty (gdm_queue)) {

        /* Throw that one away... */
        gdm_request_free (request);

        /* and get another. */
        request = (GdmRequest*) g_queue_peek_head (gdm_queue);
      }
    }

    if (g_queue_is_empty (gdm_queue)) {
      return FALSE;
    } else {
      gdm_send_command (request->query);
      return TRUE;
    }
  }
}
 
/* This sends the command |command| to GDM. */
static int gdm_send_command (const gchar *command)
{
  int ret;
  gchar *command_with_newline;
#ifndef MSG_NOSIGNAL
  void (*old_handler)(int);
#endif

  command_with_newline = g_strdup_printf ("%s\n", command);

#ifdef MSG_NOSIGNAL
  ret = send (gdm_socket, command_with_newline, strlen (command_with_newline), MSG_NOSIGNAL);
#else
  old_handler = signal (SIGPIPE, SIG_IGN);
  ret = send (gdm_socket, command_with_newline, strlen (command_with_newline), 0);
  signal (SIGPIPE, old_handler);
#endif

  g_free (command_with_newline);

  if (ret == -1)
    g_warning ("Bad result in gdm_send_command");

  return ret;
}

/* Returns a pointer to a string representing the X display number which
 * |screen| is on. It's the callers responsibility to free the pointer.
 */
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

/* This is a strange special case. The first time we need to authenticate,
 * we might find several cookies which could do. We have no way of knowing
 * without trying them which ones might solve the problem. So we send all
 * of them; the first one which succeeds stores the winning cookie so that
 * we can send only that one next time. However, there's then no point in
 * sending all the others, so we call this function to save ourselves the
 * bother.
 */
static void
chop_all_auths_off_gdm_queue ()
{
  GdmRequest *head;
  
  if (g_queue_is_empty (gdm_queue))
    return;
  
  head = (GdmRequest*) g_queue_peek_head (gdm_queue);

  while (!g_queue_is_empty (gdm_queue) &&
      strncmp (head->query, "AUTH_", 5)==0) {

      gdm_request_free (g_queue_pop_head (gdm_queue));
      head = (GdmRequest*) g_queue_peek_head (gdm_queue);
  }
}

static gboolean cookie_tried = FALSE;
static char *cookie = NULL;

/* The callback which calls chop_all_auths_off_gdm_queue, q.v. */
static void
gdm_callback_perhaps_set_official_cookie (GdmResultState is_ok,
      const gchar *dummy, gpointer candidate_cookie)
{
  if (is_ok == GDM_RESULT_OK) {
    cookie = candidate_cookie;
    cookie_tried = TRUE;
    chop_all_auths_off_gdm_queue ();
  } else {
    g_free (candidate_cookie);
  }
}

/* See the description in the header file. */
void
queue_authentication (GdkScreen *screen)
{
  FILE *fp;
  char *number;
  Xauth *xau;

  if (cookie_tried) {
    /* Been there, done that. */
    if (cookie)
      ask_gdm (NULL, NULL, GDM_CMD_AUTH_LOCAL, cookie);
    return;
  }

  VE_IGNORE_EINTR (fp = fopen (XauFileName (), "r"));

  if (fp == NULL) {
    /* Oops, no xauthority file! */
    cookie = NULL;
    cookie_tried = TRUE;
    return;
  }

  number = get_dispnum (screen);

  cookie = NULL;

  while ((xau = XauReadAuth (fp)) != NULL) {
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
      /* so, not what we want; throw it away and continue */
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

    ask_gdm (gdm_callback_perhaps_set_official_cookie,
        g_strdup (buffer),
        GDM_CMD_AUTH_LOCAL, buffer);
  }

  VE_IGNORE_EINTR (fclose (fp));

  g_free (number);
}

/* See the description in the header file. */
gchar *
get_mit_magic_cookie (GdkScreen *screen,
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

