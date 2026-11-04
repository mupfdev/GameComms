/** @file pcb_bt.h
 *
 *  Bluetooth side of the PC bridge: an RFCOMM listener that N-Gages
 *  connect to, published as a Serial Port service so that the replacement
 *  GAMECOMMS.DLL finds it with its normal service search.
 *
 *  Implemented for Windows (pcb_bt_win.c).  Other systems use the stub in
 *  pcb_bt_none.c; there the bridge can still be tested with --device-tcp.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef PCB_BT_H
#define PCB_BT_H

#include "pcb_sock.h"

typedef struct
{
    const char *service_name; /* shown in SDP, e.g. "GameComms Bridge" */
    const char *pin;          /* legacy PIN answered automatically, "" = off */
    int channel;              /* RFCOMM channel, 0 = any free channel */
    const char *adapter;      /* Linux: BlueZ adapter, e.g. "hci0" */
    void (*log)(int level, const char *fmt, ...);
} pcb_bt_cfg;

/** Open the listener and register the service.  Returns 0 on success,
 *  otherwise writes a reason to err. */
int pcb_bt_open(const pcb_bt_cfg *cfg, char *err, size_t errlen);

/** Listening socket (for select), SOCK_INVALID if not open. */
sock_t pcb_bt_listener(void);

/** Accept a pending connection.  addr receives the remote Bluetooth
 *  address, most significant byte first.  Returns SOCK_INVALID on error. */
sock_t pcb_bt_accept(unsigned char addr[6]);

/** Unregister the service and close the listener. */
void pcb_bt_close(void);

#endif /* PCB_BT_H */
