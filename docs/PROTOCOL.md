# GameComms network protocol, version 1

This document specifies every byte that travels between the three
components:

```
 N-Gage (GAMECOMMS.DLL)  ──Bluetooth SPP──  ESP32 bridge  ──TCP──  endpoint server
        gcd_dev                                bridge_core              gcs_core
```

The protocol has three layers:

| Layer | Name | Scope | Implementation |
|---|---|---|---|
| 1 | **Framing** | per hop | `protocol/src/gc_codec.c` |
| 2 | **GCL** link layer | per hop (SPP hop, TCP hop) | `protocol/src/gc_link.c` |
| 3 | **GCM** messages | end-to-end device ⇄ server | `protocol/src/gc_msg.c` |
| 3a | **GCS** session control | end-to-end device ⇄ device, inside GCM `DATA` | `dll/core/gc_dev.c` |

All three components use the same C implementation of layers 1–3, so there
is exactly one definition of the wire format.  Integers are big-endian.
Strings are UTF-8, prefixed by a `u8` length, not terminated, at most 32
bytes for names.

It replaces the earlier draft (`UID:`/`DID:`/`NET:`/`ROL:` text lines and a
two byte header terminated by `0x0A`), which could not carry binary game
data containing `0x0A`, had no integrity check, no acknowledgements, no
way to resynchronise after a partial read and no versioning.  The draft's
information is preserved: `UID` → `HOST_OPEN.game_uid`/`JOIN.game_uid`,
`DID` → `REGISTER.name` + `device_id`, `ROL` → `HOST_OPEN`/`JOIN`, `NET`
moved to the bridge configuration (the N-Gage no longer needs to know the
server).

---

## 1 Framing

A frame on the byte stream is

```
0x00 | COBS( body | CRC16(body) ) | 0x00
```

* **COBS** (Consistent Overhead Byte Stuffing) removes every `0x00` from the
  encoded block, so `0x00` is an unambiguous delimiter.  Overhead is at
  most 1 byte per 254 bytes; any binary payload is safe.
