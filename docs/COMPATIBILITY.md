# Compatibility

One replacement GAMECOMMS.DLL is built for each original variant
(`build/dll/out/vN/gamecomms.dll`). Each has the original UIDs, the same
number of exports, and every export number (ordinal) points to an
implementation of the original function. The export tables are defined
in `dll/variants/variants.py` and checked against the original DLLs by
`tests/compat/check_variants.py`.

Confidence:

- V: verified from the original code
- S: behaviour verified, name taken from the export order
- I: inferred

Status:

- Full: implemented over the network with the original behaviour
- Inert: present and safe to call, but without function (see below)

## Variants

The list of games and which DLL each one needs is in the
[README](../README.md#which-dll-for-which-game).

| Variant | md5sum of the original | Version tag | API | Ordinals |
|---|---|---|---|---|
| v0 | cb326c500bdb795494d55d30196b8711 | SDK 1.0 beta | 1.x | 33 |
| v1 | 79c25cdd1972c287712475ee544c7bf3 | none | 1.x | 33 |
| v2 | 8863fdea897f1c63807ad69252a3566f | none | 2.x | 55 |
| v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | none | 2.x | 56 |
| v4 | c2f184012cd0635f3065b7d5042702ce | SDK v1.0 | 2.x | 56 |
| v5 | e8d317204a763d71215680d567eb71e0 | SDK v2.6 | 2.x | 57 |
| v6 | bb02377c5361597f8d8503f5de584845 | SDK v2.5 | 2.x | 57 |
| v7 | f07f48ee44cca60046f40ce41858a160 | SDK v2.5 | 2.x | 57 |
| v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | SDK v2.0 | 2.x | 57 |
| v9 | 6ac8b1a1e490b367a1d5e46bb720fe70 | SDK v1.0 | 2.x | 57 |
| v10 | 827df54717bc210daa96aef9e12de4a3 | SDK v3.0 | 2.x | 63 |
| v11 | 75536306ef48b1928b4562890d01c9c6 | SDK v3.1 | 2.x | 63 |

v0 is the SDK beta and was not shipped with a game, so no replacement is
built for it. v5, v6 and v7 contain identical code. They are still built
as separate files, so every game gets exactly the export table it was
linked against.

Install the replacement built for the game's variant. Using a different
variant will not work, because v1 games use a different API than v2 to
v11.

## GameComms 1.x (v1)

| Ordinal | Function | Conf. | Status | Notes |
|---|---|---|---|---|
| 1 | RSGEDebugLog::RSGEDebugLog() | V | Full | 20 byte layout kept |
| 2 | CGameBTComms::ConnectState() | V | Full | |
| 3 | CGameBTComms::ConnectionRole() | V | Full | |
| 4 | CGameBTComms::ContinueMultiPlayerGame() | S | Full | continues when the host and all clients asked |
| 5 | CGameBTComms::DisconnectClient(TUint16) | V | Full | KErrNotReady unless host, KErrBadHandle for 0 |
| 6 | CGameBTComms::Disconnect() | V | Full | while paused: temporary, ReconnectL restores |
| 7 | CGameBTComms::EndMultiPlayerGame() | S | Full | |
| 8 | CGameBTComms::GameState() | V | Full | |
| 9 | CGameBTComms::GetLocalDeviceName(THostName&) | V | Full | DeviceName from GameComms.ini, else the Bluetooth name |
| 10 | CGameBTComms::IsShowingDeviceSelectDlg() | S | Full | true while the bridge selection or the host list is shown |
| 11 | CGameBTComms::NewL(MGameBTCommsNotify*, TUint32, RSGEDebugLog*) | V | Full | |
| 12, 13, 22 to 30 | RSGEDebugLog::NewLine, Open, WriteLine, Write | V/S | Full | return KErrNone, as in the original |
| 14 | CGameBTComms::PauseMultiPlayerGame() | S | Full | |
| 15 | CGameBTComms::ReconnectL(TBool) | S | Full | |
| 16 | CGameBTComms::SendDataToAllClients(TDesC8&) | V | Full | |
| 17 | CGameBTComms::SendDataToClient(TUint16, TDesC8&) | V | Full | |
| 18 | CGameBTComms::SendDataToHost(TDesC8&) | V | Full | |
| 19 | RSGEDebugLog::SetLoggingLevel(TLogLevel) | V | Full | |
| 20 | CGameBTComms::StartClientL() | V | Full | shows the host list instead of the Bluetooth device dialog |
| 21 | CGameBTComms::StartHostL(TUint16, TUint16) | V | Full | |
| 31 | CGameBTComms::~CGameBTComms() | V | Full | |
| 32 | RSGEDebugLog::~RSGEDebugLog() | V | Full | |
| 33 | RSGEDebugLog::Close() | V | Full | |

All 12 callbacks of MGameBTCommsNotify are delivered asynchronously with
the documented arguments and error codes.

## GameComms 2.x (v2 to v11)

The "Variants" column lists which variants have the export.

| Ordinal | Function | Conf. | Status | Variants |
|---|---|---|---|---|
| 1 | RSGEDebugLog::RSGEDebugLog() | V | Full | v2 to v11 |
| 2 | BluetoothPowerState(TBool&) | V | Full | v2 to v11 |
| 3 | CGameBTDiscoverer::Cancel() | V | Inert | v2 to v11 |
| 4 | CGameBTDeviceList::Reset() | V | Inert | v2 to v11 |
| 5 | RSGEDebugLog::Close() | V | Full | v2 to v11 |
| 6 | ConnectState() | V | Full | v2 to v11 |
| 7 | Connect() | V | Full | v2 to v11 |
| 8 | Connect(TUint16) | V | Full | v2 to v11 |
| 9 | ConnectionRole() | V | Full | v2 to v11 |
| 10 | CGameBTDeviceList::Count() | V | Inert | v2 to v11 |
| 11 | DeclareRemoteDevice(const TDeviceDetails&) | V | Full | v2 to v11 |
| 12 | CGameBTDeviceList::Delete(TInt) | V | Inert | v2 to v11 |
| 13 | CGameBTDiscoverer::Devices() | V | Inert | v2 to v11 |
| 14 | Disconnect() | V | Full | v2 to v11 |
| 15 | Disconnect(TUint16, TBool) | V | Full | v2 to v11 |
| 16 | CGameBTResolver::Start(const TDeviceDetails&) | V | Inert | v2 to v11 |
| 17 | CGameBTDeviceList::CurrentItem() | V | Inert | v2 to v11 |
| 18 | CGameBTDeviceList::ListBox() | V | Inert | v2 to v11 |
| 19 | CGameBTDeviceList::At(TInt) | V | Inert | v2 to v11 |
| 20 | GetLocalDeviceName(THostName&) | V | Full | v2 to v11 |
| 21 | CGameBTDeviceList::Hide() | V | Inert | v2 to v11 |
| 22 | HostAcceptMode() | V | Full | v2 to v11 |
| 23 | CGameBTDeviceList::InsertL(TInt, const TDeviceDetails&) | V | Inert | v2 to v11 |
| 24 | CGameBTDiscoverer::IsIdle() | V | Inert | v2 to v11 |
| 25 | CGameBTResolver::NewL(...) | V | Inert | v2 to v11 |
| 26 | CGameBTDeviceList::NewL(...) | V | Inert | v2 to v11 |
| 27 | NewL(MGameBTCommsNotify*, TUint32, RSGEDebugLog*) | V | Full | v2 to v11 |
| 28 | CGameBTDiscoverer::NewL(...) | V | Inert | v2 to v11 |
| 29, 30, 41 to 49 | RSGEDebugLog::NewLine, Open, WriteLine, Write | V/S | Full | v2 to v11 |
| 31 | CGameBTDiscoverer::StartL() | V | Inert | v2 to v11 |
| 32 | SendData(const TDesC8&) | V | Full | v2 to v11 |
| 33 | SendData(TUint16, const TDesC8&) | V | Full | v2 to v11 |
| 34 | SetBluetoothPowerState(TBool) | V | Full | v2 to v11 |
| 35 | CGameBTDiscoverer setter (const TUint32&) | V | Inert | v2 to v11 |
| 36 | SetHostAcceptMode(TAcceptMode) | V | Full | v2 to v11 |
| 37 | RSGEDebugLog::SetLoggingLevel() | V | Full | v2 to v11 |
| 38 | CGameBTDeviceList::Show() | V | Inert | v2 to v11 |
| 39 | StartClientL(TInt, CEikAppUi*) | V | Full | v2 to v11 |
| 40 | StartHostL(TInt, CEikAppUi*) | V | Full | v2 to v11 |
| 50 | CGameBTResolver destructor | V | Inert | v2 to v11 |
| 51 | CGameBTDeviceList destructor | V | Inert | v2 to v11 |
| 52 | ~CGameBTComms() | V | Full | v2 to v11 |
| 53 | RSGEDebugLog destructor | V | Full | v2 to v11 |
| 54 | CGameBTDiscoverer destructor | V | Inert | v2 to v11 |
| 55 | CGameBTDeviceList::AppendL(const TDeviceDetails&) | V | Inert | v2 to v11 |
| 56 | SetLocalDeviceName(THostName) | V | Full | v3 to v11 |
| 57 | CGameBTDiscoverer::StartFullL() | V | Inert | v5 to v11 |
| 58 | EndSession() | V | Full | v10, v11 |
| 59, 60, 63 | discoverability helpers | I | Full (1) | v10, v11 |
| 61 | SetDiscoverabilityModeLimited(TBool) | V | Full (1) | v10, v11 |
| 62 | discoverer search mode setter | I | Inert | v10, v11 |

(1) Returns KErrNone. Bluetooth discoverability has no meaning when the
sessions run over the network.

### Inert helper objects

The device list, discoverer and resolver are internal parts of the
original library that were exported as well. The replacement returns real
objects that accept the same arguments. The list keeps its items
(AppendL, InsertL, At, Count, Delete and Reset work). Discovery and
resolution finish without results. If a game turns out to use these
classes for its own host search, the discoverer can be connected to the
network host list in `CGcEngine`.

### Differences to the original on the network

| Original (Bluetooth) | Replacement (network) |
|---|---|
| The client finds hosts by Bluetooth inquiry or the game's own UI | The built-in list shows open sessions of the same game. DeclareRemoteDevice with any other device joins the first open session of the same game. |
| TDeviceDetails holds a Bluetooth address | It holds a made-up address starting with 'G' 'C' plus the session or client id, and the player name |
| Host accept modes 0, 1, 2 through the Bluetooth service record | The session is listed (modes 0 and 1) or closed (mode 2) on the server. Confirmation happens on the host. |
| Connect on the client registers with the host, Connect on the host connects back | Same sequence: client Connect joins, host Connect sends CONNECT, both report Connected |
| KErrIncompatibleGame from the Bluetooth service check | From the server's game UID check |
| BluetoothPowerState and SetBluetoothPowerState | Report "on" and succeed. Bluetooth has to be on for the bridge anyway. |
| Panic GC-MAIN 4 when Bluetooth is off | Not raised. A missing bridge is reported through the normal error callbacks. |

## Limits common to all variants

- At most 1024 bytes per send. The 2.x originals used 512 byte buffers,
  1.x documents no limit. Larger sends return KErrOverflow. A send also
  returns KErrOverflow while 16 messages are still unacknowledged on the
  Bluetooth connection (the original would block instead).
- Latency is higher than on a direct Bluetooth connection: expect 20 to
  60 ms round trip on a local network, plus the network delay over the
  Internet.
- All callbacks are asynchronous, never from inside an API call.
- IsShowingDeviceSelectDlg also returns true while the one-time bridge
  selection dialog is shown.
