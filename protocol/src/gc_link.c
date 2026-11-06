/** @file gc_link.c
 *
 *  GCL - GameComms Link layer implementation.  See gc_link.h and
 *  docs/PROTOCOL.md section "Link layer".
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "gc_link.h"
#include <string.h>

#define SEQ_DIFF(a, b) ((gc_i16)((gc_u16)(a) - (gc_u16)(b)))

static gc_u8 link_peer_window(gc_u8 window)
{
    return window ? window : 1;
}

void gc_link_default_cfg(gc_link_cfg *cfg, gc_u8 role, int initiator)
{
    cfg->rto_ms = 400;
    cfg->rto_max_ms = 3000;
    cfg->ka_ms = 2000;
    cfg->dead_ms = 10000;
    cfg->hello_ms = 1000;
    cfg->role = role;
    cfg->initiator = (gc_u8)(initiator ? 1 : 0);
    cfg->window = (gc_u8)GC_LINK_QUEUE;
}

static void link_frame_cb(void *ctx, const gc_u8 *body, size_t len);

void gc_link_init(gc_link *l, const gc_link_cfg *cfg, const gc_link_ops *ops, void *ctx)
{
    memset(l, 0, sizeof(*l));
    l->cfg = *cfg;
    if (!l->cfg.window) {
        l->cfg.window = (gc_u8)GC_LINK_QUEUE;
    }
    l->ops = *ops;
    l->ctx = ctx;
    l->state = GC_LS_IDLE;
    l->rto_cur = cfg->rto_ms;
    gc_deframer_init(&l->rx, link_frame_cb, l);
}

int gc_link_is_up(const gc_link *l)
{
    return l->state == GC_LS_UP;
}

int gc_link_free_slots(const gc_link *l)
{
    return GC_LINK_QUEUE - l->q_count;
}

/* ------------------------------------------------------------------ */
/* low level transmit                                                  */
/* ------------------------------------------------------------------ */

static void link_tx_frame(gc_link *l, gc_u8 type, gc_u16 seq, const gc_u8 *payload, size_t plen, gc_u32 now)
{
    gc_u8 *body = l->scratch;
    size_t n;

    if (plen > GC_LINK_MAX_MSG) {
        return;
    }
    body[0] = type;
    body[1] = 0x01; /* ack field valid */
    body[2] = (gc_u8)(seq >> 8);
    body[3] = (gc_u8)(seq & 0xFF);
    body[4] = (gc_u8)(l->rx_next >> 8);
    body[5] = (gc_u8)(l->rx_next & 0xFF);
    if (plen) {
        gc_memmove(body + GC_LINK_HDR_LEN, payload, plen);
    }
    n = gc_frame_encode(body, GC_LINK_HDR_LEN + plen, l->txbuf, sizeof(l->txbuf));
    if (n && l->ops.write) {
        l->ops.write(l->ctx, l->txbuf, n);
    }
    l->t_last_tx = now;
    l->ack_pending = 0; /* every frame carries the cumulative ack */
}

static void link_tx_hello(gc_link *l, gc_u8 type, gc_u8 vmin, gc_u8 vmax, int resumed, gc_u32 now)
{
    gc_u8 p[16];
    gc_writer w;

    gc_w_init(&w, p, sizeof(p));
    gc_w_u8(&w, GC_LINK_MAGIC_0);
    gc_w_u8(&w, GC_LINK_MAGIC_1);
    gc_w_u8(&w, vmin);
    gc_w_u8(&w, vmax);
    gc_w_u8(&w, l->cfg.role);
    gc_w_u32(&w, l->link_id);
    gc_w_u16(&w, l->rx_next);
    gc_w_u16(&w, (gc_u16)GC_LINK_MAX_MSG);
    gc_w_u8(&w, l->cfg.window);
    if (type == GC_FT_HELLO_ACK) {
        gc_w_u8(&w, (gc_u8)(resumed ? 1 : 0));
    }
    link_tx_frame(l, type, 0, p, w.len, now);
}

static gc_link_slot *link_slot(gc_link *l, gc_u16 i)
{
    return &l->q[(l->q_head + i) % GC_LINK_QUEUE];
}