* **CRC16** is CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`, no
  reflection, no final XOR), transmitted big-endian.  Check value of
  `"123456789"` is `0x29B1`.
* The leading `0x00` flushes a partial frame left at the receiver.
* Maximum body: 1100 bytes (`GC_FRAME_MAX_BODY`).

Receiver rules (`gc_deframer`):

| Situation | Action |
|---|---|
| bytes arrive in arbitrary fragments | accumulate until `0x00` |
| empty frame (`0x00 0x00`) | ignore (idle/keep-alive filler) |
| COBS decoding error, CRC mismatch, body < 1 byte | drop frame, count error |
| more than the maximum without delimiter | drop everything until the next `0x00` |
| receiver starts mid-frame | first (broken) frame is dropped, then in sync |

## 2 GCL - link layer

Every hop runs one GCL link.  The N-Gage is the **initiator** towards the
bridge, the bridge is the initiator towards the server.

### 2.1 Frame body

```
offset  size  field
0       1     type
1       1     flags     bit0: ack field valid (always set)
2       2     seq       sequence number (DATA only, else 0)
4       2     ack       next sequence number expected from the peer (cumulative)
6       n     payload
```

| type | name | payload |
|---|---|---|
| `0x01` | HELLO | handshake, see 2.2 |
| `0x02` | HELLO_ACK | handshake answer, see 2.2 |
| `0x03` | DATA | one GCM message (≤ 1094 bytes) |
| `0x04` | ACK | none (carries `ack`) |
| `0x05` | PING | 4 opaque bytes |
| `0x06` | PONG | echo of the PING payload |
| `0x07` | CLOSE | `u8 reason` - graceful, the session ends |
| `0x08` | REJECT | `u8 reason` - handshake refused |

Reason codes: `1` no common version, `2` bad HELLO, `3` timeout,
`4` retransmissions exhausted, `5` local close, `6` remote close,
`7` transport lost, `8` protocol violation.

### 2.2 Handshake and version negotiation

```
HELLO      'G' 'C' ver_min ver_max role link_id:u32 rx_next:u16 max_msg:u16 window:u8
HELLO_ACK  'G' 'C' ver     ver     role link_id:u32 rx_next:u16 max_msg:u16 window:u8 resumed:u8
```

* `role`: 1 device, 2 bridge, 3 server, 9 test harness.
* The acceptor picks `ver = min(own_max, peer_max)`; if that is below
  either side's minimum it answers REJECT(1).  This document is version 1.
* `link_id` is a random 32-bit identifier chosen by the initiator for the
  lifetime of its process (N-Gage: per CGameBTComms object; bridge: per
  boot).
* **Resumption**: if the acceptor still holds a session with the same
  `link_id`, it answers `resumed=1` and both sides retransmit everything
  the other has not acknowledged (`rx_next` tells the peer where to
  start).  Nothing is lost or duplicated.  Otherwise it answers `resumed=0`
  and both sides reset their sequence numbers; the layer above is told
  that the peer's state is unknown ("fresh").
* The initiator repeats HELLO every second until answered.  A duplicate
  HELLO for a link that is already up is answered again (lost HELLO_ACK).

### 2.3 Reliable delivery

* DATA frames carry consecutive 16-bit sequence numbers (wrapping).
* A receiver accepts only `seq == rx_next`; older frames are duplicates
  (dropped, re-acknowledged), newer frames indicate a gap (dropped, the
  sender will retransmit).  Delivery is therefore exactly-once and in order.
* Every frame carries the cumulative `ack`.  A pure ACK is sent after every
  input batch that delivered data.
* The sender keeps up to 16 unacknowledged messages (`GC_LINK_QUEUE`, 8 on
  the ESP32).  `gc_link_send()` reports `FULL` beyond that - this is the
  flow control towards the application.
* Retransmission: go-back-N after the RTO (400 ms on SPP, 1 s on TCP),
  doubling up to 3 s/5 s.  If retransmissions keep failing for twice the
  dead time the link is declared down.
* **Back-pressure**: a receiver whose application cannot take another
  message (bridge with a full forward queue) simply does not acknowledge
  it; the sender retries later.

### 2.4 Liveness

| Hop | keep-alive (PING after tx silence) | dead after rx silence |
|---|---|---|
| N-Gage ⇄ bridge | 2 s | 10 s |
| bridge ⇄ server | 5 s | 20 s |

When a link goes down the session is kept: the initiator reconnects the
transport and resumes with the same `link_id`.  The server keeps a silent
peer resumable for `link_grace_ms` (20 s), the bridge keeps the N-Gage's
server-side state for 15 s.

## 3 GCM - messages

The first byte of a DATA payload is the message type.  Decoders ignore
trailing bytes so later versions can append fields.

### 3.1 Registration

| Type | Name | Direction | Fields |
|---|---|---|---|
| `0x10` | REGISTER | dev → srv | `api:u8` (1 = GameComms 1.x, 2 = 2.x) `variant:u8` `device_id:6` `flags:u8` `name:str` [`key:str`] |
| `0x11` | REGISTERED | srv → dev | `result:i8` `peer_id:u32` `proto:u8` `server_name:str` |

* `device_id` is the N-Gage's Bluetooth address.  The DLL sends zeros; the
  bridge fills in the address of the connected N-Gage.  It identifies a
  device across reconnects (rejoin, duplicate removal).
* `key` is appended by the bridge when an access key is configured; the
  server refuses registration (`REJECTED`) if it does not match.
* A REGISTER from a device that is already registered leaves its current
  session.  A REGISTER carrying a `device_id` that another live peer uses
  removes that stale peer.
* **Unsolicited REGISTERED**: when the bridge's server link was re-created
  from scratch (server restart, grace expired) the bridge replays the
  stored REGISTER.  A device that receives REGISTERED while it believes to
  be in a session knows that the session is gone.

### 3.2 Hosting

| Type | Name | Direction | Fields |
|---|---|---|---|
| `0x20` | HOST_OPEN | dev → srv | `game_uid:u32` `api:u8` `max_players:u8` (2..4) `flags:u8` `name:str` |
| `0x21` | HOST_OPENED | srv → dev | `result:i8` `session_id:u32` |
| `0x22` | HOST_CLOSE | host → srv | `reason:u8` |
| `0x23` | SESSION_STATE | host → srv | `state:u8` 0 lobby, 1 playing, 2 paused |
| `0x24` | PEER_REQUEST | srv → host | `slot:u8` `rejoin:u8` `device_id:6` `name:str` (only with flag CONFIRM) |
| `0x25` | PEER_ACCEPT | host → srv | `slot:u8` `accept:u8` |
| `0x26` | PEER_JOINED | srv → host | `slot:u8` `rejoin:u8` `device_id:6` `name:str` |
| `0x27` | PEER_LEFT | srv → host | `slot:u8` `reason:u8` |
| `0x28` | KICK | host → srv | `slot:u8` `reason:u8` (`1` = temporary: keep the slot, `2` = permanent) |

HOST_OPEN flags: `0x01` CONFIRM_CLIENTS (server holds joiners as pending
until PEER_ACCEPT), `0x02` HIDDEN (not listed by DISCOVER).  The current
DLL implements confirmation itself and does not use the flag; the server
supports it for other clients.

Only `state = lobby` sessions accept new players.  `playing`/`paused`
sessions only accept rejoins of devices that already hold a (reserved)
slot.

### 3.3 Discovery and joining

| Type | Name | Direction | Fields |
|---|---|---|---|
| `0x30` | DISCOVER | dev → srv | `game_uid:u32` `api:u8` |
| `0x31` | SESSION_LIST | srv → dev | `count:u8` (≤ 12) `more:u8` then `count` × (`session_id:u32` `players:u8` `max:u8` `state:u8` `host_name:str`) |
| `0x32` | JOIN | dev → srv | `session_id:u32` (0 = first open session) `game_uid:u32` `api:u8` `rejoin:u8` |
| `0x33` | JOIN_RESULT | srv → dev | `result:i8` `session_id:u32` `slot:u8` `max_players:u8` `host_name:str` |
| `0x34` | LEAVE | dev → srv | `reason:u8` (`0` leave, `1` temporary - keep the slot) |
| `0x35` | SESSION_ENDED | srv → client | `reason:u8` |

* Sessions are isolated by **game UID** (the game's Symbian UID3) and
  **API family**.  DISCOVER only lists sessions of the same UID; sessions
  of the other API generation are shown as full.  JOIN of a session with
  another UID or API answers `INCOMPATIBLE`.
* The server assigns **slots**: host = 0, clients = lowest free of 1..3.
  The slot is the client id the game sees (`TUint16 aClientId`).
* A rejoining device (same `device_id`) gets its old slot back, also when
  the session no longer accepts new players.

### 3.4 Data

| Type | Name | Fields |
|---|---|---|
| `0x40` | DATA | `addr:u8` `channel:u8` `payload` (0..1024 bytes) |

* Sending: `addr` is the destination - host: 1..3 or `0xFF` (all clients);
  client: 0 (host) only.  Delivering: the server rewrites `addr` to the
  source slot.
* `channel` 0 is opaque game data, 1 is GCS session control (section 4).
* Ordering is preserved per source/destination pair; messages to members
  that are not present are dropped and counted.

### 3.5 Bridge and errors

| Type | Name | Direction | Fields |
|---|---|---|---|
| `0x50` | BRIDGE_STATUS | bridge → dev | `wifi:u8` `server:u8` (0 down, 1 connecting, 2 up) `rssi:i8` `version:str` |
| `0x51` | BRIDGE_QUERY | dev → bridge | – (bridge answers with BRIDGE_STATUS) |
| `0x60` | ERROR | any | `code:i8` `ref_type:u8` `text:str` |

When the server is not reachable the bridge answers requests itself:
REGISTER → REGISTERED(`UNAVAILABLE`), HOST_OPEN → HOST_OPENED(`UNAVAILABLE`),
JOIN → JOIN_RESULT(`UNAVAILABLE`), DISCOVER → empty SESSION_LIST.

### 3.6 Result codes

| code | name | meaning | DLL maps to |
|---|---|---|---|
| 0 | OK | | `KErrNone` |
| 1 | PENDING | waiting for host confirmation | – |
| -1 | NOT_FOUND | no such session | `KErrNotCommsHost` (-1002) |
| -2 | FULL | | `KErrInUse` |
| -3 | INCOMPATIBLE | other game UID / API | `KErrIncompatibleGame` (-1001) |
| -4 | CLOSED | game already running | `KErrNotReady` |
| -5 | REJECTED | host refused / bad access key | `KErrCancel` |
| -6 | NOT_REGISTERED | | |
| -7 | BUSY | | |
| -8 | UNAVAILABLE | bridge cannot reach the server | `KErrCouldNotConnect` |
| -9 | BAD_REQUEST | malformed / not allowed | |
| -10 | NOT_MEMBER | | |
| -11 | LIMIT | server limits reached | |

Leave reasons (PEER_LEFT, SESSION_ENDED, LEAVE, KICK): `0` leave,
`1` lost / temporary, `2` kicked, `3` reservation expired, `4` host closed,
`5` host lost.

## 4 GCS - session control (inside DATA channel 1)

GameComms semantics that involve several devices (start, pause, …) are
implemented by the DLLs themselves with the host as the authority, exactly
like the original library did over Bluetooth.  The server only routes.

### GameComms 1.x

| Byte | Name | Direction | Payload | Effect |
|---|---|---|---|---|
| `0x10` | START | h → c | – | `StartMultiPlayerGame(KErrNone)` |
| `0x11` | PAUSE | any | `origin:u8` | `PauseMultiPlayerGame()` on all but the origin (host relays) |
| `0x12` | CONTINUE_REQ | c → h | – | client called `ContinueMultiPlayerGame()` |
| `0x13` | CONTINUE | h → c | – | everybody asked: `ContinueMultiPlayerGame()` everywhere |
| `0x14` | END | h → c | `reason:i32` | `HostDisconnected(KErrNone)`, `EndMultiPlayerGame(reason)` |
| `0x15` | CLIENT_END | c → h | – | `ClientDisconnected`, then `ConnectedClientEndedGame` or END(`KErrNotEnoughPlayers`) |
| `0x16` | TEMP_DISC | any | – | `Disconnect()` while paused: peer is disconnected, slot kept |
| `0x17` | READY | c → h | – | client called `ReconnectL()` |
| `0x18` | RECONNECTED | h → c | – | host's `ReconnectL()` reached this client: `HostConnected(KErrNone)` |
| `0x19` | SYNC | h → c | `game_state:u8` | reserved for late synchronisation |

### GameComms 2.x

| Byte | Name | Direction | Effect |
|---|---|---|---|
| `0x30` | CONNECT | h → c | host's `Connect()`: client answers CONNECT_ACK and reports `Connected(0, KErrNone)` |
| `0x31` | CONNECT_ACK | c → h | host reports `Connected(id, KErrNone)` |
| `0x32` | DISCONNECT | any | `reason:i32`; peer reports `Disconnected(id, KErrNone)` |

## 5 Sequence examples

### 5.1 GameComms 1.x start-up (2 players)

```
Host DLL           bridge/server                     Client DLL
StartHostL(2,2)
 REGISTER  ───────────▶ REGISTERED
 HOST_OPEN ───────────▶ session 1234, slot 0
                                   ◀────────── StartClientL(): DISCOVER
                        SESSION_LIST ────────▶ list (UI or AutoJoin)
                                   ◀────────── JOIN 1234       HostSelected(0)
 PEER_JOINED slot 1 ◀── JOIN_RESULT OK slot 1 ▶             HostConnected(0)
