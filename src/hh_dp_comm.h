#ifndef SRC_HH_DP_COMM_H_
#define SRC_HH_DP_COMM_H_

#include <stdbool.h>
#include <dplane-rpc/dplane-rpc.h>
#include "hh_dp_msg_cache.h"

extern bool log_dataplane_msg;

/* set dp unix sock local path */
int set_dp_sock_local_path(const char *path);

/* set dp unix sock remote path */
int set_dp_sock_remote_path(const char *path);

/* initialize RPC with dataplane */
int init_dplane_rpc(void);

/* Finalize RPC with dataplane */
void fini_dplane_rpc(void);

/* set the value of dataplane status */
void dplane_set_ready(bool ready);

/* get the value of dataplane status */
bool dplane_is_ready(void);

/* Tell if dataplane socket is connected */
bool dplane_sock_is_connected(void);

/* main function to send an RPC message to dataplane. RPC messages come
 * wrapped in a struct dp_msg, which may contain a pointer to a zebra_dplane_ctx
 * object. */
int send_rpc_msg(struct dp_msg *dp_msg);

/* send rpc messages awaiting to be sent */
void send_pending_rpc_msgs(struct event *ignored);

#endif /* SRC_HH_DP_COMM_H_ */
