# Design

## Goal

Unmodified N-Gage games call GAMECOMMS.DLL as before. Instead of
connecting to other N-Gages over Bluetooth, the replacement DLL connects
to one ESP32 bridge over Bluetooth serial (SPP). The bridge forwards the
traffic over Wi-Fi and TCP to a server. The server matches players of the
same game and forwards their data. Up to four devices (one host, three
clients) can play together.

## Constraints

- Binary compatibility with 11 variants of two different APIs (see
  REVERSE_ENGINEERING.md).
- Symbian 6.1: GCC 2.9 ABI, no writable static data in DLLs, small stacks.
- The N-Gage only supports Bluetooth 1.1 with PIN pairing.
- The ESP32 has little free memory while Bluetooth and Wi-Fi are running.
- Everything must be testable without hardware.

## Components

| Part | Language | Files | Role |
|---|---|---|---|
| Protocol | C89 | `protocol/` | Framing, reliable link, messages |
| DLL engine | C89 | `dll/core/gc_dev.c` | All GameComms logic, independent of Symbian |
| DLL adapter | C++ | `dll/symbian/` | Exports, Symbian objects, Bluetooth, timers, list UI |
| Bridge engine | C89 | `firmware/src/bridge_core.c` | Forwarding between Bluetooth and TCP |
| Bridge firmware | C++ (Arduino) | `firmware/src/main.cpp` | Hardware, Wi-Fi, settings, console, setup page |
| Server engine | C99 | `server/src/gcs_core.c` | Registration, sessions, routing |
| Server program | C99 | `server/src/gcs_main.c` | Sockets, configuration, LAN discovery |

The engines are plain state machines: no threads, no blocking calls and
no own clock (the current time is passed in). The same code therefore
runs in a Symbian active object, in the Arduino loop, in the server's
poll loop and in the test simulator.

## Responsibilities

DLL: everything the game can observe. Roles, client ids, game and
connection states, callbacks and their order, the 1.x game logic (start,
pause, continue, end, reconnect), the 2.x connection logic (accept modes,
declare, connect, disconnect), the host list and pairing with the bridge.
As in the original, the host decides about the game state.

Bridge: transport only. It accepts the Bluetooth connection from the
N-Gage, connects to the server, reconnects after failures, resumes
interrupted links, adds the N-Gage's Bluetooth address and the access key
to the registration, and answers locally while the server is unreachable.
It never looks at game data.

Server: registration, sessions separated by game UID and API version,
host list, slot assignment, forwarding (host to client, client to host,
host to all), rejoin, timeouts and resource limits. It knows nothing about
pause or continue.

## States

### Link (each connection)

- The connecting side sends HELLO until it gets HELLO_ACK. The link is
  then up.
- If no data arrives for too long, or the connection breaks, the link is
  down. The connecting side reconnects with the same link id and both
  sides resend what was not acknowledged.
- If the other side no longer knows the link id, a fresh link starts and
  the layer above is told that the other side's state was lost.

### Server session

- A session is in the lobby, playing or paused. New players can only
  join in the lobby.
- A slot is empty, pending (waiting for the host's confirmation), active
  or reserved.
- A slot is reserved when its player is lost or leaves temporarily. The
  same device can rejoin into it, also while playing. The reservation
  expires after a timeout.
- If the host leaves or is lost, the session ends for all clients.

### DLL, GameComms 1.x

- Host: StartHostL opens a session. When enough players have joined, the
  game starts. The game can then be paused and continued.
- Client: StartClientL shows the host list. After selecting a host the
  client joins and waits for the start.
- The game ends when a player ends it, the host leaves, too few players
  remain or the connection is lost.
- While paused, Disconnect disconnects temporarily and ReconnectL on both
  sides restores the game.

These rules follow the SDK documentation. A client leaving during a game
ends it for everybody only if fewer than the minimum number of players
remain (KErrNotEnoughPlayers). Otherwise the host gets
ConnectedClientEndedGame.

### DLL, GameComms 2.x

- Host: StartHostL opens a session in accept mode 0 (all), 1 (confirm) or
  2 (closed). Joining clients are declared directly (mode 0) or after
  ConfirmDevice and DeclareRemoteDevice (mode 1).
- Host Connect closes the session to new players and connects every
  declared client. Each side reports Connected when this succeeds.
- Client: StartClientL, then DeclareRemoteDevice with the chosen host,
  then Connect.
- Disconnect, a host disconnect or a lost connection report Disconnected.

## Compatibility approach

- One source, many export tables. `dll/variants/variants.py` describes
  every ordinal of every variant and generates the `.def` files. The
  build creates one DLL per variant from the same sources.
- Exports are `extern "C"` functions with the original register usage, so
  they do not depend on GCC 2.9 name mangling. Objects returned to games
  derive from CBase, so deleting them works as before.
- The callback interfaces and the data structures TDeviceDetails and
  RSGEDebugLog use the layouts found in the originals
  (`dll/symbian/GcPlatform.h`).
- Callbacks are always delivered asynchronously, never from inside an API
  call. Games may call the API from inside a callback.
- No writable static data and no large stack buffers.
- Things that are inferred rather than verified (see
  REVERSE_ENGINEERING.md) are kept in one place each and covered by
  tests, so they can be corrected easily.

## Configuration

| Where | What | How |
|---|---|---|
| N-Gage | player name, bridge address, auto join, log | `E:\GameComms.ini` (optional) |
| ESP32 | Wi-Fi, server, access key, Bluetooth name and PIN | serial console or setup web page |
| Server | port, limits, timeouts, access key, logging | `gamecommsd.conf` or command line |

## Security

The server can be reachable from the Internet. All input is length
checked at every layer and fuzz tested. Sessions are separated by game,
clients can only send to their host, resource limits cap the number of
peers, sessions and queued data, and slow peers are dropped. An optional
access key restricts who can register. Traffic is not encrypted, because
the N-Gage cannot do TLS. Use a VPN on the bridge's network if needed.
