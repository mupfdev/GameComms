/** @file gc_dev.h
 *
 *  GameComms device engine.
 *
 *  Portable (C89, no writable static data, no heap use) implementation
 *  of the GameComms semantics that the replacement GAMECOMMS.DLL exposes
 *  to games.  The Symbian adapter (dll/symbian) owns the Bluetooth
 *  connection to the ESP32 bridge, the timers and the game callbacks;
 *  everything else - registration, matchmaking, the host/client state
 *  machines of both API generations and the session control
 *  sub-protocol - lives here so that it can be unit- and integration
 *  tested on a PC.
 *
 *  Two API families are implemented:
 *
 *   GC_API_V1  GameComms 1.x (variant v1 / SDK 1.0): host manages
 *              start/min players, pause/continue/end, 12 callbacks.
 *   GC_API_V2  GameComms 2.x (variants v2..v11): connection oriented,
 *              DeclareRemoteDevice/Connect/Disconnect/SendData, accept
 *              modes, 5 callbacks.
 *
 *  All API calls are synchronous and never invoke callbacks directly;
 *  callbacks are queued as events and delivered by the adapter from an
 *  active object, just like the original library did.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GC_DEV_H
#define GC_DEV_H

#include "gc_link.h"
#include "gc_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Symbian error codes used by GameComms ---- */
#define GCD_KErrNone              0
#define GCD_KErrNotFound          (-1)
#define GCD_KErrCancel            (-3)
#define GCD_KErrNoMemory          (-4)
#define GCD_KErrNotSupported      (-5)
#define GCD_KErrArgument          (-6)
#define GCD_KErrBadHandle         (-8)
#define GCD_KErrOverflow          (-9)
#define GCD_KErrAlreadyExists     (-11)
#define GCD_KErrInUse             (-14)
#define GCD_KErrNotReady          (-18)
#define GCD_KErrCouldNotConnect   (-34)
#define GCD_KErrDisconnected      (-36)
#define GCD_KErrIncompatibleGame  (-1001)
#define GCD_KErrNotCommsHost      (-1002)
#define GCD_KErrClientNotReady    (-1003)
#define GCD_KErrHostQuit          (-1004)
#define GCD_KErrNotEnoughPlayers  (-1005)
#define GCD_KErrGamePaused        (-1006)
#define GCD_KErrGameOver          (-1007)

/* ---- enums matching the public CGameBTComms enums ---- */
#define GCD_ROLE_IDLE    0 /* EIdle   */
#define GCD_ROLE_CLIENT  1 /* EClient */
#define GCD_ROLE_HOST    2 /* EHost   */

#define GCD_CS_NOT_CONNECTED 0
#define GCD_CS_CONNECTING    1
#define GCD_CS_CONNECTED     2

#define GCD_GS_OVER  0 /* EGameOver */
#define GCD_GS_PLAY  1 /* EPlay     */
#define GCD_GS_PAUSE 2 /* EPause    */

/* v2 host accept modes */
#define GCD_ACCEPT_ALL     0
#define GCD_ACCEPT_CONFIRM 1
#define GCD_ACCEPT_NONE    2

/* id meaning "all" for v2 calls */
#define GCD_ALL (-1)

