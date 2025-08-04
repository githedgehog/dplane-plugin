// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "config.h" /* FRRs config.h */
#include <dplane-rpc/dplane-rpc.h> /* HH's rpc dataplane library */

#include "zebra/zebra_dplane.h"
#include "lib/libfrr.h"
#include "lib/assert/assert.h"
#include "zebra/debug.h"

#include "hh_dp_internal.h"
#include "hh_dp_comm.h" /* send_rpc_msg() && dplane_is_ready() */
#include "hh_dp_process.h" /* struct zebra_dplane_provider */
#include "hh_dp_msg_cache.h"
#include "hh_dp_rpc_stats.h"
#include "hh_dp_msg.h"

static uint64_t seqnum = 1;

/* Build an Rpc Msg of type request */
static struct dp_msg *dp_request_new(RpcOp Op, struct zebra_dplane_ctx *ctx)
{
    BUG(!ctx && Op != Connect, NULL); /* all requests except connect require a context */
    struct dp_msg *m = dp_msg_new();  /* FRR's allocator aborts in case of OOM */
    if (m) {
        m->msg.type = Request;
        m->msg.request.op = Op;
        m->msg.request.seqn = Op == Connect ? 0 : seqnum++;
        m->ctx = ctx;
    }
    return m;
}

/* send a Connect request with our versioning information. The plugin should not send
 * other messages until the dataplane verifies the versioning information and replies
 * with a success. */
int send_rpc_request_connect(void)
{
    struct conn_info cinfo = {
            .name = "FRR-HHGW-plugin",
            .pid = (uint32_t)getpid(),
            .verinfo = {
                .major = VER_DP_MAJOR,
                .minor = VER_DP_MINOR,
                .patch = VER_DP_PATCH},
            .synt = dplane_get_synt()
    };
    struct dp_msg *m = dp_request_new(Connect, NULL);
    conninfo_as_object(&m->msg.request.object, &cinfo);
    return send_rpc_msg(m);
}

/* Send a request to Add / Del an interface address */
int send_rpc_request_ifaddress(RpcOp op, struct zebra_dplane_ctx *ctx)
{
    BUG(!ctx, -1);
    BUG(op != Add && op != Del, -1);
    struct ifaddress ifa = {0};

    const struct prefix *ifaddr = dplane_ctx_get_intf_addr(ctx);
    const char *ifname = dplane_ctx_get_ifname(ctx);
    strcpy(ifa.ifname, ifname);
    ifa.ifindex = dplane_ctx_get_ifindex(ctx);
    ifa.vrfid = dplane_ctx_get_ifp_vrf_id(ctx);
    ifa.len = ifaddr->prefixlen;
    if (ifaddr->family == AF_INET) {
        ifa.address.ipver = IPV4;
        ifa.address.addr.ipv4 = ifaddr->u.prefix4.s_addr;
    } else {
        ifa.address.ipver = IPV6;
        memcpy(ifa.address.addr.ipv6, ifaddr->u.prefix6.__in6_u.__u6_addr8, 16);
    }

    struct dp_msg *m = dp_request_new(op, ctx);
    ifaddress_as_object(&m->msg.request.object, &ifa);

    return send_rpc_msg(m);
}

/* Send a request to Add / Del an rmac */
int send_rpc_request_rmac(RpcOp op, struct zebra_dplane_ctx *ctx)
{
    BUG(!ctx, -1);
    BUG(op != Add && op != Del, -1);

    struct rmac rmac = {0};
    /* vni */
    rmac.vni = dplane_ctx_mac_get_vni(ctx);

    /* mac */
    const struct ethaddr *mac = dplane_ctx_mac_get_addr(ctx);
    for (register int i = 0; i < MAC_LEN ; i++)
        rmac.mac.bytes[i] = mac->octet[i];

    /* Ip address */
    const struct in_addr *vtep_ip = dplane_ctx_mac_get_vtep_ip(ctx);
    rmac.address.ipver = IPV4;
    rmac.address.addr.ipv4 = *((uint32_t*)vtep_ip);

    struct dp_msg *m = dp_request_new(op, ctx);
    rmac_as_object(&m->msg.request.object, &rmac);

    return send_rpc_msg(m);
}

