/** @file test_e2e.c
 *
 *  End-to-end scenarios: N-Gage engine <-> ESP32 bridge <-> server,
 *  for both GameComms API generations.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "../../protocol/tests/gc_test.h"
#include "sim.h"

static sim_world W;
#define UID 0x10005B8B

static int data_eq(gcd_event *e, const char *s)
{
    return e && e->len == strlen(s) && memcmp(e->data, s, e->len) == 0;
}

/* ================================================================ */
/* GameComms 1.x                                                     */
/* ================================================================ */

/* host + clients until the game starts */
static void v1_setup(int clients, int start, int min)
{
    int i;
    sim_init(&W, clients + 1, GC_API_V1, UID);
    sim_run(&W, 500); /* links up, registration */
    gcd_v1_start_host(&W.n[0].dev, (gc_u16)start, (gc_u16)min, W.now);
    sim_run(&W, 300);
    for (i = 1; i <= clients; i++)
    {
        W.n[i].dev.cfg.auto_join = 1;
        gcd_v1_start_client(&W.n[i].dev, W.now);
        sim_run(&W, 400);
    }
    sim_run(&W, 500);
}

static void test_v1_startup_four_players(void)
{
    int i;
    v1_setup(3, 4, 4);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_CONNECTED), 3);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_START_GAME), 1);
    for (i = 1; i <= 3; i++)
    {
        CHECK_EQ(sim_count(&W, i, GCD_EV1_HOST_SELECTED), 1);
        CHECK_EQ(sim_count(&W, i, GCD_EV1_HOST_CONNECTED), 1);
        CHECK_EQ(sim_last(&W, i, GCD_EV1_HOST_CONNECTED)->err, 0);
        CHECK_EQ(sim_count(&W, i, GCD_EV1_START_GAME), 1);
        CHECK_EQ(gcd_role(&W.n[i].dev), GCD_ROLE_CLIENT);
        CHECK_EQ(gcd_game_state(&W.n[i].dev), GCD_GS_PLAY);
    }
    CHECK_EQ(gcd_role(&W.n[0].dev), GCD_ROLE_HOST);
    CHECK_EQ(gcd_connect_state(&W.n[0].dev), GCD_CS_CONNECTED);
    /* the client ids are the slots 1..3 and carry the device names */
    CHECK(strcmp(W.n[0].ev[0].dev.name, "ngage1") == 0);
    sim_free(&W);
}

static void test_v1_game_does_not_start_early(void)
{
    v1_setup(1, 3, 2);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_CONNECTED), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_START_GAME), 0);
    CHECK_EQ(gcd_game_state(&W.n[1].dev), GCD_GS_OVER);
    /* sending before the start is a game-over error */
    CHECK_EQ(gcd_v1_send(&W.n[1].dev, GC_SLOT_HOST, (const gc_u8 *)"x", 1, W.now), GCD_KErrGameOver);
    sim_free(&W);
}

static void test_v1_data_routing(void)
{
    v1_setup(3, 4, 2);
    CHECK_EQ(gcd_v1_send(&W.n[0].dev, 2, (const gc_u8 *)"to-two", 6, W.now), 0);
    CHECK_EQ(gcd_v1_send(&W.n[0].dev, GC_SLOT_ALL, (const gc_u8 *)"to-all", 6, W.now), 0);
    CHECK_EQ(gcd_v1_send(&W.n[3].dev, GC_SLOT_HOST, (const gc_u8 *)"from-3", 6, W.now), 0);
    CHECK_EQ(gcd_v1_send(&W.n[0].dev, 9, (const gc_u8 *)"x", 1, W.now), GCD_KErrBadHandle);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_DATA_FROM_HOST), 1);
    CHECK_EQ(sim_count(&W, 2, GCD_EV1_DATA_FROM_HOST), 2);
    CHECK(data_eq(sim_last(&W, 1, GCD_EV1_DATA_FROM_HOST), "to-all"));
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_DATA_FROM_CLIENT), 1);
    CHECK_EQ(sim_last(&W, 0, GCD_EV1_DATA_FROM_CLIENT)->id, 3);
    CHECK(data_eq(sim_last(&W, 0, GCD_EV1_DATA_FROM_CLIENT), "from-3"));
    sim_free(&W);
}

