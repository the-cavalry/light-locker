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
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#if defined(HAVE_SETPRIORITY) && defined(PRIO_PROCESS)
#include <sys/resource.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gs-debug.h"
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
        GtkWidget      *widget;

        GSJobStatus     status;
        gint            pid;
        guint           watch_id;

        char           *command;
};

G_DEFINE_TYPE (GSJob, gs_job, G_TYPE_OBJECT)

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
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize  = gs_job_finalize;

        g_type_class_add_private (klass, sizeof (GSJobPrivate));
}

static void
gs_job_init (GSJob *job)
{
        job->priv = GS_JOB_GET_PRIVATE (job);
}

/* adapted from gspawn.c */
static int
wait_on_child (int pid)
{
        int status;

 wait_again:
        if (waitpid (pid, &status, 0) < 0) {
                if (errno == EINTR) {
                        goto wait_again;
                } else if (errno == ECHILD) {
                        ; /* do nothing, child already reaped */
                } else {
                        gs_debug ("waitpid () should not fail in 'GSJob'");
                }
        }

        return status;
}

static void
gs_job_died (GSJob *job)
{
        if (job->priv->pid > 0) {
                int exit_status;

                gs_debug ("Waiting on process %d", job->priv->pid);
                exit_status = wait_on_child (job->priv->pid);

                job->priv->status = GS_JOB_DEAD;

                if (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) != 0)) {
                        gs_debug ("Wait on child process failed");
                } else {
                        /* exited normally */
                }
        }
        g_spawn_close_pid (job->priv->pid);
        job->priv->pid = 0;

        gs_debug ("Job died");
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
        job->priv->command = NULL;

        G_OBJECT_CLASS (gs_job_parent_class)->finalize (object);
}

void
gs_job_set_widget  (GSJob     *job,
                    GtkWidget *widget)
{
        g_return_if_fail (job != NULL);
        g_return_if_fail (GS_IS_JOB (job));

        if (widget != job->priv->widget) {
                job->priv->widget = widget;

                /* restart job */
                if (gs_job_is_running (job)) {
                        gs_job_stop (job);
                        gs_job_start (job);
                }
        }
}

gboolean
gs_job_set_command  (GSJob      *job,
                     const char *command)
{
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        gs_debug ("Setting command for job: '%s'",
                  command != NULL ? command : "NULL");

        g_free (job->priv->command);
        job->priv->command = g_strdup (command);

        return TRUE;
}

GSJob *
gs_job_new (void)
{
        GObject *job;

        job = g_object_new (GS_TYPE_JOB, NULL);

        return GS_JOB (job);
}

GSJob *
gs_job_new_for_widget (GtkWidget  *widget)
{
        GObject *job;

        job = g_object_new (GS_TYPE_JOB, NULL);

        gs_job_set_widget (GS_JOB (job), widget);

        return GS_JOB (job);
}

static void
nice_process (int pid,
              int nice_level)
{
        g_return_if_fail (pid > 0);

        if (nice_level == 0) {
                return;
        }

#if defined(HAVE_SETPRIORITY) && defined(PRIO_PROCESS)
        gs_debug ("Setting child process priority to: %d", nice_level);
        if (setpriority (PRIO_PROCESS, pid, nice_level) != 0) {
                gs_debug ("setpriority(PRIO_PROCESS, %lu, %d) failed",
                          (unsigned long) pid, nice_level);
        }
#else
        gs_debug ("don't know how to change process priority on this system.");
#endif
}

static GPtrArray *
get_env_vars (GtkWidget *widget)
{
        GPtrArray *env;
        char      *str;
        int        i;
        static const char *allowed_env_vars [] = {
                "PATH",
                "SESSION_MANAGER",
                "XAUTHORITY",
                "XAUTHLOCALHOSTNAME",
                "LANG",
                "LANGUAGE",
                "DBUS_SESSION_BUS_ADDRESS"
        };

        env = g_ptr_array_new ();

        str = gdk_screen_make_display_name (gtk_widget_get_screen (widget));
        g_ptr_array_add (env, g_strdup_printf ("DISPLAY=%s", str));
        g_free (str);

        g_ptr_array_add (env, g_strdup_printf ("HOME=%s",
                                               g_get_home_dir ()));

        for (i = 0; i < G_N_ELEMENTS (allowed_env_vars); i++) {
                const char *var;
                const char *val;
                var = allowed_env_vars [i];
                val = g_getenv (var);
                if (val != NULL) {
                        g_ptr_array_add (env, g_strdup_printf ("%s=%s",
                                                               var,
                                                               val));
                }
        }

        str = widget_get_id_string (widget);
        g_ptr_array_add (env, g_strdup_printf ("XSCREENSAVER_WINDOW=%s", str));
        g_free (str);

        g_ptr_array_add (env, NULL);

        return env;
}

