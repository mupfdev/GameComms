# Build, flash, deploy and test

## Overview

| Part | Tools | Output |
|---|---|---|
| Replacement DLLs | ngage-toolchain (Windows), CMake, Ninja, Python 3 | `build/dll/out/vN/gamecomms.dll` |
| ESP32 firmware | PlatformIO | flashed bridge |
| PC bridge | Linux: C compiler, CMake; Windows: Visual Studio or MinGW, CMake | `gcbridge` |
| Server | C99 compiler, CMake | `gamecommsd` |
| Tests | C compiler, CMake, Python 3 | test report |

Ready-made binaries of everything above are published by CI as the
pre-release **PhantomLink Continuous Integration Build** (tag `continuous`,
GitHub Releases page): `PhantomLink.zip` with the Windows PC bridge, the
server for Linux x86_64 and Windows, all DLL variants and the ESP32
firmware. It is rebuilt on every push to `main`.

## CMake presets

`CMakePresets.json` builds each part on its own or everything at once
(also selectable in Visual Studio). Output goes to `out/build/<preset>`.

| Preset | Builds | Host / requirements |
|---|---|---|
| `N-Gage` | all DLL variants (`out/build/N-Gage/dll/out/vN/gamecomms.dll`) | `NGAGESDK` |
| `N-Gage-v1` ... `N-Gage-v11` (build presets) | a single DLL variant | `NGAGESDK` |
| `pcbridge-windows` | `gcbridge.exe` | Windows, MSVC |
| `server` | `gamecommsd` and its unit tests | any |
| `all` | PC bridge, server and all DLL variants, staged in `out/build/all/dist/PhantomLink` | Windows, MSVC, `NGAGESDK` |

```bat
cmake --workflow --preset all
cmake --workflow --preset server
cmake --preset N-Gage && cmake --build --preset N-Gage-v6
```

On the command line, the MSVC presets need a Developer Command Prompt
(or Developer PowerShell) for x64. The ESP32 firmware is not a CMake
project; build it with PlatformIO as described below.

## Replacement DLLs

