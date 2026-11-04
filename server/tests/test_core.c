/* Session manager tests: matchmaking, slots, routing, resume, timeouts.
 * Uses in-memory connections and simulated clients speaking the real
 * link + message protocol.  SPDX-License-Identifier: MIT */
#include "../../protocol/tests/gc_test.h"
#include "gcs_core.h"

#define BUFCAP 300000
typedef struct client
{
    gcs_conn conn;
    int open;      /* transport connected */
    int close_req; /* core asked to close */
    gc_u8 to_srv[BUFCAP];
    size_t n_to_srv;
    gc_u8 to_cli[BUFCAP];
    size_t n_to_cli;
    gc_link link;
    gc_u32 link_id;
    /* received messages */
    gc_msg last[64];
    gc_u8 payload[64][GC_MAX_DATA];
    int n_msgs;
    char name[16];
    gc_u8 devid[6];
} client;

static gcs_core *core;
static gc_u32 now = 5000;
static client C[8];

static void io_write(void *ctx, gcs_conn *conn, const gc_u8 *d, size_t n)
{
    client *c = (client *)conn->net;
    (void)ctx;
    if (!c->open) {
        return;
    }
    memcpy(c->to_cli + c->n_to_cli, d, n);
    c->n_to_cli += n;
}
static void io_close(void *ctx, gcs_conn *conn)
{
    client *c = (client *)conn->net;
    (void)ctx;
    c->close_req = 1;
}
static int log_enabled(void)
{
#ifdef _MSC_VER
    char *v = NULL;
    size_t len = 0;
    if (_dupenv_s(&v, &len, "GCS_TEST_LOG") != 0 || v == NULL) {
        return 0;
    }
    free(v);
    return 1;
#else
    return getenv("GCS_TEST_LOG") != NULL;
#endif
}

/* bounded, always NUL-terminated copy */
static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void io_log(void *ctx, int lvl, const char *line)
{
    (void)ctx;
    (void)lvl;
    if (log_enabled()) {
        printf("    | %s\n", line);
    }
}

static void cl_write(void *ctx, const gc_u8 *d, size_t n)
{
    client *c = (client *)ctx;
    if (!c->open) {
        return;
    }
    memcpy(c->to_srv + c->n_to_srv, d, n);
    c->n_to_srv += n;
}
static void cl_msg(void *ctx, const gc_u8 *d, size_t n)
{
    client *c = (client *)ctx;
    gc_msg m;
    if (c->n_msgs >= 64) {
        return;
    }
    if (gc_msg_decode(d, n, &m)) {
        if (m.type == GC_MT_DATA) {
            memcpy(c->payload[c->n_msgs], m.u.data.payload, m.u.data.len);
            m.u.data.payload = c->payload[c->n_msgs];
        }
        c->last[c->n_msgs++] = m;
    }
}
static void cl_state(void *ctx, int ev, int r)
{
    (void)ctx;
    (void)ev;
    (void)r;
}

static void pump(int steps)
{
    int s, i;
    for (s = 0; s < steps; s++) {
        now += 10;
        for (i = 0; i < 8; i++) {
            client *c = &C[i];
            if (c->n_to_srv && c->open) {
                gc_u8 t[BUFCAP];
                size_t n = c->n_to_srv;
                memcpy(t, c->to_srv, n);
                c->n_to_srv = 0;
                gcs_core_conn_input(core, &c->conn, t, n, now);
            }
            if (c->n_to_cli && c->open) {
                gc_u8 t[BUFCAP];
                size_t n = c->n_to_cli;
                memcpy(t, c->to_cli, n);
                c->n_to_cli = 0;
                gc_link_input(&c->link, t, n, now);
            }
            if (c->close_req && c->open) {
                c->open = 0;
                c->close_req = 0;
                gc_link_transport_lost(&c->link, now);
                gcs_core_conn_closed(core, &c->conn, now);
            }
            if (c->link.state != GC_LS_IDLE) {
                gc_link_poll(&c->link, now);
            }
        }
        gcs_core_poll(core, now);
    }
}

