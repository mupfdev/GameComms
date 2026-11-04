/** @file gcs_core.c
 *
 *  GameComms endpoint server - session manager core.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "gcs_core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* member states */
#define M_EMPTY    0
#define M_PENDING  1 /* waiting for host confirmation */
#define M_ACTIVE   2
#define M_RESERVED 3 /* lost / temporarily disconnected, may rejoin */

typedef struct
{
    int state;
    gcs_peer *peer;
    gcs_peer *reserved_owner;
    gc_u8 device_id[GC_DEVICE_ID_LEN];
    char name[GC_MAX_NAME + 1];
    gc_u32 t; /* pending since / reserved since */
} gcs_member;

struct gcs_session
{
    gc_u32 id;
    gc_u32 game_uid;
    gc_u8 api;
    gc_u8 max_players;
    gc_u8 flags;
    gc_u8 state;
    char name[GC_MAX_NAME + 1];
    gcs_member m[GC_MAX_PLAYERS];
    gc_u32 created;
    gc_u32 seq; /* creation order, for stable listing */
    int dead;
};

typedef struct gcs_qmsg
{
    struct gcs_qmsg *next;
    size_t len;
    gc_u8 data[1];
} gcs_qmsg;

struct gcs_peer
{
    gcs_core *core;
    gcs_conn *conn; /* NULL while detached */
    gc_link link;
    int registered;
    gc_u32 peer_id;
    gc_u8 api;
    gc_u8 variant;
    gc_u8 device_id[GC_DEVICE_ID_LEN];
    char name[GC_MAX_NAME + 1];
    gc_u32 t_detached;
    gcs_session *session;
    gc_u8 slot;
    gcs_qmsg *q_head;
    gcs_qmsg *q_tail;
    int q_count;
    int dead; /* scheduled for deletion */
};

struct gcs_core
{
    gcs_config cfg;
    gcs_io io;
    void *io_ctx;
    gcs_peer **peers;
    int n_peers;
    gcs_session **sessions;
    int n_sessions;
    gc_u32 rng;
    gc_u32 next_peer_id;
    gc_u32 next_seq;
    gc_u32 now;
    gcs_stats st;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void logf_(gcs_core *c, int level, const char *fmt, ...)
{
    char line[512];
    va_list ap;

    if (level > c->cfg.log_level || c->io.log == NULL) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    c->io.log(c->io_ctx, level, line);
}

#define LOG_E(...) logf_(core, 0, __VA_ARGS__)
#define LOG_W(...) logf_(core, 1, __VA_ARGS__)
#define LOG_I(...) logf_(core, 2, __VA_ARGS__)
#define LOG_D(...) logf_(core, 3, __VA_ARGS__)

static gc_u32 rnd(gcs_core *c)
{
    /* xorshift32; only used for identifiers, not for security */
    gc_u32 x = c->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return x;
}

static void sanitize_name(char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7F) {
            *s = '?';
        }
    }
}

static int id_is_zero(const gc_u8 *id)
{
    int i;
    for (i = 0; i < GC_DEVICE_ID_LEN; i++) {
        if (id[i]) {
            return 0;
        }
    }
    return 1;
}

static const char *pname(const gcs_peer *p)
{
    return p->registered ? p->name : "(unregistered)";
}

void gcs_default_config(gcs_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->server_name, "GameComms");
    cfg->max_peers = 512;
    cfg->max_sessions = 256;
    cfg->link_grace_ms = 20000;
    cfg->reserve_ms = 180000;
    cfg->pending_ms = 60000;
    cfg->max_outq = 256;
    cfg->log_level = 2;
}

/* ------------------------------------------------------------------ */
/* sending                                                             */
/* ------------------------------------------------------------------ */

static void peer_flush(gcs_peer *p)
{
    while (p->q_head && gc_link_free_slots(&p->link) > 0) {
        gcs_qmsg *m = p->q_head;
        if (gc_link_send(&p->link, m->data, m->len, p->core->now) != GC_LINK_OK) {
            break;
        }
        p->q_head = m->next;
        if (!p->q_head) {
            p->q_tail = NULL;
        }
        p->q_count--;
        free(m);
    }
}

static void peer_kill(gcs_peer *p, int reason);

static void peer_send_raw(gcs_peer *p, const gc_u8 *data, size_t len)
{
    gcs_core *core = p->core;
    gcs_qmsg *m;

    if (p->dead) {
        return;
    }
    if (!p->q_head && gc_link_free_slots(&p->link) > 0 &&
        gc_link_send(&p->link, data, len, core->now) == GC_LINK_OK) {
        return;
    }
    if (p->q_count >= core->cfg.max_outq) {
        LOG_W("peer %u '%s': outbound queue overflow, dropping peer", p->peer_id, pname(p));
        core->st.msgs_dropped++;
        peer_kill(p, GC_LEFT_LOST);
        return;
    }
    m = (gcs_qmsg *)malloc(sizeof(gcs_qmsg) + len);
    if (!m) {
        core->st.msgs_dropped++;
        return;
    }
    m->next = NULL;
    m->len = len;
    memcpy(m->data, data, len);
    if (p->q_tail) {
        p->q_tail->next = m;
    } else {
        p->q_head = m;
    }
    p->q_tail = m;
    p->q_count++;
}