/* transmit everything queued but not yet sent */
static void link_flush(gc_link *l, gc_u32 now)
{
    if (l->state != GC_LS_UP) {
        return;
    }
    while (l->q_sent < l->q_count && l->q_sent < l->peer_window) {
        gc_link_slot *s = link_slot(l, l->q_sent);
        link_tx_frame(l, GC_FT_DATA, s->seq, s->data, s->len, now);
        if (l->q_sent == 0) {
            l->t_retx = now + l->rto_cur;
        }
        l->q_sent++;
        l->st_tx_msgs++;
    }
}

/* go-back-N: resend every unacknowledged message */
static void link_retransmit(gc_link *l, gc_u32 now)
{
    gc_u16 i;

    for (i = 0; i < l->q_sent && i < l->peer_window; i++) {
        gc_link_slot *s = link_slot(l, i);
        link_tx_frame(l, GC_FT_DATA, s->seq, s->data, s->len, now);
        l->st_retx++;
    }
    l->t_retx = now + l->rto_cur;
}

/* ------------------------------------------------------------------ */
/* state handling                                                      */
/* ------------------------------------------------------------------ */

static void link_notify(gc_link *l, int ev, int reason)
{
    if (l->ops.on_state) {
        l->ops.on_state(l->ctx, ev, reason);
    }
}

static void link_reset_session(gc_link *l)
{
    l->q_head = 0;
    l->q_count = 0;
    l->q_sent = 0;
    l->tx_next = 0;
    l->rx_next = 0;
    l->retries = 0;
    l->rto_cur = l->cfg.rto_ms;
    l->ack_pending = 0;
    l->peer_window = 0;
}

void gc_link_forget(gc_link *l)
{
    link_reset_session(l);
    l->has_session = 0;
    l->link_id = 0;
}

static void link_go_down(gc_link *l, int reason, gc_u32 now)
{
    (void)now;
    if (l->state == GC_LS_DOWN || l->state == GC_LS_CLOSED || l->state == GC_LS_IDLE) {
        return;
    }
    l->state = GC_LS_DOWN;
    l->q_sent = 0; /* everything is retransmitted after resumption */
    gc_deframer_reset(&l->rx);
    link_notify(l, GC_LE_DOWN, reason);
}

static void link_go_closed(gc_link *l, int reason)
{
    int was = l->state;

    l->state = GC_LS_CLOSED;
    gc_link_forget(l);
    gc_deframer_reset(&l->rx);
    if (was != GC_LS_CLOSED && was != GC_LS_IDLE) {
        link_notify(l, GC_LE_CLOSED, reason);
    }
}

/* the peer acknowledged everything below 'ack' */
static void link_process_ack(gc_link *l, gc_u16 ack, gc_u32 now)
{
    int progressed = 0;

    while (l->q_sent > 0) {
        gc_link_slot *s = link_slot(l, 0);
        if (SEQ_DIFF(ack, s->seq) <= 0) {
            break;
        }
        l->q_head = (gc_u16)((l->q_head + 1) % GC_LINK_QUEUE);
        l->q_count--;
        l->q_sent--;
        progressed = 1;
    }
    if (progressed) {
        l->retries = 0;
        l->rto_cur = l->cfg.rto_ms;
        l->t_retx = now + l->rto_cur;
    }
}

/* after resumption: drop what the peer already has, resend the rest */
static void link_resume_tx(gc_link *l, gc_u16 peer_rx_next, gc_u32 now)
{
    l->q_sent = l->q_count; /* treat all as outstanding for ack processing */
    link_process_ack(l, peer_rx_next, now);
    l->q_sent = 0;
    l->retries = 0;
    l->rto_cur = l->cfg.rto_ms;
    link_flush(l, now);
}

int gc_link_parse_hello(const gc_u8 *p, size_t len, gc_hello *h)
{
    gc_reader r;

    gc_r_init(&r, p, len);
    if (gc_r_u8(&r) != GC_LINK_MAGIC_0 || gc_r_u8(&r) != GC_LINK_MAGIC_1) {
        return 0;
    }
    h->ver_min = gc_r_u8(&r);
    h->ver_max = gc_r_u8(&r);
    h->role = gc_r_u8(&r);
    h->link_id = gc_r_u32(&r);
    h->rx_next = gc_r_u16(&r);
    h->max_msg = gc_r_u16(&r);
    h->window = gc_r_u8(&r);
    if (r.error || h->ver_min > h->ver_max) {
        return 0;
    }
    return 1;
}

