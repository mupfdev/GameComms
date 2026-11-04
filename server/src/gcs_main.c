/** @file gcs_main.c
 *
 *  GameComms endpoint server - network front end.
 *
 *  Single threaded, poll() based TCP server (WSAPoll on Windows) plus a
 *  tiny UDP responder that lets ESP32 bridges find the server on a LAN
 *  without configuration.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID   INVALID_SOCKET
#define sock_close     closesocket
#define poll           WSAPoll
#define SOCK_EAGAIN(e) ((e) == WSAEWOULDBLOCK)
static int sock_err(void) { return WSAGetLastError(); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID   (-1)
#define sock_close     close
#define SOCK_EAGAIN(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
static int sock_err(void) { return errno; }
#endif

#include "gcs_core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GCS_VERSION       "1.0.0"
#define GCS_DEFAULT_PORT  9888
#define GCS_DISCOVERY_REQ "GCDISCOVER1"
#define GCS_DISCOVERY_RSP "GCSERVER1"
#define MAX_CONNS         1024
#define OUTBUF_MAX        (512 * 1024)

typedef struct
{
    gcs_conn conn; /* must be first */
    sock_t fd;
    int used;
    int closing;
    gc_u8 *out;
    size_t out_len;
    size_t out_cap;
    char addr[64];
} net_conn;

static net_conn g_conns[MAX_CONNS];
static gcs_core *g_core;
static volatile int g_stop;
static volatile int g_dump;
static int g_log_level = 2;
static FILE *g_logfile;

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

static void log_line(void *ctx, int level, const char *line)
{
    static const char *lv[] = { "ERR", "WRN", "INF", "DBG" };
    char ts[32];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    (void)ctx;

    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(g_logfile ? g_logfile : stdout, "%s %s %s\n", ts, lv[level < 0 ? 0 : level > 3 ? 3
                                                                                           : level],
            line);
    fflush(g_logfile ? g_logfile : stdout);
}

static void logmsg(int level, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    if (level > g_log_level) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    log_line(NULL, level, line);
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

/* ---- core I/O callbacks ---- */

static void try_flush(net_conn *c)
{
    while (c->out_len > 0) {
        int n = (int)send(c->fd, (const char *)c->out, (int)c->out_len, 0);
        if (n > 0) {
            memmove(c->out, c->out + n, c->out_len - (size_t)n);
            c->out_len -= (size_t)n;
        } else {
            if (n < 0 && SOCK_EAGAIN(sock_err())) {
                return;
            }
            c->closing = 1; /* write error */
            return;
        }
    }
}

static void io_write(void *ctx, gcs_conn *conn, const gc_u8 *data, size_t len)
{
    net_conn *c = (net_conn *)conn;
    (void)ctx;
    if (c->closing) {
        return;
    }
    if (c->out_len + len > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 4096;
        gc_u8 *n;
        while (cap < c->out_len + len) {
            cap *= 2;
        }
        if (cap > OUTBUF_MAX) {
            logmsg(1, "%s: output buffer overflow, closing", c->addr);
            c->closing = 1;
            return;
        }
        n = (gc_u8 *)realloc(c->out, cap);
        if (!n) {
            c->closing = 1;
            return;
        }
        c->out = n;
        c->out_cap = cap;
    }
    memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
    try_flush(c);
}

static void io_close(void *ctx, gcs_conn *conn)
{
    (void)ctx;
    ((net_conn *)conn)->closing = 1; /* closed after the current pass */
}

static void conn_free(net_conn *c, gc_u32 now)
{
    gcs_core_conn_closed(g_core, &c->conn, now);
    try_flush(c);
    sock_close(c->fd);
    free(c->out);
    logmsg(3, "%s: closed", c->addr);
    memset(c, 0, sizeof(*c));
    c->fd = SOCK_INVALID;
}

/* ---- sockets ---- */

static sock_t open_listener(const char *bind_addr, int port, int type)
{
    struct addrinfo hints, *res = NULL, *ai;
    char ports[16];
    sock_t s = SOCK_INVALID;
    int one = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;
    hints.ai_flags = AI_PASSIVE;
    sprintf(ports, "%d", port);
    if (getaddrinfo(bind_addr[0] ? bind_addr : NULL, ports, &hints, &res) != 0) {
        return SOCK_INVALID;
    }
    /* prefer IPv4 for maximum compatibility with the ESP32 lwIP default */
    for (ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET) {
            break;
        }
    }
    if (!ai) {
        ai = res;
    }
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s != SOCK_INVALID) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
        if (bind(s, ai->ai_addr, (int)ai->ai_addrlen) != 0 ||
            (type == SOCK_STREAM && listen(s, 64) != 0) || set_nonblocking(s) != 0) {
            sock_close(s);
            s = SOCK_INVALID;
        }
    }
    freeaddrinfo(res);
    return s;
}

