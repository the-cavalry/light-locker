/* 
 * Fast User Switch Applet: fusa-user-menu-item.c
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

#include <unistd.h>
#include <sys/types.h>

#include <string.h>

#include <gtk/gtkbutton.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtkicontheme.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkradiobutton.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>

#include "fusa-user-menu-item.h"


/* **************** *
 *  Private Macros  *
 * **************** */

#define DEFAULT_ICON_SIZE		24
#define CLOSE_ENOUGH_SIZE		2

#define INDICATOR_ICON_NAME		GTK_STOCK_MEDIA_PAUSE
#define INSENSITIVE_INDICATOR_ICON_NAME	GTK_STOCK_MEDIA_PLAY
#define INDICATOR_ICON_SIZE		GTK_ICON_SIZE_MENU
#define DEFAULT_INDICATOR_SIZE		16


/* ********************** *
 *  Private Enumerations  *
 * ********************** */

enum
{
  PROP_0,
  PROP_USER,
  PROP_ICON_SIZE
};


/* ******************** *
 *  Private Structures  *
 * ******************** */
  
struct _FusaUserMenuItem
{
  GtkImageMenuItem parent;

  FusaUser *user;

  GtkWidget *image;
  GtkWidget *indicator_image;
  GtkWidget *label;

  gulong user_notify_id;
  gulong user_icon_changed_id;
  gulong user_displays_changed_id;
  gint icon_size;
  gint indicator_size;
};

struct _FusaUserMenuItemClass
{
  GtkImageMenuItemClass parent_class;
};


/* ********************* *
 *  Function Prototypes  *
 * ********************* */

/* GObject Functions */
static void fusa_user_menu_item_set_property (GObject      *object,
					      guint         param_id,
					      const GValue *value,
					      GParamSpec   *pspec);
static void fusa_user_menu_item_get_property (GObject      *object,
					      guint         param_id,
					      GValue       *value,
					      GParamSpec   *pspec);
static void fusa_user_menu_item_finalize     (GObject      *object);

/* FusaUser Callbacks */
static void user_notify_cb           (GObject    *object,
				      GParamSpec *pspec,
				      gpointer    data);
static void user_icon_changed_cb     (FusaUser   *user,
				      gpointer    data);
static void user_displays_changed_cb (FusaUser   *user,
				      gpointer    data);
static void indicator_style_set_cb   (GtkWidget *widget,
				      GtkStyle  *old_style,
				      gpointer   data);
static void user_weak_notify         (gpointer    data,
				      GObject    *user_ptr);

/* Widget Callbacks */
static void image_style_set_cb (GtkWidget *widget,
				GtkStyle  *old_style,
				gpointer   data);
static void label_style_set_cb (GtkWidget *widget,
				GtkStyle  *old_style,
				gpointer   data);

/* Utility Functions */
static void reset_label     (FusaUserMenuItem *item);
static void reset_icon      (FusaUserMenuItem *item);
static void reset_indicator (FusaUserMenuItem *item);


/* ******************* *
 *  GType Declaration  *
 * ******************* */

G_DEFINE_TYPE (FusaUserMenuItem, fusa_user_menu_item, GTK_TYPE_IMAGE_MENU_ITEM);


/* ***************** *
 *  GType Functions  *
 * ***************** */

static void
fusa_user_menu_item_class_init (FusaUserMenuItemClass *class)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = fusa_user_menu_item_set_property;
  gobject_class->get_property = fusa_user_menu_item_get_property;
  gobject_class->finalize = fusa_user_menu_item_finalize;

  g_object_class_install_property (gobject_class,
				   PROP_USER,
				   g_param_spec_object ("user",
							_("User"),
							_("The user this menu item represents."),
							FUSA_TYPE_USER,
							(G_PARAM_READWRITE |
							 G_PARAM_CONSTRUCT_ONLY)));
  g_object_class_install_property (gobject_class,
				   PROP_ICON_SIZE,
				   g_param_spec_int ("icon-size",
						     _("Icon Size"),
						     _("The size of the icon to use."),
						     12, G_MAXINT, DEFAULT_ICON_SIZE,
						     G_PARAM_READWRITE));
}

