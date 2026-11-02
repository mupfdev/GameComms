/** @file gcsim.c
 *
 *  Network-level simulator / smoke tester.
 *
 *  Runs N virtual N-Gages, each with its own (real) bridge core, and
 *  connects every bridge over a real TCP socket to a running
 *  gamecommsd.  Only the Bluetooth hop is simulated (in-process).
 *
 *    gcsim [--server=HOST] [--port=N] [--api=1|2] [--players=N]
 *          [--uid=0x...] [--messages=N] [--key=TEXT] [--verbose]
 *
 *  Scenario: device 0 hosts, the others join, the game starts, the host
 *  broadcasts and every client answers; all payloads are verified.
 *  Exit status 0 on success.  Useful to check a server deployment from
 *  anywhere ("does my VPS work?") without hardware.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gc_dev.h"
#include "bridge_core.h"

#define MAXN 4

typedef struct
{
    gcd_dev dev;
    bridge  br;
    sock_t  fd;
    int     want_connect;
    gc_u8   to_bridge[65536];
    size_t  n_to_bridge;
    gc_u8   to_dev[65536];
    size_t  n_to_dev;
    int     started;
    int     rx;
    int     bad;
    int     connected2;
    char    name[16];
} vdev;

static vdev        V[MAXN];
static int         N = 4;
static const char *server = "127.0.0.1";
static int         port = 9888;
static int         verbose = 0;

static gc_u32 now_ms(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gc_u32)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

static void d_write(void *c, const gc_u8 *d, size_t n)
{
    vdev *v = (vdev *)c;
    if (v->n_to_bridge + n <= sizeof(v->to_bridge)) { memcpy(v->to_bridge + v->n_to_bridge, d, n); v->n_to_bridge += n; }
}
static void d_log(void *c, const char *l) { if (verbose) printf("[%s] %s\n", ((vdev *)c)->name, l); }
static void d_wake(void *c) { (void)c; }
static void b_dev_write(void *c, const gc_u8 *d, size_t n)
{
    vdev *v = (vdev *)c;
    if (v->n_to_dev + n <= sizeof(v->to_dev)) { memcpy(v->to_dev + v->n_to_dev, d, n); v->n_to_dev += n; }
}
static void b_srv_write(void *c, const gc_u8 *d, size_t n)
{
    vdev *v = (vdev *)c;
    size_t k = 0;
    while (k < n && v->fd != (sock_t)-1)
    {
        int r = (int)send(v->fd, (const char *)d + k, (int)(n - k), 0);
        if (r <= 0) break;
        k += (size_t)r;
    }
}
static void b_srv_connect(void *c) { ((vdev *)c)->want_connect = 1; }
static void b_srv_disconnect(void *c)
{
    vdev *v = (vdev *)c;
    if (v->fd != (sock_t)-1) { CLOSESOCK(v->fd); v->fd = (sock_t)-1; }
}
static void b_log(void *c, const char *l) { if (verbose) printf("[%s bridge] %s\n", ((vdev *)c)->name, l); }

static sock_t tcp_connect(void)
{
    struct addrinfo hints, *res, *ai;
    char            ps[16];
    sock_t          s = (sock_t)-1;
    int             one = 1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    sprintf(ps, "%d", port);
    if (getaddrinfo(server, ps, &hints, &res) != 0) return (sock_t)-1;
    for (ai = res; ai; ai = ai->ai_next)
    {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == (sock_t)-1) continue;
        if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        CLOSESOCK(s);
        s = (sock_t)-1;
    }
    freeaddrinfo(res);
    if (s != (sock_t)-1) setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    return s;
}

static void pump(gc_u32 dur)
{
    gc_u32 end = now_ms() + dur;
    while ((gc_i32)(end - now_ms()) > 0)
    {
        fd_set         rs;
        struct timeval tv = {0, 5000};
        int            i, maxfd = 0;
        gc_u32         t;
        FD_ZERO(&rs);
        for (i = 0; i < N; i++)
        {
            if (V[i].fd != (sock_t)-1) { FD_SET(V[i].fd, &rs); if ((int)V[i].fd > maxfd) maxfd = (int)V[i].fd; }
        }
        select(maxfd + 1, &rs, NULL, NULL, &tv);
        t = now_ms();
        for (i = 0; i < N; i++)
        {
            vdev     *v = &V[i];
            gcd_event e;
            if (v->want_connect)
            {
                v->want_connect = 0;
                v->fd = tcp_connect();
                if (v->fd != (sock_t)-1) bridge_srv_connected(&v->br, t);
                else bridge_srv_disconnected(&v->br, t);
            }
            if (v->fd != (sock_t)-1 && FD_ISSET(v->fd, &rs))
            {
                gc_u8 buf[8192];
                int   r = (int)recv(v->fd, (char *)buf, sizeof(buf), 0);
                if (r > 0) bridge_srv_input(&v->br, buf, (size_t)r, t);
                else { CLOSESOCK(v->fd); v->fd = (sock_t)-1; bridge_srv_disconnected(&v->br, t); }
            }
            if (v->n_to_bridge) { size_t n = v->n_to_bridge; v->n_to_bridge = 0; bridge_dev_input(&v->br, v->to_bridge, n, t); }
            if (v->n_to_dev) { size_t n = v->n_to_dev; v->n_to_dev = 0; gcd_transport_input(&v->dev, v->to_dev, n, t); }
            gcd_poll(&v->dev, t);
            bridge_poll(&v->br, t);
            while (gcd_pop_event(&v->dev, &e))
            {
                if (verbose) printf("[%s] event %d id %d err %d len %u\n", v->name, e.type, e.id, e.err, e.len);
                if (e.type == GCD_EV1_START_GAME || (e.type == GCD_EV2_CONNECTED && e.err == 0)) { v->started++; }
                if (e.type == GCD_EV1_DATA_FROM_HOST || e.type == GCD_EV1_DATA_FROM_CLIENT || e.type == GCD_EV2_DATA)
                {
                    v->rx++;
                    if (e.len < 4 || memcmp(e.data, "GCS!", 4) != 0) v->bad++;
                    /* clients echo everything back to the host */
                    if (v->dev.role == GCD_ROLE_CLIENT)
                    {
                        if (v->dev.cfg.api == GC_API_V1) gcd_v1_send(&v->dev, GC_SLOT_HOST, e.data, e.len, t);
                        else gcd_v2_send(&v->dev, GCD_ALL, e.data, e.len, t);
                    }
                }
                if (e.type == GCD_EV2_DEVICE_DECLARED && v->dev.role == GCD_ROLE_HOST && e.err == 0) v->connected2++;
            }
        }
    }
}

