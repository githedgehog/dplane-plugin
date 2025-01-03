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
#include "hh_dp_msg.h"

uint64_t seqnum = 1;
bool dplane_acked_connect = false;

/* send a Connect request with our versioning information. The plugin should not send
 * other messages until the dataplane verifies the versioning information and replies
 * with a success. */
int send_rpc_request_connect(void)
{
    struct ver_info info = {.major = VER_DP_MAJOR, .minor = VER_DP_MINOR, .patch = VER_DP_PATCH};
    struct RpcMsg msg = {.type = Request, .request.op = Connect, .request.seqn = seqnum++};
    verinfo_as_object(&msg.request.object, &info);
    return send_rpc_msg(&msg);
}

/* Send a request to Add / Del an interface address */
int send_rpc_request_ifaddress(RpcOp op, struct zebra_dplane_ctx *ctx)
{
    BUG(!ctx, -1);
    BUG(op != Add && op != Del, -1);
    struct ifaddress ifa = {0};

    const struct prefix *ifaddr = dplane_ctx_get_intf_addr(ctx);
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
    struct RpcMsg msg = {.type = Request, .request.op = op, .request.seqn = seqnum++};
    ifaddress_as_object(&msg.request.object, &ifa);
    return send_rpc_msg(&msg);
}


/* handle messages from dataplane */
static void handle_rpc_response(struct RpcResponse *resp)
{
    zlog_debug("Handling response to request #%lu '%s': %s",
            resp->seqn, str_rpc_op(resp->op), str_rescode(resp->rescode));

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
        case Get:
            // TODO
            zlog_warn("Unhandled response!");
            break;
        default:
            zlog_err("Received response to unknown operation %u!!", resp->op);
            break;
    }
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
            // TODO
            zlog_warn("Received msg '%s' from dataplane", str_msg_type(msg->type));
            break;
        default:
            break;
    }
}


