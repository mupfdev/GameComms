/** @file PhantomLinkApp.h
 *
 *  Application and document classes.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef PHANTOMLINKAPP_H
#define PHANTOMLINKAPP_H

#include <aknapp.h>
#include <akndoc.h>

class CPhantomLinkApplication : public CAknApplication
{
  public:
    TUid AppDllUid() const;

  protected:
    CApaDocument *CreateDocumentL();
};

class CPhantomLinkDocument : public CAknDocument
{
  public:
    CPhantomLinkDocument(CEikApplication &aApp) : CAknDocument(aApp) {}

  protected:
    CEikAppUi *CreateAppUiL();
};

#endif /* PHANTOMLINKAPP_H */
