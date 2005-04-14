/*
 * Fast User Switch Applet: fusa-display.c
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

#include "fusa-user-private.h"
#include "fusa-manager.h"

#include "fusa-display-private.h"

/* **************** *
 *  Private Macros  *
 * **************** */
 
#define FUSA_DISPLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), FUSA_TYPE_DISPLAY, FusaDisplayClass))
#define FUSA_IS_DISPLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), FUSA_TYPE_DISPLAY))
#define FUSA_DISPLAY_GET_CLASS(object) \
  (G_TYPE_INSTANCE_GET_CLASS ((object), FUSA_TYPE_DISPLAY, FusaDisplayClass))


/* ********************** *
 *  Private Enumerations  *
 * ********************** */

enum
{
  PROP_0,
  PROP_MANAGER,
  PROP_NAME,
  PROP_USER,
  PROP_CONSOLE,
  PROP_NESTED
};


/* ******************** *
 *  Private Structures  *
 * ******************** */

struct _FusaDisplay
{
  GObject parent;

  FusaManager *manager;
  gchar *name;
  FusaUser *user;
  gint console;
  guint8 nested : 1;
};

typedef struct _FusaDisplayClass
{
  GObjectClass parent_class;
}
FusaDisplayClass;


/* ********************* *
 *  Function Prototypes  *
 * ********************* */

/* GObject Functions */
static void fusa_display_set_property (GObject      *object,
				       guint         param_id,
				       const GValue *value,
				       GParamSpec   *pspec);
static void fusa_display_get_property (GObject      *object,
				       guint         param_id,
				       GValue       *value,
				       GParamSpec   *pspec);
static void fusa_display_finalize     (GObject      *object);


/* ******************* *
 *  GType Declaration  *
 * ******************* */

G_DEFINE_TYPE (FusaDisplay, fusa_display, G_TYPE_OBJECT);


/* ********************** *
 *  GType Initialization  *
 * ********************** */

