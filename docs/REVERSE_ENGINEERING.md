# Reverse engineering of GAMECOMMS.DLL

This document records how the eleven original `GAMECOMMS.DLL` variants
(`reference/gamecomms.v1.dll` … `v11.dll`) were analysed, what was found and
how certain each finding is.  Everything here can be reproduced with the
scripts in `tools/re/`.

Legend used throughout: **[V]** verified from the binary code, **[S]**
behaviour verified, name derived from the export sort order, **[D]** taken
from the Nokia SDK documentation (`sdk-docu/`), **[I]** inferred / assumed.

## 1 Method and tools

| Step | Tool | Notes |
|---|---|---|
| E32 image parsing | `tools/re/e32.py` | EKA1 uncompressed images: header, code/text sizes, export directory, import blocks, relocations |
| Import names | `tools/re/implib.py` | reads the SDK import libraries (`euser.lib`, `esock.lib`, …) to map imported ordinals to mangled names |
| Disassembly | `tools/re/dis.py` (llvm-objdump) | labels exports, import thunks, literal pools, `_LIT` strings |
| Decompilation | Ghidra 10.4 headless, `tools/re/ghidra/Decomp.java` | raw binary loader at `0x10000000`, ARM v5t, labels from the two scripts above, every vtable target added as function entry |
| Cross-variant matching | `tools/re/xmatch.py` | normalised instruction streams of each export compared between variants |
| Callback scan | `tools/re/notify_scan.py` | every virtual call through the game's notify object |

Facts about the images [V]:

* All variants: UID1 `0x10000079` (DLL), UID2 `0x1000008d`, UID3
  `0x101f5ee2`, code base `0x10000000`, no data/bss section (no writable
  static data), uncompressed, ARM (not Thumb) code built with GCC 2.9 for
  the `ARMI` ABI.  Note: the old CMake used UID1 `0x1000007a` (EXE).
* The import address table lies at `text_size`; each entry initially holds
  the ordinal.  Imports go through 16-byte thunks
  `ldr ip,[pc,#4]; ldr ip,[ip]; bx ip; .word iat`.
* Exports are code offsets; there are no names in the image.
* GCC 2.9 "vtable thunks" layout: two zero words, first virtual function at
  `+8`; `delete p` calls `vtable[+8](p, 3)`.

Embedded version tags (`@(#)N-Gage SDK vX.Y`) [V]:

| v1 | v2 | v3 | v4 | v5 | v6 | v7 | v8 | v9 | v10 | v11 |
|---|---|---|---|---|---|---|---|---|---|---|
| – | – | – | SDK v1.0 | SDK v2.6 | SDK v2.5 | SDK v2.5 | SDK v2.0 | SDK v1.0 | SDK v3.0 | SDK v3.1 |

## 2 Two API generations

The most important finding: **variant v1 and variants v2–v11 implement two
different libraries** behind the same file name and UID.

### 2.1 GameComms 1.x (v1, 33 ordinals, = SDK 1.0 beta API)

The API documented in `sdk-docu/Documentation/API_Documentation_GameComms.pdf`.

* Imports: `BLUETOOTH`, `BTEXTNOTIFIERS` (standard device selection
  dialog), `BTMANCLIENT`, `ESOCK`, `EUSER`, `SDPAGENT`, `SDPDATABASE` - no UI
  libraries [V].
* `CGameBTComms` (20 bytes): `+0` vptr (CBase), `+4` notify, `+8` game UID,
  `+0xc` log, `+0x10` role object (`CGameBTHost` or `CGameBTClient`, both
  derived from an abstract base that derives from `CActive`) [V].
* All 12 notify callbacks are called: vtable offsets `+0x08 … +0x34` in
  declaration order of the documented `MGameBTCommsNotify` [V].
* Export wrappers [V]: `KErrNotReady` (-18) if no role was started or the
  role does not fit (e.g. `SendDataToHost` on the host); `DisconnectClient(0)`
  returns `KErrBadHandle`; send functions return `KErrBadHandle` for an
  unknown id, `KErrGamePaused` (-1006) while paused, `KErrGameOver` (-1007)
  otherwise.
* Role mapping [V]: the role object reports "is client" (1/0); the wrapper
  maps it to `EIdle`/`EClient`/`EHost`.

### 2.2 GameComms 2.x (v2–v11, 55–63 ordinals)

An undocumented redesign, connection oriented instead of game-state
oriented.  Pause/continue/end and start/min players are gone; the game
manages them itself.

* Additional imports: `BAFL`, `BTDEVICE`, `BTENG` (Bluetooth power and
  name), `CONE`, `EIKCOCTL`, `EIKCORE` - the library brings its own device
  list control instead of the system dialog [V].
* `MGameBTCommsNotify` (2.x) has **five** pure virtual functions; the scan of
  every call site gives exactly the offsets `+0x08 … +0x18` in all ten
  variants [V].  Their meaning, from the call sites [V]:

