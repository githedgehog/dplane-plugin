#ifndef SRC_HH_DP_COMM_H_
#define SRC_HH_DP_COMM_H_

#include <dplane-rpc/dplane-rpc.h>

/* initialize RPC with dataplane */
int init_dplane_rpc(void);

/* Finalize RPC with dataplane */
void fini_dplane_rpc(void);

/* main function to send an RPC message to dataplane */
int send_rpc_msg(struct RpcMsg *msg);

#endif /* SRC_HH_DP_COMM_H_ */
