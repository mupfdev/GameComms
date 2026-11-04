/* Tests for CRC, COBS, reader/writer and framer. SPDX-License-Identifier: MIT */
#include "gc_codec.h"
#include "gc_test.h"

static void test_crc(void)
{
    /* CRC-16/CCITT-FALSE check value */
    CHECK_EQ(gc_crc16((const gc_u8 *)"123456789", 9), 0x29B1);
    CHECK_EQ(gc_crc16((const gc_u8 *)"", 0), 0xFFFF);
}

static void test_cobs_vectors(void)
{
    /* vectors from the COBS paper / Wikipedia */
    struct
    {
        gc_u8 in[8];
        size_t n;
        gc_u8 out[10];
        size_t m;
    } v[] = {
        { { 0x00 }, 1, { 0x01, 0x01 }, 2 },
        { { 0x00, 0x00 }, 2, { 0x01, 0x01, 0x01 }, 3 },
        { { 0x11, 0x22, 0x00, 0x33 }, 4, { 0x03, 0x11, 0x22, 0x02, 0x33 }, 5 },
        { { 0x11, 0x22, 0x33, 0x44 }, 4, { 0x05, 0x11, 0x22, 0x33, 0x44 }, 5 },
        { { 0x11, 0x00, 0x00, 0x00 }, 4, { 0x02, 0x11, 0x01, 0x01, 0x01 }, 5 },
    };
    size_t i;
    for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        gc_u8 enc[16], dec[16];
        size_t n = gc_cobs_encode(v[i].in, v[i].n, enc, sizeof(enc));
        CHECK_EQ(n, v[i].m);
        CHECK(memcmp(enc, v[i].out, n) == 0);
        CHECK_EQ(gc_cobs_decode(enc, n, dec, sizeof(dec)), v[i].n);
        CHECK(memcmp(dec, v[i].in, v[i].n) == 0);
    }
}

static void test_cobs_long_runs(void)
{
    gc_u8 in[1000], enc[1100], dec[1000];
    size_t len, n, k;
    for (len = 0; len < 600; len += 37) {
        for (k = 0; k < len; k++) {
            in[k] = (gc_u8)(k % 7 == 3 ? 0 : (k % 251) + 1);
        }
        n = gc_cobs_encode(in, len, enc, sizeof(enc));
        CHECK(n > 0 && n <= GC_COBS_MAX_ENCODED(len));
        for (k = 0; k < n; k++) {
            CHECK(enc[k] != 0);
        }
        CHECK_EQ(gc_cobs_decode(enc, n, dec, sizeof(dec)), len);
        CHECK(memcmp(in, dec, len) == 0);
    }
    /* 254+ non-zero run boundary */
    for (k = 0; k < 700; k++) {
        in[k] = 0x55;
    }
    n = gc_cobs_encode(in, 700, enc, sizeof(enc));
    CHECK_EQ(gc_cobs_decode(enc, n, dec, sizeof(dec)), 700);
    CHECK(memcmp(in, dec, 700) == 0);
    /* in-place decode */
    n = gc_cobs_encode(in, 300, enc, sizeof(enc));
    CHECK_EQ(gc_cobs_decode(enc, n, enc, sizeof(enc)), 300);
    CHECK(memcmp(in, enc, 300) == 0);
}

static void test_cobs_malformed(void)
{
    gc_u8 bad1[] = { 0x05, 0x11, 0x22 }; /* truncated */
    gc_u8 bad2[] = { 0x02, 0x11, 0x00, 0x01 };
    gc_u8 out[16];
    CHECK_EQ(gc_cobs_decode(bad1, sizeof(bad1), out, sizeof(out)), GC_COBS_ERROR);
    CHECK_EQ(gc_cobs_decode(bad2, sizeof(bad2), out, sizeof(out)), GC_COBS_ERROR);
    CHECK_EQ(gc_cobs_encode(bad1, 3, out, 2), 0); /* capacity */
}

static void test_rw(void)
{
    gc_u8 buf[32];
    gc_writer w;
    gc_reader r;
    char s[8];
    gc_w_init(&w, buf, sizeof(buf));
    gc_w_u8(&w, 0xAB);
    gc_w_u16(&w, 0x1234);
    gc_w_u32(&w, 0xDEADBEEF);
    gc_w_str8(&w, "hello world", 11);
    CHECK(!w.overflow);
    gc_r_init(&r, buf, w.len);
    CHECK_EQ(gc_r_u8(&r), 0xAB);
    CHECK_EQ(gc_r_u16(&r), 0x1234);
    CHECK_EQ(gc_r_u32(&r), 0xDEADBEEF);
    CHECK_EQ(gc_r_str8(&r, s, sizeof(s)), 7); /* truncated */
    CHECK(strcmp(s, "hello w") == 0);
    CHECK(!r.error);
    gc_r_u8(&r);
    CHECK(r.error);
    gc_w_init(&w, buf, 3);
    gc_w_u32(&w, 1);
    CHECK(w.overflow);
}

