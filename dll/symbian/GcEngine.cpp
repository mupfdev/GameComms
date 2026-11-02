/** @file GcEngine.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <eikenv.h>
#include <eikappui.h>
#include <coemain.h>
#include <badesca.h>
#include <e32hal.h>
#include "GcEngine.h"
#include "GcListControl.h"

_LIT(KGcLogFile, "E:\\GameComms.log");
_LIT(KGcSelectHost, "Select game");
_LIT(KGcConfirmPlayers, "Players");

const TInt KGcTimerUs = 20000;        /* 50 Hz engine tick while active */
const TInt KGcRetryUs = 3000000;      /* bridge reconnect interval      */

CGcEngine* CGcEngine::NewL(MGcEventSink& aSink, TUint8 aApi, TUint8 aVariant, TUint32 aGameUid)
{
    CGcEngine* self = new (ELeave) CGcEngine(aSink);
    CleanupStack::PushL(self);
    self->ConstructL(aApi, aVariant, aGameUid);
    CleanupStack::Pop();
    return self;
}

CGcEngine::CGcEngine(MGcEventSink& aSink) : iSink(aSink)
{
}

static void GetBluetoothNameL(TDes8& aName)
{
    /* the local Bluetooth name is the natural "DID" of a device */
    RSocketServ ss;
    User::LeaveIfError(ss.Connect());
    CleanupClosePushL(ss);
    RHostResolver hr;
    _LIT(KBTLinkManager, "BTLinkManager");
    TProtocolDesc pd;
    User::LeaveIfError(ss.FindProtocol(KBTLinkManager(), pd));
    User::LeaveIfError(hr.Open(ss, pd.iAddrFamily, pd.iProtocol));
    CleanupClosePushL(hr);
    THostName name;
    User::LeaveIfError(hr.GetHostName(name));
    aName.Copy(name.Left(32));
    CleanupStack::PopAndDestroy(2);
}

void CGcEngine::ConstructL(TUint8 aApi, TUint8 aVariant, TUint32 aGameUid)
{
    iConfig.Load();
    if (iConfig.iLog && iFs.Connect() == KErrNone)
    {
        iLogOpen = iLogFile.Replace(iFs, KGcLogFile, EFileWrite | EFileShareAny) == KErrNone;
    }

    gcd_config cfg;
    gcd_ops    ops;
    gcd_default_config(&cfg);
    cfg.api = aApi;
    cfg.variant = aVariant;
    cfg.game_uid = aGameUid;
    cfg.auto_join = iConfig.iAutoJoin ? 1 : 0;
    TBuf8<33> name = iConfig.iDeviceName;
    if (name.Length() == 0)
    {
        TRAPD(err, GetBluetoothNameL(name));
        if (err != KErrNone || name.Length() == 0)
        {
            name = _L8("N-Gage");
        }
    }
    TInt i;
    for (i = 0; i < name.Length() && i < GC_MAX_NAME; i++)
    {
        cfg.name[i] = (char)name[i];
    }
    cfg.name[i] = '\0';
    TTime t;
    t.UniversalTime();
    cfg.link_id = t.Int64().Low() ^ (TUint32)this ^ 0x5A5A0000;
    if (cfg.link_id == 0)
    {
        cfg.link_id = 1;
    }
    ops.write = OpWrite;
    ops.log = OpLog;
    ops.wake = OpWake;

    iDev = new (ELeave) gcd_dev;
    iEvent = new (ELeave) gcd_event;
    gcd_init(iDev, &cfg, &ops, this);

    iTransport = CGcTransport::NewL(*this, iConfig);
    iTimer = CPeriodic::NewL(CActive::EPriorityStandard);
    iRetry = CPeriodic::NewL(CActive::EPriorityIdle);
    TCallBack dispatch(DispatchCallBack, this);
    iDispatcher = new (ELeave) CAsyncCallBack(dispatch, CActive::EPriorityStandard);

    TTimeIntervalMicroSeconds32 period;
    UserHal::TickPeriod(period);
    iTickPeriodUs = period.Int();
    iLastTick = User::TickCount();
    iTimer->Start(KGcTimerUs, KGcTimerUs, TCallBack(TimerCallBack, this));
}

