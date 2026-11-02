# Build, flash, deploy, test

## 1 Overview

| Component | Toolchain | Command | Output |
|---|---|---|---|
| Replacement DLLs | [ngage-toolchain](https://github.com/ngagesdk/ngage-toolchain) (Windows) | `cmake -G Ninja -B build && cmake --build build` | `build/dll/out/vN/gamecomms.dll` |
| ESP32 firmware | PlatformIO | `pio run -d firmware -t upload` | flashed bridge |
| Server | any C99 compiler, CMake | `cmake -S server -B build/server && cmake --build build/server` | `gamecommsd` |
| Tests | CMake, Python 3 | `cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests` | test report |

## 2 Replacement DLLs

Requirements: the N-Gage SDK toolchain set up as described in its README
(`NGAGESDK` environment variable pointing to the `ngage-toolchain`
checkout), CMake ≥ 3.10, Ninja, Python 3 (regenerates the `.def` files).

```bat
set NGAGESDK=C:\Development\Projects\ngage-toolchain
cmake -G Ninja -B build
cmake --build build
python tests\compat\check_variants.py --built build\dll\out
```

The last command checks every built DLL (UIDs, number of exports, no
empty export slots) against the original of the same variant.  To build
only some variants: `cmake -B build -DGC_VARIANTS="2;6"`.

### Installing on the N-Gage

1. Find the game's variant: copy its `gamecomms.dll` (in the game's
   `\System\Apps\<game>\` folder) to the PC and run
   `python tools/gc_identify.py dll gamecomms.dll`.
2. Back up the original and replace it with `build/dll/out/vN/gamecomms.dll`
   of the same variant.
3. Optional `E:\GameComms.ini`:

```ini
[Config]
; name other players see (default: the phone's Bluetooth name)
DeviceName=mupfdev
; 1 = join the first open game without showing the list
AutoJoin=0
; 1 = write E:\GameComms.log
Log=0

[Bridge]
; filled in automatically after the first pairing
;Address=0002ee112233
;Channel=0
```

On first use the standard Bluetooth device selection appears once: pick
the bridge ("GameCommsBridge", PIN `1234` unless changed).  Its address
is saved to the ini file.

### Checking a game

`python tools/gc_identify.py game path/to/game.app` lists which GameComms
ordinals a game imports and flags the exported helper classes (see
`COMPATIBILITY.md`).  Please report games that import helper ordinals.

## 3 ESP32 bridge firmware

Hardware: any ESP32 **with Bluetooth Classic** (ESP32-WROOM-32/WROVER,
e.g. "ESP32 DevKitC"); ESP32-S2/S3/C3/C6 lack Bluetooth Classic.  One
bridge per N-Gage.  USB power is enough; the on-board LED (GPIO 2) shows
the state: off = no Wi-Fi/server, blinking = server connected, on = server
and N-Gage connected.

```sh
pip install platformio
pio run -d firmware -t upload        # flash
pio device monitor -d firmware       # console, 115200 baud
```

(With the Arduino IDE: install the "esp32" core, copy `protocol/include`
and `protocol/src` files next to `firmware/src`, open `main.cpp`, select
"ESP32 Dev Module" and Partition Scheme "Huge APP".)

### Configuration

Without Wi-Fi settings the bridge opens the access point
`GameCommsBridge-Setup`; browse to `http://192.168.4.1/`.  Or use the
console:

```
set ssid MyWifi
set pass secret
set host auto            # LAN discovery; or a host name / IP of the server
set port 9888
set key change-me        # only if the server requires an access key
set btname GameCommsBridge
set pin 1234
save
reboot
status
```

`factory` erases all settings.

## 4 Endpoint server

### Linux / macOS

```sh
cmake -S server -B build/server -DCMAKE_BUILD_TYPE=Release
cmake --build build/server
ctest --test-dir build/server            # unit tests
./build/server/gamecommsd -c server/deploy/gamecommsd.conf
```

As a service:

```sh
sudo install build/server/gamecommsd /usr/local/bin/
sudo install -m 644 server/deploy/gamecommsd.conf /etc/gamecommsd.conf
sudo install -m 644 server/deploy/gamecommsd.service /etc/systemd/system/
sudo systemctl enable --now gamecommsd
journalctl -u gamecommsd -f
```

`kill -USR1 $(pidof gamecommsd)` writes a status dump (sessions, slots).

### Docker

```sh
docker build -t gamecommsd -f server/Dockerfile .
docker run -d --name gamecommsd -p 9888:9888/tcp -p 9888:9888/udp gamecommsd
```

### Windows

`cmake -S server -B build\server && cmake --build build\server --config Release`
gives `gamecommsd.exe` (Winsock, no other dependencies).

### Local network vs Internet

* **LAN**: run the server on any PC in the same network as the bridges and
  leave `host auto` on the bridges; they find the server by UDP broadcast
  on port 9888.
* **Internet**: run the server on a public host, open TCP 9888, set
  `host your.server.name` on every bridge, and set an `access_key` on the
  server and the same `key` on the bridges.  Players in different places
  then see each other's games.

Check a deployment from any PC without hardware:

```sh
./build/tests/gcsim --server=your.server.name --port=9888 --api=1 --players=4 [--key=...]
./build/tests/gcsim --server=your.server.name --api=2
```

`gcsim` runs four virtual N-Gages with real bridge cores against the
server and verifies every payload.

## 5 Automated tests

```sh
cmake -S tests -B build/tests -DGC_SANITIZE=ON
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

| Test | What it covers |
|---|---|
| `protocol_codec` | CRC check value, COBS vectors and long runs, malformed COBS, reader/writer bounds, framing with arbitrary fragmentation, garbage, corruption, truncation, oversize frames, 20 000 random fuzz inputs |
| `protocol_link` | handshake, version reject, bulk bidirectional transfer, 10 % corruption + 10 % frame loss, resumption after transport loss with in-flight data, fresh session after peer reboot, dead-peer detection, keep-alive, queue limits, garbage while up, back-pressure |
| `protocol_msg` | round trip of every message, every truncated prefix, malformed lists/strings, 200 000 random inputs |
| `server_core` | registration, matchmaking, slot assignment, full sessions, UID/API isolation, routing (unicast, broadcast, client→host only), ordering under load, confirm mode and timeout, session lock and rejoin, link resume keeps membership, grace expiry, host loss, reservation expiry, kick, host close, duplicate device, malformed input |
| `e2e_simulated` | real DLL engine + real bridge + real server with simulated Bluetooth/TCP: 1.x start-up with 4 players, early start prevented, unicast/broadcast/to-host data, 150 binary payloads in order, pause/continue, client end with enough / too few players, host end, phone-call interrupt + `ReconnectL` (SDK scenarios 3.8/3.9), client link loss, incompatible game, host selection list and cancel; 2.x accept-all/connect, data, not-connected errors, confirm mode, closed host, disconnect/reconnect, built-in list, real-BT-device declaration; TCP outage mid-stream without loss, bridge reboot, noisy Bluetooth link, two concurrent sessions of the same game |
| `e2e_network` | starts `gamecommsd`, runs `gcsim` over real TCP sockets for 1.x and 2.x with 2–4 players, checks a wrong access key is refused |
| `dll_variants` | every variant's export table against the original binary (count, contiguity, thunk presence, argument counts) |

CI (`.github/workflows/ci.yaml`) runs the host tests on Linux (with
sanitizers) and Windows, builds the server Docker image and the ESP32
firmware.

## 6 Hardware test plan

The automated tests cover everything except the Symbian adapter and the
radio links.  With real hardware:

1. **Smoke**: server + one bridge + one N-Gage.  Start a 1.x or 2.x game
   as host; the server log shows `registered` and `session … opened`.
   `gcsim --api=N --players=2` cannot join a game started on a real
   device (it hosts itself), but a second N-Gage can.
2. **Two devices on a LAN**: both bridges `host auto`.  Host on device A,
   join on device B through the in-game multiplayer menu; play a full
   match.  Verify: list shows A's name, game starts on both, input arrives
   both ways.
3. **Four devices** (one host, three clients) for a 4-player title (e.g.
   Bomberman v8, Worms World Party v6, Pathway to Glory v5).
4. **Interruptions**: pause from a client, continue from all; call the
   host or a client while playing (1.x games: SDK scenario 3.8); switch
   the bridge off for 10 s (resume) and for 60 s (game end with
   `KErrDisconnected`); restart the server (sessions lost, devices get
   `HostDisconnected`/`Disconnected`).
5. **Internet**: one device at home, one via a phone hotspot, server on a
   VPS with an access key.
6. Run each test once per variant group (v1, v2, v3/v4, v5–v9, v10/v11).

Enable `Log=1` in `E:\GameComms.ini` and `log_level=3` on the server to
correlate both sides; the bridge console shows link statistics with
`status`.

### Emulator

EKA2L1 emulates N-Gage games, but not Bluetooth.  The DLL can still be
loaded there to verify that a game links against the replacement and that
`NewL`/`StartHostL` run (the bridge connection then fails and is retried).
