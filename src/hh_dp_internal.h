// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SRC_HH_DP_INTERNAL_H_
#define SRC_HH_DP_INTERNAL_H_

#include "lib/assert/assert.h"
#include "lib/zlog.h"
#include "hh_dp_config.h"

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/* FRR redefines assert() to uncoditionally
 * abort and we may not want this in production builds,
 * at least not in the plugin.
 */

#define BUG(cond, ...)                                        \
    do {                                                      \
         if (unlikely(cond)) {                                \
             zlog(LOG_CRIT, "BUG: '%s' at %s, %s():%d]",      \
                 #cond, __FILE__, __FUNCTION__, __LINE__);    \
            if (DEBUG_BUILD)                                  \
                assert(0);                                    \
            return __VA_ARGS__;                               \
        }                                                     \
    } while (0)

/* format buffer */
extern struct fmt_buff *fb;

#endif /* SRC_HH_DP_INTERNAL_H_ */
