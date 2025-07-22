// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h" /* FRRs config */

#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <dplane-rpc/dplane-rpc.h> /* struct RpcMsg, buff_t */

#include "lib/zebra.h"
#include "lib/libfrr.h"
#include "zebra/zebra_dplane.h" /* dplane_get_thread_master */

#include "hh_dp_internal.h"
#include "hh_dp_comm.h"
#include "hh_dp_msg.h"
#include "hh_dp_msg_cache.h"
#include "hh_dp_rpc_stats.h"

/* fw decl */
static void dp_connect(struct event *e);
static void dp_send_keepalive(struct event *e);
static void wakeon_dp_write_avail(void);

#define DPLANE_CONNECT_SEC 5 /* default connection-retry timer value */
#define DPLANE_KEEPALIVE_SEC 5 /* keepalive timer */
#define NO_SOCK -1 /* sock descriptor initializer */

/* max length of unix sock */
#define MAX_SUN_PATH sizeof(((struct sockaddr_un*)0)->sun_path)

/* default paths */
#define DFLT_LOC_DPSOCK_PATH "/var/run/frr/hhplugin.sock"
#define DFLT_REM_DPSOCK_PATH "/var/run/frr/hh_dataplane.sock"
_Static_assert ((sizeof(DFLT_LOC_DPSOCK_PATH) <= MAX_SUN_PATH), "Path exceeds unix max path length");
_Static_assert ((sizeof(DFLT_REM_DPSOCK_PATH) <= MAX_SUN_PATH), "Path exceeds unix max path length");

/* statics */
static char plugin_sock_path[MAX_SUN_PATH + 1] = DFLT_LOC_DPSOCK_PATH;
static char dp_sock_path[MAX_SUN_PATH + 1] = DFLT_REM_DPSOCK_PATH;
static struct event *ev_connect_timer = NULL;
static struct event *ev_recv = NULL;
static struct event *ev_send = NULL;
static struct event *ev_keepalive = NULL;
static int dp_sock = NO_SOCK;
static bool dp_sock_connected = false;
static buff_t *tx_buff;
static buff_t *rx_buff;
static struct fmt_buff FB = {0};
static bool __dplane_is_ready = false;
static uint64_t synt = 0;

/* global */
struct fmt_buff *fb = NULL;
bool log_dataplane_msg = true;

/* tell if a unix path is valid */
static bool is_valid_unix_path(const char *path)
{
    size_t path_len = strlen(path);
    if (!path_len || path_len > MAX_SUN_PATH) {
        zlog_err("Invalid unix socket path %s: too long %zu > %zu", path, path_len, MAX_SUN_PATH);
        return false;
    }
    return true;
}

/* set dp unix sock local path */
int set_dp_sock_local_path(const char *path)
{
    BUG(!path, -1);

    if (!is_valid_unix_path(path))
        return -1;

    strncpy(plugin_sock_path, path, MAX_SUN_PATH);
    zlog_debug("Configured dp local sock path to %s", plugin_sock_path);
    return 0;
}

/* set dp unix sock remote path */
int set_dp_sock_remote_path(const char *path)
{
    BUG(!path, -1);

    if (!is_valid_unix_path(path))
        return -1;

    strncpy(dp_sock_path, path, MAX_SUN_PATH);
    zlog_debug("Configured dp remote sock path to %s", dp_sock_path);
    return 0;
}

/* mark state of dataplane: readiness happens when DP replies to Connect successfully */
void dplane_set_ready(bool ready) {
    __dplane_is_ready = ready;
}

/* set the synt */
void dplane_set_synt(uint64_t value) {
    synt = value;
}

/* get the synt */
uint64_t dplane_get_synt(void) {
    return synt;
}

/* Tell if communication with dataplane has been established */
bool dplane_is_ready(void) {
    return __dplane_is_ready;
}

/* Tell if dataplane socket is connected */
bool dplane_sock_is_connected(void) {
    return dp_sock_connected;
}

/*
 * Close unix socket to dataplane
 */
static void dp_unix_sock_close(void)
{
    if (dp_sock != NO_SOCK) {
        zlog_debug("Closing socket to dataplane...");
        close(dp_sock);
        dp_sock = NO_SOCK;
        dp_sock_connected = false;
        dplane_set_ready(false);
    }
    if (unlink(plugin_sock_path) == 0)
        zlog_debug("Deleted unix path at '%s'", plugin_sock_path);
}

/*
 * Open a Unix socket and bind it to the specified path.
 */
