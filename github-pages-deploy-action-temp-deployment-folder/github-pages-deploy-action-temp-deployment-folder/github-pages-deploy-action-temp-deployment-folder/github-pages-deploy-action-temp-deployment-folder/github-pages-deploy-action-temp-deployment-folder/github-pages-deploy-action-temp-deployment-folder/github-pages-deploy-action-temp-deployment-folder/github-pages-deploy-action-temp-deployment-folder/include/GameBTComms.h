/** @file GameBTComms.h
 *
 *  Class that provides games with Bluetooth comms.
 *
 *  Note that it is the game's responsibility to provide the UI to
 *  accompany this (with the exception of the BT device selection UI)
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef __GAMEBTCOMMS_H
#define __GAMEBTCOMMS_H

/**
 * @def VERSION
 *      Version of GameComms implementation.
 */
#define VERSION 0

#include <btsdp.h>
#include <e32base.h>
#include <e32std.h>
#include <es_sock.h>
#include "GameBTCommsConsts.h"
#include "MessageClient.h"

class MGameBTCommsNotify;
class RSGEDebugLog;
class CGameBTBase;

struct TBTCommsMsgBase;

/**
 * @name  Class CGameBTComms
 *
 * @class CGameBTComms
 *
 * @brief This is the main interface to the library exposing all
 *        multidevice games operations available to a developer.
 *
 *        This class is used to govern the client-host relationship and
 *        transfer data between devices.  It also provides functionality
 *        for common scenarios (e.g. Pause / Continue).
 */
class CGameBTComms : public CBase
{
public:
    /**
     * @name  NewL
     *
     * @fn    static CGameBTComms* NewL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog = NULL)
     *
     * @brief Creates a new CGameBTComms object.
     *
     * @param aEventHandler  Object implementing the MGameBTCommsNotify
     *                       interface.  It will receive callbacks when
     *                       events occur.
     *
     * @param aGameUID       Unique identifier representing the game to be
     *                       played.
     *
     * @param aLog           Debug log file.
     *
     * @return A new CGameBTComms object.
     *
     */
    IMPORT_C static CGameBTComms* NewL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog = NULL);
    IMPORT_C ~CGameBTComms();

public:
    enum                 { KMaxPlayers         = KBTMaxPlayers };
    enum                 { KServerConnectionId = KBTHostConnectionId };
    enum TConnectionRole { EIdle, EClient, EHost };
    enum TGameState      { EGameOver, EPlay, EPause };
    enum TConnectState   { ENotConnected, EConnecting, EConnected };
    enum TGameCommsState { EInit, ERegisterUID, ERegisterDeviceName, ERegisterNetConfig, ERegisterRole, EHandleMessages };
    enum                 { KQueueSize = 512 };

#if VERSION >= 9

    enum THostAcceptMode { EConfirmClients };

#endif /* VERSION >= 9 */