static void test_v1_binary_payloads_in_order(void)
{
    int k, ok = 1, n0;
    v1_setup(1, 2, 2);
    n0 = W.n[1].n_ev;
    for (k = 0; k < 150; k++)
    {
        gc_u8 p[300];
        int   j;
        for (j = 0; j < (int)sizeof(p); j++) p[j] = (gc_u8)((j * 7 + k) & 0xFF); /* includes 0x00 and 0x0A */
        p[0] = (gc_u8)k;
        while (gcd_v1_send(&W.n[0].dev, 1, p, 1 + (size_t)(k % 299), W.now) == GCD_KErrOverflow)
        {
            sim_run(&W, 20);
        }
        if (k % 10 == 0) sim_run(&W, 20);
    }
    sim_run(&W, 2000);
    CHECK_EQ(W.n[1].n_ev - n0, 150);
    for (k = 0; k < 150 && k + n0 < W.n[1].n_ev; k++)
    {
        gcd_event *e = &W.n[1].ev[n0 + k];
        if (e->type != GCD_EV1_DATA_FROM_HOST || e->data[0] != (gc_u8)k || e->len != 1 + (k % 299)) ok = 0;
    }
    CHECK(ok);
    sim_free(&W);
}

static void test_v1_pause_continue(void)
{
    v1_setup(2, 3, 2);
    CHECK_EQ(gcd_v1_pause(&W.n[1].dev, W.now), 0);
    sim_run(&W, 300);
    /* pause callback on everybody but the caller */
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_PAUSE_GAME), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_PAUSE_GAME), 0);
    CHECK_EQ(sim_count(&W, 2, GCD_EV1_PAUSE_GAME), 1);
    CHECK_EQ(gcd_game_state(&W.n[2].dev), GCD_GS_PAUSE);
    CHECK_EQ(gcd_v1_send(&W.n[0].dev, 1, (const gc_u8 *)"x", 1, W.now), GCD_KErrGamePaused);
    /* continue only when everybody asked for it */
    gcd_v1_continue(&W.n[0].dev, W.now);
    gcd_v1_continue(&W.n[1].dev, W.now);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CONTINUE_GAME), 0);
    gcd_v1_continue(&W.n[2].dev, W.now);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CONTINUE_GAME), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_CONTINUE_GAME), 1);
    CHECK_EQ(sim_count(&W, 2, GCD_EV1_CONTINUE_GAME), 1);
    CHECK_EQ(gcd_game_state(&W.n[1].dev), GCD_GS_PLAY);
    sim_free(&W);
}

static void test_v1_client_end_enough_players(void)
{
    v1_setup(3, 4, 3);
    gcd_v1_end(&W.n[2].dev, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_ENDED_GAME), 1);
    CHECK_EQ(sim_last(&W, 0, GCD_EV1_CLIENT_ENDED_GAME)->id, 2);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_DISCONNECTED), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_END_GAME), 0);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_END_GAME), 0);
    sim_free(&W);
}

static void test_v1_client_end_not_enough_players(void)
{
    v1_setup(3, 4, 4);
    gcd_v1_end(&W.n[2].dev, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_END_GAME), 1);
    CHECK_EQ(sim_last(&W, 0, GCD_EV1_END_GAME)->err, GCD_KErrNotEnoughPlayers);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_END_GAME), 1);
    CHECK_EQ(sim_last(&W, 1, GCD_EV1_END_GAME)->err, GCD_KErrNotEnoughPlayers);
    CHECK_EQ(sim_count(&W, 3, GCD_EV1_END_GAME), 1);
    CHECK_EQ(gcd_game_state(&W.n[3].dev), GCD_GS_OVER);
    sim_free(&W);
}

