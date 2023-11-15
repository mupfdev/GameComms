/* Copyright (c) 2002, Nokia. All rights reserved */

#include "MessageServer.h"
#include "MessageProtocolConstants.h"
#include "MessageServiceAdvertiser.h"
#include "BTPointToPoint.pan"

static const TInt KListeningQueSize = 1;

CMessageServer* CMessageServer::NewL()
    {
    CMessageServer* self = NewLC();
    CleanupStack::Pop(self);
    return self;
    }
    
CMessageServer* CMessageServer::NewLC()
    {
    CMessageServer* self = new (ELeave) CMessageServer();
    CleanupStack::PushL(self);
    self->ConstructL();
    return self;
    }

CMessageServer::CMessageServer()
: CActive(CActive::EPriorityStandard),
  iState(EDisconnected)
    {
    CActiveScheduler::Add(this);
    }

CMessageServer::~CMessageServer()
    {
    TRAPD(err,StopL());
    if (err != KErrNone)
        {
        Panic(EBTPointToPointServerStop);
        }
    delete iAdvertiser;
    iAdvertiser = NULL;

    Cancel();
    }

void CMessageServer::ConstructL()
    {
    iAdvertiser = CMessageServiceAdvertiser::NewL();
    }

void CMessageServer::DoCancel()
    {
    }

void CMessageServer::RunL()
    {
    if (iStatus == KErrDisconnected)
        {
        // Disconnected so go back to listening
        // Disconnected
        StopL();
        return;
        }
    else if (iStatus == KErrAbort)
        {
        // Disconnected
        StopL();
        return;
        }
    else if (iStatus != KErrNone)
        {
        // Receiver Error
        StopL();
        return;
        }

    switch (iState)
        {
        case EConnecting:
            // Connected
            
            // do not accept any more connections
            iAdvertiser->UpdateAvailabilityL(EFalse);
            RequestData();
            iState = EWaitingForMessage;
            break;

        case EWaitingForMessage:
            {
            HBufC* text = HBufC::NewLC(iBuffer.Length());
            text->Des().Copy(iBuffer);

            CleanupStack::PopAndDestroy(text);
            RequestData(); //  Get more data
            }
            break;

        default:
            Panic(EBTPointToPointReceiverInvalidState);
            break;
        }
    }

void CMessageServer::StartL()
    {
    if (iState != EDisconnected)
        {
        User::Leave(KErrInUse);
        }

    User::LeaveIfError(iSocketServer.Connect());
    TInt result = iListeningSocket.Open(iSocketServer, KServerTransportName);
    if (result != KErrNone)
        {
        iSocketServer.Close();
        User::Leave(result);
        }

    //  Get a channel to listen on - same as the socket's port number
    TInt channel;
    User::LeaveIfError(
        iListeningSocket.GetOpt(KRFCOMMGetAvailableServerChannel, KSolBtRFCOMM, channel)
    );

    TBTSockAddr listeningAddress;
    listeningAddress.SetPort(channel);
    // Get port

    User::LeaveIfError(iListeningSocket.Bind(listeningAddress));
    User::LeaveIfError(iListeningSocket.Listen(KListeningQueSize));

    iAcceptedSocket.Close();    // close old connection - if any
    User::LeaveIfError(iAcceptedSocket.Open(iSocketServer));  // Open abstract socket

    iState = EConnecting;
    iListeningSocket.Accept(iAcceptedSocket, iStatus);
    SetActive();

    // Accept next connection

    SetSecurityOnChannelL(EFalse, EFalse, ETrue, channel);

    iAdvertiser->StartAdvertisingL(channel);
    iAdvertiser->UpdateAvailabilityL(ETrue);
    }

void CMessageServer::SetSecurityOnChannelL(TBool aAuthentication,
                                           TBool aEncryption, 
                                           TBool aAuthorisation,
                                           TInt aChannel)
    { 
    // a connection to the security manager
    RBTMan secManager;

    // a security session
    RBTSecuritySettings secSettingsSession;

    // define the security on this port
    User::LeaveIfError(secManager.Connect());
    CleanupClosePushL(secManager);
    User::LeaveIfError(secSettingsSession.Open(secManager));
    CleanupClosePushL(secSettingsSession);

    // the security settings 
    TBTServiceSecurity serviceSecurity(KUidBTPointToPointApp, KSolBtRFCOMM, 0);

    //Define security requirements
    serviceSecurity.SetAuthentication(aAuthentication);    
    serviceSecurity.SetEncryption(aEncryption); 
    serviceSecurity.SetAuthorisation(aAuthorisation);

    serviceSecurity.SetChannelID(aChannel);
    TRequestStatus status;
    secSettingsSession.RegisterService(serviceSecurity, status);
    
    User::WaitForRequest(status); // wait until the security settings are set
    User::LeaveIfError(status.Int());
    
    CleanupStack::PopAndDestroy();  //  secManager
    CleanupStack::PopAndDestroy();  //  secSettingsSession
    }
    
void CMessageServer::StopL()
    {
    if (iState != EDisconnected)
        {
        if (iAdvertiser->IsAdvertising())
            {
            iAdvertiser->StopAdvertisingL();
            }
        iAcceptedSocket.Close();
        iListeningSocket.Close();
        iSocketServer.Close();
        }
        iState = EDisconnected;
    }

void CMessageServer::RequestData()
    {
    iAcceptedSocket.RecvOneOrMore(iBuffer, 0, iStatus, iLen);
    SetActive();
    }

TBool CMessageServer::IsConnected()
    {
    return !(iState == EDisconnected);
    }
