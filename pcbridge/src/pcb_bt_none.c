/** @file pcb_bt_none.c
 *
 *  Bluetooth stub for systems other than Windows.  The bridge still runs
 *  with --device-tcp (used by the automated tests).
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <stdio.h>
#include "pcb_bt.h"

int pcb_bt_open(const pcb_bt_cfg *cfg, char *err, size_t errlen)
{
    (void)cfg;
    snprintf(err, errlen, "Bluetooth is only supported on Windows in this version");
    return -1;
}

sock_t pcb_bt_listener(void)
{
    return SOCK_INVALID;
}

sock_t pcb_bt_accept(unsigned char addr[6])
{
    (void)addr;
    return SOCK_INVALID;
}

void pcb_bt_close(void)
{
}