| Offset | Our name | Arguments | Called when |
|---|---|---|---|
| `+0x08` | `ConfirmDevice` | `const TDeviceDetails&` | host in accept mode 1 without built-in UI: a client registers |
| `+0x0c` | `DeviceDeclared` | `TInt id, const TDeviceDetails&, TInt err` | after `DeclareRemoteDevice` (host: id = assigned client id; client: id 0); `KErrInUse` (client already connected), `KErrAlreadyExists` (host, declared twice) |
| `+0x10` | `Connected` | `TInt id, TInt err` | connection established or failed: `KErrIncompatibleGame` (SDP shows another game UID), `KErrNotCommsHost`, `KErrInUse`, retries exhausted |
| `+0x14` | `Disconnected` | `TInt id, TInt reason` | a connected peer went away / was disconnected |
| `+0x18` | `ReceiveData` | `TInt id, const TDesC8&` | data from a connected peer |

* `TDeviceDetails` is 560 bytes: a `TBTSockAddr` (40 bytes) followed by a
  `TBuf<256>` name at offset 40 - from the 40-byte register copy loop and
  the 520-byte `Mem::Copy` in `DeclareRemoteDevice`, and the `+40` name
  accesses everywhere [V].
* `CGameBTComms` 2.x (20 bytes v2–v9, 28 bytes v10/v11 with a
  discoverability helper at `+0x14` and a `CPeriodic` at `+0x18`) [V].
* Public API, ordinals identical in v2–v11 (`xmatch.py`: ordinals 1–55 have
  ≥ 95 % identical normalised code in v2–v9; the v10/v11 differences are
  recompilations with changed member offsets) [V]:

| Ord | Function | Notes |
|---|---|---|
| 27 | `NewL(MGameBTCommsNotify*, TUint32 aGameUid, RSGEDebugLog*)` | [V] |
| 52 | `~CGameBTComms()` | [V] |
| 40 | `StartHostL(TInt aShowList, CEikAppUi* aAppUi)` | panics `GC-MAIN 2` if `aShowList && !aAppUi`, `GC-MAIN 4` if Bluetooth is off [V]; with list + AppUi the host starts in accept mode 1 with its own list control, otherwise in mode 2 (not accepting) [V] |
| 39 | `StartClientL(TInt aShowList, CEikAppUi* aAppUi)` | same checks; with list it starts an inquiry and shows found devices [V] |
| 9 | `ConnectionRole()` | [V] |
| 6 | `ConnectState()` | 0/1/2 from the role object's state at `+0x38` [V] |
| 11 | `DeclareRemoteDevice(const TDeviceDetails&)` | host: accept a registered client; client: choose the host [V] |
| 7 | `Connect()` | client: SDP query, game UID check, registration with the host; host: stops accepting (mode 2) and connects every declared client in turn [V] |
| 8 | `Connect(TUint16 aClientId)` | host only (panic `GC-MAIN 1` otherwise) [V] |
| 14 | `Disconnect()` | everything, asynchronous via the role object's `RunL` [V] |
| 15 | `Disconnect(TUint16 aClientId, TBool aPermanently)` | host only; permanently also forgets the device [V] |
| 32 | `SendData(const TDesC8&)` | client: to host; host: to all clients [V] |
| 33 | `SendData(TUint16, const TDesC8&)` | host only [V] |
| 36 | `SetHostAcceptMode(TAcceptMode)` | 0 accept all, 1 confirm, 2 closed; `KErrNotReady` while connecting/connected [V] |
| 22 | `HostAcceptMode()` | [V] |
| 20 | `GetLocalDeviceName(THostName&)` | [V] |
| 2 | `BluetoothPowerState(TBool&)` | `CBTMCMSettings::GetPowerStateL` [V] |
| 34 | `SetBluetoothPowerState(TBool)` | `CBTMCMSettings::SetPowerStateL` [V] |
| 56 | `SetLocalDeviceName(THostName)` | v3+: `CBTMCMSettings::SetLocalBTName` [V] |
| 58 | `EndSession()` | v10+: deletes the role object [V] |
| 61 | `SetDiscoverabilityModeLimited(TBool)` | v10+: limited discoverable, refreshed every 55 s by a `CPeriodic` [V] |
| 59, 60, 63 | discoverability helpers | v10+, no arguments, `KErrGeneral` if the helper object is missing; exact names unknown [I] |

* The names `Connect`, `DeclareRemoteDevice`, `SetBluetoothPowerState`,
  `SetLocalDeviceName`, `BluetoothPowerState`, `Disconnect(TUint16, TBool)`,
  `EndSession`, `SendData`, `SetDiscoverabilityModeLimited`,
  `SetHostAcceptMode`, `SetSearchModeLimited` come from the Rifts (v10)
  source; their behaviour matches the code above.  The export order is the
  alphabetical order of the GCC 2.9 mangled names (`BluetoothPowerState`=2,
  `Cancel`=3, … `StartHostL`=40, `Write*`=41–49, destructors 50–54), with
  ordinal 1 (the `RSGEDebugLog` constructor) and 55 as exceptions [S].
