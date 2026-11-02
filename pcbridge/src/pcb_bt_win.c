/** @file pcb_bt_win.c
 *
 *  Windows Bluetooth side of the PC bridge.
 *
 *  - RFCOMM listening socket (Winsock AF_BTH)
 *  - SDP record for the Serial Port service class, so the N-Gage's
 *    service search finds the channel
 *  - optional automatic answer to legacy PIN pairing requests, so the
 *    N-Gage can pair without a Windows dialog
 *  - the PC is made discoverable and connectable while the bridge runs
 *
 *  The pairing and discovery functions live in bthprops.cpl and are
 *  loaded at run time, so no extra import library is needed and the
 *  bridge still starts if they are missing.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "pcb_sock.h"
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <stdio.h>
#include <string.h>
#include "pcb_bt.h"

/* Serial Port service class, 00001101-0000-1000-8000-00805F9B34FB */
static GUID g_spp_uuid = {0x00001101, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

typedef DWORD(WINAPI *fn_register_auth)(const BLUETOOTH_DEVICE_INFO *, HBLUETOOTH_AUTHENTICATION_REGISTRATION *,
                                        PFN_AUTHENTICATION_CALLBACK_EX, PVOID);
typedef DWORD(WINAPI *fn_send_auth)(HANDLE, BLUETOOTH_AUTHENTICATE_RESPONSE *);
typedef BOOL(WINAPI *fn_unregister_auth)(HBLUETOOTH_AUTHENTICATION_REGISTRATION);
typedef BOOL(WINAPI *fn_enable)(HANDLE, BOOL);

static sock_t                                g_listener = SOCK_INVALID;
static int                                   g_registered;
static WSAQUERYSETA                          g_qs;
static CSADDR_INFO                           g_csa;
static SOCKADDR_BTH                          g_local;
static char                                  g_service_name[64];
static char                                  g_pin[17];
static HMODULE                               g_bthprops;
static HBLUETOOTH_AUTHENTICATION_REGISTRATION g_auth;
static fn_send_auth                          g_send_auth;
static fn_unregister_auth                    g_unregister_auth;
static void (*g_log)(int level, const char *fmt, ...);

static void addr_to_bytes(BTH_ADDR a, unsigned char out[6])
{
    int i;
    for (i = 0; i < 6; i++)
    {
        out[i] = (unsigned char)((a >> (8 * (5 - i))) & 0xFF);
    }
}

/* Called by Windows on its own thread when a device wants to pair. */
static BOOL CALLBACK auth_callback(LPVOID param, PBLUETOOTH_AUTHENTICATION_CALLBACK_PARAMS p)
{
    BLUETOOTH_AUTHENTICATE_RESPONSE r;
    DWORD                           rc;
    unsigned char                   a[6];
    (void)param;

    memset(&r, 0, sizeof(r));
    r.bthAddressRemote = p->deviceInfo.Address;
    r.authMethod = p->authenticationMethod;
    addr_to_bytes(p->deviceInfo.Address.ullLong, a);
    if (p->authenticationMethod == BLUETOOTH_AUTHENTICATION_METHOD_LEGACY)
    {
        size_t n = strlen(g_pin);
        memcpy(r.pinInfo.pin, g_pin, n);
        r.pinInfo.pinLength = (UCHAR)n;
    }
    else if (p->authenticationMethod == BLUETOOTH_AUTHENTICATION_METHOD_NUMERIC_COMPARISON)
    {
        r.numericCompInfo.NumericValue = p->Numeric_Value;
    }
    else
    {
        g_log(1, "pairing request from %02x:%02x:%02x:%02x:%02x:%02x with unsupported method %d, answer it in Windows",
              a[0], a[1], a[2], a[3], a[4], a[5], (int)p->authenticationMethod);
        return FALSE;
    }
    rc = g_send_auth(NULL, &r);
    g_log(rc == ERROR_SUCCESS ? 2 : 1, "pairing request from %02x:%02x:%02x:%02x:%02x:%02x answered%s (%lu)", a[0], a[1],
          a[2], a[3], a[4], a[5], p->authenticationMethod == BLUETOOTH_AUTHENTICATION_METHOD_LEGACY ? " with the PIN" : "",
          (unsigned long)rc);
    return TRUE;
}

static void setup_pairing_and_discovery(void)
{
    fn_register_auth reg;
    fn_enable        discovery, incoming;

    g_bthprops = LoadLibraryA("bthprops.cpl");
    if (!g_bthprops)
    {
        g_log(1, "bthprops.cpl not found: pair the N-Gage in the Windows Bluetooth settings");
        return;
    }
    discovery = (fn_enable)(void (*)(void))GetProcAddress(g_bthprops, "BluetoothEnableDiscovery");
    incoming = (fn_enable)(void (*)(void))GetProcAddress(g_bthprops, "BluetoothEnableIncomingConnections");
    if (incoming)
    {
        incoming(NULL, TRUE);
    }
    if (discovery && !discovery(NULL, TRUE))
    {
        g_log(1, "could not make this PC discoverable; if the N-Gage does not find it, open the Windows Bluetooth settings");
    }

    if (g_pin[0] == '\0')
    {
        g_log(2, "automatic PIN answer disabled: confirm pairing requests in Windows");
        return;
    }
    reg = (fn_register_auth)(void (*)(void))GetProcAddress(g_bthprops, "BluetoothRegisterForAuthenticationEx");
    g_send_auth = (fn_send_auth)(void (*)(void))GetProcAddress(g_bthprops, "BluetoothSendAuthenticationResponseEx");
    g_unregister_auth = (fn_unregister_auth)(void (*)(void))GetProcAddress(g_bthprops, "BluetoothUnregisterAuthentication");
    if (!reg || !g_send_auth)
    {
        g_log(1, "automatic PIN answer not available: confirm pairing requests in Windows");
        return;
    }
    if (reg(NULL, &g_auth, auth_callback, NULL) != ERROR_SUCCESS)
    {
        g_auth = NULL;
        g_log(1, "could not register for pairing requests: confirm them in Windows");
        return;
    }
    g_log(2, "pairing requests are answered with PIN %s", g_pin);
}

int pcb_bt_open(const pcb_bt_cfg *cfg, char *err, size_t errlen)
{
    int len;

    g_log = cfg->log;
    strncpy(g_service_name, cfg->service_name, sizeof(g_service_name) - 1);
    strncpy(g_pin, cfg->pin, sizeof(g_pin) - 1);

    g_listener = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (g_listener == SOCK_INVALID)
    {
        snprintf(err, errlen, "no Bluetooth support (error %d): is a Bluetooth adapter present and switched on?",
                 sock_err());
        return -1;
    }
    memset(&g_local, 0, sizeof(g_local));
    g_local.addressFamily = AF_BTH;
    g_local.port = cfg->channel > 0 ? (ULONG)cfg->channel : BT_PORT_ANY;
    if (bind(g_listener, (SOCKADDR *)&g_local, sizeof(g_local)) != 0)
    {
        snprintf(err, errlen, "cannot use Bluetooth (bind error %d): is a Bluetooth adapter present and switched on?",
                 sock_err());
        pcb_bt_close();
        return -1;
    }
    len = sizeof(g_local);
    if (getsockname(g_listener, (SOCKADDR *)&g_local, &len) != 0 || listen(g_listener, 4) != 0)
    {
        snprintf(err, errlen, "cannot listen on Bluetooth (error %d)", sock_err());
        pcb_bt_close();
        return -1;
    }

    memset(&g_csa, 0, sizeof(g_csa));
    g_csa.LocalAddr.iSockaddrLength = sizeof(g_local);
    g_csa.LocalAddr.lpSockaddr = (LPSOCKADDR)&g_local;
    g_csa.iSocketType = SOCK_STREAM;
    g_csa.iProtocol = BTHPROTO_RFCOMM;
    memset(&g_qs, 0, sizeof(g_qs));
    g_qs.dwSize = sizeof(g_qs);
    g_qs.lpszServiceInstanceName = g_service_name;
    g_qs.lpszComment = (LPSTR) "GameComms bridge for the Nokia N-Gage";
    g_qs.lpServiceClassId = &g_spp_uuid;
    g_qs.dwNameSpace = NS_BTH;
    g_qs.dwNumberOfCsAddrs = 1;
    g_qs.lpcsaBuffer = &g_csa;
    if (WSASetServiceA(&g_qs, RNRSERVICE_REGISTER, 0) != 0)
    {
        snprintf(err, errlen, "cannot register the Serial Port service (error %d)", sock_err());
        pcb_bt_close();
        return -1;
    }
    g_registered = 1;
    g_log(2, "Bluetooth Serial Port service '%s' on RFCOMM channel %lu", g_service_name, (unsigned long)g_local.port);
    setup_pairing_and_discovery();
    return 0;
}

sock_t pcb_bt_listener(void)
{
    return g_listener;
}

sock_t pcb_bt_accept(unsigned char addr[6])
{
    SOCKADDR_BTH remote;
    int          len = sizeof(remote);
    sock_t       s;

    memset(&remote, 0, sizeof(remote));
    s = accept(g_listener, (SOCKADDR *)&remote, &len);
    if (s == SOCK_INVALID)
    {
        return SOCK_INVALID;
    }
    addr_to_bytes(remote.btAddr, addr);
    return s;
}

void pcb_bt_close(void)
{
    if (g_registered)
    {
        WSASetServiceA(&g_qs, RNRSERVICE_DELETE, 0);
        g_registered = 0;
    }
    if (g_auth && g_unregister_auth)
    {
        g_unregister_auth(g_auth);
        g_auth = NULL;
    }
    if (g_listener != SOCK_INVALID)
    {
        sock_close(g_listener);
        g_listener = SOCK_INVALID;
    }
    if (g_bthprops)
    {
        FreeLibrary(g_bthprops);
        g_bthprops = NULL;
    }
}