CGcEngine::~CGcEngine()
{
    if (iDev)
    {
        gcd_end_session(iDev, Now());
        gc_link_close(&iDev->link, GC_LR_LOCAL);
    }
    CloseList();
    delete iDoomedList;
    delete iDispatcher;
    delete iRetry;
    delete iTimer;
    delete iTransport;
    delete iEvent;
    delete iDev;
    if (iLogOpen)
    {
        iLogFile.Close();
    }
    iFs.Close();
}

gc_u32 CGcEngine::Now()
{
    TUint tick = User::TickCount();
    iMs += (gc_u32)((tick - iLastTick) * (TUint)iTickPeriodUs / 1000);
    iLastTick = tick;
    return iMs;
}

void CGcEngine::Log(const TDesC8& aText)
{
    if (!iLogOpen)
    {
        return;
    }
    TBuf8<16> ts;
    ts.Num(iMs);
    ts.Append(' ');
    iLogFile.Write(ts);
    iLogFile.Write(aText);
    iLogFile.Write(_L8("\r\n"));
}

void CGcEngine::EnsureTransport()
{
    iWantTransport = ETrue;
    if (!iTransport->IsBusy())
    {
        TRAPD(err, iTransport->ConnectL());
        if (err != KErrNone)
        {
            TransportDown(err);
        }
    }
}

void CGcEngine::Kick()
{
    gcd_poll(iDev, Now());
    if (iDev->ev_count && !iDispatcher->IsActive())
    {
        iDispatcher->CallBack();
    }
}

TBool CGcEngine::IsShowingDialog() const
{
    return iList != NULL || iSelectingBridge;
}

/* ---- gcd_ops ---- */

void CGcEngine::OpWrite(void* aCtx, const gc_u8* aData, size_t aLen)
{
    CGcEngine* self = (CGcEngine*)aCtx;
    if (self->iTransport->Write(TPtrC8(aData, (TInt)aLen)) != KErrNone)
    {
        self->Log(_L8("write buffer overflow (link layer will retransmit)"));
    }
}

void CGcEngine::OpLog(void* aCtx, const char* aLine)
{
    CGcEngine* self = (CGcEngine*)aCtx;
    self->Log(TPtrC8((const TUint8*)aLine, User::StringLength((const TUint8*)aLine)));
}

void CGcEngine::OpWake(void* aCtx)
{
    CGcEngine* self = (CGcEngine*)aCtx;
    if (!self->iDispatcher->IsActive())
    {
        self->iDispatcher->CallBack();
    }
}

/* ---- timers ---- */

TInt CGcEngine::TimerCallBack(TAny* aSelf)
{
    ((CGcEngine*)aSelf)->Tick();
    return 1;
}

TInt CGcEngine::RetryCallBack(TAny* aSelf)
{
    CGcEngine* self = (CGcEngine*)aSelf;
    self->iRetry->Cancel();
    if (self->iWantTransport)
    {
        self->EnsureTransport();
    }
    return 0;
}

void CGcEngine::Tick()
{
    if (iDoomedList)
    {
        delete iDoomedList;
        iDoomedList = NULL;
    }
    gcd_poll(iDev, Now());
}

/* ---- transport ---- */

void CGcEngine::TransportUp()
{
    Log(_L8("bridge connected"));
    gcd_transport_up(iDev, Now());
}

void CGcEngine::TransportData(const TDesC8& aData)
{
    gcd_transport_input(iDev, aData.Ptr(), (size_t)aData.Length(), Now());
}

void CGcEngine::TransportDown(TInt aError)
{
    TBuf8<48> t;
    t.Copy(_L8("bridge connection failed/lost: "));
    t.AppendNum(aError);
    Log(t);
    gcd_transport_down(iDev, Now());
    if (aError == KErrCancel && iDev->role == GCD_ROLE_IDLE)
    {
        iWantTransport = EFalse;
    }
    if (aError == KErrCancel && iDev->selecting)
    {
        /* user cancelled the bridge selection: cancel the host selection */
        gcd_select_session(iDev, 0, Now());
        iWantTransport = EFalse;
    }
    if (iWantTransport && !iRetry->IsActive())
    {
        iRetry->Start(KGcRetryUs, KGcRetryUs, TCallBack(RetryCallBack, this));
    }
}

void CGcEngine::TransportSelecting(TBool aShowing)
{
    iSelectingBridge = aShowing;
}

/* ---- event dispatch ---- */

