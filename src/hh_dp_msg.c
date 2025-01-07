#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "config.h" /* FRRs config.h */
#include <dplane-rpc/dplane-rpc.h> /* HH's rpc dataplane library */

#include "zebra/zebra_dplane.h"
#include "lib/libfrr.h"
#include "lib/assert/assert.h"
#include "zebra/debug.h"

#include "hh_dp_internal.h"
#include "hh_dp_comm.h" /* send_rpc_msg() */
#include "hh_dp_process.h" /* struct zebra_dplane_provider */
#include "hh_dp_msg_cache.h"
#include "hh_dp_msg.h"

bool dplane_acked_connect = false;
bool dplane_is_ready(void) {
    return dplane_acked_connect;
}

/* Build an Rpc Msg of type request */
static struct dp_msg *dp_request_new(RpcOp Op, struct zebra_dplane_ctx *ctx)
{
    BUG(!ctx && Op != Connect, NULL); /* all requests except connect require a context */

    static uint64_t seqnum = 1;
    struct dp_msg *m = dp_msg_new();  /* FRR's allocator aborts in case of OOM */
    if (m) {
        m->msg.type = Request;
        m->msg.request.op = Op;
        m->msg.request.seqn = seqnum++;
        m->ctx = ctx;
    }
    return m;
}

/* send a Connect request with our versioning information. The plugin should not send
 * other messages until the dataplane verifies the versioning information and replies
 * with a success. */
int send_rpc_request_connect(void)
{
    struct ver_info info = {.major = VER_DP_MAJOR, .minor = VER_DP_MINOR, .patch = VER_DP_PATCH};

    struct dp_msg *m = dp_request_new(Connect, NULL);
    verinfo_as_object(&m->msg.request.object, &info);

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

static RouteType encode_route_type(unsigned int zebra_route_type) {
    switch(zebra_route_type) {
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
    for (ALL_NEXTHOPS_PTR(nhg, nh)) {
        nhop_encode(&nhop, nh);
        ip_route_add_nhop(&route, &nhop);
    }

    /* build dp_msg with route */
    struct dp_msg *m = dp_request_new(op, ctx);
    iproute_as_object(&m->msg.request.object, &route);

    return send_rpc_msg(m);
}

/* handle messages from dataplane */
static inline bool cached_matches(struct RpcResponse *resp, struct RpcRequest *req) {
    return (resp->op == req->op) && (resp->seqn == req->seqn);
}
static void handle_rpc_response(struct RpcResponse *resp)
{
    BUG(!prov_p);

    zlog_debug("Handling response to request #%lu '%s': %s",
            resp->seqn, str_rpc_op(resp->op), str_rescode(resp->rescode));

    struct dp_msg *m = dp_msg_pop_inflight();
    if (!m) {
        zlog_err("Unable to find outstanding request");
        return;
    }
    // FIXME: we should only cache requests, or have a dedicated list for requests..
    assert(m->msg.type == Request);
    if (!cached_matches(resp, &m->msg.request)) {
        zlog_err("Too bad! Recovered request does not match incoming response");
        goto done;
    }

    zlog_debug("Request corresponds to %s(%s)", str_rpc_op(m->msg.request.op), str_object_type(m->msg.request.object.type));

    switch(resp->op) {
        case Connect:
            dplane_acked_connect = (resp->rescode == Ok);
            if (!dplane_acked_connect) {
                zlog_err("Dataplane refused Connect request. Aborting....");
                abort();
            }
            break;
        case Add:
        case Del:
        case Update:
            if (m->ctx) {
                /* set the result and queue the request back to zebra */
                enum zebra_dplane_result result = resp->rescode == Ok ? ZEBRA_DPLANE_REQUEST_SUCCESS : ZEBRA_DPLANE_REQUEST_FAILURE;
                dplane_ctx_set_status(m->ctx, result);
                dplane_provider_enqueue_out_ctx(prov_p, m->ctx);
                m->ctx = NULL;
                dplane_provider_work_ready();
            }
            break;
        case Get:
            /* Not handled yet */
            break;
        default:
            zlog_err("Received response to unknown operation %u!!", resp->op);
            break;
    }

done:
    /* recycle message */
    dp_msg_recycle(m);
}
void handle_rpc_msg(struct RpcMsg *msg)
{
    BUG(!msg);
    switch(msg->type) {
        case Response:
            handle_rpc_response(&msg->response);
            break;
        case Control:
        case Request:
        case Notification:
            /* These messages are not handled yet as the behavior is not specified */
            zlog_warn("Received msg '%s' from dataplane", str_msg_type(msg->type));
            break;
        default:
            zlog_err("Received msg of unknown type via dataplane socket");
            break;
    }
}


