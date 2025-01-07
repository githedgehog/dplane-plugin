#ifndef SRC_HH_DP_COMM_H_
#define SRC_HH_DP_COMM_H_

#include <dplane-rpc/dplane-rpc.h>
#include "hh_dp_msg_cache.h"

/* initialize RPC with dataplane */
int init_dplane_rpc(void);

/* Finalize RPC with dataplane */
void fini_dplane_rpc(void);

/* main function to send an RPC message to dataplane. RPC messages come
 * wrapped in a struct dp_msg, which may contain a pointer to a zebra_dplane_ctx
 * object. */
int send_rpc_msg(struct dp_msg *dp_msg);

#endif /* SRC_HH_DP_COMM_H_ */
