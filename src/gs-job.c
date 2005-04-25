/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gs-job.h"

#include "subprocs.h"

static void gs_job_class_init (GSJobClass *klass);
static void gs_job_init       (GSJob      *job);
static void gs_job_finalize   (GObject    *object);

#define GS_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_JOB, GSJobPrivate))

typedef enum {
        GS_JOB_INVALID,
        GS_JOB_RUNNING,
        GS_JOB_STOPPED,
        GS_JOB_KILLED,
        GS_JOB_DEAD
} GSJobStatus;

struct GSJobPrivate
{
        GtkWidget  *widget;
        char       *command;

        GSJobStatus status;
        gint        pid;
        guint       watch_id;

};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSJob, gs_job, G_TYPE_OBJECT);

static char *
widget_get_id_string (GtkWidget *widget)
{
        char *id = NULL;

        g_return_val_if_fail (widget != NULL, NULL);

        id = g_strdup_printf ("0x%X",
                              (guint32)GDK_WINDOW_XID (widget->window));
        return id;
}

static void
gs_job_class_init (GSJobClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize  = gs_job_finalize;

        g_type_class_add_private (klass, sizeof (GSJobPrivate));
}

static void
gs_job_init (GSJob *job)
{
        job->priv = GS_JOB_GET_PRIVATE (job);

        /* zero is fine for everything */
}

/* adapted from gspawn.c */
static int
wait_on_child (int pid)
{
        int status;

 wait_again:
        if (waitpid (pid, &status, 0) < 0) {
                if (errno == EINTR)
                        goto wait_again;
                else if (errno == ECHILD)
                        ; /* do nothing, child already reaped */
                else
                        g_warning ("waitpid () should not fail in 'GSJob'");
        }

        return status;
}

static void
gs_job_died (GSJob *job)
{
        if (job->priv->pid > 0) {
                int exit_status;
                        
                exit_status = wait_on_child (job->priv->pid);

                job->priv->status = GS_JOB_DEAD;

                if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
                } else {
                        /* exited normally */
                }
        }

        g_spawn_close_pid (job->priv->pid);
        job->priv->pid = 0;
}

static void
gs_job_finalize (GObject *object)
{
        GSJob *job;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_JOB (object));

        job = GS_JOB (object);

        g_return_if_fail (job->priv != NULL);

        if (job->priv->pid > 0) {
                signal_pid (job->priv->pid, SIGTERM);
                gs_job_died (job);
        }

        g_free (job->priv->command);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
gs_job_set_widget  (GSJob     *job,
                    GtkWidget *widget)
{
        g_return_if_fail (job != NULL);
        g_return_if_fail (GS_IS_JOB (job));

        job->priv->widget = widget;
}

static const char *known_locations [] = {
        SAVERDIR,
        LIBEXECDIR "/xscreensaver",
        "/usr/X11R6/lib/xscreensaver",
        "/usr/lib/xscreensaver",
        NULL
};

/* Returns the full path to the queried command */
static char *
find_command (const char  *command,
              const char **known_locations)
{
        int i;

        for (i = 0; known_locations [i]; i++){
                char *path;

                path = g_build_filename (known_locations [i], command, NULL);

                if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE)
                    && !g_file_test (path, G_FILE_TEST_IS_DIR))
                        return path;

                g_free (path);
        }

        return NULL;
}

void
gs_job_set_command  (GSJob      *job,
                     const char *command)
{
        char *path;
        char *line;

        g_return_if_fail (job != NULL);
        g_return_if_fail (GS_IS_JOB (job));

        g_free (job->priv->command);
        job->priv->command = NULL;

        if (! command)
                return;

        /* try to find command in well known locations */
        path = find_command (command, known_locations);

        /* TODO: parse configuration file to determine default args */

        if (path) {
                line = g_strdup_printf ("%s -root", path);
                g_free (path);
                job->priv->command = line;
        }
}

GSJob *
gs_job_new (void)
{
        GObject *job;

        job = g_object_new (GS_TYPE_JOB, NULL);

        return GS_JOB (job);
}

GSJob *
gs_job_new_for_widget (GtkWidget  *widget,
                       const char *command)
{
        GObject *job;

        job = g_object_new (GS_TYPE_JOB, NULL);

        gs_job_set_widget  (GS_JOB (job), widget);
        gs_job_set_command (GS_JOB (job), command);

        return GS_JOB (job);
}

