#include "config.h" /* FRR config: must include */
#include "lib/zebra.h"
#include "lib/libfrr.h"
#include "lib/assert/assert.h"
#include "zebra/zebra_dplane.h"
#include "zebra/debug.h"

#define PLUGIN_NAME "Hedgehog-GW-plugin"

static const char *plugin_name = PLUGIN_NAME;

static struct zebra_dplane_provider *prov_p;

/*
 * Startup/init callback, called from the dataplane.
 */
static int zd_hh_start(struct zebra_dplane_provider *prov)
{
    zlog_info("%s: Starting...", dplane_provider_get_name(prov));

    return 0;
}

/*
 * Shutdown/cleanup callback, called from the dataplane pthread.
 */
static int zd_hh_fini(struct zebra_dplane_provider *prov, bool early)
{
    zlog_info("%s: Finalizing...", dplane_provider_get_name(prov));

    return 0;
}


/*
 * Callback from the dataplane to process incoming work; this runs in the
 * dplane pthread.
 */
static int zd_hh_process(struct zebra_dplane_provider *prov)
{
    int counter;
    int limit;
    struct zebra_dplane_ctx *ctx;

    if (IS_ZEBRA_DEBUG_DPLANE)
        zlog_debug("%s: Process...", dplane_provider_get_name(prov));

    limit = dplane_provider_get_work_limit(prov_p);
    for (counter = 0; counter < limit; counter++) {
        ctx = dplane_provider_dequeue_in_ctx(prov_p);
        if (!ctx)
            break;

        dplane_ctx_set_status(ctx, ZEBRA_DPLANE_REQUEST_SUCCESS);
        dplane_provider_enqueue_out_ctx(prov_p, ctx);
    }
    return 0;
}

/*
 * Init entry point called during zebra startup. This is registered during
 * module init.
 */
static int init_hh_dplane_plugin(struct event_loop *tm)
{
    int ret;

    /* Register the plugin with the dataplane infrastructure. We
     * register to be called before the kernel, and we register
     * our init, process work, and shutdown callbacks.
     */
    ret = dplane_provider_register(
            plugin_name, DPLANE_PRIO_PRE_KERNEL,
            DPLANE_PROV_FLAGS_DEFAULT,
            zd_hh_start, zd_hh_process, zd_hh_fini,
            NULL, &prov_p);

    if (ret != 0) {
        zlog_err("Failed to register %s provider: %d", plugin_name, ret);
        return ret;
    }

    zlog_info("Enabling interface extra info");
    dplane_enable_intf_extra_info();

#if 0 /* we don't need this atm */
    zlog_info("Enabling sys routes notifs");
    dplane_enable_sys_route_notifs();
#endif

    zlog_info("Sucessfully registered %s dataplane provider. Provider id is %u",
            plugin_name, dplane_provider_get_id(prov_p));
    return 0;
}

/*
 * Base FRR loadable module info: basic info including module entry-point.
 */
static int module_init(void)
{
    zlog_info("Initializing %s dataplane provider. FRR dataplane version is %u",
            plugin_name, zebra_dplane_get_version());

    hook_register(frr_late_init, init_hh_dplane_plugin);
    return 0;
}

FRR_MODULE_SETUP(
    .name = PLUGIN_NAME,
    .version = "0.0.1",
    .description = "Hedgehog GW dataplane plugin",
    .init = module_init,
);
