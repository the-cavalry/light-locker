#include "light-locker-conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

static gpointer light_locker_conf_object = NULL;

/* Property identifiers */
enum
{
    PROP_0,
    PROP_LOCK_ON_SUSPEND,
    PROP_LATE_LOCKING,
    PROP_LOCK_AFTER_SCREENSAVER,
    N_PROP
};


static void light_locker_conf_finalize      (GObject        *object);
static void light_locker_conf_get_property  (GObject        *object,
                                             guint           prop_id,
                                             GValue         *value,
                                             GParamSpec     *pspec);
static void light_locker_conf_set_property  (GObject        *object,
                                             guint           prop_id,
                                             const GValue   *value,
                                             GParamSpec     *pspec);
static void light_locker_conf_prop_changed  (GSettings        *settings,
                                             const gchar      *prop_name,
                                             LightLockerConf  *conf);
GVariant *gvalue_to_gvariant                (const GValue *gvalue);
gboolean gvariant_to_gvalue                 (GVariant *variant,
                                             GValue *out_gvalue);


struct _LightLockerConfClass
{
    GObjectClass __parent__;
};

struct _LightLockerConf
{
    GObject    __parent__;
    GSettings *settings;
    gulong     property_changed_id;
};

G_DEFINE_TYPE (LightLockerConf, light_locker_conf, G_TYPE_OBJECT)


GVariant *gvalue_to_gvariant (const GValue *value)
{
    GVariant *variant = NULL;

    if (G_VALUE_HOLDS_BOOLEAN (value))
        variant = g_variant_new ("b", g_value_get_boolean(value));

    else if (G_VALUE_HOLDS_UINT (value))
        variant = g_variant_new ("u", g_value_get_uint(value));

    else
        g_warning ("Unable to convert GValue to GVariant");

    return variant;
}

gboolean gvariant_to_gvalue (GVariant *variant, GValue *out_gvalue)
{
    const GVariantType *type = g_variant_get_type (variant);

    if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
        g_value_init (out_gvalue, G_TYPE_BOOLEAN);

    else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT32))
        g_value_init (out_gvalue, G_TYPE_UINT);

    else
    {
        g_warning ("Unable to convert GVariant to GValue");
        return FALSE;
    }

    g_dbus_gvariant_to_gvalue (variant, out_gvalue);

    return TRUE;
}

/**
 * light_locker_conf_set_property:
 * @object  : a #LightLockerConf instance passed as #GObject.
 * @prop_id : the ID of the property being set.
 * @value   : the value of the property being set.
 * @pspec   : the property #GParamSpec.
 *
 * Write property-values to GSettings.
 **/
static void light_locker_conf_set_property (GObject *object,
                                            guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
    LightLockerConf  *conf = LIGHT_LOCKER_CONF (object);
    GVariant         *variant;
    gchar             prop_name[64];

    /* leave if the channel is not set */
    if (G_UNLIKELY (conf->settings == NULL))
        return;

    /* build property name */
    g_snprintf (prop_name, sizeof (prop_name), "%s", g_param_spec_get_name (pspec));

    /* freeze */
    g_signal_handler_block (conf->settings, conf->property_changed_id);

    /* convert and write */
    variant = gvalue_to_gvariant(value);
    g_settings_set_value (conf->settings, prop_name, variant);

    /* thaw */
    g_signal_handler_unblock (conf->settings, conf->property_changed_id);

    light_locker_conf_prop_changed(conf->settings, prop_name, conf);
}

/**
 * light_locker_conf_get_property:
 * @object  : a #LightLockerConf instance passed as #GObject.
 * @prop_id : the ID of the property being retrieved.
 * @value   : the return variable for the value of the property being retrieved.
 * @pspec   : the property #GParamSpec.
 *
 * Read property-values from GSettings.
 **/
static void light_locker_conf_get_property (GObject *object,
                                            guint prop_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
    LightLockerConf  *conf = LIGHT_LOCKER_CONF (object);
    GVariant         *variant = NULL;
    GValue            src = { 0, };
    gchar             prop_name[64];

    /* only set defaults if channel is not set */
    if (G_UNLIKELY (conf->settings == NULL))
    {
        g_param_value_set_default (pspec, value);
        return;
    }

    /* build property name */
    g_snprintf (prop_name, sizeof (prop_name), "%s", g_param_spec_get_name (pspec));

    variant = g_settings_get_value (conf->settings, prop_name);
    if (gvariant_to_gvalue(variant, &src))
    {
        if (G_VALUE_TYPE (value) == G_VALUE_TYPE (&src))
            g_value_copy (&src, value);
        else if (!g_value_transform (&src, value))
            g_printerr ("Failed to transform property %s\n", prop_name);
        g_value_unset (&src);
    }
    else
    {
        /* value is not found, return default */
        g_param_value_set_default (pspec, value);
    }
}

/**
 * light_locker_conf_prop_changed:
 * @channel   : the #XfconfChannel where settings are stored.
 * @prop_name : the name of the property being modified.
 * @conf      : the #ParoleConf instance.
 *
 * Event handler for when a property is modified.
 **/
