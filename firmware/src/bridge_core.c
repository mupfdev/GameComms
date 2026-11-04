/** @file bridge_core.c
 *
 *  ESP32 bridge core.  See bridge_core.h and docs/PROTOCOL.md section
 *  "Bridge behaviour".
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "bridge_core.h"
#include <stdio.h>
#include <string.h>

/* ---- logging ---- */

static void blog(bridge *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#include <stdarg.h>
static void blog(bridge *b, const char *fmt, ...)
{
    char line[160];
    va_list ap;
    if (!b->io.log) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    b->io.log(b->ctx, line);
}

/* ---- fifo ---- */

static int fifo_push(bridge_fifo *f, const gc_u8 *d, size_t n)
{
    bridge_slot *s;
    if (f->count >= BRIDGE_FIFO_LEN || n > GC_LINK_MAX_MSG) {
        return 0;
    }
    s = &f->s[(f->head + f->count) % BRIDGE_FIFO_LEN];
    s->len = (gc_u16)n;
    memcpy(s->data, d, n);
    f->count++;
    return 1;
}

static bridge_slot *fifo_peek(bridge_fifo *f)
{
    return f->count ? &f->s[f->head] : NULL;
}

static void fifo_pop(bridge_fifo *f)
{
    if (f->count) {
        f->head = (f->head + 1) % BRIDGE_FIFO_LEN;
        f->count--;
    }
}

static void fifo_clear(bridge_fifo *f)
{
    f->head = 0;
    f->count = 0;
}

/* move as much as possible from fifo into link */
static void fifo_drain(bridge *b, bridge_fifo *f, gc_link *l)
{
    bridge_slot *s;
    if (l->state != GC_LS_UP) {
        return;
    }
    while ((s = fifo_peek(f)) != NULL && gc_link_free_slots(l) > 0) {
        if (gc_link_send(l, s->data, s->len, b->now) != GC_LINK_OK) {
            break;
        }
        fifo_pop(f);
    }
}

/* ---- device side output ---- */

static void dev_send(bridge *b, const gc_msg *m)
{
    gc_u8 buf[GC_LINK_MAX_MSG];
    size_t n = gc_msg_encode(m, buf, sizeof(buf));
    if (!n) {
        return;
    }
    if (b->dev.state == GC_LS_UP && b->to_dev.count == 0 && gc_link_send(&b->dev, buf, n, b->now) == GC_LINK_OK) {
        return;
    }
    fifo_push(&b->to_dev, buf, n);
}

static void send_status(bridge *b)
{
    gc_msg m;
    if (b->dev.state != GC_LS_UP) {
        return;
    }
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_BRIDGE_STATUS;
    m.u.bridge.wifi = (gc_u8)(b->wifi_up ? 1 : 0);
    m.u.bridge.server = (gc_u8)b->srv_state;
    m.u.bridge.rssi = b->rssi;
    strcpy(m.u.bridge.version, "bridge " BRIDGE_VERSION);
    dev_send(b, &m);
}

static void set_srv_state(bridge *b, int st)
{
    if (b->srv_state != st) {
        b->srv_state = st;
        send_status(b);
    }
}

/* answer a device request locally because the server is unreachable */
static void reply_unavailable(bridge *b, gc_u8 req_type)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    switch (req_type) {
    case GC_MT_REGISTER:
        m.type = GC_MT_REGISTERED;
        m.u.registered.result = GC_R_UNAVAILABLE;
        break;
    case GC_MT_HOST_OPEN:
        m.type = GC_MT_HOST_OPENED;
        m.u.host_opened.result = GC_R_UNAVAILABLE;
        break;
    case GC_MT_JOIN:
        m.type = GC_MT_JOIN_RESULT;
        m.u.join_result.result = GC_R_UNAVAILABLE;
        m.u.join_result.slot = GC_SLOT_NONE;
        break;
    case GC_MT_DISCOVER:
        m.type = GC_MT_SESSION_LIST; /* empty list */
        break;
    default:
        m.type = GC_MT_ERROR;
        m.u.error.code = GC_R_UNAVAILABLE;
        m.u.error.ref_type = req_type;
        strcpy(m.u.error.text, "server unreachable");
        break;
    }
    dev_send(b, &m);
}

/* ---- server side output ---- */

static int srv_usable(bridge *b)
{
    /* up, or temporarily down with a resumable session */
    return b->srv.state == GC_LS_UP || (b->srv.has_session && b->srv_state != GC_BS_DOWN);
}

