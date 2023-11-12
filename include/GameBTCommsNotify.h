/** @file GameBTCommsNotify.h
 *
 *  A GameComms re-implementation for the Nokia N-Gage.
 *
 *  Copyright (c) 2023, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#ifndef __GAMEBTCOMMSNOTIFY_H
#define __GAMEBTCOMMSNOTIFY_H

const TInt KUnknownClientId     = 0;       ///< Unknown client Id
const TInt KErrIncompatibleGame = (-1001); ///< Incompatible game error code
const TInt KErrNotCommsHost     = (-1002); ///< Not a GameComms host error code
const TInt KErrClientNotReady   = (-1003); ///< Client not ready error code
const TInt KErrHostQuit         = (-1004); ///< Host quit error code
const TInt KErrNotEnoughPlayers = (-1005); ///< Not enough players error code
const TInt KErrGamePaused       = (-1006); ///< Game paused error code
const TInt KErrGameOver         = (-1007); ///< Game over error code

/**
 * @class MGameBTCommsNotify
 *
 * @brief This is the notification interface provided by the library.
 *
 *        The application developer should provide a class implementing
 *        this interface with suitable handling for each callback
 *        method.  An object of that type can then be passed to the
 *        CGameBTComms object on creation; the framework will then call
 *        that object to handle each event.
 */
class MGameBTCommsNotify
{
public:
    /**
     * @name  ClientConnected
     *
     * @fn    virtual void ClientConnected(TUint16 aClientId, TDesC& aClientName, TInt aError) = 0;
     *
     * @brief
     *        Always called on the host application (with KErrNone ==
     *        aError) for each sucessful connection made with a
     *        client.  Also called with an error code on the host
     *        application when there is a connection error.  In the
     *        second case the interpretation is different depending upon
     *        the API called prior to receiving the callback:
     *
     *        1. When StartHostL() is called on the host application.
     *           If KErrNone != aError, the connection process is
     *           abandoned.  Any existing clients will be
     *           disconnected. The host application will receive no
     *           further notifications.
     *
     *        2. When ReconnectL(ETrue) is called on the host
     *           application (when a game is paused).  If KErrNone !=
     *           aError, the re-connection process is abandoned. Any
     *           connected clients will remain connected and ReconnectL
     *           can be called again to attempt re-connection to the
     *           others.
     *
     *        3. When ReconnectL(EFalse) is callled on the host
     *           application (when a game is paused) If KErrNone !=
     *           aError, the host will continue trying to reconnect to
     *           the other clients after a failure.  In this case the
     *           host application can receive a number of
     *           ClientConnected() callbacks where aError != KErrNone.
     *           It is the host application's responsibility to monitor
     *           the number of ClientConnected() notifications received,
     *           so the user can be informed the success of the
     *           reconnection process.
     *
     * @param aClientId   Indicates which client has now connected
     *                    (from 1 to CGameBTComms::KMaxPlayers - 1)
     *
     *                    If aError != KErrNone, aClientId may be
     *                    KUnknownClientId indicating there was a
     *                    connection error, but that the client id was
     *                    not known.  Note: if an error occurs, the host
     *                    automatically disconnects all sockets and no
     *                    further connections are possible.
     *                    CGameBTComms::StartHostL() must be called for
     *                    further connection attempts.
     *
     * @param aClientName If aError == KErrNone, aClientName will
     *
     *                    1. contain the BT Device name of the client
     *                       device, or
     *
     *                    2. have zero length if a client has been
     *                       reconnected to (using
     *                       CGameBTComms::ReconnectL)
     *
     *                    Otherwise aClientName will have zero length.
     *
     * @param aError
     *               - KErrNone If successful
     *
     *               - KErrCancel If user presses cancel before connection
     *
     *               - KErrNotReady
     *
     *               - Or any other standard EPOC error code
     *
     */
    virtual void ClientConnected(TUint16 aClientId, TDesC& aClientName, TInt aError) = 0;

    /**
     * @name  HostSelected
     *
     * @fn    virtual void HostSelected(TInt aError) = 0
     *
     * @brief Called on a client application when a host is selected by
     *        the user after calling StartClientL().
     *
     *        This notification is only really of value to let the
     *        application know that the user has canceled the host
     *        selection dialog.  In this case, KErrCancel == aError.
     *
     * @param aError
     *               - KErrNone   If successful
     *
     *               - KerrCancel If user presses cancel without
     *                            selecting a device
     */
    virtual void HostSelected(TInt aError) = 0;