static void connect_client(client *c)
{
    c->open = 1;
    c->n_to_srv = c->n_to_cli = 0;
    c->close_req = 0;
    c->conn.net = c;
    c->conn.peer = NULL;
    gcs_core_conn_open(core, &c->conn, now);
    gc_link_start(&c->link, c->link_id, now);
    pump(3);
}

static void disconnect_client(client *c)
{
    c->open = 0;
    gc_link_transport_lost(&c->link, now);
    gcs_core_conn_closed(core, &c->conn, now);
}

static void send_msg(client *c, const gc_msg *m)
{
    gc_u8 b[GC_LINK_MAX_MSG];
    size_t n = gc_msg_encode(m, b, sizeof(b));
    CHECK(n > 0);
    CHECK_EQ(gc_link_send(&c->link, b, n, now), GC_LINK_OK);
}

static void new_client(int i, const char *name)
{
    gc_link_cfg cfg;
    gc_link_ops ops;
    client *c = &C[i];
    memset(c, 0, sizeof(*c));
    copy_str(c->name, sizeof(c->name), name);
    c->devid[0] = 0x00;
    c->devid[1] = 0x0e;
    c->devid[5] = (gc_u8)(i + 1);
    c->link_id = 0xA000 + (gc_u32)i;
    gc_link_default_cfg(&cfg, GC_ROLE_TEST, 1);
    ops.write = cl_write;
    ops.on_msg = cl_msg;
    ops.on_state = cl_state;
    ops.on_hello = NULL;
    ops.can_accept = NULL;
    gc_link_init(&c->link, &cfg, &ops, c);
    connect_client(c);
}

static void do_register(int i, gc_u8 api)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_REGISTER;
    m.u.reg.api = api;
    m.u.reg.variant = 6;
    memcpy(m.u.reg.device_id, C[i].devid, 6);
    copy_str(m.u.reg.name, sizeof(m.u.reg.name), C[i].name);
    send_msg(&C[i], &m);
    pump(3);
}

static gc_msg *find(client *c, gc_u8 type)
{
    int i;
    for (i = c->n_msgs - 1; i >= 0; i--) {
        if (c->last[i].type == type) {
            return &c->last[i];
        }
    }
    return NULL;
}
static int count(client *c, gc_u8 type)
{
    int i, n = 0;
    for (i = 0; i < c->n_msgs; i++) {
        n += c->last[i].type == type;
    }
    return n;
}

static gc_u32 host_open(int i, gc_u32 uid, gc_u8 api, gc_u8 maxp, gc_u8 flags)
{
    gc_msg m, *r;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_HOST_OPEN;
    m.u.host_open.game_uid = uid;
    m.u.host_open.api = api;
    m.u.host_open.max_players = maxp;
    m.u.host_open.flags = flags;
    copy_str(m.u.host_open.name, sizeof(m.u.host_open.name), C[i].name);
    send_msg(&C[i], &m);
    pump(3);
    r = find(&C[i], GC_MT_HOST_OPENED);
    CHECK(r != NULL);
    if (!r) {
        return 0;
    }
    CHECK_EQ(r->u.host_opened.result, GC_R_OK);
    return r->u.host_opened.session_id;
}

static gc_msg *join(int i, gc_u32 sid, gc_u32 uid, gc_u8 api, int rejoin)
{
    gc_msg m;
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_JOIN;
    m.u.join.session_id = sid;
    m.u.join.game_uid = uid;
    m.u.join.api = api;
    m.u.join.rejoin = (gc_u8)rejoin;
    send_msg(&C[i], &m);
    pump(3);
    return find(&C[i], GC_MT_JOIN_RESULT);
}

static void send_data(int i, gc_u8 addr, const char *txt)
{
    gc_u8 b[GC_LINK_MAX_MSG];
    size_t n = gc_msg_encode_data(addr, GC_CH_GAME, (const gc_u8 *)txt, strlen(txt), b, sizeof(b));
    CHECK_EQ(gc_link_send(&C[i].link, b, n, now), GC_LINK_OK);
}

