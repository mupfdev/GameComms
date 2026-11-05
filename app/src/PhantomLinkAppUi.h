/** @file PhantomLinkAppUi.h
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef PHANTOMLINKAPPUI_H
#define PHANTOMLINKAPPUI_H

#include "BridgeClient.h"
#include <aknappui.h>

class CPhantomLinkView;

class CPhantomLinkAppUi : public CAknAppUi, public MBridgeClientObserver
{
  public:
    void ConstructL();
    CPhantomLinkAppUi();
    ~CPhantomLinkAppUi();

  public: // from CEikAppUi
    void HandleCommandL(TInt aCommand);
    void DynInitMenuPaneL(TInt aResourceId, CEikMenuPane *aMenuPane);

  public: // from MBridgeClientObserver
    void BridgeFinished(TInt aError);
    void BridgeProgress(const TDesC &aText);

  private:
    enum TMode
    {
        EGames,
        EBridge
    };
    enum TOperation
    {
        ENone,
        ERead,
        EWrite,
        ERestart,
        EFactory
    };

    void ShowGamesL();
    void ShowBridgeL();
    void RefreshBridgeL();
    void SetStatusL(const TDesC &aText);
    void InstallSelectedL();
    void EditSelectedL();
    void StartL(TOperation aOperation, const TDesC8 &aCommands, TBool aChooseBridge);
    void WriteSettingsL();
    void ShowAboutL();
    TBool ConfirmL(const TDesC &aPrompt);
    void InfoL(const TDesC &aText);
    void ErrorL(const TDesC &aText);
    void DescribeError(TInt aError, TDes &aText);

    CPhantomLinkView *iView;
    CBridgeClient *iClient;
    TBridgeSettings iSettings;
    TMode iMode;
    TOperation iOperation;
    TBuf<80> iStatus;
};

#endif /* PHANTOMLINKAPPUI_H */
