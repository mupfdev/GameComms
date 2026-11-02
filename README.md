# GameComms

[![ci](https://github.com/mupfdev/GameComms/actions/workflows/ci.yaml/badge.svg)](https://github.com/mupfdev/GameComms/actions/workflows/ci.yaml)

GameComms was the multiplayer library of the Nokia N-Gage SDK. Most
N-Gage multiplayer games use it to connect up to four devices over
Bluetooth. This project replaces it so that these games can play over
Wi-Fi and the Internet, without changes to the games.

The N-Gage talks Bluetooth to an ESP32 bridge. The bridge connects over
Wi-Fi to a server, which matches players of the same game and forwards
their data.

## Contents

- `dll/` - replacement GAMECOMMS.DLL for all 11 original variants
- `firmware/` - ESP32 bridge firmware
- `server/` - endpoint server (`gamecommsd`)
- `protocol/` - protocol library shared by all three parts
- `tests/` - automated tests
- `tools/` - variant identification and reverse engineering scripts
- `legacy/` - the previous prototype, kept for reference

## Documentation

- [docs/BUILD.md](docs/BUILD.md) - build, flash, deploy, configure, test
- [docs/DESIGN.md](docs/DESIGN.md) - architecture
- [docs/PROTOCOL.md](docs/PROTOCOL.md) - wire protocol
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) - variants, games, exports
- [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) - analysis of the original DLLs

## Quick start

1. Server: build and run `gamecommsd` on a PC in your network
   (see docs/BUILD.md).
2. Bridge: flash an ESP32 with `pio run -d firmware -t upload`, connect
   to its `GameCommsBridge-Setup` Wi-Fi and enter your Wi-Fi settings.
3. N-Gage: replace the game's `gamecomms.dll` with the matching build
   (see the list below), start a multiplayer game and select the bridge
   when asked.

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
- Symbian DLLs: implemented. They need the Windows N-Gage toolchain to
  build and still have to be tested on real hardware.

## License

MIT, see [LICENSE.md](LICENSE.md). The files in `dll/symbian/bt/` are
based on Nokia's Bluetooth example code. The original DLLs in
`reference/` and the documents in `sdk-docu/` belong to Nokia and are
only included for analysis.