void gc_link_start(gc_link *l, gc_u32 link_id, gc_u32 now)
{
    if (!l->has_session || link_id != l->link_id) {
        link_reset_session(l);
        l->link_id = link_id;
        l->has_session = 0;
    }
    gc_deframer_reset(&l->rx);
    l->state = GC_LS_HELLO_SENT;
    l->t_last_rx = now;
    l->t_hello = now + l->cfg.hello_ms;
    link_tx_hello(l, GC_FT_HELLO, GC_PROTO_VERSION_MIN, GC_PROTO_VERSION_MAX, 0, now);
}

void gc_link_listen(gc_link *l, gc_u32 now)
{
    gc_deframer_reset(&l->rx);
    l->state = GC_LS_WAIT_HELLO;
    l->t_last_rx = now;
}

void gc_link_accept(gc_link *l, const gc_hello *h, int resume, gc_u32 now)
{
    gc_u8 ver = h->ver_max < GC_PROTO_VERSION_MAX ? h->ver_max : GC_PROTO_VERSION_MAX;
    int resumed;

    if (ver < GC_PROTO_VERSION_MIN || ver < h->ver_min) {
        gc_link_reject(l, GC_LR_VERSION);
        return;
    }
    resumed = resume && l->has_session && h->link_id == l->link_id;
    if (!resumed) {
        link_reset_session(l);
        l->link_id = h->link_id;
        l->has_session = 1;
    }
    l->version = ver;
    l->peer_role = h->role;
    l->peer_window = link_peer_window(h->window);
    l->state = GC_LS_UP;
    l->t_last_rx = now;
    link_tx_hello(l, GC_FT_HELLO_ACK, ver, ver, resumed, now);
    if (resumed) {
        l->st_resumes++;
        link_notify(l, GC_LE_UP_RESUMED, 0);
        link_resume_tx(l, h->rx_next, now);
    } else {
        link_notify(l, GC_LE_UP_FRESH, 0);
        link_flush(l, now);
    }
}

void gc_link_reject(gc_link *l, gc_u8 reason)
{
    int was_established = l->state == GC_LS_UP || l->has_session;

    link_tx_frame(l, GC_FT_REJECT, 0, &reason, 1, l->t_last_tx);
    l->state = GC_LS_CLOSED;
    gc_link_forget(l);
    gc_deframer_reset(&l->rx);
    /* An established session that ends here must be reported; otherwise
     * the owner keeps state for a link that silently stopped existing. */
    if (was_established) {
        link_notify(l, GC_LE_CLOSED, reason);
    }
}

void gc_link_transport_lost(gc_link *l, gc_u32 now)
{
    if (l->state == GC_LS_UP) {
        link_go_down(l, GC_LR_TRANSPORT, now);
    } else if (l->state == GC_LS_HELLO_SENT || l->state == GC_LS_WAIT_HELLO) {
        l->state = l->has_session ? GC_LS_DOWN : GC_LS_IDLE;
    }
}

void gc_link_close(gc_link *l, gc_u8 reason)
{
    if (l->state == GC_LS_UP) {
        link_tx_frame(l, GC_FT_CLOSE, 0, &reason, 1, l->t_last_tx);
    }
    link_go_closed(l, GC_LR_LOCAL);
}

int gc_link_send(gc_link *l, const gc_u8 *msg, size_t len, gc_u32 now)
{
    gc_link_slot *s;

    if (len > GC_LINK_MAX_MSG) {
        return GC_LINK_E_TOOBIG;
    }
    if (!(l->state == GC_LS_UP || (l->state == GC_LS_DOWN && l->has_session) ||
          (l->state == GC_LS_HELLO_SENT && l->has_session))) {
        return GC_LINK_E_STATE;
    }
    if (l->q_count >= GC_LINK_QUEUE) {
        return GC_LINK_E_FULL;
    }
    s = link_slot(l, l->q_count);
    s->len = (gc_u16)len;
    s->seq = l->tx_next++;
    memcpy(s->data, msg, len);
    l->q_count++;
    link_flush(l, now);
    return GC_LINK_OK;
}

