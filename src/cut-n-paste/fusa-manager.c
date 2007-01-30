/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 2 -*-
 * 
 * Fast User Switch Applet: fusa-manager.c
 * 
 * Copyright (C) 2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <glib/gi18n.h>

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include <stdlib.h>
#include <string.h>

#include <X11/Xauth.h>
#include <X11/Xlib.h>
#ifdef WITH_XNEST
#include <X11/Xmu/WinUtil.h>
#endif

#include <gdk/gdkx.h>

#include <gtk/gtkicontheme.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkstock.h>

#include <libgnomevfs/gnome-vfs-ops.h>

#include "gdm-queue.h"
#include "fusa-utils.h"
#include "fusa-display-private.h"
#include "fusa-user-private.h"

#include "fusa-manager-private.h"


/* **************** *
 *  Private Macros  *
 * **************** */

#define FUSA_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), FUSA_TYPE_MANAGER, FusaManagerClass))
#define FUSA_IS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), FUSA_TYPE_MANAGER))
#define FUSA_MANAGER_GET_CLASS(object) \
  (G_TYPE_INSTANCE_GET_CLASS ((object), FUSA_TYPE_MANAGER, FusaManagerClass))

/* Prefs Defaults */
#define DEFAULT_ALLOW_ROOT	TRUE
#define DEFAULT_MAX_ICON_SIZE	128
#define DEFAULT_USER_MAX_FILE	65536
#define DEFAULT_MINIMAL_UID	100
#define DEFAULT_GLOBAL_FACE_DIR	DATADIR "/faces"
#define DEFAULT_USER_ICON	"stock_person"
#define DEFAULT_EXCLUDE		{ "bin",	\
				  "daemon",	\
				  "adm",	\
				  "lp",		\
				  "sync",	\
				  "shutdown",	\
				  "halt",	\
				  "mail",	\
				  "news",	\
				  "uucp",	\
				  "operator",	\
				  "nobody",	\
				  "gdm",	\
				  "postgres",	\
				  "pvm",	\
				  "rpm",	\
				  "nfsnobody",	\
				  "pcap",	\
				  NULL }

/* Magic */
#define MINIMAL_GDM_VERSION	"2.8.0"


/* ********************** *
 *  Private Enumerations  *
 * ********************** */

enum
{
  USER_ADDED,
  USER_REMOVED,
  DISPLAY_ADDED,
  DISPLAY_REMOVED,
  LAST_SIGNAL
};

/* ******************** *
 *  Private Structures  *
 * ******************** */

/* Class Structures */
struct _FusaManager
{
  GObject parent;

  /* Child Objects */
  GHashTable *displays;
  GHashTable *displays_by_console;
  GHashTable *users;
  GHashTable *users_by_uid;

  /* Updaters */
  GnomeVFSMonitorHandle *gdmconfig_monitor;
  GnomeVFSMonitorHandle *passwd_monitor;
  GnomeVFSMonitorHandle *shells_monitor;
  guint results_pull_id;
  guint update_displays_id;

  GHashTable *shells;
  GHashTable *exclusions;
  gchar *global_face_dir;
  uid_t minimal_uid;
  gsize user_max_file;
  gint max_icon_size;

  guint8 allow_root  : 1;
  guint8 relax_group : 1;
  guint8 relax_other : 1;

  /* Dirty flags */
  guint8 users_dirty : 1;
  guint8 icons_dirty : 1;
};

typedef struct _FusaManagerClass
{
  GObjectClass parent_class;

  /* Signals */
  void (*user_added)        (FusaManager *manager,
			     FusaUser    *user);
  void (*user_removed)      (FusaManager *manager,
			     FusaUser    *user);
  void (*display_added)     (FusaManager *manager,
			     FusaDisplay *display);
  void (*display_removed)   (FusaManager *manager,
			     FusaDisplay *display);
}
FusaManagerClass;

/* Thread Back-n-Forth Structures */
typedef struct _DisplayUpdate
{
  gchar *name;
  gchar *username;
  gchar *location;
}
DisplayUpdate;

typedef struct _CleanDisplaysData
{
  FusaManager *manager;
  GSList *displays;
}
CleanDisplaysData;

typedef struct _FusaManagerWithCallback
{
  FusaManager *manager;
  FusaManagerDisplayCallback callback;
  FusaDisplay *display_for_callback;
  gchar *name_of_display;
  gpointer data_for_callback;
}
FusaManagerWithCallback;


/* ********************* *
 *  Function Prototypes  *
 * ********************* */

/* GObject Functions */
static void fusa_manager_finalize (GObject *object);

/* GnomeVFSMonitor Callbacks */
static void gdmconfig_monitor_cb (GnomeVFSMonitorHandle    *handle,
				  const gchar              *text_uri,
				  const gchar              *info_uri,
				  GnomeVFSMonitorEventType  event_type,
				  gpointer                  user_data);
static void shells_monitor_cb    (GnomeVFSMonitorHandle    *handle,
				  const gchar              *text_uri,
				  const gchar              *info_uri,
				  GnomeVFSMonitorEventType  event_type,
				  gpointer                  user_data);
static void passwd_monitor_cb    (GnomeVFSMonitorHandle    *handle,
				  const gchar              *text_uri,
				  const gchar              *info_uri,
				  GnomeVFSMonitorEventType  event_type,
				  gpointer                  user_data);

/* User Functions */
static gboolean        check_user_file                (const gchar  *filename,
						       uid_t         uid,
						       gssize        max_file_size,
						       gboolean      relax_group,
						       gboolean      relax_other);
static void            emit_user_icon_changed_signals (gpointer      key,
						       gpointer      value,
						       gpointer      user_data);
static inline gboolean strv_equals_string_table       (gchar       **strv,
						       GHashTable   *table);
static void            reload_gdm_config              (FusaManager  *manager,
                                                       gboolean      force_reload);
static void            reload_shells                  (FusaManager  *manager);
static void            reload_passwd                  (FusaManager  *manager);

static FusaManagerWithCallback* new_fusa_manager_with_callback(FusaManager *manager,
    FusaManagerDisplayCallback callback,
    FusaDisplay *display_for_callback,
    gpointer data_for_callback);

/* Displays - Main Thread Functions */
static inline void process_display_updates   (FusaManager                *manager,
                                              DisplayUpdate              *items,
                                              guint                      n_items);

/* Utility Functions */
static void         listify_hash_values_hfunc (gpointer      key,
					       gpointer      value,
					       gpointer      data);
static gboolean     clear_hash_table_hrfunc   (gpointer      key,
					       gpointer      value,
					       gpointer      data);
