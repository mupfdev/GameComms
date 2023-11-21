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

_LIT8(KHost, "\x00");
_LIT8(KClient1, "\x01");
_LIT8(KClient2, "\x02");
_LIT8(KClient3, "\x03");
_LIT8(KNewLine, "\n");

GLDEF_C TInt E32Dll(TDllReason /*aReason*/)
{
    return(KErrNone);
}

EXPORT_C CGameBTComms *CGameBTComms::NewL(MGameBTCommsNotify *aEventHandler, TUint32 aGameUID, RSGEDebugLog *aLog = NULL)
{
    CGameBTComms *pCGameBTComms = new CGameBTComms;

    CleanupStack::PushL(pCGameBTComms);
    pCGameBTComms->ConstructL(aEventHandler, aGameUID, aLog);
    CleanupStack::Pop();

    return pCGameBTComms;
}

CGameBTComms::CGameBTComms()
{
}

EXPORT_C CGameBTComms::~CGameBTComms()
{
    if (iClient->IsConnected())
    {
        iClient->DisconnectL();
    }
}

EXPORT_C void CGameBTComms::StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
{
    iStartPlayers       = aStartPlayers;
    iMinPlayers         = aMinPlayers;
    iConnectionRoleTemp = EHost;

    Update();
}

EXPORT_C void CGameBTComms::StartClientL()
{
    iConnectionRoleTemp = EClient;

    iNotify->HostSelected(KErrNone);
    iConnectState = EConnecting;

    iNotify->HostConnected(KErrNone);
    iConnectState = EConnected;

    Update();
}

EXPORT_C CGameBTComms::TConnectionRole CGameBTComms::ConnectionRole()
{
    Update();

    return iConnectionRole;
}

EXPORT_C CGameBTComms::TGameState CGameBTComms::GameState()
{
    Update();

    return iGameState;
}

EXPORT_C CGameBTComms::TConnectState CGameBTComms::ConnectState()
{
    Update();

    return iConnectState;
}

EXPORT_C TInt CGameBTComms::GetLocalDeviceName(THostName &aHostName)
{
    TInt aError = KErrNone;

    char device_name[32] = { 0 };

    ini_gets("Config", "DeviceName", "bosley", (char *)device_name, 32, IniFile);

    aHostName.Copy(TPtrC8((const TText8 *)device_name));

    Update();

    return aError;
}

EXPORT_C void CGameBTComms::Disconnect()
{
    Update();
}