static int dp_unix_sock_open(const char *bind_path)
{
    BUG(!bind_path, -1);

    zlog_debug("Opening unix sock for dataplane RPC...");

    /* open sock, non-blocking */
    int sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sock < 0) {
        zlog_err("Failed to open unix socket");
        return -1;
    }

    /* build bind address */
    struct sockaddr_un un_src = {0};
    size_t path_len = strlen(bind_path);
    BUG(!path_len || path_len >= sizeof(un_src.sun_path), -1);
    un_src.sun_family = AF_UNIX;
    strncpy(un_src.sun_path, bind_path, sizeof(un_src.sun_path));

    /* sanity: remove file system entry */
    if (unlink(un_src.sun_path) == 0)
        zlog_debug("Deleted unix socket path at '%s'", un_src.sun_path);

    /* bind to the provided path */
    if (bind(sock, (struct sockaddr*)&un_src, sizeof(un_src)) < 0) {
        zlog_err("Unable to bind to %s : %s", un_src.sun_path, strerror(errno));
        goto fail;
    }

    /* set permissions */
    if (chmod(bind_path, S_IRWXU | S_IRWXG | S_IRWXO) < 0 ) {
        zlog_err("Failure setting permissions to %s : %s", un_src.sun_path, strerror(errno));
        goto fail;
    }

    /* success */
    zlog_info("Successfully created unix socket for dataplane RPC");
    return sock;

fail:
    zlog_err("Failed to create unix socket");
    if (sock != NO_SOCK)
        close(sock);
    return NO_SOCK;
}

/*
 * Connect unix socket to the specified path
 */
static int dp_unix_connect(const char *conn_path)
{
    BUG(!conn_path, -1);
    BUG(dp_sock == NO_SOCK, -1);

    /* set path to connect to */
    struct sockaddr_un dst = {0};
    size_t path_len = strlen(conn_path);
    if (!path_len || path_len >= sizeof(dst.sun_path)) {
        zlog_err("Invalid unix socket path length of %zu", path_len);
        return -1;
    } else {
        dst.sun_family = AF_UNIX;
        strcpy(dst.sun_path, conn_path);
    }

    /* connect */
    zlog_debug("Connecting unix socket to '%s'...", conn_path);
    if (connect(dp_sock, (const struct sockaddr *)&dst, sizeof(dst)) < 0) {
        if (errno == EISCONN)
            return 0;
        zlog_err("Failed to connect to dataplane: %s", strerror(errno));
        return -1;
    }
    zlog_info("Successfully connected unix sock to '%s'", conn_path);
    return 0;
}

/* tells if a msg can be xmited. Connects always can */
static inline bool can_send_rpc_request(struct RpcMsg *msg)
{
    BUG(!msg, false);
    if ((msg->type == Request && msg->request.op == Connect) || dplane_is_ready()) {
        return true;
    } else {
        if (log_dataplane_msg)
            zlog_debug("Not sending request %s : dataplane availability has not been confirmed",
                    fmt_rpc_msg(fb, true, msg));
        return false;
    }
}

/*
 * Sending of a single RpcMsg. This function should only return success (0)
 * if the message was successfully sent over the socket.
 */
static int do_send_rpc_msg(struct RpcMsg *msg)
{
    BUG(!msg, -1);
    BUG(!tx_buff, -1);

    /* check if we're allowed to send message */
    if (!can_send_rpc_request(msg))
        return -1;

    if (log_dataplane_msg && msg->type != Control)
        zlog_debug("Sending %s", fmt_rpc_msg(fb, true, msg));

    /* encode the message into the tx buffer */
    buff_clear(tx_buff);
    int r = encode_msg(tx_buff, msg);
    if (r != E_OK ) {
        rpc_count_encode_failure();
        zlog_err("Fatal: failed to encode RPC message: %s", err2str(r));
        return -1;
    }

    /* send the buffer: we never block */
    r = send(dp_sock, tx_buff->storage, tx_buff->w, MSG_DONTWAIT);
    if (r == -1) {
        int _err = errno;
        (_err != EAGAIN) ? rpc_count_tx_failure() : rpc_count_tx_eagain();

        switch(_err) {
            case ENOBUFS:
            case ENOMEM:
                zlog_err("Temporary error sending msg to dataplane: %s(%d)", strerror(_err), _err);
            /* fallthrough */
            case EAGAIN:
            /* sock is not writable at this point: register callback for later xmit */
                wakeon_dp_write_avail();
                return -1;
            case EINTR:
                zlog_warn("Tx to dataplane was interrupted!");
                return -1;
            /* errors that require reconnecting */
            case EPIPE:
            case ENOTCONN:
            case ECONNREFUSED:
            case ECONNRESET:
                zlog_err("Connection error sending msg to dataplane: %s(%d)", strerror(_err), _err);
                dp_sock_connected = false;
                dplane_set_ready(false);
                if (!ev_connect_timer)
                    dp_connect(NULL);
                return -1;
            default:
                zlog_err("Error sending msg to dataplane: %s(%d)", strerror(_err), _err);
                return -1;
        }
    } else if ((index_t)r != tx_buff->w) {
        zlog_err("Error sending msg to dataplane: only %u out of %u octets sent", r, tx_buff->w);
        return -1;
    }
    /* success */
    rpc_count_tx();
    return 0;
}