/* encode an ip_route next-hop */
static inline void nhop_encode(struct next_hop *nhop, struct nexthop *nh)
{
    BUG(!nhop || !nh);
    memset(nhop, 0, sizeof(*nhop));

    nhop->vrfid = nh->vrf_id;
    nhop->ifindex = nh->ifindex;
    nhop->fwaction = Forward; /* unless overwritten below */

    /* next-hop address */
    switch(nh->type) {
        case NEXTHOP_TYPE_IFINDEX:
            break;
        case NEXTHOP_TYPE_IPV4:
        case NEXTHOP_TYPE_IPV4_IFINDEX:
            nhop->address.ipver = IPV4;
            nhop->address.addr.ipv4 = nh->gate.ipv4.s_addr;
            break;
        case NEXTHOP_TYPE_IPV6:
        case NEXTHOP_TYPE_IPV6_IFINDEX:
            nhop->address.ipver = IPV6;
            memcpy(nhop->address.addr.ipv6, nh->gate.ipv6.__in6_u.__u6_addr8, 16);
            break;
        case NEXTHOP_TYPE_BLACKHOLE:
            nhop->fwaction = Drop;
            break;
    }
    /* set encapsulation */
    if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_EVPN)) {
        if (nh->nh_encap_type == NET_VXLAN) {
            nhop->encap.type = VXLAN;
            nhop->encap.vxlan.vni = nh->nh_encap.vni;
        }
    }
}

/* map FRR's route types to the RPC types */
static RouteType encode_route_type(unsigned int zebra_route_type) {
    switch(zebra_route_type) {
        case ZEBRA_ROUTE_LOCAL: return Local;
        case ZEBRA_ROUTE_CONNECT: return Connected;
        case ZEBRA_ROUTE_STATIC: return Static;
        case ZEBRA_ROUTE_OSPF: return Ospf;
        case ZEBRA_ROUTE_ISIS: return Isis;
        case ZEBRA_ROUTE_BGP: return Bgp;
        default: return Other;
    }
}

/* Send a request to Add / Del / Update an ip route. Updates may be treated like Adds */
int send_rpc_request_iproute(RpcOp op, struct zebra_dplane_ctx *ctx)
{
    BUG(!ctx, -1);
    BUG(op != Add && op != Del && op != Update, -1);

    struct ip_route route = {0};
    route.tableid = dplane_ctx_get_table(ctx);
    route.type = encode_route_type(dplane_ctx_get_type(ctx));
    route.distance = dplane_ctx_get_distance(ctx);
    route.metric = dplane_ctx_get_metric(ctx);
    route.vrfid = dplane_ctx_get_vrf(ctx);

    const struct prefix *p = dplane_ctx_get_dest(ctx);
    route.len = p->prefixlen;
    if (p->family == AF_INET) {
        route.prefix.ipver = IPV4;
        route.prefix.addr.ipv4 = p->u.prefix4.s_addr;
    } else {
        route.prefix.ipver = IPV6;
        memcpy(route.prefix.addr.ipv6, p->u.prefix6.__in6_u.__u6_addr8, 16);
    }

    const struct nexthop_group *nhg = dplane_ctx_get_ng(ctx);
    struct nexthop *nh;
    struct next_hop nhop;

    // check if any of the next-hops is recursive
    bool has_recursive = false;
    for (ALL_NEXTHOPS_PTR(nhg, nh)) {
        if (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_RECURSIVE)) {
            has_recursive = true;
            break;
        }
    }

    // process next-hops
    for (ALL_NEXTHOPS_PTR(nhg, nh)) {
        if NEXTHOP_IS_ACTIVE(nh->flags) {
            // if we have recursive next-hops, only send those. Else, send them all.
            if ((has_recursive && (CHECK_FLAG(nh->flags, NEXTHOP_FLAG_RECURSIVE))) || !has_recursive) {
                nhop_encode(&nhop, nh);
                ip_route_add_nhop(&route, &nhop);
            }
        }
    }

    /* build dp_msg with route */
    struct dp_msg *m = dp_request_new(op, ctx);
    iproute_as_object(&m->msg.request.object, &route);

    return send_rpc_msg(m);
}

