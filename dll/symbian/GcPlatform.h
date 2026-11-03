/** @file GcPlatform.h
 *
 *  Binary-compatible declarations of the types that games share with
 *  GAMECOMMS.DLL, as reconstructed from the original binaries (see
 *  docs/REVERSE_ENGINEERING.md).  Only layout and virtual-function order
 *  matter for compatibility; names are descriptive.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCPLATFORM_H
#define GCPLATFORM_H

#include <e32base.h>
#include <e32std.h>
#include <es_sock.h>
#include <bt_sock.h>

extern "C" {
#include "gc_dev.h"
}

/* ------------------------------------------------------------------ */
/* GameComms 1.x callback interface (variant v1, SDK 1.0)              */
/*                                                                     */
/* Virtual function order verified from the call sites in v1:          */
/* +0x08 .. +0x34 (GCC 2.9 vtable thunks: first slot at +8).           */
/* ------------------------------------------------------------------ */
class MGameBTCommsNotify
{
public:
    virtual void ClientConnected(TUint16 aClientId, TDesC& aClientName, TInt aError) = 0;  /* +0x08 */
    virtual void HostSelected(TInt aError) = 0;                                             /* +0x0c */
    virtual void HostConnected(TInt aError) = 0;                                            /* +0x10 */
    virtual void StartMultiPlayerGame(TInt aError) = 0;                                     /* +0x14 */
    virtual void ContinueMultiPlayerGame() = 0;                                             /* +0x18 */
    virtual void PauseMultiPlayerGame() = 0;                                                /* +0x1c */
    virtual void EndMultiPlayerGame(TInt aReason) = 0;                                      /* +0x20 */
    virtual void ConnectedClientEndedGame(TUint16 aClientId) = 0;                           /* +0x24 */
    virtual void ClientDisconnected(TUint16 aClientId, TInt aError) = 0;                    /* +0x28 */
    virtual void HostDisconnected(TInt aError) = 0;                                         /* +0x2c */
    virtual void ReceiveDataFromClient(TUint16 aClientId, TDesC8& aData) = 0;               /* +0x30 */
    virtual void ReceiveDataFromHost(TDesC8& aData) = 0;                                    /* +0x34 */
};

/* ------------------------------------------------------------------ */
/* GameComms 2.x remote device description (variants v2..v11)          */
/*                                                                     */
/* 560 bytes: TBTSockAddr (40) followed by TBuf<256> (520).  The size   */
/* and the name offset (+40) are verified from the copy loops in       */
/* CGameBTComms::DeclareRemoteDevice and the host's device table.      */
/* ------------------------------------------------------------------ */
class TDeviceDetails
{
public:
    TBTSockAddr iAddress;
    TBuf<256>   iName;
};

/* ------------------------------------------------------------------ */
/* GameComms 2.x callback interface: five pure virtuals at +0x08..0x18 */
/* (all call sites in v2..v11 enumerated).  Names are ours.            */
/* ------------------------------------------------------------------ */
class MGameBTCommsNotify2
{
public:
    /** Host, accept mode "confirm" without built-in UI: a device wants
     *  to join; accept it with DeclareRemoteDevice(). */
    virtual void ConfirmDevice(const TDeviceDetails& aDevice) = 0;                          /* +0x08 */
    /** A remote device was declared (host: client id 1..3, client: 0). */
    virtual void DeviceDeclared(TInt aId, const TDeviceDetails& aDevice, TInt aError) = 0;  /* +0x0c */
    /** Connection to aId established (or failed with aError). */
    virtual void Connected(TInt aId, TInt aError) = 0;                                      /* +0x10 */
    /** Connection to aId closed. */
    virtual void Disconnected(TInt aId, TInt aReason) = 0;                                  /* +0x14 */
    /** Data from aId (client: 0 = host). */
    virtual void ReceiveData(TInt aId, const TDesC8& aData) = 0;                            /* +0x18 */
};

/* ------------------------------------------------------------------ */
/* Observers of the exported helper classes (GameComms 2.x).  Some      */
/* games (e.g. Ashen) search for hosts themselves with the discoverer   */
/* and check them with the resolver.  Layouts verified from the call    */
/* sites in the originals: one function at vtable +0x08 each.           */
/* ------------------------------------------------------------------ */

/** CGameBTDiscoverer: once per device found (aError KErrNone), then once
 *  with an empty TDeviceDetails and KErrEof when the search is over. */
class MGameBTDiscovererObserver
{
public:
    virtual void DeviceFound(const TDeviceDetails& aDevice, TInt aError) = 0; /* +0x08 */
};

/** CGameBTResolver: result of Start(): the device, its RFCOMM channel and
 *  KErrNone, or KErrNotFound / another error. */
class MGameBTResolverObserver
{
public:
    virtual void Resolved(const TDeviceDetails& aDevice, TInt aChannel, TInt aError) = 0; /* +0x08 */
};

/* ------------------------------------------------------------------ */
/* RSGEDebugLog - layout of the original (20 bytes, vptr at +0x10).    */
/* Games may embed it, so the size must never change.  The shipped     */
/* release builds were no-ops; so is this one.                         */
/* ------------------------------------------------------------------ */
class RSGEDebugLog
{
public:
    enum TLogLevel { EAll = 0 };
    RSGEDebugLog();
    virtual ~RSGEDebugLog();
public:
    TInt  iReserved0;
    TInt  iReserved1;
    TInt  iReserved2;
    TInt  iLevel;     /* +0x0c, written by SetLoggingLevel */
};

/* Magic prefix of the synthetic Bluetooth addresses we hand out in
 * TDeviceDetails so that DeclareRemoteDevice() can map them back. */
const TUint8 KGcAddrMagic0 = 0x47; /* 'G' */
const TUint8 KGcAddrMagic1 = 0x43; /* 'C' */

void GcDeviceToDetails(const gcd_device& aDev, TDeviceDetails& aDetails);
void GcDetailsToDevice(const TDeviceDetails& aDetails, gcd_device& aDev);

#endif /* GCPLATFORM_H */