public:
    /**
     * @name  StartHostL
     *
     * @fn    void StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers)
     *
     * @brief Set device as a the host.
     *
     * The device is set as the host; any previous connections are
     * deleted.  The game (as represented by the game's unique Symbian
     * App UID3 value – defined in the game's .mmp file) is added to the
     * device's bluetooth advertising database, and the host starts
     * listeners for any connections from client devices.  The method is
     * non-blocking, and notification of the following events is
     * received asynchronously.
     *
     * Each time a client connects, MGameBTCommsNotify::ClientConnected
     * is called (on the host device).  When sufficient clients have
     * connected (i.e >= aStartPlayers),
     * MGameBTCommsNotify::StartMuliPlayerGame is called (on all
     * connected devices) to indicate that the game can be started.
     *
     * @param aStartPlayers Number of players required before the game can start
     *
     * @param aMinPlayers   Minimum number of players needed in game after
     *                      starting to continue playing.  If numbers
     *                      later fall below this,
     *                      MGameBTCommsNotify::EndMultiPlayerGame will
     *                      be called (on each connected device).
     */
    IMPORT_C void StartHostL(TUint16 aStartPlayers, TUint16 aMinPlayers);

    /**
     * @name  StartClientL
     *
     * @fn    void StartClientL()
     *
     * @brief Set the device as a client.
     *
     *        The device is set as a client; any previous connections
     *        are deleted.  A list of Bluetooth devices is presented to
     *        the user – by means of the standard series 60 device
     *        selection dialog.  When a host is selected (or if the user
     *        presses cancel), MGameBTCommsNotify::HostSelected is
     *        called (on this client device).  If selection was
     *        successful, an attempt is made to connect to the host
     *        device.  It is the responsibility of the game to notify
     *        the user that a connection attempt to the host is being
     *        made.
     *
     *        MGameBTCommsNotify::ClientConnected is called (on the host
     *        device) when a connection to the host is made, and
     *        MGameBTCommsNotify::HostConnected is called (on this
     *        client device), though this will also be called if an
     *        error occurred (including the user pressing cancel on the
     *        selection list).
     */
    IMPORT_C void StartClientL();

    /**
     * @name  TConnectionRole
     *
     * @fn    TConnectionRole ConnectionRole()
     *
     * @brief Indicates whether the device is the host or a client.
     *
     *        This is a simple synchronous method that makes no
     *        external request.
     *
     * @retval EHots   If StartHostL has been called (most recently)
     *
     * @retval EClient If StartClientL has been called (most recently)
     *
     * @retval EIdle   Otherwise (CGameBTComms has been just constructed)
     *
     */
    IMPORT_C TConnectionRole ConnectionRole();

    /**
     * @name  GameState
     *
     * @fn    TGameState GameState()
     *
     * @brief Indicates the state of the game.
     *
     *        This is a simple synchronous method that makes no external
     *        request.
     *
     * @retval EPlay     If the game is playing
     *
     * @retval EPause    If the game is paused
     *
     * @retval EGameOver If the game has ended (or has not started yet)
     */
    IMPORT_C TGameState GameState();

    /**
     * @name  ConnectState
     *
     * @fn    TConnectState ConnectState()
     *
     * @brief Indicates the state of the connection.
     *
     *        This is a simple synchronous method that makes no external
     *        request.
     *
     * @retval EConnected    If the necessary connections are in place:
     *                       1. On the host, all clients must be connected
     *                       2. On a client, must be connected to the host
     *
     * @retval ENotConnected If all necessary connections are not in place
     *
     * @retval EConnecting   In the process of a connection attempt
     */
    IMPORT_C TConnectState ConnectState();

    /**
     * @name  GetLocalDeviceName
     *
     * @fn    TInt GetLocalDeviceName(THostName& aHostName)
     *
     * @brief Gets the name from the user's Bluetooth settings
     *
     * @param aHostName Buffer to receive the name
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     */
    IMPORT_C TInt GetLocalDeviceName(THostName& aHostName);

#if VERSION < 9

    /**
     * @name  Disconnect
     *
     * @fn    void Disconnect()
     *
     * @brief When called from a client, the connection to the host is
     *        dropped.
     *
     *        MGameBTCommsNotify::HostDisconnected will be called on
     *        each client device.
     *
     *        If the game is paused, a client disconnecting does not end
     *        the game.  However, if the game is in play a client
     *        disconnecting will end the game for itself – if then there
     *        are too few players left,
     *        MGameBTCommsNotify::EndMultiPlayerGame, will be called (on
     *        each device) to indicate that the game should be ended,
     *        otherwise MGameBTCommsNotify::ConnectedClientEndedGame
     *        will be called on the host device.
     *
     *        If the game is playing, when Disconnect is called,
     *        MGameBTCommsNotify::EndMultiPlayerGame will be called (on
     *        the disconnected device).
     *
     *        Disconnect MUST be called when:
     *
     *        1. An application using the library goes into the
     *           background (override CAknAppUi::HandleForegroundEventL
     *           to detect when this occurs - NB see also
     *           IsShowingDeviceSelectionDlg).  This means that another
     *           application can used Bluetooth comms without the risk
     *           of interference – pause should be called first in this
     *           event so the game can be continued.
     *
     *        Note that PauseMultiPlayerGame should be called beforehand
     *        if the game is to be continued.
     */
    IMPORT_C void Disconnect();