/* ------------------------------------------------------------------ */
/* receive                                                             */
/* ------------------------------------------------------------------ */

static void link_handle_frame(gc_link *l, const gc_u8 *body, size_t len, gc_u32 now)
{
    gc_u8 type;
    gc_u16 seq;
    gc_u16 ack;
    const gc_u8 *p;
    size_t plen;
    gc_hello h;

    if (len < GC_LINK_HDR_LEN) {
        return;
    }
    /* A frame earlier in the same input buffer may have closed or dropped
     * the link (CLOSE, REJECT, owner callback).  Nothing after that may be
     * acted upon: the owner may already have released its state. */
    if (l->state == GC_LS_CLOSED || l->state == GC_LS_DOWN || l->state == GC_LS_IDLE) {
        return;
    }
    type = body[0];
    seq = (gc_u16)((body[2] << 8) | body[3]);
    ack = (gc_u16)((body[4] << 8) | body[5]);
    p = body + GC_LINK_HDR_LEN;
    plen = len - GC_LINK_HDR_LEN;
    l->t_last_rx = now;

    switch (type) {
    case GC_FT_HELLO:
        if (l->cfg.initiator) {
            return; /* two initiators: ignore */
        }
        if (!gc_link_parse_hello(p, plen, &h)) {
            gc_link_reject(l, GC_LR_BAD_HELLO);
            return;
        }
        if (l->state == GC_LS_UP && h.link_id == l->link_id) {
            /* duplicate HELLO (our ACK was lost): answer again */
            gc_link_accept(l, &h, 1, now);
            return;
        }
        if (l->ops.on_hello) {
            l->ops.on_hello(l->ctx, &h);
        } else {
            gc_link_accept(l, &h, 1, now);
        }
        return;

    case GC_FT_HELLO_ACK:
    {
        int resumed;
        if (!l->cfg.initiator || l->state != GC_LS_HELLO_SENT) {
            return;
        }
        if (!gc_link_parse_hello(p, plen, &h) || plen < 15) {
            link_go_closed(l, GC_LR_BAD_HELLO);
            return;
        }
        if (h.ver_max < GC_PROTO_VERSION_MIN || h.ver_max > GC_PROTO_VERSION_MAX) {
            link_go_closed(l, GC_LR_VERSION);
            return;
        }
        resumed = p[14] && l->has_session;
        l->version = h.ver_max;
        l->peer_role = h.role;
        l->state = GC_LS_UP;
        if (resumed) {
            l->peer_window = link_peer_window(h.window);
            l->st_resumes++;
            link_notify(l, GC_LE_UP_RESUMED, 0);
            link_resume_tx(l, h.rx_next, now);
        } else {
            link_reset_session(l);
            l->has_session = 1;
            l->peer_window = link_peer_window(h.window);
            link_notify(l, GC_LE_UP_FRESH, 0);
            link_flush(l, now);
        }
        return;
    }

    case GC_FT_REJECT:
        link_go_closed(l, plen ? p[0] : GC_LR_REMOTE);
        return;

    case GC_FT_CLOSE:
        link_go_closed(l, GC_LR_REMOTE);
        return;

    default:
        break;
    }

    if (l->state != GC_LS_UP) {
        return; /* data before handshake: ignore */
    }

    link_process_ack(l, ack, now);
    link_flush(l, now); /* an ACK may have opened peer-window slots */

    switch (type) {
    case GC_FT_DATA:
    {
        gc_i16 d = SEQ_DIFF(seq, l->rx_next);
        if (d == 0 && l->ops.can_accept && !l->ops.can_accept(l->ctx, p, plen)) {
            l->st_backpressure++; /* not acknowledged: peer retries */
        } else if (d == 0) {
            l->rx_next++;
            l->st_rx_msgs++;
            l->ack_pending = 1;
            if (l->ops.on_msg) {
                l->ops.on_msg(l->ctx, p, plen);
            }
        } else if (d < 0) {
            l->st_dup++;
            l->ack_pending = 1; /* re-acknowledge duplicates */
        } else {
            l->st_ooo++; /* gap: wait for retransmission */
            l->ack_pending = 1;
        }
        break;
    }
    case GC_FT_PING:
        link_tx_frame(l, GC_FT_PONG, 0, p, plen, now);
        break;
    case GC_FT_ACK:
    case GC_FT_PONG:
    default:
        break;
    }
}