static FusaDisplay *real_get_display          (FusaManager  *manager,
					       const gchar  *name);

static void
gdm_callback_update_displays (GdmResultState is_ok,
			      const gchar *answer, gpointer manager);

/* ****************** *
 *  Global Variables  *
 * ****************** */

static guint          signals[LAST_SIGNAL] = { 0 };
static gpointer       default_manager = NULL;
static gchar         *default_config_file = NULL;


/* ******************* *
 *  GType Declaration  *
 * ******************* */

G_DEFINE_TYPE (FusaManager, fusa_manager, G_TYPE_OBJECT);


/* ********************** *
 *  GType Initialization  *
 * ********************** */
static void gdm_callback_config_file(GdmResultState is_ok,
    const gchar *answer, gpointer dummy)
{
  if (is_ok == GDM_RESULT_OK &&
      (!default_config_file ||
       strcmp (answer, default_config_file) != 0))
    {
    if (default_config_file)
      g_free (default_config_file);

    default_config_file = g_strdup (answer);
    }
}

static void
fusa_manager_class_init (FusaManagerClass *class)
{
  GObjectClass *gobject_class;
#if 0
  gchar *config_result;
#endif

  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->finalize = fusa_manager_finalize;

  signals[USER_ADDED] =
    g_signal_new ("user-added",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (FusaManagerClass, user_added),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__OBJECT,
		  G_TYPE_NONE, 1, FUSA_TYPE_USER);
  signals[USER_REMOVED] =
    g_signal_new ("user-removed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (FusaManagerClass, user_removed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__OBJECT,
		  G_TYPE_NONE, 1, FUSA_TYPE_USER);
  signals[DISPLAY_ADDED] =
    g_signal_new ("display-added",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (FusaManagerClass, display_added),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__OBJECT,
		  G_TYPE_NONE, 1, FUSA_TYPE_DISPLAY);
  signals[DISPLAY_REMOVED] =
    g_signal_new ("display-removed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (FusaManagerClass, display_removed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__OBJECT,
		  G_TYPE_NONE, 1, FUSA_TYPE_DISPLAY);

  ask_gdm(gdm_callback_config_file, NULL, GDM_CMD_GET_CONFIG_FILE);
}

static void
fusa_manager_init (FusaManager *manager)
{
  GError *error;
  gchar *uri;
  GnomeVFSResult result;

  /* GDM config file */
  manager->exclusions = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, NULL);
  reload_gdm_config (manager, FALSE);
  error = NULL;
  uri = g_filename_to_uri (GDMCONFIGFILE, NULL, &error);
  if (!uri)
    {
      g_critical ("Could not create URI for GDM configuration file `%s': %s",
		  GDMCONFIGFILE, error->message);
      g_error_free (error);
    }
  else
    {
      result = gnome_vfs_monitor_add (&(manager->gdmconfig_monitor), uri,
				      GNOME_VFS_MONITOR_FILE,
				      gdmconfig_monitor_cb, manager);
      g_free (uri);

      if (result != GNOME_VFS_OK)
	g_critical ("Could not install monitor for GDM configuration file `%s': %s",
		    GDMCONFIGFILE, gnome_vfs_result_to_string (result));
    }

  /* /etc/shells */
  manager->shells = g_hash_table_new_full (g_str_hash, g_str_equal,
					   g_free, NULL);
  reload_shells (manager);
  error = NULL;
  uri = g_filename_to_uri ("/etc/shells", NULL, &error);
  if (!uri)
    {
      g_critical ("Could not create URI for shells file `/etc/shells': %s",
		  error->message);
      g_error_free (error);
    }
  else
    {
      result = gnome_vfs_monitor_add (&(manager->shells_monitor), uri,
				      GNOME_VFS_MONITOR_FILE,
				      shells_monitor_cb, manager);
      g_free (uri);

      if (result != GNOME_VFS_OK)
	g_critical ("Could not install monitor for shells file `/etc/shells': %s",
		    gnome_vfs_result_to_string (result));
    }

  /* /etc/passwd */
  manager->users =
    g_hash_table_new_full (g_str_hash, g_str_equal,
			   g_free, (GDestroyNotify) g_object_run_dispose);
  manager->users_by_uid = g_hash_table_new (NULL, NULL);
  error = NULL;
  uri = g_filename_to_uri ("/etc/passwd", NULL, &error);
  if (!uri)
    {
      g_critical ("Could not create URI for password file `/etc/passwd': %s",
		  error->message);
      g_error_free (error);
    }
  else
    {
      result = gnome_vfs_monitor_add (&(manager->passwd_monitor), uri,
				      GNOME_VFS_MONITOR_FILE,
				      passwd_monitor_cb, manager);
      g_free (uri);

      if (result != GNOME_VFS_OK)
	g_critical ("Could not install monitor for password file `/etc/passwd: %s",
		    gnome_vfs_result_to_string (result));
    }


  /* Displays */
  manager->displays =
    g_hash_table_new_full (g_str_hash, g_str_equal,
			   g_free, (GDestroyNotify) g_object_run_dispose);
  manager->displays_by_console = g_hash_table_new (NULL, NULL);

  manager->users_dirty = FALSE;
  manager->icons_dirty = FALSE;
}

void
fusa_manager_request_update_displays (FusaManager *manager,
				      FusaManagerDisplayCallback callback,
				      gpointer *data)
{
  ask_gdm (gdm_callback_update_displays,
	   new_fusa_manager_with_callback (manager, callback, NULL, data),
	   GDM_CMD_CONSOLE_SERVERS);
}

/* ******************* *
 *  GObject Functions  *
 * ******************* */

static void
fusa_manager_finalize (GObject *object)
{
  FusaManager *manager;

  manager = FUSA_MANAGER (object);

  if (manager->update_displays_id)
    g_source_remove (manager->update_displays_id);

  if (manager->results_pull_id)
    g_source_remove (manager->results_pull_id);

  g_hash_table_destroy (manager->displays);
  g_hash_table_destroy (manager->displays_by_console);

  gnome_vfs_monitor_cancel (manager->shells_monitor);
  g_hash_table_destroy (manager->shells);

  gnome_vfs_monitor_cancel (manager->gdmconfig_monitor);
  g_hash_table_destroy (manager->exclusions);
  g_free (manager->global_face_dir);

  gnome_vfs_monitor_cancel (manager->passwd_monitor);
  g_hash_table_destroy (manager->users);
  g_hash_table_destroy (manager->users_by_uid);

  if (G_OBJECT_CLASS (fusa_manager_parent_class)->finalize)
    (*G_OBJECT_CLASS (fusa_manager_parent_class)->finalize) (object);
}


