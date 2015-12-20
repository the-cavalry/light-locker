#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#include "ll-config.h"

#define LIGHT_LOCKER_SCHEMA          "apps.light-locker"

/* Property identifiers */
enum
{
    PROP_0,
    PROP_LOCK_ON_SUSPEND,
    PROP_LATE_LOCKING,
    PROP_LOCK_AFTER_SCREENSAVER,
    PROP_LOCK_ON_LID,
    PROP_IDLE_HINT,
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
    gboolean   lock_on_lid : 1;
    gboolean   idle_hint : 1;
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

        case PROP_LOCK_ON_LID:
            conf->lock_on_lid = g_value_get_boolean(value);
            break;

        case PROP_IDLE_HINT:
            conf->idle_hint = g_value_get_boolean(value);
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

        case PROP_LOCK_ON_LID:
            g_value_set_boolean(value, conf->lock_on_lid);
            break;

        case PROP_IDLE_HINT:
            g_value_set_boolean(value, conf->idle_hint);
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

    /**
     * LLConfig:lock-on-lid:
     *
     * Enable lock-on-lid
     **/
    g_object_class_install_property (object_class,
                                     PROP_LOCK_ON_LID,
                                     g_param_spec_boolean ("lock-on-lid",
                                                           NULL,
                                                           NULL,
                                                           FALSE,
                                                           G_PARAM_READWRITE));

    /**
     * LLConfig:idle-hint:
     *
     * Set idle hint during screensaver
     **/
    g_object_class_install_property (object_class,
                                     PROP_IDLE_HINT,
                                     g_param_spec_boolean ("idle-hint",
                                                           NULL,
                                                           NULL,
                                                           FALSE,
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
#ifdef WITH_SETTINGS_BACKEND
    GSettingsSchemaSource *schema_source;
    GSettingsSchema       *schema;
    GParamSpec           **prop_list;
    guint                  i, n_prop;
#endif

    conf->lock_after_screensaver = 5;
#ifdef WITH_LATE_LOCKING
    conf->late_locking = WITH_LATE_LOCKING;
#endif
#ifdef WITH_LOCK_ON_SUSPEND
    conf->lock_on_suspend = WITH_LOCK_ON_SUSPEND;
#endif
#ifdef WITH_LOCK_ON_LID
    conf->lock_on_lid = WITH_LOCK_ON_LID;
#endif
    conf->idle_hint = FALSE;

#ifdef WITH_SETTINGS_BACKEND
#define GSETTINGS 1
#if WITH_SETTINGS_BACKEND == GSETTINGS
    schema_source = g_settings_schema_source_get_default();
    schema = g_settings_schema_source_lookup (schema_source, LIGHT_LOCKER_SCHEMA, TRUE);
    if (schema != NULL)
    {
        conf->settings = g_settings_new(LIGHT_LOCKER_SCHEMA);

        prop_list = g_object_class_list_properties (G_OBJECT_GET_CLASS (conf), &n_prop);
        for (i = 0; i < n_prop; i++)
        {
            const gchar *name = g_param_spec_get_name (prop_list[i]);
            g_settings_bind (conf->settings, name, conf, name, G_SETTINGS_BIND_DEFAULT);
        }
        g_free (prop_list);

        g_settings_schema_unref (schema);
    }
    else
    {
        g_warning("Schema \"%s\" not found. Not storing runtime settings.", LIGHT_LOCKER_SCHEMA);
    }
#endif
#undef GSETTINGS
#endif
}

/**
 * ll_config_new:
 *
 * Create a new #LLConfig instance.
 **/
LLConfig *
ll_config_new (void)
{
    return g_object_new (LL_TYPE_CONFIG, NULL);
}