Requirements: the [ngage-toolchain](https://github.com/ngagesdk/ngage-toolchain)
set up as described in its README, with the `NGAGESDK` environment
variable pointing to it.

```bat
set NGAGESDK=C:\Development\Projects\ngage-toolchain
cmake -G Ninja -B build
cmake --build build
python tests\compat\check_variants.py --built build\dll\out
```

The last command compares every built DLL with the original of the same
variant (UIDs, number of exports). To build only some variants, use
`cmake -B build -DGC_VARIANTS="2;6"`.

Each variant is linked in its own build folder (`build/dll/v1`, `v2`, ...),
because the toolchain's `dlltool` writes temporary files with fixed names.
If an older build folder exists, delete it before building.

### PhantomLink Symbian app

The same build also creates the N-Gage application `PhantomLink.sis`
(`build/dll/app/PhantomLink.sis`). It contains all DLL variants and lets you

- pick a game from a list: the matching `gamecomms.dll` is copied to
  `E:\System\Libs` (if the game ships its own copy in its folder, replace
  that one as described below);
- configure the ESP32 bridge over Bluetooth (see "Bluetooth configuration").

Install the SIS on the memory card. Disable it with
`cmake -B build -DGC_BUILD_APP=OFF`. The game list is kept in sync with the
README by `python tests/compat/check_app_games.py`.

### Installing on the N-Gage

1. Copy the game's `gamecomms.dll` (in `\System\Apps\<game>\`) to the PC
   and run `python tools/gc_identify.py dll gamecomms.dll` to find its
   variant.
2. Back up the original and replace it with `build/dll/out/vN/gamecomms.dll`
   of the same variant. Using a different variant will not work.
3. Optional: create `E:\GameComms.ini`:

```ini
[Config]
; name shown to other players (default: the phone's Bluetooth name)
DeviceName=Maxwell
; 1 = join the first open game without showing the list
AutoJoin=0
; 1 = write E:\GameComms.log
Log=0

[Bridge]
; saved automatically after the first connection
;Address=0002ee112233
;Channel=0
```

The first time, the phone shows the Bluetooth device selection. Pick the
bridge (default name `GameCommsBridge`, PIN `1234`). Its address is then
saved to the ini file.

### Checking a game

`python tools/gc_identify.py game path/to/game.app` lists the GameComms
functions a game imports. Please report games that use the internal
helper functions marked in the output (see COMPATIBILITY.md).

## ESP32 bridge

Hardware: an ESP32 with Bluetooth Classic, for example an ESP32-WROOM-32
or WROVER dev board. ESP32-S2, S3, C3 and C6 do not have Bluetooth
Classic and will not work. You need one bridge per N-Gage.

The on-board LED shows the state:

- off: no Wi-Fi or no server
- blinking: connected to the server
- on: connected to the server and to the N-Gage
- fast blinking: setup mode

Flash and open the console (115200 baud):

```sh
pip install platformio
pio run -d firmware -t upload
pio device monitor -d firmware
```

With the Arduino IDE instead: install the esp32 core, copy the files from
`protocol/include` and `protocol/src` next to `firmware/src/main.cpp`,
select "ESP32 Dev Module" and the partition scheme "Huge APP".

### Configuration

Without Wi-Fi settings the bridge starts in setup mode: it opens the
access point `GameCommsBridge-Setup`. Connect to it and open
`http://192.168.4.1/`.

To change the settings later, start setup mode in one of these ways:

- Hold the BOOT button for 3 seconds while the bridge is running. The bridge
  restarts in setup mode and the LED blinks fast.
- Enter `setup` on the serial console.

The current settings stay. Leave the password field empty to keep the
saved password. "Reboot without changes" leaves setup mode. Setup mode
is only used for the next start; after a power cycle the bridge starts
normally again.

Or use the serial console:

```
set ssid MyWifi
set pass secret
set host auto
set port 9888
set key change-me
set btname GameCommsBridge
set pin 1234
save
reboot
```

- `host auto` finds the server on the local network. Otherwise enter a
  host name or IP address.
- `key` is only needed if the server requires an access key.
- `status` shows the current state, `setup` restarts in setup mode,
  `factory` erases all settings.

The ESP32 services server discovery, DNS, TCP connection and writes without
blocking the Bluetooth receive loop. TCP output uses a bounded queue; if it
fills, the transport is closed and the reliable link attempts to resume.
An updated sender respects the receiver's advertised link window. Update the
server and N-Gage DLL as well as the ESP32 to limit bursts in both directions;
older peers remain compatible but do not limit their own bursts. Bluetooth
writes still use Arduino's transmit queue and can block when it is full.

For Bluetooth disconnects, capture the serial console log from connection
through the failure. SPP open and close events include their handle, status,
port status on close, and whether the close was asynchronous (`async=0`
means locally initiated). A close marked `active 1` applies to the tracked
connection. The firmware prints link counters and current and lowest heap at
an active close; `status` also shows these counters. Bluetooth input uses a
bounded 4096-byte buffer, and its dropped-byte counter and overflow messages
show when that buffer fills. Frame errors count errors received by the bridge,
not errors on the N-Gage or bytes lost before the Bluetooth data callback.
Write failures count calls that did not accept the full buffer; Bluetooth
writes accepted by the stack may still fail later. If SPP events are reported
lost, reboot before relying on connection state diagnostics.

### Bluetooth configuration

The PhantomLink app (menu "Bridge setup") reads and writes the same
settings as the serial console: Wi-Fi SSID and password, server host, port,
key, Bluetooth name, PIN and verbose, plus reboot, setup mode and factory
reset. Passwords and the key are never sent back to the phone; leave them
unchanged to keep the saved values.

The configuration shares the Bluetooth SPP channel with the game. A
connection whose first bytes are `GCCFG1` is a configuration session; any
other connection is a game session as before. Configuration is therefore only
possible while no game is connected. Protocol: the phone sends `GCCFG1\n`,
the bridge answers `OK GCCFG1 <version>` and `END`. After that every line is
a serial console command (`show`, `set <key> <value>`, `save`, `reboot`,
`setup`, `factory`, `status`, `help`) and each reply ends with a line `END`
(`reboot`, `setup` and `factory` restart the bridge without `END`). Pairing
uses the bridge PIN, so change the default `1234`.

## PC bridge (Linux and Windows)

The PC bridge `gcbridge` does the same as the ESP32 bridge, using the
computer's Bluetooth adapter instead. One computer can serve several
N-Gages at the same time (default 4, at most 7). Each N-Gage gets its own
connection to the server.

Linux is the recommended platform, for example a Raspberry Pi. On Windows
the bridge depends on the Bluetooth adapter and may not work at all, see
"Windows: known problem" below.

### Linux and Raspberry Pi

Requirements: BlueZ 5 (the standard Linux Bluetooth stack, installed on
Raspberry Pi OS, Debian, Ubuntu and most other distributions), a C
compiler and CMake. No other libraries are needed.

```sh
sudo apt install build-essential cmake bluez
cmake -S pcbridge -B build/pcbridge -DCMAKE_BUILD_TYPE=Release
cmake --build build/pcbridge
```

The bridge talks to BlueZ, so the user running it must be allowed to: run
it as root, or add the user to the `bluetooth` group once
(`sudo usermod -aG bluetooth $USER`, then log in again). Make sure
Bluetooth is not blocked (`rfkill unblock bluetooth`).

```sh
./build/pcbridge/gcbridge --server=auto
```

On start the bridge switches the adapter on, makes it visible and
pairable, registers the Serial Port service and answers pairing requests
with the PIN (default `1234`). On the N-Gage, start a multiplayer game and
select the computer from the Bluetooth device list.

To start it automatically with systemd, create
`/etc/systemd/system/gcbridge.service`:

```ini
[Unit]
Description=GameComms PC bridge
After=bluetooth.service network-online.target
Wants=bluetooth.service network-online.target

[Service]
ExecStart=/usr/local/bin/gcbridge -c /etc/gcbridge.ini
Restart=always

[Install]
WantedBy=multi-user.target
```

```sh
sudo install build/pcbridge/gcbridge /usr/local/bin/
sudo install -m 644 pcbridge/gcbridge.ini /etc/gcbridge.ini
sudo systemctl enable --now gcbridge
```

Use `adapter = hci1` in `gcbridge.ini` if the computer has more than one
Bluetooth adapter.

### Windows

Requirements: Windows 10 or 11 with a Bluetooth adapter that is switched
on. Download `PhantomLink.zip` from the CI release (folder
`pcbridge/windows`), `gcbridge-windows` from the latest CI run on GitHub
(Actions tab), or build it:

```bat
cmake -S pcbridge -B build\pcbridge
cmake --build build\pcbridge --config Release
```

The result is `build\pcbridge\Release\gcbridge.exe`. It has no
dependencies and can be copied to any Windows PC together with
`pcbridge\gcbridge.ini`.

1. Start the server (on this PC or another one), see below.
2. Start `gcbridge.exe`. It shows the Bluetooth channel it uses and
   whether it found the server.
3. On the N-Gage, start a multiplayer game. When the Bluetooth device
   list appears, select this PC.
4. If the N-Gage asks for a PIN, enter `1234`. The bridge answers the
   pairing request automatically.

If the N-Gage does not find the PC, open the Bluetooth settings in Windows
while searching (Windows 11: set "Bluetooth devices discovery" to
"Advanced"), or pair the N-Gage once from Windows with "Add device".

#### Windows: known problem

On the first PC tested (Windows 11), Windows did not complete the
N-Gage's connection: the N-Gage found the PC and its Serial Port service,
but its connection attempt never finished (the N-Gage log stops at
`connecting RFCOMM channel N`, the bridge sees nothing). The same happened
with the bridge's own service and with Windows' own incoming COM port, so
the problem lies in the Windows Bluetooth stack or the adapter's driver,
not in the bridge. A different Bluetooth adapter may behave differently.
If it happens, use Linux (for example a Raspberry Pi) or an ESP32 bridge.

#### COM mode

COM mode lets Windows itself accept the N-Gage's connection and pass it to
the bridge through a COM port. It did not help on the PC described above,
but may on others.

1. Open the classic Bluetooth settings: Settings, Bluetooth & devices,
   Devices, "More Bluetooth settings" (or run `control bthprops.cpl` and
   choose "Change Bluetooth settings").
2. On the "COM Ports" tab click "Add", choose "Incoming (device initiates
   the connection)" and confirm. Note the port name, for example `COM5`.
   If no name appears, close and reopen the dialog or look under
   "Ports (COM & LPT)" in Device Manager.
