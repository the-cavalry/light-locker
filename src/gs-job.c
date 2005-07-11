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
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#if defined(HAVE_SETPRIORITY) && defined(PRIO_PROCESS)
#include <sys/resource.h>
#endif

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <glib.h>
#include <glib/gstdio.h>
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
        GtkWidget      *widget;

        GSJobStatus     status;
        gint            pid;
        guint           watch_id;

        char           *current_theme;

        char          **search_path;
        int             search_path_len;

        gboolean        themes_valid;
        GHashTable     *all_themes;

        long            last_stat_time;
        GList          *dir_mtimes;
};

typedef struct 
{
        char  *dir;
        time_t mtime; /* 0 == not existing or not a dir */
} ThemeDirMtime;

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
        xmlXPathFreeContext (context);

        if (! result)
                return NULL;

        if (xmlXPathNodeSetIsEmpty (result->nodesetval)) {
                g_warning ("Node set is empty for %s", xpath);
                xmlXPathFreeObject (result);
                return NULL;
        }

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

                xmlXPathFreeObject (result);
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

void
gs_job_set_theme_path (GSJob      *job,
                       const char *path [],
                       int         n_elements)
{
        int i;

        g_return_if_fail (GS_IS_JOB (job));

        for (i = 0; i < job->priv->search_path_len; i++)
                g_free (job->priv->search_path [i]);

        g_free (job->priv->search_path);

        job->priv->search_path = g_new (char *, n_elements);
        job->priv->search_path_len = n_elements;
        for (i = 0; i < job->priv->search_path_len; i++)
                job->priv->search_path [i] = g_strdup (path [i]);

        /*do_theme_change (job);*/
}

void
gs_job_get_theme_path (GSJob *job,
                       char **path [],
                       int   *n_elements)
{
        int i;

        g_return_if_fail (GS_IS_JOB (job));

        if (n_elements)
                *n_elements = job->priv->search_path_len;
  
        if (path) {
                *path = g_new (char *, job->priv->search_path_len + 1);
                for (i = 0; i < job->priv->search_path_len; i++)
                        (*path) [i] = g_strdup (job->priv->search_path [i]);
                (*path) [i] = NULL;
        }
}

void
gs_job_prepend_theme_path (GSJob      *job,
                           const char *path)
{
        int i;

        g_return_if_fail (GS_IS_JOB (job));
        g_return_if_fail (path != NULL);

        job->priv->search_path_len++;
        job->priv->search_path = g_renew (char *, job->priv->search_path, job->priv->search_path_len);

        for (i = job->priv->search_path_len - 1; i > 0; i--)
                job->priv->search_path [i] = job->priv->search_path [i - 1];
  
        job->priv->search_path [0] = g_strdup (path);

        /*do_theme_change (job);*/
}

static GSJobThemeInfo *
theme_info_new (void)
{
        GSJobThemeInfo *info = g_new0 (GSJobThemeInfo, 1);

        return info;
}

void
gs_job_theme_info_free (GSJobThemeInfo *info)
{
        g_return_if_fail (info != NULL);

        g_free (info->name);
        g_free (info->title);
        g_strfreev (info->argv);

        g_free (info);
}

static GSJobThemeInfo *
gs_job_theme_info_copy (const GSJobThemeInfo *info)
{
        GSJobThemeInfo *copy;

        g_return_val_if_fail (info != NULL, NULL);

        copy = g_memdup (info, sizeof (GSJobThemeInfo));
        if (copy->name)
                copy->name = g_strdup (copy->name);
        if (copy->title)
                copy->title = g_strdup (copy->title);
        if (copy->argv)
                copy->argv = g_strdupv (copy->argv);

        return copy;
}

static gboolean
parse_theme (const char *path,
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

        /* xmlChar and char differ in sign */
        label_val = (char *) get_xml_config_string (doc, "/screensaver", "_label");
        name_val  = (char *) get_xml_config_string (doc, "/screensaver", "name");
        cmd_val   = (char *) get_xml_config_string (doc, "/screensaver/command", "name");
        arg_val   = (char *) get_xml_config_string (doc, "/screensaver/command", "arg");

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

static void
load_themes (GSJob *job)
{
        GDir       *gdir;
        int         i;
        const char *file;
        GTimeVal    tv;
  
        job->priv->all_themes = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       (GDestroyNotify)gs_job_theme_info_free);
  
        for (i = 0; i < job->priv->search_path_len; i++) {
                char *dir;

                dir = job->priv->search_path [i];

                gdir = g_dir_open (dir, 0, NULL);

                if (gdir == NULL)
                        continue;
      
                while ((file = g_dir_read_name (gdir))) {
                        GSJobThemeInfo *info = NULL;
                        char           *path;
                        char           *name;
                        char           *title;
                        char          **argv;

                        path = g_build_filename (dir, file, NULL);

                        if (! parse_theme (path,
                                           &name,
                                           &title,
                                           &argv)) {
                                g_free (path);
                                continue;
                        }
                        g_free (path);

                        info = theme_info_new ();
                        info->name  = g_strdup (name);
                        info->title = g_strdup (title);
                        info->argv  = g_strdupv (argv);

                        g_free (title);
                        g_free (name);
                        g_strfreev (argv);

                        if (g_hash_table_lookup (job->priv->all_themes,
                                                 info->name)) {
                                gs_job_theme_info_free (info);
                                continue;
                        }
                                
                        g_hash_table_insert (job->priv->all_themes,
                                             g_strdup (info->name),
                                             info);
		}

                g_dir_close (gdir);
        }

        job->priv->themes_valid = TRUE;
  
        g_get_current_time (&tv);
        job->priv->last_stat_time = tv.tv_sec;
}