static void peer_send(gcs_peer *p, const gc_msg *msg)
{
    gc_u8 buf[GC_LINK_MAX_MSG];
    size_t n = gc_msg_encode(msg, buf, sizeof(buf));

    if (n) {
        peer_send_raw(p, buf, n);
    }
}

static void send_error(gcs_peer *p, gc_i8 code, gc_u8 ref, const char *text)
{
    gc_msg m;

    memset(&m, 0, sizeof(m));
    m.type = GC_MT_ERROR;
    m.u.error.code = code;
    m.u.error.ref_type = ref;
    strncpy(m.u.error.text, text, GC_MAX_NAME);
    peer_send(p, &m);
}

static void send_reason(gcs_peer *p, gc_u8 type, gc_u8 reason)
{
    gc_msg m;

    memset(&m, 0, sizeof(m));
    m.type = type;
    m.u.reason.reason = reason;
    peer_send(p, &m);
}

static void send_peer_left(gcs_peer *host, gc_u8 slot, gc_u8 reason)
{
    gc_msg m;

    if (!host) {
        return;
    }
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_PEER_LEFT;
    m.u.peer_left.slot = slot;
    m.u.peer_left.reason = reason;
    peer_send(host, &m);
}

static void send_peer_joined(gcs_peer *host, gc_u8 type, gc_u8 slot, const gcs_member *mb, int rejoin)
{
    gc_msg m;

    if (!host) {
        return;
    }
    memset(&m, 0, sizeof(m));
    m.type = type;
    m.u.peer.slot = slot;
    m.u.peer.rejoin = (gc_u8)rejoin;
    memcpy(m.u.peer.device_id, mb->device_id, GC_DEVICE_ID_LEN);
    strcpy(m.u.peer.name, mb->name);
    peer_send(host, &m);
}

static void send_join_result(gcs_peer *p, gc_i8 result, const gcs_session *s, gc_u8 slot)
{
    gc_msg m;

    memset(&m, 0, sizeof(m));
    m.type = GC_MT_JOIN_RESULT;
    m.u.join_result.result = result;
    m.u.join_result.slot = slot;
    if (s) {
        m.u.join_result.session_id = s->id;
        m.u.join_result.max_players = s->max_players;
        if (s->m[0].peer) {
            strcpy(m.u.join_result.host_name, s->m[0].name);
        }
    }
    peer_send(p, &m);
}

/* ------------------------------------------------------------------ */
/* sessions                                                            */
/* ------------------------------------------------------------------ */

static gcs_session *session_find(gcs_core *core, gc_u32 id)
{
    int i;
    for (i = 0; i < core->n_sessions; i++) {
        if (core->sessions[i]->id == id && !core->sessions[i]->dead) {
            return core->sessions[i];
        }
    }
    return NULL;
}

static int session_count(const gcs_session *s, int include_pending)
{
    int i, n = 0;
    for (i = 0; i < GC_MAX_PLAYERS; i++) {
        if (s->m[i].state == M_ACTIVE || s->m[i].state == M_RESERVED ||
            (include_pending && s->m[i].state == M_PENDING)) {
            n++;
        }
    }
    return n;
}

static gcs_peer *session_host(gcs_session *s)
{
    return s->m[0].state == M_ACTIVE ? s->m[0].peer : NULL;
}

static void member_clear(gcs_member *mb)
{
    memset(mb, 0, sizeof(*mb));
}

static void session_end(gcs_core *core, gcs_session *s, gc_u8 reason)
{
    int i;

    LOG_I("session %08x (uid %08x) ended, reason %u", s->id, s->game_uid, reason);
    for (i = 1; i < GC_MAX_PLAYERS; i++) {
        gcs_member *mb = &s->m[i];
        if (mb->peer && mb->peer->session == s) {
            if (mb->state == M_PENDING) {
                send_join_result(mb->peer, GC_R_CLOSED, s, (gc_u8)i);
            } else {
                send_reason(mb->peer, GC_MT_SESSION_ENDED, reason);
            }
            mb->peer->session = NULL;
        }
        member_clear(mb);
    }
    if (s->m[0].peer && s->m[0].peer->session == s) {
        s->m[0].peer->session = NULL;
    }
    member_clear(&s->m[0]);
    s->dead = 1;
}

/* remove peer p from its session (if any) */
static void session_leave(gcs_core *core, gcs_peer *p, gc_u8 reason, int reserve)
{
    gcs_session *s = p->session;
    gcs_member *mb;

    if (!s) {
        return;
    }
    if (p->slot == GC_SLOT_HOST) {
        session_end(core, s, reason == GC_LEFT_LEAVE ? GC_LEFT_HOST_CLOSED : GC_LEFT_HOST_LOST);
        return;
    }
    mb = &s->m[p->slot];
    p->session = NULL;
    if (mb->peer != p) {
        return;
    }
    if (mb->state == M_PENDING) {
        member_clear(mb);
        /* the host never accepted this one; withdraw the request */
        send_peer_left(session_host(s), p->slot, GC_LEFT_LEAVE);
        return;
    }
    if (reserve && s->state != GC_SS_LOBBY) {
        mb->state = M_RESERVED;
        mb->peer = NULL;
        mb->reserved_owner = p;
        mb->t = core->now;
        LOG_I("session %08x: slot %u ('%s') reserved for rejoin", s->id, p->slot, mb->name);
    } else {
        member_clear(mb);
    }
    send_peer_left(session_host(s), p->slot, reason);
}