static void test_v1_host_end(void)
{
    v1_setup(2, 3, 2);
    gcd_v1_end(&W.n[0].dev, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_HOST_DISCONNECTED), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_END_GAME), 1);
    CHECK_EQ(sim_last(&W, 2, GCD_EV1_END_GAME)->err, GCD_KErrHostQuit);
    CHECK_EQ(gcd_game_state(&W.n[0].dev), GCD_GS_OVER);
    sim_free(&W);
}

static void test_v1_interrupt_and_reconnect(void)
{
    /* scenario 3.8/3.9 of the Nokia GameComms overview */
    v1_setup(3, 4, 4);
    gcd_v1_pause(&W.n[1].dev, W.now);
    sim_run(&W, 200);
    gcd_v1_disconnect(&W.n[1].dev, W.now);  /* incoming call */
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_DISCONNECTED), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_END_GAME), 0); /* paused: tolerated */
    /* host tries to reconnect before the client is ready */
    sim_clear(&W, 0);
    gcd_v1_reconnect(&W.n[0].dev, 1, W.now);
    sim_run(&W, 200);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_CONNECTED), 1);
    CHECK_EQ(sim_last(&W, 0, GCD_EV1_CLIENT_CONNECTED)->err, GCD_KErrNotReady);
    /* call over: client ready, host reconnects */
    gcd_v1_reconnect(&W.n[1].dev, 1, W.now);
    sim_run(&W, 300);
    sim_clear(&W, 0);
    gcd_v1_reconnect(&W.n[0].dev, 1, W.now);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_CONNECTED), 1);
    CHECK_EQ(sim_last(&W, 0, GCD_EV1_CLIENT_CONNECTED)->err, 0);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_HOST_CONNECTED), 2); /* initial + reconnect */
    /* everybody continues */
    gcd_v1_continue(&W.n[1].dev, W.now);
    gcd_v1_continue(&W.n[2].dev, W.now);
    gcd_v1_continue(&W.n[3].dev, W.now);
    gcd_v1_continue(&W.n[0].dev, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_CONTINUE_GAME), 1);
    CHECK_EQ(gcd_game_state(&W.n[0].dev), GCD_GS_PLAY);
    CHECK_EQ(gcd_v1_send(&W.n[1].dev, GC_SLOT_HOST, (const gc_u8 *)"back", 4, W.now), 0);
    sim_run(&W, 200);
    CHECK(data_eq(sim_last(&W, 0, GCD_EV1_DATA_FROM_CLIENT), "back"));
    sim_free(&W);
}

static void test_v1_client_link_lost_while_playing(void)
{
    v1_setup(3, 4, 3);
    sim_spp_drop(&W, 3);      /* N-Gage 3 out of range of its bridge */
    sim_run(&W, 30000);       /* bridge grace, LEAVE */
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_DISCONNECTED), 1);
    CHECK_EQ(sim_last(&W, 0, GCD_EV1_CLIENT_DISCONNECTED)->err, GCD_KErrDisconnected);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_ENDED_GAME), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_END_GAME), 0);
    sim_free(&W);
}

static void test_v1_incompatible_game(void)
{
    sim_init(&W, 2, GC_API_V1, UID);
    W.n[1].dev.cfg.game_uid = UID + 1; /* a different game */
    sim_run(&W, 500);
    gcd_v1_start_host(&W.n[0].dev, 2, 2, W.now);
    sim_run(&W, 300);
    W.n[1].dev.cfg.auto_join = 1;
    gcd_v1_start_client(&W.n[1].dev, W.now);
    sim_run(&W, 3000);
    /* the other game's session is never offered */
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_HOST_SELECTED), 0);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_CONNECTED), 0);
    CHECK(sim_count(&W, 1, GCD_EV_SESSION_LIST) == 0 || sim_last(&W, 1, GCD_EV_SESSION_LIST)->id == 0);
    /* forcing a join by id is rejected as incompatible */
    W.n[1].dev.selecting = 1;
    gcd_select_session(&W.n[1].dev, W.n[0].dev.session_id, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_last(&W, 1, GCD_EV1_HOST_CONNECTED)->err, GCD_KErrIncompatibleGame);
    sim_free(&W);
}

