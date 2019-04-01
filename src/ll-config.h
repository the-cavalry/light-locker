#ifndef __LL_CONFIG_H
#define __LL_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

#define LL_TYPE_CONFIG ll_config_get_type ()
G_DECLARE_FINAL_TYPE (LLConfig, ll_config, LL, CONFIG, GObject)

LLConfig       *ll_config_new      (void);

G_END_DECLS;

#endif /* __LL_CONFIG_H */
