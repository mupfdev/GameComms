/** @file bridge_core.h
 *
 *  ESP32 bridge core: translates between the N-Gage side (Bluetooth SPP,
 *  GCL link, bridge is acceptor) and the server side (TCP, GCL link,
 *  bridge is initiator).
 *
 *  Hardware independent: the Arduino front end (main.cpp) and the host
 *  test harness drive it through the functions below and receive output
 *  through bridge_io.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef BRIDGE_CORE_H
#define BRIDGE_CORE_H

#include "gc_link.h"
#include "gc_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BRIDGE_FIFO_LEN
#define BRIDGE_FIFO_LEN 8
#endif

#define BRIDGE_VERSION "1.0.0"

typedef struct
{
    gc_u16 len;
    gc_u8  data[GC_LINK_MAX_MSG];
} bridge_slot;

typedef struct
{
    bridge_slot s[BRIDGE_FIFO_LEN];
    int         head;
    int         count;
} bridge_fifo;

typedef struct
{
    void (*dev_write)(void *ctx, const gc_u8 *data, size_t len);
    void (*srv_write)(void *ctx, const gc_u8 *data, size_t len);
    /** start an asynchronous TCP connect; report with
     *  bridge_srv_connected() / bridge_srv_disconnected() */
    void (*srv_connect)(void *ctx);
    void (*srv_disconnect)(void *ctx);
    void (*log)(void *ctx, const char *line);
} bridge_io;

typedef struct
{
    gc_u32 server_link_id;     /* random per boot */
    char   access_key[GC_MAX_NAME + 1];
    gc_u32 device_grace_ms;    /* keep the device's server state after BT loss */
    gc_u32 reconnect_min_ms;
    gc_u32 reconnect_max_ms;
} bridge_cfg;

typedef struct
{
    bridge_cfg  cfg;
    bridge_io   io;
    void       *ctx;

    gc_link     dev;           /* N-Gage side */
    gc_link     srv;           /* server side */
    bridge_fifo to_srv;        /* waiting for the server link */
    bridge_fifo to_dev;        /* waiting for the device link */

    int         dev_connected; /* SPP transport present */
    gc_u8       dev_addr[GC_DEVICE_ID_LEN];
    gc_u32      t_dev_lost;
    int         dev_lost_pending;

    int         wifi_up;
    gc_i8       rssi;
    int         srv_state;     /* GC_BS_* */
    gc_u32      t_reconnect;
    gc_u32      backoff;
    int         srv_ever_up;

    /* last REGISTER from the device, replayed after a fresh server link */
    gc_u8       reg_msg[GC_LINK_MAX_MSG];
    gc_u16      reg_len;
    int         registered;    /* server confirmed the registration */

    gc_u32      now;
    gc_u32      st_dev_msgs;
    gc_u32      st_srv_msgs;
} bridge;

void bridge_default_cfg(bridge_cfg *cfg);
void bridge_init(bridge *b, const bridge_cfg *cfg, const bridge_io *io, void *ctx);

void bridge_dev_connected(bridge *b, const gc_u8 *bt_addr, gc_u32 now);
void bridge_dev_input(bridge *b, const gc_u8 *data, size_t len, gc_u32 now);
void bridge_dev_disconnected(bridge *b, gc_u32 now);

void bridge_set_wifi(bridge *b, int up, gc_i8 rssi, gc_u32 now);
void bridge_srv_connected(bridge *b, gc_u32 now);
void bridge_srv_input(bridge *b, const gc_u8 *data, size_t len, gc_u32 now);
void bridge_srv_disconnected(bridge *b, gc_u32 now);

void   bridge_poll(bridge *b, gc_u32 now);
gc_u32 bridge_next_timeout(bridge *b, gc_u32 now);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_CORE_H */
