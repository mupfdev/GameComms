/** @file GameBTComms.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "GameBTComms.h"

_LIT(KGcMainPanic, "GC-MAIN");

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void Utf8ToDes(const char* aText, TDes& aOut)
{
    aOut.Copy(TPtrC8((const TUint8*)aText, User::StringLength((const TUint8*)aText)).Left(aOut.MaxLength()));
}

void GcDeviceToDetails(const gcd_device& aDev, TDeviceDetails& aDetails)
{
    /* hosts are identified by session id (< 0x80000000), clients by
     * 0xFF0000xx with xx = slot */
    TUint32 v = aDev.slot == GC_SLOT_HOST ? aDev.session_id : (0xFF000000u | aDev.slot);
    TUint8 b[6];
    b[0] = KGcAddrMagic0;
    b[1] = KGcAddrMagic1;
    b[2] = (TUint8)(v >> 24);
    b[3] = (TUint8)(v >> 16);
    b[4] = (TUint8)(v >> 8);
    b[5] = (TUint8)v;
    aDetails.iAddress = TBTSockAddr();
    aDetails.iAddress.SetBTAddr(TBTDevAddr(TPtrC8(b, 6)));
    Utf8ToDes(aDev.name, aDetails.iName);
}

void GcDetailsToDevice(const TDeviceDetails& aDetails, gcd_device& aDev)
{
    Mem::FillZ(&aDev, sizeof(aDev));
    TBTDevAddr a = aDetails.iAddress.BTAddr();
    TInt       i;
    TPtrC      name = aDetails.iName.Left(GC_MAX_NAME);
    for (i = 0; i < name.Length(); i++)
    {
        aDev.name[i] = (char)(name[i] < 0x80 ? name[i] : '?');
    }
    aDev.name[i] = '\0';
    if (a[0] == KGcAddrMagic0 && a[1] == KGcAddrMagic1)
    {
        TUint32 v = ((TUint32)a[2] << 24) | ((TUint32)a[3] << 16) | ((TUint32)a[4] << 8) | a[5];
        if ((v & 0xFF000000u) == 0xFF000000u)
        {
            aDev.slot = (gc_u8)(v & 0xFF); /* a client, as seen by the host */
        }
        else
        {
            aDev.session_id = v;           /* a host session */
            aDev.slot = GC_SLOT_HOST;
        }
    }
    else
    {
        /* a real Bluetooth device the game found by itself: the client
         * joins the first compatible session, the host matches by name */
        aDev.session_id = 0;
        aDev.slot = GC_SLOT_NONE;
    }
}

