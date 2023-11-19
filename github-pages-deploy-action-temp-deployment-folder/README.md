# GameComms

[![documentation](https://github.com/mupfdev/GameComms/actions/workflows/documentation.yaml/badge.svg)](https://github.com/mupfdev/GameComms/actions/workflows/documentation.yaml)

GameComms is a connectivity library specialised for multidevice games
programming on the Series 60 platform to connect up to four devices via
Bluetooth.  GameComms was part of the proprietary Nokia N-Gage SDK and
was used in 42 of a total of 55 games.

This project is an attempt to reverse engineer the GameComms library and
create versions that can be used as drop-in replacements for the 10
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

All incoming and outgoing messages consist of 3 bytes each.  The first
byte represents either the recipient, the sender or a command specifier:

| Byte 1 | Description       |
| :----: | :---------------- |
| `0x00` | Client 0 (Host)   |
| `0x01` | Client 1          |
| `0x02` | Client 2          |
| `0x03` | Client 3          |
| `0x42` | Broadcast         |
| `0x43` | Connect           |
| `0x44` | Disconnect        |
| `0x50` | Pause Game        |
| `0x52` | Resume Game       |

The second byte holds the payload.  If the first byte is a command
specifier, the payload contains either the ID of the sender or the
receiver.

The byte sequence is always terminated with a newline character `\n`.

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
|   07    | Ashen                                    | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   07    | Bomberman                                | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   07    | Colin McRae Rally 2005                   | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   07    | Crash Nitro Kart                         | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   07    | Operation Shadow                         | d4c10fcebfb6da8da6b757aa8392b6d7 |    57    |
|   08    | Tiger Woods PGA Tour 2004                | 6ac8b1a1e490b367a1d5e46bb720fe70 |    57    |
|   09    | Glimmerati                               | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   09    | ONE                                      | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   09    | Rifts: Promise of Power                  | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   09    | System Rush                              | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   09    | The Roots: Gates of Chaos                | 827df54717bc210daa96aef9e12de4a3 |    63    |
|   10    | Asphalt: Urban GT 2                      | 75536306ef48b1928b4562890d01c9c6 |    63    |
|   10    | Warhammer 40,000: Glory in Death         | 75536306ef48b1928b4562890d01c9c6 |    63    |

# License

This project's source code is, unless stated otherwise, licensed under
the "The MIT License".  See the file [LICENSE.md](LICENSE.md) for
details.