3. Start the bridge with `gcbridge --com=COM5`, or set `com = COM5` in
   `gcbridge.ini`.
4. On the N-Gage, delete the `[Bridge]` section in `E:\GameComms.ini` if
   it exists, start a multiplayer game and select this PC.

Every incoming COM port publishes its own Serial Port service and the
N-Gage uses the last one it finds, so remove incoming ports you do not need
or list them all (`com = COM5,COM6`). In COM mode the bridge does not
answer pairing requests; pair the N-Gage with Windows as usual.

### Settings

`gcbridge.ini` is read from the current folder or from the folder of the
program. Every setting can also be given on the command line, for example
`gcbridge --server=my.server.name --key=secret`.

| Setting | Default | Meaning |
|---|---|---|
| `server` | `auto` | server host name or IP address, `auto` finds it on the local network |
| `port` | `9888` | server port |
| `key` | empty | access key, if the server requires one |
| `pin` | `1234` | PIN for pairing; empty = pair the N-Gage with the system's own tools |
| `name` | `GameComms Bridge` | name of the Bluetooth service |
| `max_devices` | `4` | N-Gages at the same time (1 to 7) |
| `log_level` | `2` | 0 errors, 1 warnings, 2 information, 3 debug |
| `channel` | `0` | RFCOMM channel, 0 = chosen automatically |
| `adapter` | `hci0` | Linux only: Bluetooth adapter |
| `com` | empty | Windows only: COM mode with incoming Bluetooth COM port(s), e.g. `COM5,COM6` |