/* *************************** *
 *  GnomeVFSMonitor Callbacks  *
 * *************************** */

static void
gdmconfig_monitor_cb (GnomeVFSMonitorHandle    *handle,
		      const gchar              *text_uri,
		      const gchar              *info_uri,
		      GnomeVFSMonitorEventType  event_type,
		      gpointer                  data)
{
  FusaManager *manager = data;

  if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED &&
      event_type != GNOME_VFS_MONITOR_EVENT_CREATED)
    return;
  
  manager->users_dirty = FALSE;
  manager->icons_dirty = FALSE;

  reload_gdm_config (data, TRUE);
}

static void
shells_monitor_cb (GnomeVFSMonitorHandle    *handle,
		   const gchar              *text_uri,
		   const gchar              *info_uri,
		   GnomeVFSMonitorEventType  event_type,
		   gpointer                  data)
{
  if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED &&
      event_type != GNOME_VFS_MONITOR_EVENT_CREATED)
    return;

  reload_shells (data);
  reload_passwd (data);
}

static void
passwd_monitor_cb (GnomeVFSMonitorHandle    *handle,
		   const gchar              *text_uri,
		   const gchar              *info_uri,
		   GnomeVFSMonitorEventType  event_type,
		   gpointer                  user_data)
{
  if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED &&
      event_type != GNOME_VFS_MONITOR_EVENT_CREATED)
    return;

  reload_passwd (user_data);
}

static FusaManagerWithCallback*
new_fusa_manager_with_callback(
  FusaManager *manager,
  FusaManagerDisplayCallback callback,
  FusaDisplay *display_for_callback,
  gpointer data_for_callback)
{
  FusaManagerWithCallback *result = g_new0(FusaManagerWithCallback, 1);

  result->manager = manager;
  result->callback = callback;
  result->display_for_callback = display_for_callback;
  result->name_of_display = NULL;
  result->data_for_callback = data_for_callback;

  return result;
}

/* ************************ *
 *  User Updates Functions  *
 * ************************ */

static void
emit_user_icon_changed_signals (gpointer key,
			        gpointer value,
			        gpointer user_data)
{
  _fusa_user_icon_changed (value);
}

static inline gboolean
strv_equals_string_table (gchar      **strv,
			  GHashTable  *table)
{
  guint i;

  if (!strv)
    return FALSE;

  /* Ensure sizes are equal */
  if (g_strv_length (strv) != g_hash_table_size (table))
    return FALSE;

  /* Ensure that everything in strv is also in table */
  for (i = 0; strv[i] != NULL; i++)
    {
      if (!g_hash_table_lookup (table, strv[i]))
	return FALSE;
    }

  return TRUE;
}

static gboolean
check_user_file (const gchar *filename,
		 uid_t        user,
		 gssize       max_file_size,
		 gboolean     relax_group,
		 gboolean     relax_other)
{
  struct stat fileinfo;

  if (max_file_size < 0)
    max_file_size = G_MAXSIZE;

  /* Exists/Readable? */
  if (stat (filename, &fileinfo) < 0)
    return FALSE;

  /* Is a regular file */
  if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode)))
    return FALSE;

  /* Owned by user? */
  if (G_UNLIKELY (fileinfo.st_uid != user))
    return FALSE;

  /* Group not writable or relax_group? */
  if (G_UNLIKELY ((fileinfo.st_mode & S_IWGRP) == S_IWGRP && !relax_group))
    return FALSE;

  /* Other not writable or relax_other? */
  if (G_UNLIKELY ((fileinfo.st_mode & S_IWOTH) == S_IWOTH && !relax_other))
    return FALSE;

  /* Size is kosher? */
  if (G_UNLIKELY (fileinfo.st_size > max_file_size))
    return FALSE;

  return TRUE;
}


/* Returns TRUE if changed */
static gboolean
merge_gdm_exclusions (FusaManager  *manager,
		      gchar       **excludev,
		      const gchar  *exclude_default[])
{
  gint i;

  if (strv_equals_string_table (excludev, manager->exclusions))
    return FALSE;

  g_hash_table_foreach_remove (manager->exclusions,
			       clear_hash_table_hrfunc, NULL);

  if (exclude_default)
    {
      for (i = 0; exclude_default[i] != NULL; i++)
        g_hash_table_insert (manager->exclusions,
			     g_strdup (exclude_default[i]),
			     GUINT_TO_POINTER (TRUE));
    }

  if (excludev)
    {
      for (i = 0; excludev[i] != NULL; i++)
        g_hash_table_insert (manager->exclusions,
			     g_strdup (excludev[i]), GUINT_TO_POINTER (TRUE));
    }

  return TRUE;
}

static void resolve_dirty_flags(GdmResultState dummy1, const gchar *dummy2, gpointer data)
{
  FusaManager *manager = data;

  if (manager->users_dirty)
    reload_passwd (data);

  if (manager->icons_dirty)
    g_hash_table_foreach (FUSA_MANAGER (data)->users,
			  emit_user_icon_changed_signals, NULL);

  manager->users_dirty = FALSE;
  manager->icons_dirty = FALSE;
}

/* We need to pull in settings from the gdm daemon. When we ask it
 * about something, it sends us back a string. So we need a set of
 * functions which can parse these strings and do useful things with
 * the information...
 */

static void handler_MinimalUID(GdmResultState is_ok, const gchar *value, gpointer data)
{
  FusaManager *manager = data;
  uid_t tmp_minimal_uid = strtoul (value, NULL, 10);

  if (tmp_minimal_uid != ULONG_MAX) {
    manager->minimal_uid = tmp_minimal_uid;
    manager->users_dirty = TRUE;
  }
}

static void handler_Exclude(GdmResultState is_ok, const gchar *value, gpointer data)
{
  FusaManager *manager = data;
  const gchar *exclude_default[] = DEFAULT_EXCLUDE;
  gchar** excludev = g_strsplit (value, ",", G_MAXINT);
  
  if (merge_gdm_exclusions (manager, excludev, exclude_default))
    manager->users_dirty = TRUE;
}

static void handler_GlobalFaceDir(GdmResultState is_ok, const gchar *value, gpointer data)
{
  FusaManager *manager = data;
  if (!manager->global_face_dir ||
      strcmp (value, manager->global_face_dir) != 0)
    {
      g_free (manager->global_face_dir);
      manager->global_face_dir = g_strdup (value);
      manager->icons_dirty = TRUE;
    }
}

static void handler_UserMaxFile(GdmResultState is_ok, const gchar *value, gpointer data)
{
  FusaManager *manager = data;
  gsize tmp_user_max_file = strtol (value, NULL, 10);

  if (tmp_user_max_file != ULONG_MAX && tmp_user_max_file != manager->user_max_file)
    {
      manager->user_max_file = tmp_user_max_file;
      manager->icons_dirty = TRUE;
    }
}