static void reset_world(void)
{
    gcs_config cfg;
    gcs_io io;
    if (core) {
        gcs_core_destroy(core);
    }
    gcs_default_config(&cfg);
    cfg.reserve_ms = 30000;
    cfg.link_grace_ms = 5000;
    cfg.pending_ms = 4000;
    io.write = io_write;
    io.close = io_close;
    io.log = io_log;
    core = gcs_core_create(&cfg, &io, NULL, 42);
    memset(C, 0, sizeof(C));
}

/* ----------------------------------------------------------------- */

static void test_register_required(void)
{
    gc_msg m;
    reset_world();
    new_client(0, "anon");
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_DISCOVER;
    m.u.discover.game_uid = 1;
    send_msg(&C[0], &m);
    pump(3);
    CHECK(find(&C[0], GC_MT_ERROR) != NULL);
    CHECK_EQ(find(&C[0], GC_MT_ERROR)->u.error.code, GC_R_NOT_REGISTERED);
    do_register(0, GC_API_V2);
    CHECK(find(&C[0], GC_MT_REGISTERED) != NULL);
    CHECK_EQ(find(&C[0], GC_MT_REGISTERED)->u.registered.result, GC_R_OK);
}

static void test_matchmaking_and_slots(void)
{
    gc_u32 sid;
    gc_msg *r;
    gc_msg m;
    int i;
    reset_world();
    for (i = 0; i < 5; i++) {
        char n[8];
        snprintf(n, sizeof(n), "p%d", i);
        new_client(i, n);
        do_register(i, GC_API_V2);
    }
    sid = host_open(0, 0x10005B8B, GC_API_V2, 4, 0);
    CHECK(sid != 0);
    /* discovery sees exactly this session */
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_DISCOVER;
    m.u.discover.game_uid = 0x10005B8B;
    m.u.discover.api = GC_API_V2;
    send_msg(&C[1], &m);
    pump(3);
    r = find(&C[1], GC_MT_SESSION_LIST);
    CHECK(r && r->u.list.count == 1 && r->u.list.s[0].session_id == sid && r->u.list.s[0].players == 1);
    /* three clients join, get slots 1..3 */
    for (i = 1; i <= 3; i++) {
        r = join(i, sid, 0x10005B8B, GC_API_V2, 0);
        CHECK(r && r->u.join_result.result == GC_R_OK);
        CHECK_EQ(r->u.join_result.slot, i);
    }
    CHECK_EQ(count(&C[0], GC_MT_PEER_JOINED), 3);
    /* fourth client: full */
    r = join(4, sid, 0x10005B8B, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_FULL);
    /* a full session is still listed, but with max players */
    send_msg(&C[4], &m);
    pump(3);
    r = find(&C[4], GC_MT_SESSION_LIST);
    CHECK(r && r->u.list.count == 1 && r->u.list.s[0].players == 4);
}

static void test_incompatible_and_isolation(void)
{
    gc_u32 a, b;
    gc_msg *r;
    gc_msg m;
    reset_world();
    new_client(0, "hostA");
    do_register(0, GC_API_V2);
    new_client(1, "hostB");
    do_register(1, GC_API_V2);
    new_client(2, "cli");
    do_register(2, GC_API_V2);
    a = host_open(0, 0x11111111, GC_API_V2, 2, 0);
    b = host_open(1, 0x22222222, GC_API_V2, 2, 0);
    /* wrong UID for the chosen session */
    r = join(2, a, 0x22222222, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_INCOMPATIBLE);
    /* wrong API family */
    r = join(2, a, 0x11111111, GC_API_V1, 0);
    CHECK(r && r->u.join_result.result == GC_R_INCOMPATIBLE);
    /* discovery of B's UID never lists A */
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_DISCOVER;
    m.u.discover.game_uid = 0x22222222;
    send_msg(&C[2], &m);
    pump(3);
    r = find(&C[2], GC_MT_SESSION_LIST);
    CHECK(r && r->u.list.count == 1 && r->u.list.s[0].session_id == b);
    /* auto-join (session id 0) picks the matching session */
    r = join(2, 0, 0x11111111, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_OK && r->u.join_result.session_id == a);
    /* unknown session */
    r = join(2, 0x7777, 0x11111111, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_NOT_FOUND);
}