static void accept_new(sock_t lfd, gc_u32 now)
{
    for (;;) {
        struct sockaddr_storage sa;
        socklen_t sl = sizeof(sa);
        sock_t fd = accept(lfd, (struct sockaddr *)&sa, &sl);
        int i, one = 1;
        net_conn *c = NULL;

        if (fd == SOCK_INVALID) {
            return;
        }
        for (i = 0; i < MAX_CONNS; i++) {
            if (!g_conns[i].used) {
                c = &g_conns[i];
                break;
            }
        }
        if (!c) {
            logmsg(1, "connection table full");
            sock_close(fd);
            continue;
        }
        set_nonblocking(fd);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&one, sizeof(one));
        memset(c, 0, sizeof(*c));
        c->used = 1;
        c->fd = fd;
        c->conn.net = c;
        getnameinfo((struct sockaddr *)&sa, sl, c->addr, sizeof(c->addr) - 8, NULL, 0, NI_NUMERICHOST);
        logmsg(3, "%s: connected", c->addr);
        gcs_core_conn_open(g_core, &c->conn, now);
    }
}

static void udp_discovery(sock_t ufd, int tcp_port, const char *name)
{
    char buf[64];
    struct sockaddr_storage sa;
    socklen_t sl = sizeof(sa);
    int n;

    while ((n = (int)recvfrom(ufd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&sa, &sl)) > 0) {
        char rsp[96];
        buf[n] = '\0';
        if (strncmp(buf, GCS_DISCOVERY_REQ, strlen(GCS_DISCOVERY_REQ)) != 0) {
            continue;
        }
        n = snprintf(rsp, sizeof(rsp), "%s %d %s", GCS_DISCOVERY_RSP, tcp_port, name);
        sendto(ufd, rsp, n, 0, (struct sockaddr *)&sa, sl);
        sl = sizeof(sa);
    }
}

#ifndef _WIN32
static void on_signal(int sig)
{
    if (sig == SIGUSR1) {
        g_dump = 1;
    } else {
        g_stop = 1;
    }
}
#endif

/* ---- configuration ---- */

typedef struct
{
    char bind_addr[64];
    int port;
    int discovery;
    gcs_config core;
    char logfile[256];
    int status_interval;
} app_cfg;

static void trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') {
        memmove(s, s + 1, strlen(s));
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
}

static int set_option(app_cfg *a, const char *key, const char *val)
{
    if (!strcmp(key, "bind")) {
        snprintf(a->bind_addr, sizeof(a->bind_addr), "%s", val);
    } else if (!strcmp(key, "port")) {
        a->port = atoi(val);
    } else if (!strcmp(key, "discovery")) {
        a->discovery = atoi(val);
    } else if (!strcmp(key, "name")) {
        snprintf(a->core.server_name, sizeof(a->core.server_name), "%s", val);
    } else if (!strcmp(key, "access_key")) {
        snprintf(a->core.access_key, sizeof(a->core.access_key), "%s", val);
    } else if (!strcmp(key, "max_peers")) {
        a->core.max_peers = atoi(val);
    } else if (!strcmp(key, "max_sessions")) {
        a->core.max_sessions = atoi(val);
    } else if (!strcmp(key, "link_grace_ms")) {
        a->core.link_grace_ms = (gc_u32)atol(val);
    } else if (!strcmp(key, "reserve_ms")) {
        a->core.reserve_ms = (gc_u32)atol(val);
    } else if (!strcmp(key, "pending_ms")) {
        a->core.pending_ms = (gc_u32)atol(val);
    } else if (!strcmp(key, "max_outq")) {
        a->core.max_outq = atoi(val);
    } else if (!strcmp(key, "log_level")) {
        a->core.log_level = atoi(val);
    } else if (!strcmp(key, "log_file")) {
        snprintf(a->logfile, sizeof(a->logfile), "%s", val);
    } else if (!strcmp(key, "status_interval")) {
        a->status_interval = atoi(val);
    } else {
        return 0;
    }
    return 1;
}

static int load_config(app_cfg *a, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int ln = 0;
    if (!f) {
        fprintf(stderr, "cannot open config file %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        char *eq;
        ln++;
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "%s:%d: expected key = value\n", path, ln);
            continue;
        }
        *eq = '\0';
        trim(line);
        trim(eq + 1);
        if (!set_option(a, line, eq + 1)) {
            fprintf(stderr, "%s:%d: unknown option '%s'\n", path, ln, line);
        }
    }
    fclose(f);
    return 1;
}

static void usage(const char *argv0)
{
    printf("GameComms endpoint server %s\n\n"
           "usage: %s [-c file] [--key=value ...]\n\n"
           "  -c FILE              read options from FILE (key = value)\n"
           "  --port=N             TCP port (default %d)\n"
           "  --bind=ADDR          bind address (default all)\n"
           "  --discovery=0|1      answer LAN discovery on UDP port (default 1)\n"
           "  --name=TEXT          server name shown to devices\n"
           "  --access_key=TEXT    require this key from bridges (default none)\n"
           "  --max_peers=N --max_sessions=N\n"
           "  --link_grace_ms=N    keep silent links for resumption (default 20000)\n"
           "  --reserve_ms=N       keep slots of lost players (default 180000)\n"
           "  --pending_ms=N       confirmation timeout (default 60000)\n"
           "  --log_level=0..3 --log_file=PATH --status_interval=SECONDS\n",
           GCS_VERSION, argv0, GCS_DEFAULT_PORT);
}

