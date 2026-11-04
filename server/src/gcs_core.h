/** @file gcs_core.h
 *
 *  GameComms endpoint server - session manager core.
 *
 *  The core is transport independent: the network layer hands it
 *  connections and received bytes and the core answers through the
 *  write callback.  This keeps all matchmaking, slot assignment,
 *  routing and timeout logic unit-testable without sockets.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCS_CORE_H
#define GCS_CORE_H

#include "gc_link.h"
#include "gc_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gcs_core gcs_core;
typedef struct gcs_peer gcs_peer;
typedef struct gcs_session gcs_session;

/** A transport connection owned by the network layer. */
typedef struct gcs_conn
{
    void *net;      /* network layer handle */
    gcs_peer *peer; /* maintained by the core */
} gcs_conn;

typedef struct
{
    char server_name[GC_MAX_NAME + 1];
    char access_key[GC_MAX_NAME + 1]; /* empty: no key required */
    int max_peers;
    int max_sessions;
    gc_u32 link_grace_ms; /* keep a silent peer's link for resumption */
    gc_u32 reserve_ms;    /* keep a lost client's slot for rejoin      */
    gc_u32 pending_ms;    /* how long a JOIN may wait for confirmation */
    int max_outq;         /* queued messages per peer before dropping  */
    int log_level;        /* 0 errors .. 3 debug                       */
} gcs_config;

typedef struct
{
    /** write bytes to the connection */
    void (*write)(void *ctx, gcs_conn *conn, const gc_u8 *data, size_t len);
    /** the core wants this connection closed */
    void (*close)(void *ctx, gcs_conn *conn);
    /** log line */
    void (*log)(void *ctx, int level, const char *line);
} gcs_io;

void gcs_default_config(gcs_config *cfg);

gcs_core *gcs_core_create(const gcs_config *cfg, const gcs_io *io, void *io_ctx, gc_u32 seed);
void gcs_core_destroy(gcs_core *core);

/** A new transport connection was accepted. */
void gcs_core_conn_open(gcs_core *core, gcs_conn *conn, gc_u32 now);
/** Bytes were received on a connection. */
void gcs_core_conn_input(gcs_core *core, gcs_conn *conn, const gc_u8 *data, size_t len, gc_u32 now);
/** The transport connection is gone; the conn struct may be freed after
 *  this call returns. */
void gcs_core_conn_closed(gcs_core *core, gcs_conn *conn, gc_u32 now);
/** Timers. Call regularly (e.g. every 50-100 ms). */
void gcs_core_poll(gcs_core *core, gc_u32 now);

/* ---- introspection (tests, status output) ---- */

typedef struct
{
    int peers;
    int peers_attached;
    int sessions;
    gc_u32 msgs_routed;
    gc_u32 msgs_dropped;
    gc_u32 resumes;
} gcs_stats;

void gcs_core_stats(const gcs_core *core, gcs_stats *st);
/** Writes a human readable status dump via the log callback. */
void gcs_core_dump(gcs_core *core);

#ifdef __cplusplus
}
#endif

#endif /* GCS_CORE_H */
