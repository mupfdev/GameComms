# Compatibility report

One replacement `GAMECOMMS.DLL` is built per original variant
(`build/dll/out/vN/gamecomms.dll`).  Each has the original UIDs, the same
number of ordinals and every ordinal bound to an implementation of the
original function (`dll/variants/variants.py`, checked by
`tests/compat/check_variants.py` against the original binaries).

Confidence: **V** verified from the original code, **S** behaviour verified
+ name from sort order, **I** inferred.  Status: **Full** = implemented on
the network transport with the original semantics; **Inert** = present and
memory-safe but without function (see notes).

## 1 Variants and games

| Variant | md5 | Tag | API | Ordinals | Games |
|---|---|---|---|---|---|
| (v0) | cb326c500bdb795494d55d30196b8711 | SDK 1.0 beta | 1.x | 33 | – (SDK only, identical API to v1) |
| v1 | 79c25cdd1972c287712475ee544c7bf3 | – | 1.x | 33 | Marcel Desailly Pro Soccer |
| v2 | 8863fdea897f1c63807ad69252a3566f | – | 2.x | 55 | MotoGP, Pandemonium!, Puyo Pop, Puzzle Bobble VS, Tomb Raider, Tony Hawk's Pro Skater |
| v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | – | 2.x | 56 | Asphalt Urban GT, FIFA Football 2004, NCAA Football 2004, Rayman 3, Ghost Recon: Jungle Storm |
| v4 | c2f184012cd0635f3065b7d5042702ce | SDK v1.0 | 2.x | 56 | The Sims Bustin' Out |
| v5 | e8d317204a763d71215680d567eb71e0 | SDK v2.6 | 2.x | 57 | Catan, Mile High Pinball, Pathway to Glory, Requiem of Hell, KOF Extreme, Splinter Cell: Chaos Theory |
| v6 | bb02377c5361597f8d8503f5de584845 | SDK v2.5 | 2.x | 57 | Call of Duty, FIFA 2005, High Seize, Pathway to Glory: Ikusa Islands, SSX Out of Bounds, WWE Aftershock, Worms World Party, X-Men Legends I/II, Xanadu Next |
| v7 | f07f48ee44cca60046f40ce41858a160 | SDK v2.5 | 2.x | 57 | Snakes |
| v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | SDK v2.0 | 2.x | 57 | Ashen, Bomberman, Colin McRae Rally 2005, Crash Nitro Kart, Operation Shadow |
| v9 | 6ac8b1a1e490b367a1d5e46bb720fe70 | SDK v1.0 | 2.x | 57 | Tiger Woods PGA Tour 2004 |
| v10 | 827df54717bc210daa96aef9e12de4a3 | SDK v3.0 | 2.x | 63 | Glimmerati, ONE, Rifts: Promise of Power, System Rush, The Roots |
| v11 | 75536306ef48b1928b4562890d01c9c6 | SDK v3.1 | 2.x | 63 | Asphalt: Urban GT 2, Habbo Islands, Warhammer 40,000: Glory in Death |

The original compatibility table lists 12 groups (v0–v11); v0 is the SDK
beta and not shipped with a game.  v5, v6 and v7 contain identical code
(only version tag and import layout differ); they are still built as
separate files so that every game gets the exact export table it was
linked against.

**Installation:** copy the replacement built for the game's variant over
the game's `gamecomms.dll` (identify the variant by md5 of the original, see
`docs/BUILD.md`).  Using the wrong variant is not safe: v1 games would call
2.x functions and vice versa.

## 2 GameComms 1.x - variant v1