The N-Gage sees the computer under its Bluetooth name.

## Server

### Linux and macOS

```sh
cmake -S server -B build/server -DCMAKE_BUILD_TYPE=Release
cmake --build build/server
./build/server/gamecommsd -c server/deploy/gamecommsd.conf
```

Install as a systemd service:

```sh
sudo install build/server/gamecommsd /usr/local/bin/
sudo install -m 644 server/deploy/gamecommsd.conf /etc/gamecommsd.conf
sudo install -m 644 server/deploy/gamecommsd.service /etc/systemd/system/
sudo systemctl enable --now gamecommsd
```

`kill -USR1 $(pidof gamecommsd)` writes the current sessions to the log.

### Docker

The bundled config binds to container loopback. For remote bridge connections,
provide a config that binds to a VPN/tunnel interface, set
`allow_plaintext_network = 1`, and keep the container reachable only through
that secured network. Do not publish the plaintext port directly to the
Internet.

```sh
docker build -t gamecommsd -f server/Dockerfile .
# Run with a secured overlay network and a config bound to its interface.
docker run -d --name gamecommsd --network your-secured-overlay \
-v /path/to/gamecommsd.conf:/etc/gamecommsd.conf:ro gamecommsd
```

### Windows

```bat
cmake -S server -B build\server
cmake --build build\server --config Release
```

### Local network or Internet

The server listens on `127.0.0.1` by default. To accept remote connections,
set `bind` to a specific private/VPN interface address, configure a non-empty
`access_key`, and explicitly set `allow_plaintext_network = 1`. Use only a
trusted VPN or secured tunnel between every bridge and the server; do not bind
to a public interface or expose TCP port 9888 directly to untrusted networks.
The protocol is still unencrypted TCP and the shared access key is sent in
cleartext; the opt-in and key requirement reduce accidental exposure but are
not substitutes for transport security.

