# Protocol, version 1

This document describes all data exchanged between the N-Gage
(replacement DLL), the ESP32 bridge and the server. The N-Gage talks to
the bridge over Bluetooth serial (SPP), the bridge talks to the server
over TCP.

There are four layers:

| Layer | Purpose | Scope | Code |
|---|---|---|---|
| Framing | Separates frames, detects corruption | each connection | `protocol/src/gc_codec.c` |
| Link (GCL) | Reliable, ordered delivery, resume after loss | each connection | `protocol/src/gc_link.c` |
| Messages (GCM) | Registration, sessions, data | N-Gage to server | `protocol/src/gc_msg.c` |
| Session control (GCS) | Start, pause, connect between N-Gages | N-Gage to N-Gage | `dll/core/gc_dev.c` |

All parts use the same C code for the first three layers. Integers are
big endian. Strings are UTF-8 with a one byte length prefix and no
terminator. Names are at most 32 bytes.

This protocol replaces the earlier text based draft (UID, DID, NET and ROL
lines with a two byte header ending in 0x0A). That draft could not carry
binary game data containing 0x0A and had no checksums, no
acknowledgements, no way to recover from partial reads and no version.
Its information is still there: the game UID is part of HOST_OPEN and
JOIN, the device name and id are part of REGISTER, the role follows from
HOST_OPEN or JOIN, and the server address is now configured on the bridge.

## 1 Framing

A frame is sent as:

    0x00, COBS(body + CRC16(body)), 0x00

- COBS (Consistent Overhead Byte Stuffing) removes all 0x00 bytes from
  the encoded data, so 0x00 always marks a frame boundary. Any binary
  data can be sent.
- CRC16 is CRC-16/CCITT-FALSE (polynomial 0x1021, start value 0xFFFF, no
  reflection, no final XOR), sent big endian. The check value for
  "123456789" is 0x29B1.
- The leading 0x00 ends any partial frame still at the receiver.
- The maximum body size is 1100 bytes.

The receiver:

- collects bytes until 0x00, no matter how they arrive,
- ignores empty frames,
- drops frames with COBS errors, wrong checksum or no content,
- drops oversized data up to the next 0x00,
- drops the first frame if it started listening in the middle of one.

## 2 Link layer (GCL)

Each connection runs one link. The N-Gage connects to the bridge, the
bridge connects to the server. The server link uses plain TCP: CRC detects
accidental corruption but does not encrypt or authenticate traffic. The
optional shared access key is visible to anyone able to observe the TCP
connection. Use a trusted network or a separately secured tunnel; do not
expose this protocol directly to an untrusted network as a confidential or
tamper-resistant channel. The server defaults to loopback and requires an
explicit opt-in for non-loopback listeners.

### 2.1 Frame body

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | type |
| 1 | 1 | flags, bit 0 = ack field valid (always set) |
| 2 | 2 | seq, sequence number (DATA only, otherwise 0) |
| 4 | 2 | ack, next sequence number expected from the other side |
| 6 | n | payload |

| Type | Name | Payload |
|---|---|---|
| 0x01 | HELLO | handshake, see 2.2 |
| 0x02 | HELLO_ACK | handshake answer, see 2.2 |
| 0x03 | DATA | one message (up to 1094 bytes) |
| 0x04 | ACK | none |
| 0x05 | PING | 4 bytes |
| 0x06 | PONG | the PING payload, returned |
| 0x07 | CLOSE | reason (1 byte), ends the link |
| 0x08 | REJECT | reason (1 byte), handshake refused |

Reasons: 1 no common version, 2 bad HELLO, 3 timeout, 4 too many
retransmissions, 5 local close, 6 remote close, 7 connection lost,
8 protocol error.

### 2.2 Handshake

HELLO payload:

| Field | Size |
|---|---|
| 'G' 'C' | 2 |
| lowest supported version | 1 |
| highest supported version | 1 |
| role | 1 |
| link id | 4 |
| next expected sequence number | 2 |
| maximum message size | 2 |
| window | 1 |

HELLO_ACK has the same fields, with the chosen version in both version
fields, followed by one byte "resumed" (0 or 1).

- Role: 1 N-Gage, 2 bridge, 3 server, 9 test tool.
- The answering side picks the highest version both support. If there is
  none, it sends REJECT with reason 1. This document describes version 1.
