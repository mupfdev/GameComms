/** @file gc_dev.c
 *
 *  GameComms device engine - see gc_dev.h and docs/DESIGN.md.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <string.h>
#include "gc_dev.h"

/* ------------------------------------------------------------------ */
/* GameComms session control sub-protocol (DATA channel GC_CH_CONTROL) */
/* ------------------------------------------------------------------ */

/* GameComms 1.x */
#define CTL_START        0x10 /* h->c  game starts                      */
#define CTL_PAUSE        0x11 /* any   u8 origin slot                   */
#define CTL_CONTINUE_REQ 0x12 /* c->h  client called Continue           */
#define CTL_CONTINUE     0x13 /* h->c  everybody continues              */
#define CTL_END          0x14 /* h->c  i32 reason                       */
#define CTL_CLIENT_END   0x15 /* c->h  client ended its game            */
#define CTL_TEMP_DISC    0x16 /* any   temporary disconnect (paused)    */
#define CTL_READY        0x17 /* c->h  client called ReconnectL         */
#define CTL_RECONNECTED  0x18 /* h->c  host reconnected this client     */
#define CTL_SYNC         0x19 /* h->c  u8 game state (late/rejoin sync) */
/* GameComms 2.x */
#define CTL2_CONNECT     0x30 /* h->c  host establishes the connection  */
#define CTL2_CONNECT_ACK 0x31 /* c->h                                   */
#define CTL2_DISCONNECT  0x32 /* any   i32 reason                       */

#define RETRY_MS 3000

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void dlog(gcd_dev *d, const char *msg, int a)
{
    char   line[96];
    size_t n = 0;
    char   num[12];
    int    k = 0;
    unsigned int v;

    if (!d->ops.log)
    {
        return;
    }
    while (*msg && n < sizeof(line) - 14)
    {
        line[n++] = *msg++;
    }
    line[n++] = ' ';
    if (a < 0)
    {
        line[n++] = '-';
        v = (unsigned int)(-a);
    }
    else
    {
        v = (unsigned int)a;
    }
    do
    {
        num[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && k < 11);
    while (k)
    {
        line[n++] = num[--k];
    }
    line[n] = '\0';
    d->ops.log(d->ctx, line);
}

static void copy_name(char *dst, const char *src)
{
    size_t n = 0;
    while (src && src[n] && n < GC_MAX_NAME)
    {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static int name_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static gcd_event *ev_new(gcd_dev *d, int type)
{
    gcd_event *e;

    if (d->ev_count >= GCD_EVENT_QUEUE)
    {
        d->ev_dropped++;
        dlog(d, "event queue overflow, dropped event", type);
        return NULL;
    }
    e = &d->ev[(d->ev_head + d->ev_count) % GCD_EVENT_QUEUE];
    d->ev_count++;
    memset(e, 0, sizeof(*e) - sizeof(e->data));
    e->type = type;
    if (d->ops.wake)
    {
        d->ops.wake(d->ctx);
    }
    return e;
}

static void ev(gcd_dev *d, int type, int id, int err)
{
    gcd_event *e = ev_new(d, type);
    if (e)
    {
        e->id = id;
        e->err = err;
    }
}

static void ev_dev(gcd_dev *d, int type, int id, int err, const gcd_device *dev)
{
    gcd_event *e = ev_new(d, type);
    if (e)
    {
        e->id = id;
        e->err = err;
        e->dev = *dev;
    }
}

static void ev_data(gcd_dev *d, int type, int id, const gc_u8 *p, size_t n)
{
    gcd_event *e = ev_new(d, type);
    if (e)
    {
        e->id = id;
        e->len = (gc_u16)n;
        memcpy(e->data, p, n);
    }
}

int gcd_pop_event(gcd_dev *d, gcd_event *out)
{
    if (d->ev_count == 0)
    {
        return 0;
    }
    *out = d->ev[d->ev_head];
    d->ev_head = (d->ev_head + 1) % GCD_EVENT_QUEUE;
    d->ev_count--;
    return 1;
}

/* ---- sending ---- */

static int send_raw(gcd_dev *d, const gc_u8 *buf, size_t n)
{
    int r;
    if (!n)
    {
        return GCD_KErrArgument;
    }
    r = gc_link_send(&d->link, buf, n, d->now);
    if (r == GC_LINK_OK)
    {
        return GCD_KErrNone;
    }
    if (r == GC_LINK_E_FULL)
    {
        return GCD_KErrOverflow;
    }
    if (r == GC_LINK_E_TOOBIG)
    {
        return GCD_KErrArgument;
    }
    return GCD_KErrNotReady;
}

static int send_msg(gcd_dev *d, const gc_msg *m)
{
    gc_u8 buf[64 + GC_MAX_LIST * 48];
    return send_raw(d, buf, gc_msg_encode(m, buf, sizeof(buf)));
}

static int send_ctl(gcd_dev *d, gc_u8 dst, gc_u8 type, const gc_u8 *arg, size_t alen)
{
    gc_u8 p[8];
    gc_u8 buf[16];
    p[0] = type;
    if (alen)
    {
        memcpy(p + 1, arg, alen);
    }
    return send_raw(d, buf, gc_msg_encode_data(dst, GC_CH_CONTROL, p, alen + 1, buf, sizeof(buf)));
}

static int send_ctl_i32(gcd_dev *d, gc_u8 dst, gc_u8 type, int v)
{
    gc_u8 a[4];
    a[0] = (gc_u8)((gc_u32)v >> 24);
    a[1] = (gc_u8)((gc_u32)v >> 16);
    a[2] = (gc_u8)((gc_u32)v >> 8);
    a[3] = (gc_u8)v;
    return send_ctl(d, dst, type, a, 4);
}

static void send_simple(gcd_dev *d, gc_u8 type, gc_u8 reason)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = type;
    m.u.reason.reason = reason;
    send_msg(d, &m);
}

static void send_session_state(gcd_dev *d, gc_u8 state)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_SESSION_STATE;
    m.u.session_state.state = state;
    send_msg(d, &m);
}

static void send_kick(gcd_dev *d, int slot, gc_u8 reason)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_KICK;
    m.u.peer_left.slot = (gc_u8)slot;
    m.u.peer_left.reason = reason;
    send_msg(d, &m);
}

static int map_result(int r)
{
    switch (r)
    {
        case GC_R_OK: return GCD_KErrNone;
        case GC_R_NOT_FOUND: return GCD_KErrNotCommsHost;
        case GC_R_INCOMPATIBLE: return GCD_KErrIncompatibleGame;
        case GC_R_FULL: return GCD_KErrInUse;
        case GC_R_CLOSED: return GCD_KErrNotReady;
        case GC_R_REJECTED: return GCD_KErrCancel;
        case GC_R_UNAVAILABLE: return GCD_KErrCouldNotConnect;
        default: return GCD_KErrCouldNotConnect;
    }
}

/* ------------------------------------------------------------------ */
/* state reset                                                         */
/* ------------------------------------------------------------------ */

static void reset_role(gcd_dev *d)
{
    d->role = GCD_ROLE_IDLE;
    d->want_role = GCD_ROLE_IDLE;
    d->session_open = 0;
    d->session_id = 0;
    d->slot = GC_SLOT_NONE;
    d->conn_state = GCD_CS_NOT_CONNECTED;
    d->game_state = GCD_GS_OVER;
    memset(d->peer, 0, sizeof(d->peer));
    d->host_cont = 0;
    d->selecting = 0;
    d->joining = 0;
    d->n_sessions = 0;
    d->target_set = 0;
    d->temp_disc = 0;
    d->ready_sent = 0;
    d->connect_all_pending = 0;
    d->accept_mode = GCD_ACCEPT_NONE;
}

