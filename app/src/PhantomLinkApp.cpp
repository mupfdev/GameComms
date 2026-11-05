/** @file PhantomLinkApp.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "PhantomLinkApp.h"
#include "PhantomLinkAppUi.h"

static const TUid KUidPhantomLinkApp = { UID3 };

TUid CPhantomLinkApplication::AppDllUid() const
{
    return KUidPhantomLinkApp;
}

CApaDocument *CPhantomLinkApplication::CreateDocumentL()
{
    return new (ELeave) CPhantomLinkDocument(*this);
}

CEikAppUi *CPhantomLinkDocument::CreateAppUiL()
{
    return new (ELeave) CPhantomLinkAppUi;
}

GLDEF_C TInt E32Dll(TDllReason /* aReason */)
{
    return KErrNone;
}

EXPORT_C CApaApplication *NewApplication()
{
    return new CPhantomLinkApplication;
}