static void handler_AllowRoot(GdmResultState is_ok, const gchar *value, gpointer data)
{
  FusaManager *manager = data;
  gboolean tmp_allow_root = strcasecmp(value, "false") != 0;
  
  if (tmp_allow_root != manager->allow_root)
    {
      manager->allow_root = tmp_allow_root;
      manager->users_dirty = TRUE;
    }
}

static void handler_MaxIconSize(GdmResultState is_ok, const gchar *value, gpointer data)
{
  FusaManager *manager = data;
  gint tmp_max_icon_size = strtol (value, NULL, 10);

  if (tmp_max_icon_size > manager->max_icon_size)
    {
      manager->max_icon_size = tmp_max_icon_size;
      manager->icons_dirty = TRUE;
    }
}

/* ...and we need a table mapping the strings which identify settings to
 * these functions.
 */
struct SettingsHandler {
        gchar *key;
        GdmMessageCallback *handler;
} settings_handlers[] = {
  { "greeter/MinimalUID", handler_MinimalUID },
  { "greeter/Exclude", handler_Exclude },
  { "greeter/GlobalFaceDir", handler_GlobalFaceDir },
  { "security/UserMaxFile", handler_UserMaxFile },
  { "security/AllowRoot", handler_AllowRoot },
  { "gui/MaxIconWidth", handler_MaxIconSize },
  { "gui/MaxIconHeight", handler_MaxIconSize },
  { 0, 0 },
};

/* Now we just walk through the table, asking the daemon about each
 * entry and calling the handler when we hear an answer.
 */
static void
reload_gdm_config (FusaManager *manager,
                   gboolean    force_reload)
{
  struct SettingsHandler *handler;

  for (handler=settings_handlers; handler->key != NULL; handler++) {

    /* If we're here because the file has been updated, then *we* know the
     * value might have changed (because gnome-vfs told us), but gdm is most
     * probably still unaware. Therefore, we must tell it to go and look.
     * Unfortunately, there's no way of telling gdm over the socket to go
     * and reload the file in toto, but we can tell it to reload those
     * fields we're checking, and that's probably good enough.
     */

    if (force_reload) {
      ask_gdm (NULL, NULL, GDM_CMD_UPDATE_CONFIG, handler->key);
    }

    ask_gdm (handler->handler, manager, GDM_CMD_GET_CONFIG, handler->key);
  }

  ask_gdm (resolve_dirty_flags, manager, NULL);
}

static void
reload_shells (FusaManager *manager)
{
  gchar *shell;

  setusershell();

  g_hash_table_foreach_remove (manager->shells, clear_hash_table_hrfunc, NULL);
  for (shell = getusershell (); shell; shell = getusershell ())
    {
      g_hash_table_insert (manager->shells, g_strdup (shell),
			   GUINT_TO_POINTER (TRUE));
    }

  endusershell ();
}

static void
reload_passwd (FusaManager *manager)
{
  struct passwd *pwent;
  GSList *old_users, *new_users, *list;
  
  old_users = NULL;
  new_users = NULL;

  g_hash_table_foreach (manager->users, listify_hash_values_hfunc, &old_users);
  g_slist_foreach (old_users, (GFunc) g_object_ref, NULL);

  /* Make sure we keep users who are logged in no matter what. */
  for (list = old_users; list; list = list->next)
    {
      if (fusa_user_get_n_displays (list->data))
	{
	  g_object_freeze_notify (G_OBJECT (list->data));
	  new_users = g_slist_prepend (new_users, g_object_ref (list->data));
	}
    }

  setpwent ();

  for (pwent = getpwent (); pwent; pwent = getpwent ())
    {
      FusaUser *user;

      user = NULL;

      /* Skip users below MinimalUID... */
      if (pwent->pw_uid < manager->minimal_uid)
	continue;

      /* ...And users w/ invalid shells... */
      if (!pwent->pw_shell ||
	  !g_hash_table_lookup (manager->shells, pwent->pw_shell))
	continue;

      /* ...And explicitly excluded users */
      if (g_hash_table_lookup (manager->exclusions, pwent->pw_name))
	continue;

      user = g_hash_table_lookup (manager->users, pwent->pw_name);

      /* Update users already in the *new* list */
      if (g_slist_find (new_users, user))
	{
	  _fusa_user_update (user, pwent);
	  continue;
	}

      if (!user)
	user = g_object_new (FUSA_TYPE_USER, "manager", manager, NULL);
      else
	g_object_ref (user);

      /* Freeze & update users not already in the new list */
      g_object_freeze_notify (G_OBJECT (user));
      _fusa_user_update (user, pwent);

      new_users = g_slist_prepend (new_users, user);
    }
  
  endpwent ();

  /* Go through and handle added users */
  for (list = new_users; list; list = list->next)
    {
      if (!g_slist_find (old_users, list->data))
	{
	  g_hash_table_insert (manager->users,
			       g_strdup (fusa_user_get_user_name (list->data)),
			       g_object_ref (list->data));
	  g_hash_table_insert (manager->users_by_uid,
			       GINT_TO_POINTER (fusa_user_get_uid (list->data)),
			       list->data);
	  g_signal_emit (manager, signals[USER_ADDED], 0, list->data);
	}
    }

  /* Go through and handle removed users */
  for (list = old_users; list; list = list->next)
    {
      if (!g_slist_find (new_users, list->data))
	{
	  g_signal_emit (manager, signals[USER_REMOVED], 0, list->data);
	  g_hash_table_remove (manager->users_by_uid,
			       GINT_TO_POINTER (fusa_user_get_uid (list->data)));
	  g_hash_table_remove (manager->users,
			       fusa_user_get_user_name (list->data));
          /* FIXME: signals[USER_REMOVED]??? Why not? */
	}
    }

  /* Cleanup */
  g_slist_foreach (new_users, (GFunc) g_object_thaw_notify, NULL);
  g_slist_foreach (new_users, (GFunc) g_object_unref, NULL);
  g_slist_free (new_users);

  g_slist_foreach (old_users, (GFunc) g_object_unref, NULL);
  g_slist_free (old_users);
}


/* ********************************** *
 *  Displays - Main Thread Functions  *
 * ********************************** */

