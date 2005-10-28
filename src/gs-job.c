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
#include <string.h>

#if defined(HAVE_SETPRIORITY) && defined(PRIO_PROCESS)
#include <sys/resource.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gmenu-tree.h"

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
};

typedef struct 
{
        char  *dir;
        time_t mtime; /* 0 == not existing or not a dir */
} ThemeDirMtime;

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GSJob, gs_job, G_TYPE_OBJECT)

static const char *known_engine_locations [] = {
        SAVERDIR,
#ifdef XSCREENSAVER_HACK_DIR
        XSCREENSAVER_HACK_DIR,
#endif
        LIBEXECDIR "/xscreensaver",
        "/usr/X11R6/lib/xscreensaver",
        "/usr/libexec/xscreensaver",
        "/usr/lib/xscreensaver",
        NULL
};

/* Returns the full path to the queried command */
static char *
find_command (const char *command)
{
        int i;

        for (i = 0; known_engine_locations [i]; i++){
                char *path;

                path = g_build_filename (known_engine_locations [i], command, NULL);

                if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE)
                    && ! g_file_test (path, G_FILE_TEST_IS_DIR))
                        return path;

                g_free (path);
        }

        return NULL;
}

static gboolean
check_command (char *command)
{
        char *path;
        char **argv;

        g_return_val_if_fail (command != NULL, FALSE);

        g_shell_parse_argv (command, NULL, &argv, NULL);
        path = find_command (argv [0]);
        g_strfreev (argv);

        if (path) {
                g_free (path);
                return TRUE;
        }

        return FALSE;
}

static void
add_known_engine_locations_to_path (void)
{
        static gboolean already_added;
        int      i;
        GString *str;

        /* We only want to add the items to the path once */
        if (already_added)
                return;

        already_added = TRUE;

        /* TODO: set a default PATH ? */

        str = g_string_new (g_getenv ("PATH"));
        for (i = 0; known_engine_locations [i]; i++) {
                /* TODO: check that permissions are safe */
                if (g_file_test (known_engine_locations [i], G_FILE_TEST_IS_DIR))
                        g_string_append_printf (str, ":%s", known_engine_locations [i]);
        }

        g_setenv ("PATH", str->str, TRUE);
        g_string_free (str, TRUE);
}

static GMenuTree *
get_themes_tree (void)
{
        static GMenuTree *themes_tree;

        if (themes_tree)
                return themes_tree;

        /* we only need to add the locations to the path once
           and since this is only run once we'll do it here */
        add_known_engine_locations_to_path ();

	themes_tree = gmenu_tree_lookup ("gnome-screensavers.menu", GMENU_TREE_FLAGS_NONE);

        return themes_tree;
}

GSJobThemeInfo *
gs_job_theme_info_ref (GSJobThemeInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);
        g_return_val_if_fail (info->refcount > 0, NULL);

        info->refcount++;

        return info;
}

void
gs_job_theme_info_unref (GSJobThemeInfo *info)
{
        g_return_if_fail (info != NULL);
        g_return_if_fail (info->refcount > 0);

        if (--info->refcount == 0) {
                g_free (info->name);
                g_free (info->comment);
                g_free (info->icon);
                g_free (info->exec);
                g_free (info->path);
                g_free (info->file_id);

                g_free (info);
        }
}

const char *
gs_job_theme_info_get_id (GSJobThemeInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);

        return info->file_id;
}

const char *
gs_job_theme_info_get_name (GSJobThemeInfo *info)
{
        g_return_val_if_fail (info != NULL, NULL);

        return info->name;
}

static GSJobThemeInfo *
gs_job_theme_info_new_from_gmenu_tree_entry (GMenuTreeEntry *entry)
{
        GSJobThemeInfo *info;
        const char     *str;
        char           *pos;

        info = g_new0 (GSJobThemeInfo, 1);

        info->refcount = 1;
        info->name     = g_strdup (gmenu_tree_entry_get_name (entry));
        info->comment  = g_strdup (gmenu_tree_entry_get_comment (entry));
        info->icon     = g_strdup (gmenu_tree_entry_get_icon (entry));
        info->exec     = g_strdup (gmenu_tree_entry_get_exec (entry));
        info->path     = g_strdup (gmenu_tree_entry_get_desktop_file_path (entry));

        /* remove the .desktop suffix */
        str = gmenu_tree_entry_get_desktop_file_id (entry);
        pos = g_strrstr (str, ".desktop");
        if (pos) {
                info->file_id = g_strndup (str, pos - str);
        } else {
                info->file_id  = g_strdup (str);
        }

        return info;
}

static GSJobThemeInfo *
find_info_for_id (GMenuTree  *tree,
                  const char *id)
{
        GSJobThemeInfo     *info;
	GMenuTreeDirectory *root;
        GSList             *items;
        GSList             *l;

	root = gmenu_tree_get_root_directory (tree);
        if (! root)
                return NULL;

        items = gmenu_tree_directory_get_contents (root);

        info = NULL;

        for (l = items; l; l = l->next) {
                if (info == NULL
                    && gmenu_tree_item_get_type (l->data) == GMENU_TREE_ITEM_ENTRY) {
                        GMenuTreeEntry *entry = l->data;
                        const char     *file_id;

                        file_id = gmenu_tree_entry_get_desktop_file_id (entry);
                        if (file_id && id && strcmp (file_id, id) == 0) {
                                info = gs_job_theme_info_new_from_gmenu_tree_entry (entry);
                        }
                }

                gmenu_tree_item_unref (l->data);
        }

        g_slist_free (items);
        gmenu_tree_item_unref (root);

        return info;
}

