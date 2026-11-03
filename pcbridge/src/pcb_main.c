/** @file pcb_main.c
 *
 *  GameComms PC bridge: does on a PC what the ESP32 bridge does.
 *
 *  N-Gages connect to the PC's Bluetooth adapter (Serial Port service).
 *  For every N-Gage the bridge runs one bridge core (the same code as the
 *  ESP32 firmware) and one TCP connection to the GameComms server.
 *  Several N-Gages can use the same PC at the same time.
 *
 *    gcbridge [-c FILE] [--server=auto|HOST] [--port=N] [--key=TEXT]
 *             [--pin=NNNN] [--name=TEXT] [--channel=N] [--max_devices=N]
 *             [--log_level=0..3] [--bluetooth=0|1] [--device_tcp=PORT]
 *
 *  --device_tcp=PORT additionally accepts "N-Gages" over TCP on PORT.
 *  This is used by the automated tests (tests/net/gcsim --bridge=...).
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "pcb_sock.h"
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bridge_core.h"
#include "pcb_bt.h"
#include "pcb_com.h"

#define PCB_VERSION        "1.0.0"
#define PCB_MAX_DEVICES    7
#define PCB_OUT_MAX        (256 * 1024)
#define PCB_FREE_AFTER_MS  3000 /* after the device grace time */

/* ------------------------------------------------------------------ */
/* configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct
{
    char server[128]; /* "auto" = LAN discovery */
    int  port;
    char key[GC_MAX_NAME + 1];
    char pin[17];
    char name[64];
    int  channel;
    int  max_devices;
    int  log_level;
    int  bluetooth;
    int  device_tcp;
    char com[256];    /* serial ports, e.g. "COM5" or "COM5,COM6" */
    char adapter[32]; /* Linux: Bluetooth adapter, e.g. hci0 */
} pcb_config;

static pcb_config g_cfg;

static void config_defaults(pcb_config *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->server, "auto");
    c->port = 9888;
    strcpy(c->pin, "1234");
    strcpy(c->name, "GameComms Bridge");
    c->max_devices = 4;
    c->log_level = 2;
    c->bluetooth = 1;
}

/* copies at most n-1 characters and always terminates */
static void copy_str(char *dst, size_t n, const char *src)
{
    size_t k = strlen(src);
    if (k > n - 1) k = n - 1;
    memmove(dst, src, k);
    dst[k] = '\0';
}

static int config_set(pcb_config *c, const char *k, const char *v)
{
    if (!strcmp(k, "server")) copy_str(c->server, sizeof(c->server), v);
    else if (!strcmp(k, "port")) c->port = atoi(v);
    else if (!strcmp(k, "key")) copy_str(c->key, sizeof(c->key), v);
    else if (!strcmp(k, "pin")) copy_str(c->pin, sizeof(c->pin), v);
    else if (!strcmp(k, "name")) copy_str(c->name, sizeof(c->name), v);
    else if (!strcmp(k, "channel")) c->channel = atoi(v);
    else if (!strcmp(k, "max_devices")) c->max_devices = atoi(v);
    else if (!strcmp(k, "log_level")) c->log_level = atoi(v);
    else if (!strcmp(k, "bluetooth")) c->bluetooth = atoi(v);
    else if (!strcmp(k, "device_tcp")) c->device_tcp = atoi(v);
    else if (!strcmp(k, "com")) copy_str(c->com, sizeof(c->com), v);
    else if (!strcmp(k, "adapter")) copy_str(c->adapter, sizeof(c->adapter), v);
    else return -1;
    return 0;
}