static gboolean
clean_displays_table (gpointer key,
		      gpointer value,
		      gpointer data)
{
  CleanDisplaysData *clean;
  GSList *li;

  clean = data;

  li = g_slist_find (clean->displays, value);

  if (!li)
    {
      g_signal_emit (clean->manager, signals[DISPLAY_REMOVED], 0, value);
      g_hash_table_remove (clean->manager->displays_by_console,
			   GINT_TO_POINTER (fusa_display_get_console (value)));
      return TRUE;
    }
  else
    {
      g_object_unref (li->data);
      clean->displays = g_slist_delete_link (clean->displays, li);
    }

  return FALSE;
}

static void
process_display_updates (FusaManager *manager,
                         DisplayUpdate *items,
                         guint n_items)
{
  CleanDisplaysData clean_data;
  FusaDisplay *display;
  FusaUser *user;
  guint i;

  clean_data.manager = manager;
  clean_data.displays = NULL;

  for (i = 0; i < n_items; i++)
    {
      if (items[i].username != NULL)
	{
	  user = fusa_manager_get_user (manager,
					items[i].username);
	}
      else
	user = NULL;

      display = real_get_display (manager, items[i].name);

      if (display)
	{
	  _fusa_display_set_user (display, user);
	  g_object_ref (display);
	}
      else
	{
	  gint console;
	  gboolean nested;

	  console = -1;
	  nested = (items[i].location[0] == ':');

	  /* Xnest: We don't need to loop to get around Xnest-in-Xnest insanity
	   * becase all existing FusaDisplays will have a console set from their
	   * insert -- plus that's just crackish anyways. */
	  if (nested)
	    {
	      FusaDisplay *display;
	      gchar *ptr;

	      /* Strip the ".0" */
	      ptr = strchr (items[i].location, '.');
	      if (ptr)
		*ptr = '\0';

	      display = real_get_display (manager,
					  items[i].location);

	      if (display)
		console = fusa_display_get_console (display);
	    }
	  /* Console */
	  else if (sscanf (items[i].location, "%d",
			   &console) != 1)
	    {
	      g_critical ("Invalid display location returned from GDM: `%s'.",
			  items[i].location);
	    }

	  display = _fusa_display_new (manager,
				       items[i].name, user,
				       console, nested);

	  g_hash_table_insert (manager->displays,
			       g_strdup (items[i].name),
			       g_object_ref (display));
	  g_hash_table_insert (manager->displays_by_console,
			       GINT_TO_POINTER (console), display);

	  g_signal_emit (manager, signals[DISPLAY_ADDED], 0, display);
	}

      clean_data.displays = g_slist_prepend (clean_data.displays, display);
    }

  g_hash_table_foreach_remove (manager->displays, clean_displays_table,
			       &clean_data);
}

static GError*
g_error_from_gdm_answer (GdmResultState is_ok,
    const gchar *answer)
{
  gint code;
  const gchar *message;
 
  g_assert (is_ok != GDM_RESULT_OK);
  
  if (is_ok == GDM_RESULT_BIZARRE || answer == NULL || sscanf (answer, "%d", &code) != 1)
    {
      code = FUSA_MANAGER_DM_ERROR_UNKNOWN;
      message = _("The display manager could not be contacted for unknown reasons.");
    }
  else
    {
      switch (code)
	{
	case FUSA_MANAGER_DM_ERROR_NOT_RUNNING:
	  message = _("The display manager is not running or too old.");
	  break;
	case FUSA_MANAGER_DM_ERROR_FLEXI_LIMIT_REACHED:
	  message = _("The configured limit of flexible servers has been reached.");
	  break;
	case FUSA_MANAGER_DM_ERROR_X_ERROR:
	  message = _("There was an unknown error starting X.");
	  break;
	case FUSA_MANAGER_DM_ERROR_X_FAILED:
	  message = _("The X server failed to finish starting.");
	  break;
	case FUSA_MANAGER_DM_ERROR_X_LIMIT_REACHED:
	  message = _("There are too many X sessions running.");
	  break;
	case FUSA_MANAGER_DM_ERROR_XNEST_AUTH_ERROR:
	  message = _("The nested X server (Xnest) cannot connect to your current X server.");
	  break;
	case FUSA_MANAGER_DM_ERROR_X_NOT_FOUND:
	  message = _("The X server in the GDM configuration could not be found.");
	  break;
	case FUSA_MANAGER_DM_ERROR_LOGOUT_ACTION:
	  message = _("Trying to set an unknown logout action, or trying to set a logout action which is not available.");
	  break;
	case FUSA_MANAGER_DM_ERROR_CONSOLES_UNSUPPORTED:
	  message = _("Virtual terminals not supported.");
	  break;
	case FUSA_MANAGER_DM_ERROR_INVALID_CONSOLE:
	  message = _("Invalid virtual terminal number.");
	  break;
	case FUSA_MANAGER_DM_ERROR_UNKNOWN_CONFIG:
	  message = _("Trying to update an unsupported configuration key.");
	  break;
	case FUSA_MANAGER_DM_ERROR_PERMISSIONS:
	  message = _("~/.Xauthority file badly configured or missing.");
	  break;
	case FUSA_MANAGER_DM_ERROR_TOO_MANY_MESSAGES:
	  message = _("Too many messages were sent to the display manager, and it hung up.");
	  break;
	default:
	  code = FUSA_MANAGER_DM_ERROR_OTHER;
	  message = _("The display manager sent an unknown error message.");
	  break;
	}
    }
  return g_error_new (FUSA_MANAGER_DM_ERROR, code,
	"%s\n%s", message, answer);
 
}

static void
gdm_callback_update_displays (GdmResultState is_ok,
    const gchar *answer, gpointer data)
{
  FusaManagerWithCallback *manager_with_callback = (FusaManagerWithCallback *) data;
  gchar **displays;
  DisplayUpdate *items;
  guint n_items;  

  if (is_ok == GDM_RESULT_OK) {
    displays = g_strsplit (answer, ";", -1);

    if (displays)
      {
      guint i;

      i = g_strv_length (displays);
      if (i)
        {
        items = g_new0 (DisplayUpdate, i);
        n_items = i;

        while (i > 0)
          {
          gchar **args;

          i--;

          args = g_strsplit (displays[i], ",", 3);

          /* Display name (e.g. :0) */
          items[i].name = args[0];

          /* Username */
          if (args[1] != NULL && args[1][0] != '\0')
            items[i].username = args[1];
          else
            {
            items[i].username = NULL;
            g_free (args[1]);
            }

          /* VT or display name Location (e.g "7" or ":0.0") */
          items[i].location = args[2];

          g_free (args);

          }

        /* FIXME: This is the only place p_d_u is called. Merge it in here. */
        process_display_updates (manager_with_callback->manager,
            items,
            n_items);
        }

      g_strfreev (displays);
      }

    /* Now that we know which displays have what names, we can look up to see
     * whether we stashed away the name of a display earlier before we could
     * look it up.
     */

    if (manager_with_callback->name_of_display != NULL &&
        manager_with_callback->display_for_callback == NULL) {

      manager_with_callback->display_for_callback =
        real_get_display (manager_with_callback->manager,
            manager_with_callback->name_of_display);

      g_free (manager_with_callback->name_of_display);
 
    }

    if (manager_with_callback->callback)
    manager_with_callback->callback(
        manager_with_callback->manager,
        manager_with_callback->display_for_callback,
        NULL,
        manager_with_callback->data_for_callback
        );
  } else if (manager_with_callback->callback) {
    GError *error = g_error_from_gdm_answer (is_ok, answer);
    manager_with_callback->callback(
        manager_with_callback->manager,
        manager_with_callback->display_for_callback,
        error,
        manager_with_callback->data_for_callback
        );
    g_error_free (error);
  }

  g_free (manager_with_callback);
}

