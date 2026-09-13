# Hello world — plain C

[All examples](../README.md) · [Source](main.c)

Two players move squares with the D-pad. This example uses devkitARM and a
mode-3 bitmap, with no engine, font or graphics assets.

## Build and run

Set the [shared example key](../README.md#set-your-game-key), then from the SDK root:

```sh
make -C examples/hello_world
```

Output: `examples/hello_world/hello_world.gba`. The Makefile uses
`DEVKITARM` and `DEVKITPRO`, defaulting to the usual `/opt/devkitpro`
installation if unset.

Load the same ROM on two consoles, each with a configured CanoFlash connected.
The example probes the device automatically, opens a session and matchmakes
into a public two-seat room with empty parameters. The master starts once
both seats are occupied. See [Loading](../../docs/getting-started.md#load-and-test).

## Screen and controls

| Display | Meaning |
| --- | --- |
| White bar moving across the top | Connection wait callback is running |
| Bar along the bottom | Waiting for the room to fill |
| One blinking square | Invalid configuration or no responding device |
| Two blinking squares | Session/room could not be opened |
| Three blinking squares | Link/session failed during play |
| Two moving squares | Local movement and received remote state |

The D-pad moves your square. Error screens are terminal in this minimal
example; reset after correcting the problem. It has no online exit menu.

## Read the networking in order

1. `cf_config_init()` and `cf_connect()` open the session.
2. `cf_room_init()` and `cf_matchmake()` enter a room.
3. `cf_poll()` updates the waiting lobby; the master requests playing.
4. Each game frame polls once and queues the local position with `cf_send()`.

The four-byte position struct contains signed 16-bit x/y coordinates.
Only matching builds should exchange it. There is one remote participant, so
this example does not need to route multiple sender slots.

## Scope

The example demonstrates normal state transport and basic connection status.
It does not implement reliable events, full state reconciliation or a complete
reconnection interface. See [Lobby](../lobby/README.md) for menus/codes and
[Reliable events](../../docs/reliable.md) for acknowledged application events.

An emulator can test rendering and startup, but requires real CanoFlash
hardware for the network path.
