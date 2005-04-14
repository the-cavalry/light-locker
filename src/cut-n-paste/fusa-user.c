/* 
 * Fast User Switch Applet: fusa-user.c
 * 
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
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

#include <string.h>

#include <gtk/gtkicontheme.h>

#include "fusa-manager-private.h"
#include "fusa-user-private.h"


/* **************** *
 *  Private Macros  *
 * **************** */

#define FUSA_USER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), FUSA_TYPE_USER, FusaUserClass))
#define FUSA_IS_USER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), FUSA_TYPE_USER))
#define FUSA_USER_GET_CLASS(object) \
  (G_TYPE_INSTANCE_GET_CLASS ((object), FUSA_TYPE_USER, FusaUserClass))


/* ********************** *
 *  Private Enumerations  *
 * ********************** */

enum
{
  PROP_0,
  PROP_MANAGER,
  PROP_DISPLAY_NAME,
  PROP_USER_NAME,
  PROP_UID,
  PROP_HOME_DIR,
  PROP_SHELL,
  PROP_DISPLAYS
};

enum
{
  ICON_CHANGED,
  DISPLAYS_CHANGED,
  LAST_SIGNAL
};


/* ******************** *
 *  Private Structures  *
 * ******************** */

struct _FusaUser
{
  GObject parent;

  FusaManager *manager;

  uid_t uid;
  gchar *username;
  gchar *display_name;
  gchar *home_dir;
  gchar *shell;

  GSList *displays;
};

typedef struct _FusaUserClass
{
  GObjectClass parent_class;

  void (*icon_changed)     (FusaUser *user);
  void (*displays_changed) (FusaUser *user);
}
FusaUserClass;


/* ********************* *
 *  Function Prototypes  *
 * ********************* */

/* GObject Functions */
static void fusa_user_set_property (GObject      *object,
				    guint         param_id,
				    const GValue *value,
				    GParamSpec   *pspec);
static void fusa_user_get_property (GObject      *object,
				    guint         param_id,
				    GValue       *value,
				    GParamSpec   *pspec);
static void fusa_user_finalize     (GObject      *object);

/* Utility Functions */
static void display_weak_notify (gpointer  data,
				 GObject  *old_display);


/* ****************** *
 *  Global Variables  *
 * ****************** */

static guint signals[LAST_SIGNAL] = { 0 };


/* ******************* *
 *  GType Declaration  *
 * ******************* */

G_DEFINE_TYPE (FusaUser, fusa_user, G_TYPE_OBJECT);


/* ********************** *
 *  GType Initialization  *
 * ********************** */