| Ord | Function | Conf. | Status | Notes |
|---:|---|:-:|:-:|---|
| 1 | `RSGEDebugLog::RSGEDebugLog()` | V | Full | 20-byte layout kept |
| 2 | `CGameBTComms::ConnectState()` | V | Full | |
| 3 | `CGameBTComms::ConnectionRole()` | V | Full | |
| 4 | `CGameBTComms::ContinueMultiPlayerGame()` | S | Full | continues when host and all connected clients asked |
| 5 | `CGameBTComms::DisconnectClient(TUint16)` | V | Full | `KErrNotReady` unless host, `KErrBadHandle` for 0 |
| 6 | `CGameBTComms::Disconnect()` | V | Full | while paused: temporary, `ReconnectL` restores |
| 7 | `CGameBTComms::EndMultiPlayerGame()` | S | Full | |
| 8 | `CGameBTComms::GameState()` | V | Full | |
| 9 | `CGameBTComms::GetLocalDeviceName(THostName&)` | V | Full | `GameComms.ini` DeviceName, else Bluetooth name |
| 10 | `CGameBTComms::IsShowingDeviceSelectDlg()` | S | Full | true while the bridge pairing dialog or the host list is shown |
| 11 | `CGameBTComms::NewL(MGameBTCommsNotify*, TUint32, RSGEDebugLog*)` | V | Full | |
| 12, 13, 22–30 | `RSGEDebugLog::NewLine/Open/WriteLine/Write` | V/S | Full | return `KErrNone` (as shipped) |
| 14 | `CGameBTComms::PauseMultiPlayerGame()` | S | Full | |
| 15 | `CGameBTComms::ReconnectL(TBool)` | S | Full | |
| 16 | `CGameBTComms::SendDataToAllClients(TDesC8&)` | V | Full | |
| 17 | `CGameBTComms::SendDataToClient(TUint16, TDesC8&)` | V | Full | |
| 18 | `CGameBTComms::SendDataToHost(TDesC8&)` | V | Full | |
| 19 | `RSGEDebugLog::SetLoggingLevel(TLogLevel)` | V | Full | |
| 20 | `CGameBTComms::StartClientL()` | V | Full | host list replaces the Bluetooth device dialog |
| 21 | `CGameBTComms::StartHostL(TUint16, TUint16)` | V | Full | |
| 31 | `CGameBTComms::~CGameBTComms()` | V | Full | |
| 32 | `RSGEDebugLog::~RSGEDebugLog()` | V | Full | |
| 33 | `RSGEDebugLog::Close()` | V | Full | |

All 12 callbacks of `MGameBTCommsNotify` are delivered asynchronously from
an active object with the documented arguments and error codes.

## 3 GameComms 2.x - variants v2 … v11

| Ord | Function | Conf. | Status | v2 | v3–v4 | v5–v9 | v10–v11 |
|---:|---|:-:|:-:|:-:|:-:|:-:|:-:|
| 1 | `RSGEDebugLog::RSGEDebugLog()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 2 | `BluetoothPowerState(TBool&)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 3 | `CGameBTDiscoverer::Cancel()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 4 | `CGameBTDeviceList::Reset()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 5 | `RSGEDebugLog::Close()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 6 | `ConnectState()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 7 | `Connect()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 8 | `Connect(TUint16)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 9 | `ConnectionRole()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 10 | `CGameBTDeviceList::Count()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 11 | `DeclareRemoteDevice(const TDeviceDetails&)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 12 | `CGameBTDeviceList::Delete(TInt)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 13 | `CGameBTDiscoverer::Devices()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 14 | `Disconnect()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 15 | `Disconnect(TUint16, TBool)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 16 | `CGameBTResolver::Start(const TDeviceDetails&)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 17 | `CGameBTDeviceList::CurrentItem()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 18 | `CGameBTDeviceList::ListBox()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 19 | `CGameBTDeviceList::At(TInt)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 20 | `GetLocalDeviceName(THostName&)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 21 | `CGameBTDeviceList::Hide()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 22 | `HostAcceptMode()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 23 | `CGameBTDeviceList::InsertL(TInt, const TDeviceDetails&)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 24 | `CGameBTDiscoverer::IsIdle()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 25 | `CGameBTResolver::NewL(…)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 26 | `CGameBTDeviceList::NewL(…)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 27 | `NewL(MGameBTCommsNotify*, TUint32, RSGEDebugLog*)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 28 | `CGameBTDiscoverer::NewL(…)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 29, 30, 41–49 | `RSGEDebugLog::NewLine/Open/WriteLine/Write` | V/S | Full | ✓ | ✓ | ✓ | ✓ |
| 31 | `CGameBTDiscoverer::StartL()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 32 | `SendData(const TDesC8&)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 33 | `SendData(TUint16, const TDesC8&)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 34 | `SetBluetoothPowerState(TBool)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 35 | `CGameBTDiscoverer::Set…(const TUint32&)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 36 | `SetHostAcceptMode(TAcceptMode)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 37 | `RSGEDebugLog::SetLoggingLevel()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 38 | `CGameBTDeviceList::Show()` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 39 | `StartClientL(TInt, CEikAppUi*)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 40 | `StartHostL(TInt, CEikAppUi*)` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 50 | `CGameBTResolver::~` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 51 | `CGameBTDeviceList::~` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 52 | `~CGameBTComms()` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 53 | `RSGEDebugLog::~` | V | Full | ✓ | ✓ | ✓ | ✓ |
| 54 | `CGameBTDiscoverer::~` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 55 | `CGameBTDeviceList::AppendL(const TDeviceDetails&)` | V | Inert | ✓ | ✓ | ✓ | ✓ |
| 56 | `SetLocalDeviceName(THostName)` | V | Full | | ✓ | ✓ | ✓ |
| 57 | `CGameBTDiscoverer::StartFullL()` | V | Inert | | | ✓ | ✓ |
| 58 | `EndSession()` | V | Full | | | | ✓ |
| 59, 60, 63 | discoverability helpers | I | Full* | | | | ✓ |
| 61 | `SetDiscoverabilityModeLimited(TBool)` | V | Full* | | | | ✓ |
| 62 | discoverer search mode setter | I | Inert | | | | ✓ |

