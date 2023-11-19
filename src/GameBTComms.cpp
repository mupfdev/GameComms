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

extern "C"
{
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#undef NULL
}

#include <e32def.h>
#include <e32std.h>

#include "GameBTComms.h"
#include "GameBTCommsNotify.h"
#include "MessageClient.h"
#include "DebugLog.h"

#include "minIni.h"

const char IniFile[] = "E:\\GameComms.ini";

#define LOG "E:\\GameBTComms.txt"

_LIT8(KHost,    "\x00");
_LIT8(KClient1, "\x01");
_LIT8(KClient2, "\x02");
_LIT8(KClient3, "\x03");
_LIT8(KNewLine, "\n");

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

    if (iClient->IsConnected())
    {
        iClient->DisconnectL();
    }
}

EXPORT_C void CGameBTComms::StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    iStartPlayers       = aStartPlayers;
    iMinPlayers         = aMinPlayers;
    iConnectionRoleTemp = EHost;

    Update();
}

EXPORT_C void CGameBTComms::StartClientL()
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    iConnectionRoleTemp = EClient;

    iNotify->HostSelected(KErrNone);
    iConnectState = EConnecting;

    iNotify->HostConnected(KErrNone);
    iConnectState = EConnected;

    Update();
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

    char device_name[32] = { 0 };

    ini_gets("Config", "DeviceName", "bosley", (char*)device_name, 32, IniFile);

    aHostName.Copy(TPtrC8((const TText8*)device_name));

    Update();

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

    switch (aClientId)
    {
        case 1:
        {

            break;
        }
        case 2:
        {

            break;
        }
        case 3:
        {

            break;
        }
        default:
            break;
    }

    DebugLog(LOG, "SendDataToClient(%u, 0x%X)\n", aClientId, aData);

    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToAllClients(TDesC8& aData)
{
    TInt  aError = KErrNone;

    DebugLog(LOG, "%s\n", __FUNCTION__);

    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToHost(TDesC8& aData)
{
    TInt  aError = KErrNone;
    TPtr8 aPtr   = iToHostQueue.Des();

    DebugLog(LOG, "%s\n", __FUNCTION__);

    /* Todo: How to properly queue and send data? */
    aPtr.Append(aData);

    Update();

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
    char buffer[512] = { 0 };

    switch (iGameCommsState)
    {
        case EInit:
        {
            if (iClient->IsConnected())
            {
                if (iClient->IsReadyToSendMessage())
                {
                    iGameCommsState = ERegisterUID;
                }
            }
            break;
        }
        case ERegisterUID:
        {
            if (iClient->IsConnected() && iClient->IsReadyToSendMessage())
            {
                sprintf(buffer, (const char*)"UID:0x%8X\n", (unsigned int)iGameUID);
                iClient->SendMessageL(TPtrC8((const TText8*)buffer));
                iGameCommsState = ERegisterDeviceName;
            }
            break;
        }
        case ERegisterDeviceName:
        {
            if (iClient->IsConnected() && iClient->IsReadyToSendMessage())
            {
                char device_name[32] = { 0 };

                ini_gets("Config", "DeviceName", "bosley", (char*)device_name, 32, IniFile);

                sprintf(buffer, (const char*)"DID:%s\n", device_name);
                iClient->SendMessageL(TPtrC8((const TText8*)buffer));
                iGameCommsState = ERegisterNetConfig;
            }
            break;
        }
        case ERegisterNetConfig:
        {
            if (iClient->IsConnected() && iClient->IsReadyToSendMessage())
            {
                char host[32] = { 0 };
                long port     = ini_getbool("Network", "Port", 8889, IniFile);

                ini_gets("Network", "Host", "localhost", (char*)host, 32, IniFile);

                sprintf(buffer, (const char*)"NET:%s:%u\n", host, (unsigned short)port);
                iClient->SendMessageL(TPtrC8((const TText8*)buffer));
                iGameCommsState = ERegisterRole;
            }
            break;
        }
        case ERegisterRole:
            if (iClient->IsConnected() && iClient->IsReadyToSendMessage())
            {
                char role;

                if (iConnectionRoleTemp == EHost)
                {
                    role = 'H';
                }
                else
                {
                    role = 'C';
                }

                sprintf(buffer, (const char*)"ROL:%c\n", role);
                iClient->SendMessageL(TPtrC8((const TText8*)buffer));

                iNotify->StartMultiPlayerGame(KErrNone);

                iGameState      = EPlay;
                iGameCommsState = EHandleMessages;
            }
            break;
        case EHandleMessages:
            iConnectionRole = iConnectionRoleTemp; /* Asign selected connection role */

            iClient->PollMessagesL(iRecvBuffer, iRecvLength);

            /* Handle incoming messages. */
            if (iGameState == EPlay)
            {
                if (iRecvLength > 0)
                {
                    char aCommand = iRecvBuffer[0];
                    char aPayload = iRecvBuffer[1];
                    char aNewLine = iRecvBuffer[2];

                    DebugLog(LOG, "%s", iRecvBuffer);

                    if (iConnectionRole == EClient)
                    {
                        if (aNewLine == '\n')
                        {
                            switch (aCommand)
                            {
                            case 0x00: // Host
                            case 0x42: // Broadcast
                            {
                                TDesC8 Data = TPtrC8((TUint8*)aPayload);
                                iNotify->ReceiveDataFromHost(Data);
                                break;
                            }
                            case 0x44: // Disconnect
                            {
                                break;
                            }
                            case 0x50: // Pause Game
                            {
                                iNotify->PauseMultiPlayerGame();
                                break;
                            }
                            case 0x52: // Resume Game
                                iNotify->ContinueMultiPlayerGame();
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    else if (iConnectionRole == EHost)
                    {
                        /* Todo. */
                    }
                    memcpy(iRecvBuffer, 0, 512);
                    iRecvLength = 0;
                }
            }
            break;
    }
}

void CGameBTComms::ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog)
{
    DebugLog(LOG, "%s\n", __FUNCTION__);

    iNotify               = aEventHandler;
    iGameUID              = aGameUID;
    iLog                  = aLog;
    iConnectionRole       = EIdle;
    iConnectionRoleTemp   = EIdle;
    iConnectState         = ENotConnected;
    iGameState            = EGameOver;
    iClient               = CMessageClient::NewL();
    iRecvLength           = 0;

    iToHostQueue.AllocL();

    if (iClient)
    {
        DebugLog(LOG, "%s: Connect Message Client\n", __FUNCTION__);
        iClient->ConnectL();
    }
}
