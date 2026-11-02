/** @file sim.c  - see sim.h.  SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"

static unsigned int sim_seed = 4711;
static unsigned int srand_(void)
{
    sim_seed = sim_seed * 1103515245u + 12345u;
    return (sim_seed >> 8) & 0xFFFFFF;
}

static void pipe_put(sim_pipe *p, const gc_u8 *d, size_t n)
{
    size_t i;
    if (!p->up || p->len + n > SIM_PIPE_CAP)
    {
        return;
    }
    for (i = 0; i < n; i++)
    {
        gc_u8 c = d[i];
        if (p->corrupt_permille && (int)(srand_() % 1000) < p->corrupt_permille)
        {
            c ^= (gc_u8)(1 + srand_() % 255);
        }
        p->buf[p->len++] = c;
    }
}

/* ---- device ops ---- */
static void dev_write(void *ctx, const gc_u8 *d, size_t n) { pipe_put(&((sim_node *)ctx)->spp_to_bridge, d, n); }
static void dev_log(void *ctx, const char *l)
{
    sim_node *s = (sim_node *)ctx;
    if (getenv("SIM_LOG")) printf("      [%s dev] %s\n", s->name, l);
}
static void dev_wake(void *ctx) { (void)ctx; }

/* ---- bridge ops ---- */
static void br_dev_write(void *ctx, const gc_u8 *d, size_t n) { pipe_put(&((sim_node *)ctx)->spp_to_dev, d, n); }
static void br_srv_write(void *ctx, const gc_u8 *d, size_t n) { pipe_put(&((sim_node *)ctx)->tcp_to_server, d, n); }
static void br_srv_connect(void *ctx) { ((sim_node *)ctx)->tcp_connect_req = 1; }
static void br_srv_disconnect(void *ctx) { ((sim_node *)ctx)->tcp_close_req = 1; }
static void br_log(void *ctx, const char *l)
{
    sim_node *s = (sim_node *)ctx;
    if (getenv("SIM_LOG")) printf("      [%s bridge] %s\n", s->name, l);
}

/* ---- server ops ---- */
static void srv_write(void *ctx, gcs_conn *c, const gc_u8 *d, size_t n) { (void)ctx; pipe_put(&((sim_node *)c->net)->tcp_to_bridge, d, n); }
static void srv_close(void *ctx, gcs_conn *c) { (void)ctx; ((sim_node *)c->net)->srv_close_req = 1; }
static void srv_log(void *ctx, int lvl, const char *l) { (void)ctx; (void)lvl; if (getenv("SIM_LOG")) printf("      [server] %s\n", l); }

static void node_setup(sim_world *w, sim_node *s, int i, gc_u8 api, gc_u32 uid)
{
    gcd_config dc;
    gcd_ops    dops;
    bridge_cfg bc;
    bridge_io  bio;
    gc_u8      addr[6] = {0x00, 0x0e, 0x6d, 0, 0, 0};

    memset(s, 0, sizeof(*s));
    s->idx = i;
    sprintf(s->name, "ngage%d", i);
    gcd_default_config(&dc);
    dc.api = api;
    dc.variant = api == GC_API_V1 ? 1 : 6;
    dc.game_uid = uid;
    strcpy(dc.name, s->name);
    dc.link_id = 0x1000 + (gc_u32)i;
    dops.write = dev_write;
    dops.log = dev_log;
    dops.wake = dev_wake;
    gcd_init(&s->dev, &dc, &dops, s);

    bridge_default_cfg(&bc);
    bc.server_link_id = 0x2000 + (gc_u32)i;
    bio.dev_write = br_dev_write;
    bio.srv_write = br_srv_write;
    bio.srv_connect = br_srv_connect;
    bio.srv_disconnect = br_srv_disconnect;
    bio.log = br_log;
    bridge_init(&s->br, &bc, &bio, s);

    s->spp_to_bridge.up = s->spp_to_dev.up = 1;
    s->spp_to_bridge.chunk = s->spp_to_dev.chunk = 23; /* small BT reads */
    s->tcp_to_bridge.chunk = 700;
    /* N-Gage pairs with its bridge */
    addr[5] = (gc_u8)i;
    bridge_dev_connected(&s->br, addr, w->now);
    gcd_transport_up(&s->dev, w->now);
    bridge_set_wifi(&s->br, 1, -50, w->now);
}

void sim_init(sim_world *w, int count, gc_u8 api, gc_u32 uid)
{
    gcs_config cfg;
    gcs_io     io;
    int        i;

    memset(w, 0, sizeof(*w));
    w->now = 100000;
    w->count = count;
    gcs_default_config(&cfg);
    cfg.link_grace_ms = 8000;
    cfg.reserve_ms = 30000;
    cfg.log_level = 3;
    io.write = srv_write;
    io.close = srv_close;
    io.log = srv_log;
    w->core = gcs_core_create(&cfg, &io, w, 99);
    for (i = 0; i < count; i++)
    {
        node_setup(w, &w->n[i], i, api, uid);
    }
}

void sim_free(sim_world *w)
{
    gcs_core_destroy(w->core);
}

static void deliver(sim_pipe *p, void (*fn)(void *, const gc_u8 *, size_t, gc_u32), void *obj, gc_u32 now)
{
    static gc_u8 tmp[SIM_PIPE_CAP];
    size_t       n = p->len, k = 0;
    if (!n)
    {
        return;
    }
    memcpy(tmp, p->buf, n);
    p->len = 0;
    while (k < n)
    {
        size_t c = p->chunk ? 1 + srand_() % (size_t)p->chunk : n - k;
        if (k + c > n)
        {
            c = n - k;
        }
        fn(obj, tmp + k, c, now);
        k += c;
    }
}

