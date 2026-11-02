# GameComms

[![ci](https://github.com/mupfdev/GameComms/actions/workflows/ci.yaml/badge.svg)](https://github.com/mupfdev/GameComms/actions/workflows/ci.yaml)

GameComms was the proprietary multiplayer library of the Nokia N-Gage SDK.
44 of the 57 N-Gage games use it to connect up to four devices over
Bluetooth.  This project replaces it so that those games can play
together **over Wi-Fi and the Internet**, without modifying the games:

```
 N-Gage + replacement GAMECOMMS.DLL ──Bluetooth──▶ ESP32 bridge ──Wi-Fi/TCP──▶ endpoint server
```

* **`dll/`** - drop-in replacements for all 11 shipped `gamecomms.dll`
  variants (both API generations), same UIDs and ordinals, original
  callbacks and error codes.
* **`firmware/`** - ESP32 bridge: Bluetooth serial towards the N-Gage,
  Wi-Fi/TCP towards the server, link resumption, local fallback answers.
* **`server/`** - endpoint server: registration, game-UID isolated
  matchmaking, slot assignment, routing, reconnect handling.  No
  dependencies, runs on Linux, Windows, macOS or in Docker.
* **`protocol/`** - the shared protocol library (COBS/CRC framing,
  reliable resumable link, message codec) used by all three.

## Documentation

| Document | Content |
|---|---|
| [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) | analysis of all 11 variants: exports, classes, callbacks, evidence |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | per-variant and per-ordinal compatibility report, game list |
| [docs/DESIGN.md](docs/DESIGN.md) | architecture, component interfaces, state machines, compatibility strategy |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | wire protocol specification |
| [docs/BUILD.md](docs/BUILD.md) | building, flashing, server deployment, configuration, tests, hardware test plan |

## Quick start

1. **Server** (any PC on your network, or a public host):
   `cmake -S server -B build/server && cmake --build build/server && build/server/gamecommsd`
2. **Bridge**: flash an ESP32 with `pio run -d firmware -t upload`, join
   its `GameCommsBridge-Setup` Wi-Fi, enter your Wi-Fi (server `auto` on a
   LAN).
3. **N-Gage**: replace the game's `gamecomms.dll` with the build for its
   variant (`python tools/gc_identify.py dll gamecomms.dll`), start the
   game's multiplayer mode and pick the bridge once when asked.

Details and Internet setup: [docs/BUILD.md](docs/BUILD.md).

## Status

| Part | State |
|---|---|
| Reverse engineering | all variants analysed (Ghidra + objdump); two API generations identified; open questions listed in REVERSE_ENGINEERING.md §5 |
| Protocol, server, bridge core, DLL engine | implemented, covered by unit, simulation and network tests (`ctest`) |
| ESP32 firmware | implemented; built in CI |
| Symbian DLL adapter | implemented; needs the Windows N-Gage toolchain to build and real hardware to validate (see the hardware test plan) |

The previous prototype (line-based protocol, 1.x only) is kept in
`legacy/` for reference.

## License

MIT, see [LICENSE.md](LICENSE.md).  The files in `dll/symbian/bt/` are
derived from Nokia's 2002 Bluetooth example code (copyright Nokia), as in
the original repository.  The original DLLs in `reference/` and the SDK
documents in `sdk-docu/` are Nokia's and only included for analysis.