static void light_locker_conf_prop_changed    (GSettings        *settings,
                                               const gchar      *prop_name,
                                               LightLockerConf  *conf)
{
    GParamSpec *pspec;

    /* check if the property exists and emit change */
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (conf), prop_name);
    if (G_LIKELY (pspec != NULL))
        g_object_notify_by_pspec (G_OBJECT (conf), pspec);

    g_debug("Propchange:%s,%p", prop_name, pspec);
}

/**
 * light_locker_conf_finalize:
 * @object : a #LightLockerConf instance passed as #GObject.
 *
 * Finalize a #LightLockerConf instance.
 **/
static void
light_locker_conf_finalize (GObject *object)
{
    LightLockerConf *conf = LIGHT_LOCKER_CONF (object);

    /* disconnect from the updates */
    g_signal_handler_disconnect (conf->settings, conf->property_changed_id);

    (*G_OBJECT_CLASS (light_locker_conf_parent_class)->finalize) (object);
}

/**
 * transform_string_to_boolean:
 * @src : source #GValue string to be transformed.
 * @dst : destination #GValue boolean variable to store the transformed string.
 *
 * Transform a #GValue string into a #GValue boolean.
 **/
static void
transform_string_to_boolean (const GValue *src,
                             GValue       *dst)
{
    g_value_set_boolean (dst, !g_strcmp0 (g_value_get_string (src), "TRUE"));
}

/**
 * transform_string_to_int:
 * @src : source #GValue string to be transformed.
 * @dst : destination #GValue int variable to store the transformed string.
 *
 * Transform a #GValue string into a #GValue int.
 **/
static void
transform_string_to_int (const GValue *src,
                         GValue       *dst)
{
    g_value_set_int (dst, strtol (g_value_get_string (src), NULL, 10));
}

/**
 * light_locker_conf_class_init:
 * @klass : a #LightLockerConfClass to initialize.
 *
 * Initialize a base #LightLockerConfClass instance.
 **/
static void
light_locker_conf_class_init (LightLockerConfClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = light_locker_conf_finalize;

    object_class->get_property = light_locker_conf_get_property;
    object_class->set_property = light_locker_conf_set_property;

    if (!g_value_type_transformable (G_TYPE_STRING, G_TYPE_INT))
        g_value_register_transform_func (G_TYPE_STRING, G_TYPE_INT, transform_string_to_int);

    if (!g_value_type_transformable (G_TYPE_STRING, G_TYPE_BOOLEAN))
        g_value_register_transform_func (G_TYPE_STRING, G_TYPE_BOOLEAN, transform_string_to_boolean);

    /**
     * LightLockerConf:lock-on-suspend:
     *
     * Enable lock-on-suspend
     **/
    g_object_class_install_property (object_class,
                                     PROP_LOCK_ON_SUSPEND,
                                     g_param_spec_boolean ("lock-on-suspend",
                                                           "/apps/light-locker/lock-on-suspend",
                                                           NULL,
                                                           FALSE,
                                                           G_PARAM_READWRITE));

    /**
     * LightLockerConf:late-locking:
     *
     * Lock after screensaver has ended.
     **/
    g_object_class_install_property (object_class,
                                     PROP_LATE_LOCKING,
                                     g_param_spec_boolean ("late-locking",
                                                           "/apps/light-locker/late-locking",
                                                           NULL,
                                                           FALSE,
                                                           G_PARAM_READWRITE));

    /**
     * LightLockerConf:lock-after-screensaver:
     *
     * Seconds after screensaver to lock
     **/
    g_object_class_install_property (object_class,
                                     PROP_LOCK_AFTER_SCREENSAVER,
                                     g_param_spec_uint ("lock-after-screensaver",
                                                       "/apps/light-locker/lock-after-screensaver",
                                                        NULL,
                                                        0,
                                                        3600,
                                                        0,
                                                        G_PARAM_READWRITE));

}

/**
 * light_locker_conf_init:
 * @conf : a #LightLockerConf instance.
 *
 * Initialize a #LightLockerConf instance.
 **/
static void
light_locker_conf_init (LightLockerConf *conf)
{
    conf->settings = g_settings_new("apps.light-locker");

    conf->property_changed_id =
    g_signal_connect (G_OBJECT (conf->settings), "changed",
                      G_CALLBACK (light_locker_conf_prop_changed), conf);
}

/**
 * light_locker_conf_new:
 *
 * Create a new #LightLockerConf instance.
 **/
LightLockerConf *
light_locker_conf_new (void)
{
    if ( light_locker_conf_object != NULL )
    {
        g_object_ref (light_locker_conf_object);
    }
    else
    {
        light_locker_conf_object = g_object_new (LIGHT_LOCKER_TYPE_CONF, NULL);
        g_object_add_weak_pointer (light_locker_conf_object, &light_locker_conf_object);
    }

    return LIGHT_LOCKER_CONF (light_locker_conf_object);
}