static void f_bridge_dev(void *o, const gc_u8 *d, size_t n, gc_u32 t) { bridge_dev_input((bridge *)o, d, n, t); }
static void f_dev(void *o, const gc_u8 *d, size_t n, gc_u32 t) { gcd_transport_input((gcd_dev *)o, d, n, t); }
static void f_bridge_srv(void *o, const gc_u8 *d, size_t n, gc_u32 t) { bridge_srv_input((bridge *)o, d, n, t); }
static sim_world *g_w;
static gcs_conn  *g_conn;
static void f_srv(void *o, const gc_u8 *d, size_t n, gc_u32 t) { (void)o; gcs_core_conn_input(g_w->core, g_conn, d, n, t); }

static void tcp_disconnect(sim_world *w, sim_node *s)
{
    if (s->tcp_to_server.up)
    {
        s->tcp_to_server.up = s->tcp_to_bridge.up = 0;
        s->tcp_to_server.len = s->tcp_to_bridge.len = 0;
        gcs_core_conn_closed(w->core, &s->conn, w->now);
        bridge_srv_disconnected(&s->br, w->now);
    }
}

void sim_run(sim_world *w, int ms)
{
    int step, i;
    gcd_event e;

    for (step = 0; step < ms / 10; step++)
    {
        w->now += 10;
        for (i = 0; i < w->count; i++)
        {
            sim_node *s = &w->n[i];
            if (s->tcp_connect_req)
            {
                s->tcp_connect_req = 0;
                if (!s->tcp_to_server.up)
                {
                    s->tcp_to_server.up = s->tcp_to_bridge.up = 1;
                    s->conn.net = s;
                    s->conn.peer = NULL;
                    gcs_core_conn_open(w->core, &s->conn, w->now);
                    bridge_srv_connected(&s->br, w->now);
                }
            }
            if (s->tcp_close_req || s->srv_close_req)
            {
                s->tcp_close_req = s->srv_close_req = 0;
                tcp_disconnect(w, s);
            }
            deliver(&s->spp_to_bridge, f_bridge_dev, &s->br, w->now);
            deliver(&s->spp_to_dev, f_dev, &s->dev, w->now);
            g_w = w;
            g_conn = &s->conn;
            if (s->tcp_to_server.up)
            {
                deliver(&s->tcp_to_server, f_srv, NULL, w->now);
            }
            deliver(&s->tcp_to_bridge, f_bridge_srv, &s->br, w->now);
            gcd_poll(&s->dev, w->now);
            bridge_poll(&s->br, w->now);
            while (gcd_pop_event(&s->dev, &e))
            {
                if (getenv("SIM_LOG"))
                {
                    printf("      [%s] EVENT type %d id %d err %d len %u\n", s->name, e.type, e.id, e.err, e.len);
                }
                if (s->n_ev < SIM_MAX_EVT)
                {
                    s->ev[s->n_ev++] = e;
                }
            }
        }
        gcs_core_poll(w->core, w->now);
    }
}

int sim_count(sim_world *w, int i, int type)
{
    int k, n = 0;
    for (k = 0; k < w->n[i].n_ev; k++)
    {
        n += w->n[i].ev[k].type == type;
    }
    return n;
}

gcd_event *sim_last(sim_world *w, int i, int type)
{
    int k;
    for (k = w->n[i].n_ev - 1; k >= 0; k--)
    {
        if (w->n[i].ev[k].type == type)
        {
            return &w->n[i].ev[k];
        }
    }
    return NULL;
}

void sim_clear(sim_world *w, int i)
{
    w->n[i].n_ev = 0;
}

void sim_tcp_drop(sim_world *w, int i)
{
    tcp_disconnect(w, &w->n[i]);
}

void sim_bridge_reboot(sim_world *w, int i)
{
    sim_node  *s = &w->n[i];
    bridge_cfg bc;
    bridge_io  io = s->br.io;
    gc_u8      addr[6];
    tcp_disconnect(w, s);
    memcpy(addr, s->br.dev_addr, 6);
    bridge_default_cfg(&bc);
    bc.server_link_id = 0x3000 + (gc_u32)i; /* new random id after boot */
    gcd_transport_down(&s->dev, w->now);
    s->spp_to_bridge.len = s->spp_to_dev.len = 0;
    bridge_init(&s->br, &bc, &io, s);
    bridge_dev_connected(&s->br, addr, w->now);
    bridge_set_wifi(&s->br, 1, -50, w->now);
    gcd_transport_up(&s->dev, w->now);
}

void sim_spp_drop(sim_world *w, int i)
{
    sim_node *s = &w->n[i];
    s->spp_to_bridge.up = s->spp_to_dev.up = 0;
    s->spp_to_bridge.len = s->spp_to_dev.len = 0;
    bridge_dev_disconnected(&s->br, w->now);
    gcd_transport_down(&s->dev, w->now);
}

void sim_spp_restore(sim_world *w, int i)
{
    sim_node *s = &w->n[i];
    s->spp_to_bridge.up = s->spp_to_dev.up = 1;
    bridge_dev_connected(&s->br, s->br.dev_addr, w->now);
    gcd_transport_up(&s->dev, w->now);
}