static void test_v1_host_selection_ui(void)
{
    gcd_event *e;
    sim_init(&W, 3, GC_API_V1, UID);
    sim_run(&W, 500);
    gcd_v1_start_host(&W.n[0].dev, 2, 2, W.now);
    gcd_v1_start_host(&W.n[1].dev, 2, 2, W.now);
    sim_run(&W, 300);
    gcd_v1_start_client(&W.n[2].dev, W.now); /* no auto join: list event */
    sim_run(&W, 500);
    e = sim_last(&W, 2, GCD_EV_SESSION_LIST);
    CHECK(e != NULL && e->id == 2);
    CHECK(gcd_is_selecting(&W.n[2].dev));
    if (e)
    {
        gcd_device *d = (gcd_device *)e->data;
        gcd_select_session(&W.n[2].dev, d[1].session_id, W.now); /* pick the second host */
    }
    sim_run(&W, 500);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_START_GAME), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_START_GAME), 0);
    /* cancel */
    gcd_v1_start_client(&W.n[2].dev, W.now);
    sim_run(&W, 300);
    gcd_select_session(&W.n[2].dev, 0, W.now);
    sim_run(&W, 50);
    CHECK_EQ(sim_last(&W, 2, GCD_EV1_HOST_SELECTED)->err, GCD_KErrCancel);
    CHECK_EQ(sim_last(&W, 2, GCD_EV1_HOST_CONNECTED)->err, GCD_KErrCancel);
    sim_free(&W);
}

/* ================================================================ */
/* GameComms 2.x                                                     */
/* ================================================================ */

static void v2_connect_all(int clients, int mode)
{
    int i;
    sim_init(&W, clients + 1, GC_API_V2, UID);
    sim_run(&W, 500);
    gcd_v2_start(&W.n[0].dev, 1, 0, W.now);
    gcd_v2_set_accept_mode(&W.n[0].dev, mode, W.now);
    sim_run(&W, 300);
    for (i = 1; i <= clients; i++)
    {
        gcd_device host;
        memset(&host, 0, sizeof(host));
        host.session_id = W.n[0].dev.session_id;
        host.slot = GC_SLOT_HOST;
        strcpy(host.name, "ngage0");
        gcd_v2_start(&W.n[i].dev, 0, 0, W.now);
        gcd_v2_declare(&W.n[i].dev, &host, W.now);
        gcd_v2_connect(&W.n[i].dev, GCD_ALL, W.now);
        sim_run(&W, 300);
    }
}

static void test_v2_accept_all_and_connect(void)
{
    int i;
    v2_connect_all(3, GCD_ACCEPT_ALL);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DEVICE_DECLARED), 3);
    for (i = 1; i <= 3; i++)
    {
        CHECK_EQ(sim_count(&W, i, GCD_EV2_DEVICE_DECLARED), 1);
        CHECK_EQ(gcd_connect_state(&W.n[i].dev), GCD_CS_CONNECTING); /* waits for host */
    }
    CHECK_EQ(gcd_v2_connect(&W.n[0].dev, GCD_ALL, W.now), 0);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_CONNECTED), 3);
    for (i = 1; i <= 3; i++)
    {
        CHECK_EQ(sim_count(&W, i, GCD_EV2_CONNECTED), 1);
        CHECK_EQ(sim_last(&W, i, GCD_EV2_CONNECTED)->err, 0);
        CHECK_EQ(gcd_connect_state(&W.n[i].dev), GCD_CS_CONNECTED);
    }
    CHECK_EQ(gcd_connect_state(&W.n[0].dev), GCD_CS_CONNECTED);
    /* closed for newcomers after Connect() */
    CHECK_EQ(gcd_v2_accept_mode(&W.n[0].dev), GCD_ACCEPT_NONE);
    sim_free(&W);
}

