#ifndef SRC_HH_DP_MSG_H_
#define SRC_HH_DP_MSG_H_

#include <dplane-rpc/dplane-rpc.h>

/* Functions to send dataplane RPC requests */
int send_rpc_request_connect(void);
int send_rpc_request_ifaddress(RpcOp op, struct zebra_dplane_ctx *ctx);
int send_rpc_request_rmac(RpcOp op, struct zebra_dplane_ctx *ctx);
int send_rpc_request_iproute(RpcOp op, struct zebra_dplane_ctx *ctx);

/* Entry point for RPC msg processing */
void handle_rpc_msg(struct RpcMsg *msg);

#endif /* SRC_HH_DP_MSG_H_ */