/* leave whatever we have on the server */
static void leave_network(gcd_dev *d, gc_u8 reason)
{
    if (!d->session_open)
    {
        return;
    }
    if (d->role == GCD_ROLE_HOST)
    {
        send_simple(d, GC_MT_HOST_CLOSE, reason);
    }
    else
    {
        send_simple(d, GC_MT_LEAVE, reason);
    }
    d->session_open = 0;
}

static int connected_clients(gcd_dev *d)
{
    int i, n = 0;
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        n += d->peer[i].present && d->peer[i].connected && !d->peer[i].temp_disc;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* role start (deferred until registered)                              */
/* ------------------------------------------------------------------ */

static void do_host_open(gcd_dev *d)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_HOST_OPEN;
    m.u.host_open.game_uid = d->cfg.game_uid;
    m.u.host_open.api = d->cfg.api;
    if (d->cfg.api == GC_API_V1)
    {
        int maxp = d->start_players;
        if (maxp < 2)
        {
            maxp = 2;
        }
        if (maxp > GC_MAX_PLAYERS)
        {
            maxp = GC_MAX_PLAYERS;
        }
        m.u.host_open.max_players = (gc_u8)maxp;
    }
    else
    {
        m.u.host_open.max_players = GC_MAX_PLAYERS;
    }
    copy_name(m.u.host_open.name, d->cfg.name);
    send_msg(d, &m);
    d->t_retry = d->now + RETRY_MS * 3;
}

static void do_discover(gcd_dev *d)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_DISCOVER;
    m.u.discover.game_uid = d->cfg.game_uid;
    m.u.discover.api = d->cfg.api;
    send_msg(d, &m);
    d->t_discover = d->now + d->cfg.discover_ms;
}

static void do_join(gcd_dev *d, gc_u32 sid, int rejoin)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_JOIN;
    m.u.join.session_id = sid;
    m.u.join.game_uid = d->cfg.game_uid;
    m.u.join.api = d->cfg.api;
    m.u.join.rejoin = (gc_u8)rejoin;
    d->joining = 1;
    send_msg(d, &m);
}

static int ready(gcd_dev *d)
{
    return d->registered && gc_link_is_up(&d->link);
}

/* called whenever registration becomes available */
static void kick_role(gcd_dev *d)
{
    if (!ready(d))
    {
        return;
    }
    if (d->want_role == GCD_ROLE_HOST && !d->session_open)
    {
        do_host_open(d);
    }
    else if (d->want_role == GCD_ROLE_CLIENT && d->selecting)
    {
        do_discover(d);
    }
    else if (d->want_role == GCD_ROLE_CLIENT && d->joining && d->target_set)
    {
        do_join(d, d->target.session_id, 0);
    }
    if (d->discover_once && !d->selecting)
    {
        do_discover(d);
    }
}

/* ------------------------------------------------------------------ */
/* communication failures                                              */
/* ------------------------------------------------------------------ */

/* the session vanished underneath us (bridge/server lost, host gone) */
static void session_lost(gcd_dev *d, int err, int host_quit)
{
    int i;

    if (d->role == GCD_ROLE_CLIENT)
    {
        int had = d->session_open || d->joining;
        int gs = d->game_state;
        if (d->cfg.api == GC_API_V1)
        {
            if (d->joining && !d->session_open)
            {
                ev(d, GCD_EV1_HOST_CONNECTED, 0, err);
            }
            else if (had && !d->temp_disc)
            {
                ev(d, GCD_EV1_HOST_DISCONNECTED, 0, host_quit ? GCD_KErrNone : err);
                if (gs != GCD_GS_OVER || host_quit)
                {
                    ev(d, GCD_EV1_END_GAME, 0, host_quit ? GCD_KErrHostQuit : err);
                }
            }
        }
        else
        {
            if (d->conn_state == GCD_CS_CONNECTED)
            {
                ev(d, GCD_EV2_DISCONNECTED, 0, err);
            }
            else if (d->conn_state == GCD_CS_CONNECTING)
            {
                ev(d, GCD_EV2_CONNECTED, 0, err);
            }
        }
        d->session_open = 0;
        d->joining = 0;
        d->conn_state = GCD_CS_NOT_CONNECTED;
        d->game_state = GCD_GS_OVER;
        d->temp_disc = 0;
    }
    else if (d->role == GCD_ROLE_HOST)
    {
        int was_open = d->session_open;
        for (i = 1; i < GC_MAX_PLAYERS; i++)
        {
            gcd_peer *p = &d->peer[i];
            if (!p->present)
            {
                continue;
            }
            if (d->cfg.api == GC_API_V1)
            {
                if (p->connected && !p->temp_disc)
                {
                    ev(d, GCD_EV1_CLIENT_DISCONNECTED, i, err);
                }
            }
            else if (p->connected2)
            {
                ev(d, GCD_EV2_DISCONNECTED, i, err);
            }
        }
        if (d->cfg.api == GC_API_V1 && d->game_state != GCD_GS_OVER)
        {
            ev(d, GCD_EV1_END_GAME, 0, err);
        }
        memset(d->peer, 0, sizeof(d->peer));
        d->session_open = 0;
        d->conn_state = GCD_CS_NOT_CONNECTED;
        d->game_state = GCD_GS_OVER;
        /* keep hosting: re-open as soon as the network is back */
        if (was_open || d->want_role == GCD_ROLE_HOST)
        {
            d->want_role = GCD_ROLE_HOST;
            d->t_retry = d->now + RETRY_MS;
        }
    }
}

/* ------------------------------------------------------------------ */
/* GameComms 1.x host logic                                            */
/* ------------------------------------------------------------------ */

static void v1_host_end(gcd_dev *d, int reason, int notify_host)
{
    int i;
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        if (d->peer[i].present)
        {
            send_ctl_i32(d, (gc_u8)i, CTL_END, reason);
        }
    }
    if (notify_host)
    {
        ev(d, GCD_EV1_END_GAME, 0, reason);
    }
    leave_network(d, GC_LEFT_LEAVE);
    memset(d->peer, 0, sizeof(d->peer));
    d->game_state = GCD_GS_OVER;
    d->conn_state = GCD_CS_NOT_CONNECTED;
    d->want_role = GCD_ROLE_IDLE;
}

static void v1_host_check_start(gcd_dev *d)
{
    int i;
    if (d->game_state != GCD_GS_OVER || 1 + connected_clients(d) < d->start_players)
    {
        return;
    }
    send_session_state(d, GC_SS_PLAYING);
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        if (d->peer[i].present && d->peer[i].connected)
        {
            send_ctl(d, (gc_u8)i, CTL_START, NULL, 0);
        }
    }
    d->game_state = GCD_GS_PLAY;
    d->conn_state = GCD_CS_CONNECTED;
    ev(d, GCD_EV1_START_GAME, 0, GCD_KErrNone);
}

/* a client is gone (ended, disconnected, kicked, lost) while playing */
static void v1_host_client_gone(gcd_dev *d, int id, int notify_disconnect, int err)
{
    gcd_peer *p = &d->peer[id];
    int       gs = d->game_state;

    if (notify_disconnect && p->connected && !p->temp_disc)
    {
        ev(d, GCD_EV1_CLIENT_DISCONNECTED, id, err);
    }
    memset(p, 0, sizeof(*p));
    if (gs == GCD_GS_PLAY || gs == GCD_GS_PAUSE)
    {
        if (1 + connected_clients(d) < d->min_players)
        {
            v1_host_end(d, GCD_KErrNotEnoughPlayers, 1);
        }
        else
        {
            ev(d, GCD_EV1_CLIENT_ENDED_GAME, id, 0);
        }
    }
}