    /**
     * @name  HostConnected
     *
     * @fn    virtual void HostConnected(TInt aError) = 0
     *
     * @brief Always called on each client application after
     *        StartClientL() is called (unless no device is selected).
     *
     *        Also called on each client after reconnection to the host
     *        using ReconnectL().  The connection is only successful if
     *        KErrNone == aError.
     *
     * @param aError
     *               - KErrNone                     If successful
     *               - KErrCancel                   If user presses cancel before connection
     *               - KErrIncompatibleGame         If host is running a different game
     *               - KErrIncompatibleCommsVersion If library version is not compatible
     *               - KErrNotCommsHost             If remote device is not actually a host
     */
    virtual void HostConnected(TInt aError) = 0;

    /**
     * @name  StartMultiPlayerGame
     *
     * @fn    virtual void StartMultiPlayerGame(TInt aError) = 0
     *
     * @brief Always called on all applications when the game can be
     *        started (KErrNone == aError).
     *
     *        This means that all the necessary connections are in place
     *        (i.e the number of requested players have been connected).
     *        Note that this is only called after StartHostL() and not
     *        ReconnectL().  If KErrNotEnoughPlayers == aError, this
     *        means that there has been a comms error immediately after
     *        all the clients have connected, and some clients have been
     *        disconnected, leaving not enough players to play the game.
     *
     * @param aError
     *
     *               - KErrNone if successful and the game could be
     *                          started, otherwise
     *
     *               - KErrNotEnoughPlayers if the game could not be
     *                 started because there are not enough players
     *                 caused by player(s) exiting before play started
     *                 or IO error(s).
     */
    virtual void StartMultiPlayerGame(TInt aError) = 0;

    /**
     * @name  ContinueMultiPlayerGame
     *
     * @fn    virtual void ContinueMultiPlayerGame() = 0
     *
     * @brief Always called on all applications when a game can be
     *        continued after it has been paused.
     *
     *        This will be called when enough connected applications
     *        (number of players >= the minimum required for the game)
     *        have called CGameBTComms::ContinueMultiplayerGame().
     */
    virtual void ContinueMultiPlayerGame() = 0;

    /**
     * @name  PauseMultiPlayerGame
     *
     * @fn    virtual void PauseMultiPlayerGame() = 0
     *
     * @brief Called on all applications except the one that called
     *        CGameBTComms::PauseMultiPlayerGame()
     *
     *        Indicates the game should be paused.  Attempting to call a
     *        SendDataxxx function whilst in the paused state will
     *        result in an error code of KErrGamePaused being returned.
     */
    virtual void PauseMultiPlayerGame() = 0;

    /**
     * @name  EndMultiPlayerGame
     *
     * @fn    virtual void EndMultiPlayerGame(TInt aReason) = 0
     *
     * @brief Notifies the game application that the game should be
     *        ended.
     *
     *        Called on all applications except the one that called
     *        CGameBTComms::EndMultiPlayerGame().  Currently all client
     *        applications (even the one that called
     *        CGameBTComms::EndMultiPlayerGame()) also receive a
     *        MGameBTCommsNotify::HostDisconnected notification
     *        beforehand.
     *
     *        Called in the following cases:
     *
     *        1. A client ends their game by calling
     *           CGameBTComms::EndMultiPlayerGame() and there are fewer
     *           players than the minimum remaining.
     *
     *        2. A client disconnects from the host whilst playing and
     *           there are fewer players than the minimum remaining.
     *
     *        3. The host application disconnects the client application
     *           via the DisconnectClient API and there are fewer
     *           players than the minimum remaining.
     *
     * @param aReason
     *
     *        - KErrHostQuit         The host has quit and so the game
     *                               cannot be played anymore
     *
     *        - KErrNotEnoughPlayers A client has quit such that the
     *                               number of players falls below the
     *                               minimum needed.
     *
     *        - KErrDisconnected     Disconnect() has been called
     *                               whilst playing or there has been a
     *                               comms error.
     */
    virtual void EndMultiPlayerGame(TInt aReason) = 0;

