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

#define DPLANE_CONNECT_SEC 5 /* default connection-retry timer value */
#define NO_SOCK -1 /* sock descriptor initializer */

/* statics */
static const char *plugin_sock_path = "/var/run/frr/hhplugin.sock";
static const char *dp_sock_path = "/var/run/frr/hh_dataplane.sock";
static struct event *ev_connect_timer = NULL;
static struct event *ev_recv = NULL;
static struct event *ev_send = NULL;
static int dp_sock = NO_SOCK;
buff_t *tx_buff;
buff_t *rx_buff;

/*
 * Close unix socket to dataplane
 */
static void dp_unix_sock_close(void)
{
    if (dp_sock != NO_SOCK) {
        zlog_debug("Closing socket to dataplane...");
        close(dp_sock);
        dp_sock = NO_SOCK;
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
    if (!path_len || path_len >= sizeof(un_src.sun_path)) {
        zlog_err("Invalid unix socket path length of %zu", path_len);
        goto fail;
    } else {
        un_src.sun_family = AF_UNIX;
        strcpy(un_src.sun_path, bind_path);
    }

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
    return -1;
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

/* callback: sock is writable again */
static void send_pending_rpc_msgs(struct event *ignored) {
    send_rpc_msg(NULL);
}

/*
 * Plain send of RpcMsg. This function should only return success (0)
 * if the message was sent over the socket.
 */
static int do_send_rpc_msg(struct RpcMsg *msg)
{
    BUG(!msg, -1);
    BUG(!tx_buff, -1);

    zlog_debug("Sending request #%lu: %s %s",
            msg->request.seqn,
            str_rpc_op(msg->request.op),
            str_object_type(msg->request.object.type));

    if (msg->type == Request && msg->request.op != Connect && !dplane_is_ready()) {
        zlog_debug("Not sending request: dataplane has not yet answered");
        return -1;
    }

    buff_clear(tx_buff);

    /* encode the message into the tx buffer */
    int r = encode_msg(tx_buff, msg);
    if (r != E_OK ) {
        zlog_err("Fatal: failed to encode RPC message: %s", err2str(r));
        return -1;
    }

    /* send the buffer: we never block */
    r = send(dp_sock, tx_buff->storage, tx_buff->w, MSG_DONTWAIT);
    if (r == -1) {
        int _err = errno;
        struct event_loop *ev_loop = dplane_get_thread_master();
        switch(_err) {
            case ENOBUFS:
            case ENOMEM:
                zlog_err("Error sending msg to dataplane: %s", strerror(_err));
                /* fallthrough */
            case EAGAIN:
                /* sock is not writable at this point: register callback for later xmit */
                event_add_write(ev_loop, send_pending_rpc_msgs, NULL, dp_sock, &ev_send);
                return -1;
            case EINTR:
                zlog_warn("Tx to dataplane was interrupted!");
                return -1;
            default:
                zlog_err("Error sending msg to dataplane: %s", strerror(_err));
                return -1;
        }
    } else if ((index_t)r != tx_buff->w) {
        zlog_err("Error sending msg to dataplane: only %u out of %u octets sent", r, tx_buff->w);
        return -1;
    }
    /* success */
    return 0;
}

/* Main function to send an RPC message. If given a message, this function queues it for xmit
 * in the unsent queue and attempts to send all messages queued. This function is also called
 * from send_pending_rpc_msgs when the socket is writable again after an EAGAIN */
 int send_rpc_msg(struct dp_msg *dp_msg)
{
    /* cache in unsent list, at tail, since there may be messages pending for xmit */
    if (dp_msg)
        dp_msg_cache_unsent(dp_msg);

    /* Drain the unsent list (from head) until no more messages, or xmit fails */
    struct dp_msg *m;
    while((m = dp_msg_pop_unsent()) != NULL) {
        if (do_send_rpc_msg(&m->msg) == 0) {
            /* if it is a request, move to in-flight list */
            if (m->msg.type == Request)
                dp_msg_cache_inflight(m);
            else
                dp_msg_recycle(m);
        } else {
            /* send failed: put msg back at head of unsent list */
            dp_msg_unsent_push_back(m);
            break;
        }
    }
    /* this function always succeeds. Success does not necessarily imply that a message
     * has been sent. Generically it implies that the sending logic takes care of the
     * message and it will be sent when possible. */
    return 0;
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
         switch(_err) {
             case EAGAIN:
                 /* dp_rpc_recv() has registered callback. So, we'll retry
                  * when polling infra notifies availability */
                 return -1;
             case EINTR:
                 zlog_warn("Rx from dataplane was interrupted!");
                 return -1;
             default:
                 zlog_err("Error receiving msg from dataplane: %s", strerror(_err));
                 assert(0);
                 return -1;
         }
     }
     buff->w = (index_t)r;
     return r;
}

/*
 * Recv over unix socket with dataplane, decode messages
 * and call main handler
 */
static void dp_rpc_recv(struct event *thread)
{
    int r;

    /* sched next recv */
    event_add_read(thread->master, dp_rpc_recv, NULL, dp_sock, &ev_recv);

    /* Receive over sock on rx buff */
    while((r = sock_recv(rx_buff)) > 0) {
        struct RpcMsg msg = {0};

        /* decode message */
        r = decode_msg(rx_buff, &msg);
        if (r != E_OK) {
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

        /* send Connect RPC request with versioning info */
        send_rpc_request_connect();

        /* sched recv */
        event_add_read(ev_loop, dp_rpc_recv, NULL, dp_sock, &ev_recv);
    }
}

/* Finalize RPC to dataplane */
void fini_dplane_rpc(void)
{
    /* close Unix sock */
    dp_unix_sock_close();

    /* destroy rx / tx buffers */
    if (tx_buff)
        buff_free(tx_buff);
    if (rx_buff)
        buff_free(rx_buff);

    /* finalize message cache */
    fini_dp_msg_cache();
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

    /* open unix socket and bind it */
    dp_sock = dp_unix_sock_open(plugin_sock_path);
    if (dp_sock == -1)
        return -1;

    /* allocate Tx/Rx buffers for RPC encoding/decoding */
    if (init_rpc_buffers() != 0)
        return -1;

    /* initialize msg cache */
    init_dp_msg_cache();

    /* attempt connection to DP. This step in the initialization can fail
     * if the dataplane has not yet opened the unix socket for communication. */
    dp_connect(NULL);

    zlog_info("RPC initialization succeeded");

    /* success */
    return 0;
}
