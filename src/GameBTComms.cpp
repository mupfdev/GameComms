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
#include "GameBTComms.h"

GLDEF_C TInt E32Dll(TDllReason /*aReason*/)
{
    return(KErrNone);
}

EXPORT_C CGameBTComms* CGameBTComms::NewL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog = NULL)
{
    CGameBTComms* pCGameBTComms = new CGameBTComms;
    CleanupStack::PushL(pCGameBTComms);
    pCGameBTComms->ConstructL(aEventHandler, aGameUID, aLog);
    CleanupStack::Pop();
    return pCGameBTComms;
}

CGameBTComms::CGameBTComms()
{
    iNotify  = NULL;
    iGameUID = 0;
    iLog     = NULL;
    iComms   = NULL;
}

EXPORT_C CGameBTComms::~CGameBTComms()
{
}

EXPORT_C void CGameBTComms::StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
{
}

EXPORT_C void CGameBTComms::StartClientL()
{
}

EXPORT_C CGameBTComms::TConnectionRole CGameBTComms::ConnectionRole()
{
    TConnectionRole aRole = EIdle;

    return aRole;
}

EXPORT_C CGameBTComms::TGameState CGameBTComms::GameState()
{
    TGameState aState = EPlay;

    return aState;
}

EXPORT_C CGameBTComms::TConnectState CGameBTComms::ConnectState()
{
    TConnectState aState = EConnected;

    return aState;
}

EXPORT_C TInt CGameBTComms::GetLocalDeviceName(THostName& aHostName)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C void CGameBTComms::Disconnect()
{
}

EXPORT_C TInt CGameBTComms::DisconnectClient(TUint16 aClientId)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToClient(TUint16 aClientId, TDesC8& aData)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToAllClients(TDesC8& aData)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToHost(TDesC8& aData)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::ContinueMultiPlayerGame()
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::ReconnectL(TBool aMustReconnectToAll = ETrue)
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::PauseMultiPlayerGame()
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TInt CGameBTComms::EndMultiPlayerGame()
{
    TInt aError = KErrNone;

    return aError;
}

EXPORT_C TBool CGameBTComms::IsShowingDeviceSelectDlg()
{
    TBool aState = EFalse;

    return aState;
}

void CGameBTComms::ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog)
{
}
