# Reverse engineering

This document describes how the eleven original GAMECOMMS.DLL variants
(`reference/gamecomms.v1.dll` to `v11.dll`) were analysed, what was found
and how certain each result is. The scripts are in `tools/re/`.

Confidence markers:

- [V] verified from the original code
- [S] behaviour verified, name taken from the export order
- [D] from the Nokia SDK documentation in `sdk-docu/`
- [I] inferred

## 1 Method

| Step | Tool |
|---|---|
| Parse the E32 images (header, exports, imports, relocations) | `tools/re/e32.py` |
| Map imported ordinals to names using the SDK import libraries | `tools/re/implib.py` |
| Disassemble with labels for exports, imports and strings | `tools/re/e32dis.py` (llvm-objdump) |
| Decompile | Ghidra 10.4 headless, `tools/re/ghidra/Decomp.java` |
| Compare exported functions between variants | `tools/re/xmatch.py` |
| Find all calls to the game's callback object | `tools/re/notify_scan.py` |

Facts about the images [V]:

- All variants: UID1 0x10000079 (DLL), UID2 0x1000008d, UID3 0x101f5ee2,
  code base 0x10000000, no writable data, uncompressed ARM code built with
  GCC 2.9 for the ARMI ABI. The old project used UID1 0x1000007a, which
  is the UID for EXE files.
- Imports go through small code stubs that load the address from an
  import table at the end of the code section.
- Exports are code offsets without names.
- GCC 2.9 vtables start with two zero words. The first virtual function
  is at offset 8. Deleting an object calls the function at offset 8 with
  the argument 3.

Version tags found in the files [V]:

| Variant | Tag |
|---|---|
| v1, v2, v3 | none |
| v4 | SDK v1.0 |
| v5 | SDK v2.6 |
| v6, v7 | SDK v2.5 |
| v8 | SDK v2.0 |
| v9 | SDK v1.0 |
| v10 | SDK v3.0 |
| v11 | SDK v3.1 |

## 2 Two different APIs

The most important finding: v1 and v2 to v11 are two different libraries
with the same file name and UID.

### 2.1 GameComms 1.x (v1, 33 exports)

This is the API documented in
`sdk-docu/Documentation/API_Documentation_GameComms.pdf`, the same as
the SDK 1.0 beta.

- Imports Bluetooth and socket libraries and the standard Bluetooth device
  selection dialog. No UI libraries. [V]
- CGameBTComms is 20 bytes: vtable, callback object, game UID, log object
  and a role object for host or client. [V]
- All 12 callbacks of MGameBTCommsNotify are used, at vtable offsets 0x08
  to 0x34 in the documented order. [V]
- Error handling of the exports [V]:
  - KErrNotReady (-18) if no role was started or the role does not fit,
    for example SendDataToHost on the host.
  - DisconnectClient(0) returns KErrBadHandle.
  - Send functions return KErrBadHandle for an unknown id, KErrGamePaused
    (-1006) while paused and KErrGameOver (-1007) after the game ended.

### 2.2 GameComms 2.x (v2 to v11, 55 to 63 exports)

An undocumented redesign that manages connections instead of game states.
Start, minimum players, pause, continue and end are gone. Games handle
these themselves.

- Additional imports for Bluetooth power and name settings and for UI
  controls. The library has its own device list instead of the system
  dialog. [V]
- The callback interface has five functions, at vtable offsets 0x08 to
  0x18 in all ten variants. [V]

| Offset | Our name | Arguments | Called when |
|---|---|---|---|
| 0x08 | ConfirmDevice | const TDeviceDetails& | host in accept mode 1 without the built-in list: a client wants to join |
| 0x0c | DeviceDeclared | TInt id, const TDeviceDetails&, TInt err | after DeclareRemoteDevice. Errors: KErrInUse (client already connected), KErrAlreadyExists (host, declared twice) |
| 0x10 | Connected | TInt id, TInt err | connection made or failed: KErrIncompatibleGame, KErrNotCommsHost, KErrInUse, retries used up |
| 0x14 | Disconnected | TInt id, TInt reason | a peer went away or was disconnected |
| 0x18 | ReceiveData | TInt id, const TDesC8& | data from a connected peer |

