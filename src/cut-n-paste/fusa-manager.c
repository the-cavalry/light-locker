/* 
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
#include <X11/Xmu/WinUtil.h>

#include <gdk/gdkx.h>

#include <gtk/gtkicontheme.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkstock.h>

#include <libgnomevfs/gnome-vfs-ops.h>

#include "gdmcomm.h"
#include "fusa-utils.h"
#include "fusa-display-private.h"
#include "fusa-user-private.h"

#include "fusa-manager-private.h"

#define DISABLE_XNEST 1

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
#define MINIMAL_GDM_VERSION	"2.6.0.1"


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

typedef enum
{
  OP_UPDATE_DISPLAYS,
  OP_NEW_CONSOLE,
  OP_NEW_XNEST,
  OP_ACTIVATE_DISPLAY
}
OpType;

typedef enum
{
  OP_RESULT_DISPLAY_UPDATES,
  OP_RESULT_DISPLAY_ACTIVATED,
  OP_RESULT_NEW_DISPLAY,
  OP_RESULT_ERROR
}
OpResultType;


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
  GnomeVFSMonitorHandle *passwd_monitor;
  GnomeVFSMonitorHandle *gdmconfig_monitor;
  GnomeVFSMonitorHandle *shells_monitor;
  GAsyncQueue *results_q;
  guint results_pull_id;
  guint update_displays_id;

  /* GDM Config */
  GHashTable *shells;
  GHashTable *exclusions;
  gchar *global_face_dir;
  uid_t minimal_uid;
  gsize user_max_file;
  gint max_icon_size;

  guint8 allow_root  : 1;
  guint8 relax_group : 1;
  guint8 relax_other : 1;
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

typedef struct _DisplayClosure
{
  FusaManagerDisplayCallback func;
  gpointer data;
  GDestroyNotify notify;
}
DisplayClosure;

typedef struct _Op
{
  FusaManager *manager;
  FusaDisplay *display;
  GdkScreen *screen;
  gint console;
  DisplayClosure *closure;

  OpType type:3;
}
Op;

typedef union _OpResult
{
  OpResultType type;

  struct {
    OpResultType type;
    DisplayUpdate *items;
    guint n_items;
  } updates;

  struct {
    OpResultType type;
    DisplayClosure *closure;
    gchar *name;
  } new;

  struct {
    OpResultType type;
    DisplayClosure *closure;
    FusaDisplay *display;
  } activate;

  struct {
    OpResultType type;
    DisplayClosure *closure;
    GError *error;
  } error;
}
OpResult;

typedef struct _CleanDisplaysData
{
  FusaManager *manager;
  GSList *displays;
}
CleanDisplaysData;


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
						       gboolean     *users_dirty,
						       gboolean     *icons_dirty);
static void            reload_shells                  (FusaManager  *manager);
static void            reload_passwd                  (FusaManager  *manager);

/* Displays - Main Thread Functions */
static void        op_result_free            (OpResult                   *result);
static inline void process_display_updates   (FusaManager                *manager,
					      OpResult                   *result);
static gboolean    results_pull_func         (gpointer                    data);
static gboolean    push_update_displays_func (gpointer                    data);
static void        push_dm_op                (FusaManager                *manager,
					      OpType                      type,
					      GdkScreen                  *screen,
					      FusaDisplay                *display,
					      FusaManagerDisplayCallback  func,
					      gpointer                    data,
					      GDestroyNotify              notify);

/* Displays - IO Thread Functions */
static void        push_new_result       (FusaManager *manager,
					  Op          *op,
					  const gchar *name);
static void        push_error_result     (FusaManager *manager,
					  Op          *op,
					  const gchar *result);
static gboolean    dm_op_update_displays (FusaManager *manager);
static inline void dm_op_new_console     (FusaManager *manager,
					  Op          *op);
static inline void dm_op_new_xnest       (FusaManager *manager,
					  Op          *op);
static gpointer    dm_op_threadfunc      (gpointer     data);

/* Utility Functions */
static void         listify_hash_values_hfunc (gpointer      key,
					       gpointer      value,
					       gpointer      data);
static gboolean     clear_hash_table_hrfunc   (gpointer      key,
					       gpointer      value,
					       gpointer      data);
static FusaDisplay *real_get_display          (FusaManager  *manager,
					       const gchar  *name);


