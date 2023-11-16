/* Copyright (c) 2002, Nokia. All rights reserved */

#include "MessageClient.h"
#include "MessageServiceSearcher.h"
#include "BTPointToPoint.pan"

_LIT8(KMessage, "Hello world");

CMessageClient* CMessageClient::NewL()
    {
    CMessageClient* self = NewLC();
    CleanupStack::Pop(self);
    return self;
    }
    
CMessageClient* CMessageClient::NewLC()
    {
    CMessageClient* self = new (ELeave) CMessageClient();
    CleanupStack::PushL(self);
    self->ConstructL(KMessage);
    return self;
    }

CMessageClient::CMessageClient()
: CActive(CActive::EPriorityStandard),
  iState(EWaitingToGetDevice)
{
    CActiveScheduler::Add(this);
}

CMessageClient::~CMessageClient()
    {

	// Close() will wait forever for Read to complete
	if (iState == EConnected)
	{
		iSendingSocket.CancelRead();
	}
    Cancel();

    iSendingSocket.Close();
    iSocketServer.Close();

    delete iMessage;
    iMessage = NULL;

    delete iServiceSearcher;
    iServiceSearcher = NULL;
    }

void CMessageClient::ConstructL(const TDesC8& aMessage)
    {
    iServiceSearcher = CMessageServiceSearcher::NewL();

    iMessage = aMessage.AllocL();

	User::LeaveIfError(iSocketServer.Connect());

    }

void CMessageClient::DoCancel()
    {
    // no implementation required
    }

void CMessageClient::RunL()
    {
    if (iStatus != KErrNone)
        {
        switch (iState)
            {
            case EGettingDevice:
                if (iStatus == KErrCancel)
                    {
                    // No device selected;
                    }
                iState = EWaitingToGetDevice;
                break;
            case EGettingService:
            case EGettingConnection:
                // Connection error
                iState = EWaitingToGetDevice;
                break;
			case EConnected:
                // Lost connection
				DisconnectFromServerL();
				iState = EDisconnecting;
				break;
            case ESendingMessage:
                // Message Failed
				DisconnectFromServerL();
				iState = EDisconnecting;
                break;
			case EDisconnecting:
				if (iStatus == KErrDisconnected)
				{
					// Disconnection complete
					iSendingSocket.Close();
					iState = EWaitingToGetDevice;
				}
				else
				{
					// Failed to disconnect
					Panic(EBTPointToPointUnableToDisconnect);
				}
				break;
            default:
                Panic(EBTPointToPointInvalidLogicState);
                break;
            }
        }
    else 
        {
        switch (iState)
            {
            case EGettingDevice:
                // found a device now search for a suitable service
                iState = EGettingService;
                iStatus = KRequestPending; // this means that the RunL can not be called until
                                           // this program does something to iStatus
                iServiceSearcher->FindServiceL(iStatus);
                SetActive();
                break;
            case EGettingService:
                // Found service
                iState = EGettingConnection;
                ConnectToServerL();
                break;
            case EGettingConnection:
                // Connected
                iState = EConnected;
				// Catch disconnection event 
				// By waiting to read socket
                RequestData();
                break;
			case EConnected:
                // Data Recieved
				// Just dump data
				//iBuffer.Zero();
                RequestData();
				// Catch disconnection event 
				// By waiting to read socket
				break;
            case ESendingMessage:
                // Sent message
                iState = EConnected;
				// Catch disconnection event 
				// By waiting to read socket
                RequestData();
                break;
            case EDisconnecting:
                // Disconnection complete
				iSendingSocket.Close();
                iState = EWaitingToGetDevice;
                break;
            default:
                Panic(EBTPointToPointInvalidLogicState);
                break;
            };
        }
    }

void CMessageClient::ConnectL()
    {
    if (iState == EWaitingToGetDevice && !IsActive())
        {
        iState = EGettingDevice;
        iServiceSearcher->SelectDeviceByDiscoveryL(iStatus);
        SetActive();
        }
    else
        {
        // Client busy
        User::Leave(KErrInUse);
        }
    }

void CMessageClient::DisconnectL()
	{
	if ((iState == EConnected)||(iState == ESendingMessage))
	{
		DisconnectFromServerL();
		iState = EDisconnecting;
	}
	else
	{
        // No connection!
        User::Leave(KErrDisconnected);
	}
	}

void CMessageClient::DisconnectFromServerL()
	{
	// Terminate all operations
	iSendingSocket.CancelAll();
	Cancel();
  
	// Releasing connection
	iSendingSocket.Shutdown(RSocket::ENormal,iStatus);
	SetActive();

	}

void CMessageClient::ConnectToServerL()
    {
    // Connecting to service

	User::LeaveIfError(iSendingSocket.Open(iSocketServer, _L("RFCOMM")));

    TBTSockAddr address;
    address.SetBTAddr(iServiceSearcher->BTDevAddr());
    address.SetPort(iServiceSearcher->Port());

    iSendingSocket.Connect(address, iStatus);

#ifdef __WINS__
    User::After(1);     // Fix to allow emulator client to connect to server
#endif

    SetActive();
    }

void CMessageClient::RequestData()
{
    iSendingSocket.RecvOneOrMore(iBuffer, 0, iStatus, iLen);
    SetActive();
    }

void CMessageClient::SendMessageL(const TDesC8& aMessage)
    {
    if (iState != EConnected)
        {
        User::Leave(KErrDisconnected);
        }

	// Stop reading socket
	iSendingSocket.CancelRead();
	if (IsActive()) 
    {
        Cancel();
    }
    iMessage = aMessage.AllocL();
    iState   = ESendingMessage;
    iSendingSocket.Write(*iMessage, iStatus);
    SetActive();
    }

TBool CMessageClient::IsReadyToSendMessage()
    {
	return (iState == EConnected);
    }

TBool CMessageClient::IsConnected()
    {
    return ((iState == EConnected)||(iState == ESendingMessage));
    }

TBool CMessageClient::IsConnecting()
    {
    return ((iState == EGettingDevice)
		    ||
			(iState == EGettingService)
			||
			(iState == EGettingConnection));
    }

TBool CMessageClient::IsSendingMessage()
    {
    return (iState == ESendingMessage);
    }