static gboolean
spawn_on_widget (GtkWidget  *widget,
                 const char *command,
                 int        *pid,
                 GIOFunc     watch_func,
                 gpointer    user_data,
                 guint      *watch_id)
{
        int         argc;
        char      **argv;
        char       *envp [5];
        int         nenv = 0;
        int         i;
        char       *window_id;
        gboolean    result;
        GIOChannel *channel;
        GError     *error = NULL;
        int         standard_error;
        int         child_pid;
        int         id;

        if (! g_shell_parse_argv (command, &argc, &argv, NULL))
                return FALSE;

        window_id = widget_get_id_string (widget);
        envp [nenv++] = g_strdup_printf ("XSCREENSAVER_WINDOW=%s", window_id);
        envp [nenv++] = g_strdup_printf ("DISPLAY=%s",
                                         gdk_display_get_name (gdk_display_get_default ()));
        envp [nenv++] = g_strdup_printf ("HOME=%s",
                                         g_get_home_dir ());
        envp [nenv++] = g_strdup_printf ("PATH=%s", g_getenv ("PATH"));
        envp [nenv++] = NULL;

        result = gdk_spawn_on_screen_with_pipes (gtk_widget_get_screen (widget),
                                                 g_get_home_dir (),
                                                 argv,
                                                 envp,
                                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                                 NULL,
                                                 NULL,
                                                 &child_pid,
                                                 NULL,
                                                 NULL,
                                                 &standard_error,
                                                 &error);

        if (! result) {
                g_message ("Could not start command '%s': %s", command, error->message);
                g_error_free (error);
                return FALSE;
        }

        if (pid)
                *pid = child_pid;
        else
                g_spawn_close_pid (child_pid);

        channel = g_io_channel_unix_new (standard_error);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        id = g_io_add_watch (channel,
                             G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                             watch_func,
                             user_data);
        if (watch_id)
                *watch_id = id;

        /*g_io_channel_unref (channel);*/

        for (i = 0; i < nenv; i++)
                g_free (envp [i]);

        g_strfreev (argv);

        return result;
}

static gboolean
command_watch (GIOChannel   *source,
               GIOCondition  condition,
               GSJob        *job)
{
        GIOStatus status;
        GError   *error = NULL;
        gboolean  done  = FALSE;

        g_return_val_if_fail (job != NULL, FALSE);

        if (condition & G_IO_IN) {
                char *str;

                status = g_io_channel_read_line (source, &str, NULL, NULL, &error);
		if (status == G_IO_STATUS_NORMAL) {
		} else if (status == G_IO_STATUS_EOF) {
                        done = TRUE;
		}

                g_free (str);
        } else if (condition & G_IO_HUP) {
                done = TRUE;
        }

        if (done) {
                gs_job_died (job);

                job->priv->watch_id = 0;
                return FALSE;
        }

        return TRUE;
}

gboolean
gs_job_start (GSJob *job)
{
        gboolean result;

        g_return_val_if_fail (job != NULL, FALSE);
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        if (job->priv->pid) {
                g_warning ("Cannot restart active job.");
                return FALSE;
        }

        if (!job->priv->command)
                return FALSE;

        if (!job->priv->widget)
                return FALSE;

        result = spawn_on_widget (job->priv->widget,
                                  (const char *)job->priv->command,
                                  &job->priv->pid,
                                  (GIOFunc)command_watch,
                                  job,
                                  &job->priv->watch_id);

        if (result)
                job->priv->status = GS_JOB_RUNNING;

        return result;
}

gboolean
gs_job_stop (GSJob *job)
{
        g_return_val_if_fail (job != NULL, FALSE);
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        if (! job->priv->pid)
                return FALSE;

        if (job->priv->status == GS_JOB_STOPPED)
                gs_job_suspend (job, FALSE);

        g_source_remove (job->priv->watch_id);
        job->priv->watch_id = 0;

        signal_pid (job->priv->pid, SIGTERM);

        job->priv->status = GS_JOB_KILLED;

        gs_job_died (job);

        return TRUE;
}

gboolean
gs_job_suspend (GSJob   *job,
                gboolean suspend)
{
        g_return_val_if_fail (job != NULL, FALSE);
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        if (! job->priv->pid)
                return FALSE;

        signal_pid (job->priv->pid, (suspend ? SIGSTOP : SIGCONT));

        job->priv->status = (suspend ? GS_JOB_STOPPED : GS_JOB_RUNNING);

        return TRUE;
}
