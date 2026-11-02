# Build, flash, deploy and test

## Overview

| Part | Tools | Output |
|---|---|---|
| Replacement DLLs | ngage-toolchain (Windows), CMake, Ninja, Python 3 | `build/dll/out/vN/gamecomms.dll` |
| ESP32 firmware | PlatformIO | flashed bridge |
| Server | C99 compiler, CMake | `gamecommsd` |
| Tests | C compiler, CMake, Python 3 | test report |

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
DeviceName=mupfdev
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

```sh
docker build -t gamecommsd -f server/Dockerfile .
docker run -d --name gamecommsd -p 9888:9888/tcp -p 9888:9888/udp gamecommsd
```

### Windows

```bat
cmake -S server -B build\server
cmake --build build\server --config Release
```

### Local network or Internet

- Local network: run the server on any PC in the same network and keep
  `host auto` on the bridges.
- Internet: run the server on a public host, open TCP port 9888, set the
  server's host name on every bridge, and set the same access key on the
  server (`access_key`) and the bridges (`key`).

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
| `dll_variants` | Export tables of all variants compared with the original DLLs |

CI runs these tests on Linux and Windows and also builds the Docker image
and the ESP32 firmware.

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