/* Send a control message (keepalive) */
int send_rpc_control(uint8_t refresh) {
    struct dp_msg *m = dp_msg_new();
    m->msg.type = Control;
    m->msg.control.refresh = refresh;
    return send_rpc_msg(m);
}

/* handle messages from dataplane */
static inline bool got_expected_response(struct RpcResponse *resp, struct RpcRequest *req) {
    if (unlikely((resp->seqn != req->seqn) || (resp->op != req->op))) {
        /* We got a response that does not match the one we expected. Since Unix socks
         * are reliable and assuming that the "connection" was not dropped, this can only
         * happen due to: 1) a bug in this code 2) the peer (dataplane) not sending some
         * response, reordering responses or badly sequencing them 3) dataplane having restarted
         * and having send a connect response. However, we solve 3) before we get here */
        zlog_err("Warning! recovered request (op: %s, seqn: %lu) does not match received response (op: %s, seqn: %lu) !!",
                str_rpc_op(req->op), req->seqn, str_rpc_op(resp->op), resp->seqn);
        return false;
    } else {
        return true;
    }
}
static struct dp_msg *recover_request(struct RpcResponse *resp)
{
    BUG(!resp, NULL);

    /* dequeue msg from in-flight list/queue */
    struct dp_msg *m = dp_msg_pop_inflight();
    if (!m) {
        /* we got a response but had no request outstanding. Either we failed to store a request
         * or received an unsolicited / duplicate response */
        zlog_err("Unable to find request with seqn #%lu: there are no outstanding requests", resp->seqn);
        return NULL;
    }
    /* we should recover a request, since we only cache requests */
    if (m->msg.type != Request) {
        zlog_err("BUG: message recovered from in-flight queue is not a request !!");
        goto done;
    }
    /* make sure that the request corresponds to the response. We rely on responses not being re-ordered
     * here, by design. Otherwise a hash table keyed on the seqn could be used, instead of a list */
    if (!got_expected_response(resp, &m->msg.request))
        goto done;

