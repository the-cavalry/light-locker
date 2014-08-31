#ifndef __LIGHT_LOCKER_CONF_H
#define __LIGHT_LOCKER_CONF_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _LightLockerConfClass LightLockerConfClass;
typedef struct _LightLockerConf      LightLockerConf;

#define LIGHT_LOCKER_TYPE_CONF             (light_locker_conf_get_type () )
#define LIGHT_LOCKER_CONF(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHT_LOCKER_TYPE_CONF, LightLockerConf))
#define LIGHT_LOCKER_CONF_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHT_LOCKER_TYPE_CONF, LightLockerConfClass))
#define LIGHT_LOCKER_IS_CONF(o)            (G_TYPE_CHECK_INSTANCE_TYPE ((o), LIGHT_LOCKER_TYPE_CONF))
#define LIGHT_LOCKER_IS_CONF_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHT_LOCKER_TYPE_CONF))
#define LIGHT_LOCKER_CONF_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHT_LOCKER_TYPE_CONF, LightLockerConfClass))

GType              light_locker_conf_get_type           (void) G_GNUC_CONST;

LightLockerConf   *light_locker_conf_new                (void);


G_END_DECLS;

#endif /* __LIGHT_LOCKER_CONF_H */