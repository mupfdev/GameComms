# PhantomLink

[![PhantomLink](https://raw.githubusercontent.com/ngagesdk/PhantomLink/main/media/logo-modern.png)](https://raw.githubusercontent.com/ngagesdk/PhantomLink/main/media/logo-modern.png?raw=true "PhantomLink")

[![Continuous Integration](https://github.com/ngagesdk/PhantomLink/actions/workflows/continuous-integration.yml/badge.svg)](https://github.com/ngagesdk/PhantomLink/actions/workflows/continuous-integration.yml)

PhantomLink aims to provide drop-in replacements for **GameComms**, the proprietary
multiplayer communication library included in the Nokia N-Gage SDK.

GameComms provided a unified API that allowed up to four Nokia N-Gage devices to
communicate with each other over Bluetooth. PhantomLink replaces this functionality,
enabling existing N-Gage multiplayer games to be played over Wi-Fi and the Internet
without requiring any modifications to the original games.

To achieve this, PhantomLink uses a bridge device that communicates with the N-Gage
over Bluetooth and relays multiplayer data to a central server. The bridge can run
on an ESP32 development board or on a computer with Bluetooth support, such as a
Linux system (including a Raspberry Pi) or, with some limitations, Windows.

The server handles matchmaking between players running the same game and forwards
communication data between their devices, effectively extending the original
Bluetooth multiplayer functionality across the Internet.

## Project History

PhantomLink was originally started in **2023**. However, development was put on hold
due to the lack of useful documentation on GameComms and the challenges involved in
understanding and reproducing its underlying behavior.

In **2026**, the project was handed over to **Claude** with the goal of finally
bringing it into a functional, working state and turning the original concept into a
usable replacement for GameComms.

The ultimate goal remains simple: bring online multiplayer back to the Nokia N-Gage,
allowing players to enjoy existing multiplayer games regardless of physical distance.

## Why PhantomLink?

The name **PhantomLink** captures the idea of playing local multiplayer without being
physically local. The _Phantom_ represents the invisible presence of players who are
connected from somewhere else, seemingly sharing the same room despite being miles
apart. The _Link_ is the connection that makes this possible, bridging the gap between
the original Bluetooth experience and the world beyond. Together, PhantomLink lets
distant players become phantoms in each other's local multiplayer sessions.

## Requirements

To use PhantomLink and play Nokia N-Gage multiplayer games online, you need the
following:

### 1. A Nokia N-Gage game

- You need a copy of the game with copy protection removed, as PhantomLink replaces
  the original `GAMECOMMS.DLL`.

- Original N-Gage game cartridges are read-only, so the required modification cannot
  be applied directly to them.

- Naturally, we encourage you to use your own legally purchased games and create your
  own game copies for use with PhantomLink.

### 2. A Bluetooth bridge device

You need one of the following devices to connect your N-Gage to PhantomLink:

- An ESP32 development board with Bluetooth support.

- A Linux PC with Bluetooth support.

- A Raspberry Pi with Bluetooth support.

- A Windows PC with Bluetooth support (some limitations apply).

### 3. An Internet connection

- The bridge device needs Internet access to communicate with the PhantomLink server.
- All players must connect to the same server to find each other and play together.

### 4. A Nokia N-Gage device

- Obviously, you need a working Nokia N-Gage (Classic or QD) capable of running the
  game you want to play.

That's it! PhantomLink takes care of bridging the original Bluetooth multiplayer
communication over the Internet, allowing you to play classic N-Gage multiplayer
games with other players regardless of physical distance.

## Contents

- `dll/` - replacement GAMECOMMS.DLL for all 11 original variants
- `firmware/` - ESP32 bridge firmware
- `pcbridge/` - PC bridge for Linux and Windows (`gcbridge`), same job as the ESP32
- `server/` - endpoint server (`gamecommsd`)
- `protocol/` - protocol library shared by all three parts
- `tests/` - automated tests
- `tools/` - variant identification and reverse engineering scripts

## Documentation

- [docs/BUILD.md](docs/BUILD.md) - build, flash, deploy, configure, test
- [docs/DESIGN.md](docs/DESIGN.md) - architecture
- [docs/PROTOCOL.md](docs/PROTOCOL.md) - wire protocol
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) - variants, games, exports
- [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) - analysis of the original DLLs
- [docs/SECURITY_AUDIT.md](docs/SECURITY_AUDIT.md) - Security audit: gamecommsd

## Quick start

1. Server: build and run `gamecommsd` on a PC in your network
   (see docs/BUILD.md).
2. Bridge, one of:
   - Linux computer with Bluetooth, for example a Raspberry Pi: build and
     start `gcbridge`. One computer can serve several N-Gages.
   - Windows PC: `gcbridge.exe` works the same way, but depends on the
     Bluetooth adapter and may not work (see docs/BUILD.md).
   - ESP32: flash it with `pio run -d firmware -t upload`, connect to its
     `GameCommsBridge-Setup` Wi-Fi and enter your Wi-Fi settings. One
     ESP32 per N-Gage.
3. N-Gage: replace the game's `gamecomms.dll` with the matching build
   (see the list below), start a multiplayer game and select the bridge
   when asked. Or install `PhantomLink.sis`: it copies the right
   `gamecomms.dll` to `E:\System\Libs` for the game you pick and can
   configure the ESP32 bridge over Bluetooth (docs/BUILD.md).

## Which DLL for which game

Replace the game's `gamecomms.dll` with the build from the "Use" column
(`build/dll/out/vN/gamecomms.dll`). You can check the original with its
md5sum, or with `python tools/gc_identify.py dll gamecomms.dll`.

| Game | Use | md5sum of the original | Ordinals |
|---|---|---|---|
| Nokia N-Gage SDK 1.0 Beta | none (SDK only, same API as v1) | cb326c500bdb795494d55d30196b8711 | 33 |
| Marcel Desailly Pro Soccer | v1 | 79c25cdd1972c287712475ee544c7bf3 | 33 |
| MotoGP | v2 | 8863fdea897f1c63807ad69252a3566f | 55 |
| Pandemonium! | v2 | 8863fdea897f1c63807ad69252a3566f | 55 |
| Puyo Pop | v2 | 8863fdea897f1c63807ad69252a3566f | 55 |
| Puzzle Bobble VS | v2 | 8863fdea897f1c63807ad69252a3566f | 55 |
| Tomb Raider | v2 | 8863fdea897f1c63807ad69252a3566f | 55 |
| Tony Hawk's Pro Skater | v2 | 8863fdea897f1c63807ad69252a3566f | 55 |
| Asphalt Urban GT | v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | 56 |
| FIFA Football 2004 | v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | 56 |
| NCAA Football 2004 | v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | 56 |
| Rayman 3 | v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | 56 |
| Tom Clancy's Ghost Recon: Jungle Storm | v3 | f6fbc3c745ae0ecf584b3ef7ec4371ef | 56 |
| The Sims Bustin' Out | v4 | c2f184012cd0635f3065b7d5042702ce | 56 |
| Catan | v5 | e8d317204a763d71215680d567eb71e0 | 57 |
| Mile High Pinball | v5 | e8d317204a763d71215680d567eb71e0 | 57 |
| Pathway to Glory | v5 | e8d317204a763d71215680d567eb71e0 | 57 |
| Requiem of Hell | v5 | e8d317204a763d71215680d567eb71e0 | 57 |
| The King of Fighters Extreme | v5 | e8d317204a763d71215680d567eb71e0 | 57 |
| Tom Clancy's Splinter Cell: Chaos Theory | v5 | e8d317204a763d71215680d567eb71e0 | 57 |
| Call of Duty | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| FIFA Football 2005 | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| High Seize | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| Pathway to Glory: Ikusa Islands | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| SSX Out of Bounds | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| WWE Aftershock | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| Worms World Party | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| X-Men Legends | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| X-Men Legends II: Rise of Apocalypse | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| Xanadu Next | v6 | bb02377c5361597f8d8503f5de584845 | 57 |
| Snakes | v7 | f07f48ee44cca60046f40ce41858a160 | 57 |
| Ashen | v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | 57 |
| Bomberman | v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | 57 |
| Colin McRae Rally 2005 | v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | 57 |
| Crash Nitro Kart | v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | 57 |
| Operation Shadow | v8 | d4c10fcebfb6da8da6b757aa8392b6d7 | 57 |
| Tiger Woods PGA Tour 2004 | v9 | 6ac8b1a1e490b367a1d5e46bb720fe70 | 57 |
| Glimmerati | v10 | 827df54717bc210daa96aef9e12de4a3 | 63 |
| ONE | v10 | 827df54717bc210daa96aef9e12de4a3 | 63 |
| Rifts: Promise of Power | v10 | 827df54717bc210daa96aef9e12de4a3 | 63 |
| System Rush | v10 | 827df54717bc210daa96aef9e12de4a3 | 63 |
| The Roots: Gates of Chaos | v10 | 827df54717bc210daa96aef9e12de4a3 | 63 |
| Asphalt: Urban GT 2 | v11 | 75536306ef48b1928b4562890d01c9c6 | 63 |
| Habbo Islands | v11 | 75536306ef48b1928b4562890d01c9c6 | 63 |
| Warhammer 40,000: Glory in Death | v11 | 75536306ef48b1928b4562890d01c9c6 | 63 |

Always use the matching version. v1 and v2 to v11 have different APIs and
cannot be mixed. Other combinations are not tested.

## Status

- Protocol, server, bridge and DLL logic: implemented and covered by
  automated tests.
- ESP32 firmware: implemented, built in CI.
- PC bridge: implemented and tested with simulated devices (Linux: against
  a simulated BlueZ). Linux still has to be tested with a real N-Gage. On
  Windows the first real test failed inside the Windows Bluetooth stack
  (see docs/BUILD.md, "Windows: known problem").
- Symbian DLLs: implemented. They need the Windows N-Gage toolchain to
  build and still have to be tested on real hardware.

## License

MIT, see [LICENSE.md](LICENSE.md).  

[![Co-created with AI](https://madebyhuman.iamjarl.com/badges/co-created-white.svg)](https://madebyhuman.iamjarl.com)

The files in `dll/symbian/bt/` are based on Nokia's Bluetooth example code.

Both [PhantomLink logos](media/) were provided by [Dan Whelan](https://danwhelan.ie/)
and are released under the
[CC0 1.0 Universal license](https://creativecommons.org/publicdomain/zero/1.0/).

You are free to use, modify, and distribute them without restriction.
