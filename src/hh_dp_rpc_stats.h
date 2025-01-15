#ifndef SRC_HH_DP_RPC_STATS_H_
#define SRC_HH_DP_RPC_STATS_H_

#include <stdint.h>
#include <dplane-rpc/proto.h> /* Codes for RpcOp and ObjType */

/* A stats cell for a request */
struct rpc_req_stat_cell {
    _Atomic uint64_t sent;                      /* number of requests sent */
    _Atomic uint64_t replied;                   /* number of responses received (See NOTE) */
    _Atomic uint64_t unk_err;                   /* request was answered, but result code seems not to be valid */
    _Atomic uint64_t rescode[RpcResultCodeMax]; /* outcome of the request, if answered */

    /* NOTE: For most requests, we may have that sent <= replied, equality happening when all requests have been
     * answered. However, the RPC wire protocol allows for more than one response to be sent in Get requests
     * (such responses but the last include result code ExpectMore). This is because, the protocol imposes a limit
     * on the maximum size of a message (hence response), which might be exceeded if responses contain many objects.
     * As a result, the "replied" counter may exceed the "sent" one in Get Operations.
     */
};

/* Main structure to keep RPC statistic counters */
struct rpc_stats {
    /* IO errors: tx */
    _Atomic uint64_t tx_ok;
    _Atomic uint64_t tx_failure;
    _Atomic uint64_t tx_eagain;

    /* IO errors: rx */
    _Atomic uint64_t rx_ok;
    _Atomic uint64_t rx_failure;
    _Atomic uint64_t rx_eagain;


    /* wire-protocol issues */
    _Atomic uint64_t msg_encode_failure;
    _Atomic uint64_t msg_decode_failure;

    /* stats for RPC requests */
    struct rpc_req_stat_cell requests[MaxObjType][MaxRpcOp];

    /* stats for other RPC message types ... */
};

/* Increment RPC stats counters */
void rpc_count_encode_failure(void);
void rpc_count_decode_failure(void);
void rpc_count_request_sent(enum RpcOp op, enum ObjType otype);
void rpc_count_request_replied(enum RpcOp op, enum ObjType otype, enum RpcResultCode rescode);

/* increment IO Tx counters */
void rpc_count_tx(void);
void rpc_count_tx_failure(void);
void rpc_count_tx_eagain(void);

/* increment IO Rx counters */
void rpc_count_rx(void);
void rpc_count_rx_failure(void);
void rpc_count_rx_eagain(void);

/* vty: show RPC stats */
void hh_vty_show_stats(struct vty *vty);

#endif /* SRC_HH_DP_RPC_STATS_H_ */
