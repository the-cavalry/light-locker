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

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
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
        char      **argv;

        GSJobStatus status;
        gint        pid;
        guint       watch_id;

};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSJob, gs_job, G_TYPE_OBJECT);


static xmlXPathObjectPtr
getnodeset (xmlDocPtr      doc,
            const xmlChar *xpath)
{	
	xmlXPathContextPtr context;
	xmlXPathObjectPtr  result;

	context = xmlXPathNewContext (doc);
	result = xmlXPathEvalExpression (xpath, context);
	if (xmlXPathNodeSetIsEmpty (result->nodesetval)) {
                g_warning ("Node set is empty for %s", xpath);
		return NULL;
        }

	xmlXPathFreeContext (context);

	return result;
}

static xmlChar *
get_first_xpath_prop (xmlDocPtr      doc,
                      const xmlChar *xpath,
                      const xmlChar *prop)
{ 
        xmlChar          *keyword = NULL;
	xmlXPathObjectPtr result;
	xmlNodeSetPtr     nodeset;

	result = getnodeset (doc, xpath);
        if (result) {
		nodeset = result->nodesetval;
                if (nodeset->nodeNr > 0)
                        keyword = xmlGetProp (nodeset->nodeTab [0], prop);
	}

        return keyword;
}

static xmlChar *
get_xml_config_string (xmlDocPtr   doc,
                       const char *xpath,
                       const char *prop)
{
        xmlChar *str;

        str = get_first_xpath_prop (doc,
                                    (const xmlChar  *)xpath,
                                    (const xmlChar  *)prop);

        return str;
}

gboolean
gs_job_theme_parse (const char *path,
                    char      **name,
                    char      **label,
                    char     ***argv)
{
        xmlDocPtr  doc;
        xmlNodePtr node;
        char      *contents;
        char      *name_val;
        char      *label_val;
        char      *cmd_val;
        char      *arg_val;
        gsize      length;
        GError    *error = NULL;

        if (name)
                *name = NULL;
        if (label)
                *label = NULL;
        if (argv)
                *argv = NULL;

        if (! g_file_get_contents (path, &contents, &length, &error))
                return FALSE;

        contents = (char *)g_realloc (contents, length + 1);
        contents [length] = '\0';

        doc = xmlParseMemory (contents, length);
        if (doc == NULL)
                doc = xmlRecoverMemory (contents, length);

        g_free (contents);

        /* If the document has no root, or no name */
        if (!doc || !doc->children || !doc->children->name) {
                if (doc != NULL)
                        xmlFreeDoc (doc);
                return FALSE;
        }

        node = xmlDocGetRootElement (doc);
        if (! node)
                return FALSE;

        label_val = get_xml_config_string (doc, "/screensaver", "_label");
        name_val  = get_xml_config_string (doc, "/screensaver", "name");
        cmd_val   = get_xml_config_string (doc, "/screensaver/command", "name");
        arg_val   = get_xml_config_string (doc, "/screensaver/command", "arg");

        if (! cmd_val) {
                /* this is to support the xscreensaver config format where
                   the command and name are the same */
                cmd_val = g_strdup (name_val);
        }

        if (name)
                *name = g_strdup (name_val);

        if (label)
                *label = g_strdup (label_val);

        if (argv) {
                char *command;

                command = g_strdup_printf ("%s %s", cmd_val, arg_val);
                g_shell_parse_argv (command, NULL, argv, NULL);
                g_free (command);
        }

        xmlFree (label_val);
        xmlFree (name_val);
        xmlFree (cmd_val);
        xmlFree (arg_val);
        xmlFreeDoc (doc);

        return TRUE;
}

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

        g_strfreev (job->priv->argv);

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
gs_job_set_command (GSJob *job,
                    char **argv)
{
        char  *path;
        char **new_argv;

        g_return_if_fail (job != NULL);
        g_return_if_fail (GS_IS_JOB (job));

        g_strfreev (job->priv->argv);
        job->priv->argv = NULL;

        if (! argv)
                return;

        new_argv = g_strdupv (argv);
        /* try to find command in well known locations */
        path = find_command (new_argv [0], known_locations);

        if (path) {
                g_free (new_argv [0]);
                new_argv [0] = path;
                job->priv->argv = new_argv;
        }
}

void
gs_job_set_theme  (GSJob      *job,
                   const char *theme)
{
        char  *filename;
        char  *path;
        char **argv;

        g_return_if_fail (GS_IS_JOB (job));
        g_return_if_fail (theme != NULL);

        /* for now assume theme name -> command mapping */
        filename = g_strdup_printf ("%s.xml", theme);
        path = g_build_filename (THEMESDIR, filename, NULL);

        if (! gs_job_theme_parse (path,
                                  NULL,
                                  NULL,
                                  &argv)) {
                g_free (path);
                return;
        }
        g_free (path);

        if (! argv)
                return;

        gs_job_set_command (job, argv);

        g_strfreev (argv);
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
                       const char *theme)
{
        GObject *job;

        job = g_object_new (GS_TYPE_JOB, NULL);

        gs_job_set_widget (GS_JOB (job), widget);
        gs_job_set_theme (GS_JOB (job), theme);

        return GS_JOB (job);
}

static gboolean
spawn_on_widget (GtkWidget  *widget,
                 char      **argv,
                 int        *pid,
                 GIOFunc     watch_func,
                 gpointer    user_data,
                 guint      *watch_id)
{
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

        if (! argv)
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
                g_message ("Could not start command '%s': %s", argv [0], error->message);
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

        g_io_channel_unref (channel);

        for (i = 0; i < nenv; i++)
                g_free (envp [i]);

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

        if (! job->priv->argv)
                return FALSE;

        if (! job->priv->widget)
                return FALSE;

        result = spawn_on_widget (job->priv->widget,
                                  job->priv->argv,
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
