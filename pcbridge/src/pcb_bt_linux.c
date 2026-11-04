/** @file pcb_bt_linux.c
 *
 *  Linux Bluetooth side of the PC bridge, using BlueZ over D-Bus.
 *
 *  - registers a Serial Port profile (org.bluez.ProfileManager1): BlueZ
 *    publishes the service record, listens on the RFCOMM channel and hands
 *    every connected N-Gage to the bridge as a socket (Profile1.NewConnection)
 *  - registers a pairing agent (org.bluez.Agent1) that answers PIN requests
 *  - makes the adapter powered, pairable and discoverable
 *
 *  The few D-Bus messages involved are encoded here directly, so the
 *  bridge needs no D-Bus library.  Only the system bus over a Unix socket
 *  with EXTERNAL authentication is supported (that is what BlueZ uses).
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#define _DEFAULT_SOURCE
#include "pcb_bt.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SPP_UUID     "00001101-0000-1000-8000-00805f9b34fb"
#define PROFILE_PATH "/org/gamecomms/profile"
#define AGENT_PATH   "/org/gamecomms/agent"
#define MAX_MSG      (64 * 1024)
#define MAX_FDS      16
#define MAX_PENDING  8

/* ------------------------------------------------------------------ */
/* D-Bus message writer                                                */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint8_t buf[2048];
    size_t len;
    int err;
} dw;

static void dw_pad(dw *w, size_t a)
{
    while (w->len % a) {
        if (w->len >= sizeof(w->buf)) {
            w->err = 1;
            return;
        }
        w->buf[w->len++] = 0;
    }
}

static void dw_bytes(dw *w, const void *p, size_t n)
{
    if (w->len + n > sizeof(w->buf)) {
        w->err = 1;
        return;
    }
    memcpy(w->buf + w->len, p, n);
    w->len += n;
}

static void dw_u8(dw *w, uint8_t v) { dw_bytes(w, &v, 1); }

static void dw_u16(dw *w, uint16_t v)
{
    uint8_t b[2];
    dw_pad(w, 2);
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    dw_bytes(w, b, 2);
}

static void dw_u32(dw *w, uint32_t v)
{
    uint8_t b[4];
    dw_pad(w, 4);
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    dw_bytes(w, b, 4);
}

static void dw_patch_u32(dw *w, size_t at, uint32_t v)
{
    w->buf[at] = (uint8_t)v;
    w->buf[at + 1] = (uint8_t)(v >> 8);
    w->buf[at + 2] = (uint8_t)(v >> 16);
    w->buf[at + 3] = (uint8_t)(v >> 24);
}

/* 's' and 'o' */
static void dw_str(dw *w, const char *s)
{
    size_t n = strlen(s);
    dw_u32(w, (uint32_t)n);
    dw_bytes(w, s, n + 1);
}

/* 'g' */
static void dw_sig(dw *w, const char *s)
{
    size_t n = strlen(s);
    dw_u8(w, (uint8_t)n);
    dw_bytes(w, s, n + 1);
}

/* array: returns the position of the length, call dw_array_end() after
   the elements; elem_align is the alignment of the element type */
static size_t dw_array_begin(dw *w, size_t elem_align, size_t *start)
{
    size_t at;
    dw_u32(w, 0);
    at = w->len - 4;
    dw_pad(w, elem_align);
    *start = w->len;
    return at;
}

static void dw_array_end(dw *w, size_t at, size_t start)
{
    dw_patch_u32(w, at, (uint32_t)(w->len - start));
}

/* one {sv} entry of an a{sv} dictionary */
static void dw_dict_str(dw *w, const char *key, const char *val)
{
    dw_pad(w, 8);
    dw_str(w, key);
    dw_sig(w, "s");
    dw_str(w, val);
}

static void dw_dict_bool(dw *w, const char *key, int val)
{
    dw_pad(w, 8);
    dw_str(w, key);
    dw_sig(w, "b");
    dw_u32(w, val ? 1 : 0);
}

static void dw_dict_u16(dw *w, const char *key, uint16_t val)
{
    dw_pad(w, 8);
    dw_str(w, key);
    dw_sig(w, "q");
    dw_u16(w, val);
}

