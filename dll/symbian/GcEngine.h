/** @file GcEngine.h
 *
 *  Symbian side of the GameComms engine: owns the portable gcd_dev
 *  state machine, the Bluetooth link to the bridge, the timer and the
 *  asynchronous event delivery to the game.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCENGINE_H
#define GCENGINE_H

#include <e32base.h>
#include <f32file.h>
#include "GcPlatform.h"
#include "GcConfig.h"
#include "GcTransport.h"

class CGcListControl;
class CEikAppUi;

/** Receives engine events (implemented by the CGameBTComms classes). */
class MGcEventSink
{
public:
    virtual void HandleGcEventL(const gcd_event& aEvent) = 0;
};

class MGcListObserver
{
public:
    virtual void ListItemSelected(TInt aIndex) = 0;
    virtual void ListCancelled() = 0;
};

class CGcEngine : public CBase, public MGcTransportObserver, public MGcListObserver
{
public:
    static CGcEngine* NewL(MGcEventSink& aSink, TUint8 aApi, TUint8 aVariant, TUint32 aGameUid);
    ~CGcEngine();

    gcd_dev& Dev() { return *iDev; }
    gc_u32 Now();

    /** Bring up the bridge connection if necessary. */
    void EnsureTransport();
    /** Run the engine after an API call (timers, event delivery). */
    void Kick();

    /** Built-in UI (host selection / confirm list). */
    void SetAppUi(CEikAppUi* aAppUi) { iAppUi = aAppUi; }
    TBool IsShowingDialog() const;

    const TGcConfig& Config() const { return iConfig; }
    void Log(const TDesC8& aText);
    void LogNum(const TDesC8& aText, TInt aValue);

    /* MGcTransportObserver */
    void TransportUp();
    void TransportData(const TDesC8& aData);
    void TransportDown(TInt aError);
    void TransportSelecting(TBool aShowing);
    void TransportProgress(const TDesC8& aStep, TInt aValue);

    /* MGcListObserver */
    void ListItemSelected(TInt aIndex);
    void ListCancelled();

private:
    CGcEngine(MGcEventSink& aSink);
    void ConstructL(TUint8 aApi, TUint8 aVariant, TUint32 aGameUid);
    static TInt TimerCallBack(TAny* aSelf);
    static TInt DispatchCallBack(TAny* aSelf);
    static TInt RetryCallBack(TAny* aSelf);
    void Tick();
    void Dispatch();
    void ShowListL(const gcd_event& aEvent, TBool aHostConfirm);
    void CloseList();
    void RefreshListL();

    /* gcd_ops trampolines */
    static void OpWrite(void* aCtx, const gc_u8* aData, size_t aLen);
    static void OpLog(void* aCtx, const char* aLine);
    static void OpWake(void* aCtx);

    MGcEventSink&    iSink;
    TGcConfig        iConfig;
    gcd_dev*         iDev;
    CGcTransport*    iTransport;
    CPeriodic*       iTimer;
    CPeriodic*       iRetry;
    CAsyncCallBack*  iDispatcher;
    CGcListControl*  iList;
    CGcListControl*  iDoomedList;
    TBool            iListIsHostConfirm;
    gcd_device       iListItems[GCD_MAX_SESSIONS];
    TInt             iListCount;
    CEikAppUi*       iAppUi;
    TBool            iSelectingBridge;
    TBool            iWantTransport;
    TInt             iTickPeriodUs;
    TUint            iLastTick;
    gc_u32           iMs;
    RFs              iFs;
    RFile            iLogFile;
    TBool            iLogOpen;
    TBool            iDispatching;
    gcd_event*       iEvent;      /* scratch, heap: events are ~1.1 KB */
};

#endif /* GCENGINE_H */