static void
fusa_user_menu_item_init (FusaUserMenuItem *item)
{
  GtkWidget *box;

  item->icon_size = DEFAULT_ICON_SIZE;
  item->indicator_size = DEFAULT_INDICATOR_SIZE;

  item->image = gtk_image_new ();
  g_signal_connect (item->image, "style-set",
		    G_CALLBACK (image_style_set_cb), item);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
				 item->image);
  gtk_widget_show (item->image);

  box = gtk_hbox_new (FALSE, 12);
  gtk_container_add (GTK_CONTAINER (item), box);
  gtk_widget_show (box);

  item->label = gtk_label_new (NULL);
  gtk_label_set_use_markup (GTK_LABEL (item->label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (item->label), 0.0, 0.5);
  g_signal_connect (item->label, "style-set",
		    G_CALLBACK (label_style_set_cb), item);
  gtk_container_add (GTK_CONTAINER (box), item->label);
  gtk_widget_show (item->label);

  item->indicator_image = gtk_image_new ();
  gtk_widget_set_size_request (item->indicator_image,
			       DEFAULT_INDICATOR_SIZE + CLOSE_ENOUGH_SIZE,
			       DEFAULT_INDICATOR_SIZE + CLOSE_ENOUGH_SIZE);
  g_signal_connect (item->indicator_image, "style-set",
		    G_CALLBACK (indicator_style_set_cb), item);
  gtk_box_pack_start (GTK_BOX (box), item->indicator_image,
		      FALSE, FALSE, 0);
  gtk_widget_show (item->indicator_image);
}


/* ******************* *
 *  GObject Functions  *
 * ******************* */

