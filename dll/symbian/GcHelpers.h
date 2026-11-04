/** @file GcHelpers.h
 *
 *  Working versions of two helper classes that the original GameComms 2.x
 *  exported besides CGameBTComms: the device discoverer and the service
 *  resolver.  Some games (e.g. Ashen) use them for their own host search
 *  instead of the built-in list:
 *
 *    discoverer.StartL()  -> observer gets one "device" per open game of
 *                            the same title on the server, then KErrEof
 *    resolver.Start(dev)  -> observer gets KErrNone for such an entry
 *    CGameBTComms::DeclareRemoteDevice(dev) + Connect() joins it
 *
 *  The entries carry the synthetic addresses of GcDeviceToDetails(), so
 *  DeclareRemoteDevice() knows which session was chosen.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCHELPERS_H
#define GCHELPERS_H

#include "GcEngine.h"
#include "GcPlatform.h"
#include <e32base.h>

class CGcDiscoverer : public CBase, public MGcSessionListObserver
{
  public:
    static CGcDiscoverer *NewL(MGameBTDiscovererObserver *aObserver);
    ~CGcDiscoverer();

    TInt StartL();
    TInt Cancel();
    TBool IsIdle() const { return !iBusy; }
    TUint32 &DeviceClass() { return iDeviceClass; }
    void SetDeviceClass(const TUint32 &aClass) { iDeviceClass = aClass; }
    TInt SetSearchMode(TUint32 aMode)
    {
        iSearchMode = aMode;
        return KErrNone;
    }

    /* MGcSessionListObserver */
    void SessionList(const gcd_device *aSessions, TInt aCount);

  private:
    CGcDiscoverer(MGameBTDiscovererObserver *aObserver) : iObserver(aObserver) {}
    void ConstructL();
    static TInt Poll(TAny *aSelf);
    static TInt Deliver(TAny *aSelf);
    void DoPoll();
    void DoDeliver();
    void Finish();

    MGameBTDiscovererObserver *iObserver;
    TUint32 iDeviceClass;
    TUint32 iSearchMode;
    TBool iBusy;
    TBool iRequested; /* engine asked for the session list */
    TBool iCancelled;
    TInt iWaitTicks;
    CPeriodic *iTimer;          /* waits for the engine / the answer */
    CAsyncCallBack *iDeliverer; /* reports outside of the engine's dispatch */
    gcd_device iFound[GCD_MAX_SESSIONS];
    TInt iFoundCount;
    TDeviceDetails *iScratch; /* heap: 560 bytes */
};

class CGcResolver : public CBase
{
  public:
    static CGcResolver *NewL(MGameBTResolverObserver *aObserver);
    ~CGcResolver();
    TInt Start(const TDeviceDetails &aDevice);

  private:
    CGcResolver(MGameBTResolverObserver *aObserver) : iObserver(aObserver) {}
    void ConstructL();
    static TInt Deliver(TAny *aSelf);
    void DoDeliver();

    MGameBTResolverObserver *iObserver;
    TBool iBusy;
    CAsyncCallBack *iDeliverer;
    TDeviceDetails *iDevice; /* heap: 560 bytes */
};

#endif /* GCHELPERS_H */
