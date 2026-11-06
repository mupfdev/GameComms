/** @file gc_link.h
 *
 *  GCL - GameComms Link layer.
 *
 *  Reliable, ordered, duplicate-free message delivery over a byte stream
 *  that may corrupt, truncate or lose data and that may be torn down and
 *  re-established (Bluetooth SPP between N-Gage and bridge, TCP between
 *  bridge and server).  The same implementation is used on every hop.
 *
 *  Features
 *   - COBS/CRC framing (see gc_codec.h), resynchronises after garbage
 *   - HELLO handshake with protocol version negotiation
 *   - 16-bit sequence numbers, cumulative acknowledgements, go-back-N
 *     retransmission with exponential back-off
 *   - keep-alive and dead-peer detection
 *   - session resumption: a link identified by its 32-bit link id can
 *     survive a transport reconnect without losing or duplicating
 *     messages
 *
 *  The link is completely passive: it never blocks, never allocates and
 *  has no notion of wall-clock time other than the millisecond counter
 *  passed into every call.  This makes it trivial to unit-test and to
 *  embed into a Symbian active object, an Arduino loop() or a poll()
 *  based server.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GC_LINK_H
#define GC_LINK_H

#include "gc_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- protocol constants ---- */

#define GC_LINK_MAGIC_0      0x47 /* 'G' */
#define GC_LINK_MAGIC_1      0x43 /* 'C' */
#define GC_PROTO_VERSION_MIN 1
#define GC_PROTO_VERSION_MAX 1

#define GC_LINK_HDR_LEN 6
#define GC_LINK_MAX_MSG (GC_FRAME_MAX_BODY - GC_LINK_HDR_LEN)

#ifndef GC_LINK_QUEUE
#define GC_LINK_QUEUE 16 /* max. unacknowledged + pending messages */
#endif

/* frame types */
#define GC_FT_HELLO     0x01
#define GC_FT_HELLO_ACK 0x02
#define GC_FT_DATA      0x03
#define GC_FT_ACK       0x04
#define GC_FT_PING      0x05
#define GC_FT_PONG      0x06
#define GC_FT_CLOSE     0x07
#define GC_FT_REJECT    0x08

/* roles announced in HELLO (diagnostics / sanity checks only) */
#define GC_ROLE_DEVICE 1 /* N-Gage (GameComms DLL)       */
#define GC_ROLE_BRIDGE 2 /* ESP32 bridge                  */
#define GC_ROLE_SERVER 3 /* endpoint server               */
#define GC_ROLE_TEST   9 /* test harness / simulator      */

/* reject / close reasons */
#define GC_LR_NONE      0
#define GC_LR_VERSION   1 /* no common protocol version    */
#define GC_LR_BAD_HELLO 2
#define GC_LR_TIMEOUT   3 /* peer silent for dead_ms       */
#define GC_LR_RETRIES   4 /* data never acknowledged       */
#define GC_LR_LOCAL     5 /* closed by the local side      */
#define GC_LR_REMOTE    6 /* CLOSE received                */
#define GC_LR_TRANSPORT 7 /* underlying stream went away   */
#define GC_LR_PROTOCOL  8 /* protocol violation            */

/* link states */
#define GC_LS_IDLE       0 /* not started                         */
#define GC_LS_HELLO_SENT 1 /* initiator waiting for HELLO_ACK     */
#define GC_LS_WAIT_HELLO 2 /* acceptor waiting for HELLO          */
#define GC_LS_UP         3
#define GC_LS_DOWN       4 /* lost; may be resumed               */
#define GC_LS_CLOSED     5 /* closed for good                    */

/* events reported through gc_link_ops.on_state */
#define GC_LE_UP_FRESH   1 /* new session: peer state is unknown  */
#define GC_LE_UP_RESUMED 2 /* resumed: nothing was lost           */
#define GC_LE_DOWN       3 /* transport lost, resumable           */
#define GC_LE_CLOSED     4 /* closed / rejected, not resumable    */

/* gc_link_send() results */
#define GC_LINK_OK       0
#define GC_LINK_E_FULL   (-1)
#define GC_LINK_E_TOOBIG (-2)
#define GC_LINK_E_STATE  (-3)

typedef struct
{
    gc_u8 ver_min;
    gc_u8 ver_max;
    gc_u8 role;
    gc_u32 link_id;
    gc_u16 rx_next; /* next sequence number the sender expects */
    gc_u16 max_msg;
    gc_u8 window;
} gc_hello;

typedef struct
{
    /** write raw bytes to the transport (must accept all bytes or drop
     *  them; the link layer recovers from losses) */
    void (*write)(void *ctx, const gc_u8 *data, size_t len);
    /** a reliable, in-order message was received */
    void (*on_msg)(void *ctx, const gc_u8 *msg, size_t len);
    /** link state change, see GC_LE_* */
    void (*on_state)(void *ctx, int event, int reason);
    /** acceptor only: a HELLO arrived.  The owner decides whether this
     *  link (or another, detached one) answers it by calling
     *  gc_link_accept(). May be NULL: then the link accepts itself and
     *  resumes if the link id matches its previous one. */
    void (*on_hello)(void *ctx, const gc_hello *hello);
    /** optional back-pressure: return 0 if the owner cannot take this
     *  message right now. The message is treated as not received
     *  (not acknowledged) and the peer retransmits it later. */
    int (*can_accept)(void *ctx, const gc_u8 *msg, size_t len);
} gc_link_ops;