/* ------------------------------------------------------------------ */
/* D-Bus message reader                                                */
/* ------------------------------------------------------------------ */

typedef struct
{
    const uint8_t *p;
    size_t len;
    size_t pos;
    int err;
} dr;

static void dr_pad(dr *r, size_t a)
{
    while (r->pos % a) {
        r->pos++;
    }
    if (r->pos > r->len) {
        r->err = 1;
    }
}

static uint32_t dr_u32(dr *r)
{
    uint32_t v;
    dr_pad(r, 4);
    if (r->err || r->pos + 4 > r->len) {
        r->err = 1;
        return 0;
    }
    v = (uint32_t)r->p[r->pos] | ((uint32_t)r->p[r->pos + 1] << 8) | ((uint32_t)r->p[r->pos + 2] << 16) |
        ((uint32_t)r->p[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

static uint8_t dr_u8(dr *r)
{
    if (r->err || r->pos + 1 > r->len) {
        r->err = 1;
        return 0;
    }
    return r->p[r->pos++];
}

/* 's' / 'o': copies into out (truncated to n-1) */
static void dr_str(dr *r, char *out, size_t n)
{
    uint32_t l = dr_u32(r);
    if (r->err || r->pos + l + 1 > r->len) {
        r->err = 1;
        if (n) {
            out[0] = '\0';
        }
        return;
    }
    if (n) {
        size_t k = l < n - 1 ? l : n - 1;
        memcpy(out, r->p + r->pos, k);
        out[k] = '\0';
    }
    r->pos += l + 1;
}

static void dr_sig(dr *r, char *out, size_t n)
{
    uint8_t l = dr_u8(r);
    if (r->err || r->pos + l + 1 > r->len) {
        r->err = 1;
        if (n) {
            out[0] = '\0';
        }
        return;
    }
    if (n) {
        size_t k = l < n - 1 ? l : n - 1;
        memcpy(out, r->p + r->pos, k);
        out[k] = '\0';
    }
    r->pos += (size_t)l + 1;
}

/* ------------------------------------------------------------------ */
/* connection                                                          */
/* ------------------------------------------------------------------ */

typedef struct
{
    int type; /* 1 call, 2 return, 3 error, 4 signal */
    int flags;
    uint32_t serial;
    uint32_t reply_serial;
    uint32_t nfds;
    char path[128];
    char iface[64];
    char member[64];
    char error[96];
    char sender[64];
    char sig[32];
    const uint8_t *body;
    size_t body_len;
} dmsg;

typedef struct
{
    int fd;
    unsigned char addr[6];
} pending_conn;

static int g_bus = -1;
static uint32_t g_serial;
static uint8_t g_rx[MAX_MSG];
static size_t g_rx_len;
static int g_fds[MAX_FDS]; /* received with the bytes in g_rx */
static int g_nfds;
static pending_conn g_pending[MAX_PENDING];
static int g_npending;
static char g_pin[17];
static char g_adapter_path[64];
static int g_profile_registered;
static int g_agent_registered;
static void (*g_log)(int level, const char *fmt, ...);

static int bus_send(const dw *w)
{
    size_t off = 0;
    if (w->err) {
        return -1;
    }
    while (off < w->len) {
        ssize_t n = send(g_bus, w->buf + off, w->len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd p;
                p.fd = g_bus;
                p.events = POLLOUT;
                poll(&p, 1, 1000);
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* one header field with a string or object path value */
static void field_str(dw *m, uint8_t code, const char *type, const char *val)
{
    dw_pad(m, 8);
    dw_u8(m, code);
    dw_sig(m, type);
    dw_str(m, val);
}

/* builds header + body; body must be 8-aligned at its start (it is) */
static int bus_message(int type, int flags, const char *dest, const char *path, const char *iface,
                       const char *member, uint32_t reply_serial, const char *errname, const char *sig,
                       const dw *body, uint32_t *serial_out)
{
    dw m;
    size_t at, start;
    memset(&m, 0, sizeof(m));
    dw_u8(&m, 'l');
    dw_u8(&m, (uint8_t)type);
    dw_u8(&m, (uint8_t)flags);
    dw_u8(&m, 1);
    dw_u32(&m, body ? (uint32_t)body->len : 0);
    dw_u32(&m, ++g_serial);
    at = dw_array_begin(&m, 8, &start);
    if (path) {
        field_str(&m, 1, "o", path);
    }
    if (iface) {
        field_str(&m, 2, "s", iface);
    }
    if (member) {
        field_str(&m, 3, "s", member);
    }
    if (errname) {
        field_str(&m, 4, "s", errname);
    }
    if (reply_serial) {
        dw_pad(&m, 8);
        dw_u8(&m, 5);
        dw_sig(&m, "u");
        dw_u32(&m, reply_serial);
    }
    if (dest) {
        field_str(&m, 6, "s", dest);
    }
    if (sig && sig[0]) {
        dw_pad(&m, 8);
        dw_u8(&m, 8);
        dw_sig(&m, "g");
        dw_sig(&m, sig);
    }
    dw_array_end(&m, at, start);
    dw_pad(&m, 8);
    if (body) {
        dw_bytes(&m, body->buf, body->len);
    }
    if (serial_out) {
        *serial_out = g_serial;
    }
    return bus_send(&m);
}

static int bus_call(const char *dest, const char *path, const char *iface, const char *member, const char *sig,
                    const dw *body, uint32_t *serial)
{
    return bus_message(1, 0, dest, path, iface, member, 0, NULL, sig, body, serial);
}

static void bus_reply(const dmsg *m, const char *sig, const dw *body)
{
    if (m->flags & 1) {
        return; /* NO_REPLY_EXPECTED */
    }
    bus_message(2, 1, m->sender[0] ? m->sender : NULL, NULL, NULL, NULL, m->serial, NULL, sig, body, NULL);
}

static void bus_error(const dmsg *m, const char *name)
{
    if (m->flags & 1) {
        return;
    }
    bus_message(3, 1, m->sender[0] ? m->sender : NULL, NULL, NULL, NULL, m->serial, name, NULL, NULL, NULL);
}

/* reads what is available into g_rx (blocking up to timeout_ms); returns
   -1 when the bus connection is gone */
static int bus_read(int timeout_ms)
{
    struct pollfd p;
    struct msghdr mh;
    struct iovec iov;
    union
    {
        struct cmsghdr h;
        char b[CMSG_SPACE(sizeof(int) * MAX_FDS)];
    } ctl;
    struct cmsghdr *c;
    ssize_t n;

    p.fd = g_bus;
    p.events = POLLIN;
    if (poll(&p, 1, timeout_ms) <= 0) {
        return 0;
    }
    if (g_rx_len >= sizeof(g_rx)) {
        return -1; /* message too large */
    }
    memset(&mh, 0, sizeof(mh));
    iov.iov_base = g_rx + g_rx_len;
    iov.iov_len = sizeof(g_rx) - g_rx_len;
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = ctl.b;
    mh.msg_controllen = sizeof(ctl.b);
    n = recvmsg(g_bus, &mh, MSG_CMSG_CLOEXEC);
    if (n == 0) {
        return -1;
    }
    if (n < 0) {
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    }
    g_rx_len += (size_t)n;
    for (c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int k, cnt = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            int *fds = (int *)CMSG_DATA(c);
            for (k = 0; k < cnt; k++) {
                if (g_nfds < MAX_FDS) {
                    g_fds[g_nfds++] = fds[k];
                } else {
                    close(fds[k]);
                }
            }
        }
    }
    return 1;
}

/* takes the first complete message out of g_rx; returns its total size
   (0 = incomplete, -1 = corrupt) */
static long bus_parse(dmsg *m)
{
    dr r;
    uint32_t fields_len, total;
    if (g_rx_len < 16) {
        return 0;
    }
    if (g_rx[0] != 'l') {
        return -1; /* big endian buses do not exist in practice */
    }
    memset(m, 0, sizeof(*m));
    m->type = g_rx[1];
    m->flags = g_rx[2];
    r.p = g_rx;
    r.len = g_rx_len;
    r.pos = 4;
    r.err = 0;
    m->body_len = dr_u32(&r);
    m->serial = dr_u32(&r);
    fields_len = dr_u32(&r);
    total = 16 + fields_len;
    total = (total + 7) & ~7u;
    if (m->body_len > MAX_MSG || fields_len > MAX_MSG) {
        return -1;
    }
    total += (uint32_t)m->body_len;
    if (total > sizeof(g_rx)) {
        return -1;
    }
    if (g_rx_len < total) {
        return 0;
    }
    r.len = 16 + fields_len;
    while (r.pos < r.len && !r.err) {
        uint8_t code;
        char sig[8];
        dr_pad(&r, 8);
        if (r.pos >= r.len) {
            break;
        }
        code = dr_u8(&r);
        dr_sig(&r, sig, sizeof(sig));
        switch (sig[0]) {
        case 'u':
        {
            uint32_t v = dr_u32(&r);
            if (code == 5) {
                m->reply_serial = v;
            } else if (code == 9) {
                m->nfds = v;
            }
            break;
        }
        case 'g':
            if (code == 8) {
                dr_sig(&r, m->sig, sizeof(m->sig));
            } else {
                dr_sig(&r, NULL, 0);
            }
            break;
        case 's':
        case 'o':
        {
            char tmp[128];
            char *dst = tmp;
            size_t dn = sizeof(tmp);
            if (code == 1) {
                dst = m->path;
                dn = sizeof(m->path);
            } else if (code == 2) {
                dst = m->iface;
                dn = sizeof(m->iface);
            } else if (code == 3) {
                dst = m->member;
                dn = sizeof(m->member);
            } else if (code == 4) {
                dst = m->error;
                dn = sizeof(m->error);
            } else if (code == 7) {
                dst = m->sender;
                dn = sizeof(m->sender);
            }
            dr_str(&r, dst, dn);
            break;
        }
        default:
            return -1;
        }
    }
    if (r.err) {
        return -1;
    }
    m->body = g_rx + ((16 + fields_len + 7) & ~7u);
    return (long)total;
}

static void bus_consume(long total, const dmsg *m)
{
    int k;
    /* the message's file descriptors are the first nfds received */
    int used = (int)m->nfds < g_nfds ? (int)m->nfds : g_nfds;
    for (k = 0; k < used; k++) {
        if (g_fds[k] >= 0) {
            close(g_fds[k]); /* not taken by the handler */
        }
    }
    memmove(g_fds, g_fds + used, (size_t)(g_nfds - used) * sizeof(int));
    g_nfds -= used;
    memmove(g_rx, g_rx + total, g_rx_len - (size_t)total);
    g_rx_len -= (size_t)total;
}

/* ------------------------------------------------------------------ */
/* incoming method calls                                               */
/* ------------------------------------------------------------------ */

/* "/org/bluez/hci0/dev_00_60_57_AE_51_6B" -> 00:60:57:ae:51:6b */
static void path_to_addr(const char *path, unsigned char a[6])
{
    const char *d = strstr(path, "dev_");
    unsigned v[6];
    memset(a, 0, 6);
    if (d && sscanf(d + 4, "%2x_%2x_%2x_%2x_%2x_%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        int i;
        for (i = 0; i < 6; i++) {
            a[i] = (unsigned char)v[i];
        }
    }
}

static void handle_call(const dmsg *m)
{
    dr r;
    char dev[128] = "";
    r.p = m->body;
    r.len = m->body_len;
    r.pos = 0;
    r.err = 0;
    if (m->sig[0] == 'o' || m->sig[0] == 's') {
        dr_str(&r, dev, sizeof(dev));
    }

    if (!strcmp(m->path, PROFILE_PATH) && !strcmp(m->iface, "org.bluez.Profile1")) {
        if (!strcmp(m->member, "NewConnection") && (m->sig[0] == 'o' || m->sig[0] == 's') && m->sig[1] == 'h') {
            uint32_t idx = dr_u32(&r);
            if (r.err || (int)idx >= g_nfds || (int)idx >= (int)m->nfds || g_npending >= MAX_PENDING) {
                bus_error(m, "org.bluez.Error.Rejected");
                return;
            }
            g_pending[g_npending].fd = g_fds[idx];
            g_fds[idx] = -1; /* taken */
            path_to_addr(dev, g_pending[g_npending].addr);
            g_npending++;
            bus_reply(m, NULL, NULL);
            return;
        }
        if (!strcmp(m->member, "RequestDisconnection") || !strcmp(m->member, "Release")) {
            bus_reply(m, NULL, NULL);
            return;
        }
    } else if (!strcmp(m->path, AGENT_PATH) && !strcmp(m->iface, "org.bluez.Agent1")) {
        unsigned char a[6];
        path_to_addr(dev, a);
        if (!strcmp(m->member, "RequestPinCode")) {
            dw b;
            memset(&b, 0, sizeof(b));
            dw_str(&b, g_pin);
            bus_reply(m, "s", &b);
            g_log(2, "pairing request from %02x:%02x:%02x:%02x:%02x:%02x answered with the PIN", a[0], a[1], a[2],
                  a[3], a[4], a[5]);
            return;
        }
        if (!strcmp(m->member, "RequestPasskey")) {
            dw b;
            memset(&b, 0, sizeof(b));
            dw_u32(&b, (uint32_t)strtoul(g_pin, NULL, 10));
            bus_reply(m, "u", &b);
            return;
        }
        if (!strcmp(m->member, "RequestConfirmation") || !strcmp(m->member, "RequestAuthorization") ||
            !strcmp(m->member, "AuthorizeService") || !strcmp(m->member, "DisplayPinCode") ||
            !strcmp(m->member, "DisplayPasskey") || !strcmp(m->member, "Cancel") || !strcmp(m->member, "Release")) {
            bus_reply(m, NULL, NULL);
            return;
        }
    }
    bus_error(m, "org.freedesktop.DBus.Error.UnknownMethod");
}

/* processes complete messages; with want != 0 returns 1 when the reply to
   that serial arrived (error name copied to err, "" on success) */
static int bus_process(uint32_t want, char *err, size_t errlen)
{
    dmsg m;
    long total;
    int got = 0;
    while ((total = bus_parse(&m)) > 0) {
        if (m.type == 1) {
            handle_call(&m);
        } else if ((m.type == 2 || m.type == 3) && want && m.reply_serial == want) {
            got = 1;
            if (err) {
                snprintf(err, errlen, "%s", m.type == 3 ? m.error : "");
            }
        }
        bus_consume(total, &m);
        if (got) {
            return 1;
        }
    }
    if (total < 0) {
        g_log(0, "D-Bus: unreadable message, closing the connection");
        close(g_bus);
        g_bus = -1;
    }
    return 0;
}

/* call and wait for the reply (5 s); returns 0 and err = "" on success */
static int bus_call_sync(const char *dest, const char *path, const char *iface, const char *member,
                         const char *sig, const dw *body, char *err, size_t errlen)
{
    uint32_t serial;
    int i;
    snprintf(err, errlen, "no reply");
    if (g_bus < 0 || bus_call(dest, path, iface, member, sig, body, &serial) != 0) {
        snprintf(err, errlen, "D-Bus write failed");
        return -1;
    }
    for (i = 0; i < 50 && g_bus >= 0; i++) {
        if (bus_process(serial, err, errlen)) {
            return err[0] ? -1 : 0;
        }
        if (bus_read(100) < 0) {
            snprintf(err, errlen, "D-Bus connection closed");
            return -1;
        }
        if (bus_process(serial, err, errlen)) {
            return err[0] ? -1 : 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* connecting to the system bus                                        */
/* ------------------------------------------------------------------ */

static int read_line(int fd, char *buf, size_t n)
{
    size_t k = 0;
    while (k + 1 < n) {
        char c;
        struct pollfd p;
        ssize_t r;
        p.fd = fd;
        p.events = POLLIN;
        if (poll(&p, 1, 5000) <= 0) {
            return -1;
        }
        r = read(fd, &c, 1);
        if (r <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            buf[k++] = c;
        }
    }
    buf[k] = '\0';
    return 0;
}

static int bus_connect(char *err, size_t errlen)
{
    struct sockaddr_un sa;
    const char *addr = getenv("DBUS_SYSTEM_BUS_ADDRESS");
    char path[108] = "/run/dbus/system_bus_socket";
    char line[256], uid[16], hex[40];
    size_t i;
    dw none;

    if (addr && !strncmp(addr, "unix:path=", 10)) {
        const char *e = strchr(addr + 10, ',');
        size_t n = e ? (size_t)(e - addr - 10) : strlen(addr + 10);
        if (n >= sizeof(path)) {
            n = sizeof(path) - 1;
        }
        memcpy(path, addr + 10, n);
        path[n] = '\0';
    }
    g_bus = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_bus < 0) {
        snprintf(err, errlen, "cannot create a socket for D-Bus");
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    memcpy(sa.sun_path, path, strlen(path) + 1);
    if (connect(g_bus, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        snprintf(err, errlen, "cannot connect to the D-Bus system bus at %s (%s)", path, strerror(errno));
        close(g_bus);
        g_bus = -1;
        return -1;
    }

    /* SASL: EXTERNAL with our uid, then ask for file descriptor passing */
    snprintf(uid, sizeof(uid), "%u", (unsigned)getuid());
    for (i = 0; uid[i]; i++) {
        sprintf(hex + 2 * i, "%02x", (unsigned char)uid[i]);
    }
    snprintf(line, sizeof(line), "AUTH EXTERNAL %s\r\n", hex);
    if (write(g_bus, "", 1) != 1 || write(g_bus, line, strlen(line)) != (ssize_t)strlen(line) ||
        read_line(g_bus, line, sizeof(line)) != 0 || strncmp(line, "OK ", 3) != 0) {
        snprintf(err, errlen, "D-Bus authentication failed");
        close(g_bus);
        g_bus = -1;
        return -1;
    }
    if (write(g_bus, "NEGOTIATE_UNIX_FD\r\n", 19) != 19 || read_line(g_bus, line, sizeof(line)) != 0 ||
        strncmp(line, "AGREE_UNIX_FD", 13) != 0) {
        snprintf(err, errlen, "D-Bus does not allow passing sockets");
        close(g_bus);
        g_bus = -1;
        return -1;
    }
    if (write(g_bus, "BEGIN\r\n", 7) != 7) {
        snprintf(err, errlen, "D-Bus authentication failed");
        close(g_bus);
        g_bus = -1;
        return -1;
    }
    fcntl(g_bus, F_SETFL, fcntl(g_bus, F_GETFL, 0) | O_NONBLOCK);

    memset(&none, 0, sizeof(none));
    if (bus_call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello", NULL, NULL,
                      line, sizeof(line)) != 0) {
        snprintf(err, errlen, "D-Bus Hello failed: %s", line);
        close(g_bus);
        g_bus = -1;
        return -1;
    }
    return 0;
}

/* org.freedesktop.DBus.Properties.Set(adapter, prop, variant) */
static void set_adapter_prop(const char *prop, char type, uint32_t val)
{
    dw b;
    char err[160];
    char sig[2];
    memset(&b, 0, sizeof(b));
    dw_str(&b, "org.bluez.Adapter1");
    dw_str(&b, prop);
    sig[0] = type;
    sig[1] = '\0';
    dw_sig(&b, sig);
    dw_u32(&b, val); /* 'b' and 'u' are both 32 bit */
    if (bus_call_sync("org.bluez", g_adapter_path, "org.freedesktop.DBus.Properties", "Set", "ssv", &b, err,
                      sizeof(err)) != 0) {
        g_log(1, "could not set %s on %s: %s", prop, g_adapter_path, err);
    }
}

/* ------------------------------------------------------------------ */
/* pcb_bt interface                                                    */
/* ------------------------------------------------------------------ */

int pcb_bt_open(const pcb_bt_cfg *cfg, char *err, size_t errlen)
{
    dw b;
    size_t at, start;
    char e[200];

    g_log = cfg->log;
    snprintf(g_pin, sizeof(g_pin), "%s", cfg->pin);
    snprintf(g_adapter_path, sizeof(g_adapter_path), "/org/bluez/%s",
             cfg->adapter && cfg->adapter[0] ? cfg->adapter : "hci0");
    if (bus_connect(err, errlen) != 0) {
        return -1;
    }

    set_adapter_prop("Powered", 'b', 1);
    set_adapter_prop("Pairable", 'b', 1);
    set_adapter_prop("DiscoverableTimeout", 'u', 0);
    set_adapter_prop("Discoverable", 'b', 1);

    if (g_pin[0]) {
        memset(&b, 0, sizeof(b));
        dw_str(&b, AGENT_PATH);
        dw_str(&b, "KeyboardDisplay");
        if (bus_call_sync("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", "os", &b, e,
                          sizeof(e)) == 0) {
            g_agent_registered = 1;
            memset(&b, 0, sizeof(b));
            dw_str(&b, AGENT_PATH);
            bus_call_sync("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", "o", &b, e,
                          sizeof(e));
            g_log(2, "pairing requests are answered with PIN %s", g_pin);
        } else {
            g_log(1, "could not register the pairing agent (%s): pair the N-Gage with bluetoothctl", e);
        }
    }

    memset(&b, 0, sizeof(b));
    dw_str(&b, PROFILE_PATH);
    dw_str(&b, SPP_UUID);
    at = dw_array_begin(&b, 8, &start);
    dw_dict_str(&b, "Name", cfg->service_name);
    dw_dict_str(&b, "Role", "server");
    if (cfg->channel > 0) {
        dw_dict_u16(&b, "Channel", (uint16_t)cfg->channel);
    }
    dw_dict_bool(&b, "RequireAuthentication", 0);
    dw_dict_bool(&b, "RequireAuthorization", 0);
    dw_array_end(&b, at, start);
    if (bus_call_sync("org.bluez", "/org/bluez", "org.bluez.ProfileManager1", "RegisterProfile", "osa{sv}", &b, e,
                      sizeof(e)) != 0) {
        snprintf(err, errlen, "cannot register the Serial Port profile with BlueZ: %s", e);
        pcb_bt_close();
        return -1;
    }
    g_profile_registered = 1;
    if (cfg->channel > 0) {
        g_log(2, "Bluetooth Serial Port service '%s' on RFCOMM channel %d (BlueZ, %s)", cfg->service_name,
              cfg->channel, g_adapter_path);
    } else {
        g_log(2, "Bluetooth Serial Port service '%s' registered (BlueZ, %s)", cfg->service_name, g_adapter_path);
    }
    return 0;
}

sock_t pcb_bt_listener(void)
{
    return g_bus;
}

sock_t pcb_bt_accept(unsigned char addr[6])
{
    int fd;
    if (g_npending == 0 && g_bus >= 0) {
        if (bus_read(0) < 0) {
            g_log(0, "lost the connection to D-Bus; restart the bridge");
            close(g_bus);
            g_bus = -1;
        } else {
            bus_process(0, NULL, 0);
        }
    }
    if (g_npending == 0) {
        return SOCK_INVALID;
    }
    fd = g_pending[0].fd;
    memcpy(addr, g_pending[0].addr, 6);
    memmove(g_pending, g_pending + 1, (size_t)(g_npending - 1) * sizeof(g_pending[0]));
    g_npending--;
    return fd;
}

void pcb_bt_close(void)
{
    char e[160];
    dw b;
    int i;
    if (g_bus >= 0 && g_profile_registered) {
        memset(&b, 0, sizeof(b));
        dw_str(&b, PROFILE_PATH);
        bus_call_sync("org.bluez", "/org/bluez", "org.bluez.ProfileManager1", "UnregisterProfile", "o", &b, e,
                      sizeof(e));
    }
    if (g_bus >= 0 && g_agent_registered) {
        memset(&b, 0, sizeof(b));
        dw_str(&b, AGENT_PATH);
        bus_call_sync("org.bluez", "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent", "o", &b, e,
                      sizeof(e));
    }
    g_profile_registered = 0;
    g_agent_registered = 0;
    for (i = 0; i < g_npending; i++) {
        close(g_pending[i].fd);
    }
    g_npending = 0;
    for (i = 0; i < g_nfds; i++) {
        if (g_fds[i] >= 0) {
            close(g_fds[i]);
        }
    }
    g_nfds = 0;
    if (g_bus >= 0) {
        close(g_bus);
    }
    g_bus = -1;
}