- TDeviceDetails is 560 bytes: a 40 byte TBTSockAddr followed by a
  TBuf<256> name at offset 40. [V]
- CGameBTComms is 20 bytes in v2 to v9 and 28 bytes in v10 and v11, which
  add a discoverability helper and a timer. [V]
- The public functions have the same ordinals in v2 to v11. Ordinals 1 to
  55 have at least 95 percent identical code in v2 to v9. The differences
  in v10 and v11 come from recompilation. [V]

| Ordinal | Function | Notes |
|---|---|---|
| 27 | NewL(MGameBTCommsNotify*, TUint32 aGameUid, RSGEDebugLog*) | [V] |
| 52 | ~CGameBTComms() | [V] |
| 40 | StartHostL(TInt aShowList, CEikAppUi* aAppUi) | Panics GC-MAIN 2 if aShowList is set without aAppUi, GC-MAIN 4 if Bluetooth is off. With list and AppUi the host starts in accept mode 1 with its own list, otherwise in mode 2. [V] |
| 39 | StartClientL(TInt aShowList, CEikAppUi* aAppUi) | Same checks. With list it searches for devices and shows them. [V] |
| 9 | ConnectionRole() | [V] |
| 6 | ConnectState() | 0, 1 or 2 [V] |
| 11 | DeclareRemoteDevice(const TDeviceDetails&) | Host: accept a client. Client: choose the host. [V] |
| 7 | Connect() | Client: checks the game UID and registers with the host. Host: stops accepting and connects every declared client. [V] |
| 8 | Connect(TUint16 aClientId) | host only, panics GC-MAIN 1 otherwise [V] |
| 14 | Disconnect() | disconnects everything, asynchronously [V] |
| 15 | Disconnect(TUint16 aClientId, TBool aPermanently) | host only. Permanently also forgets the device. [V] |
| 32 | SendData(const TDesC8&) | client: to the host, host: to all clients [V] |
| 33 | SendData(TUint16, const TDesC8&) | host only [V] |
| 36 | SetHostAcceptMode(TAcceptMode) | 0 accept all, 1 confirm, 2 closed. KErrNotReady while connecting or connected. [V] |
| 22 | HostAcceptMode() | [V] |
| 20 | GetLocalDeviceName(THostName&) | [V] |
| 2 | BluetoothPowerState(TBool&) | [V] |
| 34 | SetBluetoothPowerState(TBool) | [V] |
| 56 | SetLocalDeviceName(THostName) | v3 and later [V] |
| 58 | EndSession() | v10 and later, deletes the role object [V] |
| 61 | SetDiscoverabilityModeLimited(TBool) | v10 and later, refreshed every 55 seconds by a timer [V] |
| 59, 60, 63 | discoverability helpers | v10 and later, no arguments, KErrGeneral if the helper is missing. Exact names unknown. [I] |

- Function names such as Connect, DeclareRemoteDevice, EndSession,
  SendData, SetHostAcceptMode and SetDiscoverabilityModeLimited come from
  the Rifts (v10) source and match the code. The export order is the
  alphabetical order of the GCC 2.9 mangled names, except for ordinals 1
  and 55. [S]
- Internal helper classes are exported too: a device list control, a
  device discoverer and a service resolver (ordinals 3, 4, 10, 12, 13, 16
  to 19, 21, 23 to 26, 28, 31, 35, 38, 50, 51, 54, 55, 57, 62). The library
  uses them internally. There is no evidence that games call them. [V]

### 2.3 Original Bluetooth behaviour (2.x)

The replacement does not talk to the original DLLs, so this was only
analysed as far as needed to understand the behaviour [V]:

1. The host publishes a Bluetooth service record with the game UID.
2. On Connect, the client reads the record. A different game UID gives
   Connected(0, KErrIncompatibleGame). Failures are retried up to 10 times.
3. The client connects to the host and sends its address, name and
   listening port. The host declares the client (mode 0) or asks for
   confirmation (mode 1).
4. The host's Connect connects back to every declared client. Only then
   do both sides report Connected.
5. Each connection uses two sockets, data is sent with a length prefix.

### 2.4 Discoverer and resolver (2.x)