static void
gdm_callback_activate_display (GdmResultState is_ok,
    const gchar *answer, gpointer data)
{
  FusaManagerWithCallback *manager_with_callback = (FusaManagerWithCallback *) data;

  g_assert (manager_with_callback != NULL);

  if (is_ok == GDM_RESULT_OK) {

    /* Yay, we updated the display. We need to make a note of
     * where the new display is.
     */

    if (manager_with_callback->name_of_display == NULL) {
      manager_with_callback->name_of_display = g_strdup (answer);
    }
    
    /* Better re-check the display list. */
    ask_gdm (gdm_callback_update_displays,
        manager_with_callback, 
        GDM_CMD_CONSOLE_SERVERS);

    /* But... do not free manager_with_callback because we're passing it
     * straight through to gdm_callback_update_displays.
     */

  } else if (manager_with_callback->callback) {
    GError *error = g_error_from_gdm_answer (is_ok, answer);
    manager_with_callback->callback(
        manager_with_callback->manager,
        manager_with_callback->display_for_callback,
        error,
        manager_with_callback->data_for_callback
        );
    g_free (manager_with_callback);
  }
}

/* ******************* *
 *  Utility Functions  *
 * ******************* */

static void
listify_hash_values_hfunc (gpointer key,
			   gpointer value,
			   gpointer user_data)
{
  GSList **list = user_data;

  *list = g_slist_prepend (*list, value);
}

static gboolean
clear_hash_table_hrfunc (gpointer key,
			 gpointer value,
			 gpointer user_data)
{
  return TRUE;
}

static FusaDisplay *
real_get_display (FusaManager *manager,
		  const gchar *name)
{
  FusaDisplay *display;
  gchar *ptr, *key;

  ptr = strchr (name, '.');
  if (ptr)
    key = g_strndup (name, ptr - name);
  else
    key = g_strdup (name);

  display = g_hash_table_lookup (manager->displays, key);
  g_free (key);

  return display;
}


/* ************************************************************************** *
 *  Semi-Private API                                                          *
 * ************************************************************************** */

static GdkPixbuf *
render_icon_from_home (FusaManager  *manager,
		       FusaUser     *user,
		       GtkWidget    *widget,
		       gint          icon_size)
{
  GdkPixbuf *retval;
  const gchar *homedir, *username;
  uid_t uid;
  gchar *path;
  GnomeVFSURI *uri;
  gboolean is_local;

  homedir = fusa_user_get_home_directory (user);
  uid = fusa_user_get_uid (user);
  username = NULL;

  /* special case: look at parent of home to detect autofs
     this is so we don't try to trigger an automount */
  path = g_path_get_dirname (homedir);
  uri = gnome_vfs_uri_new (path);
  is_local = gnome_vfs_uri_is_local (uri);
  gnome_vfs_uri_unref (uri);
  g_free (path);

  /* now check that home dir itself is local */
  if (is_local)
    {
      uri = gnome_vfs_uri_new (homedir);
      is_local = gnome_vfs_uri_is_local (uri);
      gnome_vfs_uri_unref (uri);
    }

  /* only look at local home directories so we don't try to
     read from remote (e.g. NFS) volumes */
  if (!is_local)
    return NULL;

  /* First, try "~/.face" */
  path = g_build_filename (homedir, ".face", NULL);
  if (check_user_file (path, uid, manager->user_max_file,
		       manager->relax_group, manager->relax_other))
    retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size, NULL);
  else
    retval = NULL;
  g_free (path);

  /* Next, try "~/.face.icon" */
  if (!retval)
    {
      path = g_build_filename (homedir, ".face.icon", NULL);
      if (check_user_file (path, uid, manager->user_max_file,
			   manager->relax_group, manager->relax_other))
	retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size,
						   NULL);
      else
	retval = NULL;

      g_free (path);
    }

  /* Still nothing, try the user's personal GDM config */
  if (!retval)
    {
      path = g_build_filename (homedir, ".gnome", "gdm", NULL);
      if (check_user_file (path, uid, manager->user_max_file,
			   manager->relax_group, manager->relax_other))
	{
	  GKeyFile *keyfile;
	  gchar *icon_path;

	  keyfile = g_key_file_new ();
	  g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL);

	  icon_path = g_key_file_get_string (keyfile, "face", "picture", NULL);
	  if (icon_path &&
	      check_user_file (icon_path, uid, manager->user_max_file,
			       manager->relax_group, manager->relax_other))
	    retval = gdk_pixbuf_new_from_file_at_size (path, icon_size,
						       icon_size, NULL);
	  else
	    retval = NULL;

	  g_free (icon_path);
	  g_key_file_free (keyfile);
	}
      else
	retval = NULL;

      g_free (path);
    }

  /* Still nothing, try "~/.gnome/photo" */
  if (!retval)
    {
      path = g_build_filename (homedir, ".gnome", "photo", NULL);
      if (check_user_file (path, uid, manager->user_max_file,
			   manager->relax_group, manager->relax_other))
	retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size,
						   NULL);
      else
	retval = NULL;

      g_free (path);
    }

  return retval;
}

