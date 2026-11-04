/* Tests for the GCM message codec. SPDX-License-Identifier: MIT */
#include "gc_msg.h"
#include "gc_test.h"

static void roundtrip(const gc_msg *in, gc_msg *out)
{
    gc_u8 buf[GC_LINK_MAX_MSG];
    size_t n = gc_msg_encode(in, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(gc_msg_decode(buf, n, out));
    CHECK_EQ(out->type, in->type);
    /* every strict prefix must be rejected or decode without overrun */
    while (n > 1) {
        gc_msg tmp;
        n--;
        (void)gc_msg_decode(buf, n, &tmp);
    }
}

static void test_register(void)
{
    gc_msg a, b;
    memset(&a, 0, sizeof(a));
    a.type = GC_MT_REGISTER;
    a.u.reg.api = GC_API_V2;
    a.u.reg.variant = 6;
    memcpy(a.u.reg.device_id, "\x00\x02\xee\x11\x22\x33", 6);
    a.u.reg.flags = 3;
    strcpy(a.u.reg.name, "Maxwell");
    roundtrip(&a, &b);
    CHECK_EQ(b.u.reg.api, GC_API_V2);
    CHECK_EQ(b.u.reg.variant, 6);
    CHECK(memcmp(b.u.reg.device_id, a.u.reg.device_id, 6) == 0);
    CHECK(strcmp(b.u.reg.name, "Maxwell") == 0);
}

static void test_host_open_and_list(void)
{
    gc_msg a, b;
    int i;
    memset(&a, 0, sizeof(a));
    a.type = GC_MT_HOST_OPEN;
    a.u.host_open.game_uid = 0x10005B8B;
    a.u.host_open.api = 1;
    a.u.host_open.max_players = 4;
    a.u.host_open.flags = GC_HF_CONFIRM_CLIENTS;
    strcpy(a.u.host_open.name, "Host");
    roundtrip(&a, &b);
    CHECK_EQ(b.u.host_open.game_uid, 0x10005B8B);
    CHECK_EQ(b.u.host_open.flags, 1);
    memset(&a, 0, sizeof(a));
    a.type = GC_MT_SESSION_LIST;
    a.u.list.count = GC_MAX_LIST;
    a.u.list.more = 1;
    for (i = 0; i < GC_MAX_LIST; i++) {
        a.u.list.s[i].session_id = 1000 + i;
        a.u.list.s[i].players = 1;
        a.u.list.s[i].max_players = 4;
        memset(a.u.list.s[i].host_name, 'x', GC_MAX_NAME);
        a.u.list.s[i].host_name[GC_MAX_NAME] = 0;
    }
    roundtrip(&a, &b);
    CHECK_EQ(b.u.list.count, GC_MAX_LIST);
    CHECK_EQ(b.u.list.s[11].session_id, 1011);
    CHECK_EQ(strlen(b.u.list.s[3].host_name), GC_MAX_NAME);
}

static void test_join_and_result(void)
{
    gc_msg a, b;
    memset(&a, 0, sizeof(a));
    a.type = GC_MT_JOIN;
    a.u.join.session_id = 7;
    a.u.join.game_uid = 0x10203040;
    a.u.join.api = 2;
    a.u.join.rejoin = 1;
    roundtrip(&a, &b);
    CHECK_EQ(b.u.join.session_id, 7);
    CHECK_EQ(b.u.join.game_uid, 0x10203040);
    CHECK_EQ(b.u.join.rejoin, 1);
    memset(&a, 0, sizeof(a));
    a.type = GC_MT_JOIN_RESULT;
    a.u.join_result.result = GC_R_INCOMPATIBLE;
    a.u.join_result.slot = 3;
    roundtrip(&a, &b);
    CHECK_EQ(b.u.join_result.result, GC_R_INCOMPATIBLE);
    CHECK_EQ(b.u.join_result.slot, 3);
}

static void test_data_binary(void)
{
    gc_u8 payload[GC_MAX_DATA], buf[GC_LINK_MAX_MSG];
    gc_msg m;
    size_t n;
    int i;
    for (i = 0; i < GC_MAX_DATA; i++) {
        payload[i] = (gc_u8)(i & 0xFF);
    }
    n = gc_msg_encode_data(GC_SLOT_ALL, GC_CH_GAME, payload, sizeof(payload), buf, sizeof(buf));
    CHECK_EQ(n, GC_MAX_DATA + 3);
    CHECK(gc_msg_decode(buf, n, &m));
    CHECK_EQ(m.u.data.addr, GC_SLOT_ALL);
    CHECK_EQ(m.u.data.len, GC_MAX_DATA);
    CHECK(memcmp(m.u.data.payload, payload, GC_MAX_DATA) == 0);
    CHECK_EQ(gc_msg_encode_data(1, 0, payload, GC_MAX_DATA + 1, buf, sizeof(buf)), 0);
    /* empty payload is legal */
    n = gc_msg_encode_data(0, 1, NULL, 0, buf, sizeof(buf));
    CHECK(gc_msg_decode(buf, n, &m));
    CHECK_EQ(m.u.data.len, 0);
    /* header only, truncated */
    CHECK(!gc_msg_decode(buf, 2, &m));
}

static void test_malformed(void)
{
    gc_u8 b1[] = { 0x31, 13, 0 };                      /* list count above GC_MAX_LIST */
    gc_u8 b2[] = { 0x77 };                             /* unknown type */
    gc_u8 b3[] = { 0x10, 2, 6, 1, 2, 3 };              /* truncated register */
    gc_u8 b4[] = { 0x11, 0, 0, 0, 0, 1, 1, 200, 'a' }; /* string length beyond end */
    gc_msg m;
    CHECK(!gc_msg_decode(b1, sizeof(b1), &m));
    CHECK(!gc_msg_decode(b2, sizeof(b2), &m));
    CHECK(!gc_msg_decode(b3, sizeof(b3), &m));
    CHECK(!gc_msg_decode(b4, sizeof(b4), &m));
    CHECK(!gc_msg_decode(b1, 0, &m));
}

static void test_fuzz(void)
{
    gc_u8 buf[300];
    gc_msg m;
    int i, k, ok = 0;
    for (i = 0; i < 200000; i++) {
        int n = 1 + (int)(gct_rand() % 60);
        buf[0] = (gc_u8)(0x10 + gct_rand() % 0x55);
        for (k = 1; k < n; k++) {
            buf[k] = (gc_u8)gct_rand();
        }
        if (gc_msg_decode(buf, (size_t)n, &m)) {
            gc_u8 out[GC_LINK_MAX_MSG];
            ok++;
            /* re-encoding a decoded message must work */
            CHECK(gc_msg_encode(&m, out, sizeof(out)) > 0 || m.type == GC_MT_DATA);
        }
    }
    CHECK(ok > 0);
}

int main(void)
{
    RUN(test_register);
    RUN(test_host_open_and_list);
    RUN(test_join_and_result);
    RUN(test_data_binary);
    RUN(test_malformed);
    RUN(test_fuzz);
    TEST_MAIN_END();
}