Found in the v8 code [V]; the other 2.x variants have the same exports.

- `CGameBTDiscoverer::NewL(observer, log)` creates an active object. The
  observer has one function at vtable +0x08:
  `(const TDeviceDetails& aDevice, TInt aError)`.
- `StartL()` panics `GC-DISCOVERER` if a search is running, otherwise runs a
  Bluetooth inquiry. Every device whose class matches the filter (default:
  major service class 0x200, phone, cellular) is reported with KErrNone.
  At the end the observer gets an empty TDeviceDetails with KErrEof (-25).
- `Cancel()` returns KErrNotReady when idle; otherwise the search ends with
  the same final KErrEof report. `IsIdle()` is true when no search runs.
- Ordinal 13 returns a reference to the class filter, ordinal 35 sets it.
- `CGameBTResolver::NewL(observer, game uid)`; `Start(const TDeviceDetails&)`
  queries the device's service records. The observer (vtable +0x08) gets
  `(const TDeviceDetails&, TInt aChannel, TInt aError)`: KErrNone with the
  RFCOMM channel, KErrNotFound (-1) if the device has no matching record,
  or the error.

### 2.5 RSGEDebugLog (all variants)

A 20 byte object with the vtable at offset 0x10 and the log level at 0x0c.
All shipped variants were built with logging disabled: Open, NewLine,
WriteLine and Write return KErrNone and Close does nothing. Games may
embed the object, so its size must stay the same. [V]

## 3 Variant comparison

| Variant | Exports | API | Code size |
|---|---|---|---|
| v1 | 33 | 1.x | 0x53f8 |
| v2 | 55 | 2.x | 0x75e0 |
| v3 | 56 | 2.x | 0x78b8 |
| v4 | 56 | 2.x | 0x790c |
| v5 | 57 | 2.x | 0x7c18 |
| v6 | 57 | 2.x | 0x7c18 |
| v7 | 57 | 2.x | 0x7c18 |
| v8 | 57 | 2.x | 0x7c6c |
| v9 | 57 | 2.x | 0x7d3c |
| v10 | 63 | 2.x | 0x83b8 |
| v11 | 63 | 2.x | 0x8470 |

Added exports: SetLocalDeviceName (56) from v3, a discoverer refresh (57)
from v5, EndSession and discoverability functions (58 to 63) in v10 and
v11.

Other differences [V]:

- v5, v6 and v7 have identical code. Only the version tag and the import
  table layout differ.
- Between v2 and v3, v3 and v4, and v8 and v9, between 6 and 11 internal
  functions changed (device bookkeeping, device search filtering, error
  handling). The exported functions and the callback calls did not
  change, so games see the same behaviour.
- v10 and v11 have the same functions, recompiled.

So one implementation per API is enough. The variants only differ in
their export tables, which the replacement reproduces exactly.

## 4 Problems in the earlier code

The previous code, now in `legacy/`:

- implemented only the 1.x API, as a prototype: role and state were set
  immediately, callbacks were called from inside StartClientL, there was
  no pause, continue or end logic and no client ids,
- would not work with any v2 to v11 game (wrong callbacks, wrong
  ordinals, wrong StartHostL and StartClientL arguments),
- used the line based draft protocol, which breaks on binary data,
- used the wrong UID1 (0x1000007a instead of 0x10000079),
- the ESP32 client printed received data instead of forwarding it, and
  the server was an empty stub.

## 5 Open questions

| Question | Status | How it is handled |
|---|---|---|
| Exact names of v10 ordinals 59, 60, 63 and of the helper class methods | [I] | Implemented as harmless functions returning KErrNone. Nothing depends on them. |
| Meaning of the first argument of 2.x StartHostL and StartClientL | only non-zero tested [V] | Treated as "show the built-in list". |
| Whether any game calls the exported helper classes | Ashen (v8) uses the discoverer for its own host search (seen on hardware) | Discoverer and resolver implemented on top of the server's session list (see 2.4). The device list is still inert. |
| Order of callbacks within one event in 1.x, for example HostDisconnected before EndMultiPlayerGame | [D] | Follows the SDK documentation. Kept in one place in `dll/core/gc_dev.c` and covered by tests. |