static void
fusa_user_class_init (FusaUserClass *class)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = fusa_user_set_property;
  gobject_class->get_property = fusa_user_get_property;
  gobject_class->finalize = fusa_user_finalize;

  g_object_class_install_property (gobject_class,
				   PROP_MANAGER,
				   g_param_spec_object ("manager",
							_("Manager"),
							_("The user manager object this user is controlled by."),
							FUSA_TYPE_MANAGER,
							(G_PARAM_READWRITE |
							 G_PARAM_CONSTRUCT_ONLY)));

  g_object_class_install_property (gobject_class,
				   PROP_DISPLAY_NAME,
				   g_param_spec_string ("display-name",
							"Display Name",
							"The user-friendly name to display for this user.",
							NULL,
							G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
				   PROP_UID,
				   g_param_spec_ulong ("uid",
						       "User ID",
						       "The UID for this user.",
						       0, G_MAXULONG, 0,
						       G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
				   PROP_USER_NAME,
				   g_param_spec_string ("user-name",
							"User Name",
							"The login name for this user.",
							NULL,
							G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
				   PROP_HOME_DIR,
				   g_param_spec_string ("home-directory",
							"Home Directory",
							"The home directory for this user.",
							NULL,
							G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
				   PROP_SHELL,
				   g_param_spec_string ("shell",
							"Shell",
							"The shell for this user.",
							NULL,
							G_PARAM_READABLE));

  signals[ICON_CHANGED] =
    g_signal_new ("icon-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (FusaUserClass, icon_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  signals[DISPLAYS_CHANGED] =
    g_signal_new ("displays-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (FusaUserClass, displays_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
fusa_user_init (FusaUser *user)
{
  user->manager = NULL;
  user->username = NULL;
  user->display_name = NULL;
  user->displays = NULL;
}


/* ******************* *
 *  GObject Functions  *
 * ******************* */

static void
fusa_user_set_property (GObject      *object,
			guint         param_id,
			const GValue *value,
			GParamSpec   *pspec)
{
  FusaUser *user;

  user = FUSA_USER (object);

  switch (param_id)
    {
    case PROP_MANAGER:
      user->manager = g_value_get_object (value);
      g_assert (user->manager);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
fusa_user_get_property (GObject    *object,
			guint       param_id,
			GValue     *value,
			GParamSpec *pspec)
{
  FusaUser *user;

  user = FUSA_USER (object);

  switch (param_id)
    {
    case PROP_MANAGER:
      g_value_set_object (value, user->manager);
      break;
    case PROP_USER_NAME:
      g_value_set_string (value, user->username);
      break;
    case PROP_DISPLAY_NAME:
      g_value_set_string (value, user->display_name);
      break;
    case PROP_HOME_DIR:
      g_value_set_string (value, user->home_dir);
      break;
    case PROP_UID:
      g_value_set_ulong (value, user->uid);
      break;
    case PROP_SHELL:
      g_value_set_string (value, user->shell);
      break;
    case PROP_DISPLAYS:
      if (user->displays)
	{
	  GValueArray *ar;
	  GSList *list;
	  GValue tmp = { 0 };

	  ar = g_value_array_new (g_slist_length (user->displays));
	  g_value_init (&tmp, FUSA_TYPE_DISPLAY);
	  for (list = user->displays; list; list = list->next)
	    {
	      g_value_set_object (&tmp, list->data);
	      g_value_array_append (ar, &tmp);
	      g_value_reset (&tmp);
	    }

	  g_value_take_boxed (value, ar);
	}
      else
	g_value_set_boxed (value, NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
fusa_user_finalize (GObject *object)
{
  FusaUser *user;

  user = FUSA_USER (object);

  g_free (user->username);
  g_free (user->display_name);

  while (user->displays)
    {
      g_object_weak_unref (G_OBJECT (user->displays->data), display_weak_notify,
			   user);
      user->displays = g_slist_delete_link (user->displays, user->displays);
    }


  if (G_OBJECT_CLASS (fusa_user_parent_class)->finalize)
    (*G_OBJECT_CLASS (fusa_user_parent_class)->finalize) (object);
}

/* ******************* *
 *  Utility Functions  *
 * ******************* */

static void
display_weak_notify (gpointer  user_data,
		     GObject  *old_display)
{
  FusaUser *user;

  user = FUSA_USER (user_data);

  user->displays = g_slist_delete_link (user->displays,
					g_slist_find (user->displays,
						      old_display));
  g_signal_emit (user, signals[DISPLAYS_CHANGED], 0);
}


/* ************************************************************************** *
 *  Semi-Private API                                                          *
 * ************************************************************************** */

/**
 * _fusa_user_update:
 * @user: the user object to update.
 * @pwent: the user data to use.
 * 
 * Updates the properties of @user using the data in @pwent.
 * 
 * Since: 1.0
 **/
void
_fusa_user_update (FusaUser            *user,
		   const struct passwd *pwent)
{
  gchar *display_name;

  g_return_if_fail (FUSA_IS_USER (user));
  g_return_if_fail (pwent != NULL);

  g_object_freeze_notify (G_OBJECT (user));

  /* Display Name */
  if (pwent->pw_gecos && pwent->pw_gecos[0] != '\0')
    {
      gchar *first_comma;

      first_comma = strchr (pwent->pw_gecos, ',');
      if (first_comma)
	display_name = g_strndup (pwent->pw_gecos,
				  (first_comma - pwent->pw_gecos));
      else
	display_name = g_strdup (pwent->pw_gecos);

      if (display_name[0] == '\0')
	{
	  g_free (display_name);
	  display_name = NULL;
	}
    }
  else
    display_name = NULL;

  if ((display_name && !user->display_name) ||
      (!display_name && user->display_name) ||
      (display_name &&
       user->display_name &&
       strcmp (display_name, user->display_name) != 0))
    {
      g_free (user->display_name);
      user->display_name = display_name;
      g_object_notify (G_OBJECT (user), "display-name");
    }
  else
    g_free (display_name);

  /* UID */
  if (pwent->pw_uid != user->uid)
    {
      user->uid = pwent->pw_uid;
      g_object_notify (G_OBJECT (user), "uid");
    }

  /* Username */
  if ((pwent->pw_name && !user->username) ||
      (!pwent->pw_name && user->username) ||
      (pwent->pw_name &&
       user->username &&
       strcmp (user->username, pwent->pw_name) != 0))
    {
      g_free (user->username);
      user->username = g_strdup (pwent->pw_name);
      g_object_notify (G_OBJECT (user), "user-name");
    }

  /* Home Directory */
  if ((pwent->pw_dir && !user->home_dir) ||
      (!pwent->pw_dir && user->home_dir) ||
      strcmp (user->home_dir, pwent->pw_dir) != 0)
    {
      g_free (user->home_dir);
      user->home_dir = g_strdup (pwent->pw_dir);
      g_object_notify (G_OBJECT (user), "home-directory");
      g_signal_emit (user, signals[ICON_CHANGED], 0);
    }

  /* Shell */
  if ((pwent->pw_shell && !user->shell) ||
      (!pwent->pw_shell && user->shell) ||
      (pwent->pw_shell &&
       user->shell &&
       strcmp (user->shell, pwent->pw_shell) != 0))
    {
      g_free (user->shell);
      user->shell = g_strdup (pwent->pw_shell);
      g_object_notify (G_OBJECT (user), "shell");
    }

  g_object_thaw_notify (G_OBJECT (user));
}

/**
 * _fusa_user_add_display:
 * @user: the user to modify.
 * @display: the virtual terminal to add.
 * 
 * Adds @display to the list of terminals @user is logged in on, and emits the
 * "displays-changed" signal, if necessary.
 * 
 * Since: 1.0
 **/
void
_fusa_user_add_display (FusaUser *user,
		        FusaDisplay   *display)
{
  g_return_if_fail (FUSA_IS_USER (user));
  g_return_if_fail (FUSA_IS_DISPLAY (display));

  if (!g_slist_find (user->displays, display))
    {
      g_object_weak_ref (G_OBJECT (display), display_weak_notify, user);
      user->displays = g_slist_append (user->displays, display);
      g_signal_emit (user, signals[DISPLAYS_CHANGED], 0);
    }
}

/**
 * _fusa_user_remove_display:
 * @user: the user to modify.
 * @display: the virtual terminal to remove.
 * 
 * Removes @display from the list of terminals @user is logged in on, and emits the
 * "displays-changed" signal, if necessary.
 * 
 * Since: 1.0
 **/
void
_fusa_user_remove_display (FusaUser *user,
			   FusaDisplay   *display)
{
  GSList *li;

  g_return_if_fail (FUSA_IS_USER (user));
  g_return_if_fail (FUSA_IS_DISPLAY (display));

  li = g_slist_find (user->displays, display);
  if (li)
    {
      g_object_weak_unref (G_OBJECT (display), display_weak_notify, user);
      user->displays = g_slist_delete_link (user->displays, li);
      g_signal_emit (user, signals[DISPLAYS_CHANGED], 0);
    }
}

/**
 * _fusa_user_icon_changed:
 * @user: the user to emit the signal for.
 * 
 * Emits the "icon-changed" signal for @user.
 * 
 * Since: 1.0
 **/
void
_fusa_user_icon_changed (FusaUser *user)
{
  g_return_if_fail (FUSA_IS_USER (user));

  g_signal_emit (user, signals[ICON_CHANGED], 0);
}


/* ************************************************************************** *
 *  Public API                                                                *
 * ************************************************************************** */

/**
 * fusa_user_get_uid:
 * @user: the user object to examine.
 * 
 * Retrieves the ID of @user.
 * 
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 * 
 * Since: 1.0
 **/

uid_t
fusa_user_get_uid (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), -1);

  return user->uid;
}

/**
 * fusa_user_get_display_name:
 * @user: the user object to examine.
 * 
 * Retrieves the display name of @user.
 * 
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 * 
 * Since: 1.0
 **/
G_CONST_RETURN gchar *
fusa_user_get_display_name (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);

  return (user->display_name ? user->display_name : user->username);
}

/**
 * fusa_user_get_user_name:
 * @user: the user object to examine.
 * 
 * Retrieves the login name of @user.
 * 
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 * 
 * Since: 1.0
 **/

G_CONST_RETURN gchar *
fusa_user_get_user_name (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);

  return user->username;
}

/**
 * fusa_user_get_home_directory:
 * @user: the user object to examine.
 * 
 * Retrieves the home directory of @user.
 * 
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 * 
 * Since: 1.0
 **/

G_CONST_RETURN gchar *
fusa_user_get_home_directory (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);

  return user->home_dir;
}

/**
 * fusa_user_get_shell:
 * @user: the user object to examine.
 * 
 * Retrieves the login shell of @user.
 * 
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 * 
 * Since: 1.0
 **/

G_CONST_RETURN gchar *
fusa_user_get_shell (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);

  return user->shell;
}

/**
 * fusa_user_get_displays:
 * @user: the user object to examine.
 * 
 * Retrieves a new list of the displays that @user is logged in on. The list
 * itself must be freed with g_slist_free() when no longer needed.
 * 
 * Returns: a list of #FusaDisplay objects which must be freed with
 *  g_slist_free().
 * 
 * Since: 1.0
 **/
GSList *
fusa_user_get_displays (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);

  return g_slist_copy (user->displays);
}

/**
 * fusa_user_get_n_displays:
 * @user: the user object to examine.
 * 
 * Retrieves the number of displays that @user is logged into.
 * 
 * Returns: an unsigned integer.
 * 
 * Since: 1.0
 **/
guint
fusa_user_get_n_displays (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), FALSE);

  return g_slist_length (user->displays);
}

/**
 * fusa_user_render_icon:
 * @user: the user object to examine.
 * @widget: the intended destination widget.
 * @icon_size: the desired icon size.
 * @error: a pointer to a location to store errors while reading the icon.
 * 
 * Loads a copy of the face pixbuf for @user at @icon_size. If an error occurs,
 * %NULL will be returned and @error will be set, if @error is not %NULL.
 * 
 * Returns: a newly allocated #GdkPixbuf object, or %NULL.
 * 
 * Since: 1.0
 **/
GdkPixbuf *
fusa_user_render_icon (FusaUser  *user,
		       GtkWidget *widget,
		       gint       icon_size,
		       gboolean   desktop_overlay)
{
  GdkPixbuf *pixbuf;

  g_return_val_if_fail (FUSA_IS_USER (user), NULL);
  g_return_val_if_fail (widget == NULL || GTK_IS_WIDGET (widget), NULL);
  g_return_val_if_fail (icon_size > 0, NULL);

  pixbuf = _fusa_manager_render_icon (user->manager, user, widget, icon_size);

  if (desktop_overlay)
    {
      GtkIconTheme *theme;
      GdkPixbuf *overlay;
    
      if (gtk_widget_has_screen (widget))
	theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
      else
	theme = gtk_icon_theme_get_default ();

      overlay = gtk_icon_theme_load_icon (theme, "gnome-fs-desktop",
					  icon_size / 2, 0, NULL);

      if (overlay)
	{
	  gint pixbuf_width, pixbuf_height, overlay_width, overlay_height;
	  GdkPixbuf *tmp;

	  pixbuf_width = gdk_pixbuf_get_width (pixbuf);
	  pixbuf_height = gdk_pixbuf_get_height (pixbuf);

	  tmp = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
				pixbuf_width, pixbuf_height);
	  gdk_pixbuf_fill (tmp, 0x00000000);

	  overlay_width = gdk_pixbuf_get_width (overlay);
	  overlay_height = gdk_pixbuf_get_height (overlay);

	  gdk_pixbuf_copy_area (overlay, 0, 0, overlay_width, overlay_height,
				tmp, (pixbuf_width - overlay_width),
				(pixbuf_height - overlay_height));
	  g_object_unref (overlay);

	  gdk_pixbuf_composite (tmp, pixbuf,
				pixbuf_width - overlay_width,
				pixbuf_height - overlay_height,
				overlay_width, overlay_height,
				0.0, 0.0, 1.0, 1.0,
				GDK_INTERP_BILINEAR, 255);
	  g_object_unref (tmp);
	}
    }

  return pixbuf;
}

gint
fusa_user_collate (FusaUser *user1,
		   FusaUser *user2)
{
  const gchar *str1, *str2;

  g_return_val_if_fail (user1 == NULL || FUSA_IS_USER (user1), 0);
  g_return_val_if_fail (user2 == NULL || FUSA_IS_USER (user2), 0);

  if (!user1 && user2)
    return -1;

  if (user1 && !user2)
    return 1;

  if (!user1 && !user2)
    return 0;

  if (user1->display_name)
    str1 = user1->display_name;
  else
    str1 = user1->username;

  if (user2->display_name)
    str2 = user2->display_name;
  else
    str2 = user2->username;

  if (!str1 && str2)
    return -1;

  if (str1 && !str2)
    return 1;

  if (!str1 && !str2)
    return 0;

  return g_utf8_collate (str1, str2);
}