static gboolean
gs_job_theme_rescan_if_needed (GSJob *job)
{
        ThemeDirMtime *dir_mtime;
        GList         *d;
        int            stat_res;
        struct stat    stat_buf;
        GTimeVal       tv;

        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        for (d = job->priv->dir_mtimes; d != NULL; d = d->next) {
                dir_mtime = d->data;

                stat_res = g_lstat (dir_mtime->dir, &stat_buf);

                /* dir mtime didn't change */
                if (stat_res == 0 && 
                    S_ISDIR (stat_buf.st_mode) &&
                    dir_mtime->mtime == stat_buf.st_mtime)
                        continue;
                /* didn't exist before, and still doesn't */
                if (dir_mtime->mtime == 0 &&
                    (stat_res != 0 || !S_ISDIR (stat_buf.st_mode)))
                        continue;
	  
                /*do_theme_change (job);*/
                return TRUE;
        }
  
        g_get_current_time (&tv);
        job->priv->last_stat_time = tv.tv_sec;

        return FALSE;
}

static void
ensure_valid_themes (GSJob *job)
{
        GTimeVal tv;
  
        if (job->priv->themes_valid) {
                g_get_current_time (&tv);

                if (ABS (tv.tv_sec - job->priv->last_stat_time) > 5)
                        gs_job_theme_rescan_if_needed (job);
        }
  
        if (! job->priv->themes_valid)
                load_themes (job);
}

GSJobThemeInfo *
gs_job_lookup_theme_info (GSJob      *job,
                          const char *theme)
{
        GSJobThemeInfo       *info = NULL;
        const GSJobThemeInfo *value;

        g_return_val_if_fail (GS_IS_JOB (job), NULL);
        g_return_val_if_fail (theme != NULL, NULL);

        ensure_valid_themes (job);

        value = g_hash_table_lookup (job->priv->all_themes, theme);

        if (value)
                info = gs_job_theme_info_copy (value);

        return info;
}

static void
hash2slist_foreach (gpointer  key,
                    gpointer  value,
                    gpointer  user_data)
{
        GSList **slist_p = user_data;

        *slist_p = g_slist_prepend (*slist_p, g_strdup (key));
}

static GSList *
g_hash_table_slist_keys (GHashTable *hash_table)
{
        GSList *slist = NULL;

        g_return_val_if_fail (hash_table != NULL, NULL);

        g_hash_table_foreach (hash_table, hash2slist_foreach, &slist);

        return slist;
}

GSList *
gs_job_get_theme_list (GSJob *job)
{
        GSList *l;

        g_return_val_if_fail (GS_IS_JOB (job), NULL);

        ensure_valid_themes (job);

        l = g_hash_table_slist_keys (job->priv->all_themes);

        return l;
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
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        object_class->finalize  = gs_job_finalize;

        g_type_class_add_private (klass, sizeof (GSJobPrivate));
}

static void
gs_job_init (GSJob *job)
{
        int i;

        job->priv = GS_JOB_GET_PRIVATE (job);

        i = 1;
        job->priv->search_path_len = i;

        job->priv->search_path = g_new (char *, job->priv->search_path_len);

        i = 0;
        job->priv->search_path [i++] = g_strdup (THEMESDIR);

        job->priv->themes_valid = FALSE;
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
        int    i;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GS_IS_JOB (object));

        job = GS_JOB (object);

        g_return_if_fail (job->priv != NULL);

        if (job->priv->pid > 0) {
                signal_pid (job->priv->pid, SIGTERM);
                gs_job_died (job);
        }

        g_free (job->priv->current_theme);
        job->priv->current_theme = NULL;

        for (i = 0; i < job->priv->search_path_len; i++)
                g_free (job->priv->search_path [i]);

        g_free (job->priv->search_path);
        job->priv->search_path = NULL;

        g_hash_table_destroy (job->priv->all_themes);

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
        "/usr/libexec/xscreensaver",
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