static void test_v2_data(void)
{
    v2_connect_all(2, GCD_ACCEPT_ALL);
    gcd_v2_connect(&W.n[0].dev, GCD_ALL, W.now);
    sim_run(&W, 300);
    CHECK_EQ(gcd_v2_send(&W.n[0].dev, 2, (const gc_u8 *)"uni", 3, W.now), 0);
    CHECK_EQ(gcd_v2_send(&W.n[0].dev, GCD_ALL, (const gc_u8 *)"all", 3, W.now), 0);
    CHECK_EQ(gcd_v2_send(&W.n[1].dev, GCD_ALL, (const gc_u8 *)"up", 2, W.now), 0);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_DATA), 1);
    CHECK_EQ(sim_count(&W, 2, GCD_EV2_DATA), 2);
    CHECK(data_eq(sim_last(&W, 0, GCD_EV2_DATA), "up"));
    CHECK_EQ(sim_last(&W, 0, GCD_EV2_DATA)->id, 1);
    CHECK_EQ(sim_last(&W, 2, GCD_EV2_DATA)->id, 0);
    sim_free(&W);
}

static void test_v2_not_connected_errors(void)
{
    v2_connect_all(1, GCD_ACCEPT_ALL);
    /* before the host connected: KErrNotFound (as the original) */
    CHECK_EQ(gcd_v2_send(&W.n[1].dev, GCD_ALL, (const gc_u8 *)"x", 1, W.now), GCD_KErrNotFound);
    CHECK_EQ(gcd_v2_send(&W.n[0].dev, 1, (const gc_u8 *)"x", 1, W.now), GCD_KErrNotFound);
    sim_free(&W);
}

static void test_v2_confirm_mode(void)
{
    gcd_event *e;
    v2_connect_all(2, GCD_ACCEPT_CONFIRM);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_CONFIRM_DEVICE), 2);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DEVICE_DECLARED), 0);
    e = sim_last(&W, 0, GCD_EV2_CONFIRM_DEVICE);
    CHECK(e && strcmp(e->dev.name, "ngage2") == 0);
    if (e)
    {
        gcd_device d = e->dev;
        gcd_v2_declare(&W.n[0].dev, &d, W.now); /* accept only ngage2 */
    }
    sim_run(&W, 200);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DEVICE_DECLARED), 1);
    gcd_v2_connect(&W.n[0].dev, GCD_ALL, W.now);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 2, GCD_EV2_CONNECTED), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_CONNECTED), 0);
    sim_free(&W);
}

static void test_v2_closed_host_rejects(void)
{
    gcd_device host;
    sim_init(&W, 2, GC_API_V2, UID);
    sim_run(&W, 500);
    gcd_v2_start(&W.n[0].dev, 1, 0, W.now); /* default: not accepting */
    sim_run(&W, 300);
    memset(&host, 0, sizeof(host));
    host.session_id = W.n[0].dev.session_id;
    gcd_v2_start(&W.n[1].dev, 0, 0, W.now);
    gcd_v2_declare(&W.n[1].dev, &host, W.now);
    gcd_v2_connect(&W.n[1].dev, GCD_ALL, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_CONNECTED), 1);
    CHECK(sim_last(&W, 1, GCD_EV2_CONNECTED)->err != 0);
    CHECK_EQ(gcd_connect_state(&W.n[1].dev), GCD_CS_NOT_CONNECTED);
    sim_free(&W);
}

