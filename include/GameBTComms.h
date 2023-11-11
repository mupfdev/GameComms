/** @file GameBTComms.h
 *
 *  A GameComms re-implementation for the Nokia N-Gage.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef __GAMEBTCOMMS_H
#define __GAMEBTCOMMS_H

#include <e32std.h>
#include "GameBTCommsConsts.h"

class MGameBTCommsNotify;
class RSGEDebugLog;
class CGameBTBase;

struct TBTCommsMsgBase;

class CGameBTComms : public CBase
{
public:
    IMPORT_C static CGameBTComms* NewL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog = NULL);
    IMPORT_C ~CGameBTComms();

public:
    enum                 { KMaxPlayers         = KBTMaxPlayers };
    enum                 { KServerConnectionId = KBTHostConnectionId };
    enum TConnectionRole { EIdle, EClient, EHost };
    enum TGameState      { EGameOver, EPlay, EPause };
    enum TConnectState   { ENotConnected, EConnecting, EConnected };

#if VERSION >= 9

    enum TAcceptMode     { EConfirmClients };

#endif /* VERSION >= 9 */

public:
    IMPORT_C void            StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers);
    IMPORT_C void            StartClientL();
    IMPORT_C TConnectionRole ConnectionRole();
    IMPORT_C TGameState      GameState();
    IMPORT_C TConnectState   ConnectState();
    IMPORT_C TInt            GetLocalDeviceName(THostName& aHostName);

#if VERSION < 9

    IMPORT_C void            Disconnect();

#endif /* VERSION > 9 */

    IMPORT_C TInt            DisconnectClient(TUint16 aClientId);
    IMPORT_C TInt            SendDataToClient(TUint16 aClientId, TDesC8& aData);
    IMPORT_C TInt            SendDataToAllClients(TDesC8& aData);
    IMPORT_C TInt            SendDataToHost(TDesC8& aData);
    IMPORT_C TInt            ContinueMultiPlayerGame();
    IMPORT_C TInt            ReconnectL(TBool aMustReconnectToAll = ETrue);
    IMPORT_C TInt            PauseMultiPlayerGame();
    IMPORT_C TInt            EndMultiPlayerGame();
    IMPORT_C TBool           IsShowingDeviceSelectDlg();

#if VERSION >= 9

    IMPORT_C TInt            Connect();                                          /* Expects KErrNone */
    IMPORT_C TInt            DeclareRemoteDevice(const TDeviceDetails& aDevice); /* Expects KErrNone */
    IMPORT_C TInt            SetBluetoothPowerState(TBool aState);               /* Expects KErrNone */
    IMPORT_C void            SetLocalDeviceName(THostName aHostName);            /* Return type unclear */
    IMPORT_C void            BluetoothPowerState(TBool aState);                  /* Return type unclear */
    IMPORT_C void            Disconnect(TUint16 aClientId = 0, TBool aPermanently = EFalse);
    IMPORT_C void            EndSession();                                       /* Return type unclear */
    IMPORT_C void            SendData(TUint16 aClientId, TPtr8 aData);           /* Return type unclear */
    IMPORT_C void            SetDiscoverabilityModeLimited(TBool aMode);         /* Return type unclear */
    IMPORT_C void            SetHostAcceptMode(TAcceptMode aMode);               /* Return type unclear */
    IMPORT_C void            SetSearchModeLimited(TBool aMode);                  /* Return type unclear */

#endif /* VERSION >= 9 */

protected:
    CGameBTComms();
    void ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog);

protected:
    MGameBTCommsNotify* iNotify;
    TUint32             iGameUID;
    RSGEDebugLog*       iLog;
    CGameBTBase*        iComms;
};

#endif /* __GAMEBTCOMMS_H */