static void srv_queue(bridge *b, const gc_u8 *d, size_t n)
{
    if (b->srv.state == GC_LS_UP && b->to_srv.count == 0 && gc_link_send(&b->srv, d, n, b->now) == GC_LINK_OK) {
        return;
    }
    if (!fifo_push(&b->to_srv, d, n)) {
        blog(b, "server queue full, dropping %s", gc_msg_name(d[0]));
    }
}

/* ---- device link callbacks ---- */

static void dev_write_cb(void *ctx, const gc_u8 *d, size_t n)
{
    bridge *b = (bridge *)ctx;
    if (b->dev_connected && b->io.dev_write) {
        b->io.dev_write(b->ctx, d, n);
    }
}

static int dev_can_accept(void *ctx)
{
    bridge *b = (bridge *)ctx;
    return b->to_srv.count < BRIDGE_FIFO_LEN;
}

static void dev_msg_cb(void *ctx, const gc_u8 *d, size_t n)
{
    bridge *b = (bridge *)ctx;
    gc_msg m;
    gc_u8 buf[GC_LINK_MAX_MSG];

    b->st_dev_msgs++;
    if (n == 0) {
        return;
    }
    if (d[0] == GC_MT_BRIDGE_QUERY) {
        send_status(b);
        return;
    }
    if (d[0] == GC_MT_REGISTER) {
        /* fill in the device id from the Bluetooth address and add the
         * access key; remember the message for replay */
        if (!gc_msg_decode(d, n, &m)) {
            return;
        }
        if (m.u.reg.device_id[0] == 0 && m.u.reg.device_id[1] == 0 && m.u.reg.device_id[2] == 0 &&
            m.u.reg.device_id[3] == 0 && m.u.reg.device_id[4] == 0 && m.u.reg.device_id[5] == 0) {
            memcpy(m.u.reg.device_id, b->dev_addr, GC_DEVICE_ID_LEN);
        }
        strcpy(m.u.reg.key, b->cfg.access_key);
        n = gc_msg_encode(&m, buf, sizeof(buf));
        if (!n) {
            return;
        }
        memcpy(b->reg_msg, buf, n);
        b->reg_len = (gc_u16)n;
        b->registered = 0;
        d = buf;
        blog(b, "device '%s' registers (api %u, variant %u)", m.u.reg.name, m.u.reg.api, m.u.reg.variant);
    }
    if (!srv_usable(b)) {
        reply_unavailable(b, d[0]);
        return;
    }
    if (d[0] == GC_MT_REGISTER && b->srv.state != GC_LS_UP) {
        return; /* replayed automatically when the link comes up */
    }
    srv_queue(b, d, n);
}

static void dev_state_cb(void *ctx, int ev, int reason)
{
    bridge *b = (bridge *)ctx;
    switch (ev) {
    case GC_LE_UP_FRESH:
        blog(b, "device link up");
        fifo_clear(&b->to_dev);
        b->dev_lost_pending = 0;
        send_status(b);
        break;
    case GC_LE_UP_RESUMED:
        blog(b, "device link resumed");
        b->dev_lost_pending = 0;
        fifo_drain(b, &b->to_dev, &b->dev);
        break;
    case GC_LE_DOWN:
    case GC_LE_CLOSED:
        blog(b, "device link %s (%d)", ev == GC_LE_DOWN ? "down" : "closed", reason);
        if (!b->dev_lost_pending) {
            b->dev_lost_pending = 1;
            b->t_dev_lost = b->now;
        }
        break;
    default:
        break;
    }
}

static void dev_hello_cb(void *ctx, const gc_hello *h)
{
    bridge *b = (bridge *)ctx;
    /* a new link id means the DLL restarted: start a fresh link */
    gc_link_accept(&b->dev, h, 1, b->now);
}

/* ---- server link callbacks ---- */

static void srv_write_cb(void *ctx, const gc_u8 *d, size_t n)
{
    bridge *b = (bridge *)ctx;
    if (b->io.srv_write && b->srv_state != GC_BS_DOWN) {
        b->io.srv_write(b->ctx, d, n);
    }
}

static int srv_can_accept(void *ctx)
{
    bridge *b = (bridge *)ctx;
    return b->to_dev.count < BRIDGE_FIFO_LEN;
}

