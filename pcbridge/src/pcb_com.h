/** @file pcb_com.h
 *
 *  Serial port access for the PC bridge's COM mode.
 *
 *  On Windows this is used with a Bluetooth "incoming" COM port: Windows
 *  itself accepts the N-Gage's Serial Port connection and passes the data
 *  through the COM port.  Other systems open any serial device (the
 *  automated tests use a pseudo terminal).
 *
 *  All calls are non-blocking.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef PCB_COM_H
#define PCB_COM_H

#include <stddef.h>

typedef struct pcb_com pcb_com;

/** Open a port ("COM5" on Windows, a device path elsewhere).  Returns NULL
 *  and writes a reason to err on failure. */
pcb_com *pcb_com_open(const char *name, char *err, size_t errlen);

/** Read what is available: number of bytes (0 = nothing), -1 on error. */
int pcb_com_read(pcb_com *c, unsigned char *buf, int max);

/** Write as much as possible: number of bytes written, -1 on error. */
int pcb_com_write(pcb_com *c, const unsigned char *buf, int len);

/** Modem lines as bits (1 CTS, 2 DSR, 4 RING, 8 carrier detect), -1 if
 *  unknown.  On a Bluetooth COM port they show whether a device is
 *  connected. */
int pcb_com_lines(pcb_com *c);

void pcb_com_close(pcb_com *c);

#endif /* PCB_COM_H */
