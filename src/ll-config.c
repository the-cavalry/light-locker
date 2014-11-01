#include "ll-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#define LIGHT_LOCKER_SCHEMA          "apps.light-locker"

static gpointer ll_config_object = NULL;

/* Property identifiers */
enum
{
    PROP_0,
    PROP_LOCK_ON_SUSPEND,
    PROP_LATE_LOCKING,
    PROP_LOCK_AFTER_SCREENSAVER,
    N_PROP
};


static void ll_config_finalize      (GObject        *object);
static void ll_config_get_property  (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec);
static void ll_config_set_property  (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec);
static void ll_config_prop_changed  (GSettings      *settings,
                                     const gchar    *prop_name,
                                     LLConfig       *conf);
GVariant *gvalue_to_gvariant        (const GValue   *gvalue);
gboolean gvariant_to_gvalue         (GVariant       *variant,
                                     GValue         *out_gvalue);


struct _LLConfigClass
{
    GObjectClass __parent__;
};

struct _LLConfig
{
    GObject    __parent__;
    GSettings *settings;
    gulong     property_changed_id;
};

G_DEFINE_TYPE (LLConfig, ll_config, G_TYPE_OBJECT)


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
 * ll_config_set_property:
 * @object  : a #LLConfig instance passed as #GObject.
 * @prop_id : the ID of the property being set.
 * @value   : the value of the property being set.
 * @pspec   : the property #GParamSpec.
 *
 * Write property-values to GSettings.
 **/
static void ll_config_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LLConfig  *conf = LL_CONFIG (object);
    GVariant  *variant;
    gchar      prop_name[64];

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

    ll_config_prop_changed(conf->settings, prop_name, conf);
}

/**
 * ll_config_get_property:
 * @object  : a #LLConfig instance passed as #GObject.
 * @prop_id : the ID of the property being retrieved.
 * @value   : the return variable for the value of the property being retrieved.
 * @pspec   : the property #GParamSpec.
 *
 * Read property-values from GSettings.
 **/
static void ll_config_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LLConfig  *conf = LL_CONFIG (object);
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
 * ll_config_prop_changed:
 * @settings  : the #GSettings instance where settings are stored.
 * @prop_name : the name of the property being modified.
 * @conf      : the #LLConfig instance.
 *
 * Event handler for when a property is modified.
 **/
static void ll_config_prop_changed (GSettings   *settings,
                                    const gchar *prop_name,
                                    LLConfig    *conf)
{
    GParamSpec *pspec;

    /* check if the property exists and emit change */
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (conf), prop_name);
    if (G_LIKELY (pspec != NULL))
        g_object_notify_by_pspec (G_OBJECT (conf), pspec);

    g_debug("Propchange:%s,%p", prop_name, pspec);
}

/**
 * ll_config_finalize:
 * @object : a #LLConfig instance passed as #GObject.
 *
 * Finalize a #LLConfig instance.
 **/
static void
ll_config_finalize (GObject *object)
{
    LLConfig *conf = LL_CONFIG (object);

    /* disconnect from the updates */
    g_signal_handler_disconnect (conf->settings, conf->property_changed_id);

    (*G_OBJECT_CLASS (ll_config_parent_class)->finalize) (object);
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
 * ll_config_class_init:
 * @klass : a #LLConfigClass to initialize.
 *
 * Initialize a base #LLConfigClass instance.
 **/
static void
ll_config_class_init (LLConfigClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = ll_config_finalize;

    object_class->get_property = ll_config_get_property;
    object_class->set_property = ll_config_set_property;

    if (!g_value_type_transformable (G_TYPE_STRING, G_TYPE_INT))
        g_value_register_transform_func (G_TYPE_STRING, G_TYPE_INT, transform_string_to_int);

    if (!g_value_type_transformable (G_TYPE_STRING, G_TYPE_BOOLEAN))
        g_value_register_transform_func (G_TYPE_STRING, G_TYPE_BOOLEAN, transform_string_to_boolean);

    /**
     * LLConfig:lock-on-suspend:
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
     * LLConfig:late-locking:
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
     * LLConfig:lock-after-screensaver:
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
 * ll_config_init:
 * @conf : a #LLConfig instance.
 *
 * Initialize a #LLConfig instance.
 **/
static void
ll_config_init (LLConfig *conf)
{
    GSettingsSchemaSource *schema_source;
    GSettingsSchema *schema;

    schema_source = g_settings_schema_source_get_default();
    schema = g_settings_schema_source_lookup (schema_source, LIGHT_LOCKER_SCHEMA, FALSE);
    if (schema != NULL)
    {
        conf->settings = g_settings_new(LIGHT_LOCKER_SCHEMA);
        conf->property_changed_id =
        g_signal_connect (G_OBJECT (conf->settings), "changed",
                          G_CALLBACK (ll_config_prop_changed), conf);
    } else
    {
        g_warning("Schema \"%s\" not found. Not storing runtime settings.", LIGHT_LOCKER_SCHEMA);
    }

    if (schema)
        g_settings_schema_unref (schema);
    /* FIXME: Segfault if trying to free the schema source
     * (process:21551): GLib-GIO-ERROR **: g_settings_schema_source_unref() called too many times on the default schema source */
    /* if (schema_source)
       g_settings_schema_source_unref (schema_source); */
}

/**
 * ll_config_new:
 *
 * Create a new #LLConfig instance.
 **/
LLConfig *
ll_config_new (void)
{
    if ( ll_config_object != NULL )
    {
        g_object_ref (ll_config_object);
    }
    else
    {
        ll_config_object = g_object_new (LL_TYPE_CONFIG, NULL);
        g_object_add_weak_pointer (ll_config_object, &ll_config_object);
    }

    return LL_CONFIG (ll_config_object);
}