static void v1_host_try_continue(gcd_dev *d)
{
    int i;
    if (d->game_state != GCD_GS_PAUSE || !d->host_cont)
    {
        return;
    }
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        gcd_peer *p = &d->peer[i];
        if (p->present && p->connected && (p->temp_disc || !p->cont_req))
        {
            return; /* still waiting for this one */
        }
    }
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        if (d->peer[i].present && d->peer[i].connected)
        {
            send_ctl(d, (gc_u8)i, CTL_CONTINUE, NULL, 0);
            d->peer[i].cont_req = 0;
        }
    }
    d->host_cont = 0;
    d->game_state = GCD_GS_PLAY;
    send_session_state(d, GC_SS_PLAYING);
    ev(d, GCD_EV1_CONTINUE_GAME, 0, 0);
}

static void v1_host_pause(gcd_dev *d, int origin)
{
    int i;
    d->game_state = GCD_GS_PAUSE;
    d->host_cont = 0;
    send_session_state(d, GC_SS_PAUSED);
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        d->peer[i].cont_req = 0;
        if (i != origin && d->peer[i].present && d->peer[i].connected && !d->peer[i].temp_disc)
        {
            gc_u8 o = (gc_u8)origin;
            send_ctl(d, (gc_u8)i, CTL_PAUSE, &o, 1);
        }
    }
    if (origin != 0)
    {
        ev(d, GCD_EV1_PAUSE_GAME, 0, 0);
    }
}

