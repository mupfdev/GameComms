/* Tests for the GCL link layer over an impaired simulated stream.
 * SPDX-License-Identifier: MIT */
#include "gc_link.h"
#include "gc_test.h"

#define PIPE_CAP 200000
typedef struct
{
    gc_u8 buf[PIPE_CAP];
    size_t len;
} pipe_t;

typedef struct
{
    gc_link link;
    pipe_t *out; /* bytes written by this side */
    int connected;
    gc_u32 rx_count;
    gc_u32 rx_expect; /* next expected message counter */
    int rx_errors;
    int ups, resumes, downs, closes;
    int corrupt_pct; /* per written frame */
    int drop_pct;    /* per written frame */
} side_t;

static void w_cb(void *ctx, const gc_u8 *d, size_t n)
{
    side_t *s = (side_t *)ctx;
    if (!s->connected) {
        return;
    }
    if (s->drop_pct && (int)(gct_rand() % 100) < s->drop_pct) {
        return;
    }
    if (s->out->len + n > PIPE_CAP) {
        return;
    }
    memcpy(s->out->buf + s->out->len, d, n);
    if (s->corrupt_pct && (int)(gct_rand() % 100) < s->corrupt_pct && n > 3) {
        s->out->buf[s->out->len + 1 + gct_rand() % (n - 2)] ^= (gc_u8)(1 + gct_rand() % 255);
    }
    s->out->len += n;
}
static void m_cb(void *ctx, const gc_u8 *m, size_t n)
{
    side_t *s = (side_t *)ctx;
    gc_u32 v;
    if (n < 4) {
        s->rx_errors++;
        return;
    }
    v = ((gc_u32)m[0] << 24) | ((gc_u32)m[1] << 16) | ((gc_u32)m[2] << 8) | m[3];
    if (v != s->rx_expect) {
        s->rx_errors++;
    }
    s->rx_expect = v + 1;
    s->rx_count++;
}
static void st_cb(void *ctx, int ev, int reason)
{
    side_t *s = (side_t *)ctx;
    (void)reason;
    if (ev == GC_LE_UP_FRESH) {
        s->ups++;
    }
    if (ev == GC_LE_UP_RESUMED) {
        s->resumes++;
    }
    if (ev == GC_LE_DOWN) {
        s->downs++;
    }
    if (ev == GC_LE_CLOSED) {
        s->closes++;
    }
}

static pipe_t p_ab, p_ba;
static side_t A, B;
static gc_u32 now_ms;

static void setup_windows(gc_u8 a_window, gc_u8 b_window)
{
    gc_link_cfg ca, cb;
    gc_link_ops ops;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));
    p_ab.len = p_ba.len = 0;
    ops.write = w_cb;
    ops.on_msg = m_cb;
    ops.on_state = st_cb;
    ops.on_hello = NULL;
    ops.can_accept = NULL;
    gc_link_default_cfg(&ca, GC_ROLE_DEVICE, 1);
    gc_link_default_cfg(&cb, GC_ROLE_BRIDGE, 0);
    ca.window = a_window;
    cb.window = b_window;
    gc_link_init(&A.link, &ca, &ops, &A);
    gc_link_init(&B.link, &cb, &ops, &B);
    A.out = &p_ab;
    B.out = &p_ba;
    A.connected = B.connected = 1;
    now_ms = 1000;
}

static void setup(void)
{
    setup_windows((gc_u8)GC_LINK_QUEUE, (gc_u8)GC_LINK_QUEUE);
}

/* deliver pending bytes in random-sized chunks */
static void pump(int steps, int chunked)
{
    int i;
    for (i = 0; i < steps; i++) {
        size_t n;
        gc_u8 tmp[PIPE_CAP];
        now_ms += 10;
        if (p_ab.len) {
            n = p_ab.len;
            memcpy(tmp, p_ab.buf, n);
            p_ab.len = 0;
            if (chunked) {
                size_t k = 0;
                while (k < n) {
                    size_t c = 1 + gct_rand() % 17;
                    if (k + c > n) {
                        c = n - k;
                    }
                    gc_link_input(&B.link, tmp + k, c, now_ms);
                    k += c;
                }
            } else {
                gc_link_input(&B.link, tmp, n, now_ms);
            }
        }
        if (p_ba.len) {
            n = p_ba.len;
            memcpy(tmp, p_ba.buf, n);
            p_ba.len = 0;
            if (chunked) {
                size_t k = 0;
                while (k < n) {
                    size_t c = 1 + gct_rand() % 17;
                    if (k + c > n) {
                        c = n - k;
                    }
                    gc_link_input(&A.link, tmp + k, c, now_ms);
                    k += c;
                }
            } else {
                gc_link_input(&A.link, tmp, n, now_ms);
            }
        }
        gc_link_poll(&A.link, now_ms);
        gc_link_poll(&B.link, now_ms);
    }
}

