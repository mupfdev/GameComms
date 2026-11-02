/** @file GcTransport.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "GcTransport.h"
#include "MessageServiceSearcher.h"

_LIT(KRfcomm, "RFCOMM");

const TInt KGcWriteBufferSize = 16384;

/* ------------------------------------------------------------------ */
/* writer: one outstanding RSocket::Write, everything else buffered    */
/* ------------------------------------------------------------------ */

class CGcWriter : public CActive
{
public:
    CGcWriter(RSocket& aSocket) : CActive(EPriorityStandard), iSocket(aSocket)
    {
        CActiveScheduler::Add(this);
    }
    ~CGcWriter()
    {
        Cancel();
        delete iPending;
    }
    void ConstructL()
    {
        iPending = HBufC8::NewL(KGcWriteBufferSize);
    }
    TInt Write(const TDesC8& aData)
    {
        TPtr8 p = iPending->Des();
        if (p.Length() + aData.Length() > p.MaxLength())
        {
            return KErrOverflow;
        }
        p.Append(aData);
        Kick();
        return KErrNone;
    }
    void Reset()
    {
        Cancel();
        iPending->Des().Zero();
        iInFlight.Zero();
    }

protected:
    void RunL()
    {
        iInFlight.Zero();
        if (iStatus.Int() != KErrNone)
        {
            iPending->Des().Zero(); /* reader reports the error */
            return;
        }
        Kick();
    }
    void DoCancel()
    {
        iSocket.CancelWrite();
    }

private:
    void Kick()
    {
        if (IsActive() || iPending->Length() == 0)
        {
            return;
        }
        TPtr8 p = iPending->Des();
        TInt  n = p.Length() < iInFlight.MaxLength() ? p.Length() : iInFlight.MaxLength();
        iInFlight.Copy(p.Left(n));
        p.Delete(0, n);
        iSocket.Write(iInFlight, iStatus);
        SetActive();
    }

    RSocket&    iSocket;
    HBufC8*     iPending;
    TBuf8<1024> iInFlight;
};

/* ------------------------------------------------------------------ */

CGcTransport* CGcTransport::NewL(MGcTransportObserver& aObserver, TGcConfig& aConfig)
{
    CGcTransport* self = new (ELeave) CGcTransport(aObserver, aConfig);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}

CGcTransport::CGcTransport(MGcTransportObserver& aObserver, TGcConfig& aConfig)
    : CActive(EPriorityStandard), iObserver(aObserver), iConfig(aConfig), iState(EIdle)
{
    CActiveScheduler::Add(this);
}

void CGcTransport::ConstructL()
{
    User::LeaveIfError(iSocketServ.Connect());
    iSearcher = CMessageServiceSearcher::NewL();
    iWriter = new (ELeave) CGcWriter(iSocket);
    iWriter->ConstructL();
}

CGcTransport::~CGcTransport()
{
    Disconnect();
    delete iWriter;
    delete iSearcher;
    iSocketServ.Close();
}

void CGcTransport::ConnectL()
{
    if (iState != EIdle)
    {
        return;
    }
    if (iConfig.iHasBridge)
    {
        iObserver.TransportProgress(_L8("using saved bridge address, channel"), iConfig.iChannel);
        iSearcher->SetDeviceAddress(iConfig.iBridge);
        if (iConfig.iChannel > 0)
        {
            ConnectSocketL(iConfig.iChannel);
            return;
        }
        iObserver.TransportProgress(_L8("searching serial port service"), 0);
        iState = ESearching;
        iSearcher->FindServiceL(iStatus);
        SetActive();
        return;
    }
    iObserver.TransportProgress(_L8("no saved bridge, showing device selection"), 0);
    iState = ESelecting;
    iObserver.TransportSelecting(ETrue);
    iSearcher->SelectDeviceByDiscoveryL(iStatus);
    SetActive();
}

void CGcTransport::ConnectSocketL(TInt aChannel)
{
    if (iSocketOpen)
    {
        iSocket.Close();
        iSocketOpen = EFalse;
    }
    User::LeaveIfError(iSocket.Open(iSocketServ, KRfcomm));
    iSocketOpen = ETrue;
    TBTSockAddr addr;
    addr.SetBTAddr(iSearcher->BTDevAddr());
    addr.SetPort(aChannel);
    iObserver.TransportProgress(_L8("connecting RFCOMM channel"), aChannel);
    iState = EConnecting;
    iSocket.Connect(addr, iStatus);
    SetActive();
}

void CGcTransport::RunL()
{
    TInt err = iStatus.Int();
    switch (iState)
    {
        case ESelecting:
            iObserver.TransportSelecting(EFalse);
            if (err != KErrNone)
            {
                Fail(err);
                return;
            }
            iObserver.TransportProgress(_L8("device selected, address saved"), 0);
            iConfig.SaveBridgeAddress(iSearcher->BTDevAddr());
            if (iConfig.iChannel > 0)
            {
                ConnectSocketL(iConfig.iChannel);
                return;
            }
            iObserver.TransportProgress(_L8("searching serial port service"), 0);
            iState = ESearching;
            iSearcher->FindServiceL(iStatus);
            SetActive();
            return;

        case ESearching:
            if (err != KErrNone)
            {
                Fail(err);
                return;
            }
            ConnectSocketL(iSearcher->Port());
            return;

        case EConnecting:
            if (err != KErrNone)
            {
                Fail(err);
                return;
            }
            iState = EConnected;
            iObserver.TransportUp();
            iSocket.RecvOneOrMore(iReadBuf, 0, iStatus, iReadLen);
            SetActive();
            return;

        case EConnected:
            if (err != KErrNone)
            {
                Fail(err);
                return;
            }
            iObserver.TransportData(iReadBuf);
            if (iState == EConnected) /* observer may have disconnected us */
            {
                iSocket.RecvOneOrMore(iReadBuf, 0, iStatus, iReadLen);
                SetActive();
            }
            return;

        default:
            return;
    }
}

TInt CGcTransport::RunError(TInt aError)
{
    Fail(aError);
    return KErrNone;
}

void CGcTransport::DoCancel()
{
    switch (iState)
    {
        case ESelecting:
            iSearcher->CancelSelection();
            break;
        case EConnecting:
            iSocket.CancelConnect();
            break;
        case EConnected:
            iSocket.CancelRead();
            break;
        default:
            break;
    }
}

void CGcTransport::Fail(TInt aError)
{
    TBool wasUp = iState == EConnected;
    iObserver.TransportProgress(_L8("transport step failed, state"), iState);
    iWriter->Reset();
    if (iSocketOpen)
    {
        iSocket.Close();
        iSocketOpen = EFalse;
    }
    iState = EIdle;
    (void)wasUp;
    iObserver.TransportDown(aError);
}

void CGcTransport::Disconnect()
{
    Cancel();
    iWriter->Reset();
    if (iSocketOpen)
    {
        iSocket.Close();
        iSocketOpen = EFalse;
    }
    iState = EIdle;
}

TInt CGcTransport::Write(const TDesC8& aData)
{
    if (iState != EConnected)
    {
        return KErrNotReady;
    }
    return iWriter->Write(aData);
}