typedef struct
{
    gc_u16 rto_ms;     /* initial retransmission timeout          */
    gc_u16 rto_max_ms; /* back-off ceiling                         */
    gc_u16 ka_ms;      /* send PING after this much tx silence      */
    gc_u16 dead_ms;    /* declare peer dead after this rx silence   */
    gc_u16 hello_ms;   /* HELLO retry interval (initiator)          */
    gc_u8 role;
    gc_u8 initiator; /* 1: sends HELLO, 0: waits for HELLO        */
    gc_u8 window;    /* advertised receive window; 0 uses queue size */
} gc_link_cfg;

typedef struct
{
    gc_u16 len;
    gc_u16 seq;
    gc_u8 data[GC_LINK_MAX_MSG];
} gc_link_slot;

typedef struct
{
    gc_link_cfg cfg;
    gc_link_ops ops;
    void *ctx;
    gc_deframer rx;

    int state;
    gc_u8 version; /* negotiated protocol version */
    gc_u8 peer_role;
    gc_u32 link_id;    /* identity of this link session */
    int has_session;   /* link_id/sequence state are valid */
    gc_u8 peer_window; /* maximum unacknowledged messages peer accepts */

    /* transmit side */
    gc_link_slot q[GC_LINK_QUEUE];
    gc_u16 q_head; /* index of oldest queued message     */
    gc_u16 q_count;
    gc_u16 q_sent;  /* how many of q_count were transmitted */
    gc_u16 tx_next; /* sequence number of next new message  */
    gc_u32 t_last_tx;
    gc_u32 t_retx; /* deadline for retransmission          */
    gc_u16 rto_cur;
    gc_u16 retries;

    /* receive side */
    gc_u16 rx_next; /* next expected sequence number        */
    int ack_pending;
    gc_u32 t_last_rx;
    gc_u32 t_hello;

    /* statistics */
    gc_u32 st_tx_msgs;
    gc_u32 st_rx_msgs;
    gc_u32 st_retx;
    gc_u32 st_dup;
    gc_u32 st_ooo;
    gc_u32 st_resumes;
    gc_u32 st_backpressure;

    gc_u32 t_now; /* time of the input call in progress */
    gc_u8 scratch[GC_FRAME_MAX_BODY];
    gc_u8 txbuf[GC_FRAME_MAX_WIRE];
} gc_link;

void gc_link_default_cfg(gc_link_cfg *cfg, gc_u8 role, int initiator);

void gc_link_init(gc_link *l, const gc_link_cfg *cfg, const gc_link_ops *ops, void *ctx);

/** Initiator: (re)start the handshake on a freshly (re)connected
 *  transport.  If the link has a session it attempts to resume it. */
void gc_link_start(gc_link *l, gc_u32 link_id, gc_u32 now);

/** Acceptor: wait for a HELLO on a freshly connected transport. */
void gc_link_listen(gc_link *l, gc_u32 now);

/** Acceptor: answer a HELLO (usually from inside on_hello).  If resume
 *  is non-zero and the hello's link id matches this link's session the
 *  session continues, otherwise a fresh session is started. */
void gc_link_accept(gc_link *l, const gc_hello *h, int resume, gc_u32 now);

/** Acceptor: reject a HELLO (e.g. no common version). */
void gc_link_reject(gc_link *l, gc_u8 reason);

/** Feed bytes received from the transport. */
void gc_link_input(gc_link *l, const gc_u8 *data, size_t len, gc_u32 now);

/** Drive timers. Call at least every gc_link_next_timeout() ms. */
void gc_link_poll(gc_link *l, gc_u32 now);

/** Milliseconds until gc_link_poll() needs to be called (bounded). */
gc_u32 gc_link_next_timeout(const gc_link *l, gc_u32 now);

/** Queue a message for reliable delivery.  Allowed while UP and while
 *  DOWN with a resumable session (sent after resumption). */
int gc_link_send(gc_link *l, const gc_u8 *msg, size_t len, gc_u32 now);

/** Number of free queue slots. */
int gc_link_free_slots(const gc_link *l);

/** The transport went away (socket closed, BT disconnected). */
void gc_link_transport_lost(gc_link *l, gc_u32 now);

/** Graceful close: sends CLOSE (best effort) and forgets the session. */
void gc_link_close(gc_link *l, gc_u8 reason);

/** Forget session state so that the next start/accept is fresh. */
void gc_link_forget(gc_link *l);

int gc_link_is_up(const gc_link *l);

/** Parse a HELLO / HELLO_ACK payload (exposed for tests and owners). */
int gc_link_parse_hello(const gc_u8 *p, size_t len, gc_hello *h);

#ifdef __cplusplus
}
#endif

#endif /* GC_LINK_H */
