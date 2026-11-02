/** @file GameBTComms.h
 *
 *  The two CGameBTComms generations implemented on top of CGcEngine.
 *  Games only see opaque pointers returned by NewL; every exported
 *  function is reached through the extern "C" thunks in GcExports.cpp,
 *  which the per-variant .def files bind to the original ordinals.
 *
 *  The only layout requirement is that both classes start with the
 *  CBase vtable (virtual destructor at slot +8), because games delete
 *  the object through it.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GAMEBTCOMMS_H
#define GAMEBTCOMMS_H

#include "GcEngine.h"

class CEikAppUi;

/* GameComms 1.x (variant v1) */
class CGameBTComms1 : public CBase, public MGcEventSink
{
public:
    static CGameBTComms1* NewL(MGameBTCommsNotify* aNotify, TUint32 aGameUid);
    ~CGameBTComms1();

    void  StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers);
    void  StartClientL();
    TInt  ConnectionRole();
    TInt  GameState();
    TInt  ConnectState();
    TInt  GetLocalDeviceName(THostName& aName);
    void  Disconnect();
    TInt  DisconnectClient(TUint16 aClientId);
    TInt  SendDataToClient(TUint16 aClientId, const TDesC8& aData);
    TInt  SendDataToAllClients(const TDesC8& aData);
    TInt  SendDataToHost(const TDesC8& aData);
    TInt  ContinueMultiPlayerGame();
    TInt  ReconnectL(TBool aMustReconnectToAll);
    TInt  PauseMultiPlayerGame();
    TInt  EndMultiPlayerGame();
    TBool IsShowingDeviceSelectDlg();

    void HandleGcEventL(const gcd_event& aEvent);

private:
    CGameBTComms1(MGameBTCommsNotify* aNotify);
    MGameBTCommsNotify* iNotify;
    CGcEngine*          iEngine;
};

/* GameComms 2.x (variants v2..v11) */
class CGameBTComms2 : public CBase, public MGcEventSink
{
public:
    static CGameBTComms2* NewL(MGameBTCommsNotify2* aNotify, TUint32 aGameUid, TUint8 aVariant);
    ~CGameBTComms2();

    void StartHostL(TInt aShowList, CEikAppUi* aAppUi);
    void StartClientL(TInt aShowList, CEikAppUi* aAppUi);
    TInt ConnectionRole();
    TInt ConnectState();
    TInt GetLocalDeviceName(THostName& aName);
    TInt BluetoothPowerState(TBool& aState);
    TInt SetBluetoothPowerState(TBool aState);
    TInt SetLocalDeviceName(const TDesC& aName);
    TInt DeclareRemoteDevice(const TDeviceDetails& aDevice);
    TInt Connect();
    TInt Connect(TUint16 aClientId);
    void Disconnect();
    TInt Disconnect(TUint16 aClientId, TBool aPermanently);
    TInt SendData(const TDesC8& aData);
    TInt SendData(TUint16 aClientId, const TDesC8& aData);
    TInt SetHostAcceptMode(TInt aMode);
    TInt HostAcceptMode();
    void EndSession();
    TInt SetDiscoverabilityModeLimited(TBool aLimited);

    void HandleGcEventL(const gcd_event& aEvent);

private:
    CGameBTComms2(MGameBTCommsNotify2* aNotify);
    void EnsureEngineL();
    MGameBTCommsNotify2* iNotify;
    CGcEngine*           iEngine;
    TUint32              iGameUid;
    TUint8               iVariant;
    TDeviceDetails*      iDetails; /* scratch for callbacks (560 bytes) */
};

#endif /* GAMEBTCOMMS_H */