static void test_v2_disconnect(void)
{
    v2_connect_all(2, GCD_ACCEPT_ALL);
    gcd_v2_connect(&W.n[0].dev, GCD_ALL, W.now);
    sim_run(&W, 300);
    gcd_v2_disconnect(&W.n[0].dev, 1, 0, W.now); /* temporary */
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DISCONNECTED), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_DISCONNECTED), 1);
    /* not permanent: Connect(1) brings it back */
    gcd_v2_connect(&W.n[0].dev, 1, W.now);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_CONNECTED), 2);
    /* client side Disconnect() */
    gcd_v2_disconnect(&W.n[2].dev, GCD_ALL, 0, W.now);
    sim_run(&W, 300);
    CHECK_EQ(sim_count(&W, 2, GCD_EV2_DISCONNECTED), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DISCONNECTED), 2);
    sim_free(&W);
}

static void test_v2_client_ui_selection(void)
{
    gcd_event *e;
    sim_init(&W, 2, GC_API_V2, UID);
    sim_run(&W, 500);
    gcd_v2_start(&W.n[0].dev, 1, 0, W.now);
    gcd_v2_set_accept_mode(&W.n[0].dev, GCD_ACCEPT_ALL, W.now);
    sim_run(&W, 300);
    gcd_v2_start(&W.n[1].dev, 0, 1, W.now); /* built-in list */
    sim_run(&W, 500);
    e = sim_last(&W, 1, GCD_EV_SESSION_LIST);
    CHECK(e && e->id == 1);
    if (e)
    {
        gcd_select_session(&W.n[1].dev, ((gcd_device *)e->data)[0].session_id, W.now);
    }
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_DEVICE_DECLARED), 1);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DEVICE_DECLARED), 1);
    sim_free(&W);
}

/* the game's own host search (Ashen and others): the client asks once for
   the open sessions (CGameBTDiscoverer in the DLL), declares one, connects */
static void test_v2_game_host_search(void)
{
    gcd_event *e;
    sim_init(&W, 3, GC_API_V2, UID);
    sim_run(&W, 500);
    /* node 2 hosts but does not accept players: must not be listed */
    gcd_v2_start(&W.n[2].dev, 1, 0, W.now);
    gcd_v2_start(&W.n[0].dev, 1, 0, W.now);
    gcd_v2_set_accept_mode(&W.n[0].dev, GCD_ACCEPT_ALL, W.now);
    sim_run(&W, 300);
    gcd_v2_start(&W.n[1].dev, 0, 0, W.now); /* no built-in list */
    gcd_discover_once(&W.n[1].dev, W.now);
    sim_run(&W, 500);
    CHECK_EQ(sim_count(&W, 1, GCD_EV_DISCOVERED), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV_SESSION_LIST), 0);
    e = sim_last(&W, 1, GCD_EV_DISCOVERED);
    CHECK(e && e->id == 1);
    if (e && e->id == 1)
    {
        gcd_device host = ((gcd_device *)e->data)[0];
        CHECK_EQ(host.session_id, W.n[0].dev.session_id);
        CHECK(strcmp(host.name, "ngage0") == 0);
        gcd_v2_declare(&W.n[1].dev, &host, W.now);
        gcd_v2_connect(&W.n[1].dev, GCD_ALL, W.now);
    }
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DEVICE_DECLARED), 1);
    CHECK_EQ(gcd_v2_connect(&W.n[0].dev, GCD_ALL, W.now), 0);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 1, GCD_EV2_CONNECTED), 1);
    CHECK_EQ(sim_last(&W, 1, GCD_EV2_CONNECTED)->err, 0);
    /* a second search finds nothing: the host closed its door on Connect() */
    gcd_discover_once(&W.n[1].dev, W.now);
    sim_run(&W, 500);
    CHECK_EQ(sim_count(&W, 1, GCD_EV_DISCOVERED), 2);
    e = sim_last(&W, 1, GCD_EV_DISCOVERED);
    CHECK(e && e->id == 0);
    sim_free(&W);
}