static gc_u32 a_next, b_next;
static int send_n(side_t *s, gc_u32 *ctr, int n)
{
    int sent = 0;
    while (sent < n) {
        gc_u8 m[40];
        int r;
        m[0] = (gc_u8)(*ctr >> 24);
        m[1] = (gc_u8)(*ctr >> 16);
        m[2] = (gc_u8)(*ctr >> 8);
        m[3] = (gc_u8)*ctr;
        memset(m + 4, 0, sizeof(m) - 4); /* zeros stress COBS */
        r = gc_link_send(&s->link, m, 4 + (*ctr % 36), now_ms);
        if (r == GC_LINK_E_FULL) {
            break;
        }
        CHECK_EQ(r, GC_LINK_OK);
        (*ctr)++;
        sent++;
    }
    return sent;
}

static void test_handshake(void)
{
    setup();
    gc_link_start(&A.link, 0x1234, now_ms);
    pump(5, 0);
    CHECK(gc_link_is_up(&A.link) && gc_link_is_up(&B.link));
    CHECK_EQ(A.ups, 1);
    CHECK_EQ(B.ups, 1);
    CHECK_EQ(A.link.version, 1);
    CHECK_EQ(A.link.peer_window, GC_LINK_QUEUE);
    CHECK_EQ(B.link.peer_window, GC_LINK_QUEUE);
}

static void test_peer_window_and_ack_flush(void)
{
    setup_windows(2, 5);
    a_next = b_next = 0;
    gc_link_start(&A.link, 0x1235, now_ms);
    pump(3, 0);
    CHECK_EQ(A.link.peer_window, 5);
    CHECK_EQ(B.link.peer_window, 2);

    CHECK_EQ(send_n(&A, &a_next, 10), 10);
    CHECK_EQ(send_n(&B, &b_next, 10), 10);
    CHECK_EQ(A.link.q_count, 10);
    CHECK_EQ(A.link.q_sent, 5);
    CHECK_EQ(B.link.q_count, 10);
    CHECK_EQ(B.link.q_sent, 2);

    /* Receiving the first window acknowledges it and immediately flushes
     * the queued messages into the newly opened slots. */
    pump(1, 0);
    CHECK_EQ(A.link.q_sent, 5);
    CHECK_EQ(A.link.q_count, 5);
    pump(30, 0);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.rx_count, b_next);
    CHECK_EQ(A.link.q_count, 0);
    CHECK_EQ(B.link.q_count, 0);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
}

static void test_resume_with_window_shrink(void)
{
    setup_windows(4, 4);
    a_next = b_next = 0;
    gc_link_start(&A.link, 0x1236, now_ms);
    pump(3, 0);
    send_n(&A, &a_next, 8);
    send_n(&B, &b_next, 8);
    p_ab.len = p_ba.len = 0;
    A.connected = B.connected = 0;
    gc_link_transport_lost(&A.link, now_ms);
    gc_link_transport_lost(&B.link, now_ms);
    send_n(&A, &a_next, 2); /* queued messages beyond the prior window */
    A.link.cfg.window = 3;
    B.link.cfg.window = 1;

    A.connected = B.connected = 1;
    gc_link_listen(&B.link, now_ms);
    gc_link_start(&A.link, 0x1236, now_ms);
    pump(1, 0);
    CHECK_EQ(A.link.peer_window, 1);
    CHECK_EQ(B.link.peer_window, 3);
    CHECK_EQ(A.link.q_sent, 1);
    CHECK_EQ(B.link.q_sent, 3);
    pump(100, 1);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.rx_count, b_next);
    CHECK_EQ(A.link.q_count, 0);
    CHECK_EQ(B.link.q_count, 0);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
}

