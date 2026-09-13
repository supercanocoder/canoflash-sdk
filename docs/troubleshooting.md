# Troubleshooting

[Documentation](README.md) · [API reference](api.md)

Start from one unmodified example with your own configured key. Use the same
ROM on all participants and record the SDK revision and device firmware.

## Build problems

| Symptom | Check |
| --- | --- |
| `DEVKITARM is not set` or compiler missing | Install the devkitPro GBA tools; verify environment paths |
| `gbafix` missing | Install devkitPro's GBA tools and check `DEVKITPRO` |
| `LIBBUTANO does not point at a Butano checkout` | The directory must contain `butano.mak` |
| Python/graphics import failure | Verify Python 3 and follow Butano's setup instructions |
| Undefined `cf_*` symbols | Compile/link `src/canoflash.c` once; include paths alone are insufficient |
| Duplicate `cf_*` symbols | Remove duplicate source/object inclusion |
| Old key or assets after editing | Clean and rebuild the chosen example |

See [Getting started](getting-started.md) for commands. The SDK and example
source paths are relative to this checkout; no sibling project is required.

## Connection problems

| Symptom | Check |
| --- | --- |
| `cf_connect()` returns false with `CF_DISCONNECTED` | Validate nonempty config/key first; then device power, cable and Link connection |
| False with `CF_FAILED` | Firmware compatibility, Wi-Fi, account linkage, game registration and service availability |
| `CF_CONNECTED` but no peers | Connection opens a session only; call a room operation |
| Connection takes several seconds | Normal for probing/authentication/TLS; keep a waiting screen and frame callback |
| An emulator never connects | Standard emulator Link support does not emulate CanoFlash |
| Works until a long menu | Ensure that menu calls `cf_poll()` once per frame |
| Device logs ignored `0xCAFEBABE` or `0x00000048` repeatedly | Capture the complete boot/connection log and verify matching firmware; these logs alone do not identify a server problem |

The API does not expose every backend/TLS error. ESP32 serial logs can help
distinguish an SPI failure from authentication, TLS or relay rejection. Remove
credentials and personal network details before sharing logs.

## Rooms do not match

Compare the game key, parameter string, effective capacity and room visibility.
Parameters must match byte for byte and fit 31 bytes. A full, private or playing
room is not a matchmaking candidate. Away seats still count as occupied.

For joining by code, check `cf_join_error()`:
[all error values](api.md#cf_join_error-and-cf_join_error_t). A busy result means
the previous room operation has not settled; continue polling before retrying.
A timeout is an uncertain result with cancellation requested, not proof of a
wrong code.

## Data is missing or appears to lag

1. Confirm `CF_CONNECTED` **and** `cf_in_room()`.
2. Call `cf_poll()` once per frame, including pause/recovery screens.
3. Receive into `uint8_t buffer[CF_MAX_MESSAGE]` and validate returned lengths.
4. Read sender/channel metadata only after a nonzero receive.
5. Check the game's actual frame rate and [tick quantisation](integration.md#timing-and-interrupts).
6. Keep frequent state messages small; 480 bytes requires eight normal ticks.

`cf_send()` false often means an earlier state update is pending. Retry with
the latest state on a later frame. Invalid length, NULL data and unavailable
room also return false; do not treat every false as harmless queue pressure.

Normal messages can be replaced or lost. Use reliable events for actions that
must not disappear between snapshots.

## Reliable events stall or fail

Keep polling and consuming events. A small output buffer retains an oversized
event. `cf_poll(NULL, 0)` does not drain reliable data, and a full receive
queue eventually activates relay retry/removal policy.

A zero ticket means local rejection; check room state, length and the previous
ticket. A nonzero ticket already belongs to the SDK's retry mechanism.
`CF_DELIVERY_LOST` can have an unknown remote outcome: do not blindly submit
a second copy. See [Reliable events](reliable.md).

`CF_DELIVERY_DELIVERED` does not mean all games processed the action.
Use application acknowledgements if the next phase depends on that.

## Audio glitches or frame hitches

Serial exchanges temporarily mask interrupts, including during some polls.
Measure with the actual engine/audio workload. A wait callback does not make
all serial operations nonblocking. The lobby has no audio by design; changing
`guard_us` alone is not a general audio solution.

## Reconnection works, but the game state is wrong

Transport resume restores the room and seat, not a complete game snapshot.
Reconcile with the current master and handle queued events by round/version.
Master migration can happen while a player is away. See [Reconnection](reconnecting.md).

## Report a reproducible issue

Include:

- SDK revision, device firmware and console/loader model.
- Example or minimal source, build command and toolchain version.
- Number of participants and the exact sequence that triggers the problem.
- `cf_status()`, `cf_join_error()` and before/after `cf_get_stats()` counters.
- Sanitised serial logs, if available.

Use the repository's issue tracker when enabled. See [Contributing](../CONTRIBUTING.md).