/* ------------------------------------------------------------------ */
/* peers                                                               */
/* ------------------------------------------------------------------ */

static void peer_link_write(void *ctx, const gc_u8 *data, size_t len)
{
    gcs_peer *p = (gcs_peer *)ctx;
    if (p->conn && p->core->io.write) {
        p->core->io.write(p->core->io_ctx, p->conn, data, len);
    }
}

static void peer_handle_msg(gcs_peer *p, const gc_u8 *data, size_t len);
static void peer_on_state(void *ctx, int ev, int reason);
static void peer_on_hello(void *ctx, const gc_hello *h);

static void peer_link_msg(void *ctx, const gc_u8 *data, size_t len)
{
    peer_handle_msg((gcs_peer *)ctx, data, len);
}

static gcs_peer *peer_new(gcs_core *core, gcs_conn *conn)
{
    gcs_peer *p;
    gc_link_cfg cfg;
    gc_link_ops ops;
    gcs_peer **np;

    p = (gcs_peer *)calloc(1, sizeof(*p));
    if (!p) {
        return NULL;
    }
    np = (gcs_peer **)realloc(core->peers, sizeof(gcs_peer *) * (core->n_peers + 1));
    if (!np) {
        free(p);
        return NULL;
    }
    core->peers = np;
    core->peers[core->n_peers++] = p;
    p->core = core;
    p->conn = conn;
    p->slot = GC_SLOT_NONE;
    gc_link_default_cfg(&cfg, GC_ROLE_SERVER, 0);
    cfg.ka_ms = 5000;
    cfg.dead_ms = 20000;
    cfg.rto_ms = 1000;
    cfg.rto_max_ms = 5000;
    ops.write = peer_link_write;
    ops.on_msg = peer_link_msg;
    ops.on_state = peer_on_state;
    ops.on_hello = peer_on_hello;
    ops.can_accept = NULL;
    gc_link_init(&p->link, &cfg, &ops, p);
    gc_link_listen(&p->link, core->now);
    return p;
}

static void peer_free(gcs_peer *p)
{
    while (p->q_head) {
        gcs_qmsg *m = p->q_head;
        p->q_head = m->next;
        free(m);
    }
    free(p);
}

/* Peer is gone for good: release memberships, close transport. */
static void peer_kill(gcs_peer *p, int reason)
{
    gcs_core *core = p->core;

    if (p->dead) {
        return;
    }
    if (p->registered) {
        LOG_I("peer %u '%s' gone (reason %d)", p->peer_id, pname(p), reason);
    }
    p->dead = 1;
    session_leave(core, p, (gc_u8)reason, reason != GC_LEFT_LEAVE);
    {
        int i, k;
        for (i = 0; i < core->n_sessions; i++) {
            for (k = 1; k < GC_MAX_PLAYERS; k++) {
                if (core->sessions[i]->m[k].reserved_owner == p) {
                    core->sessions[i]->m[k].reserved_owner = NULL;
                }
            }
        }
    }
    if (p->conn) {
        gcs_conn *c = p->conn;
        p->conn = NULL;
        c->peer = NULL;
        if (core->io.close) {
            core->io.close(core->io_ctx, c);
        }
    }
}

static void peer_detach(gcs_peer *p)
{
    gcs_core *core = p->core;

    if (p->conn) {
        gcs_conn *c = p->conn;
        p->conn = NULL;
        c->peer = NULL;
        if (core->io.close) {
            core->io.close(core->io_ctx, c);
        }
    }
    p->t_detached = core->now;
}

static void peer_on_state(void *ctx, int ev, int reason)
{
    gcs_peer *p = (gcs_peer *)ctx;
    gcs_core *core = p->core;

    switch (ev) {
    case GC_LE_UP_FRESH:
        LOG_D("link %08x up (fresh)", p->link.link_id);
        break;
    case GC_LE_UP_RESUMED:
        LOG_I("peer %u '%s' resumed link", p->peer_id, pname(p));
        core->st.resumes++;
        break;
    case GC_LE_DOWN:
        LOG_I("peer %u '%s' link down (reason %d), waiting for resume", p->peer_id, pname(p), reason);
        peer_detach(p);
        break;
    case GC_LE_CLOSED:
        peer_kill(p, reason == GC_LR_REMOTE ? GC_LEFT_LEAVE : GC_LEFT_LOST);
        break;
    default:
        break;
    }
}