/* ---- events (callbacks to the game, delivered asynchronously) ---- */
/* GameComms 1.x - MGameBTCommsNotify, vtable order */
#define GCD_EV1_CLIENT_CONNECTED      1  /* id, name, err */
#define GCD_EV1_HOST_SELECTED         2  /* err */
#define GCD_EV1_HOST_CONNECTED        3  /* err */
#define GCD_EV1_START_GAME            4  /* err */
#define GCD_EV1_CONTINUE_GAME         5
#define GCD_EV1_PAUSE_GAME            6
#define GCD_EV1_END_GAME              7  /* err = reason */
#define GCD_EV1_CLIENT_ENDED_GAME     8  /* id */
#define GCD_EV1_CLIENT_DISCONNECTED   9  /* id, err */
#define GCD_EV1_HOST_DISCONNECTED     10 /* err */
#define GCD_EV1_DATA_FROM_CLIENT      11 /* id, data */
#define GCD_EV1_DATA_FROM_HOST        12 /* data */
/* GameComms 2.x - MGameBTCommsNotify (2.x), vtable order */
#define GCD_EV2_CONFIRM_DEVICE        20 /* device (host, confirm mode, no UI) */
#define GCD_EV2_DEVICE_DECLARED       21 /* id, device, err */
#define GCD_EV2_CONNECTED             22 /* id, err */
#define GCD_EV2_DISCONNECTED          23 /* id, err */
#define GCD_EV2_DATA                  24 /* id, data */
/* adapter internal */
#define GCD_EV_SESSION_LIST           40 /* sessions found (client UI)     */
#define GCD_EV_UI_ADD_DEVICE          41 /* host confirm list: add device   */
#define GCD_EV_UI_CLOSE               42 /* close any engine UI             */
#define GCD_EV_TRANSPORT              43 /* err: bridge link problem        */

/** A remote party as seen through TDeviceDetails (GameComms 2.x) or the
 *  host selection list.  Hosts are identified by their session id,
 *  clients (as seen by the host) by their slot. */
typedef struct
{
    gc_u32 session_id;
    gc_u8  slot;
    char   name[GC_MAX_NAME + 1];
} gcd_device;

typedef struct
{
    int        type;
    int        id;
    int        err;
    gcd_device dev;
    gc_u16     len;
    gc_u16     pad;   /* keeps data[] 4-byte aligned (ARMv5 has no unaligned loads) */
    gc_u8      data[GC_MAX_DATA];
} gcd_event;

GC_STATIC_ASSERT(gcd_event_data_aligned, (sizeof(gcd_device) % 4) == 0);

#ifndef GCD_EVENT_QUEUE
#define GCD_EVENT_QUEUE 24
#endif

#define GCD_MAX_SESSIONS 8 /* entries kept for the selection list */

typedef struct
{
    gc_u8  api;            /* GC_API_V1 / GC_API_V2 */
    gc_u8  variant;        /* 1..11, informational  */
    gc_u32 game_uid;
    char   name[GC_MAX_NAME + 1];
    int    auto_join;      /* client: join first session without UI */
    gc_u32 link_id;        /* random, identifies this DLL instance   */
    gc_u32 discover_ms;    /* discovery refresh interval             */
} gcd_config;

typedef struct
{
    void (*write)(void *ctx, const gc_u8 *data, size_t len);
    void (*log)(void *ctx, const char *line);
    /** something was queued: schedule gcd_pop_event() processing */
    void (*wake)(void *ctx);
} gcd_ops;

typedef struct
{
    int    present;     /* member of the network session            */
    char   name[GC_MAX_NAME + 1];
    /* GameComms 1.x */
    int    connected;   /* counts as connected player                */
    int    temp_disc;   /* temporarily disconnected (pause)          */
    int    ready;       /* client called ReconnectL                  */
    int    cont_req;    /* client asked to continue                  */
    /* GameComms 2.x */
    int    pending;     /* waiting for DeclareRemoteDevice (confirm)  */
    int    declared;
    int    connected2;
    int    connecting2;
} gcd_peer;

