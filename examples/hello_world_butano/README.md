# Hello world — Butano

[All examples](../README.md) · [Source](src/main.cpp)

The two-square demo using Butano sprites and C++. It follows the same session,
matchmaking and polling flow as the [plain C example](../hello_world/README.md).

## Build and run

Install Butano/Python and set the [shared key](../README.md#set-your-game-key).
From the SDK root:

```sh
make -C examples/hello_world_butano LIBBUTANO=/path/to/butano/butano -j2
```

The path must contain `butano.mak`; `DEVKITARM` must be set.
Output: `examples/hello_world_butano/hello_world_butano.gba`.

Load the same ROM on two consoles with configured CanoFlash devices.
Connection starts automatically. Both use a public two-seat room with empty
parameters. Do not mix this ROM with the plain C example: their coordinates
use different origins.

## Screen and controls

| Display | Meaning |
| --- | --- |
| One blinking error sprite | Invalid configuration or no responding device |
| Two blinking error sprites | Session/room could not be opened |
| Three blinking error sprites | Link/session failed |
| One sprite blinking in the waiting phase | Waiting for the other participant |
| Two moving squares | Local and received remote state |

Use the D-pad to move. Reset after an error; the minimal demo has no exit menu.

## Integration pattern

`while_waiting(int frames)` calls `bn::core::update()` for each requested
frame. The game loop calls `cf_poll()` once, then advances Butano once.
The SDK header provides C linkage; no additional `extern "C"` wrapper is needed.

To add the core to another Butano project:

```make
SOURCES  := src vendor/canoflash-sdk/src
INCLUDES := include vendor/canoflash-sdk/include
```

Compile `canoflash.c` once. See [Integration](../../docs/integration.md)
for serial-port ownership, interrupt effects and frame-rate-dependent polling.

## Scope

This demo sends normal position state only. It has basic status handling, not
a complete recovery or authoritative-state protocol. The [lobby](../lobby/README.md)
adds room selection and UI; [Reliable events](../../docs/reliable.md) explains
the event channel.