static void peer_on_hello(void *ctx, const gc_hello *h)
{
    gcs_peer *p = (gcs_peer *)ctx;
    gcs_core *core = p->core;
    int i;

    if (h->link_id != 0) {
        for (i = 0; i < core->n_peers; i++) {
            gcs_peer *o = core->peers[i];
            if (o == p || o->dead || !o->link.has_session || o->link.link_id != h->link_id) {
                continue;
            }
            /* resumption: move this connection over to the old peer */
            if (o->conn) {
                peer_detach(o); /* stale half-open connection */
            }
            o->conn = p->conn;
            o->conn->peer = o;
            p->conn = NULL;
            p->dead = 1;
            gc_link_listen(&o->link, core->now);
            gc_link_accept(&o->link, h, 1, core->now);
            peer_flush(o);
            return;
        }
    }
    gc_link_accept(&p->link, h, 0, core->now);
}

static gcs_peer *peer_by_device(gcs_core *core, const gc_u8 *id, const gcs_peer *except)
{
    int i;

    if (id_is_zero(id)) {
        return NULL;
    }
    for (i = 0; i < core->n_peers; i++) {
        gcs_peer *o = core->peers[i];
        if (o != except && !o->dead && o->registered && memcmp(o->device_id, id, GC_DEVICE_ID_LEN) == 0) {
            return o;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* message handlers                                                    */
/* ------------------------------------------------------------------ */

static void h_register(gcs_peer *p, const gc_msg *m)
{
    gcs_core *core = p->core;
    gcs_peer *old;
    gc_msg r;

    memset(&r, 0, sizeof(r));
    r.type = GC_MT_REGISTERED;
    r.u.registered.proto = GC_PROTO_VERSION_MAX;
    strcpy(r.u.registered.server_name, core->cfg.server_name);

    if (core->cfg.access_key[0] && strcmp(core->cfg.access_key, m->u.reg.key) != 0) {
        LOG_W("registration of '%s' rejected: bad access key", m->u.reg.name);
        r.u.registered.result = GC_R_REJECTED;
        peer_send(p, &r);
        return;
    }
    if (m->u.reg.api != GC_API_V1 && m->u.reg.api != GC_API_V2) {
        r.u.registered.result = GC_R_INCOMPATIBLE;
        peer_send(p, &r);
        return;
    }
    old = peer_by_device(core, m->u.reg.device_id, p);
    if (old) {
        LOG_W("registration of '%s' rejected: device ID is already registered", m->u.reg.name);
        r.u.registered.result = GC_R_REJECTED;
        peer_send(p, &r);
        return;
    }
    if (p->registered && p->session) {
        session_leave(core, p, GC_LEFT_LEAVE, 0);
    }
    p->registered = 1;
    if (!p->peer_id) {
        p->peer_id = ++core->next_peer_id;
    }
    p->api = m->u.reg.api;
    p->variant = m->u.reg.variant;
    memcpy(p->device_id, m->u.reg.device_id, GC_DEVICE_ID_LEN);
    strcpy(p->name, m->u.reg.name[0] ? m->u.reg.name : "N-Gage");
    sanitize_name(p->name);
    LOG_I("peer %u registered: '%s' api %u variant %u id %02x%02x%02x%02x%02x%02x", p->peer_id, p->name, p->api,
          p->variant, p->device_id[0], p->device_id[1], p->device_id[2], p->device_id[3], p->device_id[4],
          p->device_id[5]);
    r.u.registered.result = GC_R_OK;
    r.u.registered.peer_id = p->peer_id;
    peer_send(p, &r);
}

static void h_host_open(gcs_peer *p, const gc_msg *m)
{
    gcs_core *core = p->core;
    gcs_session *s;
    gcs_session **ns;
    gc_msg r;

    memset(&r, 0, sizeof(r));
    r.type = GC_MT_HOST_OPENED;

    if (p->session) {
        session_leave(core, p, GC_LEFT_LEAVE, 0);
    }
    if (core->n_sessions >= core->cfg.max_sessions) {
        r.u.host_opened.result = GC_R_LIMIT;
        peer_send(p, &r);
        return;
    }
    s = (gcs_session *)calloc(1, sizeof(*s));
    ns = s ? (gcs_session **)realloc(core->sessions, sizeof(gcs_session *) * (core->n_sessions + 1)) : NULL;
    if (!ns) {
        free(s);
        r.u.host_opened.result = GC_R_LIMIT;
        peer_send(p, &r);
        return;
    }
    core->sessions = ns;
    core->sessions[core->n_sessions++] = s;
    do {
        s->id = rnd(core) & 0x7FFFFFFF;
    } while (s->id == 0 || session_find(core, s->id) != s);
    s->game_uid = m->u.host_open.game_uid;
    s->api = m->u.host_open.api ? m->u.host_open.api : p->api;
    s->max_players = m->u.host_open.max_players;
    if (s->max_players < 2 || s->max_players > GC_MAX_PLAYERS) {
        s->max_players = GC_MAX_PLAYERS;
    }
    s->flags = m->u.host_open.flags;
    s->state = GC_SS_LOBBY;
    s->created = core->now;
    s->seq = ++core->next_seq;
    strcpy(s->name, m->u.host_open.name[0] ? m->u.host_open.name : p->name);
    sanitize_name(s->name);
    s->m[0].state = M_ACTIVE;
    s->m[0].peer = p;
    memcpy(s->m[0].device_id, p->device_id, GC_DEVICE_ID_LEN);
    strcpy(s->m[0].name, p->name);
    p->session = s;
    p->slot = GC_SLOT_HOST;
    LOG_I("session %08x opened by '%s': uid %08x api %u max %u flags %02x", s->id, p->name, s->game_uid, s->api,
          s->max_players, s->flags);
    r.u.host_opened.result = GC_R_OK;
    r.u.host_opened.session_id = s->id;
    peer_send(p, &r);
}

static int discover_match(gcs_session *s, const gc_msg *m, gcs_peer *p)
{
    return !s->dead && s->game_uid == m->u.discover.game_uid && !(s->flags & GC_HF_HIDDEN) &&
           s->state == GC_SS_LOBBY && session_host(s) != p;
}

static void h_discover(gcs_peer *p, const gc_msg *m)
{
    gcs_core *core = p->core;
    gc_msg r;
    int i, total = 0, sent = 0;

    for (i = 0; i < core->n_sessions; i++) {
        total += discover_match(core->sessions[i], m, p);
    }
    memset(&r, 0, sizeof(r));
    r.type = GC_MT_SESSION_LIST;
    for (i = 0; i < core->n_sessions; i++) {
        gcs_session *s = core->sessions[i];
        gc_session_info *e;

        if (!discover_match(s, m, p)) {
            continue;
        }
        e = &r.u.list.s[r.u.list.count++];
        e->session_id = s->id;
        e->players = (gc_u8)session_count(s, 1);
        e->max_players = s->max_players;
        e->state = s->state;
        strcpy(e->host_name, s->name);
        /* an API family mismatch is reported as a full session so that
         * different GameComms generations never look joinable */
        if (m->u.discover.api && s->api != m->u.discover.api) {
            e->players = e->max_players;
        }
        sent++;
        if (r.u.list.count == GC_MAX_LIST && sent < total) {
            r.u.list.more = 1;
            peer_send(p, &r);
            memset(&r, 0, sizeof(r));
            r.type = GC_MT_SESSION_LIST;
        }
    }
    r.u.list.more = 0;
    peer_send(p, &r); /* final page, possibly empty */
}

static gcs_session *find_open_session(gcs_core *core, gc_u32 uid, gc_u8 api, gcs_peer *except)
{
    gcs_session *best = NULL;
    int i;

    for (i = 0; i < core->n_sessions; i++) {
        gcs_session *s = core->sessions[i];
        if (s->dead || s->game_uid != uid || s->api != api || s->state != GC_SS_LOBBY ||
            (s->flags & GC_HF_HIDDEN) || session_host(s) == except ||
            session_count(s, 1) >= s->max_players) {
            continue;
        }
        if (!best || s->seq < best->seq) {
            best = s;
        }
    }
    return best;
}

static void h_join(gcs_peer *p, const gc_msg *m)
{
    gcs_core *core = p->core;
    gcs_session *s;
    int i, slot = -1;
    gc_u8 api = m->u.join.api ? m->u.join.api : p->api;

    if (p->session) {
        /* joining again while still listed somewhere: leave first */
        session_leave(core, p, GC_LEFT_LEAVE, 0);
    }

    if (m->u.join.session_id == 0) {
        s = find_open_session(core, m->u.join.game_uid, api, p);
        if (!s && m->u.join.rejoin) {
            /* only the same live peer may locate its reserved slot */
            for (i = 0; i < core->n_sessions && !s; i++) {
                int k;
                gcs_session *c = core->sessions[i];
                if (c->dead || c->game_uid != m->u.join.game_uid) {
                    continue;
                }
                for (k = 1; k < GC_MAX_PLAYERS; k++) {
                    if (c->m[k].state == M_RESERVED && c->m[k].reserved_owner == p) {
                        s = c;
                    }
                }
            }
        }
    } else {
        s = session_find(core, m->u.join.session_id);
    }

    if (!s) {
        send_join_result(p, GC_R_NOT_FOUND, NULL, GC_SLOT_NONE);
        return;
    }
    if (s->game_uid != m->u.join.game_uid || s->api != api) {
        LOG_W("'%s' tried to join session %08x of an incompatible game (uid %08x/api %u vs %08x/%u)", p->name,
              s->id, m->u.join.game_uid, api, s->game_uid, s->api);
        send_join_result(p, GC_R_INCOMPATIBLE, NULL, GC_SLOT_NONE);
        return;
    }
    if (session_host(s) == p) {
        send_join_result(p, GC_R_BUSY, NULL, GC_SLOT_NONE);
        return;
    }

    /* A device ID is public identity data, not proof of slot ownership. */
    for (i = 1; i < GC_MAX_PLAYERS; i++) {
        gcs_member *mb = &s->m[i];
        if (mb->state == M_RESERVED && mb->reserved_owner == p) {
            mb->state = M_ACTIVE;
            mb->peer = p;
            mb->reserved_owner = NULL;
            strcpy(mb->name, p->name);
            p->session = s;
            p->slot = (gc_u8)i;
            LOG_I("session %08x: '%s' rejoined slot %d", s->id, p->name, i);
            send_join_result(p, GC_R_OK, s, (gc_u8)i);
            send_peer_joined(session_host(s), GC_MT_PEER_JOINED, (gc_u8)i, mb, 1);
            return;
        }
    }

    if (s->state != GC_SS_LOBBY) {
        send_join_result(p, GC_R_CLOSED, s, GC_SLOT_NONE);
        return;
    }
    if (session_count(s, 1) >= s->max_players) {
        send_join_result(p, GC_R_FULL, s, GC_SLOT_NONE);
        return;
    }
    for (i = 1; i < GC_MAX_PLAYERS; i++) {
        if (s->m[i].state == M_EMPTY) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        send_join_result(p, GC_R_FULL, s, GC_SLOT_NONE);
        return;
    }
    s->m[slot].peer = p;
    s->m[slot].reserved_owner = NULL;
    memcpy(s->m[slot].device_id, p->device_id, GC_DEVICE_ID_LEN);
    strcpy(s->m[slot].name, p->name);
    s->m[slot].t = core->now;
    p->session = s;
    p->slot = (gc_u8)slot;
    if (s->flags & GC_HF_CONFIRM_CLIENTS) {
        s->m[slot].state = M_PENDING;
        LOG_I("session %08x: '%s' waiting for confirmation in slot %d", s->id, p->name, slot);
        send_join_result(p, GC_R_PENDING, s, (gc_u8)slot);
        send_peer_joined(session_host(s), GC_MT_PEER_REQUEST, (gc_u8)slot, &s->m[slot], 0);
    } else {
        s->m[slot].state = M_ACTIVE;
        LOG_I("session %08x: '%s' joined slot %d", s->id, p->name, slot);
        send_join_result(p, GC_R_OK, s, (gc_u8)slot);
        send_peer_joined(session_host(s), GC_MT_PEER_JOINED, (gc_u8)slot, &s->m[slot], 0);
    }
}

static int is_host(const gcs_peer *p)
{
    return p->session && p->slot == GC_SLOT_HOST;
}

static void h_peer_accept(gcs_peer *p, const gc_msg *m)
{
    gcs_session *s = p->session;
    gcs_member *mb;
    gcs_peer *c;
    gc_u8 slot = m->u.peer_accept.slot;

    if (!is_host(p) || slot == 0 || slot >= GC_MAX_PLAYERS || s->m[slot].state != M_PENDING) {
        send_error(p, GC_R_BAD_REQUEST, GC_MT_PEER_ACCEPT, "no pending peer");
        return;
    }
    mb = &s->m[slot];
    c = mb->peer;
    if (m->u.peer_accept.accept) {
        mb->state = M_ACTIVE;
        send_join_result(c, GC_R_OK, s, slot);
        send_peer_joined(p, GC_MT_PEER_JOINED, slot, mb, 0);
    } else {
        if (c) {
            c->session = NULL;
            send_join_result(c, GC_R_REJECTED, s, GC_SLOT_NONE);
        }
        member_clear(mb);
    }
}

static void h_kick(gcs_peer *p, const gc_msg *m)
{
    gcs_core *core = p->core;
    gcs_session *s = p->session;
    gcs_member *mb;
    gc_u8 slot = m->u.peer_left.slot;
    int temporary = m->u.peer_left.reason == GC_LEFT_LOST;

    if (!is_host(p) || slot == 0 || slot >= GC_MAX_PLAYERS || s->m[slot].state == M_EMPTY) {
        send_error(p, GC_R_NOT_MEMBER, GC_MT_KICK, "no such slot");
        return;
    }
    mb = &s->m[slot];
    if (mb->peer && mb->peer->session == s) {
        send_reason(mb->peer, GC_MT_SESSION_ENDED, temporary ? GC_LEFT_LOST : GC_LEFT_KICKED);
        mb->peer->session = NULL;
    }
    LOG_I("session %08x: host removed slot %u (%s)", s->id, slot, temporary ? "temporary" : "permanent");
    if (temporary && mb->state != M_PENDING) {
        mb->state = M_RESERVED;
        mb->peer = NULL;
        mb->t = core->now;
    } else {
        member_clear(mb);
    }
}

static void h_data(gcs_peer *p, const gc_msg *m, const gc_u8 *raw, size_t len)
{
    gcs_core *core = p->core;
    gcs_session *s = p->session;
    gc_u8 buf[GC_LINK_MAX_MSG];
    int i;

    if (!s || s->m[p->slot].state != M_ACTIVE || s->m[p->slot].peer != p) {
        core->st.msgs_dropped++;
        send_error(p, GC_R_NOT_MEMBER, GC_MT_DATA, "not in a session");
        return;
    }
    memcpy(buf, raw, len);
    if (p->slot == GC_SLOT_HOST) {
        buf[1] = GC_SLOT_HOST; /* source */
        for (i = 1; i < GC_MAX_PLAYERS; i++) {
            if ((m->u.data.addr == GC_SLOT_ALL || m->u.data.addr == i) && s->m[i].state == M_ACTIVE && s->m[i].peer) {
                peer_send_raw(s->m[i].peer, buf, len);
                core->st.msgs_routed++;
            } else if (m->u.data.addr == i) {
                core->st.msgs_dropped++; /* addressed member not present */
            }
        }
        if (m->u.data.addr != GC_SLOT_ALL && (m->u.data.addr == 0 || m->u.data.addr >= GC_MAX_PLAYERS)) {
            core->st.msgs_dropped++;
            send_error(p, GC_R_BAD_REQUEST, GC_MT_DATA, "bad destination");
        }
    } else {
        gcs_peer *h = session_host(s);
        if (m->u.data.addr != GC_SLOT_HOST && m->u.data.addr != GC_SLOT_ALL) {
            core->st.msgs_dropped++;
            send_error(p, GC_R_BAD_REQUEST, GC_MT_DATA, "clients may only send to the host");
            return;
        }
        buf[1] = p->slot; /* source */
        if (h) {
            peer_send_raw(h, buf, len);
            core->st.msgs_routed++;
        } else {
            core->st.msgs_dropped++;
        }
    }
}

static void peer_handle_msg(gcs_peer *p, const gc_u8 *data, size_t len)
{
    gcs_core *core = p->core;
    gc_msg m;

    if (p->dead) {
        return;
    }
    if (!gc_msg_decode(data, len, &m)) {
        LOG_W("peer %u '%s': malformed message (type %02x, %u bytes)", p->peer_id, pname(p), len ? data[0] : 0,
              (unsigned)len);
        send_error(p, GC_R_BAD_REQUEST, len ? data[0] : 0, "malformed");
        return;
    }
    LOG_D("peer %u '%s' -> %s", p->peer_id, pname(p), gc_msg_name(m.type));
    if (!p->registered && m.type != GC_MT_REGISTER && m.type != GC_MT_BRIDGE_QUERY) {
        send_error(p, GC_R_NOT_REGISTERED, m.type, "register first");
        return;
    }
    switch (m.type) {
    case GC_MT_REGISTER:
        h_register(p, &m);
        break;
    case GC_MT_HOST_OPEN:
        h_host_open(p, &m);
        break;
    case GC_MT_HOST_CLOSE:
        if (is_host(p)) {
            session_leave(core, p, GC_LEFT_LEAVE, 0);
        }
        break;
    case GC_MT_SESSION_STATE:
        if (is_host(p)) {
            p->session->state = m.u.session_state.state;
            LOG_D("session %08x state %u", p->session->id, p->session->state);
        }
        break;
    case GC_MT_PEER_ACCEPT:
        h_peer_accept(p, &m);
        break;
    case GC_MT_KICK:
        h_kick(p, &m);
        break;
    case GC_MT_DISCOVER:
        h_discover(p, &m);
        break;
    case GC_MT_JOIN:
        h_join(p, &m);
        break;
    case GC_MT_LEAVE:
        session_leave(core, p, m.u.reason.reason, m.u.reason.reason == GC_LEFT_LOST);
        break;
    case GC_MT_DATA:
        h_data(p, &m, data, len);
        break;
    case GC_MT_ERROR:
        LOG_W("peer %u '%s' reports error %d for %s: %s", p->peer_id, pname(p), m.u.error.code,
              gc_msg_name(m.u.error.ref_type), m.u.error.text);
        break;
    case GC_MT_BRIDGE_QUERY:
        break;
    default:
        send_error(p, GC_R_BAD_REQUEST, m.type, "unexpected");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

gcs_core *gcs_core_create(const gcs_config *cfg, const gcs_io *io, void *io_ctx, gc_u32 seed)
{
    gcs_core *c = (gcs_core *)calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    c->cfg = *cfg;
    c->io = *io;
    c->io_ctx = io_ctx;
    c->rng = seed ? seed : 0x9E3779B9u;
    return c;
}

void gcs_core_destroy(gcs_core *core)
{
    int i;
    if (!core) {
        return;
    }
    for (i = 0; i < core->n_peers; i++) {
        peer_free(core->peers[i]);
    }
    for (i = 0; i < core->n_sessions; i++) {
        free(core->sessions[i]);
    }
    free(core->peers);
    free(core->sessions);
    free(core);
}

static void collect_garbage(gcs_core *core)
{
    int i, j;

    for (i = 0, j = 0; i < core->n_peers; i++) {
        gcs_peer *p = core->peers[i];
        if (p->dead) {
            if (p->conn) {
                p->conn->peer = NULL;
            }
            peer_free(p);
        } else {
            core->peers[j++] = p;
        }
    }
    core->n_peers = j;
    for (i = 0, j = 0; i < core->n_sessions; i++) {
        if (core->sessions[i]->dead) {
            free(core->sessions[i]);
        } else {
            core->sessions[j++] = core->sessions[i];
        }
    }
    core->n_sessions = j;
}

void gcs_core_conn_open(gcs_core *core, gcs_conn *conn, gc_u32 now)
{
    int live = 0, i;

    core->now = now;
    for (i = 0; i < core->n_peers; i++) {
        live += !core->peers[i]->dead;
    }
    if (live >= core->cfg.max_peers) {
        LOG_W("peer limit reached, refusing connection");
        conn->peer = NULL;
        if (core->io.close) {
            core->io.close(core->io_ctx, conn);
        }
        return;
    }
    conn->peer = peer_new(core, conn);
    if (!conn->peer) {
        LOG_W("cannot allocate peer state, refusing connection");
        conn->peer = NULL;
        if (core->io.close) {
            core->io.close(core->io_ctx, conn);
        }
    }
}

void gcs_core_conn_input(gcs_core *core, gcs_conn *conn, const gc_u8 *data, size_t len, gc_u32 now)
{
    gcs_peer *p = conn->peer;

    core->now = now;
    if (!p || p->dead) {
        return;
    }
    gc_link_input(&p->link, data, len, now);
    /* the connection may have been moved to a resumed peer meanwhile */
    if (conn->peer && !conn->peer->dead) {
        peer_flush(conn->peer);
    }
}

void gcs_core_conn_closed(gcs_core *core, gcs_conn *conn, gc_u32 now)
{
    gcs_peer *p = conn->peer;

    core->now = now;
    conn->peer = NULL;
    if (!p || p->conn != conn) {
        return;
    }
    p->conn = NULL;
    if (!p->registered || !p->link.has_session) {
        p->dead = 1; /* never got anywhere: nothing to resume */
        return;
    }
    gc_link_transport_lost(&p->link, now);
    p->t_detached = now;
}

void gcs_core_poll(gcs_core *core, gc_u32 now)
{
    int i, k;

    core->now = now;
    for (i = 0; i < core->n_peers; i++) {
        gcs_peer *p = core->peers[i];
        if (p->dead) {
            continue;
        }
        if (p->conn) {
            gc_link_poll(&p->link, now);
            if (!p->dead) {
                peer_flush(p);
            }
        } else if ((gc_u32)(now - p->t_detached) >= core->cfg.link_grace_ms) {
            peer_kill(p, GC_LEFT_LOST);
        }
    }
    for (i = 0; i < core->n_sessions; i++) {
        gcs_session *s = core->sessions[i];
        if (s->dead) {
            continue;
        }
        for (k = 1; k < GC_MAX_PLAYERS; k++) {
            gcs_member *mb = &s->m[k];
            if (mb->state == M_RESERVED && (gc_u32)(now - mb->t) >= core->cfg.reserve_ms) {
                LOG_I("session %08x: reservation of slot %d ('%s') expired", s->id, k, mb->name);
                member_clear(mb);
                send_peer_left(session_host(s), (gc_u8)k, GC_LEFT_EXPIRED);
            } else if (mb->state == M_PENDING && (gc_u32)(now - mb->t) >= core->cfg.pending_ms) {
                if (mb->peer) {
                    mb->peer->session = NULL;
                    send_join_result(mb->peer, GC_R_REJECTED, s, GC_SLOT_NONE);
                }
                member_clear(mb);
                send_peer_left(session_host(s), (gc_u8)k, GC_LEFT_EXPIRED);
            }
        }
    }
    collect_garbage(core);
}

void gcs_core_stats(const gcs_core *core, gcs_stats *st)
{
    int i;

    *st = core->st;
    st->peers = 0;
    st->peers_attached = 0;
    for (i = 0; i < core->n_peers; i++) {
        if (!core->peers[i]->dead && core->peers[i]->registered) {
            st->peers++;
            st->peers_attached += core->peers[i]->conn != NULL;
        }
    }
    st->sessions = 0;
    for (i = 0; i < core->n_sessions; i++) {
        st->sessions += !core->sessions[i]->dead;
    }
}

void gcs_core_dump(gcs_core *core)
{
    static const char *mstate[] = { "-", "pending", "active", "reserved" };
    int i, k;
    gcs_stats st;
    int lvl = core->cfg.log_level;

    core->cfg.log_level = 2;
    gcs_core_stats(core, &st);
    LOG_I("status: %d peers (%d attached), %d sessions, %u routed, %u dropped, %u resumes", st.peers,
          st.peers_attached, st.sessions, st.msgs_routed, st.msgs_dropped, st.resumes);
    for (i = 0; i < core->n_sessions; i++) {
        gcs_session *s = core->sessions[i];
        if (s->dead) {
            continue;
        }
        LOG_I("  session %08x '%s' uid %08x api %u state %u players %d/%u", s->id, s->name, s->game_uid, s->api,
              s->state, session_count(s, 0), s->max_players);
        for (k = 0; k < GC_MAX_PLAYERS; k++) {
            if (s->m[k].state != M_EMPTY) {
                LOG_I("    slot %d: %-8s '%s'%s", k, mstate[s->m[k].state], s->m[k].name,
                      s->m[k].peer && !s->m[k].peer->conn ? " (detached)" : "");
            }
        }
    }
    core->cfg.log_level = lvl;
}