static char *trim(char *s)
{
    char *e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* key = value lines, '#' or ';' comments, [sections] ignored */
static int config_load(pcb_config *c, const char *path, int required)
{
    char  line[256];
    int   n = 0;
    FILE *f = fopen(path, "r");
    if (!f)
    {
        if (required)
        {
            fprintf(stderr, "cannot open %s\n", path);
            return -1;
        }
        return 0;
    }
    while (fgets(line, sizeof(line), f))
    {
        char *s = trim(line), *eq;
        n++;
        if (*s == '\0' || *s == '#' || *s == ';' || *s == '[')
        {
            continue;
        }
        eq = strchr(s, '=');
        if (!eq)
        {
            fprintf(stderr, "%s:%d: expected key = value\n", path, n);
            continue;
        }
        *eq = '\0';
        if (config_set(c, trim(s), trim(eq + 1)) != 0)
        {
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path, n, trim(s));
        }
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* logging, time                                                       */
/* ------------------------------------------------------------------ */

static void logmsg(int level, const char *fmt, ...)
{
    static const char *lv[] = {"ERR", "WRN", "INF", "DBG"};
    char       line[512], ts[16];
    va_list    ap;
    time_t     t;
    struct tm *tm;
    if (level > g_cfg.log_level)
    {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    t = time(NULL);
    tm = localtime(&t);
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    printf("%s %s %s\n", ts, lv[level < 0 ? 0 : level > 3 ? 3 : level], line);
    fflush(stdout);
}

static gc_u32 now_ms(void)
{
#ifdef _WIN32
    return (gc_u32)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gc_u32)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
#endif
}

static int set_nonblocking(sock_t s)
{
#ifdef _WIN32
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) == 0 ? 0 : -1;
#else
    int fl = fcntl(s, F_GETFL, 0);
    return fl < 0 ? -1 : fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* ------------------------------------------------------------------ */
/* output buffers                                                      */
/* ------------------------------------------------------------------ */

typedef struct
{
    gc_u8 *data;
    size_t len;
    size_t cap;
} outbuf;

/* returns -1 on a write error or overflow */
static int out_flush(sock_t s, outbuf *o)
{
    while (o->len > 0)
    {
        int n = (int)send(s, (const char *)o->data, (int)o->len, 0);
        if (n > 0)
        {
            memmove(o->data, o->data + n, o->len - (size_t)n);
            o->len -= (size_t)n;
        }
        else
        {
            return (n < 0 && SOCK_EAGAIN(sock_err())) ? 0 : -1;
        }
    }
    return 0;
}

static int out_append(outbuf *o, const gc_u8 *d, size_t n)
{
    if (o->len + n > o->cap)
    {
        size_t cap = o->cap ? o->cap : 4096;
        gc_u8 *p;
        while (cap < o->len + n) cap *= 2;
        if (cap > PCB_OUT_MAX) return -1;
        p = (gc_u8 *)realloc(o->data, cap);
        if (!p) return -1;
        o->data = p;
        o->cap = cap;
    }
    memcpy(o->data + o->len, d, n);
    o->len += n;
    return 0;
}

static void out_free(outbuf *o)
{
    free(o->data);
    memset(o, 0, sizeof(*o));
}

/* ------------------------------------------------------------------ */
/* devices                                                             */
/* ------------------------------------------------------------------ */

typedef struct
{
    int    used;
    int    index;
    bridge br;
    char   addr_str[18];
    gc_u8  addr[6];

    sock_t dev_fd;     /* Bluetooth (or test TCP) connection to the N-Gage */
    pcb_com *com;      /* COM mode: serial port instead of dev_fd */
    char   com_name[64];
    gc_u32 t_com_retry;
    int    com_up;     /* link to the N-Gage on this port is up */
    int    com_lines;  /* last modem line state, for the log */
    int    com_rx_seen; /* data seen since the link was last up */
    outbuf dev_out;
    int    dev_error;
    gc_u32 t_gone;     /* when the N-Gage disconnected */

    sock_t srv_fd;     /* TCP connection to the server */
    outbuf srv_out;
    int    srv_wanted;
    int    srv_connecting;
    int    srv_up;
    int    srv_error;
} pcb_dev;

static pcb_dev *g_dev[PCB_MAX_DEVICES];
static int      g_ndev;
static volatile int g_stop;

/* LAN discovery */
static sock_t             g_udp = SOCK_INVALID;
static struct sockaddr_in g_found;
static int                g_have_found;
static gc_u32             g_t_discover;

static void com_flush(pcb_dev *p)
{
    while (p->com && p->dev_out.len > 0)
    {
        int n = pcb_com_write(p->com, p->dev_out.data, (int)p->dev_out.len);
        if (n < 0)
        {
            p->dev_error = 1;
            return;
        }
        if (n == 0) return; /* try again in the next loop */
        memmove(p->dev_out.data, p->dev_out.data + n, p->dev_out.len - (size_t)n);
        p->dev_out.len -= (size_t)n;
    }
}

static void d_dev_write(void *ctx, const gc_u8 *d, size_t n)
{
    pcb_dev *p = (pcb_dev *)ctx;
    if (p->com_name[0])
    {
        if (!p->com || p->dev_error) return;
        if (out_append(&p->dev_out, d, n) != 0)
        {
            out_free(&p->dev_out); /* nobody reads the port: drop, the link retransmits */
            return;
        }
        com_flush(p);
        return;
    }
    if (p->dev_fd == SOCK_INVALID || p->dev_error) return;
    if (out_append(&p->dev_out, d, n) != 0 || out_flush(p->dev_fd, &p->dev_out) != 0) p->dev_error = 1;
}

static void d_srv_write(void *ctx, const gc_u8 *d, size_t n)
{
    pcb_dev *p = (pcb_dev *)ctx;
    if (!p->srv_up || p->srv_error) return;
    if (out_append(&p->srv_out, d, n) != 0 || out_flush(p->srv_fd, &p->srv_out) != 0) p->srv_error = 1;
}

static void d_srv_connect(void *ctx)
{
    ((pcb_dev *)ctx)->srv_wanted = 1;
}

static void srv_close(pcb_dev *p)
{
    if (p->srv_fd != SOCK_INVALID)
    {
        sock_close(p->srv_fd);
        p->srv_fd = SOCK_INVALID;
    }
    out_free(&p->srv_out);
    p->srv_up = 0;
    p->srv_connecting = 0;
    p->srv_error = 0;
}

static void d_srv_disconnect(void *ctx)
{
    pcb_dev *p = (pcb_dev *)ctx;
    p->srv_wanted = 0;
    srv_close(p);
}

static void d_log(void *ctx, const char *line)
{
    pcb_dev *p = (pcb_dev *)ctx;
    logmsg(3, "[%s] %s", p->addr_str, line);
}

static pcb_dev *dev_find(const gc_u8 addr[6])
{
    int i;
    for (i = 0; i < PCB_MAX_DEVICES; i++)
    {
        if (g_dev[i] && memcmp(g_dev[i]->addr, addr, 6) == 0) return g_dev[i];
    }
    return NULL;
}

static void dev_free(pcb_dev *p)
{
    if (p->dev_fd != SOCK_INVALID) sock_close(p->dev_fd);
    pcb_com_close(p->com);
    out_free(&p->dev_out);
    srv_close(p);
    g_dev[p->index] = NULL;
    g_ndev--;
    free(p);
}

static pcb_dev *dev_new(const gc_u8 addr[6], gc_u32 now)
{
    int        i;
    pcb_dev   *p;
    bridge_cfg bc;
    bridge_io  io;
    if (g_ndev >= g_cfg.max_devices)
    {
        /* full: an N-Gage that is present wins over one that is away */
        pcb_dev *old = NULL;
        for (i = 0; i < PCB_MAX_DEVICES; i++)
        {
            pcb_dev *q = g_dev[i];
            if (q && q->dev_fd == SOCK_INVALID && !q->com_name[0] &&
                (!old || (gc_u32)(now - q->t_gone) > (gc_u32)(now - old->t_gone)))
            {
                old = q;
            }
        }
        if (!old) return NULL;
        logmsg(2, "N-Gage %s removed to make room", old->addr_str);
        dev_free(old);
    }
    for (i = 0; i < PCB_MAX_DEVICES && g_dev[i]; i++)
    {
    }
    if (i == PCB_MAX_DEVICES) return NULL;
    p = (pcb_dev *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->used = 1;
    p->index = i;
    memcpy(p->addr, addr, 6);
    sprintf(p->addr_str, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    p->dev_fd = SOCK_INVALID;
    p->srv_fd = SOCK_INVALID;
    bridge_default_cfg(&bc);
    bc.server_link_id = ((gc_u32)rand() << 16) ^ (gc_u32)rand() ^ now ^ (gc_u32)i;
    if (bc.server_link_id == 0) bc.server_link_id = 1;
    copy_str(bc.access_key, sizeof(bc.access_key), g_cfg.key);
    io.dev_write = d_dev_write;
    io.srv_write = d_srv_write;
    io.srv_connect = d_srv_connect;
    io.srv_disconnect = d_srv_disconnect;
    io.log = d_log;
    bridge_init(&p->br, &bc, &io, p);
    bridge_set_wifi(&p->br, 1, 0, now);
    g_dev[i] = p;
    g_ndev++;
    return p;
}

static void dev_lost(pcb_dev *p, gc_u32 now, const char *why)
{
    if (p->dev_fd == SOCK_INVALID) return;
    sock_close(p->dev_fd);
    p->dev_fd = SOCK_INVALID;
    out_free(&p->dev_out);
    p->dev_error = 0;
    p->t_gone = now;
    logmsg(2, "N-Gage %s disconnected (%s)", p->addr_str, why);
    bridge_dev_disconnected(&p->br, now);
}

static void dev_accepted(sock_t s, const gc_u8 addr[6], gc_u32 now)
{
    pcb_dev *p = dev_find(addr);
    int      one = 1;
    if (!p)
    {
        p = dev_new(addr, now);
        if (!p)
        {
            logmsg(1, "N-Gage %02x:%02x:%02x:%02x:%02x:%02x refused: %d devices connected (max_devices)", addr[0],
                   addr[1], addr[2], addr[3], addr[4], addr[5], g_ndev);
            sock_close(s);
            return;
        }
    }
    else if (p->dev_fd != SOCK_INVALID)
    {
        dev_lost(p, now, "replaced by a new connection");
    }
    set_nonblocking(s);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one)); /* ignored on Bluetooth */
    p->dev_fd = s;
    logmsg(2, "N-Gage %s connected", p->addr_str);
    bridge_dev_connected(&p->br, addr, now);
}

/* ---- COM mode ---- */

static void com_open(pcb_dev *p, gc_u32 now)
{
    char err[160];
    p->t_com_retry = now;
    p->com = pcb_com_open(p->com_name, err, sizeof(err));
    if (!p->com)
    {
        logmsg(1, "%s (retrying)", err);
        return;
    }
    p->dev_error = 0;
    p->com_lines = pcb_com_lines(p->com);
    p->com_rx_seen = 0;
    out_free(&p->dev_out);
    logmsg(2, "listening for an N-Gage on %s", p->com_name);
    bridge_dev_connected(&p->br, p->addr, now);
}

static void com_fail(pcb_dev *p, gc_u32 now)
{
    logmsg(1, "%s failed, reopening", p->com_name);
    pcb_com_close(p->com);
    p->com = NULL;
    p->com_up = 0;
    p->t_com_retry = now;
    out_free(&p->dev_out);
    bridge_dev_disconnected(&p->br, now);
}

/* A COM port has no reliable "connected" signal: the N-Gage is noticed by
   its data and its absence by the link's keep-alive timeout. */
static void com_service(pcb_dev *p, gc_u32 now)
{
    gc_u8 buf[1024];
    int   n, up;
    if (!p->com)
    {
        if ((gc_u32)(now - p->t_com_retry) >= 3000) com_open(p, now);
        if (!p->com) return;
    }
    {
        int lines = pcb_com_lines(p->com);
        if (lines != p->com_lines)
        {
            /* carrier detect / DSR come up when a device connects to the port */
            logmsg(2, "%s: lines CTS %d DSR %d RING %d CD %d", p->com_name, lines & 1, (lines >> 1) & 1,
                   (lines >> 2) & 1, (lines >> 3) & 1);
            p->com_lines = lines;
        }
    }
    while ((n = pcb_com_read(p->com, buf, sizeof(buf))) > 0)
    {
        int st = p->br.dev.state;
        if (st != GC_LS_UP && !p->com_rx_seen)
        {
            p->com_rx_seen = 1;
            logmsg(2, "%s: receiving data (%d bytes), waiting for the N-Gage's handshake", p->com_name, n);
        }
        logmsg(3, "%s: %d bytes in", p->com_name, n);
        if (st != GC_LS_UP && st != GC_LS_WAIT_HELLO && st != GC_LS_IDLE)
        {
            bridge_dev_connected(&p->br, p->addr, now); /* listen for a new HELLO */
        }
        bridge_dev_input(&p->br, buf, (size_t)n, now);
    }
    if (n < 0) p->dev_error = 1;
    if (!p->dev_error) com_flush(p);
    if (p->dev_error)
    {
        com_fail(p, now);
        return;
    }
    up = p->br.dev.state == GC_LS_UP;
    if (up != p->com_up)
    {
        p->com_up = up;
        if (up) logmsg(2, "N-Gage connected on %s", p->com_name);
        else logmsg(2, "N-Gage on %s is gone", p->com_name);
        p->com_rx_seen = 0;
    }
}

static int com_setup(gc_u32 now)
{
    const char *c = g_cfg.com;
    int         n = 0;
    while (*c && n < PCB_MAX_DEVICES)
    {
        char name[64];
        size_t k = 0;
        while (*c == ',' || *c == ' ' || *c == ';') c++;
        while (*c && *c != ',' && *c != ' ' && *c != ';' && k + 1 < sizeof(name)) name[k++] = *c++;
        name[k] = '\0';
        if (k == 0) break;
        {
        /* made-up address 02:47:43:43:4f:nn per port, so that the server
           recognises the same N-Gage after a reconnect */
        gc_u8    addr[6] = {0x02, 0x47, 0x43, 0x43, 0x4f, 0};
        pcb_dev *p;
        addr[5] = (gc_u8)n;
        if (g_cfg.max_devices < n + 1) g_cfg.max_devices = n + 1;
        p = dev_new(addr, now);
        if (!p) break;
        copy_str(p->com_name, sizeof(p->com_name), name);
        com_open(p, now);
        n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* server connection                                                   */
/* ------------------------------------------------------------------ */

static void discovery_send(gc_u32 now)
{
    struct sockaddr_in to;
    const char        *req = "GCDISCOVER1";
    if (g_udp == SOCK_INVALID)
    {
        int one = 1;
        g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_udp == SOCK_INVALID) return;
        setsockopt(g_udp, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
        set_nonblocking(g_udp);
    }
    g_t_discover = now;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons((unsigned short)g_cfg.port); /* the server answers on its own port */
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_udp, req, (int)strlen(req), 0, (struct sockaddr *)&to, sizeof(to));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* server on this PC */
    sendto(g_udp, req, (int)strlen(req), 0, (struct sockaddr *)&to, sizeof(to));
}

static void discovery_input(void)
{
    char               buf[128];
    struct sockaddr_in from;
    socklen_t          fl = sizeof(from);
    int                n, port = 0;
    n = (int)recvfrom(g_udp, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &fl);
    if (n <= 0) return;
    buf[n] = '\0';
    if (sscanf(buf, "GCSERVER1 %d", &port) == 1 && port > 0 && port < 65536)
    {
        if (!g_have_found || g_found.sin_addr.s_addr != from.sin_addr.s_addr || g_found.sin_port != htons((unsigned short)port))
        {
            char ip[64];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            logmsg(2, "server found at %s:%d", ip, port);
        }
        g_found = from;
        g_found.sin_port = htons((unsigned short)port);
        g_have_found = 1;
    }
}

static int is_auto(void)
{
    return g_cfg.server[0] == '\0' || !strcmp(g_cfg.server, "auto");
}

static void srv_start(pcb_dev *p, gc_u32 now)
{
    struct addrinfo  hints, *res = NULL, *ai;
    struct sockaddr *sa = NULL;
    int              salen = 0, one = 1, rc;
    sock_t           s;
    char             ps[16];

    p->srv_wanted = 0;
    srv_close(p);
    if (is_auto())
    {
        if (!g_have_found)
        {
            if ((gc_u32)(now - g_t_discover) > 500) discovery_send(now);
            bridge_srv_disconnected(&p->br, now); /* retried with back-off */
            return;
        }
        sa = (struct sockaddr *)&g_found;
        salen = sizeof(g_found);
    }
    else
    {
        memset(&hints, 0, sizeof(hints));
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        sprintf(ps, "%d", g_cfg.port);
        if (getaddrinfo(g_cfg.server, ps, &hints, &res) != 0 || !res)
        {
            logmsg(1, "cannot resolve server '%s'", g_cfg.server);
            bridge_srv_disconnected(&p->br, now);
            return;
        }
        ai = res;
        sa = ai->ai_addr;
        salen = (int)ai->ai_addrlen;
    }
    s = socket(sa->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCK_INVALID)
    {
        if (res) freeaddrinfo(res);
        bridge_srv_disconnected(&p->br, now);
        return;
    }
    set_nonblocking(s);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    rc = connect(s, sa, salen);
    if (res) freeaddrinfo(res);
    p->srv_fd = s;
    if (rc == 0)
    {
        p->srv_up = 1;
        bridge_srv_connected(&p->br, now);
    }
    else if (SOCK_EINPROGRESS(sock_err()))
    {
        p->srv_connecting = 1;
    }
    else
    {
        srv_close(p);
        if (is_auto()) g_have_found = 0;
        bridge_srv_disconnected(&p->br, now);
    }
}

static void srv_connect_done(pcb_dev *p, gc_u32 now)
{
    int       err = 0;
    socklen_t len = sizeof(err);
    getsockopt(p->srv_fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
    p->srv_connecting = 0;
    if (err == 0)
    {
        p->srv_up = 1;
        logmsg(3, "[%s] server connection established", p->addr_str);
        bridge_srv_connected(&p->br, now);
    }
    else
    {
        logmsg(3, "[%s] server connection failed (%d)", p->addr_str, err);
        srv_close(p);
        if (is_auto()) g_have_found = 0; /* search again */
        bridge_srv_disconnected(&p->br, now);
    }
}

/* ------------------------------------------------------------------ */
/* status                                                              */
/* ------------------------------------------------------------------ */

static void print_status(void)
{
    int i;
    if (g_ndev == 0)
    {
        logmsg(2, "status: no N-Gage connected");
        return;
    }
    for (i = 0; i < PCB_MAX_DEVICES; i++)
    {
        pcb_dev *p = g_dev[i];
        if (!p) continue;
        logmsg(2, "status: N-Gage %s %s, server %s, messages to server %lu, from server %lu", p->addr_str,
               (p->dev_fd != SOCK_INVALID || p->com_up) ? "connected" : "away", p->br.srv_state == GC_BS_UP ? "up" : "down",
               (unsigned long)p->br.st_dev_msgs, (unsigned long)p->br.st_srv_msgs);
    }
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
static BOOL WINAPI on_ctrl(DWORD type)
{
    (void)type;
    g_stop = 1;
    return TRUE;
}
#else
static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}
#endif

static sock_t tcp_listener(int port)
{
    struct sockaddr_in a;
    int                one = 1;
    sock_t             s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCK_INVALID) return s;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 8) != 0)
    {
        sock_close(s);
        return SOCK_INVALID;
    }
    set_nonblocking(s);
    return s;
}

/* gcbridge.ini in the current folder, else next to the program */
static int load_default_config(const char *argv0)
{
    char  path[1024];
    char *slash;
    FILE *f = fopen("gcbridge.ini", "r");
    if (f)
    {
        fclose(f);
        return config_load(&g_cfg, "gcbridge.ini", 1);
    }
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, path, sizeof(path)) == 0) return 0;
    (void)argv0;
#else
    copy_str(path, sizeof(path), argv0);
#endif
    slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (!slash) return 0;
    copy_str(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "gcbridge.ini");
    return config_load(&g_cfg, path, 0);
}

