# CanoFlash Net SDK

**Online multiplayer for Game Boy Advance homebrew, through the CanoFlash device.**

A C99 library that connects your game to rooms on the CanoFlash relay. The
device handles Wi-Fi, TLS and player authentication. Your game finds players,
exchanges state and sends reliable events through a small C API.

```text
Your GBA game  ⇄  Link port  ⇄  CanoFlash  ⇄  Wi-Fi / TLS  ⇄  Relay
```

[Get started](docs/getting-started.md) · [Examples](examples/README.md) ·
[API reference](docs/api.md) · [Troubleshooting](docs/troubleshooting.md)

## Features

- Public matchmaking and private rooms with six-digit codes.
- Rooms with 2–8 seats, subject to the relay's configured limit.
- Lobbies with player names, a room master and waiting/playing states.
- Replaceable state updates and reliable game events.
- Recovery from temporary network loss while the relay still holds the seat.

The core is two files: [canoflash.h](include/canoflash.h) and
[canoflash.c](src/canoflash.c). It uses no engine headers or dynamic allocation.
The examples cover plain devkitARM C and Butano C++.

## Status and compatibility

**Version 1.0.0**, the first public release, pairs with
**CanoFlash firmware 1.0.0**. The integration has been exercised on real GBA
hardware, including the lobby, room operations and Wi-Fi recovery. Pin the
SDK version used by your game. See the [changelog](CHANGELOG.md).

Use matching device firmware and relay support. An older firmware may not
understand this SDK even if compilation succeeds. See
[Compatibility](docs/compatibility.md) for requirements and validation scope.

This SDK provides multiplayer transport. It does not include game simulation,
rollback, anti-cheat, cloud saves or leaderboards.

## Requirements

| Requirement | Why |
| --- | --- |
| A GBA-compatible console and CanoFlash per player | Communication uses the physical Link port |
| CanoFlash configured for Wi-Fi and linked to an account | The device authenticates the player |
| A registered game API key | Keeps your game's rooms separate |
| A way to load your GBA program | The supplied Makefiles build cartridge-format ROMs |
| devkitARM and the devkitPro GBA tools | Compile and package the ROM |
| Butano and Python 3, for the Butano examples | Build their graphics and C++ interface |

You can compile without a device. Ordinary emulator Link support does not
provide a CanoFlash internet connection. See [loading and testing](docs/getting-started.md#load-and-test).

## Start with an example

Clone the standalone SDK:

```sh
git clone https://github.com/supercanocoder/canoflash-sdk.git
cd canoflash-sdk
```

For a fixed release, check out the `v1.0.0` tag before building.

1. Install the [toolchain](docs/getting-started.md#install-the-toolchain).
2. Set your game key in the [local example configuration](examples/README.md#set-your-game-key).
3. Build from the SDK directory:

```sh
make -C examples/hello_world
```

Load `examples/hello_world/hello_world.gba` on two consoles, each connected to
its own configured CanoFlash. Both must use the same game key and compatible
ROM. The first player waits; the master starts when both seats are occupied.

| Example | Demonstrates |
| --- | --- |
| [Hello world](examples/hello_world/README.md) | Two-player movement, plain C, no graphics assets |
| [Hello world with Butano](examples/hello_world_butano/README.md) | The same session and matchmaking flow in C++ |
| [Lobby](examples/lobby/README.md) | Create/join/matchmake menus, names, room codes, pause and reconnection UI; no audio |

## Add the SDK to your game

Compile `src/canoflash.c` **once** and add `include/` to your include path.
For a devkitPro-style project:

```make
SOURCES  := src vendor/canoflash-sdk/src
INCLUDES := include vendor/canoflash-sdk/include
```

Include `canoflash.h` from C or C++; the header already supplies C linkage.
There is no prebuilt SDK library to install. See
[Integration](docs/integration.md) for build, memory and interrupt constraints.

## The connection lifecycle

Connecting opens a **session**. Joining a **room** is a separate step.

```c
#include "canoflash.h"

/* Fragment inside your game's online entry point.
   wait_frames(int n) must advance n display frames. */
cf_config_t config;
cf_config_init(&config);
config.api_key = "cfn_REPLACE_WITH_YOUR_GAME_KEY";

if (!cf_connect(&config, wait_frames)) {
    /* Show a connection error; return to your menu. */
    return;
}

cf_room_t room;
cf_room_init(&room);
room.max_players = 2;
room.params = "wire=1;mode=demo";

if (!cf_matchmake(&room, wait_frames)) {
    /* Inspect cf_join_error() before offering a retry. */
    cf_disconnect();
    return;
}

/* Now call cf_poll() once per frame, including while waiting for players.
   The master requests CF_ROOM_PLAYING; everyone waits for confirmation. */
```

This shows the API order; [hello_world/main.c](examples/hello_world/main.c)
is the complete buildable program.

During a session:

- Call `cf_poll()` once per frame, from one place in your game loop.
- Use `cf_status()` for connection health and `cf_in_room()` for membership.
- Pause simulation during `CF_RECONNECTING`, while continuing to poll.
- Use `cf_leave_room()` for a connected menu; use `cf_disconnect()` when
  leaving online mode.

## Choose the right channel

| | State: `cf_send()` | Events: `cf_send_reliable()` |
| --- | --- | --- |
| Typical use | Positions and repeated snapshots | Turns, actions and phase changes |
| Maximum payload | 480 bytes | 480 bytes |
| Submission | Copies locally; returns `bool` | Copies locally; returns a ticket or 0 |
| Transmission | Performed by `cf_poll()` | Performed by `cf_poll()` |
| Contract | Latest complete state per sender; intermediate updates may be lost | Ordered, deduplicated delivery within the live room session, with bounded queues |
| Confirmation | No end-to-end acknowledgement | `DELIVERED` confirms relay admission, not processing by every game |

Receive both through `cf_poll()`. After a successful receive, read the source
with `cf_last_sender()` and `cf_last_sender_generation()`. Use a
`CF_MAX_MESSAGE` buffer, validate type and length, then decode the payload.
Messages are never truncated to fit your buffer.

Read [payload design](docs/integration.md#design-your-payloads) and
[Reliable events](docs/reliable.md) before defining your game protocol.

## Documentation

| Guide | Contents |
| --- | --- |
| [Getting started](docs/getting-started.md) | Install, configure, build and run |
| [Integration](docs/integration.md) | Game loop, payloads, timing, memory and C++ |
| [Rooms](docs/rooms.md) | Matchmaking, private codes, metadata and master migration |
| [Reliable events](docs/reliable.md) | Tickets, ordering, queues and acknowledgements |
| [Reconnection](docs/reconnecting.md) | Recovery, absent peers and resynchronisation |
| [API reference](docs/api.md) | Every public function, type and constant |
| [Game API keys](docs/api-keys.md) | Registration, identity and version separation |
| [Troubleshooting](docs/troubleshooting.md) | Build, connection and gameplay symptoms |
| [Compatibility](docs/compatibility.md) | Requirements, migration and validation scope |
| [Wire protocol](docs/protocol.md) | SPI and relay formats for maintainers and ports |

## Contributing and licence

See [CONTRIBUTING.md](CONTRIBUTING.md) for reproducible reports and validation.
The SDK is [MIT licensed](LICENSE). Bundled third-party assets retain their
notices; see [THIRD_PARTY.md](THIRD_PARTY.md).
