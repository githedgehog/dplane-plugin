#ifndef SRC_HH_DP_CACHE_H_
#define SRC_HH_DP_CACHE_H_

#include "config.h" /* FRRs config */
#include "lib/libfrr.h"
#include <dplane-rpc/dplane-rpc.h> /* HH's rpc dataplane library */

/* memory type */
DECLARE_MGROUP(ZEBRA);
DECLARE_MTYPE(HH_DP_MSG);

/* custom list type */
PREDECL_DLIST(dp_msg_list);

/* Dataplane message envelope */
struct dp_msg {
    struct RpcMsg msg;
    struct zebra_dplane_ctx *ctx;
    struct dp_msg_list_item cache; /* internal linkage */
};

DECLARE_DLIST(dp_msg_list, struct dp_msg, cache);

/* initialize / finalize the msg cache */
int init_dp_msg_cache(void);
void fini_dp_msg_cache(void);

/* get a new dp_msg */
struct dp_msg *dp_msg_new(void);

/* dispose a dp_msg */
void dp_msg_recycle(struct dp_msg *msg);

/* put message in list (tail) of unsent messages */
void dp_msg_cache_unsent(struct dp_msg *msg);

/* put message in list (head) of unsent messages */
void dp_msg_unsent_push_back(struct dp_msg *msg);

struct dp_msg *dp_msg_pop_unsent(void);

/* length of unsent list */
size_t dp_msg_unsent_count(void);

/* put message in list of sent messages */
void dp_msg_cache_inflight(struct dp_msg *msg);
struct dp_msg *dp_msg_pop_inflight(void);

#endif /* SRC_HH_DP_CACHE_H_ */
