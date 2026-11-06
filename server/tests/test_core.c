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
    int deaf; /* drop everything the server sends (stalled receiver) */
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

static int log_ctrl_chars; /* log lines must never carry control characters */
static void io_log(void *ctx, int lvl, const char *line)
{
    const unsigned char *u = (const unsigned char *)line;
    (void)ctx;
    (void)lvl;
    for (; *u; u++) {
        if (*u < 0x20 || *u == 0x7F) {
            log_ctrl_chars++;
        }
    }
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
            if (c->deaf) {
                c->n_to_cli = 0;
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

static void test_default_outbound_queue_limit(void)
{
    gcs_config cfg;
    gcs_default_config(&cfg);
    CHECK_EQ(cfg.max_outq, 1024);
}

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
    gc_msg m;
    reset_world();
    new_client(0, "host");
    do_register(0, GC_API_V2);
    new_client(1, "c1");
    do_register(1, GC_API_V2);
    sid = host_open(0, 0xBB, GC_API_V2, 4, 0);
    join(1, sid, 0xBB, GC_API_V2, 0);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_SESSION_STATE;
    m.u.session_state.state = GC_SS_PLAYING;
    send_msg(&C[0], &m);
    pump(3);
    /* A duplicate identity must not evict the original peer or take its slot. */
    C[5] = C[1]; /* Copy the device identity. */
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
    r = find(&C[5], GC_MT_REGISTERED);
    CHECK(r && r->u.registered.result == GC_R_REJECTED);
    CHECK(C[1].open);
    CHECK_EQ(count(&C[1], GC_MT_SESSION_ENDED), 0);

    /* After link expiry, a new peer may not reclaim the reserved slot by ID. */
    disconnect_client(&C[1]);
    pump(501);
    do_register(5, GC_API_V2);
    r = find(&C[5], GC_MT_REGISTERED);
    CHECK(r && r->u.registered.result == GC_R_OK);
    r = join(5, sid, 0xBB, GC_API_V2, 1);
    CHECK(r && r->u.join_result.result == GC_R_CLOSED);
}

static void test_connection_refused_when_peer_limit_reached(void)
{
    gcs_config cfg;
    gcs_io io;
    if (core) {
        gcs_core_destroy(core);
    }
    gcs_default_config(&cfg);
    cfg.max_peers = 0;
    io.write = io_write;
    io.close = io_close;
    io.log = io_log;
    core = gcs_core_create(&cfg, &io, NULL, 42);
    memset(C, 0, sizeof(C));
    new_client(0, "refused");
    CHECK_EQ(C[0].open, 0);
    CHECK_EQ(C[0].close_req, 0);
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

/* ================================================================= */
/* Security regression tests (each one reproduced a real defect)     */
/* ================================================================= */

/* raw link frame on a client's connection, bypassing its gc_link */
static void inject_frame(client *c, gc_u8 type, const gc_u8 *payload, size_t plen)
{
    gc_u8 body[GC_FRAME_MAX_BODY];
    gc_u8 wire[GC_FRAME_MAX_WIRE];
    size_t n;
    memset(body, 0, GC_LINK_HDR_LEN);
    body[0] = type;
    body[1] = 1;
    if (plen) {
        memcpy(body + GC_LINK_HDR_LEN, payload, plen);
    }
    n = gc_frame_encode(body, GC_LINK_HDR_LEN + plen, wire, sizeof(wire));
    CHECK(n > 0);
    memcpy(c->to_srv + c->n_to_srv, wire, n);
    c->n_to_srv += n;
}

static void inject_hello(client *c, gc_u32 link_id, int valid)
{
    gc_u8 h[15];
    h[0] = valid ? GC_LINK_MAGIC_0 : 0x00; /* bad magic -> malformed */
    h[1] = GC_LINK_MAGIC_1;
    h[2] = 1;
    h[3] = 1;
    h[4] = GC_ROLE_TEST;
    h[5] = (gc_u8)(link_id >> 24);
    h[6] = (gc_u8)(link_id >> 16);
    h[7] = (gc_u8)(link_id >> 8);
    h[8] = (gc_u8)link_id;
    h[9] = h[10] = 0;
    h[11] = (gc_u8)(GC_LINK_MAX_MSG >> 8);
    h[12] = (gc_u8)(GC_LINK_MAX_MSG & 0xFF);
    h[13] = GC_LINK_QUEUE;
    h[14] = 0;
    inject_frame(c, GC_FT_HELLO, h, 14);
}

static void world_with(int max_peers, int max_outq, const char *key)
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
    if (max_peers) {
        cfg.max_peers = max_peers;
    }
    if (max_outq) {
        cfg.max_outq = max_outq;
    }
    if (key) {
        copy_str(cfg.access_key, sizeof(cfg.access_key), key);
    }
    io.write = io_write;
    io.close = io_close;
    io.log = io_log;
    core = gcs_core_create(&cfg, &io, NULL, 42);
    memset(C, 0, sizeof(C));
}

/* Second HELLO on an established connection that names another peer's
 * link id.  Originally the old peer was marked dead without leaving its
 * session, was freed, and the session kept a dangling host pointer:
 * heap-use-after-free on the next JOIN. */
static void test_sec_rehello_uaf(void)
{
    gc_msg *r;
    gc_u32 sid;
    reset_world();
    new_client(0, "host");
    new_client(1, "victim");
    new_client(2, "joiner");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    do_register(2, GC_API_V2);
    sid = host_open(0, 0x51, GC_API_V2, 4, 0);
    inject_hello(&C[0], C[1].link_id, 1);
    pump(5);
    r = join(2, sid, 0x51, GC_API_V2, 0);
    /* the offending host was dropped, so its session is gone */
    CHECK(r != NULL && r->u.join_result.result == GC_R_NOT_FOUND);
    CHECK(!C[0].open);
    /* the victim's connection must not have been taken over */
    CHECK_EQ(C[1].close_req, 0);
    CHECK(C[1].open);
}

/* Malformed HELLO on an established, registered link followed by a
 * transport close.  The link was closed silently, the peer then looked
 * "never established" and was freed while still hosting: UAF. */
static void test_sec_bad_hello_then_close_uaf(void)
{
    gc_msg *r;
    gc_u32 sid;
    reset_world();
    new_client(0, "host");
    new_client(2, "joiner");
    do_register(0, GC_API_V2);
    do_register(2, GC_API_V2);
    sid = host_open(0, 0x52, GC_API_V2, 4, 0);
    inject_hello(&C[0], 0xDEAD, 0);
    pump(3);
    disconnect_client(&C[0]);
    pump(3);
    r = join(2, sid, 0x52, GC_API_V2, 0);
    CHECK(r != NULL && r->u.join_result.result == GC_R_NOT_FOUND);
}

/* CLOSE followed by a HELLO naming another peer, in one TCP segment.
 * The HELLO was still processed by the already-killed peer whose
 * connection pointer was NULL: NULL pointer write, server crash.
 * Needs no registration and no access key. */
static void test_sec_close_then_hello_crash(void)
{
    gc_msg m;
    reset_world();
    new_client(0, "attacker");
    new_client(1, "bystander");
    inject_frame(&C[0], GC_FT_CLOSE, (const gc_u8 *)"\x05", 1);
    inject_hello(&C[0], C[1].link_id, 1);
    pump(3);
    CHECK_EQ(C[1].close_req, 0);
    CHECK(C[1].open);
    do_register(1, GC_API_V2);
    memset(&m, 0, sizeof(m));
    CHECK(find(&C[1], GC_MT_REGISTERED) != NULL);
}

/* fill the server's outbound path to a stalled client to exactly the limit */
static void stall_client_queue(int host, int cli, int n)
{
    int i, sent = 0;
    C[cli].deaf = 1;
    while (sent < n) {
        for (i = 0; i < 8 && sent < n; i++, sent++) {
            send_data(host, (gc_u8)1, "fill");
        }
        pump(2);
    }
}

static void test_backpressure_preserves_peer_on_resume(void)
{
    int i;
    gc_u32 sid;
    world_with(0, 4, NULL);
    new_client(0, "host");
    new_client(1, "stalled");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    sid = host_open(0, 0x53, GC_API_V2, 4, 0);
    join(1, sid, 0x53, GC_API_V2, 0);
    C[1].n_msgs = 0;
    stall_client_queue(0, 1, GC_LINK_QUEUE + 4);
    for (i = 0; i < 8; i++) {
        send_data(0, 1, "later");
    }
    pump(300);
    CHECK(C[0].open && C[1].open);
    CHECK(gc_link_is_up(&C[0].link));
    CHECK(C[0].link.q_count > 0);
    CHECK(C[0].link.st_retx > 0);

    disconnect_client(&C[1]);
    C[1].deaf = 0;
    connect_client(&C[1]);
    pump(1000);
    CHECK(C[0].open && C[1].open);
    CHECK_EQ(C[1].link.st_resumes, 1);
    CHECK_EQ(count(&C[1], GC_MT_DATA), GC_LINK_QUEUE + 12);
    for (i = 0; i < C[1].n_msgs; i++) {
        if (C[1].last[i].type == GC_MT_DATA) {
            const char *expected = i < GC_LINK_QUEUE + 4 ? "fill" : "later";
            CHECK_EQ(C[1].last[i].u.data.len, strlen(expected));
            CHECK(memcmp(C[1].last[i].u.data.payload, expected, strlen(expected)) == 0);
        }
    }
    CHECK(find(&C[0], GC_MT_PEER_LEFT) == NULL);
    send_data(1, GC_SLOT_HOST, "still joined");
    pump(10);
    CHECK(find(&C[0], GC_MT_DATA) != NULL);
}

static void test_backpressure_only_blocks_destinations(void)
{
    world_with(0, 4, NULL);
    new_client(0, "host");
    new_client(1, "stalled");
    new_client(2, "other");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    do_register(2, GC_API_V2);
    host_open(0, 0x54, GC_API_V2, 4, 0);
    join(1, 0, 0x54, GC_API_V2, 0);
    join(2, 0, 0x54, GC_API_V2, 0);
    stall_client_queue(0, 1, GC_LINK_QUEUE + 4);
    C[2].n_msgs = 0;
    send_data(0, 2, "targeted");
    pump(10);
    CHECK_EQ(count(&C[2], GC_MT_DATA), 1);
    send_data(0, GC_SLOT_ALL, "broadcast");
    pump(10);
    CHECK_EQ(count(&C[2], GC_MT_DATA), 1);
    CHECK(C[0].open && C[1].open && C[2].open);
    C[1].deaf = 0;
    pump(1000);
    CHECK_EQ(count(&C[2], GC_MT_DATA), 2);
    CHECK(C[0].open && C[1].open && C[2].open);
}

static void test_backpressure_client_to_host(void)
{
    int i, sent;
    world_with(0, 4, NULL);
    new_client(0, "host");
    new_client(1, "client");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    host_open(0, 0x55, GC_API_V2, 4, 0);
    join(1, 0, 0x55, GC_API_V2, 0);
    C[0].n_msgs = 0;
    C[0].deaf = 1;
    for (sent = 0; sent < GC_LINK_QUEUE + 4; ) {
        for (i = 0; i < 8 && sent < GC_LINK_QUEUE + 4; i++, sent++) {
            send_data(1, GC_SLOT_HOST, "fill");
        }
        pump(2);
    }
    for (i = 0; i < 8; i++) {
        send_data(1, GC_SLOT_HOST, "later");
    }
    pump(300);
    CHECK(C[0].open && C[1].open);
    CHECK(C[1].link.q_count > 0);
    C[0].deaf = 0;
    pump(1000);
    CHECK(C[0].open && C[1].open);
    CHECK_EQ(count(&C[0], GC_MT_DATA), GC_LINK_QUEUE + 12);
    CHECK(find(&C[1], GC_MT_SESSION_ENDED) == NULL);
}

/* Re-entrant peer destruction: a send that overflows the stalled
 * client's queue killed it synchronously while session_end() still
 * held the member pointer: NULL pointer dereference, server crash. */
static void test_sec_reentrant_kill_session_end(void)
{
    gc_msg m;
    world_with(0, 4, NULL);
    new_client(0, "host");
    new_client(1, "stalled");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    host_open(0, 0x53, GC_API_V2, 4, 0);
    join(1, 0, 0x53, GC_API_V2, 0);
    stall_client_queue(0, 1, GC_LINK_QUEUE + 4);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_HOST_CLOSE;
    send_msg(&C[0], &m);
    pump(5);
    CHECK(C[0].open);
    CHECK_EQ(C[1].close_req + !C[1].open, 1); /* stalled one was dropped */
}

/* Same defect through KICK. */
static void test_sec_reentrant_kill_kick(void)
{
    gc_msg m;
    world_with(0, 4, NULL);
    new_client(0, "host");
    new_client(1, "stalled");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    host_open(0, 0x54, GC_API_V2, 4, 0);
    join(1, 0, 0x54, GC_API_V2, 0);
    stall_client_queue(0, 1, GC_LINK_QUEUE + 4);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_KICK;
    m.u.peer_left.slot = 1;
    m.u.peer_left.reason = GC_LEFT_KICKED;
    send_msg(&C[0], &m);
    pump(5);
    CHECK(C[0].open);
}

/* A fresh connection must not steal a peer whose connection is alive
 * and that sits at another network address (session hijack). */
static void test_sec_no_hijack_of_attached_peer(void)
{
    gc_u32 sid;
    int i, got = 0;
    reset_world();
    new_client(0, "host");
    new_client(1, "victim");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    sid = host_open(0, 0x55, GC_API_V2, 4, 0);
    join(1, sid, 0x55, GC_API_V2, 0);
    /* attacker reuses the victim's link id from another address */
    memset(&C[2], 0, sizeof(C[2]));
    {
        gc_link_cfg cfg;
        gc_link_ops ops;
        gc_link_default_cfg(&cfg, GC_ROLE_TEST, 1);
        ops.write = cl_write;
        ops.on_msg = cl_msg;
        ops.on_state = cl_state;
        ops.on_hello = NULL;
        ops.can_accept = NULL;
        gc_link_init(&C[2].link, &cfg, &ops, &C[2]);
        C[2].link_id = C[1].link_id;
        C[2].open = 1;
        C[2].conn.net = &C[2];
#ifdef GCS_CONN_HAS_ADDR
        copy_str(C[2].conn.addr, sizeof(C[2].conn.addr), "203.0.113.66");
#endif
        gcs_core_conn_open(core, &C[2].conn, now);
        gc_link_start(&C[2].link, C[2].link_id, now);
        pump(5);
    }
    /* the victim keeps its connection and keeps receiving its traffic */
    CHECK(C[1].open);
    C[1].n_msgs = 0;
    send_data(0, 1, "secret");
    pump(5);
    for (i = 0; i < C[1].n_msgs; i++) {
        got += C[1].last[i].type == GC_MT_DATA;
    }
    CHECK_EQ(got, 1);
    /* and nothing of the victim's session reaches the attacker's socket */
    CHECK_EQ(C[2].link.st_rx_msgs + C[2].link.st_ooo + C[2].link.st_dup, 0);
}

/* Connections that never complete the handshake held peer slots
 * forever; a handful of idle sockets locked everybody out. */
static void test_sec_handshake_timeout(void)
{
    static gcs_conn idle[4];
    int i;
    world_with(4, 0, NULL);
    for (i = 0; i < 4; i++) {
        idle[i].net = &C[4 + i];
        C[4 + i].open = 1;
        gcs_core_conn_open(core, &idle[i], now);
    }
    pump(1500); /* 15 s of silence */
    for (i = 0; i < 4; i++) {
        if (C[4 + i].close_req) {
            gcs_core_conn_closed(core, &idle[i], now);
            C[4 + i].open = 0;
        }
    }
    pump(2);
    new_client(0, "legit");
    CHECK(C[0].open);
    do_register(0, GC_API_V2);
    CHECK(find(&C[0], GC_MT_REGISTERED) != NULL);
}

/* Unlimited access key guesses on one connection. */
static void test_sec_access_key_bruteforce(void)
{
    gc_msg m;
    int i;
    world_with(0, 0, "s3cret");
    new_client(0, "guesser");
    for (i = 0; i < 10 && C[0].open && !C[0].close_req; i++) {
        memset(&m, 0, sizeof(m));
        m.type = GC_MT_REGISTER;
        m.u.reg.api = GC_API_V2;
        m.u.reg.device_id[5] = 9;
        copy_str(m.u.reg.name, sizeof(m.u.reg.name), "g");
        m.u.reg.key[0] = (char)('a' + i);
        send_msg(&C[0], &m);
        pump(3);
    }
    CHECK(i <= 3);
    CHECK(C[0].close_req || !C[0].open);
}

/* Attacker-controlled strings reached the log unfiltered (log forging,
 * terminal escape injection). */
static void test_sec_log_injection(void)
{
    gc_msg m;
    log_ctrl_chars = 0;
    world_with(0, 0, "k");
    new_client(0, "inj");
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_REGISTER;
    m.u.reg.api = GC_API_V2;
    m.u.reg.device_id[5] = 1;
    copy_str(m.u.reg.name, sizeof(m.u.reg.name), "a\nERR forged\x1b[2J");
    send_msg(&C[0], &m);
    pump(3);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_ERROR;
    copy_str(m.u.error.text, sizeof(m.u.error.text), "x\r\nINF fake\x07");
    send_msg(&C[0], &m);
    pump(3);
    CHECK_EQ(log_ctrl_chars, 0);
}

/* xorshift32 output == state: one session id predicts every later one,
 * which defeats hidden (unlisted) sessions that are joined by id. */
static gc_u32 test_rand(void *ctx)
{
    (void)ctx;
    return (gct_rand() << 8) ^ gct_rand();
}
static void test_sec_session_ids_unpredictable(void)
{
    gc_u32 a, b, x;
    int predictable = 0, hi;
    reset_world();
#ifdef GCS_HAVE_SET_RANDOM
    gcs_core_set_random(core, test_rand, NULL);
#else
    (void)test_rand;
#endif
    new_client(0, "h1");
    new_client(1, "h2");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    a = host_open(0, 0x56, GC_API_V2, 4, GC_HF_HIDDEN);
    b = host_open(1, 0x56, GC_API_V2, 4, GC_HF_HIDDEN);
    for (hi = 0; hi < 2; hi++) {
        x = a | (hi ? 0x80000000u : 0);
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        predictable |= (x & 0x7FFFFFFF) == b;
    }
    CHECK(!predictable);
}

/* Functionality preserved: a bridge whose old TCP connection is still
 * half-open reconnects from the same address and resumes its link. */
static void test_sec_resume_same_address_while_attached(void)
{
    gc_u32 sid;
    int i, got = 0;
    reset_world();
    new_client(0, "host");
    new_client(6, "mover"); /* slot 6 = the old transport */
    do_register(0, GC_API_V2);
    do_register(6, GC_API_V2);
    sid = host_open(0, 0x57, GC_API_V2, 4, 0);
    join(6, sid, 0x57, GC_API_V2, 0);
    /* the old TCP connection goes half-open: the server still has it */
    C[6].open = 0;
    /* the same bridge reconnects (same address) on a new connection */
    memset(&C[1], 0, sizeof(C[1]));
    C[1].link = C[6].link;
    C[1].link.ctx = &C[1];
    C[1].link.rx.ctx = &C[1].link;
    C[1].link_id = C[6].link_id;
    gc_link_transport_lost(&C[1].link, now);
    C[1].open = 1;
    C[1].conn.net = &C[1];
    gcs_core_conn_open(core, &C[1].conn, now);
    gc_link_start(&C[1].link, C[1].link_id, now);
    pump(5);
    CHECK(gc_link_is_up(&C[1].link));
    CHECK(C[6].close_req); /* stale connection released */
    C[1].n_msgs = 0;
    send_data(0, 1, "still here");
    pump(5);
    for (i = 0; i < C[1].n_msgs; i++) {
        got += C[1].last[i].type == GC_MT_DATA;
    }
    CHECK_EQ(got, 1);
}

/* Functionality preserved: a detached link resumes from a new address
 * (e.g. the bridge moved to another Wi-Fi). */
static void test_sec_resume_detached_from_new_address(void)
{
    gc_u32 sid;
    int i, got = 0;
    reset_world();
    new_client(0, "host");
    new_client(1, "roamer");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    sid = host_open(0, 0x58, GC_API_V2, 4, 0);
    join(1, sid, 0x58, GC_API_V2, 0);
    disconnect_client(&C[1]);
    pump(10);
    C[1].open = 1;
    C[1].n_to_srv = C[1].n_to_cli = 0;
    memset(&C[1].conn, 0, sizeof(C[1].conn));
    C[1].conn.net = &C[1];
    copy_str(C[1].conn.addr, sizeof(C[1].conn.addr), "198.51.100.7");
    gcs_core_conn_open(core, &C[1].conn, now);
    gc_link_start(&C[1].link, C[1].link_id, now);
    pump(5);
    C[1].n_msgs = 0;
    send_data(0, 1, "welcome back");
    pump(5);
    for (i = 0; i < C[1].n_msgs; i++) {
        got += C[1].last[i].type == GC_MT_DATA;
    }
    CHECK_EQ(got, 1);
}

/* Invalid session state values are refused. */
static void test_sec_session_state_validated(void)
{
    gc_msg m;
    reset_world();
    new_client(0, "host");
    new_client(1, "j");
    do_register(0, GC_API_V2);
    do_register(1, GC_API_V2);
    host_open(0, 0x59, GC_API_V2, 4, 0);
    memset(&m, 0, sizeof(m));
    m.type = GC_MT_SESSION_STATE;
    m.u.session_state.state = 200;
    send_msg(&C[0], &m);
    pump(3);
    CHECK(find(&C[0], GC_MT_ERROR) != NULL);
    /* still a lobby: a join works */
    CHECK(join(1, 0, 0x59, GC_API_V2, 0) && find(&C[1], GC_MT_JOIN_RESULT)->u.join_result.result == GC_R_OK);
}

static const char *only;
#define RUNF(fn)                               \
    do {                                       \
        if (!only || strcmp(only, #fn) == 0) { \
            RUN(fn);                           \
        }                                      \
    } while (0)

int main(int argc, char **argv)
{
    only = argc > 1 ? argv[1] : NULL;
    RUNF(test_default_outbound_queue_limit);
    RUNF(test_register_required);
    RUNF(test_matchmaking_and_slots);
    RUNF(test_incompatible_and_isolation);
    RUNF(test_routing);
    RUNF(test_ordering_under_load);
    RUNF(test_confirm_mode);
    RUNF(test_session_lock_and_rejoin);
    RUNF(test_link_resume_keeps_membership);
    RUNF(test_grace_expiry_and_host_loss);
    RUNF(test_reservation_expiry);
    RUNF(test_kick_and_host_close);
    RUNF(test_duplicate_device_registration);
    RUNF(test_connection_refused_when_peer_limit_reached);
    RUNF(test_malformed_input);
    RUNF(test_sec_rehello_uaf);
    RUNF(test_sec_bad_hello_then_close_uaf);
    RUNF(test_sec_close_then_hello_crash);
    RUNF(test_backpressure_preserves_peer_on_resume);
    RUNF(test_backpressure_only_blocks_destinations);
    RUNF(test_backpressure_client_to_host);
    RUNF(test_sec_reentrant_kill_session_end);
    RUNF(test_sec_reentrant_kill_kick);
    RUNF(test_sec_no_hijack_of_attached_peer);
    RUNF(test_sec_handshake_timeout);
    RUNF(test_sec_access_key_bruteforce);
    RUNF(test_sec_log_injection);
    RUNF(test_sec_session_ids_unpredictable);
    RUNF(test_sec_resume_same_address_while_attached);
    RUNF(test_sec_resume_detached_from_new_address);
    RUNF(test_sec_session_state_validated);
    gcs_core_destroy(core);
    core = NULL;
    TEST_MAIN_END();
}