static void srv_msg_cb(void *ctx, const gc_u8 *d, size_t n)
{
    bridge *b = (bridge *)ctx;

    b->st_srv_msgs++;
    if (n == 0) {
        return;
    }
    if (d[0] == GC_MT_REGISTERED && n >= 2) {
        b->registered = ((gc_i8)d[1] == GC_R_OK);
    }
    if (b->dev.state == GC_LS_UP && b->to_dev.count == 0 && gc_link_send(&b->dev, d, n, b->now) == GC_LINK_OK) {
        return;
    }
    if (!fifo_push(&b->to_dev, d, n)) {
        blog(b, "device queue full, dropping %s", gc_msg_name(d[0])); /* cannot happen: can_accept */
    }
}

static void srv_state_cb(void *ctx, int ev, int reason)
{
    bridge *b = (bridge *)ctx;
    switch (ev) {
    case GC_LE_UP_FRESH:
        blog(b, "server link up (fresh)");
        b->backoff = b->cfg.reconnect_min_ms;
        set_srv_state(b, GC_BS_UP);
        b->registered = 0;
        /* everything the server knew about us is gone: replay the
         * registration; the DLL sees the unsolicited REGISTERED and
         * knows that any session state was lost */
        fifo_clear(&b->to_srv);
        if (b->reg_len && b->dev.state == GC_LS_UP) {
            gc_link_send(&b->srv, b->reg_msg, b->reg_len, b->now);
        }
        b->srv_ever_up = 1;
        break;
    case GC_LE_UP_RESUMED:
        blog(b, "server link resumed");
        b->backoff = b->cfg.reconnect_min_ms;
        set_srv_state(b, GC_BS_UP);
        fifo_drain(b, &b->to_srv, &b->srv);
        break;
    case GC_LE_DOWN:
        blog(b, "server link down (%d)", reason);
        if (b->io.srv_disconnect) {
            b->io.srv_disconnect(b->ctx);
        }
        set_srv_state(b, GC_BS_CONNECTING);
        b->t_reconnect = b->now + b->backoff;
        break;
    case GC_LE_CLOSED:
        blog(b, "server link closed (%d)", reason);
        if (b->io.srv_disconnect) {
            b->io.srv_disconnect(b->ctx);
        }
        fifo_clear(&b->to_srv);
        set_srv_state(b, GC_BS_CONNECTING);
        b->t_reconnect = b->now + b->backoff;
        break;
    default:
        break;
    }
}

/* ---- public ---- */

void bridge_default_cfg(bridge_cfg *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->server_link_id = 0x42524447; /* overwrite with a random value */
    cfg->device_grace_ms = 15000;
    cfg->reconnect_min_ms = 1000;
    cfg->reconnect_max_ms = 15000;
}

void bridge_init(bridge *b, const bridge_cfg *cfg, const bridge_io *io, void *ctx)
{
    gc_link_cfg lc;
    gc_link_ops ops;

    memset(b, 0, sizeof(*b));
    b->cfg = *cfg;
    b->io = *io;
    b->ctx = ctx;
    b->backoff = cfg->reconnect_min_ms;
    b->srv_state = GC_BS_DOWN;

    gc_link_default_cfg(&lc, GC_ROLE_BRIDGE, 0);
    ops.write = dev_write_cb;
    ops.on_msg = dev_msg_cb;
    ops.on_state = dev_state_cb;
    ops.on_hello = dev_hello_cb;
    ops.can_accept = dev_can_accept;
    gc_link_init(&b->dev, &lc, &ops, b);

    gc_link_default_cfg(&lc, GC_ROLE_BRIDGE, 1);
    lc.ka_ms = 5000;
    lc.dead_ms = 20000;
    lc.rto_ms = 1000;
    lc.rto_max_ms = 5000;
    lc.hello_ms = 2000;
    ops.write = srv_write_cb;
    ops.on_msg = srv_msg_cb;
    ops.on_state = srv_state_cb;
    ops.on_hello = NULL;
    ops.can_accept = srv_can_accept;
    gc_link_init(&b->srv, &lc, &ops, b);
}

void bridge_dev_connected(bridge *b, const gc_u8 *bt_addr, gc_u32 now)
{
    b->now = now;
    b->dev_connected = 1;
    if (bt_addr) {
        memcpy(b->dev_addr, bt_addr, GC_DEVICE_ID_LEN);
    }
    blog(b, "device connected %02X:%02X:%02X:%02X:%02X:%02X", b->dev_addr[0], b->dev_addr[1], b->dev_addr[2],
         b->dev_addr[3], b->dev_addr[4], b->dev_addr[5]);
    gc_link_listen(&b->dev, now);
}

void bridge_dev_input(bridge *b, const gc_u8 *data, size_t len, gc_u32 now)
{
    b->now = now;
    gc_link_input(&b->dev, data, len, now);
    fifo_drain(b, &b->to_srv, &b->srv);
}