/* ****************** *
 *  Global Variables  *
 * ****************** */

static guint        signals[LAST_SIGNAL] = { 0 };
static gpointer     default_manager = NULL;
static GThread     *dm_op_thread = NULL;
static GAsyncQueue *op_q = NULL;


/* ******************* *
 *  GType Declaration  *
 * ******************* */

G_DEFINE_TYPE (FusaManager, fusa_manager, G_TYPE_OBJECT);


/* ********************** *
 *  GType Initialization  *
 * ********************** */

static void
fusa_manager_class_init (FusaManagerClass *class)
{
  GObjectClass *gobject_class;
  GError *err;

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

  /* Classwide Variables */
  op_q = g_async_queue_new ();
  err = NULL;
  dm_op_thread = g_thread_create (dm_op_threadfunc, NULL, FALSE, &err);
  if (!dm_op_thread)
    {
      g_error ("Could not create a thread to contact the display manager: %s",
	       err->message);
      exit (-1);
    }
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
  reload_gdm_config (manager, NULL, NULL);
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
      g_critical ("Could not create URI for shells file `%s': %s",
		  GDMCONFIGFILE, error->message);
      g_error_free (error);
    }
  else
    {
      result = gnome_vfs_monitor_add (&(manager->shells_monitor), uri,
				      GNOME_VFS_MONITOR_FILE,
				      shells_monitor_cb, manager);
      g_free (uri);

      if (result != GNOME_VFS_OK)
	g_critical ("Could not install monitor for shells file `%s': %s",
		    GDMCONFIGFILE, gnome_vfs_result_to_string (result));
    }

  /* /etc/passwd */
  manager->users =
    g_hash_table_new_full (g_str_hash, g_str_equal,
			   g_free, (GDestroyNotify) g_object_run_dispose);
  manager->users_by_uid = g_hash_table_new (g_direct_hash, g_direct_equal);
  reload_passwd (manager);
  error = NULL;
  uri = g_filename_to_uri ("/etc/passwd", NULL, &error);
  if (!uri)
    {
      g_critical ("Could not create URI for password file `%s': %s",
		  GDMCONFIGFILE, error->message);
      g_error_free (error);
    }
  else
    {
      result = gnome_vfs_monitor_add (&(manager->passwd_monitor), uri,
				      GNOME_VFS_MONITOR_FILE,
				      passwd_monitor_cb, manager);
      g_free (uri);

      if (result != GNOME_VFS_OK)
	g_critical ("Could not install monitor for password file `%s': %s",
		    GDMCONFIGFILE, gnome_vfs_result_to_string (result));
    }


  /* Displays */
  manager->displays =
    g_hash_table_new_full (g_str_hash, g_str_equal,
			   g_free, (GDestroyNotify) g_object_run_dispose);
  manager->displays_by_console = g_hash_table_new (g_direct_hash,
						   g_direct_equal);
  manager->results_q = g_async_queue_new ();
  push_update_displays_func (manager);
  manager->update_displays_id =
    g_timeout_add_full (G_PRIORITY_LOW, 5000, push_update_displays_func,
			manager, NULL);
}


/* ******************* *
 *  GObject Functions  *
 * ******************* */

static void
fusa_manager_finalize (GObject *object)
{
  FusaManager *manager;
  OpResult *result;

  manager = FUSA_MANAGER (object);

  if (manager->update_displays_id)
    g_source_remove (manager->update_displays_id);

  if (manager->results_pull_id)
    g_source_remove (manager->results_pull_id);

  result = g_async_queue_try_pop (manager->results_q);
  while (result)
    {
      op_result_free (result);
      result = g_async_queue_try_pop (manager->results_q);
    }
  g_async_queue_unref (manager->results_q);

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
  gboolean users_dirty, icons_dirty;

  if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED)
    return;

  users_dirty = FALSE;
  icons_dirty = FALSE;
  reload_gdm_config (data, &users_dirty, &icons_dirty);

  if (users_dirty)
    reload_passwd (data);

  if (icons_dirty)
    g_hash_table_foreach (FUSA_MANAGER (data)->users,
			  emit_user_icon_changed_signals, NULL);
}

