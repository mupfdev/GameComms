/** @file BridgeClient.cpp
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include "BridgeClient.h"
#include <utf.h>

_LIT8(KHello, "GCCFG1\n");
_LIT8(KEndMarker, "\nEND\r\n");
_LIT(KSetText, "(set)");

const TInt KRxSize = 4096;
const TInt KMaxLine = 190; /* the bridge accepts 200 characters per line */
const TInt KTimeoutSecs = 25;

/* ------------------------------------------------------------------ */
/* TBridgeSettings                                                     */
/* ------------------------------------------------------------------ */

TBridgeSettings::TBridgeSettings() : iLoaded(EFalse)
{
    for (TInt i = 0; i < EFieldCount; i++) {
        iDirty[i] = EFalse;
    }
}

TPtrC8 TBridgeSettings::Key(TInt aField)
{
    switch (aField) {
    case ESsid:
        return _L8("ssid");
    case EPass:
        return _L8("pass");
    case EHost:
        return _L8("host");
    case EPort:
        return _L8("port");
    case EKey:
        return _L8("key");
    case EBtName:
        return _L8("btname");
    case EPin:
        return _L8("pin");
    default:
        return _L8("verbose");
    }
}

TPtrC TBridgeSettings::Label(TInt aField)
{
    switch (aField) {
    case ESsid:
        return _L("Wi-Fi SSID");
    case EPass:
        return _L("Wi-Fi password");
    case EHost:
        return _L("Server (auto = LAN)");
    case EPort:
        return _L("Server port");
    case EKey:
        return _L("Access key");
    case EBtName:
        return _L("Bluetooth name");
    case EPin:
        return _L("Bluetooth PIN");
    default:
        return _L("Log (0/1)");
    }
}

TInt TBridgeSettings::MaxLength(TInt aField)
{
    switch (aField) {
    case ESsid:
        return 32;
    case EPass:
        return 63;
    case EHost:
        return 63;
    case EPort:
        return 5;
    case EKey:
        return 32; /* GC_MAX_NAME */
    case EBtName:
        return 32;
    case EPin:
        return 16;
    default:
        return 1;
    }
}

TBool TBridgeSettings::IsSecret(TInt aField)
{
    return aField == EPass || aField == EKey;
}

void TBridgeSettings::ListText(TInt aField, TDes &aText) const
{
    aText.Zero();
    aText.Append(_L("\t"));
    aText.Append(Label(aField));
    aText.Append(_L(": "));
    if (!iLoaded && !iDirty[aField]) {
        aText.Append(_L("?"));
    } else if (iValue[aField].Length() == 0) {
        aText.Append(_L("-"));
    } else if (IsSecret(aField) && !iDirty[aField]) {
        aText.Append(KSetText);
    } else if (IsSecret(aField)) {
        aText.Append(_L("(new)"));
    } else {
        aText.Append(iValue[aField].Left(aText.MaxLength() - aText.Length()));
    }
}

void TBridgeSettings::Set(TInt aField, const TDesC &aValue)
{
    iValue[aField].Copy(aValue.Left(MaxLength(aField)));
    iDirty[aField] = ETrue;
}

TBool TBridgeSettings::HasChanges() const
{
    for (TInt i = 0; i < EFieldCount; i++) {
        if (iDirty[i]) {
            return ETrue;
        }
    }
    return EFalse;
}

void TBridgeSettings::ClearChanges()
{
    for (TInt i = 0; i < EFieldCount; i++) {
        iDirty[i] = EFalse;
    }
}

void TBridgeSettings::Parse(const TDesC8 &aReply)
{
    TPtrC8 rest(aReply);
    TInt found = 0;
    while (rest.Length() > 0) {
        TInt nl = rest.Locate('\n');
        TPtrC8 line = nl < 0 ? rest : rest.Left(nl);
        rest.Set(nl < 0 ? TPtrC8() : rest.Mid(nl + 1));
        while (line.Length() > 0 && line[line.Length() - 1] == '\r') {
            line.Set(line.Left(line.Length() - 1));
        }
        TInt eq = line.Locate('=');
        if (eq <= 0) {
            continue;
        }
        TPtrC8 key = line.Left(eq);
        TPtrC8 val = line.Mid(eq + 1);
        for (TInt f = 0; f < EFieldCount; f++) {
            if (key.Compare(Key(f)) != 0) {
                continue;
            }
            if (IsSecret(f)) {
                /* the bridge never reveals secrets, only whether one is set */
                iValue[f].Zero();
                if (val.Length() > 0) {
                    iValue[f].Copy(KSetText);
                }
            } else {
                iValue[f].Zero();
                CnvUtfConverter::ConvertToUnicodeFromUtf8(iValue[f], val.Left(KMaxLine));
            }
            iDirty[f] = EFalse;
            found++;
            break;
        }
    }
    if (found > 0) {
        iLoaded = ETrue;
    }
}

HBufC8 *TBridgeSettings::BuildCommandsL(TBool aSave) const
{
    HBufC8 *buf = HBufC8::NewLC(2048);
    TPtr8 out = buf->Des();
    TBuf8<KMaxLine> value;
    for (TInt f = 0; f < EFieldCount; f++) {
        if (!iDirty[f]) {
            continue;
        }
        value.Zero();
        if (CnvUtfConverter::ConvertFromUnicodeToUtf8(value, iValue[f]) != 0) {
            User::Leave(KErrTooBig);
        }
        for (TInt i = 0; i < value.Length(); i++) {
            if (value[i] < 0x20) {
                value[i] = ' '; /* a line break would end the command */
            }
        }
        TPtrC8 key = Key(f);
        if (4 + key.Length() + 1 + value.Length() > KMaxLine) {
            User::Leave(KErrTooBig);
        }
        out.Append(_L8("set "));
        out.Append(key);
        out.Append(' ');
        out.Append(value);
        out.Append('\n');
    }
    if (aSave) {
        out.Append(_L8("save\n"));
    }
    CleanupStack::Pop(); /* buf */
    return buf;
}

