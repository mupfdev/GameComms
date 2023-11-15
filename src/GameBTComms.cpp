/** @file GameBTComms.cpp
 *
 *  Class that provides games with Bluetooth comms.
 *
 *  Note that it is the game's responsibility to provide the UI to
 *  accompany this (with the exception of the BT device selection UI)
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <e32def.h>
#include <e32std.h>
#include <stdarg.h>
#include <stdio.h>
#include "GameBTComms.h"
#include "GameBTCommsNotify.h"
#include "MessageServer.h"
#include "MessageClient.h"

extern "C"
{
#include "DebugLog.h"
}

#define LOG "E:\\GameBTComms.txt"

GLDEF_C TInt E32Dll(TDllReason /*aReason*/)
{
    return(KErrNone);
}

EXPORT_C CGameBTComms* CGameBTComms::NewL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog = NULL)
{
    CGameBTComms* pCGameBTComms = new CGameBTComms;

    DebugLog(LOG, "NewL(%p, 0x%8x, %p)\n", aEventHandler, aGameUID, aLog);

    CleanupStack::PushL(pCGameBTComms);
    pCGameBTComms->ConstructL(aEventHandler, aGameUID, aLog);
    CleanupStack::Pop();

    return pCGameBTComms;
}

CGameBTComms::CGameBTComms()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
}

EXPORT_C CGameBTComms::~CGameBTComms()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    if (iServer)
    {
        iServer->StopL();
    }

    delete iServer;
    iServer = NULL;

    delete iClient;
    iClient = NULL;

    delete iNotify;
    iNotify = NULL;

    delete iNotify;
    iLog = NULL;
}

EXPORT_C void CGameBTComms::StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
}

EXPORT_C void CGameBTComms::StartClientL()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
}

EXPORT_C CGameBTComms::TConnectionRole CGameBTComms::ConnectionRole()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aConnectionRole;
}

EXPORT_C CGameBTComms::TGameState CGameBTComms::GameState()
{
    TGameState aState = EPlay;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aState;
}

EXPORT_C CGameBTComms::TConnectState CGameBTComms::ConnectState()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aConnectState;
}

EXPORT_C TInt CGameBTComms::GetLocalDeviceName(THostName& aHostName)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C void CGameBTComms::Disconnect()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
}

EXPORT_C TInt CGameBTComms::DisconnectClient(TUint16 aClientId)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToClient(TUint16 aClientId, TDesC8& aData)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "SendDataToClient(%u, %p)\n", aClientId, aData);

    if (iClient->IsConnected())
    {
        if (iClient->IsReadyToSendMessage())
        {
            iClient->SendMessageL(aData);
        }
    }

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToAllClients(TDesC8& aData)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToHost(TDesC8& aData)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::ContinueMultiPlayerGame()
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::ReconnectL(TBool aMustReconnectToAll = ETrue)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::PauseMultiPlayerGame()
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::EndMultiPlayerGame()
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aError;
}

EXPORT_C TBool CGameBTComms::IsShowingDeviceSelectDlg()
{
    TBool aState = EFalse;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    return aState;
}

void CGameBTComms::ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog)
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    iNotify         = aEventHandler;
    iGameUID        = aGameUID;
    iLog            = aLog;
    aConnectionRole = EIdle;
    aConnectState   = EConnecting;
    iServer         = CMessageServer::NewL();
    iClient         = CMessageClient::NewL();

    if (iServer)
    {
        DebugLog(LOG, "%s: Start Message Server\n", __FUNCTION__);
        iServer->StartL();
    }

    if (iClient)
    {
        DebugLog(LOG, "%s: Connect Message Client\n", __FUNCTION__);
        iClient->ConnectL();
    }
}
