#ifndef CANOFLASH_EXAMPLE_CONFIG_H
#define CANOFLASH_EXAMPLE_CONFIG_H

// Optional personal configuration; never included in the public checkout.
#if defined(__has_include)
#if __has_include("canoflash_example_config.local.h")
#include "canoflash_example_config.local.h"
#endif
#endif

// Replace through the local header. See examples/README.md.
#ifndef CF_EXAMPLE_API_KEY
#define CF_EXAMPLE_API_KEY "cfn_REPLACE_WITH_YOUR_GAME_KEY"
#endif

#endif