/* ------------------------------------------------------------------ */
/* CBridgeClient                                                       */
/* ------------------------------------------------------------------ */

CBridgeClient *CBridgeClient::NewL(MBridgeClientObserver &aObserver)
{
    CBridgeClient *self = new (ELeave) CBridgeClient(aObserver);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
}

CBridgeClient::CBridgeClient(MBridgeClientObserver &aObserver) : iObserver(aObserver)
{
}

void CBridgeClient::ConstructL()
{
    iTransport = CGcTransport::NewL(*this, iConfig);
    iTimer = CPeriodic::NewL(CActive::EPriorityLow);
    iNotifier = CIdle::NewL(CActive::EPriorityIdle);
    iRx = HBufC8::NewL(KRxSize);
}

CBridgeClient::~CBridgeClient()
{
    if (iTimer) {
        iTimer->Cancel();
    }
    delete iNotifier;
    delete iTimer;
    delete iTransport;
    delete iCommands;
    delete iRx;
}

void CBridgeClient::StartL(const TDesC8 &aCommands, TBool aChooseBridge)
{
    if (iBusy) {
        User::Leave(KErrInUse);
    }
    HBufC8 *commands = aCommands.AllocL();
    delete iCommands;
    iCommands = commands;
    iRx->Des().Zero();

    iNextPos = 0;
    iScanPos = 0;
    iEnds = 0;
    iExpected = 1; /* the greeting */
    for (TInt i = 0; i < iCommands->Length(); i++) {
        if ((*iCommands)[i] == '\n') {
            iExpected++;
        }
    }
    iIdleSecs = 0;
    iSelecting = EFalse;
    iAwaitRestart = EFalse;

    iConfig.Load();
    if (aChooseBridge) {
        iConfig.iHasBridge = EFalse;
    }
    iTransport->ConnectL();
    iBusy = ETrue;
    iTimer->Start(1000000, 1000000, TCallBack(TimerCallback, this));
}

void CBridgeClient::Abort()
{
    Finish(KErrCancel);
}

void CBridgeClient::SendNext()
{
    if (iNextPos >= iCommands->Length()) {
        return;
    }
    TPtrC8 rest = iCommands->Mid(iNextPos);
    TInt nl = rest.Locate('\n');
    TPtrC8 line = nl < 0 ? rest : rest.Left(nl + 1);
    iNextPos += line.Length();
    if (line.Match(_L8("reboot*")) == 0 || line.Match(_L8("setup*")) == 0 || line.Match(_L8("factory*")) == 0) {
        /* the bridge restarts after its reply and never sends "END" */
        iAwaitRestart = ETrue;
    }
    if (iTransport->Write(line) != KErrNone) {
        Finish(KErrOverflow);
    }
}

void CBridgeClient::Finish(TInt aError)
{
    if (!iBusy) {
        return;
    }
    iBusy = EFalse;
    iSelecting = EFalse;
    iTimer->Cancel();
    iTransport->Disconnect();
    iResult = aError;
    iNotifier->Start(TCallBack(NotifyCallback, this));
}

TInt CBridgeClient::NotifyCallback(TAny *aSelf)
{
    CBridgeClient *self = static_cast<CBridgeClient *>(aSelf);
    self->iObserver.BridgeFinished(self->iResult);
    return 0;
}

TInt CBridgeClient::TimerCallback(TAny *aSelf)
{
    CBridgeClient *self = static_cast<CBridgeClient *>(aSelf);
    if (!self->iSelecting && ++self->iIdleSecs > KTimeoutSecs) {
        self->Finish(KErrTimedOut);
    }
    return 1;
}

void CBridgeClient::TransportUp()
{
    iIdleSecs = 0;
    iObserver.BridgeProgress(_L("Connected"));
    if (iTransport->Write(KHello) != KErrNone) {
        Finish(KErrOverflow);
    }
}

void CBridgeClient::TransportData(const TDesC8 &aData)
{
    if (!iBusy) {
        return;
    }
    iIdleSecs = 0;
    TPtr8 rx = iRx->Des();
    if (rx.Length() + aData.Length() > rx.MaxLength()) {
        Finish(KErrOverflow);
        return;
    }
    rx.Append(aData);
    if (iAwaitRestart) {
        Finish(KErrNone); /* the reply to the restart command */
        return;
    }
    for (;;) {
        TPtrC8 unscanned = iRx->Mid(iScanPos);
        TInt at = unscanned.Find(KEndMarker);
        if (at < 0) {
            break;
        }
        iScanPos += at + KEndMarker().Length();
        iEnds++;
        if (iEnds >= iExpected) {
            Finish(KErrNone);
            return;
        }
        SendNext();
        if (!iBusy) {
            return;
        }
    }
}

void CBridgeClient::TransportDown(TInt aError)
{
    if (!iBusy) {
        return;
    }
    if (iAwaitRestart) {
        Finish(KErrNone);
    } else {
        Finish(aError != KErrNone ? aError : KErrDisconnected);
    }
}

void CBridgeClient::TransportSelecting(TBool aShowing)
{
    iSelecting = aShowing;
    iIdleSecs = 0;
}

void CBridgeClient::TransportProgress(const TDesC8 &aStep, TInt aValue)
{
    iIdleSecs = 0;
    TBuf<80> text;
    text.Copy(aStep.Left(60));
    text.Append(_L(" "));
    text.AppendNum(aValue);
    iObserver.BridgeProgress(text);
}