static gboolean
spawn_on_widget (GtkWidget  *widget,
                 const char *command,
                 int        *pid,
                 GIOFunc     watch_func,
                 gpointer    user_data,
                 guint      *watch_id)
{
        char      **argv;
        GPtrArray  *env;
        gboolean    result;
        GIOChannel *channel;
        GError     *error = NULL;
        int         standard_error;
        int         child_pid;
        int         id;
        int         i;

        if (command == NULL) {
                return FALSE;
        }

        if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
                gs_debug ("Could not parse command: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        env = get_env_vars (widget);

        error = NULL;
        result = gdk_spawn_on_screen_with_pipes (gtk_widget_get_screen (widget),
                                                 NULL,
                                                 argv,
                                                 (char **)env->pdata,
                                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                                 NULL,
                                                 NULL,
                                                 &child_pid,
                                                 NULL,
                                                 NULL,
                                                 &standard_error,
                                                 &error);
        for (i = 0; i < env->len; i++) {
                g_free (g_ptr_array_index (env, i));
        }
        g_ptr_array_free (env, TRUE);

        if (! result) {
                gs_debug ("Could not start command '%s': %s", command, error->message);
                g_error_free (error);
                g_strfreev (argv);
                return FALSE;
        }

        g_strfreev (argv);

        nice_process (child_pid, 10);

        if (pid != NULL) {
                *pid = child_pid;
        } else {
                g_spawn_close_pid (child_pid);
        }

        channel = g_io_channel_unix_new (standard_error);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        id = g_io_add_watch (channel,
                             G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                             watch_func,
                             user_data);
        if (watch_id != NULL) {
                *watch_id = id;
        }

        g_io_channel_unref (channel);

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
                        gs_debug ("command output: %s", str);

                } else if (status == G_IO_STATUS_EOF) {
                        done = TRUE;

                } else if (error != NULL) {
                        gs_debug ("command error: %s", error->message);
                        g_error_free (error);
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
gs_job_is_running (GSJob *job)
{
        gboolean running;

        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        running = (job->priv->pid > 0);

        return running;
}

gboolean
gs_job_start (GSJob *job)
{
        gboolean result;

        g_return_val_if_fail (job != NULL, FALSE);
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        gs_debug ("starting job");

        if (job->priv->pid != 0) {
                gs_debug ("Cannot restart active job.");
                return FALSE;
        }

        if (job->priv->widget == NULL) {
                gs_debug ("Could not start job: screensaver window is not set.");
                return FALSE;
        }

        if (job->priv->command == NULL) {
                /* no warning here because a NULL command is interpreted
                   as a no-op job */
                gs_debug ("No command set for job.");
                return FALSE;
        }

        result = spawn_on_widget (job->priv->widget,
                                  job->priv->command,
                                  &job->priv->pid,
                                  (GIOFunc)command_watch,
                                  job,
                                  &job->priv->watch_id);

        if (result) {
                job->priv->status = GS_JOB_RUNNING;
        }

        return result;
}

static void
remove_command_watch (GSJob *job)
{
        if (job->priv->watch_id != 0) {
                g_source_remove (job->priv->watch_id);
                job->priv->watch_id = 0;
        }
}

gboolean
gs_job_stop (GSJob *job)
{
        g_return_val_if_fail (job != NULL, FALSE);
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        gs_debug ("stopping job");

        if (job->priv->pid == 0) {
                gs_debug ("Could not stop job: pid not defined");
                return FALSE;
        }

        if (job->priv->status == GS_JOB_STOPPED) {
                gs_job_suspend (job, FALSE);
        }

        remove_command_watch (job);

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

        gs_debug ("suspending job");

        if (job->priv->pid == 0) {
                return FALSE;
        }

        signal_pid (job->priv->pid, (suspend ? SIGSTOP : SIGCONT));

        job->priv->status = (suspend ? GS_JOB_STOPPED : GS_JOB_RUNNING);

        return TRUE;
}