GdkPixbuf *
_fusa_manager_render_icon (FusaManager  *manager,
			   FusaUser     *user,
			   GtkWidget    *widget,
			   gint          icon_size)
{
  GdkPixbuf *retval;
  uid_t uid;
  const gchar *username;
  gchar *path;
  gchar *tmp;
  GtkIconTheme *theme;

  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);
  g_return_val_if_fail (widget == NULL || GTK_IS_WIDGET (widget), NULL);
  g_return_val_if_fail (icon_size > 12, NULL);

  retval = render_icon_from_home (manager, user, widget, icon_size);

  if (retval)
    return retval;

  uid = fusa_user_get_uid (user);
  username = fusa_user_get_user_name (user);

  /* Try ${GlobalFaceDir}/${username} */
  path = g_build_filename (manager->global_face_dir, username, NULL);
  if (check_user_file (path, uid, manager->user_max_file,
		       manager->relax_group, manager->relax_other))
    retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size,
					       NULL);
  else
    retval = NULL;

  g_free (path);
  if (retval)
    return retval;

  /* Finally, ${GlobalFaceDir}/${username}.png */
  tmp = g_strconcat (username, ".png", NULL);
  path = g_build_filename (manager->global_face_dir, tmp, NULL);
  g_free (tmp);
  if (check_user_file (path, uid, manager->user_max_file,
		       manager->relax_group, manager->relax_other))
    retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size,
					       NULL);
  else
    retval = NULL;

  g_free (path);
  if (retval)
    return retval;

  /* Nothing yet, use stock icon */
  if (widget && gtk_widget_has_screen (widget))
    theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
  else
    theme = gtk_icon_theme_get_default ();

  retval = gtk_icon_theme_load_icon (theme, DEFAULT_USER_ICON, icon_size,
				     0, NULL);

  if (!retval)
    retval = gtk_icon_theme_load_icon (theme, GTK_STOCK_MISSING_IMAGE,
				       icon_size, 0, NULL);

  return retval;
}

/* ************************************************************************** *
 *  Public API                                                                *
 * ************************************************************************** */

/**
 * fusa_manager_ref_default:
 * 
 * Retrieves a reference to the default manager object. This reference must be
 * released with g_object_unref() when no longer needed.
 * 
 * Returns: a reference to a #FusaManager object.
 **/
FusaManager *
fusa_manager_ref_default (void)
{
  if (default_manager)
    g_object_ref (default_manager);
  else
    {
      default_manager = g_object_new (FUSA_TYPE_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (default_manager),
				 (gpointer *) &default_manager);
    }

  return default_manager;
}

/**
 * fusa_manager_get_display:
 * @manager: the manager to query.
 * @name: the name of the X11 display to retrieve (e.g. ":0").
 * 
 * Retrieves a pointer to the #FusaDisplay object from @manager which
 * corresponds to the @name display. The returned value is not a reference, and
 * should not be released.
 * 
 * Returns: a pointer to a #FusaDisplay object.
 **/
FusaDisplay *
fusa_manager_get_display (FusaManager *manager,
			  const gchar *name)
{
  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);
  g_return_val_if_fail (name != NULL && name[0] == ':', NULL);

  return real_get_display (manager, name);
}

/**
 * fusa_manager_get_display_by_screen:
 * @manager: the manager to query.
 * @screen: the GDK representation of a screen.
 * 
 * Convenience function which retrieves a pointer to the #FusaDisplay object
 * from @manager which corresponds to the @screen display. The returned value is
 * not a reference, and should not be released.
 * 
 * Returns: a pointer to a #FusaDisplay object.
 **/
FusaDisplay *
fusa_manager_get_display_by_screen (FusaManager *manager,
				    GdkScreen   *screen)
{
  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);
  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  return real_get_display (manager,
			   gdk_display_get_name (gdk_screen_get_display (screen)));
}

/**
 * fusa_manager_list_displays:
 * @manager: the manager to query.
 * 
 * Retrieves a newly-allocated list of pointers to #FusaDisplay objects known to
 * @manager. This list must be freed with g_slist_free() when no longer needed,
 * but the list items themselves are not references and should not be released.
 * 
 * Returns: a singly-linked list of pointers to #FusaDisplay objects.
 **/
GSList *
fusa_manager_list_displays (FusaManager *manager)
{
  GSList *retval;

  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);

  retval = NULL;
  g_hash_table_foreach (manager->displays, listify_hash_values_hfunc, &retval);

  return retval;
}

/**
 * fusa_manager_activate_display:
 * @manager: the manager to query.
 * @display: the display to switch to.
 * @screen: the screen which the application is running on.
 * 
 * Attempts to asynchronously change the current display to @display from
 * @screen using @manager. If @display is a console-backed display, the display
 * will switch. If @display is a nested (Xnest) display, the window will be
 * activated. If this action was successful, the "display-activated" signal will
 * be emitted.
 **/
void
fusa_manager_activate_display (FusaManager                *manager,
			       FusaDisplay                *display,
			       GdkScreen                  *screen,
			       FusaManagerDisplayCallback  func,
			       gpointer                    data,
			       GDestroyNotify              notify)
{
  FusaDisplay *this_display;
  gint console;

  this_display =
    real_get_display (manager,
		      gdk_display_get_name (gdk_screen_get_display (screen)));

  if (display == this_display)
    return;

  console = fusa_display_get_console (display);

  if (console != fusa_display_get_console (this_display))
    {
    /* Switch to another console. Easy. */
    queue_authentication (screen);
    ask_gdm (gdm_callback_activate_display,
        new_fusa_manager_with_callback(manager, func, display, data),
        GDM_CMD_SET_VT, console);
    }
  else 
    {
#ifdef WITH_XNEST
    /* Xnest on this console, find & activate it. */
      Display *xdisplay;
      Screen *xscreen;
      Window xrootwin, dummy, client;
      Window *children;
      unsigned int n_children;

      /* Modified from xc/programs/xlsclients/xlsclients.c
       * Copyright 1989, 1998  The Open Group */
      xscreen = gdk_x11_screen_get_xscreen (screen);
      xdisplay = DisplayOfScreen (xscreen);
      xrootwin = RootWindowOfScreen (xscreen);

      if (XQueryTree (xdisplay, xrootwin, &dummy, &dummy,
		      &children, &n_children))
	{
	  guint i;
	  const gchar *display_name;
	  gboolean found;

	  display_name = fusa_display_get_name (display);
	  found = FALSE;

	  for (i = 0; !found && i < n_children; i++)
	    {
	      XTextProperty tp;
	      char *wmname;
	      char **client_argv;
	      int client_argc;

	      client_argv = NULL;
	      client_argc = 0;
	      wmname = NULL;
	      tp.value = NULL;
	      tp.encoding = None;
	      client = XmuClientWindow (xdisplay, children[i]);

	      /* We intentionally don't strip any trailing dots here because
	       * GDM doesn't use them in it's exec'ing of Xnest -- anyways, if
	       * client is Xnest and argv[1] == display_name, activate it. */
	      if (!client || !XGetWMName (xdisplay, client, &tp) || !tp.value)
		continue;
	      
	      if (strcmp ("Xnest", (const char *) tp.value) == 0 &&
		  XGetCommand (xdisplay, client, &client_argv, &client_argc) &&
		  client_argc > 1 &&
		  strcmp (display_name, client_argv[1]) == 0)
		{
		  XEvent xev;

		  /* Modified from libwnck/libwnck/xutils.c
		   * Copyright 2001-2003 Havoc Pennington.
		   * Copyright 2002 Anders Carlson
		   * Copyright 2004-2005 Elijah Newren */
		  xev.xclient.type = ClientMessage;
		  xev.xclient.serial = 0;
		  xev.xclient.send_event = True;
		  xev.xclient.display = xdisplay;
		  xev.xclient.window = client;
		  xev.xclient.message_type = XInternAtom (xdisplay,
							  "_NET_ACTIVE_WINDOW",
							  FALSE);
		  xev.xclient.format = 32;
		  xev.xclient.data.l[0] = 2;
		  xev.xclient.data.l[1] = gtk_get_current_event_time ();
		  xev.xclient.data.l[2] = 0;
		  xev.xclient.data.l[3] = 0;
		  xev.xclient.data.l[4] = 0;

		  XSendEvent (xdisplay, xrootwin, False,
			      SubstructureRedirectMask | SubstructureNotifyMask,
			      &xev);
		  found = TRUE;
		}

	      if (client_argv)
		XFreeStringList (client_argv);

	      if (tp.value)
		XFree (tp.value);
	    }

	  XFree (children);

	  if (func)
	    (*func) (manager, display, NULL, data);

	  if (data && notify)
	    (*notify) (data);
	}
#endif /* WITH_XNEST */
    }
}