/* Drain the unsent queue for xmit */
void send_pending_rpc_msgs(void)
{
    /// can connect's make it to this queue?
    /// if not, we can check here dplane_ready
    /// this way we don't need to pop a message
    /// check and then push_back on fail.


    /* Drain the unsent list (from head) until no more messages, or xmit fails */
    struct dp_msg *m;
    while((m = dp_msg_pop_unsent()) != NULL) {
        if (do_send_rpc_msg(&m->msg) == 0) {
            /* Tx succeeded: if msg is request, move to in-flight list; else, recycle */
            if (m->msg.type == Request) {
                rpc_count_request_sent(m->msg.request.op, m->msg.request.object.type);
                dp_msg_cache_inflight(m);
            } else {
                if (m->msg.type == Control)
                    rpc_count_ctl_tx();

                dp_msg_recycle(m);
            }
        } else {
            /* send failed: put msg back at head of unsent list */
            dp_msg_unsent_push_back(m);

            /* do not attempt to send more */
            return;
        }
    }
}

/* write callback */
static void dp_sock_send_cb(struct event *ev)
{
    BUG(ev->ref != &ev_send);

    zlog_debug("Attempting to send pending messages...");
    size_t pend_msg = dp_msg_unsent_count();
    if (!pend_msg) {
        zlog_debug("No pending messages to send");
        return;
    }
    zlog_debug("There are %zu pending messages", pend_msg);

    send_pending_rpc_msgs();

    /* if we did not finish, sched write */
    pend_msg = dp_msg_unsent_count();
    if (pend_msg)
        wakeon_dp_write_avail();
}
static void wakeon_dp_write_avail(void)
{
    if (!ev_send) {
        zlog_info("Requesting dp-sock write availability notification...");
        event_add_write(dplane_get_thread_master(), dp_sock_send_cb, NULL, dp_sock, &ev_send);
        assert(ev_send != NULL);
    }
}


/* Main function to send an RPC message. The dp_msg is not sent straight but queued in the
 * unsent queue (to preserve ordering) and any prior pending messages are sent first, by
 * calling send_pending_rpc_msgs(). Connect requests overtake any pending messsages.
 */
int send_rpc_msg(struct dp_msg *dp_msg)
{
    BUG(!dp_msg, -1);

    /* If we get a message for xmit and is a Connect, let it overtake all prior cached requests */
    if (dp_msg->msg.type == Request && dp_msg->msg.request.op == Connect) {
        if (do_send_rpc_msg(&dp_msg->msg) == 0) {
            rpc_count_request_sent(dp_msg->msg.request.op, dp_msg->msg.request.object.type);
            dp_msg_cache_inflight(dp_msg);
            return 0;
        } else {
            zlog_err("Failed to send Connect request");
            /* We just connected the socket successfully and send() failed. The only reasonable
             * options are failure due to a disconnect. In that case, do_send_rpc_msg() will already
             * have schedulled a new connection. If we happen to fail for any other reason, we're
             * screwed. Todo: revisit.
             */
            dp_msg_recycle(dp_msg);
            return -1;
        }
    } else {
        /* cache at tail of unsent list */
        dp_msg_cache_unsent(dp_msg);

        /* send messages in the unsent list */
        send_pending_rpc_msgs();

        /* Success does not necessarily imply that a message has been sent, but that the sending
         * logic takes care of sending it when possible */
        return 0;
    }
}

/*
 * Actual recv on Unix sock
 */
static int sock_recv(buff_t *buff)
{
    buff_clear(buff);

     /* plugin always does non-blocking rx's */
     int r = recv(dp_sock, buff->storage, buff->capacity, MSG_DONTWAIT);
     if (r == -1) {
         int _err = errno;
         (_err != EAGAIN) ? rpc_count_rx_failure() : rpc_count_rx_eagain();
         switch(_err) {
             case EAGAIN:
                 /* dp_rpc_recv() has registered callback. So, we'll retry
                  * when polling infra notifies availability */
                 return -1;
             case EINTR:
                 zlog_warn("Rx from dataplane was interrupted!");
                 return -1;
             default:
                 zlog_err("Error receiving msg from dataplane socket: %s", strerror(_err));
                 return -1;
         }
     }
     buff->w = (index_t)r;
     rpc_count_rx();
     return r;
}