void bridge_dev_disconnected(bridge *b, gc_u32 now)
{
    b->now = now;
    b->dev_connected = 0;
    blog(b, "device disconnected");
    gc_link_transport_lost(&b->dev, now);
    if (!b->dev_lost_pending) {
        b->dev_lost_pending = 1;
        b->t_dev_lost = now;
    }
}

void bridge_set_wifi(bridge *b, int up, gc_i8 rssi, gc_u32 now)
{
    b->now = now;
    b->rssi = rssi;
    if (up == b->wifi_up) {
        return;
    }
    b->wifi_up = up;
    blog(b, "wifi %s", up ? "up" : "down");
    if (up) {
        b->t_reconnect = now;
        set_srv_state(b, GC_BS_CONNECTING);
    } else {
        if (b->io.srv_disconnect) {
            b->io.srv_disconnect(b->ctx);
        }
        gc_link_transport_lost(&b->srv, now);
        set_srv_state(b, b->srv.has_session ? GC_BS_CONNECTING : GC_BS_DOWN);
    }
    send_status(b);
}

void bridge_srv_connected(bridge *b, gc_u32 now)
{
    b->now = now;
    blog(b, "tcp connected");
    gc_link_start(&b->srv, b->cfg.server_link_id, now);
}

void bridge_srv_input(bridge *b, const gc_u8 *data, size_t len, gc_u32 now)
{
    b->now = now;
    gc_link_input(&b->srv, data, len, now);
    fifo_drain(b, &b->to_dev, &b->dev);
}

void bridge_srv_disconnected(bridge *b, gc_u32 now)
{
    b->now = now;
    gc_link_transport_lost(&b->srv, now);
    if (b->srv_state != GC_BS_DOWN || b->wifi_up) {
        set_srv_state(b, b->wifi_up ? GC_BS_CONNECTING : GC_BS_DOWN);
    }
    b->t_reconnect = now + b->backoff;
    b->backoff = b->backoff * 2 > b->cfg.reconnect_max_ms ? b->cfg.reconnect_max_ms : b->backoff * 2;
}

void bridge_poll(bridge *b, gc_u32 now)
{
    b->now = now;
    gc_link_poll(&b->dev, now);
    gc_link_poll(&b->srv, now);

    /* (re)connect to the server */
    if (b->wifi_up && b->srv.state != GC_LS_UP && b->srv.state != GC_LS_HELLO_SENT &&
        !GC_TIME_BEFORE(now, b->t_reconnect)) {
        b->t_reconnect = now + b->backoff;
        b->backoff = b->backoff * 2 > b->cfg.reconnect_max_ms ? b->cfg.reconnect_max_ms : b->backoff * 2;
        set_srv_state(b, GC_BS_CONNECTING);
        if (b->io.srv_connect) {
            b->io.srv_connect(b->ctx);
        }
    }
    /* a resumable server session that never comes back is abandoned */
    if (b->srv_state == GC_BS_CONNECTING && !b->wifi_up && b->srv.has_session &&
        (gc_u32)(now - b->srv.t_last_rx) > 60000) {
        gc_link_forget(&b->srv);
        fifo_clear(&b->to_srv);
        set_srv_state(b, GC_BS_DOWN);
    }

    /* device gone for good: release its server side state */
    if (b->dev_lost_pending && b->dev.state != GC_LS_UP && (gc_u32)(now - b->t_dev_lost) >= b->cfg.device_grace_ms) {
        b->dev_lost_pending = 0;
        blog(b, "device did not come back, leaving session");
        fifo_clear(&b->to_dev);
        gc_link_forget(&b->dev);
        if (b->reg_len && b->srv.state == GC_LS_UP) {
            gc_msg m;
            gc_u8 buf[8];
            size_t n;
            memset(&m, 0, sizeof(m));
            m.type = GC_MT_LEAVE;
            m.u.reason.reason = GC_LEFT_LOST;
            n = gc_msg_encode(&m, buf, sizeof(buf));
            srv_queue(b, buf, n);
        }
        b->reg_len = 0;
    }

    fifo_drain(b, &b->to_srv, &b->srv);
    fifo_drain(b, &b->to_dev, &b->dev);
}

gc_u32 bridge_next_timeout(bridge *b, gc_u32 now)
{
    gc_u32 a = gc_link_next_timeout(&b->dev, now);
    gc_u32 c = gc_link_next_timeout(&b->srv, now);
    return a < c ? a : c;
}