/* asking before the bridge connection is up: answered once it is */
static void test_v2_game_host_search_early(void)
{
    sim_init(&W, 2, GC_API_V2, UID);
    gcd_discover_once(&W.n[1].dev, W.now); /* nothing registered yet */
    sim_run(&W, 200);
    gcd_v2_start(&W.n[0].dev, 1, 0, W.now);
    gcd_v2_set_accept_mode(&W.n[0].dev, GCD_ACCEPT_ALL, W.now);
    sim_run(&W, 3000);
    CHECK_EQ(sim_count(&W, 1, GCD_EV_DISCOVERED), 1);
    sim_free(&W);
}

static void test_v2_real_bt_device_declared(void)
{
    /* a game that discovered a real Bluetooth device itself declares it
     * with an unknown address: the client joins the game's session */
    gcd_device bt;
    sim_init(&W, 2, GC_API_V2, UID);
    sim_run(&W, 500);
    gcd_v2_start(&W.n[0].dev, 1, 0, W.now);
    gcd_v2_set_accept_mode(&W.n[0].dev, GCD_ACCEPT_ALL, W.now);
    sim_run(&W, 300);
    memset(&bt, 0, sizeof(bt));
    bt.slot = GC_SLOT_NONE;
    strcpy(bt.name, "Nokia N-Gage");
    gcd_v2_start(&W.n[1].dev, 0, 0, W.now);
    gcd_v2_declare(&W.n[1].dev, &bt, W.now);
    gcd_v2_connect(&W.n[1].dev, GCD_ALL, W.now);
    sim_run(&W, 400);
    CHECK_EQ(sim_count(&W, 0, GCD_EV2_DEVICE_DECLARED), 1);
    sim_free(&W);
}

/* ================================================================ */
/* transport robustness                                              */
/* ================================================================ */

static void test_tcp_drop_resumes_without_loss(void)
{
    int k, n0;
    v1_setup(1, 2, 2);
    n0 = W.n[1].n_ev;
    for (k = 0; k < 10; k++)
    {
        char m[16];
        sprintf(m, "m%d", k);
        gcd_v1_send(&W.n[0].dev, 1, (const gc_u8 *)m, strlen(m), W.now);
        if (k == 4)
        {
            sim_tcp_drop(&W, 1);  /* client's bridge loses TCP mid-stream */
            sim_tcp_drop(&W, 0);  /* and the host's too */
        }
        sim_run(&W, 30);
    }
    sim_run(&W, 8000); /* reconnect + resume */
    CHECK_EQ(W.n[1].n_ev - n0, 10);
    for (k = 0; k < 10 && n0 + k < W.n[1].n_ev; k++)
    {
        char m[16];
        sprintf(m, "m%d", k);
        CHECK(data_eq(&W.n[1].ev[n0 + k], m));
    }
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_DISCONNECTED), 0);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_HOST_DISCONNECTED), 0);
    CHECK(W.n[1].br.srv.st_resumes >= 1);
    sim_free(&W);
}

static void test_bridge_reboot_reports_loss(void)
{
    v1_setup(1, 2, 2);
    sim_bridge_reboot(&W, 1);
    sim_run(&W, 30000);
    /* the client learns that its session is gone ... */
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_HOST_DISCONNECTED), 1);
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_END_GAME), 1);
    /* ... and the host that the client left */
    CHECK_EQ(sim_count(&W, 0, GCD_EV1_CLIENT_DISCONNECTED), 1);
    sim_free(&W);
}

