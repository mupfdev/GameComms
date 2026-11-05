/** @file BridgeClient.h
 *
 *  Configures the ESP32 bridge over Bluetooth.
 *
 *  The bridge serves a text console on its Bluetooth serial port (RFCOMM).
 *  A connection that starts with "GCCFG1" turns into the same command set
 *  as the serial console, one command per line; every reply ends with a
 *  line "END" (see docs/BUILD.md, "Bluetooth configuration").  The
 *  connection handling (device selection, SDP lookup, RFCOMM) is shared
 *  with GAMECOMMS.DLL (GcTransport), including the saved bridge address in
 *  GameComms.ini.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef BRIDGECLIENT_H
#define BRIDGECLIENT_H

#include "GcTransport.h"
#include <e32base.h>

/* ------------------------------------------------------------------ */
/* the settings of the bridge, as shown and edited by the application  */
/* ------------------------------------------------------------------ */

class TBridgeSettings
{
  public:
    enum TField
    {
        ESsid,
        EPass,
        EHost,
        EPort,
        EKey,
        EBtName,
        EPin,
        EVerbose,
        EFieldCount
    };
    enum
    {
        EValueLen = 64
    };

    TBridgeSettings();

    static TPtrC8 Key(TInt aField);
    static TPtrC Label(TInt aField);
    static TInt MaxLength(TInt aField);
    static TBool IsSecret(TInt aField);

    /** list line: "Label: value" */
    void ListText(TInt aField, TDes &aText) const;

    /** Takes the reply of "show" (key=value lines); unknown lines are skipped. */
    void Parse(const TDesC8 &aReply);

    void Set(TInt aField, const TDesC &aValue);

    TBool HasChanges() const;

    /** The commands that write the changed fields ("set <key> <value>"),
     *  followed by "save" if aSave.  Leaves with KErrTooBig if a value does
     *  not fit into a console line. */
    HBufC8 *BuildCommandsL(TBool aSave) const;

    void ClearChanges();

  public:
    TBuf<EValueLen> iValue[EFieldCount];
    TBool iDirty[EFieldCount];
    TBool iLoaded;
};

/* ------------------------------------------------------------------ */
/* one command session                                                 */
/* ------------------------------------------------------------------ */

class MBridgeClientObserver
{
  public:
    /** The session ended; aError is KErrNone on success. */
    virtual void BridgeFinished(TInt aError) = 0;
    /** Short status text for the display. */
    virtual void BridgeProgress(const TDesC &aText) = 0;
};

class CBridgeClient : public CBase, public MGcTransportObserver
{
  public:
    static CBridgeClient *NewL(MBridgeClientObserver &aObserver);
    ~CBridgeClient();

    /** Connects, runs the commands (one per line) and disconnects.
     *  aChooseBridge forgets the saved bridge and shows the device selection. */
    void StartL(const TDesC8 &aCommands, TBool aChooseBridge);
    void Abort();
    TBool IsBusy() const { return iBusy; }
    /** everything the bridge answered in the last session */
    const TDesC8 &Reply() const { return *iRx; }

  public: // MGcTransportObserver
    void TransportUp();
    void TransportData(const TDesC8 &aData);
    void TransportDown(TInt aError);
    void TransportSelecting(TBool aShowing);
    void TransportProgress(const TDesC8 &aStep, TInt aValue);

  private:
    CBridgeClient(MBridgeClientObserver &aObserver);
    void ConstructL();
    void SendNext();
    void Finish(TInt aError);
    static TInt NotifyCallback(TAny *aSelf);
    static TInt TimerCallback(TAny *aSelf);

    MBridgeClientObserver &iObserver;
    TGcConfig iConfig;
    CGcTransport *iTransport;
    CPeriodic *iTimer;
    CIdle *iNotifier;
    HBufC8 *iCommands;
    HBufC8 *iRx;
    TInt iNextPos;  /* next command to send, offset into iCommands */
    TInt iScanPos;  /* reply scanned up to here */
    TInt iEnds;     /* "END" lines received */
    TInt iExpected; /* "END" lines until done */
    TInt iIdleSecs;
    TInt iResult;
    TBool iBusy;
    TBool iSelecting;
    TBool iAwaitRestart; /* the last command restarts the bridge: no "END" */
};

#endif /* BRIDGECLIENT_H */