#endif /* VERSION > 9 */

    /**
     * @name  DisconnectClient
     *
     * @fn    TInt DisconnectClient(TUint16 aClientId);
     *
     * @brief Disconnects the client specified.
     *
     *        This only has any effect if called from the host.  Note
     *        that MGameBTCommsNotify::ClientDisconnected is not called
     *        because the host application knows that the client is
     *        being disconnected.
     *
     *        If the game is paused, a client disconnecting does not end
     *        the game.  However, if the game is in play when a client
     *        is disconnected it's game will be ended – if there are too
     *        few players left, MGameBTCommsNotify::EndMultiPlayerGame,
     *        will be called (on each device) to indicate that the game
     *        should be ended, otherwise
     *        MGameBTCommsNotify::ConnectedClientEndedGame will be
     *        called on the host device.
     *
     *        If the game is playing, when Disconnect is called,
     *        MGameBTCommsNotify::EndMultiPlayerGame will be called (on
     *        the disconnected device).
     *
     * @param aClientId Id of client to disconnect (from 1 to
     *                  CGameBTComms::KMaxPlayers - 1)
     *
     * @return Any EPOC error code
     * @retval KErrNone If successful
     *
     */
    IMPORT_C TInt DisconnectClient(TUint16 aClientId);

    /**
     * @name  SendDataToClient
     *
     * @fn    TInt SendDataToClient(TUint16 aClientId, TDesC8& aData)
     *
     * @brief Performs a synchronous data write operation to the specified client.
     *
     *        This is only possible if called from the host while a game
     *        is playing.  MGameBTCommsNotify::ReceiveDataFromHost will
     *        be called (on the specified client).
     *
     * @param aClientId Id of client to disconnect (from 1 to
     *                  CGameBTComms::KMaxPlayers - 1)
     *
     * @param aData     Descriptor containing the data.  Note that the
     *                  length must be set (and MaxLength when using
     *                  TPtr8)
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone     If successful
     * @retval KErrPaused   If the game is in a paused state
     * @retval KErrGameOver If in a game over state
     *
     */
    IMPORT_C TInt SendDataToClient(TUint16 aClientId, TDesC8& aData);

    /**
     * @name  SendDataToAllClients
     *
     * @fn    TInt SendDataToAllClients(TDesC8& aData)
     *
     * @brief Performs a synchronous data broadcast operation to all connected
     *        clients
     *
     *        This is only possible if called from the host while a game
     *        is playing.  MGameBTCommsNotify::ReceiveDataFromHost will
     *        be called (on each client).
     *
     * @param aData Descriptor containing the data.  Note that the
     *              length must be set (and MaxLength when using TPtr8)
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone     If successful
     *
     * @retval KErrPaused   If the game is in a paused state
     *
     * @retval KErrGameOver If in a game over state
     *
     */
    IMPORT_C TInt SendDataToAllClients(TDesC8& aData);

    /**
     * @name  SendDataToHost
     *
     * @fn    TInt SendDataToHost(TDesC8& aData)
     *
     * @brief Performs a synchronous data write operation to the host.
     *
     *        This is only possible if called from a client while a game
     *        is playing.  MGameBTCommsNotify::ReceiveDataFromClient
     *        will be called (on the host).
     *
     * @param aData Descriptor containing the data.  Note that the
     *              length must be set (and MaxLength when using TPtr8)
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone     If successful
     *
     * @retval KErrPaused   If the game is in a paused state
     *
     * @retval KErrGameOver If in a game over state
     *
     */
    IMPORT_C TInt SendDataToHost(TDesC8& aData);

    /**
     * @name  ContinueMultiPlayerGame
     *
     * @fn    TInt ContinueMultiPlayerGame()
     *
     * @brief This is called to continue a multiplayer game with the
     *        same parties that was paused by receiving a
     *        MGameBTCommsNotify::PauseMultiPlayerGame.
     *
     *        If this device is not connected anymore ReconnectL must be
     *        called first (for example, because the application went
     *        into the background).
     *
     *        The host waits for all connected devices to call
     *        ContinueMultiPlayerGame().  It then sends a continue
     *        message to each device so that each device's game status
     *        is set to play and
     *        MGameBTCommsNotify::ContinueMultiPlayerGame is called on
     *        each connected device.
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     *
     */
    IMPORT_C TInt ContinueMultiPlayerGame();

    /**
     * @name  ReconnectL
     *
     * @fn    TInt ReconnectL(TBool aMustReconnectToAll = ETrue)
     *
     * @brief This is called to reconnect a disconnected device (for
     *        example after the application went into the background).
     *
     *        If this is a client, the device will wait for the host to
     *        reconnect, asynchronous notification being received via
     *        MGameBTCommsNotify::HostConnected.
     *
     *        If this is the host, all the clients that had previously
     *        joined are reconnected to, asynchronous notification being
     *        received via MGameBTCommsNotify::ClientConnected for each
     *        client.  In order for this to be successful, each client
     *        must be ready for reconnection.  Otherwise the error code
     *        KErrNotReady will be returned in
     *        MGameBTCommsNotify::ClientConnected.  This function can be
     *        called again, however, to reconnect to the remaining
     *        clients that were not previously ready.
     *
     *        In the event of a successfull callback,
     *        ContinueMultiPlayerGame should be called.  It is the
     *        responsibility of the game to display text or graphics to
     *        inform the user what is happenin.
     *
     * @param aMustReconnectToAll
     *                            - If ETrue the host will stop
     *                              reconnection attempts if it fails to
     *                              reconnect any client.
     *                            - If EFalse the host will attempt to
     *                              reconnect other clients after a
     *                              failed reconnect.
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone     If no initial errors
     *
     * @retval KErrNotReady If StartHostL or StartClientL has not been
     *                      called
     *
     */
    IMPORT_C TInt ReconnectL(TBool aMustReconnectToAll = ETrue);

    /**
     * @name  PauseMultiPlayerGame
     *
     * @fn    TInt PauseMultiPlayerGame()
     *
     * @brief Sets each device's game status to paused.
     *
     *        MGameBTCommsNotify::PauseMultiPlayerGame is then called
     *        (on each device but the caller) to instruct them to pause
     *        their game play.
     *
     *        This should be called when:
     *
     *        1. A user presses the back softkey during play, or
     *
     *        2. Prior to Disconnect() being called when the application
     *           goes into the background (override
     *           CAknAppUi::HandleForegroundEventL to detect when this
     *           occurs - NB see also IsShowingDeviceSelectionDlg()).
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone if successful
     *
     */
    IMPORT_C TInt PauseMultiPlayerGame();

    /**
     * @name  EndMultiPlayerGame
     *
     * @fn    TInt EndMultiPlayerGame();
     *
     * @brief This is called when a player no longer wishes to be part
     *        of the game (no option of continuing allowed).
     *
     *        If called from the host application, all devices will be
     *        notified to end their game play by calling
     *        MGameBTCommsNotify::EndMultiPlayerGame (on each device),
     *        and each device will be disconnected.
     *
     *        If called from the client player, the game will continue
     *        if there are sufficient players.
     *        MGameBTCommsNotify::ConnectedClientEndedGame will then be
     *        called on the host device.  If there are not enough
     *        players left, MGameBTCommsNotify::EndMultiPlayerGame will
     *        be called (on each device).
     *
     *        If called from the client player, the game will continue
     *        if there are sufficient players.
     *        MGameBTCommsNotify::ConnectedClientEndedGame will then be
     *        called on the host device.  If there are not enough
     *        players left, MGameBTCommsNotify::EndMultiPlayerGame will
     *        be called (on each device).
     *
     * @return Any EPOC error code
     *
     * @retval KErrNone If successful
     *
     */
    IMPORT_C TInt EndMultiPlayerGame();

    /**
     * @name  IsShowingDeviceSelectDlg
     *
     * @fn    TBool IsShowingDeviceSelectDlg();
     *
     * @brief Determine whether the Symbian device selection dialog is
     *        on display.
     *
     *        The client needs to know whether the Symbian device
     *        selection dialog is on display in their override of
     *        CAknAppUi::HandleForegroundEventL (see Disconnect).  This
     *        dialog causes the application that calls it to lose focus
     *        and so causes a call to HandleForegroundEventL. However is
     *        it very IMPORTANT that that user application only calls
     *        Disconnect if this dialog is NOT displayed.  When the
     *        dialog is displayed it is impossible to switch to another
     *        application, so CAknAppUi::HandleForegroundEventL will be
     *        called again if the game application loses focus for a
     *        different reason (such as receiving a phone call).
     *
     * @retval ETrue  If the device selection dialog is on display
     *
     * @retval EFalse Device selection dialog is not shown
     *
     */
    IMPORT_C TBool IsShowingDeviceSelectDlg();

