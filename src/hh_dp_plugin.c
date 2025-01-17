#include "config.h" /* FRR config: must include */
#include "lib/zebra.h"
#include "lib/libfrr.h"
#include "lib/assert/assert.h"
#include "zebra/zebra_dplane.h"
#include "zebra/debug.h"

#include "hh_dp_internal.h"
#include "hh_dp_config.h"
#include "hh_dp_process.h"
#include "hh_dp_comm.h"
#include "hh_dp_utils.h"

#define PLUGIN_NAME "Hedgehog-GW-plugin"

static const char *plugin_name = PLUGIN_NAME;

struct zebra_dplane_provider *prov_p = NULL;

/* plugin options */
static const struct option plugin_long_opts[] = {
    {"local-dp-sock-path", required_argument, 0, 'l'},
    {"remote-dp-sock-path", required_argument, 0, 'r'},
    {NULL}
};

/* Main processor of plugin options */
static int process_plugin_opt(int opt, const char *opt_arg, const struct option *long_opt)
{
    int r = 0;

    zlog_debug("Processing HHGW plugin option '%s' ...", long_opt->name);

    switch(opt) {
        case 0:
            break;
        case 'l':
            r =  set_dp_sock_local_path(opt_arg);
            break;
        case 'r':
            r = set_dp_sock_remote_path(opt_arg);
            break;
        default:
            /* If we get here, either there is a bug in the utils,
             * or the plugin_long_opts[] array defines an option
             * that we did not handle in the switch!
             * (We cannot fix this at build time)
             */
            zlog_err("Missing handler for option '%c'", (char)opt);
            return -1;
    }
    return r;
}

/*
 * Startup/init callback, called from the dataplane.
 */
static int zd_hh_start(struct zebra_dplane_provider *prov)
{
    zlog_info("%s: Starting...", dplane_provider_get_name(prov));

    int r = init_dplane_rpc();
    if (r != 0) {
        zlog_err("Plugin RPC initialization failed!!");
        abort();
    }

    return 0; /* unused */
}

/*
 * Shutdown/cleanup callback, called from the dataplane pthread.
 */
static int zd_hh_fini(struct zebra_dplane_provider *prov, bool early)
{
    zlog_info("%s: Finalizing...", dplane_provider_get_name(prov));

    fini_dplane_rpc();
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

    limit = dplane_provider_get_work_limit(prov);
    for (counter = 0; counter < limit; counter++) {
        ctx = dplane_provider_dequeue_in_ctx(prov);
        if (!ctx)
            break;

        zd_hh_process_update(prov, ctx);
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

    zlog_info("Dataplane plugin version %s (%s)", VER_STRING, BUILD_TYPE);
    zlog_info("Commit %s branch %s tag %s", GIT_COMMIT, GIT_BRANCH, GIT_TAG);
    zlog_info("Built on %s (%s)", BUILD_DATE, BUILD_TYPE);

    /* parse plugin arguments */
    if (plugin_args_parse(PLUGIN_NAME, THIS_MODULE->load_args,
            plugin_long_opts, process_plugin_opt) != 0) {

        zlog_err("Fatal: errors parsing plugin parameters");

        /* will crash */
        return 1;
    }

    hook_register(frr_late_init, init_hh_dplane_plugin);
    return 0;
}

FRR_MODULE_SETUP(
    .name = PLUGIN_NAME,
    .version = VER_STRING,
    .description = "Hedgehog GW dataplane plugin",
    .init = module_init,
);