/* NOTE: no writable static data anywhere in this library - Symbian 6.x
 * DLLs (EKA1) cannot have any.  The time of the current input call is
 * therefore kept in the link object itself. */
static void link_frame_cb(void *ctx, const gc_u8 *body, size_t len)
{
    gc_link *l = (gc_link *)ctx;
    link_handle_frame(l, body, len, l->t_now);
}

void gc_link_input(gc_link *l, const gc_u8 *data, size_t len, gc_u32 now)
{
    if (l->state == GC_LS_IDLE || l->state == GC_LS_CLOSED || l->state == GC_LS_DOWN) {
        if (!(l->state == GC_LS_IDLE && !l->cfg.initiator)) {
            return;
        }
        l->state = GC_LS_WAIT_HELLO; /* acceptor implicitly listening */
    }
    l->t_now = now;
    gc_deframer_feed(&l->rx, data, len);
    if (l->ack_pending && l->state == GC_LS_UP) {
        link_tx_frame(l, GC_FT_ACK, 0, NULL, 0, now);
    }
}

/* ------------------------------------------------------------------ */
/* timers                                                              */
/* ------------------------------------------------------------------ */

void gc_link_poll(gc_link *l, gc_u32 now)
{
    switch (l->state) {
    case GC_LS_HELLO_SENT:
        if (!GC_TIME_BEFORE(now, l->t_hello)) {
            if ((gc_u32)(now - l->t_last_rx) >= l->cfg.dead_ms) {
                gc_link_transport_lost(l, now);
                link_notify(l, GC_LE_DOWN, GC_LR_TIMEOUT);
                return;
            }
            l->t_hello = now + l->cfg.hello_ms;
            link_tx_hello(l, GC_FT_HELLO, GC_PROTO_VERSION_MIN, GC_PROTO_VERSION_MAX, 0, now);
        }
        return;

    case GC_LS_UP:
        if ((gc_u32)(now - l->t_last_rx) >= l->cfg.dead_ms) {
            link_go_down(l, GC_LR_TIMEOUT, now);
            return;
        }
        if (l->q_sent > 0 && !GC_TIME_BEFORE(now, l->t_retx)) {
            l->retries++;
            if ((gc_u32)l->retries * l->rto_cur > (gc_u32)l->cfg.dead_ms * 2) {
                link_go_down(l, GC_LR_RETRIES, now);
                return;
            }
            l->rto_cur = (gc_u16)(l->rto_cur * 2 > l->cfg.rto_max_ms ? l->cfg.rto_max_ms : l->rto_cur * 2);
            link_retransmit(l, now);
        }
        if (l->ack_pending) {
            link_tx_frame(l, GC_FT_ACK, 0, NULL, 0, now);
        }
        if ((gc_u32)(now - l->t_last_tx) >= l->cfg.ka_ms) {
            gc_u8 nonce[4];
            nonce[0] = (gc_u8)(now >> 24);
            nonce[1] = (gc_u8)(now >> 16);
            nonce[2] = (gc_u8)(now >> 8);
            nonce[3] = (gc_u8)now;
            link_tx_frame(l, GC_FT_PING, 0, nonce, 4, now);
        }
        return;

    default:
        return;
    }
}

gc_u32 gc_link_next_timeout(const gc_link *l, gc_u32 now)
{
    gc_u32 t = 1000;
    gc_u32 d;

    if (l->state == GC_LS_HELLO_SENT) {
        d = GC_TIME_BEFORE(now, l->t_hello) ? l->t_hello - now : 0;
        return d < t ? d : t;
    }
    if (l->state != GC_LS_UP) {
        return t;
    }
    if (l->q_sent > 0) {
        d = GC_TIME_BEFORE(now, l->t_retx) ? l->t_retx - now : 0;
        if (d < t) {
            t = d;
        }
    }
    d = (gc_u32)(now - l->t_last_tx) >= l->cfg.ka_ms ? 0 : l->cfg.ka_ms - (now - l->t_last_tx);
    if (d < t) {
        t = d;
    }
    return t;
}
