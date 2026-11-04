/** @file GcHelpers.cpp
 *
 *  Device discoverer and service resolver for games that search for hosts
 *  themselves, see GcHelpers.h.
 *
 *  Behaviour kept from the originals (verified in the v8 code):
 *    - StartL() while a search runs panics; Cancel() when idle returns
 *      KErrNotReady, otherwise the search ends with the usual final
 *      callback (empty details, KErrEof)
 *    - every result is reported from an active object, never from inside
 *      the call that started the search
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "GcHelpers.h"

_LIT(KGcDiscovererPanic, "GC-DISCOVERER");
_LIT(KGcResolverPanic, "GC-RESOLVER");

const TInt KGcPollUs = 250000; /* look for the engine / wait for the answer */
const TInt KGcMaxPolls = 40;   /* give up after 10 s: report "nothing found" */

static void HelperLog(const TDesC8 &aText, TInt aValue)
{
    CGcEngine *e = CGcEngine::Current();
    if (e) {
        e->LogNum(aText, aValue);
    }
}

/* ------------------------------------------------------------------ */
/* discoverer                                                          */
/* ------------------------------------------------------------------ */

CGcDiscoverer *CGcDiscoverer::NewL(MGameBTDiscovererObserver *aObserver)
{
    CGcDiscoverer *self = new (ELeave) CGcDiscoverer(aObserver);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}

void CGcDiscoverer::ConstructL()
{
    /* the original filters for phones: major service 0x200, phone, cellular */
    iDeviceClass = (0x200u << 13) | (2u << 8) | (1u << 2);
    iTimer = CPeriodic::NewL(CActive::EPriorityStandard);
    TCallBack cb(Deliver, this);
    iDeliverer = new (ELeave) CAsyncCallBack(cb, CActive::EPriorityStandard);
    iScratch = new (ELeave) TDeviceDetails;
    HelperLog(_L8("game created a device discoverer"), 0);
}

CGcDiscoverer::~CGcDiscoverer()
{
    CGcEngine *e = CGcEngine::Current();
    if (e) {
        e->CancelSessionList(this);
    }
    delete iTimer;
    delete iDeliverer;
    delete iScratch;
}

TInt CGcDiscoverer::StartL()
{
    if (iBusy) {
        User::Panic(KGcDiscovererPanic, 0); /* as the original */
    }
    iBusy = ETrue;
    iCancelled = EFalse;
    iRequested = EFalse;
    iFoundCount = 0;
    iWaitTicks = 0;
    HelperLog(_L8("game starts a device search"), 0);
    iTimer->Cancel();
    iTimer->Start(0, KGcPollUs, TCallBack(Poll, this));
    return KErrNone;
}

TInt CGcDiscoverer::Cancel()
{
    if (!iBusy) {
        return KErrNotReady;
    }
    iCancelled = ETrue;
    iTimer->Cancel();
    CGcEngine *e = CGcEngine::Current();
    if (e) {
        e->CancelSessionList(this);
    }
    if (!iDeliverer->IsActive()) {
        iDeliverer->CallBack(); /* final KErrEof report, asynchronously */
    }
    return KErrNone;
}

TInt CGcDiscoverer::Poll(TAny *aSelf)
{
    ((CGcDiscoverer *)aSelf)->DoPoll();
    return 0;
}

void CGcDiscoverer::DoPoll()
{
    if (!iBusy || iCancelled) {
        iTimer->Cancel();
        return;
    }
    if (!iRequested) {
        /* the game's GameComms object starts the engine; until it exists
           (StartClientL not called yet) keep waiting */
        CGcEngine *e = CGcEngine::Current();
        if (e) {
            iRequested = ETrue;
            e->RequestSessionList(this);
        }
    }
    if (++iWaitTicks > KGcMaxPolls) {
        HelperLog(_L8("device search timed out (no answer from the server)"), 0);
        iTimer->Cancel();
        CGcEngine *e = CGcEngine::Current();
        if (e) {
            e->CancelSessionList(this);
        }
        iFoundCount = 0;
        if (!iDeliverer->IsActive()) {
            iDeliverer->CallBack();
        }
    }
}

void CGcDiscoverer::SessionList(const gcd_device *aSessions, TInt aCount)
{
    TInt i;
    iTimer->Cancel();
    iFoundCount = 0;
    for (i = 0; i < aCount && i < GCD_MAX_SESSIONS; i++) {
        iFound[iFoundCount++] = aSessions[i];
    }
    if (!iDeliverer->IsActive()) {
        iDeliverer->CallBack();
    }
}

TInt CGcDiscoverer::Deliver(TAny *aSelf)
{
    ((CGcDiscoverer *)aSelf)->DoDeliver();
    return 0;
}

void CGcDiscoverer::DoDeliver()
{
    TInt i;
    if (!iBusy) {
        return;
    }
    for (i = 0; i < iFoundCount && !iCancelled && iObserver; i++) {
        GcDeviceToDetails(iFound[i], *iScratch);
        iObserver->DeviceFound(*iScratch, KErrNone);
    }
    HelperLog(_L8("device search reported games"), iCancelled ? 0 : iFoundCount);
    Finish();
}

void CGcDiscoverer::Finish()
{
    iBusy = EFalse; /* before the call: the game may start the next search */
    iCancelled = EFalse;
    iFoundCount = 0;
    iScratch->iAddress = TBTSockAddr();
    iScratch->iName.Zero();
    if (iObserver) {
        iObserver->DeviceFound(*iScratch, KErrEof);
    }
}

/* ------------------------------------------------------------------ */
/* resolver                                                            */
/* ------------------------------------------------------------------ */

CGcResolver *CGcResolver::NewL(MGameBTResolverObserver *aObserver)
{
    CGcResolver *self = new (ELeave) CGcResolver(aObserver);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}

void CGcResolver::ConstructL()
{
    TCallBack cb(Deliver, this);
    iDeliverer = new (ELeave) CAsyncCallBack(cb, CActive::EPriorityStandard);
    iDevice = new (ELeave) TDeviceDetails;
}

CGcResolver::~CGcResolver()
{
    delete iDeliverer;
    delete iDevice;
}

TInt CGcResolver::Start(const TDeviceDetails &aDevice)
{
    if (iBusy) {
        User::Panic(KGcResolverPanic, 0); /* as the original */
    }
    iBusy = ETrue;
    *iDevice = aDevice;
    iDeliverer->CallBack();
    return KErrNone;
}

TInt CGcResolver::Deliver(TAny *aSelf)
{
    ((CGcResolver *)aSelf)->DoDeliver();
    return 0;
}

void CGcResolver::DoDeliver()
{
    gcd_device d;
    iBusy = EFalse;
    GcDetailsToDevice(*iDevice, d);
    /* entries from the discoverer are games of this title on the server:
       "this game is running there".  Real Bluetooth devices are not. */
    TBool isGame = d.slot == GC_SLOT_HOST && d.session_id != 0;
    HelperLog(_L8("game checks a host, result"), isGame ? KErrNone : KErrNotFound);
    if (iObserver) {
        iObserver->Resolved(*iDevice, isGame ? 1 : 0, isGame ? KErrNone : KErrNotFound);
    }
}
