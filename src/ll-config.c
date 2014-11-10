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


static void ll_config_get_property  (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec);
static void ll_config_set_property  (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec);


struct _LLConfigClass
{
    GObjectClass __parent__;
};

struct _LLConfig
{
    GObject    __parent__;
    GSettings *settings;
    guint      lock_after_screensaver;
    gboolean   late_locking : 1;
    gboolean   lock_on_suspend : 1;
};

G_DEFINE_TYPE (LLConfig, ll_config, G_TYPE_OBJECT)


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

    switch (prop_id)
    {
        case PROP_LATE_LOCKING:
            conf->late_locking = g_value_get_boolean(value);
            break;

        case PROP_LOCK_AFTER_SCREENSAVER:
            conf->lock_after_screensaver = g_value_get_uint(value);
            break;

        case PROP_LOCK_ON_SUSPEND:
            conf->lock_on_suspend = g_value_get_boolean(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
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

    switch (prop_id)
    {
        case PROP_LATE_LOCKING:
            g_value_set_boolean(value, conf->late_locking);
            break;

        case PROP_LOCK_AFTER_SCREENSAVER:
            g_value_set_uint(value, conf->lock_after_screensaver);
            break;

        case PROP_LOCK_ON_SUSPEND:
            g_value_set_boolean(value, conf->lock_on_suspend);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
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

    object_class->get_property = ll_config_get_property;
    object_class->set_property = ll_config_set_property;

    /**
     * LLConfig:lock-on-suspend:
     *
     * Enable lock-on-suspend
     **/
    g_object_class_install_property (object_class,
                                     PROP_LOCK_ON_SUSPEND,
                                     g_param_spec_boolean ("lock-on-suspend",
                                                           NULL,
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
                                                           NULL,
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
                                                        NULL,
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
    GSettingsSchema       *schema;
    GParamSpec           **prop_list;
    guint                  i, n_prop;

    schema_source = g_settings_schema_source_get_default();
    schema = g_settings_schema_source_lookup (schema_source, LIGHT_LOCKER_SCHEMA, FALSE);
    if (schema != NULL)
    {
        conf->settings = g_settings_new(LIGHT_LOCKER_SCHEMA);

#if 0
        g_settings_bind (conf->settings, "late-locking", conf, "late-locking", G_SETTINGS_BIND_DEFAULT);
        g_settings_bind (conf->settings, "lock-after-screensaver", conf, "lock-after-screensaver", G_SETTINGS_BIND_DEFAULT);
        g_settings_bind (conf->settings, "lock-on-suspend", conf, "lock-on-suspend", G_SETTINGS_BIND_DEFAULT);
#else
        prop_list = g_object_class_list_properties (G_OBJECT_GET_CLASS (conf), &n_prop);
        for (i = 0; i < n_prop; i++)
        {
            const gchar *name = g_param_spec_get_name (prop_list[i]);
            g_settings_bind (conf->settings, name, conf, name, G_SETTINGS_BIND_DEFAULT);
        }
        g_free (prop_list);
#endif

        g_settings_schema_unref (schema);
    }
    else
    {
        g_warning("Schema \"%s\" not found. Not storing runtime settings.", LIGHT_LOCKER_SCHEMA);
    }

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