static void test_routing(void)
{
    gc_u32 sid;
    int i;
    gc_msg *r;
    reset_world();
    for (i = 0; i < 4; i++) {
        char n[8];
        snprintf(n, sizeof(n), "p%d", i);
        new_client(i, n);
        do_register(i, GC_API_V1);
    }
    sid = host_open(0, 0xCAFE, GC_API_V1, 4, 0);
    for (i = 1; i <= 3; i++) {
        join(i, sid, 0xCAFE, GC_API_V1, 0);
    }
    for (i = 0; i < 4; i++) {
        C[i].n_msgs = 0;
    }
    send_data(0, 2, "unicast-to-2");
    send_data(0, GC_SLOT_ALL, "broadcast");
    send_data(3, GC_SLOT_HOST, "from-3");
    pump(5);
    CHECK_EQ(count(&C[1], GC_MT_DATA), 1);
    CHECK_EQ(count(&C[2], GC_MT_DATA), 2);
    CHECK_EQ(count(&C[3], GC_MT_DATA), 1);
    CHECK_EQ(count(&C[0], GC_MT_DATA), 1);
    r = find(&C[0], GC_MT_DATA);
    CHECK(r && r->u.data.addr == 3 && r->u.data.len == 6 && memcmp(r->u.data.payload, "from-3", 6) == 0);
    r = find(&C[2], GC_MT_DATA);
    CHECK(r && r->u.data.addr == GC_SLOT_HOST && memcmp(r->u.data.payload, "broadcast", 9) == 0);
    CHECK(memcmp(C[2].last[0].u.data.payload, "unicast-to-2", 12) == 0); /* order preserved */
    /* client -> client is not allowed */
    send_data(1, 2, "sneaky");
    pump(3);
    CHECK(find(&C[1], GC_MT_ERROR) != NULL);
    CHECK_EQ(count(&C[2], GC_MT_DATA), 2);
}

static void test_ordering_under_load(void)
{
    gc_u32 sid;
    int i, k, ok = 1;
    reset_world();
    new_client(0, "h");
    do_register(0, GC_API_V2);
    new_client(1, "c");
    do_register(1, GC_API_V2);
    sid = host_open(0, 0xAB, GC_API_V2, 2, 0);
    join(1, sid, 0xAB, GC_API_V2, 0);
    C[1].n_msgs = 0;
    for (k = 0; k < 60; k++) {
        char t[16];
        snprintf(t, sizeof(t), "%d", k);
        while (gc_link_free_slots(&C[0].link) == 0) {
            pump(1);
        }
        send_data(0, 1, t);
        if (C[1].n_msgs > 50) {
            for (i = 0; i < C[1].n_msgs; i++) {
                char e[16];
                snprintf(e, sizeof(e), "%d", k - C[1].n_msgs + 1 + i);
                (void)e;
            }
        }
    }
    pump(20);
    CHECK(C[1].n_msgs == 60 || C[1].n_msgs == 64);
    for (i = 0; i < C[1].n_msgs && i < 60; i++) {
        char e[16];
        snprintf(e, sizeof(e), "%d", i);
        if (memcmp(C[1].last[i].u.data.payload, e, strlen(e)) != 0) {
            ok = 0;
        }
    }
    CHECK(ok);
}

static void test_confirm_mode(void)
{
    gc_u32 sid;
    gc_msg *r;
    gc_msg m;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    new_client(2, "c2");
    do_register(2, GC_API_V2);
    sid = host_open(0, 0x55, GC_API_V2, 4, GC_HF_CONFIRM_CLIENTS);
    r = join(1, sid, 0x55, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_PENDING);
    r = find(&C[0], GC_MT_PEER_REQUEST);
    CHECK(r && r->u.peer.slot == 1 && strcmp(r->u.peer.name, "c1") == 0);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_PEER_ACCEPT;
    m.u.peer_accept.slot = 1;
    m.u.peer_accept.accept = 1;
    send_msg(&C[0], &m);
    pump(3);
    r = find(&C[1], GC_MT_JOIN_RESULT);
    CHECK(r && r->u.join_result.result == GC_R_OK);
    CHECK(find(&C[0], GC_MT_PEER_JOINED) != NULL);
    /* reject the second one */
    r = join(2, sid, 0x55, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_PENDING);
    m.u.peer_accept.slot = 2;
    m.u.peer_accept.accept = 0;
    send_msg(&C[0], &m);
    pump(3);
    r = find(&C[2], GC_MT_JOIN_RESULT);
    CHECK(r && r->u.join_result.result == GC_R_REJECTED);
    /* pending timeout */
    r = join(2, sid, 0x55, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_PENDING);
    pump(500);
    r = find(&C[2], GC_MT_JOIN_RESULT);
    CHECK(r && r->u.join_result.result == GC_R_REJECTED);
}

