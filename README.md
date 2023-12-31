# GameComms

[![documentation](https://github.com/mupfdev/GameComms/actions/workflows/documentation.yaml/badge.svg)](https://github.com/mupfdev/GameComms/actions/workflows/documentation.yaml)

GameComms is a connectivity library specialised for multidevice games
programming on the Series 60 platform to connect up to four devices via
Bluetooth.  GameComms was part of the proprietary Nokia N-Gage SDK and
was used in 44 of a total of 57 games.

This project is an attempt to reverse engineer the GameComms library and
create versions that can be used as drop-in replacements for the 11
different versions that were included in the official games.

The primary idea is to send the data handled by GameComms serially to a
WiFi-enabled microcontroller or a modern PC, for example, in order to
connect several devices via the Internet.

The project is hosted on [GitHub](https://github.com/mupfdev/GameComms).

# Protocol (Draft)

## Registration Sequence

GameComms can be configured using the file `E:\GameComms.ini`:

```ini
[Config]
DeviceName="mupfdev"
[Network]
Host="mupf.dev"
Port=9888
```

Invalid or non-existent settings are replaced by a default.

When you start a multiplayer game, a registration sequence is sent to the server:

```
UID:0x10005B8B
DID:mupfdev
NET:mupf.dev:8889
ROL:H
```

| KEY | Description                                        |
| :-: | :------------------------------------------------- |
| UID | Unique Game ID                                     |
| DID | DeviceName as specified in `GameComms.ini`         |
| NET | Host name and port as specified in `GameComms.ini` |
| ROL | Selected connection role, H = Host or C = Client   |

## Message Handling

All messages sent from the N-Gage to the ESP32 start with a 2 byte
header.  The first byte contains the intended recipient `01h` to `05h`
and the second byte contains the length of the subsequent data.  The end
of the message is terminated with a new line character `0Ah`.

Although the ESP32 hub should only forward messages intended for the
target device, the first byte of a received message always contains its
sender. This is important as the host must pass this information on to
the callback function `MGameBTCommsNotify::ReceiveDataFromClient()`.

Possible values for byte 1:

- `00h` Intentionally unused
- `01h` Host
- `02h` Client 1
- `03h` Client 2
- `04h` Client 3
- `05h` Broadcast

# Versions

Since I can only speculate about the development status of the
individual versions, I have created an overview of all games here that
make use of the library.  Sorted by file size of the `gamecomms.dll` and
by game title.

| Version | Game                                     | md5sum                           | Ordinals |
| :-----: |:---------------------------------------- | :------------------------------- | :------: |
|   00    | Nokia N-Gage SDK 1.0 Beta                | cb326c500bdb795494d55d30196b8711 |    33    |
|   01    | Marcel Desailly Pro Soccer               | 79c25cdd1972c287712475ee544c7bf3 |    33    |
|   02    | MotoGP                                   | 8863fdea897f1c63807ad69252a3566f |    55    |
|   02    | Pandemonium!                             | 8863fdea897f1c63807ad69252a3566f |    55    |
|   02    | Puyo Pop                                 | 8863fdea897f1c63807ad69252a3566f |    55    |
|   02    | Puzzle Bobble VS                         | 8863fdea897f1c63807ad69252a3566f |    55    |
|   02    | Tomb Raider                              | 8863fdea897f1c63807ad69252a3566f |    55    |
|   02    | Tony Hawk's Pro Skater                   | 8863fdea897f1c63807ad69252a3566f |    55    |
|   03    | Asphalt Urban GT                         | f6fbc3c745ae0ecf584b3ef7ec4371ef |    56    |
|   03    | FIFA Football 2004                       | f6fbc3c745ae0ecf584b3ef7ec4371ef |    56    |
|   03    | NCAA Football 2004                       | f6fbc3c745ae0ecf584b3ef7ec4371ef |    56    |
|   03    | Rayman 3                                 | f6fbc3c745ae0ecf584b3ef7ec4371ef |    56    |
|   03    | Tom Clancy's Ghost Recon: Jungle Storm   | f6fbc3c745ae0ecf584b3ef7ec4371ef |    56    |
|   04    | The Sims Bustin' Out                     | c2f184012cd0635f3065b7d5042702ce |    56    |
|   05    | Catan                                    | e8d317204a763d71215680d567eb71e0 |    57    |
|   05    | Mile High Pinball                        | e8d317204a763d71215680d567eb71e0 |    57    |
|   05    | Pathway to Glory                         | e8d317204a763d71215680d567eb71e0 |    57    |
|   05    | Requiem of Hell                          | e8d317204a763d71215680d567eb71e0 |    57    |
|   05    | The King of Fighters Extreme             | e8d317204a763d71215680d567eb71e0 |    57    |
|   05    | Tom Clancy's Splinter Cell: Chaos Theory | e8d317204a763d71215680d567eb71e0 |    57    |
|   06    | Call of Duty                             | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | FIFA Football 2005                       | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | High Seize                               | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | Pathway to Glory: Ikusa Islands          | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | SSX Out of Bounds                        | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | WWE Aftershock                           | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | Worms World Party                        | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | X-Men Legends II: Rise of Apocalypse     | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | X-Men Legends                            | bb02377c5361597f8d8503f5de584845 |    57    |
|   06    | Xanadu Next                              | bb02377c5361597f8d8503f5de584845 |    57    |
|   07    | Snakes                                   | f07f48ee44cca60046f40ce41858a160 |    57    |
|   08    | Ashen                                    | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   08    | Bomberman                                | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   08    | Colin McRae Rally 2005                   | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   08    | Crash Nitro Kart                         | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   08    | Operation Shadow                         | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   09    | Tiger Woods PGA Tour 2004                | 6ac8b1a1e490b367a1d5e46bb720fe70 |    57    |
|   10    | Glimmerati                               | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   10    | ONE                                      | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   10    | Rifts: Promise of Power                  | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   10    | System Rush                              | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   10    | The Roots: Gates of Chaos                | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   11    | Asphalt: Urban GT 2                      | 75536306ef48b1928b4562890d01c9c6 |    63    |
|   11    | Habbo Islands                            | 75536306ef48b1928b4562890d01c9c6 |    63    |
|   11    | Warhammer 40,000: Glory in Death         | 75536306ef48b1928b4562890d01c9c6 |    63    |

# License

This project's source code is, unless stated otherwise, licensed under
the "The MIT License".  See the file [LICENSE.md](LICENSE.md) for
details.