- The link id is a random number chosen by the connecting side. The
  N-Gage chooses a new one for each GameComms object, the bridge one per
  boot.
- Resume: if the answering side still knows the link id, it answers
  resumed = 1 and both sides resend what the other has not acknowledged.
  Nothing is lost or duplicated. Otherwise it answers resumed = 0, both
  sides start new sequence numbers, and the layer above is told that the
  other side's state was lost.
- The connecting side repeats HELLO every second until it gets an answer.
  A repeated HELLO for a link that is already up is answered again.
- The link id is the only credential for resuming, so it must come from a
  cryptographically secure random source. The server accepts exactly one
  HELLO per transport connection (repeats of the same one excepted); a
  different second HELLO closes the connection. A link whose old connection
  is still open is only resumed from the same source address; otherwise the
  resume waits until the old connection has been declared dead.
- Once a CLOSE or REJECT has been processed, the rest of the same input is
  ignored. A REJECT of an established link is reported to the owner as
  closed.

### 2.3 Reliable delivery

- DATA frames carry consecutive 16 bit sequence numbers that wrap around.
- The receiver only accepts the next expected number. Older frames are
  duplicates and are dropped and acknowledged again. Newer frames mean a
  frame is missing, they are dropped and the sender resends.
- Every frame carries the acknowledgement. A separate ACK is sent after
  data was received.
- Each side advertises a message window in HELLO/HELLO_ACK (16 by default,
  4 on the ESP32). An updated sender limits its unacknowledged messages to the
  peer's advertised window; an ACK immediately flushes newly available slots.
  Older senders remain compatible but do not enforce the advertised window.
  A zero advertised window is treated as one because the window is negotiated
  only during the handshake and cannot otherwise reopen. The local queue can
  also hold up to 16 messages (4 on the ESP32), including messages waiting for
  a peer-window slot; beyond that, sending returns FULL.
- Lost frames are resent after a timeout (400 ms on Bluetooth, 1 s on
  TCP), doubling up to 3 s or 5 s.
- A receiver that cannot take more data does not acknowledge it, and the
  sender tries again later. The server applies this backpressure to routed
  game data when a destination's outbound queue is full, preserving the
  peer and message order while the destination catches up. Backpressure
  may reach the game, where a full send queue returns an overflow error.
  A prolonged stall can still exhaust retry or reconnect limits; this is
  not unlimited buffering and cannot restore a session after its peer has
  been removed.

### 2.4 Keep-alive and timeouts

| Connection | PING after no sending for | Link down after no receiving for |
|---|---|---|
| N-Gage and bridge | 2 s | 10 s |
| bridge and server | 5 s | 20 s |

When a link goes down, the connecting side reconnects and resumes with
the same link id. The server keeps a lost peer for 20 seconds. The bridge
keeps the N-Gage's state on the server for 15 seconds.

## 3 Messages (GCM)

The first byte of a DATA payload is the message type. Receivers ignore
extra bytes at the end, so later versions can add fields.

Direction: "dev" is the N-Gage, "srv" the server, "host" and "client" are
N-Gages in these roles.

### 3.1 Registration

| Type | Name | Direction | Fields |
|---|---|---|---|
| 0x10 | REGISTER | dev to srv | api (1 byte: 1 = GameComms 1.x, 2 = 2.x), variant (1), device id (6), flags (1), name (str), optional key (str) |
| 0x11 | REGISTERED | srv to dev | result (1), peer id (4), protocol version (1), server name (str) |

- The device id is the N-Gage's Bluetooth address. The DLL sends zeros
  and the bridge fills in the address. It identifies a device across
  reconnects.
- The bridge adds the access key if one is configured. The server refuses
  registration with REJECTED if the key is wrong.
- A REGISTER from a device that is already registered leaves its current
  session. A REGISTER with a device id that another peer uses removes the
  other peer.
- If the bridge had to start a fresh link to the server (server restart,
  timeout expired), it sends the stored REGISTER again. When the N-Gage
  gets a REGISTERED it did not ask for, it knows its session is gone.

### 3.2 Hosting

