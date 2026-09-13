# Compatibility and validation

[Documentation](README.md) · [Wire protocol](protocol.md)

## API version versus transport revision

The first public release is **SDK 1.0.0**, paired with
**CanoFlash firmware 1.0.0**.

| SDK version | Compatible device firmware |
| --- | --- |
| 1.0.0 | CanoFlash 1.0.0 |

SDK and firmware release numbers are distinct from internal protocol versions.
The status packet remains version 3 and the relay session protocol remains
version 2; neither needs renumbering for the first public release.

Pin the SDK release used by your game. The earlier 0.1.0 identifier belonged
to unpublished development builds and is not a supported public release.

The current SDK requires:

| Boundary | Required support |
| --- | --- |
| GBA ↔ device status | `CMD_NET_STATUS_V3` (0x233), 20-byte response, version 3 |
| GBA ↔ device normal data | `CMD_NET_TICK_V2` (0x3A), 72-byte exchange |
| GBA ↔ device reliable data | SEND_V2 (0x135) and GET_V2 (0x136) |
| Device ↔ relay | Session protocol version 2, heartbeat, seat-token resume and generation-aware reliable delivery |
| Game ↔ game | Compatible application payloads and the same SDK fragment format |

For future releases, extend the compatibility table with the tested firmware
versions. Matching release numbers alone do not negotiate protocol support.

Legacy commands remaining available in firmware do not make the new SDK
compatible with old firmware or old game payload formats.

## Updating an installation

For maintainers updating the whole stack:

1. Deploy the compatible relay.
2. Update the CanoFlash device firmware.
3. Rebuild and distribute compatible game ROMs.

For game developers using the managed service, ensure players have compatible
firmware and use the same game build. The C API has no server URL or certificate
override.

Isolate incompatible game protocols with matchmaking parameters and a
post-join version exchange, or with separate registered game keys. Code joins
do not filter by matchmaking parameters.

## Validation scope

The development integration has been checked with:

- Standalone builds of all three examples using devkitARM 15.2.0 and Python
  3.14.4. The Butano examples were also built against an unmodified checkout of
  [Butano 92e94dd](https://github.com/GValiente/butano/commit/92e94dd6f3b5539132a9f1b152991b7934d90739).
- Host tests for session/data handling, metadata, TLS and WebSocket validation
  in the full CanoFlash development project.
- Relay tests for rooms, authentication, reconnection, reliable ordering and
  registry failures in that project.
- Real hardware exercises of connection, room operations, the lobby interface
  and Wi-Fi recovery.

Those firmware/relay test harnesses are not bundled in this standalone SDK.
The [examples](../examples/README.md) are the build and integration starting point.

The `v1.0.0` tag identifies the exact SDK revision checked for the first public
release on 2026-09-13. This publication repeated the standalone builds;
hardware coverage above comes from development testing, not a new hardware run.

Systematic testing with three or more simultaneous consoles, full queue
pressure and concurrent reconnections remains a release validation task.
Support for eight protocol slots is not a claim of eight-console performance.

Old delivery percentages, latency measurements and the approximately 1 KiB
memory estimate described an earlier implementation. See
[current resource notes](integration.md#memory-and-resource-use); measure
performance for the current transport and your game.

## Hardware validation

Before releasing a game or changing the transport:

- Connect from cold boot and recover from a missing/disabled device.
- Create public/private rooms; join a valid, missing, full and playing room.
- Leave and create another room without reconnecting the session.
- Exchange normal and reliable messages with at least two players.
- Check three or more players: sender identity, slot reuse and master changes.
- Drop Wi-Fi briefly, including while reliable events are pending.
- Test an outage past the seat grace, device reset and an unavailable service.
- Confirm menus keep polling and event consumers do not silently fill queues.
- Exercise audio/interrupt workloads if your game uses them.
- Record firmware, SDK revision, ROM build and observed limitations.

The supplied Makefiles build cartridge-format ROMs. A custom multiboot target
needs its own linker/startup and memory-budget validation.