ClientConnected(1,..)
 SESSION_STATE playing ▶
 DATA ch1 START ──────────────────────────────▶ StartMultiPlayerGame(0)
StartMultiPlayerGame(0)
```

### 5.2 Bridge-to-server TCP outage during a game

```
bridge: TCP lost → link DOWN (server keeps the peer for 20 s, queues output)
bridge: reconnects, HELLO link_id=same rx_next=n
server: HELLO_ACK resumed=1 rx_next=m, retransmits from m
bridge: retransmits from n
→ the N-Gage and the game notice nothing
```

### 5.3 Error handling summary

| Failure | Detected by | Result for the game |
|---|---|---|
| corrupted / partial bytes | CRC/COBS | none (retransmission) |
| TCP drop < 20 s | link resume | none |
| ESP32 reboot | fresh link (HELLO not resumable) | session lost: `HostDisconnected`+`EndMultiPlayerGame` / `ClientDisconnected` / `Disconnected` |
| N-Gage out of BT range > 15 s | bridge sends LEAVE(lost) | host: `ClientDisconnected(id, KErrDisconnected)` (+ end logic) |
| server unreachable | bridge answers locally | `HostConnected(KErrCouldNotConnect)` / `Connected(0, KErrCouldNotConnect)`; hosts keep retrying |
| incompatible game | server | `HostConnected(KErrIncompatibleGame)` / `Connected(0, KErrIncompatibleGame)` |
| duplicate messages | sequence numbers | dropped |
| malformed message | decoder | ERROR to sender, connection stays up |