| Type | Name | Direction | Fields |
|---|---|---|---|
| 0x20 | HOST_OPEN | dev to srv | game uid (4), api (1), max players (1, 2 to 4), flags (1), name (str) |
| 0x21 | HOST_OPENED | srv to dev | result (1), session id (4) |
| 0x22 | HOST_CLOSE | host to srv | reason (1) |
| 0x23 | SESSION_STATE | host to srv | state (1): 0 lobby, 1 playing, 2 paused |
| 0x24 | PEER_REQUEST | srv to host | slot (1), rejoin (1), device id (6), name (str). Only with the CONFIRM flag. |
| 0x25 | PEER_ACCEPT | host to srv | slot (1), accept (1) |
| 0x26 | PEER_JOINED | srv to host | slot (1), rejoin (1), device id (6), name (str) |
| 0x27 | PEER_LEFT | srv to host | slot (1), reason (1) |
| 0x28 | KICK | host to srv | slot (1), reason (1): 1 temporary (keep the slot), 2 permanent |

HOST_OPEN flags: 0x01 CONFIRM (the server holds new players until the host
accepts them), 0x02 HIDDEN (not shown in host lists). The DLL handles
confirmation itself and does not use the CONFIRM flag.

Only sessions in the lobby accept new players. Sessions that are playing
or paused only accept the original live server peer rejoining into its
reserved slot. A matching device ID alone is not sufficient proof of
ownership; after a bridge TCP link drop, the old peer must resume within the
link grace period or the slot remains reserved until expiration.

### 3.3 Finding and joining a session

| Type | Name | Direction | Fields |
|---|---|---|---|
| 0x30 | DISCOVER | dev to srv | game uid (4), api (1) |
| 0x31 | SESSION_LIST | srv to dev | count (1, at most 12), more (1), then per session: session id (4), players (1), max players (1), state (1), host name (str) |
| 0x32 | JOIN | dev to srv | session id (4, 0 = first open session), game uid (4), api (1), rejoin (1; reserved-slot lookup hint only) |
| 0x33 | JOIN_RESULT | srv to dev | result (1), session id (4), slot (1), max players (1), host name (str) |
| 0x34 | LEAVE | dev to srv | reason (1): 0 leave, 1 temporary (keep the slot) |
| 0x35 | SESSION_ENDED | srv to client | reason (1) |

