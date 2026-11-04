/** @file gc_msg.c
 *
 *  GCM message encoder / decoder.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "gc_msg.h"
#include <string.h>

static size_t gc_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

#define W_STR(w, s) gc_w_str8((w), (s), gc_strnlen((s), GC_MAX_NAME))
#define R_STR(r, s) gc_r_str8((r), (s), sizeof(s))

size_t gc_msg_encode_data(gc_u8 addr, gc_u8 channel, const gc_u8 *payload, size_t len, gc_u8 *buf, size_t cap)
{
    if (len > GC_MAX_DATA || cap < len + 3) {
        return 0;
    }
    buf[0] = GC_MT_DATA;
    buf[1] = addr;
    buf[2] = channel;
    if (len) {
        gc_memmove(buf + 3, payload, len);
    }
    return len + 3;
}

size_t gc_msg_encode(const gc_msg *m, gc_u8 *buf, size_t cap)
{
    gc_writer w;
    int i;

    if (m->type == GC_MT_DATA) {
        return gc_msg_encode_data(m->u.data.addr, m->u.data.channel, m->u.data.payload, m->u.data.len, buf, cap);
    }

    gc_w_init(&w, buf, cap);
    gc_w_u8(&w, m->type);

    switch (m->type) {
    case GC_MT_REGISTER:
        gc_w_u8(&w, m->u.reg.api);
        gc_w_u8(&w, m->u.reg.variant);
        gc_w_bytes(&w, m->u.reg.device_id, GC_DEVICE_ID_LEN);
        gc_w_u8(&w, m->u.reg.flags);
        W_STR(&w, m->u.reg.name);
        if (m->u.reg.key[0] != '\0') {
            W_STR(&w, m->u.reg.key);
        }
        break;
    case GC_MT_REGISTERED:
        gc_w_u8(&w, (gc_u8)m->u.registered.result);
        gc_w_u32(&w, m->u.registered.peer_id);
        gc_w_u8(&w, m->u.registered.proto);
        W_STR(&w, m->u.registered.server_name);
        break;
    case GC_MT_HOST_OPEN:
        gc_w_u32(&w, m->u.host_open.game_uid);
        gc_w_u8(&w, m->u.host_open.api);
        gc_w_u8(&w, m->u.host_open.max_players);
        gc_w_u8(&w, m->u.host_open.flags);
        W_STR(&w, m->u.host_open.name);
        break;
    case GC_MT_HOST_OPENED:
        gc_w_u8(&w, (gc_u8)m->u.host_opened.result);
        gc_w_u32(&w, m->u.host_opened.session_id);
        break;
    case GC_MT_HOST_CLOSE:
    case GC_MT_LEAVE:
    case GC_MT_SESSION_ENDED:
        gc_w_u8(&w, m->u.reason.reason);
        break;
    case GC_MT_SESSION_STATE:
        gc_w_u8(&w, m->u.session_state.state);
        break;
    case GC_MT_PEER_REQUEST:
    case GC_MT_PEER_JOINED:
        gc_w_u8(&w, m->u.peer.slot);
        gc_w_u8(&w, m->u.peer.rejoin);
        gc_w_bytes(&w, m->u.peer.device_id, GC_DEVICE_ID_LEN);
        W_STR(&w, m->u.peer.name);
        break;
    case GC_MT_PEER_ACCEPT:
        gc_w_u8(&w, m->u.peer_accept.slot);
        gc_w_u8(&w, m->u.peer_accept.accept);
        break;
    case GC_MT_PEER_LEFT:
    case GC_MT_KICK:
        gc_w_u8(&w, m->u.peer_left.slot);
        gc_w_u8(&w, m->u.peer_left.reason);
        break;
    case GC_MT_DISCOVER:
        gc_w_u32(&w, m->u.discover.game_uid);
        gc_w_u8(&w, m->u.discover.api);
        break;
    case GC_MT_SESSION_LIST:
        if (m->u.list.count > GC_MAX_LIST) {
            return 0;
        }
        gc_w_u8(&w, m->u.list.count);
        gc_w_u8(&w, m->u.list.more);
        for (i = 0; i < m->u.list.count; i++) {
            const gc_session_info *s = &m->u.list.s[i];
            gc_w_u32(&w, s->session_id);
            gc_w_u8(&w, s->players);
            gc_w_u8(&w, s->max_players);
            gc_w_u8(&w, s->state);
            W_STR(&w, s->host_name);
        }
        break;
    case GC_MT_JOIN:
        gc_w_u32(&w, m->u.join.session_id);
        gc_w_u32(&w, m->u.join.game_uid);
        gc_w_u8(&w, m->u.join.api);
        gc_w_u8(&w, m->u.join.rejoin);
        break;
    case GC_MT_JOIN_RESULT:
        gc_w_u8(&w, (gc_u8)m->u.join_result.result);
        gc_w_u32(&w, m->u.join_result.session_id);
        gc_w_u8(&w, m->u.join_result.slot);
        gc_w_u8(&w, m->u.join_result.max_players);
        W_STR(&w, m->u.join_result.host_name);
        break;
    case GC_MT_BRIDGE_STATUS:
        gc_w_u8(&w, m->u.bridge.wifi);
        gc_w_u8(&w, m->u.bridge.server);
        gc_w_u8(&w, (gc_u8)m->u.bridge.rssi);
        W_STR(&w, m->u.bridge.version);
        break;
    case GC_MT_BRIDGE_QUERY:
        break;
    case GC_MT_ERROR:
        gc_w_u8(&w, (gc_u8)m->u.error.code);
        gc_w_u8(&w, m->u.error.ref_type);
        W_STR(&w, m->u.error.text);
        break;
    default:
        return 0;
    }
    return w.overflow ? 0 : w.len;
}

int gc_msg_decode(const gc_u8 *buf, size_t len, gc_msg *m)
{
    gc_reader r;
    int i;
    const gc_u8 *p;

    if (len < 1) {
        return 0;
    }
    memset(m, 0, sizeof(*m));
    gc_r_init(&r, buf, len);
    m->type = gc_r_u8(&r);

    switch (m->type) {
    case GC_MT_REGISTER:
        m->u.reg.api = gc_r_u8(&r);
        m->u.reg.variant = gc_r_u8(&r);
        p = gc_r_bytes(&r, GC_DEVICE_ID_LEN);
        if (p) {
            memcpy(m->u.reg.device_id, p, GC_DEVICE_ID_LEN);
        }
        m->u.reg.flags = gc_r_u8(&r);
        R_STR(&r, m->u.reg.name);
        if (!r.error && gc_r_remaining(&r) > 0) {
            R_STR(&r, m->u.reg.key);
        }
        break;
    case GC_MT_REGISTERED:
        m->u.registered.result = (gc_i8)gc_r_u8(&r);
        m->u.registered.peer_id = gc_r_u32(&r);
        m->u.registered.proto = gc_r_u8(&r);
        R_STR(&r, m->u.registered.server_name);
        break;
    case GC_MT_HOST_OPEN:
        m->u.host_open.game_uid = gc_r_u32(&r);
        m->u.host_open.api = gc_r_u8(&r);
        m->u.host_open.max_players = gc_r_u8(&r);
        m->u.host_open.flags = gc_r_u8(&r);
        R_STR(&r, m->u.host_open.name);
        break;
    case GC_MT_HOST_OPENED:
        m->u.host_opened.result = (gc_i8)gc_r_u8(&r);
        m->u.host_opened.session_id = gc_r_u32(&r);
        break;
    case GC_MT_HOST_CLOSE:
    case GC_MT_LEAVE:
    case GC_MT_SESSION_ENDED:
        m->u.reason.reason = gc_r_u8(&r);
        break;
    case GC_MT_SESSION_STATE:
        m->u.session_state.state = gc_r_u8(&r);
        break;
    case GC_MT_PEER_REQUEST:
    case GC_MT_PEER_JOINED:
        m->u.peer.slot = gc_r_u8(&r);
        m->u.peer.rejoin = gc_r_u8(&r);
        p = gc_r_bytes(&r, GC_DEVICE_ID_LEN);
        if (p) {
            memcpy(m->u.peer.device_id, p, GC_DEVICE_ID_LEN);
        }
        R_STR(&r, m->u.peer.name);
        break;
    case GC_MT_PEER_ACCEPT:
        m->u.peer_accept.slot = gc_r_u8(&r);
        m->u.peer_accept.accept = gc_r_u8(&r);
        break;
    case GC_MT_PEER_LEFT:
    case GC_MT_KICK:
        m->u.peer_left.slot = gc_r_u8(&r);
        m->u.peer_left.reason = gc_r_u8(&r);
        break;
    case GC_MT_DISCOVER:
        m->u.discover.game_uid = gc_r_u32(&r);
        m->u.discover.api = gc_r_u8(&r);
        break;
    case GC_MT_SESSION_LIST:
        m->u.list.count = gc_r_u8(&r);
        m->u.list.more = gc_r_u8(&r);
        if (m->u.list.count > GC_MAX_LIST) {
            return 0;
        }
        for (i = 0; i < m->u.list.count; i++) {
            gc_session_info *s = &m->u.list.s[i];
            s->session_id = gc_r_u32(&r);
            s->players = gc_r_u8(&r);
            s->max_players = gc_r_u8(&r);
            s->state = gc_r_u8(&r);
            R_STR(&r, s->host_name);
        }
        break;
    case GC_MT_JOIN:
        m->u.join.session_id = gc_r_u32(&r);
        m->u.join.game_uid = gc_r_u32(&r);
        m->u.join.api = gc_r_u8(&r);
        m->u.join.rejoin = gc_r_u8(&r);
        break;
    case GC_MT_JOIN_RESULT:
        m->u.join_result.result = (gc_i8)gc_r_u8(&r);
        m->u.join_result.session_id = gc_r_u32(&r);
        m->u.join_result.slot = gc_r_u8(&r);
        m->u.join_result.max_players = gc_r_u8(&r);
        R_STR(&r, m->u.join_result.host_name);
        break;
    case GC_MT_DATA:
        m->u.data.addr = gc_r_u8(&r);
        m->u.data.channel = gc_r_u8(&r);
        if (r.error || gc_r_remaining(&r) > GC_MAX_DATA) {
            return 0;
        }
        m->u.data.len = (gc_u16)gc_r_remaining(&r);
        m->u.data.payload = buf + r.pos;
        return 1;
    case GC_MT_BRIDGE_STATUS:
        m->u.bridge.wifi = gc_r_u8(&r);
        m->u.bridge.server = gc_r_u8(&r);
        m->u.bridge.rssi = (gc_i8)gc_r_u8(&r);
        R_STR(&r, m->u.bridge.version);
        break;
    case GC_MT_BRIDGE_QUERY:
        break;
    case GC_MT_ERROR:
        m->u.error.code = (gc_i8)gc_r_u8(&r);
        m->u.error.ref_type = gc_r_u8(&r);
        R_STR(&r, m->u.error.text);
        break;
    default:
        return 0;
    }
    return r.error ? 0 : 1;
}

const char *gc_msg_name(gc_u8 type)
{
    switch (type) {
    case GC_MT_REGISTER:
        return "REGISTER";
    case GC_MT_REGISTERED:
        return "REGISTERED";
    case GC_MT_HOST_OPEN:
        return "HOST_OPEN";
    case GC_MT_HOST_OPENED:
        return "HOST_OPENED";
    case GC_MT_HOST_CLOSE:
        return "HOST_CLOSE";
    case GC_MT_SESSION_STATE:
        return "SESSION_STATE";
    case GC_MT_PEER_REQUEST:
        return "PEER_REQUEST";
    case GC_MT_PEER_ACCEPT:
        return "PEER_ACCEPT";
    case GC_MT_PEER_JOINED:
        return "PEER_JOINED";
    case GC_MT_PEER_LEFT:
        return "PEER_LEFT";
    case GC_MT_KICK:
        return "KICK";
    case GC_MT_DISCOVER:
        return "DISCOVER";
    case GC_MT_SESSION_LIST:
        return "SESSION_LIST";
    case GC_MT_JOIN:
        return "JOIN";
    case GC_MT_JOIN_RESULT:
        return "JOIN_RESULT";
    case GC_MT_LEAVE:
        return "LEAVE";
    case GC_MT_SESSION_ENDED:
        return "SESSION_ENDED";
    case GC_MT_DATA:
        return "DATA";
    case GC_MT_BRIDGE_STATUS:
        return "BRIDGE_STATUS";
    case GC_MT_BRIDGE_QUERY:
        return "BRIDGE_QUERY";
    case GC_MT_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}