static void
fusa_user_menu_item_set_property (GObject      *object,
				  guint         param_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
  FusaUserMenuItem *item;

  item = FUSA_USER_MENU_ITEM (object);

  switch (param_id)
    {
    case PROP_USER:
      item->user = g_value_get_object (value);
      g_object_weak_ref (G_OBJECT (item->user), user_weak_notify, item);
      item->user_notify_id =
	g_signal_connect (item->user, "notify",
			  G_CALLBACK (user_notify_cb), item);
      item->user_icon_changed_id =
	g_signal_connect (item->user, "icon-changed",
			  G_CALLBACK (user_icon_changed_cb), item);
      item->user_displays_changed_id =
	g_signal_connect (item->user, "displays-changed",
			  G_CALLBACK (user_displays_changed_cb), item);

      if (gtk_widget_get_style (GTK_WIDGET (object)))
	{
	  reset_icon (item);
	  reset_label (item);
	}
      break;
    case PROP_ICON_SIZE:
      fusa_user_menu_item_set_icon_size (item, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
fusa_user_menu_item_get_property (GObject    *object,
				  guint       param_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
  FusaUserMenuItem *item;

  item = FUSA_USER_MENU_ITEM (object);

  switch (param_id)
    {
    case PROP_USER:
      g_value_set_object (value, item->user);
      break;
    case PROP_ICON_SIZE:
      g_value_set_int (value, item->icon_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
fusa_user_menu_item_finalize (GObject *object)
{
  FusaUserMenuItem *item;

  item = FUSA_USER_MENU_ITEM (object);

  if (item->user)
    {
      g_signal_handler_disconnect (item->user, item->user_notify_id);
      g_signal_handler_disconnect (item->user, item->user_icon_changed_id);
      g_signal_handler_disconnect (item->user, item->user_displays_changed_id);
      g_object_weak_unref (G_OBJECT (item->user), user_weak_notify, object);
    }

  if (G_OBJECT_CLASS (fusa_user_menu_item_parent_class)->finalize)
    (*G_OBJECT_CLASS (fusa_user_menu_item_parent_class)->finalize) (object);
}


/* ******************** *
 *  FusaUser Callbacks  *
 * ******************** */

static void
user_notify_cb (GObject    *object,
		GParamSpec *pspec,
		gpointer    data)
{
  if (!pspec || !pspec->name)
    return;

  if (strcmp (pspec->name, "user-name") == 0 ||
      strcmp (pspec->name, "display-name") == 0)
    reset_label (data);
}

static void
user_icon_changed_cb (FusaUser *user,
		      gpointer  data)
{
  if (gtk_widget_has_screen (data))
    reset_icon (data);
}

static void
user_displays_changed_cb (FusaUser *user,
			  gpointer  data)
{
  if (gtk_widget_has_screen (data))
    reset_indicator (data);

  if (fusa_user_get_uid (user) == getuid ())
    gtk_widget_set_sensitive (data, (fusa_user_get_n_displays (user) > 1));
}

static void
user_weak_notify (gpointer  data,
		  GObject  *user_ptr)
{
  FUSA_USER_MENU_ITEM (data)->user = NULL;

  gtk_widget_destroy (data);
}


/* ****************** *
 *  Widget Callbacks  *
 * ****************** */

static void
image_style_set_cb (GtkWidget *widget,
		    GtkStyle  *old_style,
		    gpointer   data)
{
  reset_icon (data);
}

static void
label_style_set_cb (GtkWidget *widget,
		    GtkStyle  *old_style,
		    gpointer   data)
{
  reset_label (data);
}

static void
indicator_style_set_cb (GtkWidget *widget,
			GtkStyle  *old_style,
			gpointer   data)
{
  reset_indicator (data);
}


/* ******************* *
 *  Utility Functions  *
 * ******************* */

static void
reset_label (FusaUserMenuItem *item)
{
  gchar *text;
  PangoLayout *layout;
  gint height;

  if (!item->user)
    return;

  text = g_strconcat ("<b>",
		      fusa_user_get_display_name (item->user),
		      "</b>\n",
		      "<small>",
		      fusa_user_get_user_name (item->user),
		      "</small>",
		      NULL);

  gtk_label_set_markup (GTK_LABEL (item->label), text);
  g_free (text);

  layout = gtk_label_get_layout (GTK_LABEL (item->label));
  pango_layout_get_pixel_size (layout, NULL, &height);

  if (height > (item->icon_size + CLOSE_ENOUGH_SIZE))
    gtk_label_set_markup (GTK_LABEL (item->label),
			  fusa_user_get_display_name (item->user));
}

static void
reset_icon (FusaUserMenuItem *item)
{
  GdkPixbuf *pixbuf;

  if (!item->user || !gtk_widget_has_screen (GTK_WIDGET (item)))
    return;

  g_assert (item->icon_size != 0);

  pixbuf = fusa_user_render_icon (item->user, GTK_WIDGET (item),
				  item->icon_size);
  gtk_image_set_from_pixbuf (GTK_IMAGE (item->image), pixbuf);
  g_object_unref (pixbuf);
}

static void
reset_indicator (FusaUserMenuItem *item)
{
  gint width, height;
  guint n_displays;
  GtkSettings *settings;

  n_displays = fusa_user_get_n_displays (item->user);
  if (n_displays > 0)
    {
      const gchar *indicator_name;

      if (fusa_user_get_uid (item->user) != getuid () || n_displays > 1)
	indicator_name = INDICATOR_ICON_NAME;
      else
	indicator_name = INSENSITIVE_INDICATOR_ICON_NAME;

      gtk_image_set_from_icon_name (GTK_IMAGE (item->indicator_image),
				    indicator_name, INDICATOR_ICON_SIZE);
    }
  else
    gtk_image_clear (GTK_IMAGE (item->indicator_image));

  if (gtk_widget_has_screen (item->indicator_image))
    settings = gtk_settings_get_for_screen (gtk_widget_get_screen (item->indicator_image));
  else
    settings = gtk_settings_get_default ();

  if (gtk_icon_size_lookup_for_settings (settings, INDICATOR_ICON_SIZE,
					 &width, &height))
    item->indicator_size = MAX (width, height);
  else
    item->indicator_size = DEFAULT_INDICATOR_SIZE;

  gtk_widget_set_size_request (item->indicator_image,
			       item->indicator_size,
			       item->indicator_size);
}


/* ************************************************************************** *
 *  Public API                                                                *
 * ************************************************************************** */

GtkWidget *
fusa_user_menu_item_new (FusaUser *user)
{
  g_return_val_if_fail (FUSA_IS_USER (user), NULL);

  return g_object_new (FUSA_TYPE_USER_MENU_ITEM, "user", user, NULL);
}

FusaUser *
fusa_user_menu_item_get_user (FusaUserMenuItem *item)
{
  g_return_val_if_fail (FUSA_IS_USER_MENU_ITEM (item), NULL);

  return item->user;
}

gint
fusa_user_menu_item_get_icon_size (FusaUserMenuItem *item)
{
  g_return_val_if_fail (FUSA_IS_USER_MENU_ITEM (item), -1);

  return item->icon_size;
}

void
fusa_user_menu_item_set_icon_size (FusaUserMenuItem *item,
				   gint              pixel_size)
{
  g_return_if_fail (FUSA_IS_USER_MENU_ITEM (item));
  g_return_if_fail (pixel_size != 0);

  if (pixel_size < 0)
    item->icon_size = DEFAULT_ICON_SIZE;
  else
    item->icon_size = pixel_size;

  if (gtk_widget_get_style (GTK_WIDGET (item)))
    {
      reset_icon (item);
      reset_label (item);
    }
  g_object_notify (G_OBJECT (item), "icon-size");
}