* Internal helper classes are exported too (ordinals 3, 4, 10, 12, 13, 16–19,
  21, 23–26, 28, 31, 35, 38, 50, 51, 54, 55, 57, 62): a device list control
  (`GC-LIST` panics), a device discoverer (inquiry, `GC-DISCOVERER`) and an
  SDP resolver (`GC-RESOLVER`) [V].  The library uses them internally.  No
  evidence that games call them; see the compatibility strategy.

### 2.3 Original wire behaviour (2.x) - for reference only

The replacement does not talk to original DLLs, so the Bluetooth protocol
was only analysed as far as needed for the semantics [V]:

1. Host registers an SDP record with the game UID and an RFCOMM port.
2. Client `Connect()`: SDP query; mismatching UID → `Connected(0,
   KErrIncompatibleGame)`; failures retried up to 10 times.
3. Client connects to the host and sends its address/name plus the RFCOMM
   port it now listens on ("registration"); the host stores the client as
   a declared device (mode 0) or asks for confirmation (mode 1).
4. Host `Connect()` connects back to every declared client's port; only
   then do both sides report `Connected`.
5. Every logical connection uses two sockets; data are length framed.

### 2.4 RSGEDebugLog (all variants)

20-byte object, vptr at `+0x10`, logging level at `+0x0c` [V].  All shipped
variants were built without `SGE_DEV_PHASE`: `Open`, `NewLine`,
`WriteLine(...)`, `Write(...)` return `KErrNone`, `Close` does nothing [V].
Because games may embed the object, its size must not change.

## 3 Variant comparison

| | v1 | v2 | v3 | v4 | v5 | v6 | v7 | v8 | v9 | v10 | v11 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ordinals | 33 | 55 | 56 | 56 | 57 | 57 | 57 | 57 | 57 | 63 | 63 |
| API | 1.x | 2.x | 2.x | 2.x | 2.x | 2.x | 2.x | 2.x | 2.x | 2.x | 2.x |
| code size | 0x53f8 | 0x75e0 | 0x78b8 | 0x790c | 0x7c18 | 0x7c18 | 0x7c18 | 0x7c6c | 0x7d3c | 0x83b8 | 0x8470 |
| `SetLocalDeviceName` (56) | | | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| discoverer full refresh (57) | | | | | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `EndSession`, discoverability (58–63) | | | | | | | | | | ✓ | ✓ |

Pairwise differences that look large but are not functional [V]:

* v5/v6/v7: identical code; they differ only in the version tag (`2.6` vs
  `2.5`) and in import table layout (linked against different SDK library
  versions).
* v2→v3, v3/v4, v8/v9: 6–11 internal functions changed each (device table
  bookkeeping, inquiry filtering by Bluetooth device class, SDP/resolver
  error handling; the v5+ discoverer refresh uses
  `DeleteDevicesWithoutLinkKeyL`, v8 `DeleteAllDevicesL` + `GetSearchModeL`).
  The export wrappers and the notify call sites are unchanged, so the
  game-visible contract is the same.
* v10/v11: same functionality, recompiled (different register allocation).

Consequently one implementation per API generation is sufficient; the
variants only differ in their export tables, which the replacement
reproduces one-to-one.

## 4 Gaps in the earlier source

The previous code in `legacy/` (kept for reference):

* implemented only the 1.x API, and only as a prototype: role and connect
  state set immediately, `HostSelected`/`HostConnected` called synchronously
  from inside `StartClientL`, no pause/continue/end logic, no ids;
* would not work with any v2–v11 game (wrong vtable for every callback,
  wrong ordinals, wrong `StartHostL`/`StartClientL` signatures);
* used the line-based draft protocol (`\n` terminated, breaks on binary
  data containing `0x0A`, no integrity, fixed 64-byte queue entries);
* used UID1 `0x1000007a` instead of `0x10000079`;
* the ESP32 client printed received data instead of forwarding it, the
  server was an empty SDL2_net stub.

## 5 Open questions

| Topic | Status | How it is handled |
|---|---|---|
| exact names of v10 ordinals 59, 60, 63 and the helper-class methods | [I] | implemented as harmless functions returning `KErrNone`; no behaviour of the network design depends on them |
| first argument of 2.x `StartHostL`/`StartClientL` | only tested for non-zero [V] | treated as "show built-in list" flag |
| whether any game calls the exported helper classes directly | unknown, no game binaries were available | inert, memory-safe implementations with matching argument counts; checking the import tables of the actual games (see `docs/BUILD.md`, "Checking a game") settles it |
| callback order inside one event in 1.x (e.g. `HostDisconnected` before `EndMultiPlayerGame`) | [D] documentation | follows the SDK documentation; isolated in `dll/core/gc_dev.c` and covered by tests so it can be changed in one place |
