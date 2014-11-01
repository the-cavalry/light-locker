#ifndef __LL_CONFIG_H
#define __LL_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _LLConfigClass LLConfigClass;
typedef struct _LLConfig      LLConfig;

#define LL_TYPE_CONFIG             (ll_config_get_type () )
#define LL_CONFIG(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), LL_TYPE_CONFIG, LLConfig))
#define LL_CONFIG_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), LL_TYPE_CONFIG, LLConfigClass))
#define LL_IS_CONFIG(o)            (G_TYPE_CHECK_INSTANCE_TYPE ((o), LL_TYPE_CONFIG))
#define LL_IS_CONFIG_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), LL_TYPE_CONFIG))
#define LL_CONFIG_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), LL_TYPE_CONFIG, LLConfigClass))

GType           ll_config_get_type (void) G_GNUC_CONST;

LLConfig       *ll_config_new      (void);


G_END_DECLS;

#endif /* __LL_CONFIG_H */
