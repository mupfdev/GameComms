/** @file GameBTComms.h
 *
 *  A GameComms re-implementation for the Nokia N-Gage.
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

CGameBTComms* CGameBTComms::NewL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog = NULL)
{
    CGameBTComms* pCGameBTComms = new CGameBTComms;
    CleanupStack::PushL(pCGameBTComms);
    pCGameBTComms->ConstructL(aEventHandler, aGameUID, aLog);
    CleanupStack::Pop();
    return pCGameBTComms;
}

EXPORT_C CGameBTComms::CGameBTComms()
{
    iNotify  = NULL;
    iGameUID = 0;
    iLog     = NULL;
    iComms   = NULL;
}

CGameBTComms::~CGameBTComms()
{
}

void CGameBTComms::ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog)
{
}