TInt CGcEngine::DispatchCallBack(TAny* aSelf)
{
    ((CGcEngine*)aSelf)->Dispatch();
    return 0;
}

void CGcEngine::Dispatch()
{
    if (iDispatching)
    {
        return;
    }
    iDispatching = ETrue;
    /* deliver one batch; callbacks may call back into the API */
    TInt budget = GCD_EVENT_QUEUE;
    while (budget-- > 0 && gcd_pop_event(iDev, iEvent))
    {
        switch (iEvent->type)
        {
            case GCD_EV_SESSION_LIST:
                TRAPD(e1, ShowListL(*iEvent, EFalse));
                (void)e1;
                break;
            case GCD_EV_UI_ADD_DEVICE:
                TRAPD(e2, ShowListL(*iEvent, ETrue));
                (void)e2;
                break;
            case GCD_EV_UI_CLOSE:
                CloseList();
                break;
            case GCD_EV_TRANSPORT:
                break;
            default:
            {
                TRAPD(err, iSink.HandleGcEventL(*iEvent));
                if (err != KErrNone)
                {
                    Log(_L8("game callback left"));
                }
                break;
            }
        }
    }
    iDispatching = EFalse;
    if (iDev->ev_count && !iDispatcher->IsActive())
    {
        iDispatcher->CallBack();
    }
}

/* ---- built-in UI ---- */

void CGcEngine::ShowListL(const gcd_event& aEvent, TBool aHostConfirm)
{
    if (!iList)
    {
        TRect      rect;
        CCoeEnv*   env = CCoeEnv::Static();
        CEikAppUi* ui = iAppUi;
        if (!ui && env)
        {
            ui = (CEikAppUi*)env->AppUi();
        }
        if (ui)
        {
            rect = ui->ClientRect();
        }
        else
        {
            rect = TRect(0, 0, 176, 208);
        }
        iList = CGcListControl::NewL(*this, aHostConfirm ? KGcConfirmPlayers() : KGcSelectHost(), rect);
        iListIsHostConfirm = aHostConfirm;
        iListCount = 0;
    }
    if (aHostConfirm)
    {
        if (iListCount < GCD_MAX_SESSIONS)
        {
            iListItems[iListCount++] = aEvent.dev;
        }
    }
    else
    {
        TInt n = aEvent.id;
        if (n > GCD_MAX_SESSIONS)
        {
            n = GCD_MAX_SESSIONS;
        }
        Mem::Copy(iListItems, aEvent.data, n * (TInt)sizeof(gcd_device));
        iListCount = n;
    }
    RefreshListL();
}

void CGcEngine::RefreshListL()
{
    if (!iList)
    {
        return;
    }
    CDesCArrayFlat* items = new (ELeave) CDesCArrayFlat(4);
    CleanupStack::PushL(items);
    TInt i;
    for (i = 0; i < iListCount; i++)
    {
        TBuf<GC_MAX_NAME + 1> name;
        name.Copy(TPtrC8((const TUint8*)iListItems[i].name, User::StringLength((const TUint8*)iListItems[i].name)));
        items->AppendL(name);
    }
    iList->SetItemsL(*items);
    CleanupStack::PopAndDestroy(items);
}

void CGcEngine::CloseList()
{
    /* The list may be closed from inside its own OfferKeyEventL(): hide
     * it now, delete it from the next timer tick. */
    if (iList)
    {
        iList->Dismiss();
        delete iDoomedList;
        iDoomedList = iList;
        iList = NULL;
    }
    iListCount = 0;
}

void CGcEngine::ListItemSelected(TInt aIndex)
{
    if (aIndex < 0 || aIndex >= iListCount)
    {
        return;
    }
    gcd_device dev = iListItems[aIndex];
    if (iListIsHostConfirm)
    {
        /* accept this player, keep the list for further players */
        TInt i;
        for (i = aIndex; i + 1 < iListCount; i++)
        {
            iListItems[i] = iListItems[i + 1];
        }
        iListCount--;
        gcd_v2_declare(iDev, &dev, Now());
        TRAPD(err, RefreshListL());
        (void)err;
    }
    else
    {
        CloseList();
        gcd_select_session(iDev, dev.session_id, Now());
    }
    Kick();
}

void CGcEngine::ListCancelled()
{
    CloseList();
    if (iDev->selecting)
    {
        gcd_select_session(iDev, 0, Now());
    }
    Kick();
}
