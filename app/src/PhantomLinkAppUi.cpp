/** @file PhantomLinkAppUi.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "PhantomLinkAppUi.h"
#include "GameInstaller.h"
#include "GameList.h"
#include "PhantomLink.hrh"
#include "PhantomLinkView.h"
#include <PhantomLink.rsg>
#include <aknnotewrappers.h>
#include <aknquerydialog.h>
#include <avkon.hrh>
#include <badesca.h>
#include <eikmenup.h>

#include <apparc.h>
#include <eikapp.h>

/* TRAP_IGNORE does not exist in this SDK */
#define PL_IGNORE(aStmt)        \
    do {                        \
        TRAPD(ignored_, aStmt); \
    } while (0)

const TInt KStatusLine = 0;

CPhantomLinkAppUi::CPhantomLinkAppUi() : iMode(EGames), iOperation(ENone)
{
}

void CPhantomLinkAppUi::ConstructL()
{
    BaseConstructL();
    iView = CPhantomLinkView::NewL(ClientRect());
    AddToStackL(iView);
    iClient = CBridgeClient::NewL(*this);
    iStatus = _L("not read");
    ShowGamesL();
}

CPhantomLinkAppUi::~CPhantomLinkAppUi()
{
    delete iClient;
    if (iView) {
        RemoveFromStack(iView);
        delete iView;
    }
}

/* ------------------------------------------------------------------ */
/* lists                                                               */
/* ------------------------------------------------------------------ */

void CPhantomLinkAppUi::ShowGamesL()
{
    iMode = EGames;
    CDesCArrayFlat *items = new (ELeave) CDesCArrayFlat(GameCount());
    CleanupStack::PushL(items);
    TBuf<80> line;
    TBuf<72> name;
    for (TInt i = 0; i < GameCount(); i++) {
        name.Copy(TPtrC8((const TUint8 *)GameAt(i).iName).Left(70));
        line.Copy(_L("\t"));
        line.Append(name);
        items->AppendL(line);
    }
    CleanupStack::Pop();
    iView->SetListL(items, 0);
}

void CPhantomLinkAppUi::ShowBridgeL()
{
    iMode = EBridge;
    CDesCArrayFlat *items = new (ELeave) CDesCArrayFlat(TBridgeSettings::EFieldCount + 1);
    CleanupStack::PushL(items);
    TBuf<120> line;
    line.Copy(_L("\tStatus: "));
    line.Append(iStatus);
    items->AppendL(line);
    for (TInt f = 0; f < TBridgeSettings::EFieldCount; f++) {
        iSettings.ListText(f, line);
        items->AppendL(line);
    }
    CleanupStack::Pop();
    iView->SetListL(items, 0);
}

void CPhantomLinkAppUi::RefreshBridgeL()
{
    if (iMode != EBridge) {
        return;
    }
    TBuf<120> line;
    for (TInt f = 0; f < TBridgeSettings::EFieldCount; f++) {
        iSettings.ListText(f, line);
        iView->ReplaceItemL(f + 1, line);
    }
}

