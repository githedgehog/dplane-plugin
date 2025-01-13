#include "config.h" /* FRR config.h */
#include "hh_dp_internal.h"
#include "hh_dp_rpc_stats.h"

/* RPC statistics */
static struct rpc_stats RPC_STATS = {0};

/* account: RPC encode failures */
void rpc_count_encode_failure(void) {
    atomic_fetch_add_explicit(&RPC_STATS.msg_encode_failure, 1, memory_order_relaxed);
}

/* account: RPC decode failures */
void rpc_count_decode_failure(void) {
    atomic_fetch_add_explicit(&RPC_STATS.msg_decode_failure, 1, memory_order_relaxed);
}

/* account: RPC requests sent */
void rpc_count_request_sent(enum RpcOp op, enum ObjType otype) {
    BUG(op >= MaxRpcOp);
    BUG(otype >= MaxObjType);
    atomic_fetch_add_explicit(&RPC_STATS.requests[otype][op].sent, 1, memory_order_relaxed);
}

/* account: RPC requests replied (responses rx) */
void rpc_count_request_replied(enum RpcOp op, enum ObjType otype, enum RpcResultCode rescode)
{
    BUG(op >= MaxRpcOp);
    BUG(otype >= MaxObjType);

    atomic_fetch_add_explicit(&RPC_STATS.requests[otype][op].replied, 1, memory_order_relaxed);
    if (rescode < RpcResultCodeMax)
        atomic_fetch_add_explicit(&RPC_STATS.requests[otype][op].rescode[rescode], 1, memory_order_relaxed);
    else
        atomic_fetch_add_explicit(&RPC_STATS.requests[otype][op].unk_err, 1, memory_order_relaxed);
}

