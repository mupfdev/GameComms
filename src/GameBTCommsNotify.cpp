/** @file GameBTCommsNotify.cpp
 *
 *  A GameComms re-implementation for the Nokia N-Gage.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <e32def.h>
#include <e32std.h>
#include "GameBTCommsNotify.h"

EXPORT_C void MGameBTCommsNotify::ClientConnected(TUint16 aClientId, TDesC& aClientName, TInt aError)
{
}

EXPORT_C void MGameBTCommsNotify::HostSelected(TInt aError)
{
}

EXPORT_C void MGameBTCommsNotify::HostConnected(TInt aError)
{
}

EXPORT_C void MGameBTCommsNotify::StartMultiPlayerGame(TInt aError)
{
}

EXPORT_C void MGameBTCommsNotify::ContinueMultiPlayerGame()
{
}

EXPORT_C void MGameBTCommsNotify::PauseMultiPlayerGame()
{
}

EXPORT_C void MGameBTCommsNotify::EndMultiPlayerGame(TInt aReason)
{
}

EXPORT_C void MGameBTCommsNotify::ConnectedClientEndedGame(TUint16 aClientId)
{
}

EXPORT_C void MGameBTCommsNotify::ClientDisconnected(TUint16 aClientId, TInt aError)
{
}

EXPORT_C void MGameBTCommsNotify::HostDisconnected(TInt aError)
{
}

EXPORT_C void MGameBTCommsNotify::ReceiveDataFromClient(TUint16 aClientId, TDesC8& aData)
{
}

EXPORT_C void MGameBTCommsNotify::ReceiveDataFromHost(TDesC8& aData)
{
}
