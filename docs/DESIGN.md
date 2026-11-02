# Technical design

## 1 Goal and constraints

Unmodified N-Gage games call `GAMECOMMS.DLL` exactly as before.  Instead of
building a Bluetooth piconet with other N-Gages, the replacement DLL talks
to one ESP32 bridge over Bluetooth serial; the bridge carries the traffic
over Wi-Fi/TCP to an endpoint server that matches players of the same game
and routes their messages.  Up to four devices (one host, three clients)
play together from anywhere.

Constraints that shaped the design:

* **Binary compatibility** with 11 variants of two different APIs (see
  `REVERSE_ENGINEERING.md`), GCC 2.9 ABI, Symbian 6.1 without writable
  static data in DLLs, games with small stacks.
* The N-Gage only speaks Bluetooth 1.1 (legacy PIN pairing, RFCOMM/SPP).
* The ESP32 has ~150 KB free RAM once Bluetooth Classic and Wi-Fi run.
* Everything must be testable without hardware.

## 2 Components and interfaces

```
┌────────────── N-Gage ───────────────┐      ┌──────── ESP32 ────────┐      ┌──── server ────┐
│ game ─▶ ordinal thunks (GcExports)  │      │ main.cpp (Arduino)    │      │ gcs_main.c     │
│        CGameBTComms1 / 2            │      │  BT SPP │ WiFi/TCP    │      │  sockets, poll │
│        CGcEngine (active objects)   │ SPP  │  bridge_core.c        │ TCP  │  gcs_core.c    │
│        gcd_dev core (portable C)  ◀─┼──────┼─▶ gc_link ⇄ gc_link ◀─┼──────┼─▶ gc_link      │
│        gc_link / gc_msg / gc_codec  │      │  gc_msg               │      │  gc_msg        │
└─────────────────────────────────────┘      └───────────────────────┘      └────────────────┘
```

| Module | Language | Depends on | Interface |
|---|---|---|---|
| `protocol/` | C89 | nothing | `gc_codec.h` framing/CRC/COBS, `gc_link.h` reliable link, `gc_msg.h` messages |
| `dll/core/gc_dev.*` | C89 | protocol | API-level functions (`gcd_v1_*`, `gcd_v2_*`), byte I/O (`gcd_transport_*`), event queue (`gcd_pop_event`) |
| `dll/symbian/` | C++ (GCC 2.9) | core, Symbian | ordinal thunks → `CGameBTComms1/2` → `CGcEngine` (`MGcEventSink`, `MGcTransportObserver`) |
| `firmware/src/bridge_core.*` | C89 | protocol | `bridge_dev_*` (SPP side), `bridge_srv_*` (TCP side), `bridge_poll`, `bridge_io` callbacks |
| `firmware/src/main.cpp` | C++ Arduino | bridge_core | hardware glue, NVS config, console, web setup |
| `server/src/gcs_core.*` | C99 | protocol | `gcs_core_conn_open/input/closed/poll`, `gcs_io` callbacks |
| `server/src/gcs_main.c` | C99 | gcs_core | POSIX/Winsock front end, UDP LAN discovery |

Every core is a passive state machine: no threads, no blocking, no clock
of its own (time is passed in), no hidden I/O.  The same code therefore
runs in a Symbian active object, in Arduino `loop()`, in a `poll()` server
and in the deterministic simulator used by the tests.

## 3 Responsibilities

**DLL** - everything the game can observe: roles, ids, game/connect
states, callbacks and their order, GameComms 1.x game logic (start/min
players, pause/continue/end, reconnect), GameComms 2.x connection logic
(accept modes, declare/connect/disconnect), the built-in host list, the
bridge pairing.  The host is the authority for game logic, as in the
original.