static void test_duplicate_hello_window_update(void)
{
    setup_windows(5, 5);
    a_next = b_next = 0;
    gc_link_start(&A.link, 0x1237, now_ms);
    pump(3, 0);
    send_n(&A, &a_next, 6);
    p_ab.len = 0; /* lose the original in-flight DATA frames */
    B.link.cfg.window = 1;

    gc_link_start(&A.link, 0x1237, now_ms);
    pump(1, 0);
    CHECK_EQ(A.link.peer_window, 1);
    CHECK_EQ(A.link.q_sent, 1);
    pump(100, 1);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.link.q_count, 0);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
}

static void test_zero_window_progress(void)
{
    gc_hello h;
    setup_windows((gc_u8)GC_LINK_QUEUE, 0);
    gc_link_start(&A.link, 0x1238, now_ms);
    pump(3, 0);
    CHECK_EQ(B.link.cfg.window, GC_LINK_QUEUE);
    CHECK_EQ(A.link.peer_window, GC_LINK_QUEUE);

    memset(&h, 0, sizeof(h));
    h.ver_min = GC_PROTO_VERSION_MIN;
    h.ver_max = GC_PROTO_VERSION_MAX;
    h.link_id = 0x1239;
    h.window = 0;
    gc_link_listen(&B.link, now_ms);
    gc_link_accept(&B.link, &h, 0, now_ms);
    CHECK_EQ(B.link.peer_window, 1);
    CHECK_EQ(gc_link_send(&B.link, (const gc_u8 *)"x", 1, now_ms), GC_LINK_OK);
    CHECK_EQ(B.link.q_sent, 1);
    CHECK_EQ(gc_link_send(&B.link, (const gc_u8 *)"y", 1, now_ms), GC_LINK_OK);
    CHECK_EQ(B.link.q_sent, 1);
    CHECK_EQ(B.link.q_count, 2);
}

static void test_delayed_ack_with_inflight_writes(void)
{
    setup_windows(4, 2);
    a_next = b_next = 0;
    gc_link_start(&A.link, 0x123a, now_ms);
    pump(3, 0);
    send_n(&A, &a_next, 4);
    send_n(&B, &b_next, 1);
    CHECK_EQ(A.link.q_sent, 2);

    /* Deliver B's frame carrying a stale ACK before B receives A's DATA. */
    {
        gc_u8 tmp[PIPE_CAP];
        size_t n = p_ba.len;
        memcpy(tmp, p_ba.buf, n);
        p_ba.len = 0;
        gc_link_input(&A.link, tmp, n, ++now_ms);
    }
    CHECK_EQ(A.link.q_sent, 2);

    /* B receives A's in-flight messages, but hold its ACK for now. */
    {
        gc_u8 tmp[PIPE_CAP];
        size_t n = p_ab.len;
        memcpy(tmp, p_ab.buf, n);
        p_ab.len = 0;
        gc_link_input(&B.link, tmp, n, ++now_ms);
    }
    CHECK_EQ(A.link.q_sent, 2);
    CHECK(p_ba.len > 0);

    /* The cumulative ACK releases slots and the pending messages are sent. */
    {
        gc_u8 tmp[PIPE_CAP];
        size_t n = p_ba.len;
        memcpy(tmp, p_ba.buf, n);
        p_ba.len = 0;
        gc_link_input(&A.link, tmp, n, ++now_ms);
    }
    CHECK_EQ(A.link.q_count, 2);
    CHECK_EQ(A.link.q_sent, 2);
    pump(30, 0);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.rx_count, b_next);
    CHECK_EQ(A.link.q_count, 0);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
}

static void test_bulk_bidirectional(void)
{
    int i;
    setup();
    a_next = b_next = 0;
    gc_link_start(&A.link, 1, now_ms);
    pump(3, 0);
    for (i = 0; i < 300; i++) {
        send_n(&A, &a_next, 5);
        send_n(&B, &b_next, 5);
        pump(1, 1);
    }
    pump(100, 1);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.rx_count, b_next);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
    CHECK(a_next > 1000);
}

