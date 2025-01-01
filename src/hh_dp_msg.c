#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "config.h" /* FRRs config.h */
#include <dplane-rpc/dplane-rpc.h> /* HH's rpc dataplane library */
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


