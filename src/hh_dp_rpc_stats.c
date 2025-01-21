#include "config.h" /* FRR config.h */
#include <dplane-rpc/dplane-rpc.h> /* HH's rpc dataplane library */
#include "hh_dp_internal.h"
#include "hh_dp_rpc_stats.h"
#include "hh_dp_comm.h"

/* RPC statistics */
static struct rpc_stats RPC_STATS = {0};

/* IO: tx */
void rpc_count_tx(void) {
    atomic_fetch_add_explicit(&RPC_STATS.tx_ok, 1, memory_order_relaxed);
}
void rpc_count_tx_failure(void) {
    atomic_fetch_add_explicit(&RPC_STATS.tx_failure, 1, memory_order_relaxed);
}
void rpc_count_tx_eagain(void) {
    atomic_fetch_add_explicit(&RPC_STATS.tx_eagain, 1, memory_order_relaxed);
}

/* IO: rx */
void rpc_count_rx(void) {
    atomic_fetch_add_explicit(&RPC_STATS.rx_ok, 1, memory_order_relaxed);
}
void rpc_count_rx_failure(void) {
    atomic_fetch_add_explicit(&RPC_STATS.rx_failure, 1, memory_order_relaxed);
}
void rpc_count_rx_eagain(void) {
    atomic_fetch_add_explicit(&RPC_STATS.rx_eagain, 1, memory_order_relaxed);
}

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

/* vty: show RPC stats */
#define GET_REQ_COUNT(ot, op, name)  ({uint64_t __count = atomic_load_explicit(&RPC_STATS.requests[ot][op].name, memory_order_relaxed); __count;})
#define GET_REQ_COUNT_RC(ot, op, rc) ({uint64_t __count = atomic_load_explicit(&RPC_STATS.requests[ot][op].rescode[rc], memory_order_relaxed); __count;})
#define GET_IO_COUNT(name)  ({uint64_t __count = atomic_load_explicit(&RPC_STATS.name, memory_order_relaxed); __count;})
static inline void hh_vty_show_stats_io(struct vty *vty)
{
    BUG(!vty);
    vty_out(vty, "  ─────────────────────────────────────────────── IO stats ───────────────────────────────────────────────\n");
    vty_out(vty, " %14.14s %14.14s %14.14s %14.14s %14.14s %14.14s\n", "tx", "tx-fail", "tx-retry", "rx", "rx-fail", "rx-retry");
    vty_out(vty, " %14llu %14llu %14llu %14llu %14llu %14llu\n",
            GET_IO_COUNT(tx_ok),
            GET_IO_COUNT(tx_failure),
            GET_IO_COUNT(tx_eagain),
            GET_IO_COUNT(rx_ok),
            GET_IO_COUNT(rx_failure),
            GET_IO_COUNT(rx_eagain)
    );
}
static void hh_vty_show_stats_serialization(struct vty *vty)
{
    BUG(!vty);
    uint64_t countval64;

    vty_out(vty, "  ───────────────────────────────────────── Serialization errors ─────────────────────────────────────────\n");
    countval64 = atomic_load_explicit(&RPC_STATS.msg_encode_failure, memory_order_relaxed);
    vty_out(vty, "   encoding failures: %llu\n", countval64);
    countval64 = atomic_load_explicit(&RPC_STATS.msg_decode_failure, memory_order_relaxed);
    vty_out(vty, "   decoding failures: %llu\n", countval64);
}
static void hh_vty_show_stats_rpc(struct vty *vty)
{
    BUG(!vty);

    vty_out(vty, "  ──────────────────────────────────────────── RPC statistics ────────────────────────────────────────────\n");
    vty_out(vty, "\n%10.10s:%-9.9s: ", "Object", "Operation");
    vty_out(vty, "%14.14s %14.14s %14.14s ", "sent", "replied", "unk-error");
    for (enum RpcResultCode rc = Ok; rc < RpcResultCodeMax; rc++)
        vty_out(vty, "%14.14s%c", str_rescode(rc), rc == RpcResultCodeMax - 1 ? '\n': ' ');

    for (enum ObjType ot = None + 1; ot < MaxObjType; ot++) {
        if (ot == GetFilter)
            continue;
        for (enum RpcOp op = Connect; op < MaxRpcOp; op++) {
            /* there are some combinations that are not possible. Exclude them from output */
            if (op == Get ||
               (ot == ConnectInfo && op != Connect) || (ot != ConnectInfo && op == Connect) ||
               (op == Update && ot != IpRoute ))
                continue;

            /* sent, received, unknown-rescode */
            vty_out(vty, "%10.10s:%-9.9s: %14llu %14llu %14llu ",
                    str_object_type(ot), str_rpc_op(op),
                    GET_REQ_COUNT(ot, op, sent),
                    GET_REQ_COUNT(ot, op, replied),
                    GET_REQ_COUNT(ot, op, unk_err));

            /* rest of result codes */
            for (enum RpcResultCode rc = Ok; rc < RpcResultCodeMax; rc++)
                vty_out(vty, "%14llu%c", GET_REQ_COUNT_RC(ot, op, rc), rc == RpcResultCodeMax - 1 ? '\n': ' ');
        }
    }
    vty_out(vty, "\n\n");
}
void hh_vty_show_stats(struct vty *vty)
{
    BUG(!vty);

    // N.B. connected state and readiness are booleans but not atomic
    vty_out(vty, " Dataplane sock connected: %s\n", dplane_sock_is_connected() ? "yes" : "no");
    vty_out(vty, " Dataplane contacted: %s\n", dplane_is_ready() ? "yes" : "no");

    hh_vty_show_stats_io(vty);
    hh_vty_show_stats_serialization(vty);
    hh_vty_show_stats_rpc(vty);
}