/* when started by double click, keep the window open to show the error */
static int fail_exit(int rc)
{
#ifdef _WIN32
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) == 1)
    {
        printf("\nPress Enter to close this window.\n");
        getchar();
    }
#endif
    return rc;
}

static void usage(void)
{
    printf("GameComms PC bridge %s\n\n"
           "usage: gcbridge [-c FILE] [--key=value ...]\n\n"
           "  --server=auto|HOST   server address, auto = find it on the local network (default auto)\n"
           "  --port=N             server port (default 9888)\n"
           "  --key=TEXT           access key, if the server requires one\n"
           "  --pin=NNNN           PIN for pairing N-Gages, empty = confirm in Windows (default 1234)\n"
           "  --name=TEXT          Bluetooth service name (default GameComms Bridge)\n"
           "  --channel=N          RFCOMM channel, 0 = any (default 0)\n"
           "  --adapter=hciN       Linux: Bluetooth adapter (default hci0)\n"
           "  --max_devices=N      N-Gages at the same time, 1 to 7 (default 4)\n"
           "  --log_level=N        0 errors .. 3 debug (default 2)\n"
           "  --bluetooth=0|1      use Bluetooth (default 1)\n"
           "  --com=COM5[,COM6]    COM mode: use Windows' incoming Bluetooth COM port(s) instead\n"
           "                       of the bridge's own Bluetooth service\n"
           "  --device_tcp=PORT    also accept test devices over TCP on 127.0.0.1:PORT\n\n"
           "Without -c, gcbridge.ini is read from the current folder or the program's folder.\n",
           PCB_VERSION);
}

