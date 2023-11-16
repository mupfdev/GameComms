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
#include "MessageClient.h"

extern "C"
{
#include "DebugLog.h"
}

#include "minIni.h"

const char IniFile[] = "E:\\GameComms.ini";

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
    Update();

    if (iClient->IsConnected())
    {
        iClient->DisconnectL();
    }
}

EXPORT_C void CGameBTComms::StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    iStartPlayers   = aStartPlayers;
    iMinPlayers     = aMinPlayers;
    iConnectionRole = EHost;
}

EXPORT_C void CGameBTComms::StartClientL()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    iConnectionRole = EClient;
}

EXPORT_C CGameBTComms::TConnectionRole CGameBTComms::ConnectionRole()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return iConnectionRole;
}

EXPORT_C CGameBTComms::TGameState CGameBTComms::GameState()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return iGameState;
}

EXPORT_C CGameBTComms::TConnectState CGameBTComms::ConnectState()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return iConnectState;
}

EXPORT_C TInt CGameBTComms::GetLocalDeviceName(THostName& aHostName)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    char device_name[32] = { 0 };

    ini_gets("Config", "DeviceName", "bosley", (char*)device_name, 32, IniFile);

    aHostName.Copy(TPtrC8((const TText8*)device_name));

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
    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToClient(TUint16 aClientId, TDesC8& aData)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "SendDataToClient(%u, %p)\n", aClientId, aData);
    Update();

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
    Update();

    if (iClient->IsConnected())
    {
        if (iClient->IsReadyToSendMessage())
        {
            iClient->SendMessageL(aData);
        }
    }

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToHost(TDesC8& aData)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    if (iClient->IsConnected())
    {
        if (iClient->IsReadyToSendMessage())
        {
            iClient->SendMessageL(aData);
        }
    }

    return aError;
}

EXPORT_C TInt CGameBTComms::ContinueMultiPlayerGame()
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::ReconnectL(TBool aMustReconnectToAll = ETrue)
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::PauseMultiPlayerGame()
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::EndMultiPlayerGame()
{
    TInt aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return aError;
}

EXPORT_C TBool CGameBTComms::IsShowingDeviceSelectDlg()
{
    TBool aState = EFalse;

    DebugLog(LOG, "%s\n", __FUNCTION__);
    Update();

    return aState;
}

void CGameBTComms::Update()
{
    switch (iGameCommsState)
    {
        case EInit:
        {
            if (iClient->IsConnected())
            {
                if (iClient->IsReadyToSendMessage())
                {
                    iGameCommsState = ERegister;
                }
            }
            break;
        }
        case ERegister:
        {
            if (iClient->IsConnected())
            {
                if (iClient->IsReadyToSendMessage())
                {
                    /* Send registration information to server once. */

                    /* Game UID */

                    /* Device Name */

                    /* Host or Client? */
                }
            }
            iGameCommsState = EActive;
            break;
        }
        case EActive:
            break;
    }
}

void CGameBTComms::ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog)
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    iNotify         = aEventHandler;
    iGameUID        = aGameUID;
    iLog            = aLog;
    iConnectionRole = EIdle;
    iConnectState   = ENotConnected;
    iGameState      = EGameOver;
    iClient         = CMessageClient::NewL();

    if (iClient)
    {
        DebugLog(LOG, "%s: Connect Message Client\n", __FUNCTION__);
        iClient->ConnectL();
    }
}
