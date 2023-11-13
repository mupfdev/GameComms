/** @file GameBTCommsNotify.cpp
 *
 *  Mixin class that specifies the notification callbacks that are
 *  called by the BT Comms library.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <e32def.h>
#include <e32std.h>
#include "GameBTCommsNotify.h"

void MGameBTCommsNotify::ClientConnected(TUint16 aClientId, TDesC& aClientName, TInt aError)
{
}

void MGameBTCommsNotify::HostSelected(TInt aError)
{
}

void MGameBTCommsNotify::HostConnected(TInt aError)
{
}

void MGameBTCommsNotify::StartMultiPlayerGame(TInt aError)
{
}

void MGameBTCommsNotify::ContinueMultiPlayerGame()
{
}

void MGameBTCommsNotify::PauseMultiPlayerGame()
{
}

void MGameBTCommsNotify::EndMultiPlayerGame(TInt aReason)
{
}

void MGameBTCommsNotify::ConnectedClientEndedGame(TUint16 aClientId)
{
}

void MGameBTCommsNotify::ClientDisconnected(TUint16 aClientId, TInt aError)
{
}

void MGameBTCommsNotify::HostDisconnected(TInt aError)
{
}

void MGameBTCommsNotify::ReceiveDataFromClient(TUint16 aClientId, TDesC8& aData)
{
}

void MGameBTCommsNotify::ReceiveDataFromHost(TDesC8& aData)
{
}
