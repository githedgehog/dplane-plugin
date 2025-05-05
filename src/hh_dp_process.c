// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h" /* Include this explicitly */
#include "lib/zebra.h"
#include "lib/libfrr.h"
#include "lib/assert/assert.h"
#include "zebra/zebra_dplane.h"
#include "zebra/debug.h"

#include "hh_dp_internal.h"
#include "hh_dp_process.h"
#include "hh_dp_msg.h"

typedef enum hh_dp_res_e {
    HH_OK = ZEBRA_DPLANE_REQUEST_SUCCESS,
    HH_FAIL = ZEBRA_DPLANE_REQUEST_FAILURE,
    HH_QUEUED,
    HH_IGNORED,
    HH_BUG
} hh_dp_res_t;

static const char *zd_hh_ret_str(hh_dp_res_t value) {
    switch(value) {
        case HH_QUEUED: return "Queued-to-DP";
        case HH_IGNORED: return "Ignored";
        case HH_OK: return "Ok";
        case HH_FAIL: return "Fail";
        case HH_BUG: return "Bug";
        default: return "Unknown";
    }
}

static enum zebra_dplane_result hh_ret_to_zebra(hh_dp_res_t r) {

    switch(r) {
        case HH_OK:
        case HH_FAIL:
            return r;
        case HH_QUEUED:
            BUG(true, HH_BUG);
            return ZEBRA_DPLANE_REQUEST_FAILURE;
        case HH_IGNORED:
            return ZEBRA_DPLANE_REQUEST_SUCCESS;
        case HH_BUG:
        default:
            return ZEBRA_DPLANE_REQUEST_FAILURE;
    }
}