void CPhantomLinkAppUi::SetStatusL(const TDesC &aText)
{
    iStatus.Copy(aText.Left(iStatus.MaxLength()));
    if (iMode == EBridge) {
        TBuf<120> line;
        line.Copy(_L("\tStatus: "));
        line.Append(iStatus);
        iView->ReplaceItemL(KStatusLine, line);
    }
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

void CPhantomLinkAppUi::DynInitMenuPaneL(TInt aResourceId, CEikMenuPane *aMenuPane)
{
    if (aResourceId != R_PL_MENU) {
        return;
    }
    TBool games = iMode == EGames;
    aMenuPane->SetItemDimmed(EPlCmdSelect, EFalse);
    aMenuPane->SetItemDimmed(EPlCmdGames, games);
    aMenuPane->SetItemDimmed(EPlCmdBridge, !games);
    aMenuPane->SetItemDimmed(EPlCmdRead, games);
    aMenuPane->SetItemDimmed(EPlCmdWrite, games);
    aMenuPane->SetItemDimmed(EPlCmdReboot, games);
    aMenuPane->SetItemDimmed(EPlCmdSetup, games);
    aMenuPane->SetItemDimmed(EPlCmdFactory, games);
    aMenuPane->SetItemDimmed(EPlCmdChoose, games);
}

void CPhantomLinkAppUi::HandleCommandL(TInt aCommand)
{
    switch (aCommand) {
    case EAknSoftkeyExit:
    case EAknCmdExit:
    case EEikCmdExit:
        Exit();
        return;
    default:
        break;
    }

    if (iClient->IsBusy()) {
        InfoL(_L("Please wait, talking to the bridge"));
        return;
    }

    switch (aCommand) {
    case EPlCmdSelect:
        if (iMode == EGames) {
            InstallSelectedL();
        } else if (iView->CurrentIndex() == KStatusLine) {
            StartL(ERead, _L8("show\n"), EFalse);
        } else {
            EditSelectedL();
        }
        break;
    case EPlCmdGames:
        ShowGamesL();
        break;
    case EPlCmdBridge:
        ShowBridgeL();
        break;
    case EPlCmdRead:
        StartL(ERead, _L8("show\n"), EFalse);
        break;
    case EPlCmdWrite:
        WriteSettingsL();
        break;
    case EPlCmdReboot:
        StartL(ERestart, _L8("reboot\n"), EFalse);
        break;
    case EPlCmdSetup:
        if (ConfirmL(_L("Restart the bridge into setup mode?"))) {
            StartL(ERestart, _L8("setup\n"), EFalse);
        }
        break;
    case EPlCmdFactory:
        if (ConfirmL(_L("Erase ALL settings of the bridge?"))) {
            StartL(EFactory, _L8("factory\n"), EFalse);
        }
        break;
    case EPlCmdChoose:
        StartL(ERead, _L8("show\n"), ETrue);
        break;
    case EPlCmdAbout:
        ShowAboutL();
        break;
    default:
        break;
    }
}

void CPhantomLinkAppUi::InstallSelectedL()
{
    TInt index = iView->CurrentIndex();
    if (index < 0 || index >= GameCount()) {
        return;
    }
    const TGameEntry &game = GameAt(index);
    Install(game.iVariant, ((CApaApplication *)Application())->AppFullName(), game.iTargetDir);
    TBuf<160> text;
    if (err == KErrNone) {
        TBuf<64> name;
        name.Copy(TPtrC8((const TUint8 *)game.iName).Left(60));
        text.Copy(_L("gamecomms.dll for "));
        text.Append(name);
        text.Append(_L(" copied"));
        InfoL(text);
    } else {
        DescribeError(err, text);
        ErrorL(text);
    }
}

void CPhantomLinkAppUi::EditSelectedL()
{
    TInt field = iView->CurrentIndex() - 1;
    if (field < 0 || field >= TBridgeSettings::EFieldCount) {
        return;
    }
    TBuf<TBridgeSettings::EValueLen> value;
    if (!TBridgeSettings::IsSecret(field) || iSettings.iDirty[field]) {
        value.Copy(iSettings.iValue[field]);
    }
    CAknTextQueryDialog *dialog = CAknTextQueryDialog::NewL(value);
    dialog->SetMaxLength(TBridgeSettings::MaxLength(field));
    if (!dialog->ExecuteLD(R_PL_TEXT_QUERY, TBridgeSettings::Label(field))) {
        return;
    }

    TLex lex(value);
    TInt number = 0;
    switch (field) {
    case TBridgeSettings::EPort:
        if (lex.Val(number) != KErrNone || !lex.Eos() || number < 1 || number > 65535) {
            ErrorL(_L("Port must be 1 to 65535"));
            return;
        }
        break;
    case TBridgeSettings::EVerbose:
        if (value.Length() != 1 || (value[0] != '0' && value[0] != '1')) {
            ErrorL(_L("Enter 0 or 1"));
            return;
        }
        break;
    case TBridgeSettings::EBtName:
    case TBridgeSettings::EPin:
        if (value.Length() == 0) {
            ErrorL(_L("This value cannot be empty"));
            return;
        }
        break;
    default:
        break;
    }
    iSettings.Set(field, value);
    RefreshBridgeL();
}

void CPhantomLinkAppUi::WriteSettingsL()
{
    if (!iSettings.HasChanges()) {
        InfoL(_L("No changes to send"));
        return;
    }
    HBufC8 *commands = iSettings.BuildCommandsL(ETrue);
    CleanupStack::PushL(commands);
    if (ConfirmL(_L("Reboot the bridge to apply the settings?"))) {
        commands->Des().Append(_L8("reboot\n")); /* the buffer has room for it */
    }
    StartL(EWrite, *commands, EFalse);
    CleanupStack::PopAndDestroy(); /* commands */
}

void CPhantomLinkAppUi::StartL(TOperation aOperation, const TDesC8 &aCommands, TBool aChooseBridge)
{
    if (iMode != EBridge) {
        ShowBridgeL();
    }
    iOperation = aOperation;
    SetStatusL(_L("connecting..."));
    TRAPD(err, iClient->StartL(aCommands, aChooseBridge));
    if (err != KErrNone) {
        iOperation = ENone;
        TBuf<100> text;
        DescribeError(err, text);
        SetStatusL(text);
        ErrorL(text);
    }
}

/* ------------------------------------------------------------------ */
/* bridge client callbacks                                             */
/* ------------------------------------------------------------------ */

void CPhantomLinkAppUi::BridgeProgress(const TDesC &aText)
{
    PL_IGNORE(SetStatusL(aText));
}

void CPhantomLinkAppUi::BridgeFinished(TInt aError)
{
    TOperation operation = iOperation;
    iOperation = ENone;
    TBuf<100> text;

    if (aError != KErrNone) {
        DescribeError(aError, text);
        PL_IGNORE(SetStatusL(text));
        PL_IGNORE(ErrorL(text));
        return;
    }

    switch (operation) {
    case ERead:
        iSettings.Parse(iClient->Reply());
        if (!iSettings.iLoaded) {
            PL_IGNORE(SetStatusL(_L("unexpected reply")));
            PL_IGNORE(ErrorL(_L("The bridge sent no settings. Is the firmware up to date?")));
            return;
        }
        PL_IGNORE(SetStatusL(_L("settings read")));
        PL_IGNORE(RefreshBridgeL());
        break;
    case EWrite:
        iSettings.ClearChanges();
        PL_IGNORE(SetStatusL(_L("settings saved")));
        PL_IGNORE(RefreshBridgeL());
        PL_IGNORE(InfoL(_L("Settings saved")));
        break;
    case EFactory:
        iSettings = TBridgeSettings();
        PL_IGNORE(SetStatusL(_L("factory reset done")));
        PL_IGNORE(RefreshBridgeL());
        PL_IGNORE(InfoL(_L("Bridge reset to factory settings and restarting")));
        break;
    case ERestart:
        PL_IGNORE(SetStatusL(_L("bridge restarting")));
        PL_IGNORE(InfoL(_L("Bridge restarting")));
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

void CPhantomLinkAppUi::DescribeError(TInt aError, TDes &aText)
{
    switch (aError) {
    case KErrInUse:
        aText.Copy(_L("File in use. Close the game first."));
        break;
    case KErrNotFound:
    case KErrPathNotFound:
        aText.Copy(_L("Not found (memory card in E:? bridge in range?)"));
        break;
    case KErrTimedOut:
        aText.Copy(_L("The bridge did not answer"));
        break;
    case KErrCancel:
        aText.Copy(_L("Cancelled"));
        break;
    case KErrDisconnected:
    case KErrCommsLineFail:
        aText.Copy(_L("Connection to the bridge lost"));
        break;
    case KErrTooBig:
        aText.Copy(_L("A value is too long"));
        break;
    default:
        aText.Copy(_L("Error "));
        aText.AppendNum(aError);
        break;
    }
}

TBool CPhantomLinkAppUi::ConfirmL(const TDesC &aPrompt)
{
    CAknQueryDialog *dialog = CAknQueryDialog::NewL();
    return dialog->ExecuteLD(R_PL_CONFIRM_QUERY, aPrompt) != 0;
}

void CPhantomLinkAppUi::InfoL(const TDesC &aText)
{
    CAknInformationNote *note = new (ELeave) CAknInformationNote(ETrue);
    note->ExecuteLD(aText);
}

void CPhantomLinkAppUi::ErrorL(const TDesC &aText)
{
    CAknErrorNote *note = new (ELeave) CAknErrorNote(ETrue);
    note->ExecuteLD(aText);
}

void CPhantomLinkAppUi::ShowAboutL()
{
    InfoL(_L("PhantomLink: pick your game to install its gamecomms.dll, or set up the ESP32 bridge over Bluetooth."));
}