static void test_noisy_bluetooth_link(void)
{
    int k, n0;
    v1_setup(1, 2, 2);
    W.n[1].spp_to_dev.corrupt_permille = 2; /* ~0.2 % corrupted bytes */
    W.n[0].spp_to_bridge.corrupt_permille = 2;
    n0 = W.n[1].n_ev;
    for (k = 0; k < 60; k++)
    {
        gc_u8 p[64];
        memset(p, (int)k, sizeof(p));
        while (gcd_v1_send(&W.n[0].dev, 1, p, sizeof(p), W.now) == GCD_KErrOverflow) sim_run(&W, 20);
        sim_run(&W, 20);
    }
    W.n[1].spp_to_dev.corrupt_permille = 0;
    W.n[0].spp_to_bridge.corrupt_permille = 0;
    sim_run(&W, 10000);
    CHECK_EQ(W.n[1].n_ev - n0, 60);
    for (k = 0; k < 60 && n0 + k < W.n[1].n_ev; k++)
    {
        CHECK_EQ(W.n[1].ev[n0 + k].data[10], k);
    }
    CHECK(W.n[1].dev.link.rx.err_crc + W.n[1].br.dev.rx.err_crc + W.n[1].dev.link.rx.err_cobs > 0);
    sim_free(&W);
}

static void test_concurrent_sessions_isolated(void)
{
    /* two independent games of the same title run side by side */
    int i;
    sim_init(&W, 4, GC_API_V1, UID);
    sim_run(&W, 500);
    gcd_v1_start_host(&W.n[0].dev, 2, 2, W.now);
    sim_run(&W, 300);
    W.n[1].dev.cfg.auto_join = 1;
    gcd_v1_start_client(&W.n[1].dev, W.now);
    sim_run(&W, 600);
    gcd_v1_start_host(&W.n[2].dev, 2, 2, W.now);
    sim_run(&W, 300);
    W.n[3].dev.cfg.auto_join = 1;
    gcd_v1_start_client(&W.n[3].dev, W.now);
    sim_run(&W, 600);
    for (i = 0; i < 4; i++)
    {
        CHECK_EQ(gcd_game_state(&W.n[i].dev), GCD_GS_PLAY);
    }
    gcd_v1_send(&W.n[0].dev, GC_SLOT_ALL, (const gc_u8 *)"game-A", 6, W.now);
    gcd_v1_send(&W.n[2].dev, GC_SLOT_ALL, (const gc_u8 *)"game-B", 6, W.now);
    sim_run(&W, 300);
    CHECK(data_eq(sim_last(&W, 1, GCD_EV1_DATA_FROM_HOST), "game-A"));
    CHECK(data_eq(sim_last(&W, 3, GCD_EV1_DATA_FROM_HOST), "game-B"));
    CHECK_EQ(sim_count(&W, 1, GCD_EV1_DATA_FROM_HOST), 1);
    CHECK_EQ(sim_count(&W, 3, GCD_EV1_DATA_FROM_HOST), 1);
    sim_free(&W);
}

int main(void)
{
    RUN(test_v1_startup_four_players);
    RUN(test_v1_game_does_not_start_early);
    RUN(test_v1_data_routing);
    RUN(test_v1_binary_payloads_in_order);
    RUN(test_v1_pause_continue);
    RUN(test_v1_client_end_enough_players);
    RUN(test_v1_client_end_not_enough_players);
    RUN(test_v1_host_end);
    RUN(test_v1_interrupt_and_reconnect);
    RUN(test_v1_client_link_lost_while_playing);
    RUN(test_v1_incompatible_game);
    RUN(test_v1_host_selection_ui);
    RUN(test_v2_accept_all_and_connect);
    RUN(test_v2_data);
    RUN(test_v2_not_connected_errors);
    RUN(test_v2_confirm_mode);
    RUN(test_v2_game_host_search);
    RUN(test_v2_game_host_search_early);
    RUN(test_v2_closed_host_rejects);
    RUN(test_v2_disconnect);
    RUN(test_v2_client_ui_selection);
    RUN(test_v2_real_bt_device_declared);
    RUN(test_tcp_drop_resumes_without_loss);
    RUN(test_bridge_reboot_reports_loss);
    RUN(test_noisy_bluetooth_link);
    RUN(test_concurrent_sessions_isolated);
    TEST_MAIN_END();
}