static hh_dp_res_t hh_process_routes(struct zebra_dplane_ctx *ctx)
{
    afi_t afi = dplane_ctx_get_afi(ctx);
    safi_t safi = dplane_ctx_get_safi(ctx);
    int type = dplane_ctx_get_type(ctx);

    /* currently we only care about IPv4 and L2vpn */
    if (afi != AFI_IP && afi != AFI_L2VPN)
        return HH_IGNORED;

    /* currently we only care about unicast and evpn */
    if (safi != SAFI_UNICAST && safi != SAFI_EVPN)
        return HH_IGNORED;

    int r;
    switch(dplane_ctx_get_op(ctx)) {
        case DPLANE_OP_ROUTE_INSTALL:
            r = send_rpc_request_iproute(Add, ctx);
            break;
        case DPLANE_OP_ROUTE_UPDATE:
            r = send_rpc_request_iproute(Update, ctx);
            break;
        case DPLANE_OP_ROUTE_DELETE:
            r = send_rpc_request_iproute(Del, ctx);
            break;
        case DPLANE_OP_ROUTE_NOTIFY:
            return HH_IGNORED;
        case DPLANE_OP_SYS_ROUTE_ADD:
        case DPLANE_OP_SYS_ROUTE_DELETE:
            return HH_IGNORED;
        default:
            BUG(true, HH_BUG);
    }
    return !r ? HH_QUEUED: HH_FAIL;
}
static hh_dp_res_t hh_process_ifaddr(struct zebra_dplane_ctx *ctx)
{
    /* we only care about ipv4 atm */
    const struct prefix *ifaddr = dplane_ctx_get_intf_addr(ctx);
    if (ifaddr->family != AF_INET)
        return HH_IGNORED;

    int r;
    switch (dplane_ctx_get_op(ctx))
    {
        case DPLANE_OP_ADDR_INSTALL:
            r = send_rpc_request_ifaddress(Add, ctx);
            break;
        case DPLANE_OP_ADDR_UNINSTALL:
            r = send_rpc_request_ifaddress(Del, ctx);
            break;
        default:
            BUG(true, HH_BUG);
    }
    return !r ? HH_QUEUED: HH_FAIL;
}
static hh_dp_res_t hh_process_neigh(struct zebra_dplane_ctx *ctx)
{
    switch (dplane_ctx_get_op(ctx)) {
        case DPLANE_OP_NEIGH_INSTALL:
        case DPLANE_OP_NEIGH_UPDATE:
        case DPLANE_OP_NEIGH_DELETE:
        case DPLANE_OP_NEIGH_DISCOVER:
            return HH_IGNORED;
        default:
            BUG(true, HH_BUG);
    }
}
static hh_dp_res_t hh_process_nh(struct zebra_dplane_ctx *ctx)
{
    switch (dplane_ctx_get_op(ctx)) {
        case DPLANE_OP_NH_INSTALL:
        case DPLANE_OP_NH_UPDATE:
        case DPLANE_OP_NH_DELETE:
            return HH_IGNORED;
        default:
            BUG(true, HH_BUG);
    }
}
static hh_dp_res_t hh_process_macinfo(struct zebra_dplane_ctx *ctx)
{
    int r;
    switch(dplane_ctx_get_op(ctx)) {
        case DPLANE_OP_MAC_INSTALL:
            r = send_rpc_request_rmac(Add, ctx);
            break;
        case DPLANE_OP_MAC_DELETE:
            r = send_rpc_request_rmac(Del, ctx);
            break;
        default:
            BUG(true, HH_BUG);
    }
    return !r ? HH_QUEUED: HH_FAIL;
}
static hh_dp_res_t hh_process(struct zebra_dplane_ctx *ctx)
{
    if (IS_ZEBRA_DEBUG_DPLANE)
        zlog_debug("HH-Plugin: processing [%s]... vrfid: %u", dplane_op2str(dplane_ctx_get_op(ctx)), dplane_ctx_get_vrf(ctx));

    switch (dplane_ctx_get_op(ctx))
    {
        /* noop */
        case DPLANE_OP_NONE:
            return HH_IGNORED;

        /* Route update - must process */
        case DPLANE_OP_ROUTE_INSTALL:
        case DPLANE_OP_ROUTE_UPDATE:
        case DPLANE_OP_ROUTE_DELETE:
        case DPLANE_OP_ROUTE_NOTIFY:
            return hh_process_routes(ctx);

        /* Nexthop update - may optionally process */
        case DPLANE_OP_NH_INSTALL:
        case DPLANE_OP_NH_UPDATE:
        case DPLANE_OP_NH_DELETE:
            return hh_process_nh(ctx);

        /* MPLS LSP update - Ignored */
        case DPLANE_OP_LSP_INSTALL:
        case DPLANE_OP_LSP_UPDATE:
        case DPLANE_OP_LSP_DELETE:
        case DPLANE_OP_LSP_NOTIFY:
            return HH_IGNORED;

        /* Pseudowire update - Ignored */
        case DPLANE_OP_PW_INSTALL:
        case DPLANE_OP_PW_UNINSTALL:
            return HH_IGNORED;

        /* System route notification - Ignored */
        /* Will only get those if zdplane_info.dg_sys_route_notifs is true,
         * which happens if dplane_enable_sys_route_notifs() has been called.
         */
        case DPLANE_OP_SYS_ROUTE_ADD:
        case DPLANE_OP_SYS_ROUTE_DELETE:
            return hh_process_routes(ctx);

        /* Interface address update */
        case DPLANE_OP_ADDR_INSTALL:
        case DPLANE_OP_ADDR_UNINSTALL:
            return hh_process_ifaddr(ctx);

        /* MAC address update (mac fwd) */
        case DPLANE_OP_MAC_INSTALL:
        case DPLANE_OP_MAC_DELETE:
            return hh_process_macinfo(ctx);

        /* EVPN neighbor updates */
        case DPLANE_OP_NEIGH_INSTALL:
        case DPLANE_OP_NEIGH_UPDATE:
        case DPLANE_OP_NEIGH_DELETE:
            return hh_process_neigh(ctx);

        /* EVPN VTEP updates */
        case DPLANE_OP_VTEP_ADD:
        case DPLANE_OP_VTEP_DELETE:
            return hh_process_neigh(ctx);

        /* Policy based routing rule update - Ignored */
        case DPLANE_OP_RULE_ADD:
        case DPLANE_OP_RULE_DELETE:
        case DPLANE_OP_RULE_UPDATE:
            return HH_IGNORED;

        /* Link layer address discovery */
        case DPLANE_OP_NEIGH_DISCOVER:
            return hh_process_neigh(ctx);

        /* bridge port update */
        case DPLANE_OP_BR_PORT_UPDATE:
            return HH_IGNORED;

        /* Policy based routing iptable update */
        case DPLANE_OP_IPTABLE_ADD:
        case DPLANE_OP_IPTABLE_DELETE:
            return HH_IGNORED;

        /* Policy based routing ipset update */
        case DPLANE_OP_IPSET_ADD:
        case DPLANE_OP_IPSET_DELETE:
        case DPLANE_OP_IPSET_ENTRY_ADD:
        case DPLANE_OP_IPSET_ENTRY_DELETE:
            return HH_IGNORED;

        /* LINK LAYER IP address update */
        case DPLANE_OP_NEIGH_IP_INSTALL:
        case DPLANE_OP_NEIGH_IP_DELETE:
        case DPLANE_OP_NEIGH_TABLE_UPDATE:
            return HH_IGNORED;

        /* GRE */
        case DPLANE_OP_GRE_SET:
            return HH_IGNORED;

        /* Incoming interface address events */
        case DPLANE_OP_INTF_ADDR_ADD:
        case DPLANE_OP_INTF_ADDR_DEL:
            return HH_IGNORED;

        /* Incoming interface config events */
        case DPLANE_OP_INTF_NETCONFIG:
            return HH_IGNORED;

        /* Interface update */
        case DPLANE_OP_INTF_INSTALL:
        case DPLANE_OP_INTF_UPDATE:
        case DPLANE_OP_INTF_DELETE:
            return HH_IGNORED;

        /* Traffic control */
        case DPLANE_OP_TC_QDISC_INSTALL:
        case DPLANE_OP_TC_QDISC_UNINSTALL:
        case DPLANE_OP_TC_CLASS_ADD:
        case DPLANE_OP_TC_CLASS_DELETE:
        case DPLANE_OP_TC_CLASS_UPDATE:
        case DPLANE_OP_TC_FILTER_ADD:
        case DPLANE_OP_TC_FILTER_DELETE:
        case DPLANE_OP_TC_FILTER_UPDATE:
            return HH_IGNORED;

        /* Startup Control */
        case DPLANE_OP_STARTUP_STAGE:
            return HH_IGNORED;

        /* Source address for SRv6 encapsulation */
        case DPLANE_OP_SRV6_ENCAP_SRCADDR_SET:
            return HH_IGNORED;
    }

    return HH_FAIL;
}

void zd_hh_process_update(struct zebra_dplane_provider *prov, struct zebra_dplane_ctx *ctx)
{
    hh_dp_res_t r = hh_process(ctx);

    if (IS_ZEBRA_DEBUG_DPLANE)
        zlog_debug("result: %s", zd_hh_ret_str(r));

    /* Currently, we do not want to skip kernel. If we do,
       this should not be called from here and be selectively done
       for certain ctx's only */
    if (0)
        dplane_ctx_set_skip_kernel(ctx);

    /* if we did not queue a request to dataplane (e.g. maybe because we're not interested
     * in that piece of information, or because there was a failure) set the result and
     * queue the ctx back to zebra.
     */
    if (r != HH_QUEUED) {
        /* map result to zebra's */
        enum zebra_dplane_result res = hh_ret_to_zebra(r);
        dplane_ctx_set_status(ctx, res);
        dplane_provider_enqueue_out_ctx(prov, ctx);
    }
}
