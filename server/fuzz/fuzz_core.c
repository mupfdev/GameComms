/* libFuzzer harness: stateful server core.
 *
 * The input is a program for up to 8 simulated connections.  Messages are
 * wrapped in correctly sequenced link frames so the fuzzer reaches the
 * session manager, while raw-frame and raw-byte opcodes still let it attack
 * the link layer, the deframer and the handshake.  The harness mirrors the
 * real network layer: close requests are applied after the current pass.
 * SPDX-License-Identifier: MIT */
#include "gcs_core.h"
#include <stdlib.h>
#include <string.h>

#define NC 8
typedef struct
{
    gcs_conn conn;
    int open;
    int close_req;
    gc_u16 seq;
} fconn;

static fconn F[NC];
static gc_u32 now;

static void io_write(void *ctx, gcs_conn *c, const gc_u8 *d, size_t n)
{
    fconn *f = (fconn *)c->net;
    volatile gc_u8 sink = 0;
    size_t i;
    (void)ctx;
    if (!f->open) {
        abort(); /* the core must never write to a closed transport */
    }
    for (i = 0; i < n; i++) {
        sink ^= d[i]; /* touch every byte: ASan validates the buffer */
    }
    (void)sink;
}
static void io_close(void *ctx, gcs_conn *c)
{
    (void)ctx;
    ((fconn *)c->net)->close_req = 1;
}
static void io_log(void *ctx, int lvl, const char *line)
{
    const unsigned char *u = (const unsigned char *)line;
    static int skip = -1;
    (void)ctx;
    (void)lvl;
    if (skip < 0) {
        skip = getenv("FUZZ_NO_LOGCHECK") != NULL;
    }
    for (; *u && !skip; u++) {
        if (*u < 0x20 || *u == 0x7F) {
            abort(); /* log injection */
        }
    }
}

static void send_frame(gcs_core *core, fconn *f, gc_u8 type, gc_u16 seq, const gc_u8 *p, size_t n)
{
    gc_u8 body[GC_FRAME_MAX_BODY], wire[GC_FRAME_MAX_WIRE];
    size_t w;
    if (n > GC_LINK_MAX_MSG) {
        n = GC_LINK_MAX_MSG;
    }
    body[0] = type;
    body[1] = 1;
    body[2] = (gc_u8)(seq >> 8);
    body[3] = (gc_u8)seq;
    body[4] = 0xFF; /* acknowledge everything the server sent */
    body[5] = 0xFF;
    memcpy(body + GC_LINK_HDR_LEN, p, n);
    w = gc_frame_encode(body, GC_LINK_HDR_LEN + n, wire, sizeof(wire));
    if (w) {
        gcs_core_conn_input(core, &f->conn, wire, w, now);
    }
}

static void apply_closes(gcs_core *core)
{
    int i;
    for (i = 0; i < NC; i++) {
        if (F[i].open && F[i].close_req) {
            F[i].open = 0;
            F[i].close_req = 0;
            gcs_core_conn_closed(core, &F[i].conn, now);
        }
    }
}

static gc_u32 frand(void *ctx)
{
    static gc_u32 x = 0x12345678;
    (void)ctx;
    x = x * 1664525u + 1013904223u;
    return x;
}

int LLVMFuzzerTestOneInput(const unsigned char *d, size_t size)
{
    gcs_config cfg;
    gcs_io io;
    gcs_core *core;
    size_t pos = 0;
    int i;

    memset(F, 0, sizeof(F));
    now = 1000;
    gcs_default_config(&cfg);
    cfg.max_peers = 6;
    cfg.max_sessions = 4;
    cfg.max_outq = 6;
    cfg.link_grace_ms = 3000;
    cfg.reserve_ms = 6000;
    cfg.pending_ms = 4000;
    cfg.log_level = 3;
    strcpy(cfg.access_key, "k");
    memset(&io, 0, sizeof(io));
    io.write = io_write;
    io.close = io_close;
    io.log = io_log;
    core = gcs_core_create(&cfg, &io, NULL, 7);
    if (!core) {
        return 0;
    }
#ifdef GCS_HAVE_SET_RANDOM
    gcs_core_set_random(core, frand, NULL);
#else
    (void)frand;
#endif

    while (pos + 2 <= size) {
        gc_u8 op = d[pos++];
        fconn *f = &F[d[pos++] % NC];
        size_t len = 0;

        if (pos < size) {
            len = d[pos] % 64;
            if ((op & 0x0F) >= 3 && pos + 1 < size && d[pos] >= 0xF0) {
                len = 64 + (size_t)d[pos + 1] * 4; /* occasionally large */
                pos++;
            }
            pos++;
        }
        if (len > size - pos) {
            len = size - pos;
        }
        switch (op & 0x0F) {
        case 0: /* connect */
            if (!f->open) {
                memset(f, 0, sizeof(*f));
                f->open = 1;
                f->conn.net = f;
#ifdef GCS_CONN_HAS_ADDR
                f->conn.addr[0] = (char)('A' + (op >> 6)); /* 4 source addresses */
#endif
                gcs_core_conn_open(core, &f->conn, now);
            }
            break;
        case 1: /* transport lost */
            if (f->open) {
                f->open = 0;
                gcs_core_conn_closed(core, &f->conn, now);
            }
            break;
        case 2: /* time passes */
            now += (gc_u32)(op >> 4) * 700u + 10u;
            gcs_core_poll(core, now);
            break;
        case 3: /* raw bytes */
            if (f->open) {
                gcs_core_conn_input(core, &f->conn, d + pos, len, now);
            }
            break;
        case 4: /* HELLO with a small link id (collides between conns) */
        {
            gc_u8 h[15] = { 0x47, 0x43, 1, 1, 9, 0, 0, 0, 0, 0, 0, 4, 0x44, 16, 0 };
            h[8] = (gc_u8)(1 + (op >> 4) % 4);
            if (f->open) {
                send_frame(core, f, GC_FT_HELLO, 0, h, 14);
                f->seq = 0;
            }
            break;
        }
        case 5:
        case 6:
        case 7:
        case 8: /* in-sequence DATA frame carrying a message */
            if (f->open) {
                send_frame(core, f, GC_FT_DATA, f->seq++, d + pos, len);
            }
            break;
        case 9: /* arbitrary frame type / sequence */
            if (f->open && len >= 3) {
                send_frame(core, f, d[pos], (gc_u16)((d[pos + 1] << 8) | d[pos + 2]), d + pos + 3, len - 3);
            }
            break;
        default:
            gcs_core_poll(core, now);
            break;
        }
        pos += len;
        apply_closes(core);
    }
    for (i = 0; i < 40; i++) {
        now += 1000;
        gcs_core_poll(core, now);
        apply_closes(core);
    }
    gcs_core_destroy(core);
    return 0;
}