`max_outq` limits queued outbound messages per peer (default 1024, range
1 to 4096). Memory is allocated as messages are queued, so a higher limit
can increase server memory use and latency under congestion. An explicit
value in `gamecommsd.conf` overrides the default.

Built-in abuse limits (all can be set in `gamecommsd.conf` or on the
command line):

| Option | Default | Effect |
|---|---|---|
| `hello_timeout_ms` | 10000 | close connections that do not complete the link handshake |
| `max_auth_failures` | 3 | close a connection after this many wrong access keys |
| `max_conns_per_ip` | 16 | concurrent connections per source address |
| `conn_burst`, `conn_rate` | 10, 2 | new connections per source address (burst, per second) |
| `limit_loopback` | 0 | also apply the per-address limits to loopback clients |

Per-address limits skip loopback by default because local tunnels (SSH port
forwarding, stunnel) make every bridge appear as `127.0.0.1`. Such tunnels
also weaken the protection against resuming another bridge's link, since
all clients share one address; prefer a VPN (e.g. WireGuard) that keeps a
distinct address per bridge. Invalid or out-of-range option values are
errors, and the server refuses to start without an operating-system random
source.
server address routes through that secured network.

To test a server without hardware:

```sh
./build/tests/gcsim --server=your.server.name --port=9888 --api=1 --players=4
./build/tests/gcsim --server=your.server.name --api=2
```

`gcsim` simulates four N-Gages with bridges and checks that all data
arrives.

## Automated tests

```sh
cmake -S tests -B build/tests -DGC_SANITIZE=ON
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

| Test | Covers |
|---|---|
| `protocol_codec` | Framing, checksums, broken and fragmented input, random input |
| `protocol_link` | Handshake, retransmission, packet loss, resuming after a lost connection, keep-alive, flow control |
| `protocol_msg` | Encoding and decoding of every message, truncated and random input |
| `server_core` | Registration, matchmaking, slots, isolation by game, routing, timeouts, rejoin, kick |
| `e2e_simulated` | DLL logic, bridge and server together with simulated Bluetooth and TCP: all 1.x and 2.x game flows, interruptions, reconnects, bridge reboot |
| `e2e_network` | Real server and `gcsim` over TCP, 2 to 4 players, wrong access key |
| `pcbridge_bluez` | PC bridge on Linux against a simulated BlueZ on a private D-Bus bus: adapter setup, Serial Port profile and pairing agent registration, PIN answer, connections handed over by BlueZ, reconnect, clean unregistration (skipped without `dbus-daemon` and Python dbus) |
| `pcbridge_network` | Real server, PC bridge and `gcsim` over TCP (instead of Bluetooth): 1.x and 2.x, explicit server and LAN discovery, an N-Gage dropping out and coming back, COM mode through a pseudo terminal including an N-Gage restarting on the same port |
| `dll_variants` | Export tables of all variants compared with the original DLLs |

CI runs these tests on Linux and Windows and also builds the Docker image,
the ESP32 firmware, the server, `gcbridge.exe` and all DLL variants, packs
them into `PhantomLink.zip` and publishes it as the CI release (pushes to
`main` only).

## Hardware test plan

The automated tests cover everything except the Symbian code and the
radio links. With real hardware:

1. One server, one bridge, one N-Gage. Host a game. The server log shows
   the registration and the new session.
2. Two N-Gages on the same network. Host on one, join on the other, play
   a full match.
3. Four N-Gages with a 4-player game, for example Bomberman (v8) or Worms
   World Party (v6).
4. Interruptions: pause and continue, a phone call during a game, bridge
   off for 10 seconds (game continues) and for 60 seconds (game ends),
   server restart.
5. Internet: one device at home, one on a phone hotspot, server on a
   public host with an access key.
6. Repeat for each variant group: v1, v2, v3 and v4, v5 to v9, v10 and v11.

For debugging, set `Log=1` in `E:\GameComms.ini` and `log_level=3` on the
server. The bridge console command `status` shows connection statistics.

The EKA2L1 emulator does not emulate Bluetooth, but it can show whether a
game loads the replacement DLL and starts its multiplayer mode.
