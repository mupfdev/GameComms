/** @file pcb_com_win.c
 *
 *  Windows serial port access for the PC bridge's COM mode.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "pcb_com.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

struct pcb_com
{
    HANDLE h;
};

pcb_com *pcb_com_open(const char *name, char *err, size_t errlen)
{
    char path[64];
    HANDLE h;
    COMMTIMEOUTS to;
    DCB dcb;
    pcb_com *c;

    /* \\.\COMn works for every port number, plain "COMn" only up to 9 */
    snprintf(path, sizeof(path), "\\\\.\\%s", name);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, errlen, "cannot open %s (error %lu)", name, (unsigned long)GetLastError());
        return NULL;
    }

    /* the speed is irrelevant for Bluetooth ports; raw 8N1 without flow
       control so that no byte is interpreted */
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(h, &dcb)) {
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fNull = FALSE;
        dcb.fAbortOnError = FALSE;
        SetCommState(h, &dcb);
    }

    /* reads return at once with whatever is there, writes wait at most 50 ms */
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 50;
    if (!SetCommTimeouts(h, &to)) {
        snprintf(err, errlen, "cannot configure %s (error %lu)", name, (unsigned long)GetLastError());
        CloseHandle(h);
        return NULL;
    }
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    c = (pcb_com *)calloc(1, sizeof(*c));
    if (!c) {
        CloseHandle(h);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    c->h = h;
    return c;
}

int pcb_com_read(pcb_com *c, unsigned char *buf, int max)
{
    DWORD n = 0, errors;
    if (!ReadFile(c->h, buf, (DWORD)max, &n, NULL)) {
        ClearCommError(c->h, &errors, NULL);
        return GetLastError() == ERROR_OPERATION_ABORTED ? 0 : -1;
    }
    return (int)n;
}

int pcb_com_write(pcb_com *c, const unsigned char *buf, int len)
{
    DWORD n = 0, errors;
    if (!WriteFile(c->h, buf, (DWORD)len, &n, NULL)) {
        ClearCommError(c->h, &errors, NULL);
        /* a write timeout (no N-Gage connected) is not an error */
        return GetLastError() == ERROR_OPERATION_ABORTED ? 0 : (n > 0 ? (int)n : 0);
    }
    return (int)n;
}

int pcb_com_lines(pcb_com *c)
{
    DWORD s = 0;
    if (!GetCommModemStatus(c->h, &s)) {
        return -1;
    }
    return ((s & MS_CTS_ON) ? 1 : 0) | ((s & MS_DSR_ON) ? 2 : 0) | ((s & MS_RING_ON) ? 4 : 0) |
           ((s & MS_RLSD_ON) ? 8 : 0);
}

void pcb_com_close(pcb_com *c)
{
    if (c) {
        CloseHandle(c->h);
        free(c);
    }
}