static void
fusa_display_class_init (FusaDisplayClass *class)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = fusa_display_set_property;
  gobject_class->get_property = fusa_display_get_property;
  gobject_class->finalize = fusa_display_finalize;

  g_object_class_install_property (gobject_class,
				   PROP_MANAGER,
				   g_param_spec_object ("manager",
							_("Manager"),
							_("The manager which owns this object."),
							FUSA_TYPE_MANAGER,
							(G_PARAM_READWRITE |
							 G_PARAM_CONSTRUCT_ONLY)));
  g_object_class_install_property (gobject_class,
				   PROP_NAME,
				   g_param_spec_string ("name",
							_("Name"),
							_("The name of the X11 display this object refers to."),
							NULL,
							G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
				   PROP_USER,
				   g_param_spec_object ("user",
							_("User"),
							_("The user currently logged in on this virtual terminal."),
							FUSA_TYPE_USER,
							G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
				   PROP_CONSOLE,
				   g_param_spec_int ("console",
						     _("Console"),
						     _("The number of the virtual console this display can be found on, or %-1."),
						     -1, G_MAXINT, -1,
						     G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
				   PROP_NESTED,
				   g_param_spec_boolean ("nested",
							 _("Nested"),
							 _("Whether or not this display is a windowed (Xnest) display."),
							 FALSE,
							 G_PARAM_READABLE));
}

static void
fusa_display_init (FusaDisplay *display)
{
  display->console = -1;
}


/* ******************* *
 *  GObject Functions  *
 * ******************* */

static void
fusa_display_set_property (GObject      *object,
			   guint         param_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
  FusaDisplay *display;

  display = FUSA_DISPLAY (object);

  switch (param_id)
    {
    case PROP_MANAGER:
      display->manager = g_value_get_object (value);
      g_object_add_weak_pointer (G_OBJECT (display->manager),
				 (gpointer *) &display->manager);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
fusa_display_get_property (GObject    *object,
			   guint       param_id,
			   GValue     *value,
			   GParamSpec *pspec)
{
  FusaDisplay *display;

  display = FUSA_DISPLAY (object);

  switch (param_id)
    {
    case PROP_NAME:
      g_value_set_string (value, display->name);
      break;
    case PROP_USER:
      g_value_set_object (value, display->user);
      break;
    case PROP_CONSOLE:
      g_value_set_int (value, display->console);
      break;
    case PROP_NESTED:
      g_value_set_boolean (value, display->nested);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
fusa_display_finalize (GObject *object)
{
  FusaDisplay *display;

  display = FUSA_DISPLAY (object);

  _fusa_display_set_user (display, NULL);
  g_free (display->name);
  g_object_remove_weak_pointer (G_OBJECT (display->manager),
				(gpointer *) &display->manager);

  if (G_OBJECT_CLASS (fusa_display_parent_class)->finalize)
    (*G_OBJECT_CLASS (fusa_display_parent_class)->finalize) (object);
}


/* ******************* *
 *  Utility Functions  *
 * ******************* */

static void
user_weak_notify (gpointer  data,
		  GObject  *user_ptr)
{
  FusaDisplay *display;

  display = FUSA_DISPLAY (data);

  display->user = NULL;
  g_object_notify (G_OBJECT (data), "user");
}


/* ************************************************************************** *
 *  Semi-Private API                                                          *
 * ************************************************************************** */

FusaDisplay *
_fusa_display_new (FusaManager *manager,
		   const gchar *name,
		   FusaUser    *user,
		   gint         console,
		   gboolean     nested)
{
  FusaDisplay *display;

  display = g_object_new (FUSA_TYPE_DISPLAY, "manager", manager, NULL);
  display->name = g_strdup (name);
  _fusa_display_set_user (display, user);
  display->console = console;
  display->nested = !!nested;

  return display;
}

/**
 * _fusa_display_set_user:
 * @display: the virtual terminal object to modify.
 * @user: the user logged in on the terminal, or %NULL
 * 
 * Updates @display to know who (if anyone) is logged in on the session it
 * represents.
 * 
 * Since: 1.0
 **/
void
_fusa_display_set_user (FusaDisplay *display,
			FusaUser    *user)
{
  g_return_if_fail (FUSA_IS_DISPLAY (display));
  g_return_if_fail (user == NULL || FUSA_IS_USER (user));

  if (display->user == user)
    return;

  if (display->user)
    {
      g_object_weak_unref (G_OBJECT (display->user), user_weak_notify, display);
      _fusa_user_remove_display (display->user, display);
    }

  display->user = user;

  if (user)
    {
      g_object_weak_ref (G_OBJECT (display->user), user_weak_notify, display);
      _fusa_user_add_display (user, display);
    }

  g_object_notify (G_OBJECT (display), "user");
}


/* ************************************************************************** *
 *  Public API                                                                *
 * ************************************************************************** */

/**
 * fusa_display_get_name:
 * @display: the display object to examine.
 * 
 * Retrieves the name for the X11 session on @display, e.g. ":0".
 * 
 * Returns: a constant character array which should not be modified or freed.
 * 
 * Since: 1.0
 **/
G_CONST_RETURN gchar *
fusa_display_get_name (FusaDisplay *display)
{
  g_return_val_if_fail (FUSA_IS_DISPLAY (display), NULL);

  return display->name;
}

/**
 * fusa_display_get_user:
 * @display: the virtual terminal object to examine.
 * 
 * Retrieves the user currently logged in to the virtual terminal @display.
 * 
 * Returns: a pointer to a #FusaUser object which should not be unreferenced.
 * 
 * Since: 1.0
 **/
FusaUser *
fusa_display_get_user (FusaDisplay *display)
{
  g_return_val_if_fail (FUSA_IS_DISPLAY (display), NULL);

  return display->user;
}

/**
 * fusa_display_get_console:
 * @display: the display to examine.
 * 
 * Retrieves the numeric ID of the virtual console used by @display. If
 * @display represents a nested session, this will be the console that @display
 * exists on.
 * 
 * Returns: an integer.
 * 
 * Since: 1.0
 **/
gint
fusa_display_get_console (FusaDisplay *display)
{
  g_return_val_if_fail (FUSA_IS_DISPLAY (display), -1);

  return display->console;
}

/**
 * fusa_display_get_nested:
 * @display: the display to examine.
 * 
 * Retrieves whether or not @display is a nested (Xnest) session.
 * 
 * Returns: a boolean value.
 * 
 * Since: 1.0
 **/
gboolean
fusa_display_get_nested (FusaDisplay *display)
{
  g_return_val_if_fail (FUSA_IS_DISPLAY (display), FALSE);

  return display->nested;
}