**Bridge** - transport only: Bluetooth SPP acceptor towards the N-Gage,
TCP initiator towards the server, Wi-Fi management, server discovery,
reconnect with back-off, link resumption, forwarding with back-pressure,
device identity (fills the N-Gage's Bluetooth address into REGISTER), access
key, answering locally while the server is unreachable, releasing the
device's server state if the N-Gage disappears.  It never interprets game
traffic.

**Server** - registration, game-UID/API isolated session directory,
matchmaking (list or auto-join), slot assignment, routing (host ↔ clients,
broadcast), membership changes, reservation of slots for rejoin, timeouts,
resource limits.  It knows nothing about pause/continue.

## 4 State machines

### 4.1 Link (each hop)

```
IDLE ─start─▶ HELLO_SENT ─HELLO_ACK─▶ UP ─timeout/transport lost─▶ DOWN
               ▲   │ retry 1 s          │                           │
               │   └────────────────────┘◀── start (same link_id) ──┘
UP/DOWN ─CLOSE/REJECT/forget─▶ CLOSED
```
`UP_FRESH` vs `UP_RESUMED` tells the layer above whether the peer kept the
session.

### 4.2 Server: session and slot

```
session:  LOBBY ──SESSION_STATE playing/paused──▶ PLAYING/PAUSED ──▶ (closed)
           ▲  └───────────────── lobby ◀────────────────┘
slot:     EMPTY ─JOIN─▶ ACTIVE ─link lost (grace) / LEAVE(1) / KICK(1)─▶ RESERVED ─rejoin─▶ ACTIVE
            ▲    └─CONFIRM─▶ PENDING ─accept─▶ ACTIVE        │ reserve_ms
            └──────── LEAVE(0) / KICK(2) / lobby loss / expiry ┘
host lost or HOST_CLOSE → SESSION_ENDED to every client, session removed
```

### 4.3 DLL, GameComms 1.x

```
host:   IDLE ─StartHostL─▶ OPENING ─HOST_OPENED─▶ LOBBY ─players ≥ start─▶ PLAY ⇄ PAUSE
client: IDLE ─StartClientL─▶ SELECTING ─select─▶ JOINING ─JOIN ok─▶ CONNECTED ─START─▶ PLAY ⇄ PAUSE
PLAY/PAUSE ─End / host quit / not enough players / link lost─▶ OVER
PAUSE ─Disconnect─▶ temporarily disconnected ─ReconnectL (both sides)─▶ PAUSE
```
Game end rules follow the SDK documentation: a client leaving while
playing ends the game for everybody only if fewer than `aMinPlayers`
remain (`KErrNotEnoughPlayers`), otherwise the host gets
`ConnectedClientEndedGame`; while paused, disconnects are tolerated.

### 4.4 DLL, GameComms 2.x

```
host:   IDLE ─StartHostL─▶ session open, accept mode (0 all | 1 confirm | 2 closed)
        client joins ─▶ mode 0: declared │ mode 1: ConfirmDevice/UI ─DeclareRemoteDevice─▶ declared
        Connect() ─▶ mode 2, CONNECT to each declared client ─CONNECT_ACK─▶ Connected(id)
client: IDLE ─StartClientL─▶ (list) ─DeclareRemoteDevice─▶ declared ─Connect()─▶ CONNECTING (joined)
        ─host CONNECT─▶ CONNECTED ─Disconnect / host disconnect / loss─▶ NOT_CONNECTED
```

## 5 Compatibility strategy

* **One source, many export tables.**  `dll/variants/variants.py` describes
  every ordinal of every variant once and generates the `.def` files; the
  build produces one DLL per variant from the same sources (only
  `GC_VARIANT` differs).  Shared implementations get aliased export names.
* **ABI by construction.**  Exports are `extern "C"` thunks with the
  original register usage (`this` in r0), so the game's import ordinals
  land on functions with the right argument layout without depending on
  GCC 2.9 name mangling.  Objects handed to games derive from `CBase`, so
  `delete` through the vtable works.  The two notify interfaces and
  `TDeviceDetails`/`RSGEDebugLog` are declared with the verified layouts
  (`dll/symbian/GcPlatform.h`).
* **Asynchronous callbacks** from a `CAsyncCallBack`, never re-entrant from
  inside an API call; the game may call the API from its callbacks.
* **No writable static data**, no large stack buffers (events, scratch
  `TDeviceDetails` and file buffers live on the heap).
* **Isolation of uncertainty.**  Everything that is inferred rather than
  verified (callback order inside one event, names of inert helpers, the
  meaning of v10 helper ordinals) lives in one place each
  (`gc_dev.c`, `GcExports.cpp`) and is covered by tests, so evidence from
  real games can be applied without touching the rest.

## 6 Configuration

| Where | What | How |
|---|---|---|
| N-Gage | player name, bridge address/channel, auto-join, log | `E:\GameComms.ini` (optional; bridge address is saved after the first pairing) |
| ESP32 | Wi-Fi, server host/port or `auto`, access key, Bluetooth name/PIN | serial console or web page (`GameCommsBridge-Setup` access point) |
| Server | port, limits, timeouts, access key, logging | `gamecommsd.conf` or `--key=value` |

## 7 Security

The server is meant to be reachable from the Internet.  Input is
length-checked at every layer (fuzz-tested), sessions are isolated by game
UID, clients can only address the host, resource limits cap peers,
sessions and queued output, a slow peer is dropped instead of growing
memory, and an optional access key restricts who may register.  Traffic is
not encrypted (the N-Gage could not do TLS); use a VPN/WireGuard tunnel on
the bridge's network if that matters.