static void v1_host_ctl(gcd_dev *d, int id, const gc_u8 *p, size_t n)
{
    gcd_peer *peer = &d->peer[id];

    if (!peer->present || n < 1)
    {
        return;
    }
    switch (p[0])
    {
        case CTL_PAUSE:
            if (d->game_state == GCD_GS_PLAY)
            {
                v1_host_pause(d, id);
            }
            break;
        case CTL_CONTINUE_REQ:
            peer->cont_req = 1;
            v1_host_try_continue(d);
            break;
        case CTL_CLIENT_END:
            v1_host_client_gone(d, id, 1, GCD_KErrNone);
            break;
        case CTL_TEMP_DISC:
            if (peer->connected && !peer->temp_disc)
            {
                ev(d, GCD_EV1_CLIENT_DISCONNECTED, id, GCD_KErrNone);
                if (d->game_state == GCD_GS_PLAY)
                {
                    /* disconnecting while playing ends that client's game */
                    v1_host_client_gone(d, id, 0, GCD_KErrNone);
                    break;
                }
                peer->temp_disc = 1;
                peer->ready = 0;
                d->conn_state = GCD_CS_NOT_CONNECTED;
            }
            break;
        case CTL_READY:
            peer->ready = 1;
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* GameComms 1.x client logic                                          */
/* ------------------------------------------------------------------ */

static void v1_client_ctl(gcd_dev *d, const gc_u8 *p, size_t n)
{
    if (n < 1)
    {
        return;
    }
    switch (p[0])
    {
        case CTL_START:
            if (d->game_state == GCD_GS_OVER)
            {
                d->game_state = GCD_GS_PLAY;
                ev(d, GCD_EV1_START_GAME, 0, GCD_KErrNone);
            }
            break;
        case CTL_SYNC:
            if (n >= 2)
            {
                d->game_state = p[1];
            }
            break;
        case CTL_PAUSE:
            if (d->game_state == GCD_GS_PLAY)
            {
                d->game_state = GCD_GS_PAUSE;
                ev(d, GCD_EV1_PAUSE_GAME, 0, 0);
            }
            break;
        case CTL_CONTINUE:
            if (d->game_state == GCD_GS_PAUSE)
            {
                d->game_state = GCD_GS_PLAY;
                ev(d, GCD_EV1_CONTINUE_GAME, 0, 0);
            }
            break;
        case CTL_END:
        {
            int reason = GCD_KErrHostQuit;
            if (n >= 5)
            {
                reason = (int)(((gc_u32)p[1] << 24) | ((gc_u32)p[2] << 16) | ((gc_u32)p[3] << 8) | p[4]);
            }
            if (!d->temp_disc)
            {
                ev(d, GCD_EV1_HOST_DISCONNECTED, 0, GCD_KErrNone);
            }
            ev(d, GCD_EV1_END_GAME, 0, reason);
            leave_network(d, GC_LEFT_LEAVE);
            d->game_state = GCD_GS_OVER;
            d->conn_state = GCD_CS_NOT_CONNECTED;
            break;
        }
        case CTL_TEMP_DISC:
            if (!d->temp_disc)
            {
                ev(d, GCD_EV1_HOST_DISCONNECTED, 0, GCD_KErrNone);
                d->temp_disc = 2; /* 2: disconnected by the host, implicitly ready */
                d->conn_state = GCD_CS_NOT_CONNECTED;
            }
            break;
        case CTL_RECONNECTED:
            if (d->temp_disc)
            {
                d->temp_disc = 0;
                d->ready_sent = 0;
                d->conn_state = GCD_CS_CONNECTED;
                ev(d, GCD_EV1_HOST_CONNECTED, 0, GCD_KErrNone);
            }
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* GameComms 2.x logic                                                 */
/* ------------------------------------------------------------------ */

static void v2_peer_device(gcd_dev *d, int id, gcd_device *dev)
{
    memset(dev, 0, sizeof(*dev));
    dev->session_id = d->session_id;
    dev->slot = (gc_u8)id;
    copy_name(dev->name, d->peer[id].name);
}

static void v2_host_declare_peer(gcd_dev *d, int id)
{
    gcd_device dev;
    gcd_peer  *p = &d->peer[id];
    p->pending = 0;
    p->declared = 1;
    v2_peer_device(d, id, &dev);
    ev_dev(d, GCD_EV2_DEVICE_DECLARED, id, GCD_KErrNone, &dev);
}

static void v2_host_peer_joined(gcd_dev *d, int id)
{
    gcd_device dev;
    gcd_peer  *p = &d->peer[id];

    switch (d->accept_mode)
    {
        case GCD_ACCEPT_ALL:
            v2_host_declare_peer(d, id);
            break;
        case GCD_ACCEPT_CONFIRM:
            p->pending = 1;
            v2_peer_device(d, id, &dev);
            ev_dev(d, d->ui ? GCD_EV_UI_ADD_DEVICE : GCD_EV2_CONFIRM_DEVICE, id, 0, &dev);
            break;
        default:
            send_kick(d, id, GC_LEFT_KICKED); /* not accepting */
            memset(p, 0, sizeof(*p));
            break;
    }
}

static void v2_update_host_state(gcd_dev *d)
{
    int i, n = 0, c = 0;
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        n += d->peer[i].connected2;
        c += d->peer[i].connecting2;
    }
    d->conn_state = n ? GCD_CS_CONNECTED : c ? GCD_CS_CONNECTING : GCD_CS_NOT_CONNECTED;
}

static void v2_host_peer_lost(gcd_dev *d, int id, int err)
{
    gcd_peer *p = &d->peer[id];
    if (p->connected2)
    {
        ev(d, GCD_EV2_DISCONNECTED, id, err);
    }
    else if (p->connecting2)
    {
        ev(d, GCD_EV2_CONNECTED, id, err);
    }
    memset(p, 0, sizeof(*p));
    v2_update_host_state(d);
}

static void v2_host_ctl(gcd_dev *d, int id, const gc_u8 *p, size_t n)
{
    gcd_peer *peer = &d->peer[id];
    if (!peer->present || n < 1)
    {
        return;
    }
    switch (p[0])
    {
        case CTL2_CONNECT_ACK:
            if (peer->connecting2)
            {
                peer->connecting2 = 0;
                peer->connected2 = 1;
                v2_update_host_state(d);
                ev(d, GCD_EV2_CONNECTED, id, GCD_KErrNone);
            }
            break;
        case CTL2_DISCONNECT:
            if (peer->connected2)
            {
                peer->connected2 = 0;
                v2_update_host_state(d);
                ev(d, GCD_EV2_DISCONNECTED, id, GCD_KErrNone);
            }
            break;
        default:
            break;
    }
}

static void v2_client_ctl(gcd_dev *d, const gc_u8 *p, size_t n)
{
    if (n < 1)
    {
        return;
    }
    switch (p[0])
    {
        case CTL2_CONNECT:
            send_ctl(d, GC_SLOT_HOST, CTL2_CONNECT_ACK, NULL, 0);
            if (d->conn_state != GCD_CS_CONNECTED)
            {
                d->conn_state = GCD_CS_CONNECTED;
                ev(d, GCD_EV2_CONNECTED, 0, GCD_KErrNone);
            }
            break;
        case CTL2_DISCONNECT:
            if (d->conn_state == GCD_CS_CONNECTED)
            {
                d->conn_state = GCD_CS_NOT_CONNECTED;
                ev(d, GCD_EV2_DISCONNECTED, 0, GCD_KErrNone);
            }
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* message dispatch                                                    */
/* ------------------------------------------------------------------ */

static void report_discovered(gcd_dev *d, const gc_msg *m)
{
    gcd_event *e;
    int        i, n = 0;
    d->discover_once = 0;
    e = ev_new(d, GCD_EV_DISCOVERED);
    if (!e)
    {
        return;
    }
    for (i = 0; i < m->u.list.count && (size_t)(n + 1) * sizeof(gcd_device) <= sizeof(e->data); i++)
    {
        const gc_session_info *s = &m->u.list.s[i];
        gcd_device             dev;
        if (s->players >= s->max_players || s->state != GC_SS_LOBBY)
        {
            continue; /* full, other API generation, or not accepting players */
        }
        memset(&dev, 0, sizeof(dev));
        dev.session_id = s->session_id;
        dev.slot = GC_SLOT_HOST;
        copy_name(dev.name, s->host_name);
        memcpy(e->data + (size_t)n * sizeof(gcd_device), &dev, sizeof(dev));
        n++;
    }
    e->id = n;
    e->len = (gc_u16)((size_t)n * sizeof(gcd_device));
}

static void on_session_list(gcd_dev *d, const gc_msg *m)
{
    int i;
    if (d->discover_once)
    {
        report_discovered(d, m);
    }
    if (!d->selecting)
    {
        return;
    }
    d->n_sessions = 0;
    for (i = 0; i < m->u.list.count && d->n_sessions < GCD_MAX_SESSIONS; i++)
    {
        const gc_session_info *s = &m->u.list.s[i];
        gcd_device            *e;
        if (s->players >= s->max_players)
        {
            continue; /* full or incompatible */
        }
        e = &d->sessions[d->n_sessions++];
        e->session_id = s->session_id;
        e->slot = GC_SLOT_HOST;
        copy_name(e->name, s->host_name);
    }
    if (d->cfg.auto_join && d->n_sessions > 0)
    {
        gcd_select_session(d, d->sessions[0].session_id, d->now);
        return;
    }
    {
        gcd_event *e = ev_new(d, GCD_EV_SESSION_LIST);
        if (e)
        {
            e->id = d->n_sessions;
            memcpy(e->data, d->sessions, sizeof(gcd_device) * (size_t)d->n_sessions);
            e->len = (gc_u16)(sizeof(gcd_device) * (size_t)d->n_sessions);
        }
    }
}

static void on_join_result(gcd_dev *d, const gc_msg *m)
{
    int r = m->u.join_result.result;

    if (!d->joining)
    {
        return;
    }
    if (r == GC_R_PENDING)
    {
        return;
    }
    d->joining = 0;
    if (r != GC_R_OK)
    {
        int err = map_result(r);
        dlog(d, "join failed", r);
        if (d->cfg.api == GC_API_V1)
        {
            ev(d, GCD_EV1_HOST_CONNECTED, 0, err);
            d->conn_state = GCD_CS_NOT_CONNECTED;
        }
        else
        {
            d->conn_state = GCD_CS_NOT_CONNECTED;
            ev(d, GCD_EV2_CONNECTED, 0, err);
        }
        return;
    }
    d->session_open = 1;
    d->session_id = m->u.join_result.session_id;
    d->slot = m->u.join_result.slot;
    copy_name(d->host_name, m->u.join_result.host_name);
    dlog(d, "joined session as slot", d->slot);
    if (d->cfg.api == GC_API_V1)
    {
        d->conn_state = GCD_CS_CONNECTED;
        ev(d, GCD_EV1_HOST_CONNECTED, 0, GCD_KErrNone);
    }
    /* v2: stay CONNECTING until the host calls Connect() */
}

static void on_peer_joined(gcd_dev *d, const gc_msg *m)
{
    int       id = m->u.peer.slot;
    gcd_peer *p;

    if (d->role != GCD_ROLE_HOST || id < 1 || id >= GC_MAX_PLAYERS)
    {
        return;
    }
    p = &d->peer[id];
    if (d->cfg.api == GC_API_V1)
    {
        if (m->u.peer.rejoin && p->present)
        {
            /* client came back after losing its link (paused game) */
            p->ready = 1;
            return;
        }
        if (d->game_state != GCD_GS_OVER)
        {
            send_kick(d, id, GC_LEFT_KICKED); /* late joiner */
            return;
        }
        memset(p, 0, sizeof(*p));
        p->present = 1;
        p->connected = 1;
        copy_name(p->name, m->u.peer.name);
        ev_dev(d, GCD_EV1_CLIENT_CONNECTED, id, GCD_KErrNone, NULL == NULL ? &d->target : &d->target);
        {
            /* the event carries the client's name */
            gcd_event *e = &d->ev[(d->ev_head + d->ev_count - 1) % GCD_EVENT_QUEUE];
            if (d->ev_count && e->type == GCD_EV1_CLIENT_CONNECTED)
            {
                memset(&e->dev, 0, sizeof(e->dev));
                e->dev.slot = (gc_u8)id;
                copy_name(e->dev.name, p->name);
            }
        }
        v1_host_check_start(d);
    }
    else
    {
        if (m->u.peer.rejoin && p->present)
        {
            return;
        }
        memset(p, 0, sizeof(*p));
        p->present = 1;
        copy_name(p->name, m->u.peer.name);
        v2_host_peer_joined(d, id);
    }
}

static void on_peer_left(gcd_dev *d, const gc_msg *m)
{
    int       id = m->u.peer_left.slot;
    int       reason = m->u.peer_left.reason;
    gcd_peer *p;

    if (d->role != GCD_ROLE_HOST || id < 1 || id >= GC_MAX_PLAYERS || !d->peer[id].present)
    {
        return;
    }
    p = &d->peer[id];
    if (d->cfg.api == GC_API_V1)
    {
        int err = reason == GC_LEFT_LEAVE ? GCD_KErrNone : GCD_KErrDisconnected;
        if (d->game_state == GCD_GS_PAUSE && reason == GC_LEFT_LOST)
        {
            /* paused: tolerate, the slot is reserved for a rejoin */
            if (p->connected && !p->temp_disc)
            {
                ev(d, GCD_EV1_CLIENT_DISCONNECTED, id, err);
            }
            p->temp_disc = 1;
            p->ready = 0;
            return;
        }
        if (d->game_state == GCD_GS_OVER)
        {
            if (p->connected && !p->temp_disc)
            {
                ev(d, GCD_EV1_CLIENT_DISCONNECTED, id, err);
            }
            memset(p, 0, sizeof(*p));
            return;
        }
        v1_host_client_gone(d, id, 1, err);
    }
    else
    {
        v2_host_peer_lost(d, id, reason == GC_LEFT_LEAVE ? GCD_KErrNone : GCD_KErrDisconnected);
    }
}

static void on_session_ended(gcd_dev *d, const gc_msg *m)
{
    int reason = m->u.reason.reason;
    if (d->role != GCD_ROLE_CLIENT)
    {
        return;
    }
    if (d->cfg.api == GC_API_V1 && reason == GC_LEFT_LOST)
    {
        /* host removed us temporarily (DisconnectClient while paused) */
        if (!d->temp_disc)
        {
            ev(d, GCD_EV1_HOST_DISCONNECTED, 0, GCD_KErrNone);
        }
        d->temp_disc = 2;
        d->session_open = 0;
        d->conn_state = GCD_CS_NOT_CONNECTED;
        return;
    }
    if (d->cfg.api == GC_API_V1 && reason == GC_LEFT_KICKED)
    {
        int gs = d->game_state;
        ev(d, GCD_EV1_HOST_DISCONNECTED, 0, GCD_KErrNone);
        if (gs != GCD_GS_OVER)
        {
            ev(d, GCD_EV1_END_GAME, 0, GCD_KErrDisconnected);
        }
        d->session_open = 0;
        d->conn_state = GCD_CS_NOT_CONNECTED;
        d->game_state = GCD_GS_OVER;
        return;
    }
    session_lost(d, reason == GC_LEFT_HOST_CLOSED ? GCD_KErrNone : GCD_KErrDisconnected,
                 reason == GC_LEFT_HOST_CLOSED || reason == GC_LEFT_HOST_LOST);
}

static void on_data(gcd_dev *d, const gc_msg *m)
{
    int src = m->u.data.addr;

    if (!d->session_open)
    {
        return;
    }
    if (d->role == GCD_ROLE_HOST)
    {
        if (src < 1 || src >= GC_MAX_PLAYERS || !d->peer[src].present)
        {
            return;
        }
        if (m->u.data.channel == GC_CH_CONTROL)
        {
            if (d->cfg.api == GC_API_V1)
            {
                v1_host_ctl(d, src, m->u.data.payload, m->u.data.len);
            }
            else
            {
                v2_host_ctl(d, src, m->u.data.payload, m->u.data.len);
            }
            return;
        }
        if (d->cfg.api == GC_API_V1)
        {
            /* "the game must be in the play state" */
            if (d->game_state == GCD_GS_PLAY && !d->peer[src].temp_disc)
            {
                ev_data(d, GCD_EV1_DATA_FROM_CLIENT, src, m->u.data.payload, m->u.data.len);
            }
        }
        else if (d->peer[src].connected2)
        {
            ev_data(d, GCD_EV2_DATA, src, m->u.data.payload, m->u.data.len);
        }
    }
    else if (d->role == GCD_ROLE_CLIENT)
    {
        if (m->u.data.channel == GC_CH_CONTROL)
        {
            if (d->cfg.api == GC_API_V1)
            {
                v1_client_ctl(d, m->u.data.payload, m->u.data.len);
            }
            else
            {
                v2_client_ctl(d, m->u.data.payload, m->u.data.len);
            }
            return;
        }
        if (d->cfg.api == GC_API_V1)
        {
            if (d->game_state == GCD_GS_PLAY && !d->temp_disc)
            {
                ev_data(d, GCD_EV1_DATA_FROM_HOST, 0, m->u.data.payload, m->u.data.len);
            }
        }
        else if (d->conn_state == GCD_CS_CONNECTED)
        {
            ev_data(d, GCD_EV2_DATA, 0, m->u.data.payload, m->u.data.len);
        }
    }
}

static void on_msg(void *ctx, const gc_u8 *buf, size_t len)
{
    gcd_dev *d = (gcd_dev *)ctx;
    gc_msg   m;

    if (!gc_msg_decode(buf, len, &m))
    {
        dlog(d, "malformed message", len ? buf[0] : 0);
        return;
    }
    switch (m.type)
    {
        case GC_MT_REGISTERED:
            if (m.u.registered.result != GC_R_OK)
            {
                dlog(d, "registration failed", m.u.registered.result);
                d->registered = 0;
                d->reg_sent = 0;
                d->t_retry = d->now + RETRY_MS;
                if (d->role == GCD_ROLE_CLIENT && (d->joining || d->selecting))
                {
                    session_lost(d, map_result(m.u.registered.result), 0);
                }
                break;
            }
            if (d->registered && (d->session_open || d->joining))
            {
                /* unsolicited: the bridge re-registered us after a fresh
                 * server link - everything on the server is gone */
                dlog(d, "server state lost", 0);
                session_lost(d, GCD_KErrDisconnected, 0);
            }
            d->registered = 1;
            kick_role(d);
            break;
        case GC_MT_HOST_OPENED:
            if (d->want_role != GCD_ROLE_HOST)
            {
                break;
            }
            if (m.u.host_opened.result == GC_R_OK)
            {
                d->session_open = 1;
                d->session_id = m.u.host_opened.session_id;
                d->slot = GC_SLOT_HOST;
                dlog(d, "hosting session", (int)d->session_id);
                if (d->cfg.api == GC_API_V2 && d->accept_mode == GCD_ACCEPT_NONE)
                {
                    send_session_state(d, GC_SS_PLAYING); /* closed until accepting */
                }
            }
            else
            {
                dlog(d, "host open failed", m.u.host_opened.result);
                d->t_retry = d->now + RETRY_MS;
            }
            break;
        case GC_MT_SESSION_LIST:
            on_session_list(d, &m);
            break;
        case GC_MT_JOIN_RESULT:
            on_join_result(d, &m);
            break;
        case GC_MT_PEER_JOINED:
            on_peer_joined(d, &m);
            break;
        case GC_MT_PEER_LEFT:
            on_peer_left(d, &m);
            break;
        case GC_MT_SESSION_ENDED:
            on_session_ended(d, &m);
            break;
        case GC_MT_DATA:
            on_data(d, &m);
            break;
        case GC_MT_BRIDGE_STATUS:
            d->server_up = m.u.bridge.server == GC_BS_UP;
            break;
        case GC_MT_ERROR:
            dlog(d, "server error", m.u.error.code);
            break;
        default:
            break;
    }
}

static void on_write(void *ctx, const gc_u8 *data, size_t len)
{
    gcd_dev *d = (gcd_dev *)ctx;
    if (d->transport_up && d->ops.write)
    {
        d->ops.write(d->ctx, data, len);
    }
}

static void send_register(gcd_dev *d)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_REGISTER;
    m.u.reg.api = d->cfg.api;
    m.u.reg.variant = d->cfg.variant;
    copy_name(m.u.reg.name, d->cfg.name);
    send_msg(d, &m);
    d->reg_sent = 1;
}

static void on_state(void *ctx, int ev_, int reason)
{
    gcd_dev *d = (gcd_dev *)ctx;
    switch (ev_)
    {
        case GC_LE_UP_FRESH:
            dlog(d, "bridge link up", 0);
            if (d->registered && (d->session_open || d->joining))
            {
                session_lost(d, GCD_KErrDisconnected, 0); /* bridge rebooted */
            }
            d->registered = 0;
            send_register(d);
            break;
        case GC_LE_UP_RESUMED:
            dlog(d, "bridge link resumed", 0);
            break;
        case GC_LE_DOWN:
        case GC_LE_CLOSED:
            dlog(d, "bridge link lost", reason);
            ev(d, GCD_EV_TRANSPORT, 0, GCD_KErrDisconnected);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* public: lifecycle / transport                                       */
/* ------------------------------------------------------------------ */

void gcd_default_config(gcd_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->api = GC_API_V2;
    cfg->variant = 6;
    copy_name(cfg->name, "N-Gage");
    cfg->link_id = 0x4E474147;
    cfg->discover_ms = 2000;
}

void gcd_init(gcd_dev *d, const gcd_config *cfg, const gcd_ops *ops, void *ctx)
{
    gc_link_cfg lc;
    gc_link_ops lo;

    memset(d, 0, sizeof(*d));
    d->cfg = *cfg;
    d->ops = *ops;
    d->ctx = ctx;
    reset_role(d);
    gc_link_default_cfg(&lc, GC_ROLE_DEVICE, 1);
    lo.write = on_write;
    lo.on_msg = on_msg;
    lo.on_state = on_state;
    lo.on_hello = NULL;
    lo.can_accept = NULL;
    gc_link_init(&d->link, &lc, &lo, d);
}

void gcd_transport_up(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    d->transport_up = 1;
    gc_link_start(&d->link, d->cfg.link_id, now);
}

void gcd_transport_input(gcd_dev *d, const gc_u8 *data, size_t len, gc_u32 now)
{
    d->now = now;
    gc_link_input(&d->link, data, len, now);
}

void gcd_transport_down(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    d->transport_up = 0;
    gc_link_transport_lost(&d->link, now);
}

void gcd_poll(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    gc_link_poll(&d->link, now);
    if (!gc_link_is_up(&d->link))
    {
        return;
    }
    if (!d->registered && (!d->reg_sent || !GC_TIME_BEFORE(now, d->t_retry)))
    {
        if (!GC_TIME_BEFORE(now, d->t_retry))
        {
            send_register(d);
            d->t_retry = now + RETRY_MS;
        }
        return;
    }
    if (d->want_role == GCD_ROLE_HOST && !d->session_open && !GC_TIME_BEFORE(now, d->t_retry))
    {
        do_host_open(d);
    }
    if ((d->selecting || d->discover_once) && !GC_TIME_BEFORE(now, d->t_discover))
    {
        do_discover(d);
    }
}

void gcd_discover_once(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    d->discover_once = 1;
    if (ready(d) && !d->selecting)
    {
        do_discover(d);
    }
}

gc_u32 gcd_next_timeout(gcd_dev *d, gc_u32 now)
{
    gc_u32 t = gc_link_next_timeout(&d->link, now);
    return t > 500 ? 500 : t;
}

/* ------------------------------------------------------------------ */
/* public: queries                                                     */
/* ------------------------------------------------------------------ */

int gcd_role(const gcd_dev *d)
{
    return d->role;
}

int gcd_connect_state(const gcd_dev *d)
{
    return d->conn_state;
}

int gcd_game_state(const gcd_dev *d)
{
    return d->game_state;
}

int gcd_is_selecting(const gcd_dev *d)
{
    return d->selecting;
}

static void start_role(gcd_dev *d, int role, gc_u32 now)
{
    d->now = now;
    if (d->role != GCD_ROLE_IDLE)
    {
        /* "any previous connections are deleted" */
        leave_network(d, GC_LEFT_LEAVE);
    }
    reset_role(d);
    d->role = role;
    d->want_role = role;
    d->t_retry = now;
}

void gcd_select_session(gcd_dev *d, gc_u32 session_id, gc_u32 now)
{
    int i;
    d->now = now;
    if (!d->selecting)
    {
        return;
    }
    d->selecting = 0;
    ev(d, GCD_EV_UI_CLOSE, 0, 0);
    if (session_id == 0)
    {
        if (d->cfg.api == GC_API_V1)
        {
            ev(d, GCD_EV1_HOST_SELECTED, 0, GCD_KErrCancel);
            ev(d, GCD_EV1_HOST_CONNECTED, 0, GCD_KErrCancel);
        }
        return;
    }
    memset(&d->target, 0, sizeof(d->target));
    d->target.session_id = session_id;
    for (i = 0; i < d->n_sessions; i++)
    {
        if (d->sessions[i].session_id == session_id)
        {
            d->target = d->sessions[i];
        }
    }
    d->target_set = 1;
    if (d->cfg.api == GC_API_V1)
    {
        ev(d, GCD_EV1_HOST_SELECTED, 0, GCD_KErrNone);
        d->conn_state = GCD_CS_CONNECTING;
        do_join(d, session_id, 0);
    }
    else
    {
        gcd_v2_declare(d, &d->target, now);
        gcd_v2_connect(d, GCD_ALL, now);
    }
}

/* ------------------------------------------------------------------ */
/* public: GameComms 1.x                                               */
/* ------------------------------------------------------------------ */

void gcd_v1_start_host(gcd_dev *d, gc_u16 start_players, gc_u16 min_players, gc_u32 now)
{
    start_role(d, GCD_ROLE_HOST, now);
    d->start_players = start_players < 1 ? 1 : start_players > GC_MAX_PLAYERS ? GC_MAX_PLAYERS : start_players;
    d->min_players = min_players;
    d->conn_state = GCD_CS_CONNECTING;
    kick_role(d);
}

void gcd_v1_start_client(gcd_dev *d, gc_u32 now)
{
    start_role(d, GCD_ROLE_CLIENT, now);
    d->selecting = 1;
    d->t_discover = now;
    kick_role(d);
}

void gcd_v1_disconnect(gcd_dev *d, gc_u32 now)
{
    int i;
    d->now = now;
    if (d->role == GCD_ROLE_CLIENT)
    {
        if (d->selecting)
        {
            d->selecting = 0;
            ev(d, GCD_EV_UI_CLOSE, 0, 0);
            return;
        }
        if (!d->session_open || d->temp_disc)
        {
            return;
        }
        if (d->game_state == GCD_GS_PAUSE)
        {
            send_ctl(d, GC_SLOT_HOST, CTL_TEMP_DISC, NULL, 0);
            d->temp_disc = 1;
            d->conn_state = GCD_CS_NOT_CONNECTED;
            return;
        }
        if (d->game_state == GCD_GS_PLAY)
        {
            send_ctl(d, GC_SLOT_HOST, CTL_TEMP_DISC, NULL, 0);
            ev(d, GCD_EV1_END_GAME, 0, GCD_KErrDisconnected);
        }
        leave_network(d, GC_LEFT_LEAVE);
        d->game_state = GCD_GS_OVER;
        d->conn_state = GCD_CS_NOT_CONNECTED;
    }
    else if (d->role == GCD_ROLE_HOST)
    {
        if (d->game_state == GCD_GS_PAUSE)
        {
            for (i = 1; i < GC_MAX_PLAYERS; i++)
            {
                if (d->peer[i].present && d->peer[i].connected && !d->peer[i].temp_disc)
                {
                    send_ctl(d, (gc_u8)i, CTL_TEMP_DISC, NULL, 0);
                    d->peer[i].temp_disc = 1;
                    d->peer[i].ready = 1; /* implicitly ready: the host left */
                }
            }
            d->conn_state = GCD_CS_NOT_CONNECTED;
            return;
        }
        if (d->game_state == GCD_GS_PLAY)
        {
            v1_host_end(d, GCD_KErrHostQuit, 0);
            ev(d, GCD_EV1_END_GAME, 0, GCD_KErrDisconnected);
            return;
        }
        v1_host_end(d, GCD_KErrHostQuit, 0);
    }
}

int gcd_v1_disconnect_client(gcd_dev *d, int id, gc_u32 now)
{
    gcd_peer *p;
    d->now = now;
    if (d->role != GCD_ROLE_HOST)
    {
        return GCD_KErrNotReady;
    }
    if (id <= 0)
    {
        return GCD_KErrBadHandle;
    }
    if (id >= GC_MAX_PLAYERS || !d->peer[id].present)
    {
        return GCD_KErrNotFound;
    }
    p = &d->peer[id];
    if (d->game_state == GCD_GS_PAUSE)
    {
        /* temporary: the client may be reconnected with ReconnectL */
        send_kick(d, id, GC_LEFT_LOST);
        p->temp_disc = 1;
        p->ready = 0;
        return GCD_KErrNone;
    }
    send_kick(d, id, GC_LEFT_KICKED);
    /* "ClientDisconnected is not called because the host knows" */
    v1_host_client_gone(d, id, 0, GCD_KErrNone);
    return GCD_KErrNone;
}

int gcd_v1_send(gcd_dev *d, int dst, const gc_u8 *data, size_t len, gc_u32 now)
{
    gc_u8 buf[GC_MAX_DATA + 8];
    d->now = now;
    if (d->game_state == GCD_GS_PAUSE)
    {
        return GCD_KErrGamePaused;
    }
    if (d->game_state != GCD_GS_PLAY)
    {
        return GCD_KErrGameOver;
    }
    if (len > GC_MAX_DATA)
    {
        return GCD_KErrOverflow;
    }
    if (d->role == GCD_ROLE_HOST)
    {
        if (dst != GC_SLOT_ALL)
        {
            if (dst < 1 || dst >= GC_MAX_PLAYERS || !d->peer[dst].present)
            {
                return GCD_KErrBadHandle;
            }
            if (d->peer[dst].temp_disc)
            {
                return GCD_KErrDisconnected;
            }
        }
    }
    else
    {
        if (d->temp_disc)
        {
            return GCD_KErrDisconnected;
        }
        dst = GC_SLOT_HOST;
    }
    return send_raw(d, buf, gc_msg_encode_data((gc_u8)dst, GC_CH_GAME, data, len, buf, sizeof(buf)));
}

int gcd_v1_pause(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    if (d->game_state == GCD_GS_OVER)
    {
        return GCD_KErrGameOver;
    }
    if (d->game_state == GCD_GS_PAUSE)
    {
        return GCD_KErrNone;
    }
    if (d->role == GCD_ROLE_HOST)
    {
        v1_host_pause(d, 0);
    }
    else
    {
        gc_u8 o = d->slot;
        d->game_state = GCD_GS_PAUSE;
        send_ctl(d, GC_SLOT_HOST, CTL_PAUSE, &o, 1);
    }
    return GCD_KErrNone;
}

int gcd_v1_continue(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    if (d->game_state == GCD_GS_OVER)
    {
        return GCD_KErrGameOver;
    }
    if (d->game_state != GCD_GS_PAUSE)
    {
        return GCD_KErrNone;
    }
    if (d->role == GCD_ROLE_HOST)
    {
        if (d->conn_state != GCD_CS_CONNECTED && connected_clients(d) == 0)
        {
            return GCD_KErrNotReady;
        }
        d->host_cont = 1;
        v1_host_try_continue(d);
        return GCD_KErrNone;
    }
    if (d->temp_disc)
    {
        return GCD_KErrNotReady; /* ReconnectL first */
    }
    return send_ctl(d, GC_SLOT_HOST, CTL_CONTINUE_REQ, NULL, 0);
}

int gcd_v1_reconnect(gcd_dev *d, int must_all, gc_u32 now)
{
    int i, all = 1;
    d->now = now;
    if (d->role == GCD_ROLE_IDLE)
    {
        return GCD_KErrNotReady;
    }
    if (d->role == GCD_ROLE_CLIENT)
    {
        if (!d->temp_disc)
        {
            return GCD_KErrNone;
        }
        if (!d->session_open)
        {
            /* removed by the host while paused: rejoin our slot */
            do_join(d, d->session_id, 1);
            d->temp_disc = 1;
        }
        if (!d->ready_sent)
        {
            send_ctl(d, GC_SLOT_HOST, CTL_READY, NULL, 0);
            d->ready_sent = 1;
        }
        return GCD_KErrNone;
    }
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        gcd_peer *p = &d->peer[i];
        if (!p->present || !p->temp_disc)
        {
            continue;
        }
        if (p->ready)
        {
            send_ctl(d, (gc_u8)i, CTL_RECONNECTED, NULL, 0);
            p->temp_disc = 0;
            p->ready = 0;
            ev_dev(d, GCD_EV1_CLIENT_CONNECTED, i, GCD_KErrNone, &d->target);
            {
                gcd_event *e = &d->ev[(d->ev_head + d->ev_count - 1) % GCD_EVENT_QUEUE];
                memset(&e->dev, 0, sizeof(e->dev));
                e->dev.slot = (gc_u8)i;
                copy_name(e->dev.name, p->name);
            }
        }
        else
        {
            all = 0;
            ev(d, GCD_EV1_CLIENT_CONNECTED, i, GCD_KErrNotReady);
            if (must_all)
            {
                break;
            }
        }
    }
    if (all)
    {
        d->conn_state = GCD_CS_CONNECTED;
    }
    return GCD_KErrNone;
}

int gcd_v1_end(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    if (d->role == GCD_ROLE_HOST)
    {
        v1_host_end(d, GCD_KErrHostQuit, 0);
        return GCD_KErrNone;
    }
    if (d->role == GCD_ROLE_CLIENT)
    {
        if (d->session_open)
        {
            send_ctl(d, GC_SLOT_HOST, CTL_CLIENT_END, NULL, 0);
            leave_network(d, GC_LEFT_LEAVE);
        }
        d->game_state = GCD_GS_OVER;
        d->conn_state = GCD_CS_NOT_CONNECTED;
        return GCD_KErrNone;
    }
    return GCD_KErrNotReady;
}

/* ------------------------------------------------------------------ */
/* public: GameComms 2.x                                               */
/* ------------------------------------------------------------------ */

void gcd_v2_start(gcd_dev *d, int host, int ui, gc_u32 now)
{
    start_role(d, host ? GCD_ROLE_HOST : GCD_ROLE_CLIENT, now);
    d->ui = ui;
    if (host)
    {
        /* the original only listens with the built-in UI; otherwise the
         * game opens the door with SetHostAcceptMode() */
        d->accept_mode = ui ? GCD_ACCEPT_CONFIRM : GCD_ACCEPT_NONE;
    }
    else if (ui)
    {
        d->selecting = 1;
        d->t_discover = now;
    }
    kick_role(d);
}

int gcd_v2_declare(gcd_dev *d, const gcd_device *dev, gc_u32 now)
{
    int i;
    d->now = now;
    if (d->role == GCD_ROLE_CLIENT)
    {
        if (d->conn_state != GCD_CS_NOT_CONNECTED && d->session_open)
        {
            ev_dev(d, GCD_EV2_DEVICE_DECLARED, 0, GCD_KErrInUse, dev);
            return GCD_KErrInUse;
        }
        d->target = *dev;
        d->target_set = 1;
        ev_dev(d, GCD_EV2_DEVICE_DECLARED, 0, GCD_KErrNone, dev);
        return GCD_KErrNone;
    }
    if (d->role != GCD_ROLE_HOST)
    {
        return GCD_KErrNotReady;
    }
    /* host: accept a client that asked to join */
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        gcd_peer *p = &d->peer[i];
        if (!p->present)
        {
            continue;
        }
        if ((dev->slot == i && (dev->session_id == d->session_id || dev->session_id == 0)) ||
            (dev->slot == GC_SLOT_NONE && name_eq(dev->name, p->name)))
        {
            if (p->declared)
            {
                gcd_device x;
                v2_peer_device(d, i, &x);
                ev_dev(d, GCD_EV2_DEVICE_DECLARED, i, GCD_KErrAlreadyExists, &x);
                return GCD_KErrNone;
            }
            v2_host_declare_peer(d, i);
            return GCD_KErrNone;
        }
    }
    {
        /* unknown device: report like the original's failed declaration */
        ev_dev(d, GCD_EV2_DEVICE_DECLARED, GCD_ALL, GCD_KErrNotFound, dev);
    }
    return GCD_KErrNone;
}

int gcd_v2_connect(gcd_dev *d, int id, gc_u32 now)
{
    int i, any = 0;
    d->now = now;
    if (d->role == GCD_ROLE_CLIENT)
    {
        if (d->conn_state != GCD_CS_NOT_CONNECTED)
        {
            return GCD_KErrNotReady;
        }
        if (!d->target_set)
        {
            return GCD_KErrNotFound;
        }
        d->conn_state = GCD_CS_CONNECTING;
        d->joining = 1;
        if (ready(d))
        {
            /* a session id of 0 (device declared from a real Bluetooth
             * search) joins the first compatible session */
            do_join(d, d->target.session_id, 0);
        }
        return GCD_KErrNone;
    }
    if (d->role != GCD_ROLE_HOST)
    {
        return GCD_KErrNotReady;
    }
    /* host: stop accepting and connect the declared clients */
    if (id == GCD_ALL && d->accept_mode != GCD_ACCEPT_NONE)
    {
        d->accept_mode = GCD_ACCEPT_NONE;
        send_session_state(d, GC_SS_PLAYING);
        ev(d, GCD_EV_UI_CLOSE, 0, 0);
    }
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        gcd_peer *p = &d->peer[i];
        if (id != GCD_ALL && id != i)
        {
            continue;
        }
        if (!p->present || !p->declared)
        {
            if (id != GCD_ALL)
            {
                ev(d, GCD_EV2_CONNECTED, i, GCD_KErrNotCommsHost);
                return GCD_KErrDisconnected;
            }
            continue;
        }
        any = 1;
        if (p->connected2)
        {
            ev(d, GCD_EV2_CONNECTED, i, GCD_KErrInUse);
            continue;
        }
        if (!p->connecting2)
        {
            p->connecting2 = 1;
            send_ctl(d, (gc_u8)i, CTL2_CONNECT, NULL, 0);
        }
    }
    v2_update_host_state(d);
    return any ? GCD_KErrNone : GCD_KErrNotFound;
}

int gcd_v2_disconnect(gcd_dev *d, int id, int permanently, gc_u32 now)
{
    int i;
    d->now = now;
    if (d->role == GCD_ROLE_CLIENT)
    {
        int was = d->conn_state;
        if (d->session_open)
        {
            send_ctl_i32(d, GC_SLOT_HOST, CTL2_DISCONNECT, 0);
            leave_network(d, GC_LEFT_LEAVE);
        }
        d->joining = 0;
        d->selecting = 0;
        d->conn_state = GCD_CS_NOT_CONNECTED;
        if (was == GCD_CS_CONNECTED)
        {
            ev(d, GCD_EV2_DISCONNECTED, 0, GCD_KErrNone);
        }
        return GCD_KErrNone;
    }
    if (d->role != GCD_ROLE_HOST)
    {
        return GCD_KErrNotReady;
    }
    for (i = 1; i < GC_MAX_PLAYERS; i++)
    {
        gcd_peer *p = &d->peer[i];
        if ((id != GCD_ALL && id != i) || !p->present)
        {
            continue;
        }
        if (p->connected2 || p->connecting2)
        {
            send_ctl_i32(d, (gc_u8)i, CTL2_DISCONNECT, 0);
            if (p->connected2)
            {
                ev(d, GCD_EV2_DISCONNECTED, i, GCD_KErrNone);
            }
            p->connected2 = 0;
            p->connecting2 = 0;
        }
        if (permanently)
        {
            send_kick(d, i, GC_LEFT_KICKED);
            memset(p, 0, sizeof(*p));
        }
    }
    v2_update_host_state(d);
    return GCD_KErrNone;
}

int gcd_v2_set_accept_mode(gcd_dev *d, int mode, gc_u32 now)
{
    int i;
    d->now = now;
    if (d->role != GCD_ROLE_HOST)
    {
        return GCD_KErrNotReady;
    }
    if (d->conn_state != GCD_CS_NOT_CONNECTED)
    {
        return GCD_KErrNotReady;
    }
    if (mode < GCD_ACCEPT_ALL || mode > GCD_ACCEPT_NONE)
    {
        return GCD_KErrArgument;
    }
    d->accept_mode = mode;
    if (d->session_open)
    {
        send_session_state(d, mode == GCD_ACCEPT_NONE ? GC_SS_PLAYING : GC_SS_LOBBY);
    }
    if (mode == GCD_ACCEPT_NONE)
    {
        ev(d, GCD_EV_UI_CLOSE, 0, 0);
    }
    else
    {
        /* players that are already waiting are handled by the new mode */
        for (i = 1; i < GC_MAX_PLAYERS; i++)
        {
            if (d->peer[i].present && !d->peer[i].declared && !d->peer[i].pending)
            {
                v2_host_peer_joined(d, i);
            }
            else if (mode == GCD_ACCEPT_ALL && d->peer[i].pending)
            {
                v2_host_declare_peer(d, i);
            }
        }
    }
    return GCD_KErrNone;
}

int gcd_v2_accept_mode(const gcd_dev *d)
{
    return d->accept_mode;
}

int gcd_v2_send(gcd_dev *d, int id, const gc_u8 *data, size_t len, gc_u32 now)
{
    gc_u8 buf[GC_MAX_DATA + 8];
    d->now = now;
    if (len > GC_MAX_DATA)
    {
        return GCD_KErrOverflow;
    }
    if (d->role == GCD_ROLE_CLIENT)
    {
        if (d->conn_state != GCD_CS_CONNECTED)
        {
            return GCD_KErrNotFound;
        }
        id = GC_SLOT_HOST;
    }
    else if (d->role == GCD_ROLE_HOST)
    {
        if (id == GCD_ALL)
        {
            int i, r = GCD_KErrNone, n = 0;
            for (i = 1; i < GC_MAX_PLAYERS; i++)
            {
                if (d->peer[i].connected2)
                {
                    int e = send_raw(d, buf, gc_msg_encode_data((gc_u8)i, GC_CH_GAME, data, len, buf, sizeof(buf)));
                    n++;
                    if (e != GCD_KErrNone)
                    {
                        r = e;
                    }
                }
            }
            return r;
        }
        if (id < 1 || id >= GC_MAX_PLAYERS || !d->peer[id].connected2)
        {
            return GCD_KErrNotFound;
        }
    }
    else
    {
        return GCD_KErrNotReady;
    }
    return send_raw(d, buf, gc_msg_encode_data((gc_u8)id, GC_CH_GAME, data, len, buf, sizeof(buf)));
}

void gcd_end_session(gcd_dev *d, gc_u32 now)
{
    d->now = now;
    leave_network(d, GC_LEFT_LEAVE);
    reset_role(d);
}