int main(int argc, char **argv)
{
    int    i, api = 1, msgs = 50, k;
    gc_u32 uid = 0x10005B8B;
    char   key[GC_MAX_NAME + 1] = "";
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    for (i = 1; i < argc; i++)
    {
        if (!strncmp(argv[i], "--server=", 9)) server = argv[i] + 9;
        else if (!strncmp(argv[i], "--port=", 7)) port = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--api=", 6)) api = atoi(argv[i] + 6);
        else if (!strncmp(argv[i], "--players=", 10)) N = atoi(argv[i] + 10);
        else if (!strncmp(argv[i], "--uid=", 6)) uid = (gc_u32)strtoul(argv[i] + 6, NULL, 0);
        else if (!strncmp(argv[i], "--messages=", 11)) msgs = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--key=", 6)) strncpy(key, argv[i] + 6, GC_MAX_NAME);
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else { printf("usage: gcsim [--server=HOST] [--port=N] [--api=1|2] [--players=2..4] [--uid=0x..] [--messages=N] [--key=K] [--verbose]\n"); return 2; }
    }
    if (N < 2 || N > MAXN) N = 4;
    srand((unsigned)time(NULL));
    for (i = 0; i < N; i++)
    {
        gcd_config dc; gcd_ops dops; bridge_cfg bc; bridge_io bio;
        gc_u8 addr[6] = {0x02, 0x47, 0x43, 0x53, 0x49, 0};
        vdev *v = &V[i];
        memset(v, 0, sizeof(*v));
        v->fd = (sock_t)-1;
        sprintf(v->name, "sim%d", i);
        gcd_default_config(&dc);
        dc.api = (gc_u8)api; dc.game_uid = uid; strcpy(dc.name, v->name);
        dc.link_id = (gc_u32)rand() | 1; dc.auto_join = 1;
        dops.write = d_write; dops.log = d_log; dops.wake = d_wake;
        gcd_init(&v->dev, &dc, &dops, v);
        bridge_default_cfg(&bc);
        bc.server_link_id = (gc_u32)rand() | 1;
        strcpy(bc.access_key, key);
        bio.dev_write = b_dev_write; bio.srv_write = b_srv_write; bio.srv_connect = b_srv_connect;
        bio.srv_disconnect = b_srv_disconnect; bio.log = b_log;
        bridge_init(&v->br, &bc, &bio, v);
        addr[5] = (gc_u8)(rand() & 0xFF);
        bridge_dev_connected(&v->br, addr, now_ms());
        gcd_transport_up(&v->dev, now_ms());
        bridge_set_wifi(&v->br, 1, -40, now_ms());
    }
    pump(1500);
    for (i = 0; i < N; i++)
    {
        if (!V[i].dev.registered) { printf("FAIL: %s did not register (server %s:%d reachable?)\n", V[i].name, server, port); return 1; }
    }
    printf("all %d devices registered\n", N);
    if (api == 1) gcd_v1_start_host(&V[0].dev, (gc_u16)N, (gc_u16)N, now_ms());
    else { gcd_v2_start(&V[0].dev, 1, 0, now_ms()); gcd_v2_set_accept_mode(&V[0].dev, GCD_ACCEPT_ALL, now_ms()); }
    pump(800);
    for (i = 1; i < N; i++)
    {
        if (api == 1) gcd_v1_start_client(&V[i].dev, now_ms());
        else { gcd_device host; memset(&host, 0, sizeof(host)); host.session_id = V[0].dev.session_id;
               gcd_v2_start(&V[i].dev, 0, 0, now_ms()); gcd_v2_declare(&V[i].dev, &host, now_ms()); gcd_v2_connect(&V[i].dev, GCD_ALL, now_ms()); }
        pump(800);
    }
    if (api == 2) { gcd_v2_connect(&V[0].dev, GCD_ALL, now_ms()); pump(800); }
    for (i = 0; i < N; i++)
    {
        if (!V[i].started) { printf("FAIL: %s: game did not start\n", V[i].name); return 1; }
    }
    printf("session %08x running with %d players\n", V[0].dev.session_id, N);
    for (k = 0; k < msgs; k++)
    {
        gc_u8 p[64]; int r;
        memcpy(p, "GCS!", 4); memset(p + 4, k, sizeof(p) - 4);
        do {
            r = api == 1 ? gcd_v1_send(&V[0].dev, GC_SLOT_ALL, p, sizeof(p), now_ms()) : gcd_v2_send(&V[0].dev, GCD_ALL, p, sizeof(p), now_ms());
            if (r == GCD_KErrOverflow) pump(10);
        } while (r == GCD_KErrOverflow);
        if (r != 0) { printf("FAIL: send returned %d\n", r); return 1; }
        pump(10);
    }
    pump(2000);
    {
        int ok = V[0].rx == msgs * (N - 1) && V[0].bad == 0;
        for (i = 1; i < N; i++) ok &= V[i].rx == msgs && V[i].bad == 0;
        printf("host received %d/%d echoes; clients received", V[0].rx, msgs * (N - 1));
        for (i = 1; i < N; i++) printf(" %d", V[i].rx);
        printf(" -> %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
}