    /**
     * @name  ConnectedClientEndedGame
     *
     * @fn    virtual void ConnectedClientEndedGame(TUint16 aClientId) = 0
     *
     * @brief
     *        Notifies the host game application that a connected client
     *        has chosen to end their game in the following cases:
     *
     *        1. A client ends their game by calling
     *           CGameBTComms::EndMultiPlayerGame() and the minimum no
     *           of players are still present.
     *
     *        2. A client disconnects (because of an i/o error or
     *           Disconnect() called) from the host when the game is
     *           playing (and not paused) and the minimum no of players
     *           are still present.
     *
     *        3. The host application disconnects the client application
     *           via the DisconnectClient API and the minimum no of
     *           players are still present.
     *
     *        It is not called when:
     *
     *        1. The host application calls
     *           CGameBTComms::EndMultiPlayerGame()
     *
     *        2. A client's game ends and there are insufficient players
     *           left.
     *
     *        3. A client is disconnected from the host (via client or
     *           host initiation) and the game is in the paused state.
     *
     * @param aClientId Indicates which connected client has ended their
     *                  game
     */
    virtual void ConnectedClientEndedGame(TUint16 aClientId) = 0;

    /**
     * @name  ClientDisconnected
     *
     * @fn    virtual void ClientDisconnected(TUint16 aClientId, TInt aError) = 0
     *
     * @brief
     *        This is called on the host application when a client
     *        device has disconnected because:
     *
     *        1. An I/O Error occurred
     *
     *        2. A client called CGameBTComms::Disconnect()
     *
     *        3. A client called CGameBTComms::EndMultiPlayerGame() to
     *           end it's game
     *
     *        4. The game was ended on the host
     *
     *        It is not called when:
     *
     *        1. A DisconnectClient() call from the host application
     *           occurs.
     *
     *        2. A Disconnect() call from the host application occurs.
     *
     * @param aClientId Indicates which client has disconnected (from 1
     *                  to CGameBTComms::KMaxPlayers - 1)
     *
     * @param aError    KErrNone if disconnection isn't due to an error
     *                  e.g. another device receiving telephone call,
     *                  otherwise another error code.
     */
    virtual void ClientDisconnected(TUint16 aClientId, TInt aError) = 0;

    /**
     * @name  HostDisconnected
     *
     * @fn    virtual void HostDisconnected(TInt aError) = 0
     *
     * @brief
     *        This is called on a client application when the host has
     *        disconnected because:
     *
     *        1. An I/O Error occurred
     *
     *        2. The host ended the game (An EndMultiPlayerGame()
     *        notification will be received after also)
     *
     *        It is not called because of:
     *
     *        1. A Disconnect() call from the client application.
     *
     * @param aError KErrNone if disconnection is not due to an error
     *               e.g. another device receiving telephone call
     *
     */
    virtual void HostDisconnected(TInt aError) = 0;

    /**
     * @name  ReceiveDataFromClient
     *
     * @fn    virtual void ReceiveDataFromClient(TUint16 aClientId, TDesC8& aData) = 0
     *
     * @brief
     *        Notifies the game application that some application
     *        specific data has been received from a client game.
     *
     *        Note: the game must be in the `play` state.
     *
     * @param aClientId Indicates which client the data is received from
     *                  (from 1 to CGameBTComms::KMaxPlayers - 1)
     *
     * @param aData     Descriptor containing the data received.  Note
     *                  that this must be copied if it is to be used
     *                  outside the scope of this function.
     */
    virtual void ReceiveDataFromClient(TUint16 aClientId, TDesC8& aData) = 0;

    /**
     * @name  ReceiveDataFromHost
     *
     * @fn    virtual void ReceiveDataFromHost(TDesC8& aData) = 0
     *
     * @brief
     *        Notifies a client game application that some application
     *        specific data has been received from the host game.
     *
     *        Note: the game must be in the `play` state.
     *
     * @param aData Descriptor containing the data received.  Note that
     *              this must be copied if it is to be used outside the
     *              scope of this function.
     */
    virtual void ReceiveDataFromHost(TDesC8& aData) = 0;
};

#endif // __GAMEBTCOMMSNOTIFY_H