static gboolean
check_command (char **argv)
{
        char *path;

        g_return_val_if_fail (argv != NULL, FALSE);

        path = find_command (argv [0], known_locations);

        if (path) {
                g_free (path);
                return TRUE;
        }

        return FALSE;
}

gboolean
gs_job_set_theme  (GSJob      *job,
                   const char *theme,
                   GError    **error)
{
        GSJobThemeInfo *info;

        g_return_val_if_fail (GS_IS_JOB (job), FALSE);
        g_return_val_if_fail (theme != NULL, FALSE);

        info = gs_job_lookup_theme_info (job, theme);

        if (! info) {
                /* FIXME: set error */
                g_warning ("Could not lookup info for theme: %s", theme);
                return FALSE;
        }

        if (! check_command (info->argv)) {
                /* FIXME: set error */
                g_warning ("Could not verify safety of command for theme: %s", theme);
                return FALSE;
        }

        gs_job_theme_info_free (info);

        g_free (job->priv->current_theme);
        job->priv->current_theme = g_strdup (theme);

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

        if (nice_level == 0)
                return;

#if defined(HAVE_SETPRIORITY) && defined(PRIO_PROCESS)
        if (setpriority (PRIO_PROCESS, pid, nice_level) != 0) {
                g_warning ("setpriority(PRIO_PROCESS, %lu, %d) failed",
                           (unsigned long) pid, nice_level);
        }
#else
        g_warning ("don't know how to change process priority on this system.");
#endif
}

static gboolean
spawn_on_widget (GtkWidget  *widget,
                 char      **argv,
                 int        *pid,
                 GIOFunc     watch_func,
                 gpointer    user_data,
                 guint      *watch_id)
{
        char       *path;
        char      **new_argv;
        GPtrArray  *env;
        char       *str;
        gboolean    result;
        GIOChannel *channel;
        GError     *error = NULL;
        int         standard_error;
        int         child_pid;
        int         id;
        int         i;

        if (! argv)
                return FALSE;

        new_argv = g_strdupv (argv);
        /* try to find command in well known locations */
        path = find_command (new_argv [0], known_locations);
        if (path) {
                g_free (new_argv [0]);
                new_argv [0] = path;
        }

        env = g_ptr_array_new ();

        str = widget_get_id_string (widget);
        g_ptr_array_add (env, g_strdup_printf ("XSCREENSAVER_WINDOW=%s", str));
        g_free (str);

        str = gdk_screen_make_display_name (gtk_widget_get_screen (widget));
        g_ptr_array_add (env, g_strdup_printf ("DISPLAY=%s", str));
        g_free (str);

        g_ptr_array_add (env, g_strdup_printf ("HOME=%s",
                                               g_get_home_dir ()));
        g_ptr_array_add (env, g_strdup_printf ("PATH=%s", g_getenv ("PATH")));
        g_ptr_array_add (env, g_strdup_printf ("XAUTHORITY=%s", g_getenv ("XAUTHORITY")));
        g_ptr_array_add (env, NULL);

        result = gdk_spawn_on_screen_with_pipes (gtk_widget_get_screen (widget),
                                                 g_get_home_dir (),
                                                 new_argv,
                                                 (char **)env->pdata,
                                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                                 NULL,
                                                 NULL,
                                                 &child_pid,
                                                 NULL,
                                                 NULL,
                                                 &standard_error,
                                                 &error);
        for (i = 0; i < env->len; i++)
                g_free (g_ptr_array_index (env, i));
        g_ptr_array_free (env, TRUE);

        if (! result) {
                g_warning ("Could not start command '%s': %s", new_argv [0], error->message);
                g_error_free (error);
                g_strfreev (new_argv);
                return FALSE;
        }

        g_strfreev (new_argv);

        nice_process (child_pid, 10);

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
        gboolean        result;
        GSJobThemeInfo *info;

        g_return_val_if_fail (job != NULL, FALSE);
        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        if (job->priv->pid) {
                g_warning ("Cannot restart active job.");
                return FALSE;
        }

        if (! job->priv->current_theme) {
                g_warning ("Could not start job: screensaver theme is not set.");
                return FALSE;
        }

        if (! job->priv->widget) {
                g_warning ("Could not start job: screensaver window is not set.");
                return FALSE;
        }

        info = gs_job_lookup_theme_info (job, job->priv->current_theme);

        result = spawn_on_widget (job->priv->widget,
                                  info->argv,
                                  &job->priv->pid,
                                  (GIOFunc)command_watch,
                                  job,
                                  &job->priv->watch_id);

        if (result)
                job->priv->status = GS_JOB_RUNNING;

        gs_job_theme_info_free (info);

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