static void test_corruption_and_loss(void)
{
    int i;
    setup();
    a_next = b_next = 0;
    gc_link_start(&A.link, 2, now_ms);
    pump(3, 0);
    A.corrupt_pct = B.corrupt_pct = 10;
    A.drop_pct = B.drop_pct = 10;
    for (i = 0; i < 400; i++) {
        send_n(&A, &a_next, 3);
        send_n(&B, &b_next, 3);
        pump(2, 1);
    }
    A.corrupt_pct = B.corrupt_pct = A.drop_pct = B.drop_pct = 0;
    pump(800, 1);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.rx_count, b_next);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
    CHECK(A.link.st_retx + B.link.st_retx > 0);
    CHECK(B.link.st_dup + A.link.st_dup + A.link.st_ooo + B.link.st_ooo > 0);
}

static void test_resume_after_transport_loss(void)
{
    int i;
    setup_windows(2, 5);
    a_next = b_next = 0;
    gc_link_start(&A.link, 77, now_ms);
    pump(3, 0);
    send_n(&A, &a_next, 10);
    send_n(&B, &b_next, 10);
    /* transport drops while frames are in flight */
    p_ab.len = 0;
    p_ba.len = 0;
    A.connected = B.connected = 0;
    gc_link_transport_lost(&A.link, now_ms);
    gc_link_transport_lost(&B.link, now_ms);
    CHECK_EQ(A.downs, 1);
    CHECK_EQ(B.downs, 1);
    /* messages may be queued while down */
    send_n(&A, &a_next, 4);
    pump(50, 0);
    /* reconnect, acceptor listens, initiator resumes */
    A.connected = B.connected = 1;
    gc_link_listen(&B.link, now_ms);
    gc_link_start(&A.link, 77, now_ms);
    for (i = 0; i < 100; i++) {
        pump(1, 1);
    }
    CHECK_EQ(A.resumes, 1);
    CHECK_EQ(B.resumes, 1);
    CHECK_EQ(B.rx_count, a_next);
    CHECK_EQ(A.rx_count, b_next);
    CHECK_EQ(A.rx_errors + B.rx_errors, 0);
}

static void test_fresh_when_peer_forgot(void)
{
    setup();
    a_next = b_next = 0;
    gc_link_start(&A.link, 5, now_ms);
    pump(3, 0);
    send_n(&A, &a_next, 3);
    pump(10, 0);
    /* bridge rebooted: acceptor has no session any more */
    gc_link_forget(&B.link);
    gc_link_listen(&B.link, now_ms);
    gc_link_transport_lost(&A.link, now_ms);
    gc_link_start(&A.link, 5, now_ms);
    pump(5, 0);
    CHECK(gc_link_is_up(&A.link));
    CHECK_EQ(A.ups, 2); /* second UP is fresh: upper layer must re-register */
    CHECK_EQ(A.resumes, 0);
}

static void test_dead_peer_detection(void)
{
    setup();
    gc_link_start(&A.link, 9, now_ms);
    pump(3, 0);
    B.connected = 0; /* B goes silent */
    A.connected = 1;
    pump(1200, 0); /* 12 s > dead_ms */
    CHECK_EQ(A.downs, 1);
    CHECK(!gc_link_is_up(&A.link));
}

static void test_keepalive_keeps_link(void)
{
    setup();
    gc_link_start(&A.link, 10, now_ms);
    pump(3, 0);
    pump(3000, 0); /* 30 s idle, keep-alives flow */
    CHECK(gc_link_is_up(&A.link) && gc_link_is_up(&B.link));
    CHECK_EQ(A.downs + B.downs, 0);
}

static void test_queue_full_and_toobig(void)
{
    gc_u8 big[GC_LINK_MAX_MSG + 1];
    setup();
    a_next = 0;
    gc_link_start(&A.link, 11, now_ms);
    pump(3, 0);
    B.connected = 0; /* no acks */
    CHECK_EQ(send_n(&A, &a_next, 100), GC_LINK_QUEUE);
    memset(big, 1, sizeof(big));
    CHECK_EQ(gc_link_send(&A.link, big, sizeof(big), now_ms), GC_LINK_E_TOOBIG);
}

static void test_version_reject(void)
{
    gc_hello h;
    setup();
    memset(&h, 0, sizeof(h));
    h.ver_min = 7;
    h.ver_max = 9;
    h.link_id = 1;
    gc_link_listen(&B.link, now_ms);
    gc_link_accept(&B.link, &h, 0, now_ms);
    CHECK(!gc_link_is_up(&B.link));
    CHECK(p_ba.len > 0); /* REJECT sent */
}