int main(int argc, char **argv)
{
    sock_t     tcp_l = SOCK_INVALID;
    gc_u32     t_status;
    int        i, bt_ok = 0, ncom = 0;
    const char *cfgfile = NULL;
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0)
    {
        fprintf(stderr, "Winsock initialisation failed\n");
        return 1;
    }
    SetConsoleCtrlHandler(on_ctrl, TRUE);
#else
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
#endif

    config_defaults(&g_cfg);
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) cfgfile = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
    }
    if ((cfgfile ? config_load(&g_cfg, cfgfile, 1) : load_default_config(argv[0])) != 0) return fail_exit(1);
    for (i = 1; i < argc; i++)
    {
        char k[64], *eq;
        if (!strcmp(argv[i], "-c")) { i++; continue; }
        if (strncmp(argv[i], "--", 2) != 0 || !(eq = strchr(argv[i], '=')) || (size_t)(eq - argv[i] - 2) >= sizeof(k))
        {
            usage();
            return 2;
        }
        memcpy(k, argv[i] + 2, (size_t)(eq - argv[i] - 2));
        k[eq - argv[i] - 2] = '\0';
        if (config_set(&g_cfg, k, eq + 1) != 0)
        {
            fprintf(stderr, "unknown option --%s\n", k);
            usage();
            return 2;
        }
    }
    if (g_cfg.max_devices < 1) g_cfg.max_devices = 1;
    if (g_cfg.max_devices > PCB_MAX_DEVICES) g_cfg.max_devices = PCB_MAX_DEVICES;
    srand((unsigned)time(NULL) ^ (unsigned)now_ms());

    logmsg(2, "GameComms PC bridge %s, server %s%s%d, up to %d N-Gages", PCB_VERSION, g_cfg.server,
           is_auto() ? " (local network), port " : ":", g_cfg.port, g_cfg.max_devices);

    if (g_cfg.com[0])
    {
        /* Windows answers the N-Gage itself; a second Serial Port service
           of our own would only confuse the N-Gage's service search */
        ncom = com_setup(now_ms());
        logmsg(2, "COM mode: the bridge's own Bluetooth service is not used");
    }
    else if (g_cfg.bluetooth)
    {
        pcb_bt_cfg bc;
        char       err[256];
        bc.service_name = g_cfg.name;
        bc.pin = g_cfg.pin;
        bc.channel = g_cfg.channel;
        bc.adapter = g_cfg.adapter;
        bc.log = logmsg;
        if (pcb_bt_open(&bc, err, sizeof(err)) == 0)
        {
            bt_ok = 1;
        }
        else
        {
            logmsg(0, "%s", err);
        }
    }
    if (g_cfg.device_tcp > 0)
    {
        tcp_l = tcp_listener(g_cfg.device_tcp);
        if (tcp_l == SOCK_INVALID)
        {
            logmsg(0, "cannot listen on TCP port %d for test devices", g_cfg.device_tcp);
        }
        else
        {
            logmsg(2, "test devices accepted on 127.0.0.1:%d", g_cfg.device_tcp);
        }
    }
    if (!bt_ok && tcp_l == SOCK_INVALID && ncom == 0)
    {
        logmsg(0, "nothing to listen on, exiting");
        return fail_exit(1);
    }
    if (bt_ok || ncom)
    {
        logmsg(2, "ready: on the N-Gage, start a multiplayer game and select this PC");
    }
    if (is_auto()) discovery_send(now_ms());
    t_status = now_ms();

    while (!g_stop)
    {
        fd_set         rs, ws, es;
        struct timeval tv;
        gc_u32         now = now_ms(), wait = 50;
        int            maxfd = 0, nsock = 0;
        sock_t         bl = pcb_bt_listener();

#define ADD(set, s)                                   \
    do                                                \
    {                                                 \
        FD_SET((s), (set));                           \
        if ((int)(s) > maxfd) maxfd = (int)(s);       \
        nsock++;                                      \
    } while (0)

        FD_ZERO(&rs);
        FD_ZERO(&ws);
        FD_ZERO(&es);
        if (bl != SOCK_INVALID) ADD(&rs, bl);
        if (tcp_l != SOCK_INVALID) ADD(&rs, tcp_l);
        if (g_udp != SOCK_INVALID) ADD(&rs, g_udp);
        for (i = 0; i < PCB_MAX_DEVICES; i++)
        {
            pcb_dev *p = g_dev[i];
            gc_u32   t;
            if (!p) continue;
            if (p->dev_fd != SOCK_INVALID)
            {
                ADD(&rs, p->dev_fd);
                if (p->dev_out.len) ADD(&ws, p->dev_fd);
            }
            if (p->srv_fd != SOCK_INVALID)
            {
                if (p->srv_connecting || p->srv_out.len) ADD(&ws, p->srv_fd);
                /* Windows reports a failed connect in the exception set */
                if (p->srv_connecting) ADD(&es, p->srv_fd);
                if (p->srv_up) ADD(&rs, p->srv_fd);
            }
            t = bridge_next_timeout(&p->br, now);
            if (t < wait) wait = t;
            if (p->com_name[0] && wait > 5) wait = 5; /* serial ports are polled */
        }
        tv.tv_sec = 0;
        tv.tv_usec = (long)(wait ? wait : 1) * 1000;
        if (nsock == 0)
        {
            /* Windows' select() refuses empty sets */
#ifdef _WIN32
            Sleep(wait ? wait : 1);
#else
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (long)(wait ? wait : 1) * 1000000L;
            nanosleep(&ts, NULL);
#endif
        }
        else if (select(maxfd + 1, &rs, &ws, &es, &tv) < 0)
        {
#ifdef _WIN32
            Sleep(10);
#endif
            FD_ZERO(&rs);
            FD_ZERO(&ws);
            FD_ZERO(&es);
        }
        now = now_ms();

        if (bl != SOCK_INVALID && FD_ISSET(bl, &rs))
        {
            gc_u8  addr[6];
            sock_t s;
            while ((s = pcb_bt_accept(addr)) != SOCK_INVALID) dev_accepted(s, addr, now);
        }
        if (tcp_l != SOCK_INVALID && FD_ISSET(tcp_l, &rs))
        {
            struct sockaddr_in from;
            socklen_t          fl = sizeof(from);
            sock_t             s = accept(tcp_l, (struct sockaddr *)&from, &fl);
            if (s != SOCK_INVALID)
            {
                /* test devices get the address 02:47:43:54:pp:pp, pp = their
                   TCP source port, so a reconnect from the same port looks
                   like the same N-Gage coming back */
                gc_u8 addr[6] = {0x02, 0x47, 0x43, 0x54, 0, 0};
                unsigned short sp = ntohs(from.sin_port);
                addr[4] = (gc_u8)(sp >> 8);
                addr[5] = (gc_u8)(sp & 0xFF);
                dev_accepted(s, addr, now);
            }
        }
        if (g_udp != SOCK_INVALID && FD_ISSET(g_udp, &rs)) discovery_input();

        for (i = 0; i < PCB_MAX_DEVICES; i++)
        {
            pcb_dev *p = g_dev[i];
            if (!p) continue;

            /* N-Gage side */
            if (p->com_name[0]) com_service(p, now);
            if (p->dev_fd != SOCK_INVALID && FD_ISSET(p->dev_fd, &rs))
            {
                gc_u8 buf[4096];
                int   n = (int)recv(p->dev_fd, (char *)buf, sizeof(buf), 0);
                if (n > 0) bridge_dev_input(&p->br, buf, (size_t)n, now);
                else if (n == 0 || !SOCK_EAGAIN(sock_err())) dev_lost(p, now, "connection closed");
            }
            if (p->dev_fd != SOCK_INVALID && FD_ISSET(p->dev_fd, &ws) && out_flush(p->dev_fd, &p->dev_out) != 0)
                p->dev_error = 1;
            if (p->dev_error) dev_lost(p, now, "write error");

            /* server side */
            if (p->srv_fd != SOCK_INVALID && p->srv_connecting && (FD_ISSET(p->srv_fd, &ws) || FD_ISSET(p->srv_fd, &es)))
                srv_connect_done(p, now);
            else if (p->srv_fd != SOCK_INVALID && p->srv_up)
            {
                if (FD_ISSET(p->srv_fd, &rs))
                {
                    gc_u8 buf[8192];
                    int   n = (int)recv(p->srv_fd, (char *)buf, sizeof(buf), 0);
                    if (n > 0) bridge_srv_input(&p->br, buf, (size_t)n, now);
                    else if (n == 0 || !SOCK_EAGAIN(sock_err())) p->srv_error = 1;
                }
                if (p->srv_fd != SOCK_INVALID && FD_ISSET(p->srv_fd, &ws) && out_flush(p->srv_fd, &p->srv_out) != 0)
                    p->srv_error = 1;
            }
            if (p->srv_error)
            {
                logmsg(3, "[%s] server connection lost", p->addr_str);
                srv_close(p);
                bridge_srv_disconnected(&p->br, now);
            }
            if (p->srv_wanted) srv_start(p, now);

            bridge_poll(&p->br, now);
            if (p->srv_wanted) srv_start(p, now);
            if (p->dev_error) dev_lost(p, now, "write error");

            /* forget an N-Gage that did not come back */
            if (p->dev_fd == SOCK_INVALID && !p->com_name[0] && !p->br.dev_lost_pending &&
                (gc_u32)(now - p->t_gone) > p->br.cfg.device_grace_ms + PCB_FREE_AFTER_MS)
            {
                logmsg(2, "N-Gage %s removed", p->addr_str);
                dev_free(p);
            }
        }

        /* keep looking for the server while it is unknown */
        if (is_auto() && !g_have_found && g_ndev > 0 && (gc_u32)(now - g_t_discover) > 2000) discovery_send(now);

        if ((gc_u32)(now - t_status) > 60000)
        {
            t_status = now;
            print_status();
        }
    }

    logmsg(2, "stopping");
    for (i = 0; i < PCB_MAX_DEVICES; i++)
    {
        if (g_dev[i]) dev_free(g_dev[i]);
    }
    pcb_bt_close();
    if (tcp_l != SOCK_INVALID) sock_close(tcp_l);
    if (g_udp != SOCK_INVALID) sock_close(g_udp);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