/*
 * Recv over unix socket with dataplane, decode messages
 * and call main handler
 */
static void dp_rpc_recv(struct event *ev)
{
    BUG(!ev);
    BUG(ev->ref != &ev_recv);
    int r;

    /* sched next recv */
    event_add_read(ev->master, dp_rpc_recv, NULL, dp_sock, &ev_recv);

    /* Receive over sock on rx buff */
    while((r = sock_recv(rx_buff)) > 0) {
        struct RpcMsg msg = {0};

        /* decode message */
        int r = decode_msg(rx_buff, &msg);
        if (r != E_OK) {
            rpc_count_decode_failure();
            // TODO: this is unrecoverable ... ?
            zlog_err("Error decoding msg from dataplane: %s", err2str(r));
            return;
        }
        /* handle message */
        handle_rpc_msg(&msg);
    }
}

/*
 * Connect to dataplane over unix socket. On failure, schedule another connection
 * attempt after HH_DPLANE_CONNECT_SEC seconds.  On success, send an RPC request "Connect"
 * with our versioning information and schedule receiving from socket. Upon receiving the
 * response to the connect, if successful, sending of RPC messages should be allowed.
 */
static void dp_connect(struct event *e)
{
    struct event_loop *ev_loop = dplane_get_thread_master();

    zlog_debug("Attempting to connect to dataplane at '%s'....", dp_sock_path);
    int r = dp_unix_connect(dp_sock_path);
    if (r != 0) {
        event_add_timer(ev_loop, dp_connect, NULL, DPLANE_CONNECT_SEC, &ev_connect_timer);
    } else {
        ev_connect_timer = NULL;
        dp_sock_connected = true;
        send_rpc_request_connect(); /* always send connect again */

        /* sched recv */
        event_add_read(ev_loop, dp_rpc_recv, NULL, dp_sock, &ev_recv);
    }
}

/* send keepalives if we're connected */
static void dp_send_keepalive(struct event *e) {
    struct event_loop *ev_loop = dplane_get_thread_master();

    if (dplane_sock_is_connected() && dplane_is_ready()) {
        send_rpc_control();
    }
    event_add_timer(ev_loop, dp_send_keepalive, NULL, DPLANE_KEEPALIVE_SEC, &ev_keepalive);
}

/* Finalize RPC to dataplane */
void fini_dplane_rpc(void)
{
    /* close Unix sock */
    dp_unix_sock_close();

    /* politeness */
    event_cancel(&ev_recv);
    event_cancel(&ev_send);
    event_cancel(&ev_connect_timer);
    event_cancel(&ev_keepalive);

    /* destroy rx / tx buffers */
    if (tx_buff)
        buff_free(tx_buff);
    if (rx_buff)
        buff_free(rx_buff);

    /* finalize message cache */
    fini_dp_msg_cache();

    /* finalize format buffer */
    if (fb) {
        fini_fmt_buff(fb);
        fb = NULL;
    }

}

/* allocate Tx/Rx buffers for RPC encoding/decoding */
static int init_rpc_buffers(void)
{
    /* create buffers for tx and rx. Buffer for tx is automatically
     * resized as needed by rpc encoding functions. Buffer for rx should be
     * large enough to hold a maximum-sized RPC message, unless we peek the
     * socket and resize depending on the message length in the header. Since
     * we only expect responses for the time being, a default size (1024) should
     * be more than enough.*/

    tx_buff = buff_new(0);
    rx_buff = buff_new(0);
    if (!tx_buff || !rx_buff) {
        fini_dplane_rpc();
        zlog_err("Failed to initialize RPC rx/tx buffers");
        return -1;
    }
    zlog_debug("Initialized RPC rx/tx buffers");
    return 0;
}

/* Initialize RPC to dataplane */
int init_dplane_rpc(void)
{
    zlog_info("Initializing HHGW dataplane RPC...");

    /* initialize RPC formatting buffer */
    if (init_fmt_buff(&FB, 0) != 0) {
        zlog_err("RPC format buffer initialization failed!!");
        return -1;
    }
    fb = &FB;

    /* open unix socket and bind it */
    dp_sock = dp_unix_sock_open(plugin_sock_path);
    if (dp_sock == NO_SOCK)
        return -1;

    /* allocate Tx/Rx buffers for RPC encoding/decoding */
    if (init_rpc_buffers() != 0)
        return -1;

    /* initialize msg cache */
    init_dp_msg_cache();

    /* attempt connection to DP. This step in the initialization can fail
     * if the dataplane has not yet opened the unix socket for communication. */
    dp_connect(NULL);

    /* start keepalive probing */
    dp_send_keepalive(NULL);

    zlog_info("RPC initialization succeeded");

    /* success */
    return 0;
}