    /* success: we recovered the right request */
    return m;

done:
    /* drop message */
    if (m)
        dp_msg_recycle(m);
    return NULL;
}
static void handle_rpc_connect_response(struct RpcResponse *resp, bool purged)
{
    BUG(!resp);

    if (resp->objects == NULL) {
        zlog_warn("Warning, got Connect response without connection info!");
    } else {
        zlog_debug("Got connect response: %s", fmt_rpcobject(fb, true, resp->objects));
    }

    if (resp->rescode == Ok) {
        if (!dplane_is_ready())
            zlog_info("Dataplane positively acked Connect.");

        /* recall synt */
        if (resp->objects)
            dplane_set_synt(resp->objects->conn_info.synt);

        /* allow further communications */
        dplane_set_ready(true);

        /* attempt to send messages that we cached because DP had not opened
         * socket or we had not received response to Connect request, but only if
         * we're not purging. Otherwise, this would cause messages to be sent
         * while the purge is ongoing and we'd answer on behalf of dataplane */
        if (!purged)
            send_pending_rpc_msgs();

    } else {
        zlog_err("Dataplane refused Connect request. Aborting....");
        abort();
    }
}
static void handle_rpc_ctx_response(struct dp_msg *m, RpcResultCode rescode)
{
    BUG(!prov_p);
    BUG(!m);

    if (m->ctx) {
        /* set the result - we treat ignored requests as successes for the time being */
        enum zebra_dplane_result result = (rescode == Ok || rescode == Ignored) ? ZEBRA_DPLANE_REQUEST_SUCCESS : ZEBRA_DPLANE_REQUEST_FAILURE;
        dplane_ctx_set_status(m->ctx, result);

        /* queue back to zebra */
        dplane_provider_enqueue_out_ctx(prov_p, m->ctx);
        dplane_provider_work_ready();
        m->ctx = NULL; /* imposed to allow recycle */
    }
}
static void do_handle_rpc_response(struct RpcResponse *resp, struct dp_msg *m, bool purged) {
    BUG(!resp || !m);

    char *pfx = purged ? "(purged)" : " ";

    /* account */
    rpc_count_request_replied(m->msg.request.op, m->msg.request.object.type, resp->rescode);

    /* log outcome of request */
    if (log_dataplane_msg) {
        switch(resp->rescode) {
            case Ok:
                zlog_debug("%s #%lu Op '%s' succeeded for %s", pfx, resp->seqn, str_rpc_op(m->msg.request.op), fmt_rpcobject(fb, true, &m->msg.request.object));
                break;
            case Ignored:
                zlog_debug("%s #%lu Op '%s' was ignored for %s", pfx, resp->seqn, str_rpc_op(m->msg.request.op), fmt_rpcobject(fb, true, &m->msg.request.object));
                break;
            default:
                zlog_err("%s #%lu Op '%s' FAILED(%s) for %s", pfx, resp->seqn, str_rpc_op(m->msg.request.op), str_rescode(resp->rescode), fmt_rpcobject(fb, true, &m->msg.request.object));
                break;
        }
    }

    /* handle response */
    switch(resp->op) {
        case Connect:
            handle_rpc_connect_response(resp, purged);
            break;
        case Add:
        case Del:
        case Update:
            handle_rpc_ctx_response(m, resp->rescode);
            break;
        default:
            zlog_err("Received response to unknown operation %u!!", resp->op);
            break;
    }
}
static void handle_rpc_response(struct RpcResponse *resp)
{
    BUG(!resp);

    /* Check if what we get is a response to a Connect */
    if (resp->op == Connect && resp->objects != NULL && dplane_get_synt() != 0 && resp->objects->conn_info.synt != dplane_get_synt()) {
        zlog_warn("Dataplane restarted! Purging in-flight queue...");

        /* Alright, we have strong evidence that dataplane restarted: we were connected before and it reports a distinct
         * sync token. This means chances are that we are expecting responses to requests sent to the previous incarnation
         * that we will never receive. Since dataplane will request a refresh of the state, drain the in-flight
         * queue pretending that all of the operations succeeded. On refresh, if they fail, the right state will
         * be sent back to frr. The only response we don't fake is the one for the Connect request.
         */
        struct dp_msg *m;
        while ((m = dp_msg_pop_inflight()) != 0) {
            if (m->msg.request.op != Connect) {
                struct RpcResponse fake = {0};
                fake.seqn = m->msg.request.seqn;
                fake.op = m->msg.request.op;
                fake.rescode = Ok;
                do_handle_rpc_response(&fake, m, true);
            } else {
                do_handle_rpc_response(resp, m, true);
            }
            dp_msg_recycle(m);
        }
        zlog_debug("Post dataplane restart purge completed");
        return;
    }

    /* lookup the request that we cached until a response was received */
    struct dp_msg *m = recover_request(resp);
    if (!m)
        return;

    /* handle the response */
    do_handle_rpc_response(resp, m, false);

    /* recycle message */
    dp_msg_recycle(m);
}
static void handle_rpc_control(struct RpcControl *ctl) {
    if (ctl->refresh) {
        zlog_warn("Got refresh request from dataplane. Requesting refresh...");
        zebra_dplane_provider_refresh(dplane_provider_get_id(prov_p), DPLANE_REFRESH_ALL);
        send_rpc_control(1);
    }
    rpc_count_ctl_rx();
}


/* entry point for incoming messages */
void handle_rpc_msg(struct RpcMsg *msg)
{
    BUG(!msg);

    if (log_dataplane_msg && msg->type != Control && msg->type != Response)
        zlog_debug("Handling %s", fmt_rpc_msg(fb, true, msg));

    switch(msg->type) {
        case Response:
            handle_rpc_response(&msg->response);
            break;
        case Control:
            handle_rpc_control(&msg->control);
            break;
        case Request:
        case Notification:
            /* These messages are not handled yet as the behavior is not specified */
            zlog_warn("Received msg '%s' from dataplane", str_msg_type(msg->type));
            break;
        default:
            zlog_err("Received msg of unknown type via dataplane socket");
            break;
    }

    /* free up additional resources */
    msg_dispose(msg);
}