static void test_garbage_input_while_up(void)
{
    gc_u8 junk[3000];
    int i;
    setup();
    a_next = 0;
    gc_link_start(&A.link, 12, now_ms);
    pump(3, 0);
    for (i = 0; i < (int)sizeof(junk); i++) {
        junk[i] = (gc_u8)gct_rand();
    }
    gc_link_input(&B.link, junk, sizeof(junk), now_ms);
    send_n(&A, &a_next, 5);
    pump(50, 1);
    CHECK_EQ(B.rx_count, 5);
    CHECK(gc_link_is_up(&B.link));
}

static int bp_block = 0;
static int bp_cb(void *ctx, const gc_u8 *msg, size_t len)
{
    (void)ctx;
    (void)msg;
    (void)len;
    return !bp_block;
}
static void test_backpressure(void)
{
    gc_link_ops ops;
    setup();
    a_next = 0;
    ops = B.link.ops;
    ops.can_accept = bp_cb;
    B.link.ops = ops;
    gc_link_start(&A.link, 13, now_ms);
    pump(3, 0);
    bp_block = 1;
    send_n(&A, &a_next, 8);
    pump(100, 0);
    CHECK_EQ(B.rx_count, 0);
    CHECK(B.link.st_backpressure > 0);
    CHECK(gc_link_is_up(&A.link));
    bp_block = 0;
    pump(400, 0);
    CHECK_EQ(B.rx_count, 8);
    CHECK_EQ(B.rx_errors, 0);
}

static void pump_delayed(int steps)
{
    static gc_u8 tmp[PIPE_CAP];
    int i;
    for (i = 0; i < steps; i++) {
        size_t n;
        now_ms += 10;
        if ((now_ms / 10) % 150 == 75 && p_ab.len) {
            n = p_ab.len;
            memcpy(tmp, p_ab.buf, n);
            p_ab.len = 0;
            gc_link_input(&B.link, tmp, n, now_ms);
        }
        if ((now_ms / 10) % 150 == 0 && p_ba.len) {
            n = p_ba.len;
            memcpy(tmp, p_ba.buf, n);
            p_ba.len = 0;
            gc_link_input(&A.link, tmp, n, now_ms);
        }
        gc_link_poll(&A.link, now_ms);
        gc_link_poll(&B.link, now_ms);
    }
}

static void test_delayed_tcp_link(void)
{
    setup();
    A.link.cfg.rto_ms = B.link.cfg.rto_ms = 1000;
    A.link.cfg.rto_max_ms = B.link.cfg.rto_max_ms = 5000;
    A.link.cfg.dead_ms = B.link.cfg.dead_ms = 20000;
    A.link.rto_cur = B.link.rto_cur = 1000;
    a_next = 0;
    gc_link_start(&A.link, 14, now_ms);
    pump_delayed(450);
    CHECK(gc_link_is_up(&A.link) && gc_link_is_up(&B.link));
    CHECK_EQ(send_n(&A, &a_next, 16), 16);
    pump_delayed(1000);
    CHECK_EQ(B.rx_count, 16);
    CHECK_EQ(B.rx_errors, 0);
    CHECK_EQ(A.link.q_count, 0);
    CHECK(A.link.st_retx > 0);
    CHECK(gc_link_is_up(&A.link) && gc_link_is_up(&B.link));
}

int main(void)
{
    RUN(test_backpressure);
    RUN(test_delayed_tcp_link);
    RUN(test_handshake);
    RUN(test_peer_window_and_ack_flush);
    RUN(test_resume_with_window_shrink);
    RUN(test_duplicate_hello_window_update);
    RUN(test_zero_window_progress);
    RUN(test_delayed_ack_with_inflight_writes);
    RUN(test_bulk_bidirectional);
    RUN(test_corruption_and_loss);
    RUN(test_resume_after_transport_loss);
    RUN(test_fresh_when_peer_forgot);
    RUN(test_dead_peer_detection);
    RUN(test_keepalive_keeps_link);
    RUN(test_queue_full_and_toobig);
    RUN(test_version_reject);
    RUN(test_garbage_input_while_up);
    TEST_MAIN_END();
}