/**
 * fusa_manager_new_console:
 * @manager: the manager to query.
 * @screen: the screen which the application is running on.
 * 
 * Attempts to asynchronously create a new console-backed display. If the action
 * was successful, the "display-opened" signal will be emitted.
 **/
void
fusa_manager_new_console (FusaManager                *manager,
			  GdkScreen                  *screen,
			  FusaManagerDisplayCallback  func,
			  gpointer                    data,
			  GDestroyNotify              notify)
{
  g_return_if_fail (FUSA_IS_MANAGER (manager));

  queue_authentication (screen);

  ask_gdm (gdm_callback_activate_display,
      new_fusa_manager_with_callback (manager, func, NULL, data),
      GDM_CMD_FLEXI_XSERVER);
}

/**
 * fusa_manager_new_xnest:
 * @manager: the manager to query.
 * @screen: the screen which the application is running on.
 * 
 * Attempts to asynchronously create a new nested (Xnest) display. If the action
 * was successful, the "display-opened" signal will be emitted.
 **/
void
fusa_manager_new_xnest (FusaManager                *manager,
			GdkScreen                  *screen,
			FusaManagerDisplayCallback  func,
			gpointer                    data,
			GDestroyNotify              notify)
{
  gchar *mit_cookie;

  g_return_if_fail (FUSA_IS_MANAGER (manager));

  mit_cookie = get_mit_magic_cookie (screen, FALSE);
  
  queue_authentication (screen);

  ask_gdm (gdm_callback_update_displays,
      new_fusa_manager_with_callback (manager, func, NULL, data),
      GDM_CMD_FLEXI_XNEST,
      gdk_display_get_name (gdk_screen_get_display (screen)),
      (int) getuid (),
      mit_cookie,
      XauFileName ());

  g_free (mit_cookie);

}

/**
 * fusa_manager_get_user:
 * @manager: the manager to query.
 * @username: the login name of the user to get.
 * 
 * Retrieves a pointer to the #FusaUser object for the login named @username
 * from @manager. This pointer is not a reference, and should not be released.
 * 
 * Returns: a pointer to a #FusaUser object.
 **/
FusaUser *
fusa_manager_get_user (FusaManager *manager,
		       const gchar *username)
{
  FusaUser *user;

  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);
  g_return_val_if_fail (username != NULL && username[0] != '\0', NULL);

  user = g_hash_table_lookup (manager->users, username);

  if (!user)
    {
      struct passwd *pwent;

      pwent = getpwnam (username);

      if (pwent)
	{
	  user = g_object_new (FUSA_TYPE_USER, "manager", manager, NULL);
	  _fusa_user_update (user, pwent);
	  g_hash_table_insert (manager->users, g_strdup (pwent->pw_name), user);
	  g_hash_table_insert (manager->users_by_uid,
			       GINT_TO_POINTER (pwent->pw_uid), user);
	  g_signal_emit (manager, signals[USER_ADDED], 0, user);
	}
    }

  return user;
}

/**
 * fusa_manager_get_user_by_uid:
 * @manager: the manager to query.
 * @uid: the ID of the user to get.
 * 
 * Retrieves a pointer to the #FusaUser object for the @uid login from @manager.
 * This pointer is not a reference, and should not be released.
 * 
 * Returns: a pointer to a #FusaUser object.
 **/
FusaUser *
fusa_manager_get_user_by_uid (FusaManager *manager,
			      uid_t        uid)
{
  FusaUser *user;

  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);
  g_return_val_if_fail (uid >= 0, NULL);

  user = g_hash_table_lookup (manager->users_by_uid, GINT_TO_POINTER (uid));

  if (!user)
    {
      struct passwd *pwent;

      pwent = getpwuid (uid);

      if (pwent)
	{
	  user = g_object_new (FUSA_TYPE_USER, "manager", manager, NULL);
	  _fusa_user_update (user, pwent);
	  g_hash_table_insert (manager->users, g_strdup (pwent->pw_name), user);
	  g_hash_table_insert (manager->users_by_uid,
			       GINT_TO_POINTER (pwent->pw_uid), user);
	  g_signal_emit (manager, signals[USER_ADDED], 0, user);
	}
    }

  return user;
}

/**
 * fusa_manager_list_displays:
 * @manager: the manager to query.
 * 
 * Retrieves a newly-allocated list of pointers to #FusaUser objects known to
 * @manager. This list will contain all user who would ordinarily show up in the
 * GDM face browser, as well as any other users currently logged into managed X
 * displays. This list must be freed with g_slist_free() when no longer needed,
 * but the list items themselves are not references and should not be released.
 * 
 * Returns: a singly-linked list of pointers to #FusaUser objects.
 **/
GSList *
fusa_manager_list_users (FusaManager *manager)
{
  GSList *retval;

  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);

  retval = NULL;
  g_hash_table_foreach (manager->users, listify_hash_values_hfunc, &retval);

  return g_slist_sort (retval, (GCompareFunc) fusa_user_collate);
}

GQuark
fusa_manager_dm_error_get_quark (void)
{
  static GQuark quark = 0;

  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string ("fusa-manager-dm-error");

  return quark;
}