int main(int argc, char *argv[])
{
    app_cfg a;
    gcs_io io;
    sock_t lfd, ufd = SOCK_INVALID;
    int i;
    gc_u32 last_status;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    memset(&a, 0, sizeof(a));
    a.port = GCS_DEFAULT_PORT;
    a.discovery = 1;
    a.status_interval = 300;
    gcs_default_config(&a.core);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            if (!load_config(&a, argv[++i])) {
                return 2;
            }
        } else if (!strncmp(argv[i], "--", 2) && strchr(argv[i], '=')) {
            char key[64];
            const char *eq = strchr(argv[i], '=');
            snprintf(key, sizeof(key), "%.*s", (int)(eq - argv[i] - 2), argv[i] + 2);
            if (!set_option(&a, key, eq + 1)) {
                fprintf(stderr, "unknown option %s\n", argv[i]);
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    g_log_level = a.core.log_level;
    if (a.logfile[0]) {
        g_logfile = fopen(a.logfile, "a");
    }

    for (i = 0; i < MAX_CONNS; i++) {
        g_conns[i].fd = SOCK_INVALID;
    }
    lfd = open_listener(a.bind_addr, a.port, SOCK_STREAM);
    if (lfd == SOCK_INVALID) {
        logmsg(0, "cannot listen on %s:%d", a.bind_addr[0] ? a.bind_addr : "*", a.port);
        return 1;
    }
    if (a.discovery) {
        ufd = open_listener(a.bind_addr, a.port, SOCK_DGRAM);
        if (ufd == SOCK_INVALID) {
            logmsg(1, "LAN discovery disabled: cannot bind UDP port %d", a.port);
        }
    }

    io.write = io_write;
    io.close = io_close;
    io.log = log_line;
    g_core = gcs_core_create(&a.core, &io, NULL, (gc_u32)time(NULL) ^ (gc_u32)now_ms());
    if (!g_core) {
        return 1;
    }
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGUSR1, on_signal);
#endif
    logmsg(2, "GameComms server %s '%s' listening on %s:%d (protocol %d)%s", GCS_VERSION, a.core.server_name,
           a.bind_addr[0] ? a.bind_addr : "*", a.port, GC_PROTO_VERSION_MAX,
           a.core.access_key[0] ? ", access key required" : "");
    last_status = now_ms();

    while (!g_stop) {
        static struct pollfd pfd[MAX_CONNS + 2];
        static net_conn *map[MAX_CONNS + 2];
        int n = 0, r;
        gc_u32 now;

        pfd[n].fd = lfd;
        pfd[n].events = POLLIN;
        map[n++] = NULL;
        if (ufd != SOCK_INVALID) {
            pfd[n].fd = ufd;
            pfd[n].events = POLLIN;
            map[n++] = NULL;
        }
        for (i = 0; i < MAX_CONNS; i++) {
            if (g_conns[i].used) {
                pfd[n].fd = g_conns[i].fd;
                pfd[n].events = POLLIN | (g_conns[i].out_len ? POLLOUT : 0);
                map[n++] = &g_conns[i];
            }
        }
        r = poll(pfd, (unsigned)n, 50);
        now = now_ms();
        if (r > 0) {
            for (i = 0; i < n; i++) {
                net_conn *c = map[i];
                if (!pfd[i].revents) {
                    continue;
                }
                if (pfd[i].fd == lfd) {
                    accept_new(lfd, now);
                    continue;
                }
                if (pfd[i].fd == ufd) {
                    udp_discovery(ufd, a.port, a.core.server_name);
                    continue;
                }
                if (!c || !c->used) {
                    continue;
                }
                if (pfd[i].revents & POLLOUT) {
                    try_flush(c);
                }
                if (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                    gc_u8 buf[8192];
                    int k = (int)recv(c->fd, (char *)buf, sizeof(buf), 0);
                    if (k > 0) {
                        gcs_core_conn_input(g_core, &c->conn, buf, (size_t)k, now);
                    } else if (k == 0 || !SOCK_EAGAIN(sock_err())) {
                        c->closing = 1;
                    }
                }
            }
        }
        gcs_core_poll(g_core, now);
        for (i = 0; i < MAX_CONNS; i++) {
            if (g_conns[i].used && g_conns[i].closing) {
                conn_free(&g_conns[i], now);
            }
        }
        if (g_dump || (a.status_interval > 0 && (gc_u32)(now - last_status) >= (gc_u32)a.status_interval * 1000u)) {
            g_dump = 0;
            last_status = now;
            gcs_core_dump(g_core);
        }
    }

    logmsg(2, "shutting down");
    for (i = 0; i < MAX_CONNS; i++) {
        if (g_conns[i].used) {
            conn_free(&g_conns[i], now_ms());
        }
    }
    gcs_core_destroy(g_core);
    sock_close(lfd);
    if (ufd != SOCK_INVALID) {
        sock_close(ufd);
    }
    if (g_logfile) {
        fclose(g_logfile);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
