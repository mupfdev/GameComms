/** @file gc_msg.h
 *
 *  GCM - GameComms Message layer.
 *
 *  Messages are carried in GCL DATA frames (see gc_link.h).  They are
 *  end-to-end between the N-Gage (GameComms DLL) and the endpoint
 *  server; the ESP32 bridge forwards them verbatim and only consumes
 *  the few BRIDGE_* messages addressed to itself.
 *
 *  All multi-byte integers are big-endian.  Strings are UTF-8 with a
 *  u8 length prefix and no terminator.  Decoders ignore trailing bytes
 *  so that later protocol versions can append fields.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GC_MSG_H
#define GC_MSG_H

#include "gc_codec.h"
#include "gc_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- message types ---- */

/* registration */
#define GC_MT_REGISTER   0x10 /* dev -> srv */
#define GC_MT_REGISTERED 0x11 /* srv -> dev */

/* hosting */
#define GC_MT_HOST_OPEN     0x20 /* dev -> srv */
#define GC_MT_HOST_OPENED   0x21 /* srv -> dev */
#define GC_MT_HOST_CLOSE    0x22 /* dev -> srv */
#define GC_MT_SESSION_STATE 0x23 /* host -> srv */
#define GC_MT_PEER_REQUEST  0x24 /* srv -> host (confirm mode) */
#define GC_MT_PEER_ACCEPT   0x25 /* host -> srv */
#define GC_MT_PEER_JOINED   0x26 /* srv -> host */
#define GC_MT_PEER_LEFT     0x27 /* srv -> host */
#define GC_MT_KICK          0x28 /* host -> srv */

/* discovery / joining */
#define GC_MT_DISCOVER      0x30 /* dev -> srv */
#define GC_MT_SESSION_LIST  0x31 /* srv -> dev */
#define GC_MT_JOIN          0x32 /* dev -> srv */
#define GC_MT_JOIN_RESULT   0x33 /* srv -> dev */
#define GC_MT_LEAVE         0x34 /* dev -> srv */
#define GC_MT_SESSION_ENDED 0x35 /* srv -> client */

/* data */
#define GC_MT_DATA 0x40 /* both directions */

/* bridge local */
#define GC_MT_BRIDGE_STATUS 0x50 /* bridge -> dev */
#define GC_MT_BRIDGE_QUERY  0x51 /* dev -> bridge */

/* errors */
#define GC_MT_ERROR 0x60 /* any direction */

/* ---- constants ---- */

#define GC_MAX_PLAYERS 4
#define GC_SLOT_HOST   0
#define GC_SLOT_ALL    0xFF /* DATA destination: every other member */
#define GC_SLOT_NONE   0xFE

#define GC_MAX_NAME      32   /* bytes, UTF-8 */
#define GC_DEVICE_ID_LEN 6    /* Bluetooth address of the N-Gage */
#define GC_MAX_DATA      1024 /* largest game payload per message */
#define GC_MAX_LIST      12   /* sessions per SESSION_LIST message */

/* API families: devices only ever meet devices of the same family */
#define GC_API_V1 1 /* GameComms 1.x (variant v1, 33 ordinals) */
#define GC_API_V2 2 /* GameComms 2.x (variants v2..v11)        */

/* DATA channels */
#define GC_CH_GAME    0 /* opaque game payload             */
#define GC_CH_CONTROL 1 /* GameComms session control (GCS) */

/* HOST_OPEN flags */
#define GC_HF_CONFIRM_CLIENTS 0x01 /* host must accept each joiner      */
#define GC_HF_HIDDEN          0x02 /* not listed by DISCOVER            */

/* session states (SESSION_STATE / SESSION_LIST) */
#define GC_SS_LOBBY   0 /* accepting new players            */
#define GC_SS_PLAYING 1 /* closed for new players, rejoin ok */
#define GC_SS_PAUSED  2 /* like PLAYING                      */

/* result codes (JOIN_RESULT, HOST_OPENED, REGISTERED, ERROR) */
#define GC_R_OK             0
#define GC_R_PENDING        1 /* waiting for host confirmation    */
#define GC_R_NOT_FOUND      (-1)
#define GC_R_FULL           (-2)
#define GC_R_INCOMPATIBLE   (-3) /* different game UID / API family */
#define GC_R_CLOSED         (-4) /* session not accepting players   */
#define GC_R_REJECTED       (-5) /* host refused                    */
#define GC_R_NOT_REGISTERED (-6)
#define GC_R_BUSY           (-7) /* already hosting / member        */
#define GC_R_UNAVAILABLE    (-8) /* server not reachable (bridge)   */
#define GC_R_BAD_REQUEST    (-9)
#define GC_R_NOT_MEMBER     (-10)
#define GC_R_LIMIT          (-11) /* server resource limit          */
#define GC_R_VERSION        (-12)