\* Return `KErrNone`: Bluetooth discoverability has no meaning when the
network carries the sessions.

**Inert helper objects.**  The device list, discoverer and resolver are
internal building blocks the original also exported.  The replacement
returns real objects with the same argument counts: the list keeps its
items (`AppendL/InsertL/At/Count/Delete/Reset` work), discovery and
resolution complete without results.  If a game turns out to drive its
own host search through these classes, the discoverer can be connected to
the network discovery in `CGcEngine` without touching anything else.

### 2.x behaviour on the network

| Original (Bluetooth) | Replacement (network) |
|---|---|
| Client finds hosts by inquiry / its own UI | built-in list shows the open sessions of the same game; `DeclareRemoteDevice` with a device found by other means (real Bluetooth address) joins the first open session of the same game |
| `TDeviceDetails` holds a Bluetooth address | holds a synthetic address `47 43 xx xx xx xx` (`'G' 'C'` + session id or `0xFF0000`+client id) and the player name |
| host accept modes 0/1/2 via SDP listener | the session is listed (modes 0/1) or closed (mode 2) on the server; confirmation happens on the host |
| `Connect()` on the client registers with the host, `Connect()` on the host connects back | identical sequence: client `Connect()` joins, host `Connect()` sends CONNECT, both report `Connected` |
| `KErrIncompatibleGame` from the SDP UID check | from the server's UID check |
| `BluetoothPowerState` / `SetBluetoothPowerState` | report "on" / succeed (Bluetooth must be on for the bridge anyway) |
| panic `GC-MAIN 4` when Bluetooth is off | not raised; a missing bridge connection is reported through the normal error callbacks |

## 4 Limits and differences common to all variants

* Maximum payload per send: 1024 bytes (the 2.x originals used 512-byte
  socket buffers, 1.x documents no limit).  Larger sends return
  `KErrOverflow`; so does a send when 16 messages are still unacknowledged
  on the Bluetooth hop (the original would block in a synchronous write).
* Latency: Bluetooth hop + Wi-Fi + Internet instead of a direct piconet.
  On a LAN expect 20–60 ms round trip, over the Internet the network RTT
  on top.
* All callbacks are asynchronous, never from inside an API call.
* `IsShowingDeviceSelectDlg()` also covers the first-time bridge pairing
  dialog (the only Bluetooth dialog left).