static void
shells_monitor_cb (GnomeVFSMonitorHandle    *handle,
		   const gchar              *text_uri,
		   const gchar              *info_uri,
		   GnomeVFSMonitorEventType  event_type,
		   gpointer                  data)
{
  if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED)
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
  if (event_type != GNOME_VFS_MONITOR_EVENT_CHANGED)
    return;

  reload_passwd (user_data);
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

static void
reload_gdm_config (FusaManager *manager,
		   gboolean    *users_dirty,
		   gboolean    *icons_dirty)
{
  GKeyFile *key_file;
  GError *error;

  error = NULL;
  key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file, GDMCONFIGFILE, G_KEY_FILE_NONE,
				  &error))
    {
      g_critical ("Could not parse GDM configuration file `%s': %s",
		  GDMCONFIGFILE, error->message);
      g_error_free (error);
    }
  else
    {
      gchar *tmp;
      gint tmp_max_width, tmp_max_height, tmp_max_icon_size;
      gboolean tmp_allow_root;
      gchar **excludev;
      guint i;
      static const gchar *exclude_default[] = DEFAULT_EXCLUDE;

      tmp = g_key_file_get_value (key_file, "greeter", "MinimalUID", NULL);
      if (tmp)
	{
	  uid_t tmp_uid;

	  tmp_uid = strtoul (tmp, NULL, 10);
	  g_free (tmp);

	  if (tmp_uid == ULONG_MAX)
	    tmp_uid = DEFAULT_MINIMAL_UID;

	  if (tmp_uid != manager->minimal_uid)
	    {
	      manager->minimal_uid = tmp_uid;
	      if (users_dirty)
		*users_dirty = TRUE;
	    }
	}
      else if (manager->minimal_uid != DEFAULT_MINIMAL_UID)
	{
	  manager->minimal_uid = DEFAULT_MINIMAL_UID;
	  if (users_dirty)
	    *users_dirty = TRUE;
	}

      excludev = g_key_file_get_string_list (key_file, "greeter", "Exclude",
					     NULL, NULL);
      if (!excludev)
	excludev = g_strdupv ((gchar **) exclude_default);

      if (!strv_equals_string_table (excludev, manager->exclusions))
	{
	  g_hash_table_foreach_remove (manager->exclusions,
				       clear_hash_table_hrfunc, NULL);

	  /* Always include the defaults */
	  for (i = 0; exclude_default[i] != NULL; i++)
	    g_hash_table_insert (manager->exclusions,
				 g_strdup (exclude_default[i]),
				 GUINT_TO_POINTER (TRUE));

	  for (i = 0; excludev[i] != NULL; i++)
	    g_hash_table_insert (manager->exclusions,
				 excludev[i], GUINT_TO_POINTER (TRUE));

	  if (users_dirty)
	    *users_dirty = TRUE;

	  g_free (excludev);
	}
      else
	g_strfreev (excludev);

      tmp = g_key_file_get_string (key_file, "greeter", "GlobalFaceDir", NULL);
      if (!tmp)
	{
	  if (!manager->global_face_dir ||
	      strcmp (manager->global_face_dir, DEFAULT_GLOBAL_FACE_DIR) != 0)
	    {
	      g_free (manager->global_face_dir);
	      manager->global_face_dir = g_strdup (DEFAULT_GLOBAL_FACE_DIR);
	    }
	}
      else
	{
	  if (!manager->global_face_dir ||
	      strcmp (tmp, manager->global_face_dir) != 0)
	    {
	      g_free (manager->global_face_dir);
	      manager->global_face_dir = g_strdup (tmp);
	    }

	  g_free (tmp);
	}

      tmp = g_key_file_get_value (key_file, "security", "UserMaxFile", NULL);
      if (tmp)
	{
	  gsize tmp_user_max_file;

	  tmp_user_max_file = strtol (tmp, NULL, 10);
	  g_free (tmp);

	  if (tmp_user_max_file == ULONG_MAX)
	    tmp_user_max_file = DEFAULT_USER_MAX_FILE;

	  if (tmp_user_max_file != manager->user_max_file)
	    {
	      manager->user_max_file = tmp_user_max_file;
	      if (icons_dirty)
		*icons_dirty = TRUE;
	    }
	}
      else if (manager->user_max_file != DEFAULT_USER_MAX_FILE)
	{
	  manager->user_max_file = DEFAULT_USER_MAX_FILE;
	  if (icons_dirty)
	    *icons_dirty = TRUE;
	}

      tmp_allow_root = g_key_file_get_boolean (key_file, "security",
					       "AllowRoot", NULL);
      if ((tmp_allow_root && !manager->allow_root) ||
	  (!tmp_allow_root && manager->allow_root))
	{
	  manager->allow_root = (tmp_allow_root != FALSE);
	  if (users_dirty)
	    *users_dirty = TRUE;
	}
      
      tmp_max_width = g_key_file_get_integer (key_file, "gui", "MaxIconWidth",
					      NULL);
      if (tmp_max_width == -1)
	tmp_max_width = DEFAULT_MAX_ICON_SIZE;

      tmp_max_height = g_key_file_get_integer (key_file, "gui", "MaxIconHeight",
					       NULL);
      if (tmp_max_height == -1)
	tmp_max_height = DEFAULT_MAX_ICON_SIZE;

      tmp_max_icon_size = MAX (tmp_max_width, tmp_max_height);
      if (tmp_max_icon_size != manager->max_icon_size)
	{
	  manager->max_icon_size = tmp_max_icon_size;
	  if (icons_dirty)
	    *icons_dirty = TRUE;
	}
    }

  g_key_file_free (key_file);
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