#if VERSION >= 9

    IMPORT_C TInt Connect();                                          /* Expects KErrNone */
    IMPORT_C TInt DeclareRemoteDevice(const TDeviceDetails& aDevice); /* Expects KErrNone */
    IMPORT_C TInt SetBluetoothPowerState(TBool aState);               /* Expects KErrNone */
    IMPORT_C void SetLocalDeviceName(THostName aHostName);            /* Return type unclear */
    IMPORT_C void BluetoothPowerState(TBool aState);                  /* Return type unclear */
    IMPORT_C void Disconnect(TUint16 aClientId = 0, TBool aPermanently = EFalse);
    IMPORT_C void EndSession();                                       /* Return type unclear */
    IMPORT_C void SendData(TUint16 aClientId, TPtr8 aData);           /* Return type unclear */
    IMPORT_C void SetDiscoverabilityModeLimited(TBool aMode);         /* Return type unclear */
    IMPORT_C void SetHostAcceptMode(THostAcceptMode aMode);               /* Return type unclear */
    IMPORT_C void SetSearchModeLimited(TBool aMode);                  /* Return type unclear */

#endif /* VERSION >= 9 */

    void Update();

private:

protected:

    /**
     * @fn    CGameBTComms()
     *
     * @brief Default C++ Constructor.
     */
    CGameBTComms();

    /**
     * @fn    void ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog)
     *
     * @brief EPOC Constructor
     */
    void ConstructL(MGameBTCommsNotify* aEventHandler, TUint32 aGameUID, RSGEDebugLog* aLog);

protected:
    MGameBTCommsNotify* iNotify;  ///< Stores a pointer to the user object that will receive callbacks
    TUint32             iGameUID; ///< UID of the game to be played
    RSGEDebugLog*       iLog;     ///< Pointer to file of debug logging (if included in build!)
    CGameBTBase*        iComms;

private:
    TConnectionRole iConnectionRole;     ///< Connection role
    TConnectionRole iConnectionRoleTemp; ///< Connection role, temporary until registration is complete
    TConnectState   iConnectState;       ///< Connect state
    TGameState      iGameState;          ///< Game state
    TGameCommsState iGameCommsState;     ///< Current main state
    TUint16         iStartPlayers;       ///< Number of players required before the game can start
    TUint16         iMinPlayers;         ///< Minimum number of players needed in game after starting to continue playing
    CMessageClient* iClient;             ///< iClient the message sending engine

    TBufC8 <KQueueSize> iToClientQueue;
    TBufC8 <KQueueSize> iToHostQueue;
    TBufC8 <KQueueSize> iToAllQueue;

    char    iRecvBuffer[512];
    TUint16 iRecvLength;
};

#endif /* __GAMEBTCOMMS_H */
