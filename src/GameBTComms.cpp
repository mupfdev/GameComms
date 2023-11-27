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
#include <stdlib.h>
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

    Update(EToAll, (char *)aData.Ptr(), aData.Length(), __FUNCTION__);

    return aError;
}

EXPORT_C TInt CGameBTComms::SendDataToHost(TDesC8 &aData)
{
    TInt aError = KErrNone;

    Update(EToHost, (char *)aData.Ptr(), aData.Length());

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

void CGameBTComms::Update(TUint16 aClientId = EInvalid, const char *aData = NULL, TUint16 aLength = 0, const char *sDebug = NULL)
{
    char    buffer[512] = { 0 };
    TUint16 offset = 0;

    if ((aLength > 0) && (aClientId != EInvalid))
    {
        TUint8 pos                      = iMessageQueue[aClientId - 1].Pos;
        TUint8 message[KMaxMessageSize] = { 0 };


        if (pos >= KMaxQueueSize)
        {
            DebugLog(LOG, "Error: queue %u full.\n", pos);
            return;
        }

        if (aLength >= KMaxMessageSize - 3)
        {
            DebugLog(LOG, "Error: message to big. Increase queue size.\n");
        }

        message[0] = aClientId;
        message[1] = aLength;
        for (TInt index = 2; index < aLength + 2; index += 1)
        {
            message[index] = aData[index - 2];
        }
        message[aLength + 2] = '\n';

        memcpy(&iMessageQueue[aClientId - 1].Queue[pos], message, aLength + 3);
        iMessageQueue[aClientId - 1].Length[pos]  = aLength + 3;
        iMessageQueue[aClientId - 1].Pos         += 1;
    }

    if (iClient->IsReadyToSendMessage() == EFalse)
    {
        return;
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
            sprintf(buffer, (const char *)"UID:0x%08X\n", (unsigned int)iGameUID);
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

            /* Handle pending messages. */
            for (TInt queueIndex = 0; queueIndex < EToAll; queueIndex += 1)
            {
                for (TInt msgIndex = 0; msgIndex < iMessageQueue[queueIndex].Pos; msgIndex += 1)
                {
                    TUint8 *queue  = iMessageQueue[queueIndex].Queue[msgIndex];
                    TUint8  length = iMessageQueue[queueIndex].Length[msgIndex];

                    if (length > 0)
                    {
                        for (TInt bufIndex = 0; bufIndex < length; bufIndex += 1)
                        {
                            buffer[offset] = queue[bufIndex];
                            offset += 1;
                        }
                    }
                }
                iMessageQueue[queueIndex].Pos = 0;
            }

            if (offset > 1)
            {
                iClient->SendMessageL(TPtrC8((const TUint8 *)buffer, offset));
            }

            /* Handle incoming messages. */
            iClient->PollMessagesL(iRecvBuffer, iRecvLength);
            /* Tbd. */

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

    for (TInt aIndex = 0; aIndex <= EToAll; aIndex += 1)
    {
        memset(&iMessageQueue[aIndex], 0, sizeof(TMessageQueue));
    }

    if (iClient)
    {
        iClient->ConnectL();
    }
}