/* PEER_LEFT / SESSION_ENDED / LEAVE / KICK reasons */
#define GC_LEFT_LEAVE       0 /* graceful LEAVE                  */
#define GC_LEFT_LOST        1 /* link lost, slot reserved        */
#define GC_LEFT_KICKED      2
#define GC_LEFT_EXPIRED     3 /* reservation expired             */
#define GC_LEFT_HOST_CLOSED 4
#define GC_LEFT_HOST_LOST   5

/* bridge status */
#define GC_BS_DOWN       0
#define GC_BS_CONNECTING 1
#define GC_BS_UP         2

/* ---- message structure ---- */

typedef struct
{
    gc_u32 session_id;
    gc_u8 players;
    gc_u8 max_players;
    gc_u8 state;
    char host_name[GC_MAX_NAME + 1];
} gc_session_info;

typedef struct
{
    gc_u8 type;
    union
    {
        struct
        {
            gc_u8 api;
            gc_u8 variant;
            gc_u8 device_id[GC_DEVICE_ID_LEN];
            gc_u8 flags;
            char name[GC_MAX_NAME + 1];
            char key[GC_MAX_NAME + 1]; /* optional access key, appended by the bridge */
        } reg;
        struct
        {
            gc_i8 result;
            gc_u32 peer_id;
            gc_u8 proto;
            char server_name[GC_MAX_NAME + 1];
        } registered;
        struct
        {
            gc_u32 game_uid;
            gc_u8 api;
            gc_u8 max_players;
            gc_u8 flags;
            char name[GC_MAX_NAME + 1];
        } host_open;
        struct
        {
            gc_i8 result;
            gc_u32 session_id;
        } host_opened;
        struct
        {
            gc_u8 reason;
        } reason; /* HOST_CLOSE, LEAVE, SESSION_ENDED */
        struct
        {
            gc_u8 state;
        } session_state;
        struct
        {
            gc_u8 slot;
            gc_u8 rejoin;
            gc_u8 device_id[GC_DEVICE_ID_LEN];
            char name[GC_MAX_NAME + 1];
        } peer; /* PEER_REQUEST, PEER_JOINED */
        struct
        {
            gc_u8 slot;
            gc_u8 accept;
        } peer_accept;
        struct
        {
            gc_u8 slot;
            gc_u8 reason;
        } peer_left; /* PEER_LEFT, KICK */
        struct
        {
            gc_u32 game_uid;
            gc_u8 api;
        } discover;
        struct
        {
            gc_u8 count;
            gc_u8 more;
            gc_session_info s[GC_MAX_LIST];
        } list;
        struct
        {
            gc_u32 session_id;
            gc_u32 game_uid;
            gc_u8 api;
            gc_u8 rejoin;
        } join;
        struct
        {
            gc_i8 result;
            gc_u32 session_id;
            gc_u8 slot;
            gc_u8 max_players;
            char host_name[GC_MAX_NAME + 1];
        } join_result;
        struct
        {
            gc_u8 addr; /* dst when sending, src when receiving */
            gc_u8 channel;
            gc_u16 len;
            const gc_u8 *payload; /* points into the decoded buffer */
        } data;
        struct
        {
            gc_u8 wifi;
            gc_u8 server;
            gc_i8 rssi;
            char version[GC_MAX_NAME + 1];
        } bridge;
        struct
        {
            gc_i8 code;
            gc_u8 ref_type;
            char text[GC_MAX_NAME + 1];
        } error;
    } u;
} gc_msg;

/** Encode msg into buf. Returns length, or 0 on error (unknown type or
 *  buffer too small). */
size_t gc_msg_encode(const gc_msg *msg, gc_u8 *buf, size_t cap);

/** Decode buf into msg.  For DATA, msg->u.data.payload points into buf.
 *  Returns 1 on success, 0 if the message is malformed or unknown. */
int gc_msg_decode(const gc_u8 *buf, size_t len, gc_msg *msg);

/** Human readable message type (diagnostics). */
const char *gc_msg_name(gc_u8 type);

/** Convenience: build a DATA message header in place.  Writes the 3 byte
 *  header followed by payload; returns total length or 0. */
size_t gc_msg_encode_data(gc_u8 addr, gc_u8 channel, const gc_u8 *payload, size_t len, gc_u8 *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* GC_MSG_H */