- Sessions are separated by game UID (the game's Symbian UID3) and API
  version. DISCOVER only lists sessions of the same game. Sessions of the
  other API version are shown as full. Joining a different game returns
  INCOMPATIBLE.
- The server assigns slots: the host is 0, clients get the lowest free
  slot from 1 to 3. The slot is the client id the game sees.
- A temporarily disconnected device can reclaim its reserved slot through
  the same live server peer, even if the session no longer accepts new
  players. A new peer with the same device ID cannot reclaim the slot.

### 3.4 Game data

| Type | Name | Fields |
|---|---|---|
| 0x40 | DATA | address (1), channel (1), payload (0 to 1024 bytes) |

- When sending, the address is the destination. A host sends to 1 to 3,
  or 0xFF for all clients. A client can only send to 0 (the host). When
  delivering, the server replaces the address with the sender's slot.
- Channel 0 is game data, channel 1 is session control (section 4).
- Order is kept for each sender and receiver pair. Data for players that
  are not present is dropped.

### 3.5 Bridge and errors

| Type | Name | Direction | Fields |
|---|---|---|---|
| 0x50 | BRIDGE_STATUS | bridge to dev | wifi (1), server (1): 0 down, 1 connecting, 2 up; signal strength (1), version (str) |
| 0x51 | BRIDGE_QUERY | dev to bridge | none, the bridge answers with BRIDGE_STATUS |
| 0x60 | ERROR | any | code (1), message type it refers to (1), text (str) |

When the server cannot be reached, the bridge answers by itself:
REGISTER, HOST_OPEN and JOIN get the result UNAVAILABLE, DISCOVER gets an
empty list.

### 3.6 Result codes

| Code | Name | Meaning | Error code in the game |
|---|---|---|---|
| 0 | OK | | KErrNone |
| 1 | PENDING | waiting for the host to confirm | |
| -1 | NOT_FOUND | no such session | KErrNotCommsHost (-1002) |
| -2 | FULL | | KErrInUse |
| -3 | INCOMPATIBLE | different game or API version | KErrIncompatibleGame (-1001) |
| -4 | CLOSED | game already running | KErrNotReady |
| -5 | REJECTED | refused by the host, or wrong access key | KErrCancel |
| -6 | NOT_REGISTERED | | |
| -7 | BUSY | | |
| -8 | UNAVAILABLE | bridge cannot reach the server | KErrCouldNotConnect |
| -9 | BAD_REQUEST | malformed or not allowed | |
| -10 | NOT_MEMBER | | |
| -11 | LIMIT | server limits reached | |

Leave reasons (PEER_LEFT, SESSION_ENDED, LEAVE, KICK): 0 left, 1 lost or
temporary, 2 kicked, 3 reservation expired, 4 host closed, 5 host lost.

## 4 Session control (GCS)

Session control messages travel as DATA on channel 1. The DLLs implement
everything that involves several devices themselves, with the host in
charge, just like the original library did over Bluetooth. The server
only forwards these messages.

### GameComms 1.x

| Byte | Name | Direction | Payload | Effect |
|---|---|---|---|---|
| 0x10 | START | host to clients | | StartMultiPlayerGame(KErrNone) |
| 0x11 | PAUSE | any | origin (1) | PauseMultiPlayerGame on all others. The host forwards it. |
| 0x12 | CONTINUE_REQ | client to host | | the client called ContinueMultiPlayerGame |
| 0x13 | CONTINUE | host to clients | | all players asked to continue: ContinueMultiPlayerGame everywhere |
| 0x14 | END | host to clients | reason (4) | HostDisconnected(KErrNone), then EndMultiPlayerGame(reason) |
| 0x15 | CLIENT_END | client to host | | ClientDisconnected, then ConnectedClientEndedGame, or END with KErrNotEnoughPlayers |
| 0x16 | TEMP_DISC | any | | Disconnect while paused: the slot is kept |
| 0x17 | READY | client to host | | the client called ReconnectL |
| 0x18 | RECONNECTED | host to client | | the host's ReconnectL reached this client: HostConnected(KErrNone) |
| 0x19 | SYNC | host to clients | game state (1) | reserved |

### GameComms 2.x

| Byte | Name | Direction | Effect |
|---|---|---|---|
| 0x30 | CONNECT | host to client | sent by the host's Connect. The client answers CONNECT_ACK and reports Connected(0, KErrNone). |
| 0x31 | CONNECT_ACK | client to host | the host reports Connected(id, KErrNone) |
| 0x32 | DISCONNECT | any | reason (4). The other side reports Disconnected(id, KErrNone). |

## 5 Examples

### 5.1 Starting a GameComms 1.x game with two players

1. The host game calls StartHostL(2, 2). The host DLL sends REGISTER and
   HOST_OPEN. The server opens a session and gives the host slot 0.
2. The client game calls StartClientL. The client DLL sends DISCOVER and
   shows the returned list (or joins the first game if AutoJoin is set).
3. The player picks the host. The client DLL sends JOIN and reports
   HostSelected.
4. The server answers JOIN_RESULT with slot 1 to the client, which reports
   HostConnected, and PEER_JOINED to the host, which reports
   ClientConnected.
5. Enough players are present. The host DLL sends SESSION_STATE playing
   and START to the client. Both report StartMultiPlayerGame.

### 5.2 Short TCP outage during a game

1. The bridge loses its TCP connection. The server keeps the peer for 20
   seconds and queues its data.
2. The bridge reconnects and sends HELLO with the same link id.
3. The server answers HELLO_ACK with resumed = 1. Both sides resend what
   was not acknowledged.
4. The N-Gage and the game do not notice anything.

### 5.3 What the game sees when something fails

| Failure | Detected by | Result for the game |
|---|---|---|
| corrupted or partial data | checksum, COBS | nothing, the data is resent |
| TCP outage under 20 s | link resume | nothing |
| bridge reboot | fresh link | session lost: HostDisconnected and EndMultiPlayerGame, ClientDisconnected or Disconnected |
| N-Gage out of Bluetooth range for more than 15 s | bridge sends LEAVE (lost) | the host gets ClientDisconnected(id, KErrDisconnected) and the end rules apply |
| server not reachable | bridge answers itself | HostConnected(KErrCouldNotConnect) or Connected(0, KErrCouldNotConnect). Hosts keep retrying. |
| different game | server | HostConnected(KErrIncompatibleGame) or Connected(0, KErrIncompatibleGame) |
| duplicate data | sequence numbers | dropped |
| malformed message | decoder | ERROR to the sender, the connection stays up |
