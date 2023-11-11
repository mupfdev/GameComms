/** @file GameBTCommsNotify.h
 *
 *  A GameComms re-implementation for the Nokia N-Gage.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef __GAMEBTCOMMSNOTIFY_H
#define __GAMEBTCOMMSNOTIFY_H

const TInt KUnknownClientId     = 0;
const TInt KErrIncompatibleGame = (-1001);
const TInt KErrNotCommsHost     = (-1002);
const TInt KErrClientNotReady   = (-1003);
const TInt KErrHostQuit         = (-1004);
const TInt KErrNotEnoughPlayers = (-1005);
const TInt KErrGamePaused       = (-1006);
const TInt KErrGameOver         = (-1007);

class MGameBTCommsNotify
{
public:
    virtual void ClientConnected(TUint16 aClientId, TDesC& aClientName, TInt aError) = 0;
    virtual void HostSelected(TInt aError)                                           = 0;
    virtual void HostConnected(TInt aError)                                          = 0;
    virtual void StartMultiPlayerGame(TInt aError)                                   = 0;
    virtual void ContinueMultiPlayerGame()                                           = 0;
    virtual void PauseMultiPlayerGame()                                              = 0;
    virtual void EndMultiPlayerGame(TInt aReason)                                    = 0;
    virtual void ConnectedClientEndedGame(TUint16 aClientId)                         = 0;
    virtual void ClientDisconnected(TUint16 aClientId, TInt aError)                  = 0;
    virtual void HostDisconnected(TInt aError)                                       = 0;
    virtual void ReceiveDataFromClient(TUint16 aClientId, TDesC8& aData)             = 0;
    virtual void ReceiveDataFromHost(TDesC8& aData)                                  = 0;
};

#endif // __GAMEBTCOMMSNOTIFY_H