typedef struct
{
    gcd_config cfg;
    gcd_ops    ops;
    void      *ctx;
    gc_link    link;
    gc_u32     now;

    /* transport / registration */
    int        transport_up;
    int        registered;
    int        server_up;      /* from BRIDGE_STATUS */
    int        reg_sent;

    /* role */
    int        role;
    int        want_role;      /* requested, waiting for registration */
    int        session_open;   /* host: HOST_OPENED ok / client: joined */
    gc_u32     session_id;
    gc_u8      slot;
    gc_u32     t_retry;
    int        conn_state;
    int        game_state;

    /* host */
    gc_u16     start_players;
    gc_u16     min_players;
    gcd_peer   peer[GC_MAX_PLAYERS];
    int        host_cont;      /* host called Continue           */
    int        accept_mode;
    int        ui;             /* engine provided UI requested   */
    int        connect_all_pending;

    /* client */
    int        selecting;      /* host selection in progress     */
    int        joining;
    gc_u32     t_discover;
    gcd_device sessions[GCD_MAX_SESSIONS];
    int        n_sessions;
    gcd_device target;         /* declared / selected host       */
    int        target_set;
    int        temp_disc;      /* v1 client temporarily disconnected */
    int        ready_sent;
    char       host_name[GC_MAX_NAME + 1];

    /* events */
    gcd_event  ev[GCD_EVENT_QUEUE];
    int        ev_head;
    int        ev_count;
    gc_u32     ev_dropped;
} gcd_dev;

void gcd_default_config(gcd_config *cfg);
void gcd_init(gcd_dev *d, const gcd_config *cfg, const gcd_ops *ops, void *ctx);

/* transport to the bridge */
void   gcd_transport_up(gcd_dev *d, gc_u32 now);
void   gcd_transport_input(gcd_dev *d, const gc_u8 *data, size_t len, gc_u32 now);
void   gcd_transport_down(gcd_dev *d, gc_u32 now);
void   gcd_poll(gcd_dev *d, gc_u32 now);
gc_u32 gcd_next_timeout(gcd_dev *d, gc_u32 now);

/* events */
int gcd_pop_event(gcd_dev *d, gcd_event *out);

/* common queries */
int gcd_role(const gcd_dev *d);
int gcd_connect_state(const gcd_dev *d);
int gcd_game_state(const gcd_dev *d);
int gcd_is_selecting(const gcd_dev *d);

/** Client host selection result (UI): session id, or 0 for cancel. */
void gcd_select_session(gcd_dev *d, gc_u32 session_id, gc_u32 now);

/* ---- GameComms 1.x ---- */
void gcd_v1_start_host(gcd_dev *d, gc_u16 start_players, gc_u16 min_players, gc_u32 now);
void gcd_v1_start_client(gcd_dev *d, gc_u32 now);
void gcd_v1_disconnect(gcd_dev *d, gc_u32 now);
int  gcd_v1_disconnect_client(gcd_dev *d, int id, gc_u32 now);
/** dst: client id 1..3, GC_SLOT_ALL (host) or GC_SLOT_HOST (client) */
int  gcd_v1_send(gcd_dev *d, int dst, const gc_u8 *data, size_t len, gc_u32 now);
int  gcd_v1_continue(gcd_dev *d, gc_u32 now);
int  gcd_v1_reconnect(gcd_dev *d, int must_reconnect_all, gc_u32 now);
int  gcd_v1_pause(gcd_dev *d, gc_u32 now);
int  gcd_v1_end(gcd_dev *d, gc_u32 now);

/* ---- GameComms 2.x ---- */
void gcd_v2_start(gcd_dev *d, int host, int ui, gc_u32 now);
int  gcd_v2_declare(gcd_dev *d, const gcd_device *dev, gc_u32 now);
int  gcd_v2_connect(gcd_dev *d, int id, gc_u32 now);
int  gcd_v2_disconnect(gcd_dev *d, int id, int permanently, gc_u32 now);
int  gcd_v2_set_accept_mode(gcd_dev *d, int mode, gc_u32 now);
int  gcd_v2_accept_mode(const gcd_dev *d);
int  gcd_v2_send(gcd_dev *d, int id, const gc_u8 *data, size_t len, gc_u32 now);
void gcd_end_session(gcd_dev *d, gc_u32 now);

#ifdef __cplusplus
}
#endif

#endif /* GC_DEV_H */