typedef struct
{
    int n;
    gc_u8 last[GC_FRAME_MAX_BODY];
    size_t last_len;
} sink;
static void sink_cb(void *ctx, const gc_u8 *b, size_t n)
{
    sink *s = (sink *)ctx;
    s->n++;
    memcpy(s->last, b, n);
    s->last_len = n;
}

static void test_framer_roundtrip_fragmented(void)
{
    gc_u8 body[600], wire[GC_FRAME_MAX_WIRE];
    gc_deframer d;
    sink s;
    size_t n, i, step;
    for (i = 0; i < sizeof(body); i++) {
        body[i] = (gc_u8)(i * 7);
    }
    n = gc_frame_encode(body, sizeof(body), wire, sizeof(wire));
    CHECK(n > 0);
    for (step = 1; step < 40; step += 3) {
        memset(&s, 0, sizeof(s));
        gc_deframer_init(&d, sink_cb, &s);
        for (i = 0; i < n; i += step) {
            gc_deframer_feed(&d, wire + i, (i + step <= n) ? step : n - i);
        }
        CHECK_EQ(s.n, 1);
        CHECK_EQ(s.last_len, sizeof(body));
        CHECK(memcmp(s.last, body, sizeof(body)) == 0);
    }
}

static void test_framer_garbage_and_corruption(void)
{
    gc_u8 body[50], wire[GC_FRAME_MAX_WIRE], stream[4000];
    gc_deframer d;
    sink s;
    size_t n, len = 0, i;
    int frames = 0, k;
    for (i = 0; i < sizeof(body); i++) {
        body[i] = (gc_u8)i;
    }
    memset(&s, 0, sizeof(s));
    gc_deframer_init(&d, sink_cb, &s);
    /* leading garbage without delimiter (e.g. connecting mid-frame) */
    for (i = 0; i < 77; i++) {
        stream[len++] = (gc_u8)(1 + gct_rand() % 255);
    }
    for (k = 0; k < 20; k++) {
        n = gc_frame_encode(body, sizeof(body), wire, sizeof(wire));
        if (k % 4 == 1) {
            wire[5] ^= 0x40; /* corrupt -> CRC error */
        } else if (k % 4 == 2) {
            n -= 7; /* truncated frame */
        } else {
            frames++;
        }
        memcpy(stream + len, wire, n);
        len += n;
        if (k % 4 == 2) {
            stream[len++] = 0; /* delimiter after truncation */
        }
    }
    gc_deframer_feed(&d, stream, len);
    CHECK_EQ(s.n, frames);
    CHECK(d.err_crc + d.err_cobs + d.err_short >= 5);
}

static void test_framer_oversize(void)
{
    gc_u8 junk[5000];
    gc_u8 body[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, wire[64];
    gc_deframer d;
    sink s;
    size_t n;
    memset(junk, 0x41, sizeof(junk));
    memset(&s, 0, sizeof(s));
    gc_deframer_init(&d, sink_cb, &s);
    gc_deframer_feed(&d, junk, sizeof(junk));
    n = gc_frame_encode(body, sizeof(body), wire, sizeof(wire));
    gc_deframer_feed(&d, wire, n);
    CHECK_EQ(d.err_oversize, 1);
    CHECK_EQ(s.n, 1);
    CHECK(gc_frame_encode(body, GC_FRAME_MAX_BODY + 1, wire, sizeof(wire)) == 0);
}

static void test_framer_fuzz(void)
{
    gc_deframer d;
    sink s;
    gc_u8 buf[256];
    int i, k;
    memset(&s, 0, sizeof(s));
    gc_deframer_init(&d, sink_cb, &s);
    for (i = 0; i < 20000; i++) {
        int n = (int)(gct_rand() % sizeof(buf));
        for (k = 0; k < n; k++) {
            buf[k] = (gc_u8)(gct_rand() % 4 == 0 ? 0 : gct_rand());
        }
        gc_deframer_feed(&d, buf, (size_t)n);
    }
    CHECK(1); /* must simply not crash (run under ASan/UBSan in CI) */
}

int main(void)
{
    RUN(test_crc);
    RUN(test_cobs_vectors);
    RUN(test_cobs_long_runs);
    RUN(test_cobs_malformed);
    RUN(test_rw);
    RUN(test_framer_roundtrip_fragmented);
    RUN(test_framer_garbage_and_corruption);
    RUN(test_framer_oversize);
    RUN(test_framer_fuzz);
    TEST_MAIN_END();
}