static inline void
process_display_updates (FusaManager *manager,
			 OpResult    *result)
{
  CleanDisplaysData clean_data;
  FusaDisplay *display;
  FusaUser *user;
  guint i;

  clean_data.manager = manager;
  clean_data.displays = NULL;

  for (i = 0; i < result->updates.n_items; i++)
    {
      if (result->updates.items[i].username != NULL)
	{
	  user = fusa_manager_get_user (manager,
					result->updates.items[i].username);
	}
      else
	user = NULL;

      display = real_get_display (manager, result->updates.items[i].name);

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
	  nested = (result->updates.items[i].location[0] == ':');

	  /* Xnest: We don't need to loop to get around Xnest-in-Xnest insanity
	   * becase all existing FusaDisplays will have a console set from their
	   * insert -- plus that's just crackish anyways. */
	  if (nested)
	    {
	      FusaDisplay *display;
	      gchar *ptr;

	      /* Strip the ".0" */
	      ptr = strchr (result->updates.items[i].location, '.');
	      if (ptr)
		*ptr = '\0';

	      display = real_get_display (manager,
					  result->updates.items[i].location);

	      if (display)
		console = fusa_display_get_console (display);
	    }
	  /* Console */
	  else if (sscanf (result->updates.items[i].location, "%d",
			   &console) != 1)
	    {
	      g_critical ("Invalid display location returned from GDM: `%s'.",
			  result->updates.items[i].location);
	    }

	  display = _fusa_display_new (manager,
				       result->updates.items[i].name, user,
				       console, nested);

	  g_hash_table_insert (manager->displays,
			       g_strdup (result->updates.items[i].name),
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

static void
display_closure_free (DisplayClosure *closure)
{
  if (closure->data && closure->notify)
    (*closure->notify) (closure->data);

  g_free (closure);
}

static void
op_result_free (OpResult *result)
{
  if (!result)
    return;

  switch (result->type)
    {
    case OP_RESULT_DISPLAY_UPDATES:
      {
	guint i;

	for (i = 0; i < result->updates.n_items; i++)
	  {
	    g_free (result->updates.items[i].name);
	    g_free (result->updates.items[i].username);
	    g_free (result->updates.items[i].location);
	  }

	g_free (result->updates.items);
      }
      break;
    case OP_RESULT_NEW_DISPLAY:
      display_closure_free (result->new.closure);
      g_free (result->new.name);
      break;
    case OP_RESULT_DISPLAY_ACTIVATED:
      g_object_unref (result->activate.display);
      display_closure_free (result->activate.closure);
      break;

    case OP_RESULT_ERROR:
      g_error_free (result->error.error);
      display_closure_free (result->error.closure);
      break;
    }

  g_free (result);
}

static gboolean
results_pull_func (gpointer data)
{
  FusaManager *manager;
  OpResult *result;

  GDK_THREADS_ENTER ();

  manager = data;
  g_async_queue_lock (manager->results_q);

  result = g_async_queue_try_pop_unlocked (manager->results_q);
  g_assert (result);

  while (result)
    {
      switch (result->type)
	{
	case OP_RESULT_DISPLAY_UPDATES:
	  process_display_updates (manager, result);
	  break;

	case OP_RESULT_DISPLAY_ACTIVATED:
	  if (result->activate.closure->func)
	    (*result->activate.closure->func) (manager,
					       result->activate.display, NULL,
					       result->activate.closure->data);
	  break;

	case OP_RESULT_NEW_DISPLAY:
	  if (result->activate.closure->func)
	    {
	      FusaDisplay *display;

	      display = real_get_display (manager, result->new.name);
	      (*result->activate.closure->func) (manager, display, NULL,
						 result->new.closure->data);
	    }
	  break;

	case OP_RESULT_ERROR:
	  if (result->error.closure->func)
	    (*result->error.closure->func) (manager, NULL, NULL,
					    result->error.closure->data);
	  break;
	}

      op_result_free (result);

      result = g_async_queue_try_pop_unlocked (manager->results_q);
    }

  manager->results_pull_id = 0;

  g_async_queue_unlock (manager->results_q);

  GDK_THREADS_LEAVE ();

  return FALSE;
}

static void
push_dm_op (FusaManager                *manager,
	    OpType                      type,
	    GdkScreen                  *screen,
	    FusaDisplay                *display,
	    FusaManagerDisplayCallback  func,
	    gpointer                    data,
	    GDestroyNotify              notify)
{
  Op *op;

  op = g_new (Op, 1);

  op->type = type;
  op->manager = g_object_ref (manager);
  op->screen = (screen ? g_object_ref (screen) : NULL);
  if (display)
    {
      op->display = g_object_ref (display);
      op->console = fusa_display_get_console (display);
    }
  else
    {
      op->display = NULL;
      op->console = -1;
    }
  op->closure = g_new (DisplayClosure, 1);
  op->closure->func = func;
  op->closure->data = data;
  op->closure->notify = notify;

  g_async_queue_push (op_q, op);
}

static gboolean
push_update_displays_func (gpointer data)
{
  push_dm_op (data, OP_UPDATE_DISPLAYS, NULL, NULL, NULL, NULL, NULL);

  return TRUE;
}


/* ******************************** *
 *  Displays - IO Thread Functions  *
 * ******************************** */

static void
push_new_result (FusaManager *manager,
		 Op          *op,
		 const gchar *name)
{
  OpResult *op_result;
    
  op_result = g_new0 (OpResult, 1);
  op_result->new.type = OP_RESULT_NEW_DISPLAY;
  op_result->new.closure = op->closure;
  op->closure = NULL;
  op_result->new.name = g_strdup (name);

  GDK_THREADS_ENTER ();
  g_async_queue_lock (manager->results_q);
  g_async_queue_push_unlocked (manager->results_q, op_result);
  if (!manager->results_pull_id)
    manager->results_pull_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
						results_pull_func,
						manager, NULL);

  g_async_queue_unlock (manager->results_q);
  GDK_THREADS_LEAVE ();
}

static void
push_error_result (FusaManager *manager,
		   Op          *op,
		   const gchar *result)
{
  OpResult *op_result;
  gint code;
  const gchar *message;
    
  op_result = g_new0 (OpResult, 1);
  op_result->type = OP_RESULT_ERROR;

  if (result == NULL || sscanf (result, "ERROR %d", &code) != 1)
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

  op_result->error.error = g_error_new (FUSA_MANAGER_DM_ERROR, code,
					"%s\n%s", message, result);
  op_result->error.closure = op->closure;
  op->closure = NULL;

  GDK_THREADS_ENTER ();
  g_async_queue_lock (manager->results_q);
  g_async_queue_push_unlocked (manager->results_q, op_result);
  if (!manager->results_pull_id)
    manager->results_pull_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
						results_pull_func,
						manager, NULL);

  g_async_queue_unlock (manager->results_q);
  GDK_THREADS_LEAVE ();
}

static gboolean
dm_op_update_displays (FusaManager *manager)
{
  gchar *result;
  gboolean retval;

  result = gdmcomm_call_gdm (GDM_SUP_CONSOLE_SERVERS, NULL,
			     MINIMAL_GDM_VERSION, 5);
  retval = (result && strncmp (result, "OK ", 3) == 0);

  /* Success */
  if (retval)
    {
      gchar **displays;

      displays = g_strsplit (result + 3, ";", -1);
      if (displays)
	{
	  guint i;
		
	  i = g_strv_length (displays);
	  if (i)
	    {
	      OpResult *op_result;

	      op_result = g_new0 (OpResult, 1);

	      op_result->type = OP_RESULT_DISPLAY_UPDATES;
	      op_result->updates.items = g_new0 (DisplayUpdate, i);
	      op_result->updates.n_items = i;

	      while (i > 0)
		{
		  gchar **args;

		  i--;

		  args = g_strsplit (displays[i], ",", 3);

		  /* Display name (e.g. :0) */
		  op_result->updates.items[i].name = args[0];

		  /* Username */
		  if (args[1] != NULL && args[1][0] != '\0')
		    op_result->updates.items[i].username = args[1];
		  else
		    {
		      op_result->updates.items[i].username = NULL;
		      g_free (args[1]);
		    }

		  /* VT or display name Location (e.g "7" or ":0.0") */
		  op_result->updates.items[i].location = args[2];

		  g_free (args);
		}

	      GDK_THREADS_ENTER ();
	      g_async_queue_lock (manager->results_q);
	      g_async_queue_push_unlocked (manager->results_q, op_result);
	      if (!manager->results_pull_id)
		manager->results_pull_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
							    results_pull_func,
							    manager, NULL);
	      g_async_queue_unlock (manager->results_q);
	      GDK_THREADS_LEAVE ();
	    }

	  g_strfreev (displays);
	}
    }

  g_free (result);

  return retval;
}

