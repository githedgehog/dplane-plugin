#ifndef SRC_HH_DP_MSG_H_
#define SRC_HH_DP_MSG_H_

#include <dplane-rpc/dplane-rpc.h>

int send_rpc_request_connect(void);
void handle_rpc_msg(struct RpcMsg *msg);

#endif /* SRC_HH_DP_MSG_H_ */
