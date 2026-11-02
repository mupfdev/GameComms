/** @file GcTransport.h
 *
 *  Bluetooth serial (RFCOMM/SPP) connection to the ESP32 bridge.
 *
 *  Connection sequence:
 *    1. bridge address from GameComms.ini, otherwise the standard
 *       Series 60 device selection dialog (result is saved)
 *    2. SDP lookup of the Serial Port service (or the configured channel)
 *    3. RFCOMM connect, then continuous RecvOneOrMore() reads
 *  Any failure is reported to the observer and retried by the engine.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef GCTRANSPORT_H
#define GCTRANSPORT_H

#include <e32base.h>
#include <es_sock.h>
#include <bt_sock.h>
#include "GcConfig.h"

class CMessageServiceSearcher;

class MGcTransportObserver
{
public:
    virtual void TransportUp() = 0;
    virtual void TransportData(const TDesC8& aData) = 0;
    virtual void TransportDown(TInt aError) = 0;
    /** the device selection dialog is shown (ETrue) / closed (EFalse) */
    virtual void TransportSelecting(TBool aShowing) = 0;
};

class CGcWriter;

class CGcTransport : public CActive
{
public:
    static CGcTransport* NewL(MGcTransportObserver& aObserver, TGcConfig& aConfig);
    ~CGcTransport();

    /** Start connecting (no-op while connecting/connected). */
    void ConnectL();
    /** Close the connection. */
    void Disconnect();
    /** Queue bytes for sending; returns KErrOverflow if the buffer is full. */
    TInt Write(const TDesC8& aData);
    TBool IsConnected() const { return iState == EConnected; }
    TBool IsBusy() const { return iState != EIdle; }

protected:
    void RunL();
    void DoCancel();
    TInt RunError(TInt aError);

private:
    CGcTransport(MGcTransportObserver& aObserver, TGcConfig& aConfig);
    void ConstructL();
    void ConnectSocketL(TInt aChannel);
    void Fail(TInt aError);

    enum TState { EIdle, ESelecting, ESearching, EConnecting, EConnected };

    MGcTransportObserver&    iObserver;
    TGcConfig&               iConfig;
    TState                   iState;
    RSocketServ              iSocketServ;
    RSocket                  iSocket;
    TBool                    iSocketOpen;
    CMessageServiceSearcher* iSearcher;
    CGcWriter*               iWriter;
    TBuf8<512>               iReadBuf;
    TSockXfrLength           iReadLen;
};

#endif /* GCTRANSPORT_H */
