// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h" /* Include this explicitly */
#include "lib/libfrr.h"
#include "hh_dp_internal.h"
#include "hh_dp_msg_cache.h"
#include "zebra/zebra_dplane.h" /* dplane_ctx_reset() */

DEFINE_MTYPE(ZEBRA, HH_DP_MSG, "HH Dataplane msg");

/* Message cache */
struct dp_msg_cache {
    struct dp_msg_list_head pool; /* empty messages available for use */
    struct dp_msg_list_head unsent; /* messages that have not yet been sent */
    struct dp_msg_list_head in_flight; /* messages sent, not yet answered */
} msg_cache = {0};

/* free a dp_msg */
static void dp_msg_del(struct dp_msg *msg)
{
    BUG(!msg);
    if (msg->ctx) {
	zlog_warn("Deleting dp_msg referring to a ctx...");
        /* dplane_ctx_free is private, hence we cannot free the context. This is only
        * called on shutdown, so all good */
#if 0
        dplane_ctx_free(&msg->ctx);
#else
        /* Workaround that avoids needing to patch frr:
         * call dplane_ctx_reset() and free ourselves.
         * XFREE wraps free(). The danger here is that the memory tracking will
         * not be aware of this free. But that's fine since we do that on termination.
         */
        dplane_ctx_reset(msg->ctx);
        free(msg->ctx);
        msg->ctx = NULL;
#endif
    }
    XFREE(MTYPE_HH_DP_MSG, msg);
}

/* empty a dp_msg list */
static void empty_dp_msg_list(struct dp_msg_list_head *list, const char *name)
{
    BUG(!list);
    struct dp_msg *msg;
    zlog_debug("Emptying cache list '%s' (%zu messages)", name, dp_msg_list_count(list));
    while((msg = dp_msg_list_pop(list)) != NULL)
        dp_msg_del(msg);
}

/* allocate a dp_msg */
static struct dp_msg *dp_msg_alloc(void) {
    return XCALLOC(MTYPE_HH_DP_MSG, sizeof(struct dp_msg));
}

/* alloc a new message (or reuse from pool) */
struct dp_msg *dp_msg_new(void)
{
    if (dp_msg_list_count(&msg_cache.pool) == 0)
        return dp_msg_alloc();
    struct dp_msg *m = dp_msg_list_pop(&msg_cache.pool);
    if (m)
        memset(m, 0, sizeof(struct dp_msg));
    return m;
}

void log_dp_msg_lists(void) {
    zlog_debug("pool: %zu unsent: %zu in-flight: %zu",
        dp_msg_pool_count(),
        dp_msg_unsent_count(),
        dp_msg_in_flight_count());
}

/* recycle a dp_msg for later reuse */
void dp_msg_recycle(struct dp_msg *msg)
{
    BUG(!msg);
    BUG(msg->ctx); /* should have been disposed and cleared */
    dp_msg_list_add_tail(&msg_cache.pool, msg);
}

/* cache a message that plugin has not been able to send; e.g.
 * because dataplane was not ready, or due to backpressure on the
 * socket.
 */
void dp_msg_cache_unsent(struct dp_msg *msg)
{
    BUG(!msg);
    dp_msg_list_add_tail(&msg_cache.unsent, msg);
}

/* cache a message back to the head of unsent messages */
void dp_msg_unsent_push_back(struct dp_msg *msg)
{
    BUG(!msg);
    dp_msg_list_add_head(&msg_cache.unsent, msg);
}

/* dequeue msg from unsent queue */
struct dp_msg *dp_msg_pop_unsent(void) {
    return dp_msg_list_pop(&msg_cache.unsent);
}

/* length of unsent list */
size_t dp_msg_unsent_count(void) {
    return dp_msg_list_count(&msg_cache.unsent);
}

/* length of pool list */
size_t dp_msg_pool_count(void) {
    return dp_msg_list_count(&msg_cache.pool);
}

/* length of inflight list */
size_t dp_msg_in_flight_count(void) {
    return dp_msg_list_count(&msg_cache.in_flight);
}

/* Cache a message (e.g. a Request) until we get the corresponding response */
void dp_msg_cache_inflight(struct dp_msg *msg)
{
    BUG(!msg);
    assert(msg->msg.type == Request);
    dp_msg_list_add_tail(&msg_cache.in_flight, msg);
}

/* dequeue msg from in-flight queue */
struct dp_msg *dp_msg_pop_inflight(void) {
    return dp_msg_list_pop(&msg_cache.in_flight);
}

/* initialize dataplane message cache */
int init_dp_msg_cache(void)
{
    zlog_debug("Initializing dataplane message cache..");
    memset(&msg_cache, 0, sizeof(msg_cache));

    /* initialize lists */
    dp_msg_list_init(&msg_cache.pool);
    dp_msg_list_init(&msg_cache.unsent);
    dp_msg_list_init(&msg_cache.in_flight);

    /* prepopulate msg pool */
    struct dp_msg *msg;
    for(int i = 0; i < 1000; i++) {
        msg = dp_msg_alloc();
        if (msg)
            dp_msg_list_add_tail(&msg_cache.pool, msg);
    }
    zlog_debug("Initialized cache with pool of %zu messages", dp_msg_pool_count());
    return 0;
}

/* Finalize dataplane message cache */
void fini_dp_msg_cache(void)
{
    zlog_debug("Finalizing dataplane message cache..");

    empty_dp_msg_list(&msg_cache.pool, "pool");
    empty_dp_msg_list(&msg_cache.unsent, "unsent");
    empty_dp_msg_list(&msg_cache.in_flight, "in-flight");
}