GSJobThemeInfo *
gs_job_lookup_theme_info (GSJob      *job,
                          const char *name)
{
        GSJobThemeInfo *info;
        char           *id;
        GMenuTree      *tree;

        g_return_val_if_fail (GS_IS_JOB (job), NULL);
        g_return_val_if_fail (name != NULL, NULL);

        tree = get_themes_tree ();

        id = g_strdup_printf ("%s.desktop", name);
        info = find_info_for_id (tree, id);
        g_free (id);

        return info;
}

static void
theme_prepend_entry (GSList         **parent_list,
                     GMenuTreeEntry  *entry,
                     const char      *filename)
{
        GSJobThemeInfo *info;

        info = gs_job_theme_info_new_from_gmenu_tree_entry (entry);

        *parent_list = g_slist_prepend (*parent_list, info);
}

static void
make_theme_list (GSList             **parent_list,
                 GMenuTreeDirectory  *directory,
                 const char          *filename)
{
        GSList *items;
        GSList *l;

        items = gmenu_tree_directory_get_contents (directory);

        for (l = items; l; l = l->next) {
                switch (gmenu_tree_item_get_type (l->data)) {

                case GMENU_TREE_ITEM_ENTRY:
                        theme_prepend_entry (parent_list, l->data, filename);
                        break;

                case GMENU_TREE_ITEM_ALIAS:
                case GMENU_TREE_ITEM_DIRECTORY:
                default:
                        break;
                }

                gmenu_tree_item_unref (l->data);
        }

        g_slist_free (items);

        *parent_list = g_slist_reverse (*parent_list);
}

GSList *
gs_job_get_theme_info_list (GSJob *job)
{
        GSList             *l = NULL;
        GMenuTreeDirectory *root;
        GMenuTree          *tree;

        g_return_val_if_fail (GS_IS_JOB (job), NULL);

        tree = get_themes_tree ();
        root = gmenu_tree_get_root_directory (tree);

        if (root) {
                make_theme_list (&l, root, "gnome-screensavers.menu");
                gmenu_tree_item_unref (root);
        }

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
        job->priv = GS_JOB_GET_PRIVATE (job);
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

        g_free (job->priv->current_theme);
        job->priv->current_theme = NULL;

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

gboolean
gs_job_set_theme  (GSJob      *job,
                   const char *theme,
                   GError    **error)
{
        GSJobThemeInfo *info;

        g_return_val_if_fail (GS_IS_JOB (job), FALSE);

        /* NULL theme is interpreted as a no-op job */

        if (theme) {
                info = gs_job_lookup_theme_info (job, theme);

                if (! info) {
                        /* FIXME: set error */
                        g_warning ("Could not lookup info for theme: %s", theme);
                        return FALSE;
                }

                if (! check_command (info->exec)) {
                        /* FIXME: set error */
                        g_warning ("Could not execute command for theme: %s", info->exec);
                        return FALSE;
                }

                gs_job_theme_info_unref (info);
        }

        g_free (job->priv->current_theme);

        if (theme)
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
                 const char *command,
                 int        *pid,
                 GIOFunc     watch_func,
                 gpointer    user_data,
                 guint      *watch_id)
{
        char       *path;
        char      **argv;
        GPtrArray  *env;
        char       *str;
        gboolean    result;
        GIOChannel *channel;
        GError     *error = NULL;
        int         standard_error;
        int         child_pid;
        int         id;
        int         i;

        if (! command)
                return FALSE;

        if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
                g_warning ("Could not parse command: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        path = find_command (argv [0]);
        if (path) {
                g_free (argv [0]);
                argv [0] = path;
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

        if (g_getenv ("XAUTHORITY"))
                g_ptr_array_add (env, g_strdup_printf ("XAUTHORITY=%s",
                                                       g_getenv ("XAUTHORITY")));
        if (g_getenv ("LANG"))
                g_ptr_array_add (env, g_strdup_printf ("LANG=%s",
                                                       g_getenv ("LANG")));
        if (g_getenv ("LANGUAGE"))
                g_ptr_array_add (env, g_strdup_printf ("LANGUAGE=%s",
                                                       g_getenv ("LANGUAGE")));
        g_ptr_array_add (env, NULL);

        error = NULL;
        result = gdk_spawn_on_screen_with_pipes (gtk_widget_get_screen (widget),
                                                 g_get_home_dir (),
                                                 argv,
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
                g_warning ("Could not start command '%s': %s", command, error->message);
                g_error_free (error);
                g_strfreev (argv);
                return FALSE;
        }

        g_strfreev (argv);

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

        if (! job->priv->widget) {
                g_warning ("Could not start job: screensaver window is not set.");
                return FALSE;
        }

        if (! job->priv->current_theme) {
                /* no warning here because a NULL theme is interpreted
                   as a no-op job */
                return FALSE;
        }

        info = gs_job_lookup_theme_info (job, job->priv->current_theme);

        if (! check_command (info->exec)) {
                g_warning ("Could not start job: unable to execute theme engine");
                return FALSE;
        }

        result = spawn_on_widget (job->priv->widget,
                                  info->exec,
                                  &job->priv->pid,
                                  (GIOFunc)command_watch,
                                  job,
                                  &job->priv->watch_id);

        if (result)
                job->priv->status = GS_JOB_RUNNING;

        gs_job_theme_info_unref (info);

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