static TInt LocalName(CGcEngine* aEngine, THostName& aName)
{
    if (aEngine)
    {
        Utf8ToDes(aEngine->Dev().cfg.name, aName);
        return KErrNone;
    }
    TGcConfig cfg;
    cfg.Load();
    if (cfg.iDeviceName.Length())
    {
        aName.Copy(cfg.iDeviceName);
        return KErrNone;
    }
    RSocketServ ss;
    TInt        err = ss.Connect();
    if (err == KErrNone)
    {
        RHostResolver hr;
        TProtocolDesc pd;
        _LIT(KBTLinkManager, "BTLinkManager");
        err = ss.FindProtocol(KBTLinkManager(), pd);
        if (err == KErrNone)
        {
            err = hr.Open(ss, pd.iAddrFamily, pd.iProtocol);
        }
        if (err == KErrNone)
        {
            err = hr.GetHostName(aName);
            hr.Close();
        }
        ss.Close();
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* GameComms 1.x                                                       */
/* ------------------------------------------------------------------ */

CGameBTComms1* CGameBTComms1::NewL(MGameBTCommsNotify* aNotify, TUint32 aGameUid)
{
    CGameBTComms1* self = new (ELeave) CGameBTComms1(aNotify);
    CleanupStack::PushL(self);
    self->iEngine = CGcEngine::NewL(*self, GC_API_V1, 1, aGameUid);
    CleanupStack::Pop();
    return self;
}

CGameBTComms1::CGameBTComms1(MGameBTCommsNotify* aNotify) : iNotify(aNotify)
{
}

CGameBTComms1::~CGameBTComms1()
{
    delete iEngine;
}

void CGameBTComms1::StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
{
    gcd_v1_start_host(&iEngine->Dev(), aStartPlayers, aMinPlayers, iEngine->Now());
    iEngine->EnsureTransport();
    iEngine->Kick();
}

void CGameBTComms1::StartClientL()
{
    gcd_v1_start_client(&iEngine->Dev(), iEngine->Now());
    iEngine->EnsureTransport();
    iEngine->Kick();
}

TInt CGameBTComms1::ConnectionRole()
{
    return gcd_role(&iEngine->Dev());
}

TInt CGameBTComms1::GameState()
{
    return gcd_game_state(&iEngine->Dev());
}

TInt CGameBTComms1::ConnectState()
{
    return gcd_connect_state(&iEngine->Dev());
}

TInt CGameBTComms1::GetLocalDeviceName(THostName& aName)
{
    return LocalName(iEngine, aName);
}

void CGameBTComms1::Disconnect()
{
    gcd_v1_disconnect(&iEngine->Dev(), iEngine->Now());
    iEngine->Kick();
}

TInt CGameBTComms1::DisconnectClient(TUint16 aClientId)
{
    /* original: KErrNotReady unless host, KErrBadHandle for id 0 */
    if (gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST)
    {
        return KErrNotReady;
    }
    if (aClientId == 0)
    {
        return KErrBadHandle;
    }
    TInt r = gcd_v1_disconnect_client(&iEngine->Dev(), aClientId, iEngine->Now());
    iEngine->Kick();
    return r;
}

TInt CGameBTComms1::SendDataToClient(TUint16 aClientId, const TDesC8& aData)
{
    if (gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST)
    {
        return KErrNotReady;
    }
    return gcd_v1_send(&iEngine->Dev(), aClientId, aData.Ptr(), aData.Length(), iEngine->Now());
}

TInt CGameBTComms1::SendDataToAllClients(const TDesC8& aData)
{
    if (gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST)
    {
        return KErrNotReady;
    }
    return gcd_v1_send(&iEngine->Dev(), GC_SLOT_ALL, aData.Ptr(), aData.Length(), iEngine->Now());
}

TInt CGameBTComms1::SendDataToHost(const TDesC8& aData)
{
    if (gcd_role(&iEngine->Dev()) != GCD_ROLE_CLIENT)
    {
        return KErrNotReady;
    }
    return gcd_v1_send(&iEngine->Dev(), GC_SLOT_HOST, aData.Ptr(), aData.Length(), iEngine->Now());
}

TInt CGameBTComms1::ContinueMultiPlayerGame()
{
    if (gcd_role(&iEngine->Dev()) == GCD_ROLE_IDLE)
    {
        return KErrNotReady;
    }
    TInt r = gcd_v1_continue(&iEngine->Dev(), iEngine->Now());
    iEngine->Kick();
    return r;
}

TInt CGameBTComms1::ReconnectL(TBool aMustReconnectToAll)
{
    TInt r = gcd_v1_reconnect(&iEngine->Dev(), aMustReconnectToAll ? 1 : 0, iEngine->Now());
    iEngine->EnsureTransport();
    iEngine->Kick();
    return r;
}

TInt CGameBTComms1::PauseMultiPlayerGame()
{
    if (gcd_role(&iEngine->Dev()) == GCD_ROLE_IDLE)
    {
        return KErrNotReady;
    }
    TInt r = gcd_v1_pause(&iEngine->Dev(), iEngine->Now());
    iEngine->Kick();
    return r;
}

TInt CGameBTComms1::EndMultiPlayerGame()
{
    if (gcd_role(&iEngine->Dev()) == GCD_ROLE_IDLE)
    {
        return KErrNotReady;
    }
    TInt r = gcd_v1_end(&iEngine->Dev(), iEngine->Now());
    iEngine->Kick();
    return r;
}

TBool CGameBTComms1::IsShowingDeviceSelectDlg()
{
    return iEngine->IsShowingDialog();
}

void CGameBTComms1::HandleGcEventL(const gcd_event& aEvent)
{
    if (!iNotify)
    {
        return;
    }
    switch (aEvent.type)
    {
        case GCD_EV1_CLIENT_CONNECTED:
        {
            TBuf<GC_MAX_NAME + 1> name;
            Utf8ToDes(aEvent.dev.name, name);
            iNotify->ClientConnected((TUint16)aEvent.id, name, aEvent.err);
            break;
        }
        case GCD_EV1_HOST_SELECTED:
            iNotify->HostSelected(aEvent.err);
            break;
        case GCD_EV1_HOST_CONNECTED:
            iNotify->HostConnected(aEvent.err);
            break;
        case GCD_EV1_START_GAME:
            iNotify->StartMultiPlayerGame(aEvent.err);
            break;
        case GCD_EV1_CONTINUE_GAME:
            iNotify->ContinueMultiPlayerGame();
            break;
        case GCD_EV1_PAUSE_GAME:
            iNotify->PauseMultiPlayerGame();
            break;
        case GCD_EV1_END_GAME:
            iNotify->EndMultiPlayerGame(aEvent.err);
            break;
        case GCD_EV1_CLIENT_ENDED_GAME:
            iNotify->ConnectedClientEndedGame((TUint16)aEvent.id);
            break;
        case GCD_EV1_CLIENT_DISCONNECTED:
            iNotify->ClientDisconnected((TUint16)aEvent.id, aEvent.err);
            break;
        case GCD_EV1_HOST_DISCONNECTED:
            iNotify->HostDisconnected(aEvent.err);
            break;
        case GCD_EV1_DATA_FROM_CLIENT:
        {
            TPtrC8 data(aEvent.data, aEvent.len);
            iNotify->ReceiveDataFromClient((TUint16)aEvent.id, data);
            break;
        }
        case GCD_EV1_DATA_FROM_HOST:
        {
            TPtrC8 data(aEvent.data, aEvent.len);
            iNotify->ReceiveDataFromHost(data);
            break;
        }
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* GameComms 2.x                                                       */
/* ------------------------------------------------------------------ */

CGameBTComms2* CGameBTComms2::NewL(MGameBTCommsNotify2* aNotify, TUint32 aGameUid, TUint8 aVariant)
{
    CGameBTComms2* self = new (ELeave) CGameBTComms2(aNotify);
    CleanupStack::PushL(self);
    self->iGameUid = aGameUid;
    self->iVariant = aVariant;
    self->iDetails = new (ELeave) TDeviceDetails;
    self->EnsureEngineL();
    CleanupStack::Pop();
    return self;
}

CGameBTComms2::CGameBTComms2(MGameBTCommsNotify2* aNotify) : iNotify(aNotify)
{
}

CGameBTComms2::~CGameBTComms2()
{
    delete iEngine;
    delete iDetails;
}

void CGameBTComms2::EnsureEngineL()
{
    if (!iEngine)
    {
        iEngine = CGcEngine::NewL(*this, GC_API_V2, iVariant, iGameUid);
    }
}

void CGameBTComms2::StartHostL(TInt aShowList, CEikAppUi* aAppUi)
{
    if (aShowList && !aAppUi)
    {
        User::Panic(KGcMainPanic, 2); /* as the original */
    }
    EnsureEngineL();
    iEngine->SetAppUi(aAppUi);
    gcd_v2_start(&iEngine->Dev(), 1, aShowList && aAppUi, iEngine->Now());
    iEngine->EnsureTransport();
    iEngine->Kick();
}

void CGameBTComms2::StartClientL(TInt aShowList, CEikAppUi* aAppUi)
{
    if (aShowList && !aAppUi)
    {
        User::Panic(KGcMainPanic, 2);
    }
    EnsureEngineL();
    iEngine->SetAppUi(aAppUi);
    gcd_v2_start(&iEngine->Dev(), 0, aShowList && aAppUi, iEngine->Now());
    iEngine->EnsureTransport();
    iEngine->Kick();
}

TInt CGameBTComms2::ConnectionRole()
{
    return iEngine ? gcd_role(&iEngine->Dev()) : GCD_ROLE_IDLE;
}

TInt CGameBTComms2::ConnectState()
{
    return iEngine ? gcd_connect_state(&iEngine->Dev()) : GCD_CS_NOT_CONNECTED;
}

TInt CGameBTComms2::GetLocalDeviceName(THostName& aName)
{
    return LocalName(iEngine, aName);
}

TInt CGameBTComms2::BluetoothPowerState(TBool& aState)
{
    /* the bridge link needs Bluetooth anyway; report it as on */
    aState = ETrue;
    return KErrNone;
}

TInt CGameBTComms2::SetBluetoothPowerState(TBool /*aState*/)
{
    return KErrNone;
}

TInt CGameBTComms2::SetLocalDeviceName(const TDesC& aName)
{
    if (iEngine)
    {
        TInt i;
        TPtrC n = aName.Left(GC_MAX_NAME);
        for (i = 0; i < n.Length(); i++)
        {
            iEngine->Dev().cfg.name[i] = (char)(n[i] < 0x80 ? n[i] : '?');
        }
        iEngine->Dev().cfg.name[i] = '\0';
    }
    return KErrNone;
}

TInt CGameBTComms2::DeclareRemoteDevice(const TDeviceDetails& aDevice)
{
    if (!iEngine || gcd_role(&iEngine->Dev()) == GCD_ROLE_IDLE)
    {
        return KErrNotReady;
    }
    gcd_device dev;
    GcDetailsToDevice(aDevice, dev);
    TInt r = gcd_v2_declare(&iEngine->Dev(), &dev, iEngine->Now());
    iEngine->Kick();
    return r;
}

TInt CGameBTComms2::Connect()
{
    if (!iEngine || gcd_role(&iEngine->Dev()) == GCD_ROLE_IDLE)
    {
        return KErrNotReady;
    }
    TInt r = gcd_v2_connect(&iEngine->Dev(), GCD_ALL, iEngine->Now());
    iEngine->EnsureTransport();
    iEngine->Kick();
    return r;
}

TInt CGameBTComms2::Connect(TUint16 aClientId)
{
    if (!iEngine || gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST)
    {
        User::Panic(KGcMainPanic, 1); /* original: host only */
    }
    TInt r = gcd_v2_connect(&iEngine->Dev(), aClientId, iEngine->Now());
    iEngine->Kick();
    return r;
}

void CGameBTComms2::Disconnect()
{
    if (!iEngine)
    {
        return;
    }
    gcd_v2_disconnect(&iEngine->Dev(), GCD_ALL, 0, iEngine->Now());
    iEngine->Kick();
}

TInt CGameBTComms2::Disconnect(TUint16 aClientId, TBool aPermanently)
{
    if (!iEngine || gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST || aClientId == 0)
    {
        User::Panic(KGcMainPanic, 1); /* original: host only, id != 0 */
    }
    TInt r = gcd_v2_disconnect(&iEngine->Dev(), aClientId, aPermanently ? 1 : 0, iEngine->Now());
    iEngine->Kick();
    return r;
}

TInt CGameBTComms2::SendData(const TDesC8& aData)
{
    if (!iEngine)
    {
        User::Panic(KGcMainPanic, 0);
    }
    /* client: to the host; host: to every connected client */
    return gcd_v2_send(&iEngine->Dev(), GCD_ALL, aData.Ptr(), aData.Length(), iEngine->Now());
}

TInt CGameBTComms2::SendData(TUint16 aClientId, const TDesC8& aData)
{
    if (!iEngine || gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST || aClientId == 0)
    {
        User::Panic(KGcMainPanic, 1);
    }
    return gcd_v2_send(&iEngine->Dev(), aClientId, aData.Ptr(), aData.Length(), iEngine->Now());
}

TInt CGameBTComms2::SetHostAcceptMode(TInt aMode)
{
    if (!iEngine || gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST)
    {
        User::Panic(KGcMainPanic, 1);
    }
    TInt r = gcd_v2_set_accept_mode(&iEngine->Dev(), aMode, iEngine->Now());
    iEngine->Kick();
    return r;
}

TInt CGameBTComms2::HostAcceptMode()
{
    if (!iEngine || gcd_role(&iEngine->Dev()) != GCD_ROLE_HOST)
    {
        User::Panic(KGcMainPanic, 1);
    }
    return gcd_v2_accept_mode(&iEngine->Dev());
}

void CGameBTComms2::EndSession()
{
    /* v10/v11: drops the role object; the engine stays for re-use */
    if (iEngine)
    {
        gcd_end_session(&iEngine->Dev(), iEngine->Now());
    }
}

TInt CGameBTComms2::SetDiscoverabilityModeLimited(TBool /*aLimited*/)
{
    return KErrNone; /* no Bluetooth discoverability involved any more */
}

void CGameBTComms2::HandleGcEventL(const gcd_event& aEvent)
{
    if (!iNotify)
    {
        return;
    }
    switch (aEvent.type)
    {
        case GCD_EV2_CONFIRM_DEVICE:
            GcDeviceToDetails(aEvent.dev, *iDetails);
            iNotify->ConfirmDevice(*iDetails);
            break;
        case GCD_EV2_DEVICE_DECLARED:
            GcDeviceToDetails(aEvent.dev, *iDetails);
            iNotify->DeviceDeclared(aEvent.id, *iDetails, aEvent.err);
            break;
        case GCD_EV2_CONNECTED:
            iNotify->Connected(aEvent.id, aEvent.err);
            break;
        case GCD_EV2_DISCONNECTED:
            iNotify->Disconnected(aEvent.id, aEvent.err);
            break;
        case GCD_EV2_DATA:
        {
            TPtrC8 data(aEvent.data, aEvent.len);
            iNotify->ReceiveData(aEvent.id, data);
            break;
        }
        default:
            break;
    }
}