EXPORT_C TInt CGameBTComms::DisconnectClient(TUint16 aClientId)
{
    TInt aError = KErrNone;

    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToClient(TUint16 aClientId, TDesC8 &aData)
{
    TInt aError = KErrNone;

    Update(aClientId, (char *)aData.Ptr(), aData.Length(), __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToAllClients(TDesC8 &aData)
{
    TInt aError = KErrNone;

    Update(0xff, (char *)aData.Ptr(), aData.Length(), __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToHost(TDesC8 &aData)
{
    TInt aError = KErrNone;

    Update(0, (char*)aData.Ptr(), aData.Length(), __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::ContinueMultiPlayerGame()
{
    TInt aError = KErrNone;

    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::ReconnectL(TBool aMustReconnectToAll = ETrue)
{
    TInt aError = KErrNone;

    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::PauseMultiPlayerGame()
{
    TInt aError = KErrNone;

    Update();

    return aError;
}

EXPORT_C TInt CGameBTComms::EndMultiPlayerGame()
{
    TInt aError = KErrNone;

    Update();

    return aError;
}

EXPORT_C TBool CGameBTComms::IsShowingDeviceSelectDlg()
{
    TBool aState = EFalse;

    Update();

    return aState;
}

void CGameBTComms::Update(TUint16 aClientId = 0, char *aData = NULL, TUint16 aLength = 0, char *sDebug = NULL)
{
    char buffer[512] = { 0 };

    if (iClient->IsReadyToSendMessage() == EFalse)
    {
        if (aLength > 0)
        {
            if (sDebug != NULL)
            {
                DebugLog(LOG, "%s: ", sDebug);
            }
            DebugLog(LOG, "Data sent but not yet ready to send messages. We need a queue!\n");
        }

        return;
    }

    /* Handle outgoing messages first. */
    if (aLength > 0)
    {
        for (int aIndex = 0; aIndex < aLength; aIndex += 1)
        {
            sprintf(buffer, (const char *)"%u %u: %02X ", aClientId, aLength, aData[aIndex]);

            DebugLog(LOG, "Data: %s\n", buffer);
            iClient->SendMessageL(TPtrC8((const TText8 *)buffer));
            iClient->SendMessageL(KNewLine);
        }
    }

    switch (iGameCommsState)
    {
        case EInit:
        {
            if (iClient->IsReadyToSendMessage() && aLength == 0)
            {
                iGameCommsState = ERegisterUID;
            }
            break;
        }
        case ERegisterUID:
        {
            sprintf(buffer, (const char *)"UID:0x%8X\n", (unsigned int)iGameUID);
            iClient->SendMessageL(TPtrC8((const TText8 *)buffer));
            iGameCommsState = ERegisterDeviceName;
            break;
        }
        case ERegisterDeviceName:
        {
            if (aLength == 0)
            {
                char device_name[32] = { 0 };

                ini_gets("Config", "DeviceName", "bosley", (char *)device_name, 32, IniFile);

                sprintf(buffer, (const char *)"DID:%s\n", device_name);

                iClient->SendMessageL(TPtrC8((const TText8 *)buffer));
                iGameCommsState = ERegisterNetConfig;
            }
            break;
        }
        case ERegisterNetConfig:
        {
            if (aLength == 0)
            {
                char host[32] = { 0 };
                long port = ini_getbool("Network", "Port", 8889, IniFile);

                ini_gets("Network", "Host", "localhost", (char *)host, 32, IniFile);

                sprintf(buffer, (const char *)"NET:%s:%u\n", host, (unsigned short)port);

                iClient->SendMessageL(TPtrC8((const TText8 *)buffer));
                iGameCommsState = ERegisterRole;
            }
            break;
        }
        case ERegisterRole:
            if (aLength == 0)
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

                sprintf(buffer, (const char *)"ROL:%c\n", role);

                iClient->SendMessageL(TPtrC8((const TText8 *)buffer));

                iNotify->StartMultiPlayerGame(KErrNone);

                iGameState      = EPlay;
                iGameCommsState = EHandleMessages;
            }
            break;
        case EHandleMessages:
            iConnectionRole = iConnectionRoleTemp; /* Asign selected connection role */

            /* Handle incoming messages. */
            iClient->PollMessagesL(iRecvBuffer, iRecvLength);

            if (iRecvLength > 0)
            {
                char aCommand = iRecvBuffer[0];
                char aPayload = iRecvBuffer[1];
                char aNewLine = iRecvBuffer[2];

                if (iConnectionRole == EClient && iGameState == EPlay)
                {
                    if (aNewLine == '\n')
                    {
                        switch (aCommand)
                        {
                            case 0x00: // Host
                            case 0x42: // Broadcast
                            {
                                TDesC8 Data = TPtrC8((TUint8 *)aPayload);
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
            break;
    }
}

void CGameBTComms::ConstructL(MGameBTCommsNotify *aEventHandler, TUint32 aGameUID, RSGEDebugLog *aLog)
{
    iNotify             = aEventHandler;
    iGameUID            = aGameUID;
    iLog                = aLog;
    iConnectionRole     = EIdle;
    iConnectionRoleTemp = EIdle;
    iConnectState       = ENotConnected;
    iGameState          = EGameOver;
    iClient             = CMessageClient::NewL();
    iRecvLength         = 0;

    if (iClient)
    {
        iClient->ConnectL();
    }
}