static inline void
dm_op_activate_display (FusaManager *manager,
		        Op          *op)
{
  gchar *auth_cookie, *command, *result;

  GDK_THREADS_ENTER ();
  auth_cookie = g_strdup (gdmcomm_get_auth_cookie (op->screen));
  GDK_THREADS_LEAVE ();

  command = g_strdup_printf (GDM_SUP_SET_VT " %d", op->console);
  result = gdmcomm_call_gdm (command, auth_cookie, MINIMAL_GDM_VERSION, 5);
  g_free (command);
  g_free (auth_cookie);

  /* Success, refresh the displays cache */
  if (result && strncmp (result, "OK", 2) == 0)
    {
      OpResult *op_result;
    
      op_result = g_new0 (OpResult, 1);
      op_result->activate.type = OP_RESULT_DISPLAY_ACTIVATED;
      op_result->activate.display = g_object_ref (op->display);
      op_result->activate.closure = op->closure;
      op->closure = NULL;

      GDK_THREADS_ENTER ();
      g_async_queue_lock (manager->results_q);
      g_async_queue_push_unlocked (manager->results_q, op_result);
      if (!manager->results_pull_id)
	manager->results_pull_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
						    results_pull_func,
						    manager, NULL);

      g_async_queue_unlock (manager->results_q);
      GDK_THREADS_LEAVE ();

      dm_op_update_displays (manager);
    }
  /* Error */
  else
    push_error_result (manager, op, result);
  
  g_free (result);
}

