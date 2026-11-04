/** @file sim.h
 *
 *  Deterministic end-to-end simulator:
 *
 *    [gcd_dev N-Gage engine] --SPP pipe-- [bridge_core] --TCP pipe-- [gcs_core server]
 *
 *  All three components are the production code; only the byte pipes
 *  and the clock are simulated.  Pipes support fragmentation, byte
 *  corruption, frame loss and disconnects to exercise the protocol.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef SIM_H
#define SIM_H

#include "bridge_core.h"
#include "gc_dev.h"
#include "gcs_core.h"

#define SIM_MAX_DEV  6
#define SIM_PIPE_CAP (256 * 1024)
#define SIM_MAX_EVT  256

typedef struct
{
    unsigned char buf[SIM_PIPE_CAP];
    size_t len;
    int up;
    int corrupt_permille; /* per byte */
    int chunk;            /* max delivery chunk, 0 = all */
} sim_pipe;

typedef struct
{
    int idx;
    gcd_dev dev;
    bridge br;
    gcs_conn conn;
    sim_pipe spp_to_bridge;
    sim_pipe spp_to_dev;
    sim_pipe tcp_to_server;
    sim_pipe tcp_to_bridge;
    int tcp_connect_req;
    int tcp_close_req;
    int srv_close_req;
    gcd_event ev[SIM_MAX_EVT];
    int n_ev;
    char name[16];
} sim_node;

typedef struct
{
    gcs_core *core;
    sim_node n[SIM_MAX_DEV];
    int count;
    gc_u32 now;
    int verbose;
} sim_world;

void sim_init(sim_world *w, int count, gc_u8 api, gc_u32 uid);
void sim_free(sim_world *w);
/** advance simulated time in 10 ms steps */
void sim_run(sim_world *w, int ms);
/** counts events of a type on node i (optionally with id/err filter) */
int sim_count(sim_world *w, int i, int type);
gcd_event *sim_last(sim_world *w, int i, int type);
void sim_clear(sim_world *w, int i);

/* impairments */
void sim_tcp_drop(sim_world *w, int i);      /* TCP connection breaks       */
void sim_bridge_reboot(sim_world *w, int i); /* ESP32 resets                */
void sim_spp_drop(sim_world *w, int i);      /* BT link to the N-Gage drops */
void sim_spp_restore(sim_world *w, int i);

#endif