static void test_session_lock_and_rejoin(void)
{
    gc_u32 sid;
    gc_msg *r;
    gc_msg m;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    new_client(2, "late");
    do_register(2, GC_API_V2);
    sid = host_open(0, 0x66, GC_API_V2, 4, 0);
    join(1, sid, 0x66, GC_API_V2, 0);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_SESSION_STATE;
    m.u.session_state.state = GC_SS_PLAYING;
    send_msg(&C[0], &m);
    pump(3);
    r = join(2, sid, 0x66, GC_API_V2, 0);
    CHECK(r && r->u.join_result.result == GC_R_CLOSED);
    /* c1 leaves temporarily (phone call) */
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_LEAVE;
    m.u.reason.reason = GC_LEFT_LOST;
    send_msg(&C[1], &m);
    pump(3);
    r = find(&C[0], GC_MT_PEER_LEFT);
    CHECK(r && r->u.peer_left.slot == 1 && r->u.peer_left.reason == GC_LEFT_LOST);
    /* ...and rejoins the same slot although the session is locked */
    r = join(1, sid, 0x66, GC_API_V2, 1);
    CHECK(r && r->u.join_result.result == GC_R_OK && r->u.join_result.slot == 1);
    r = find(&C[0], GC_MT_PEER_JOINED);
    CHECK(r && r->u.peer.rejoin == 1);
}

static void test_link_resume_keeps_membership(void)
{
    gc_u32 sid;
    int before;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    sid = host_open(0, 0x77, GC_API_V2, 2, 0);
    join(1, sid, 0x77, GC_API_V2, 0);
    /* TCP of the client drops; host keeps talking */
    disconnect_client(&C[1]);
    send_data(0, 1, "while-away-1");
    send_data(0, 1, "while-away-2");
    pump(100); /* 1 s, below grace */
    before = count(&C[0], GC_MT_PEER_LEFT);
    CHECK_EQ(before, 0);
    C[1].n_msgs = 0;
    connect_client(&C[1]); /* same link id -> resume */
    pump(10);
    CHECK_EQ(count(&C[1], GC_MT_DATA), 2);
    CHECK(C[1].link.st_resumes == 1);
    CHECK_EQ(count(&C[0], GC_MT_PEER_LEFT), 0);
}

static void test_grace_expiry_and_host_loss(void)
{
    gc_u32 sid;
    gc_msg *r;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    new_client(2, "c2");
    do_register(2, GC_API_V2);
    sid = host_open(0, 0x88, GC_API_V2, 4, 0);
    join(1, sid, 0x88, GC_API_V2, 0);
    join(2, sid, 0x88, GC_API_V2, 0);
    /* client 1 vanishes for longer than the grace period (lobby: slot freed) */
    disconnect_client(&C[1]);
    pump(700);
    r = find(&C[0], GC_MT_PEER_LEFT);
    CHECK(r && r->u.peer_left.slot == 1 && r->u.peer_left.reason == GC_LEFT_LOST);
    /* host vanishes: remaining client is told the session ended */
    disconnect_client(&C[0]);
    pump(700);
    r = find(&C[2], GC_MT_SESSION_ENDED);
    CHECK(r && r->u.reason.reason == GC_LEFT_HOST_LOST);
    {
        gcs_stats st;
        gcs_core_stats(core, &st);
        CHECK_EQ(st.sessions, 0);
    }
}