static inline void
dm_op_new_console (FusaManager *manager,
		   Op          *op)
{
  gchar *auth_cookie, *result;

  GDK_THREADS_ENTER ();
  auth_cookie = g_strdup (gdmcomm_get_auth_cookie (op->screen));
  GDK_THREADS_LEAVE ();

  result = gdmcomm_call_gdm (GDM_SUP_FLEXI_XSERVER, auth_cookie,
			     MINIMAL_GDM_VERSION, 5);
  g_free (auth_cookie);

  /* Success, refresh the displays cache */
  if (result && strncmp (result, "OK", 2) == 0)
    {
      dm_op_update_displays (manager);
      push_new_result (manager, op, result + 3);
    }
  /* Error */
  else
    push_error_result (manager, op, result);

  g_free (result);
}

static inline void
dm_op_new_xnest (FusaManager *manager,
		 Op          *op)
{
  gchar *auth_cookie, *mit_cookie, *command, *result;

  GDK_THREADS_ENTER ();
  auth_cookie = g_strdup (gdmcomm_get_auth_cookie (op->screen));
  mit_cookie = gdmcomm_get_a_cookie (op->screen, FALSE);
  command = g_strdup_printf (GDM_SUP_FLEXI_XNEST " %s %d %s %s",
			     gdk_display_get_name (gdk_screen_get_display (op->screen)),
			     (int) getuid (),
			     mit_cookie,
			     XauFileName ());
  GDK_THREADS_LEAVE ();
  g_free (mit_cookie);

  result = gdmcomm_call_gdm (command, auth_cookie, MINIMAL_GDM_VERSION, 5);
  g_free (command);
  g_free (auth_cookie);

  if (result && strncmp (result, "OK ", 3) == 0)
    {
      dm_op_update_displays (manager);
      push_new_result (manager, op, result + 3);
    }
  /* Error */
  else
    push_error_result (manager, op, result);

  g_free (result);
}

static gpointer
dm_op_threadfunc (gpointer data)
{
  Op *op;

  op = g_async_queue_pop (op_q);
  while (op)
    {
      switch (op->type)
	{
	case OP_UPDATE_DISPLAYS:
	  dm_op_update_displays (op->manager);
	  break;

	case OP_ACTIVATE_DISPLAY:
	  dm_op_activate_display (op->manager, op);
	  break;

	case OP_NEW_CONSOLE:
	  dm_op_new_console (op->manager, op);
	  break;

	case OP_NEW_XNEST:
	  dm_op_new_xnest (op->manager, op);
	  break;
	}

      GDK_THREADS_ENTER ();
      if (op->screen)
	g_object_unref (op->screen);

      if (op->display)
	g_object_unref (op->display);

      if (op->manager)
	g_object_unref (op->manager);
      GDK_THREADS_LEAVE ();

      g_free (op);

      op = g_async_queue_pop (op_q);
    }
  
  return NULL;
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

GdkPixbuf *
_fusa_manager_render_icon (FusaManager  *manager,
			   FusaUser     *user,
			   GtkWidget    *widget,
			   gint          icon_size)
{
  GdkPixbuf *retval;
  const gchar *homedir, *username;
  uid_t uid;
  gchar *path;

  g_return_val_if_fail (FUSA_IS_MANAGER (manager), NULL);
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);
  g_return_val_if_fail (widget == NULL || GTK_IS_WIDGET (widget), NULL);
  g_return_val_if_fail (icon_size > 12, NULL);

  homedir = fusa_user_get_home_directory (user);
  uid = fusa_user_get_uid (user);
  username = NULL;

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

  /* Try ${GlobalFaceDir}/${username} */
  if (!retval)
    {
      username = fusa_user_get_user_name (user);
      path = g_build_filename (manager->global_face_dir, username, NULL);
      if (check_user_file (path, uid, manager->user_max_file,
			   manager->relax_group, manager->relax_other))
	retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size,
						   NULL);
      else
	retval = NULL;

      g_free (path);
    }

  /* Finally, ${GlobalFaceDir}/${username}.png */
  if (!retval)
    {
      path = g_build_filename (manager->global_face_dir, username, ".png", NULL);
      if (check_user_file (path, uid, manager->user_max_file,
			   manager->relax_group, manager->relax_other))
	retval = gdk_pixbuf_new_from_file_at_size (path, icon_size, icon_size,
						   NULL);
      else
	retval = NULL;

      g_free (path);
    }

  /* Nothing yet, use stock icon */
  if (!retval)
    {
      GtkIconTheme *theme;

      if (widget && gtk_widget_has_screen (widget))
	theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
      else
	theme = gtk_icon_theme_get_default ();

      retval = gtk_icon_theme_load_icon (theme, DEFAULT_USER_ICON, icon_size,
					 0, NULL);

      if (!retval)
	retval = gtk_icon_theme_load_icon (theme, GTK_STOCK_MISSING_IMAGE,
					   icon_size, 0, NULL);
    }

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
    push_dm_op (manager, OP_ACTIVATE_DISPLAY, screen, display, func, data, notify);
  /* Xnest on this console, find & activate it. */
  else 
    {
#ifndef DISABLE_XNEST
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
	      
	      if (strcmp ("Xnest", tp.value) == 0 &&
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
#endif /* DISABLE_XNEST */
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

  push_dm_op (manager, OP_NEW_CONSOLE, screen, NULL, func, data, notify);
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
  g_return_if_fail (FUSA_IS_MANAGER (manager));

  push_dm_op (manager, OP_NEW_XNEST, screen, NULL, func, data, notify);
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