static void test_reservation_expiry(void)
{
    gc_u32 sid;
    gc_msg m;
    gc_msg *r;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    sid = host_open(0, 0x99, GC_API_V2, 2, 0);
    join(1, sid, 0x99, GC_API_V2, 0);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_SESSION_STATE;
    m.u.session_state.state = GC_SS_PAUSED;
    send_msg(&C[0], &m);
    pump(3);
    disconnect_client(&C[1]);
    pump(700); /* > grace -> reserved */
    r = find(&C[0], GC_MT_PEER_LEFT);
    CHECK(r && r->u.peer_left.reason == GC_LEFT_LOST);
    pump(3000); /* > reserve_ms */
    r = find(&C[0], GC_MT_PEER_LEFT);
    CHECK(r && r->u.peer_left.reason == GC_LEFT_EXPIRED);
}

static void test_kick_and_host_close(void)
{
    gc_u32 sid;
    gc_msg m;
    gc_msg *r;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    new_client(2, "c2");
    do_register(2, GC_API_V2);
    sid = host_open(0, 0xAA, GC_API_V2, 4, 0);
    join(1, sid, 0xAA, GC_API_V2, 0);
    join(2, sid, 0xAA, GC_API_V2, 0);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_KICK;
    m.u.peer_left.slot = 1;
    m.u.peer_left.reason = GC_LEFT_KICKED;
    send_msg(&C[0], &m);
    pump(3);
    r = find(&C[1], GC_MT_SESSION_ENDED);
    CHECK(r && r->u.reason.reason == GC_LEFT_KICKED);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_HOST_CLOSE;
    send_msg(&C[0], &m);
    pump(3);
    r = find(&C[2], GC_MT_SESSION_ENDED);
    CHECK(r && r->u.reason.reason == GC_LEFT_HOST_CLOSED);
}

static void test_duplicate_device_registration(void)
{
    gc_u32 sid;
    gc_msg *r;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    sid = host_open(0, 0xBB, GC_API_V2, 4, 0);
    join(1, sid, 0xBB, GC_API_V2, 0);
    memset(&C[1].link, 0, 0);
    /* the same N-Gage comes back through a rebooted bridge (new link id) */
    C[5] = C[1]; /* copy device identity */
    {
        gc_link_cfg cfg;
        gc_link_ops ops;
        client *c = &C[5];
        c->n_msgs = 0;
        c->link_id = 0xBEEF;
        gc_link_default_cfg(&cfg, GC_ROLE_TEST, 1);
        ops.write = cl_write;
        ops.on_msg = cl_msg;
        ops.on_state = cl_state;
        ops.on_hello = NULL;
        ops.can_accept = NULL;
        gc_link_init(&c->link, &cfg, &ops, c);
        connect_client(c);
    }
    do_register(5, GC_API_V2);
    r = join(5, sid, 0xBB, GC_API_V2, 1);
    CHECK(r && r->u.join_result.result == GC_R_OK && r->u.join_result.slot == 1);
}

static void test_malformed_input(void)
{
    gc_u8 junk[2000];
    int i;
    reset_world();
    new_client(0, "x");
    do_register(0, GC_API_V2);
    for (i = 0; i < (int)sizeof(junk); i++) {
        junk[i] = (gc_u8)gct_rand();
    }
    gcs_core_conn_input(core, &C[0].conn, junk, sizeof(junk), now);
    {
        gc_u8 bad[3] = { 0x31, 99, 0 };
        gc_link_send(&C[0].link, bad, 3, now);
    }
    pump(5);
    CHECK(find(&C[0], GC_MT_ERROR) != NULL);
    CHECK(gc_link_is_up(&C[0].link));
}

int main(void)
{
    RUN(test_register_required);
    RUN(test_matchmaking_and_slots);
    RUN(test_incompatible_and_isolation);
    RUN(test_routing);
    RUN(test_ordering_under_load);
    RUN(test_confirm_mode);
    RUN(test_session_lock_and_rejoin);
    RUN(test_link_resume_keeps_membership);
    RUN(test_grace_expiry_and_host_loss);
    RUN(test_reservation_expiry);
    RUN(test_kick_and_host_close);
    RUN(test_duplicate_device_registration);
    RUN(test_malformed_input);
    gcs_core_destroy(core);
    TEST_MAIN_END();
}
