# API reference

[Documentation](README.md) · [Getting started](getting-started.md) ·
[Public header](../include/canoflash.h)

All functions use C linkage and operate on one global session. Call from the
main game loop, not interrupts or an SDK wait callback. Returned strings belong
to the SDK; copy them if you need a snapshot across later polls or room changes.

## Contents

- [Configuration and constants](#configuration-and-constants)
- [Session](#session)
- [Room operations](#room-operations)
- [Room metadata](#room-metadata)
- [Normal messages](#normal-messages)
- [Reliable messages](#reliable-messages)
- [Receiving](#receiving)
- [Diagnostics](#diagnostics)

## Configuration and constants

### Constants

| Name | Value | Meaning |
| --- | --- | --- |
| `CF_VERSION_MAJOR` | 1 | API version component |
| `CF_VERSION_MINOR` | 0 | API version component |
| `CF_VERSION_PATCH` | 0 | API version component |
| `CF_MAX_MESSAGE` | 480 | Maximum payload on either channel |
| `CF_ROOM_MAX` | 24 | Legacy constant; not the current parameter or code limit |

These macros identify SDK **1.0.0**, paired with CanoFlash firmware **1.0.0**.
Protocol versions are separate; see [Compatibility](compatibility.md).

Current string limits: game key **63 bytes**, room parameters **31 bytes**,
room code **6 ASCII digits**, player name **15 UTF-8 bytes**. Storage includes
an additional terminator. Keys/parameters exceeding their SPI fields are
silently truncated, so validate input lengths before submitting.

### Configuration types

```c
typedef struct {
    const char *api_key;
    uint8_t tick_hz;
    uint16_t guard_us;
} cf_config_t;

typedef enum { CF_PRIVATE = 0, CF_PUBLIC = 1 } cf_visibility_t;

typedef struct {
    uint8_t max_players;
    cf_visibility_t visibility;
    const char *params;
} cf_room_t;
```

| Field | Default and behaviour |
| --- | --- |
| `api_key` | NULL; a nonempty registered game key is required |
| `tick_hz` | 0 selects 20 Hz; clamped above 60 and quantised to whole frame intervals |
| `guard_us` | 0 selects 5 microseconds; otherwise clamped to 1–500 |
| `max_players` | 2; use 2–8, subject to the relay's lower configured cap |
| `visibility` | `CF_PUBLIC`; creation only, matchmaking creates public rooms |
| `params` | NULL; equivalent to an empty matchmaking parameter string |

```c
void cf_config_init(cf_config_t *cfg);
void cf_room_init(cf_room_t *room);
```

Both accept NULL as a no-op. `cf_config_init()` zeroes the struct; the
effective 20 Hz / 5 microsecond defaults are applied by `cf_connect()`.
`cf_room_init()` sets two seats, public visibility and NULL parameters.
See [Timing](integration.md#timing-and-interrupts) for tick-rate rounding.

## Session

### cf_connect

```c
bool cf_connect(const cf_config_t *cfg, void (*wait_frames)(int n));
```

Resets local session state and statistics, probes the device, submits the game
key and waits for an authenticated relay session. **Does not join a room.**

Returns true when the session is connected. False can leave:

| State | Interpretation |
| --- | --- |
| `CF_DISCONNECTED` | Invalid/null configuration, empty key, or no responding device |
| `CF_FAILED` | OPEN failed, incompatible firmware, network/authentication failure, or connection wait exhausted |

Validate configuration first before interpreting `DISCONNECTED` as a
hardware fault. The API does not expose a detailed connection error code.

Expect seconds rather than one frame. Device probing allows 360 callback
frames; connection waiting adds repeated serial queries and callback waits.
There is no guaranteed wall-clock duration. Use a waiting screen.

The callback must advance `n` display frames without calling back into the
SDK. NULL provides no rendering/frame pacing. See
[Blocking calls](integration.md#blocking-calls-and-wait-callbacks).

Call `cf_disconnect()` before explicitly starting a fresh session.

### cf_disconnect

```c
void cf_disconnect(void);
```

Attempts to close the device session, then clears local state. Use when exiting
online mode. The close command has no end-to-end confirmation; if the cable or
network is already gone, the relay may retain the old seat until its timeout.
For a connected menu between matches, use `cf_leave_room()` instead.

### cf_status and cf_state_t

```c
cf_state_t cf_status(void);
```

Returns cached state without I/O:

| Value | Meaning |
| --- | --- |
| `CF_DISCONNECTED = 0` | No active SDK session |
| `CF_CONNECTING = 1` | Initial connection in progress |
| `CF_CONNECTED = 2` | Session ready; check `cf_in_room()` separately |
| `CF_FAILED = 3` | Attempt/session failed; return to an offline or retry UI |
| `CF_RECONNECTING = 4` | Recovery in progress; pause simulation and keep polling |

Status is refreshed by SDK operations and polls, not by the getter.

## Room operations

All joins require a connected session with no current room or pending
join/leave. They wait for a result using the same callback convention as
`cf_connect()`. A failed room operation does not necessarily end the session.

### cf_create_room

```c
const char *cf_create_room(const cf_room_t *room, void (*wait_frames)(int n));
```

Creates a new room. Returns an SDK-owned code string on success, NULL on
failure. A NULL room uses two public seats and empty parameters.

The initial metadata read can fail even after admission: a non-NULL return can
temporarily point to an empty string. Continue polling and display
`cf_room_code()` when it becomes available.

### cf_join_code

```c
bool cf_join_code(const char *code, void (*wait_frames)(int n));
```

Joins an existing room for this game by six-digit code, including private rooms.
Never creates a missing room. Returns true on admission. NULL/empty code returns
false with `CF_JOIN_NO_SUCH_ROOM`; supply exactly six ASCII digits.

Code joins do not compare your desired parameters/capacity. Games with several
wire versions must also negotiate compatibility after a code join.

### cf_matchmake

```c
bool cf_matchmake(const cf_room_t *room, void (*wait_frames)(int n));
```

Finds a public waiting room matching game key, effective capacity and parameter
string exactly. If none has space, creates one. Returns true on admission.
NULL uses the default room options. `visibility` does not make matchmaking
create a private room.

### cf_join_error and cf_join_error_t

```c
cf_join_error_t cf_join_error(void);
```

Cached result of the last join attempt:

| Value | Meaning / recovery |
| --- | --- |
| `CF_JOIN_OK = 0` | No join error |
| `CF_JOIN_NO_SUCH_ROOM = 1` | Missing/invalid code, or room belongs to another game |
| `CF_JOIN_FULL = 2` | All seats occupied or reserved |
| `CF_JOIN_PLAYING = 3` | New players cannot join a playing room |
| `CF_JOIN_NO_SESSION = 4` | Session is not connected |
| `CF_JOIN_NO_LINK = 5` | Device status/request exchange failed |
| `CF_JOIN_BUSY = 6` | Already in a room, or a join/leave is pending |
| `CF_JOIN_TIMEOUT = 7` | Result not confirmed in time; cancellation requested |
| `CF_JOIN_FAILED = 8` | Other relay rejection |

After BUSY or TIMEOUT, keep polling and let pending work settle. Do not blindly
repeat CREATE: the previous outcome may not yet be known.

### cf_leave_room

```c
void cf_leave_room(void);
```

Requests leaving/cancellation while keeping the session. Waits briefly for
confirmation. If delayed, membership can remain true and sends/new joins are
refused until the operation settles. Keep polling.

No-op when not connected or when no room operation exists. Confirmed leaving
clears room payload buffers and marks a pending outgoing reliable ticket LOST.
Leaving voluntarily does not reserve a seat for resume.

## Room metadata

These getters do no I/O. The normal status/metadata cadence is every **20
successful ticks**, about one second at the default rate, and slower with low
frame rates or failed exchanges. Metadata is also requested after joining.

| Signature | Result |
| --- | --- |
| `bool cf_in_room(void)` | Confirmed membership; may remain true during pending leave/recovery |
| `const char *cf_room_code(void)` | Six-digit code, or empty when absent/not yet available |
| `const char *cf_player_name(uint8_t slot)` | SDK-owned UTF-8 name; empty for unknown/invalid seat |
| `uint8_t cf_slot(void)` | Local slot, 0–7; meaningful only in a room |
| `uint8_t cf_peers(void)` | Occupied seats including self and temporarily absent players |
| `uint8_t cf_peer_mask(void)` | Bit N set for occupied/reserved seat N |
| `uint8_t cf_away_mask(void)` | Bit N set for a temporarily absent occupant |
| `uint8_t cf_capacity(void)` | Effective room capacity; zero outside a room |
| `bool cf_is_master(void)` | Connected local player is the master, with no pending leave |
| `uint8_t cf_master_slot(void)` | Master slot, or 0xFF if unknown/no master |
| `cf_room_state_t cf_room_state(void)` | Cached waiting/playing state |

Names are at most 15 bytes plus NUL. Adapt unsupported characters to your font
and provide a fallback while metadata is pending. Occupancy and names can
refresh at different times; a name is not a stable player identifier.

### cf_set_room_state and cf_room_state_t

```c
typedef enum {
    CF_ROOM_WAITING = 0,
    CF_ROOM_PLAYING = 1
} cf_room_state_t;

bool cf_set_room_state(cf_room_state_t state);
```

Only the master may request a change. Returns false if the master precondition
fails; true means the command was attempted, **not relay confirmation**.
Continue polling and wait for `cf_room_state()` to report the desired state.
Use only the two enum values. See [Rooms](rooms.md).

## Normal messages

### cf_send and cf_can_send

```c
bool cf_send(const void *data, uint16_t len);
bool cf_can_send(void);
```

`cf_send()` copies 1–480 bytes for broadcast to the other room occupants.
The sender receives no echo. Polling transmits one fragment per successful tick.

Returns false for NULL data, invalid length, no active connected room, a pending
join/leave, or an outgoing normal message still pending. A true return is local
acceptance, not remote delivery.

`cf_can_send()` checks room/state/queue availability only; it cannot validate
the buffer or length of a later call. Normal and reliable submissions have
separate outgoing slots.

## Reliable messages

### cf_send_reliable

```c
uint8_t cf_send_reliable(const void *data, uint16_t len);
```

Copies 1–480 bytes locally and returns a ticket in 1–255. Returns 0 for invalid
data/length, unavailable room, pending reliable submission, or exhaustion of
the internal operation counter. No serial I/O occurs in this call.

After a nonzero result, the SDK owns retries. Continue polling; do not submit
the same application event again just because confirmation is delayed.

### cf_delivery and cf_delivery_t

```c
cf_delivery_t cf_delivery(uint8_t ticket);
```

| Value | Meaning |
| --- | --- |
| `CF_DELIVERY_NONE = 0` | Zero/noncurrent ticket, or no tracked submission |
| `CF_DELIVERY_PENDING = 1` | Admission not yet confirmed |
| `CF_DELIVERY_DELIVERED = 2` | Relay admission confirmed; not game processing |
| `CF_DELIVERY_LOST = 3` | Admission unconfirmed/failed; remote outcome may be unknown |

Only the current ticket is tracked. Tickets wrap after 255 submissions, so
do not retain them as permanent event IDs. A later recipient failure does not
retroactively turn an admitted ticket into an application acknowledgement.

See [Reliable events](reliable.md) for ordering, removal and reconciliation.

### cf_reliable_waiting

```c
uint8_t cf_reliable_waiting(void);
```

Cached device queue count plus any complete reliable event retained locally.
Useful diagnostically; it excludes events still waiting at the relay.

## Receiving

### cf_poll

```c
uint16_t cf_poll(void *out, uint16_t max_len);
```

Call once per frame. On due ticks it exchanges data, services pending reliable
work and periodically refreshes state/metadata. Returns one complete message's
length, or zero when none is delivered. At most one message is returned per
call, and only on a due successful tick in a usable room.

Reliable messages have priority, while normal fragments are still assembled.
If a reliable message does not fit the output buffer it stays pending; another
normal message that fits may still be returned. Use `CF_MAX_MESSAGE` bytes.

NULL output or zero capacity maintains the session without consuming messages.
Normal buffers retain only the latest complete state per sender. Reliable
queues are bounded, so indefinite non-consumption can lead to removal.

### Receive metadata

```c
uint8_t cf_last_sender(void);
uint32_t cf_last_sender_generation(void);
bool cf_last_was_reliable(void);
```

Read immediately after a nonzero poll:

- Sender is the relay-assigned slot, for either channel.
- Generation distinguishes successive occupants of a slot; preserved on resume.
- The boolean identifies the reliable channel.

Do not interpret these getters as metadata for a zero-length poll. Before any
message, sender is 0xFF and generation is zero.

## Diagnostics

```c
void cf_get_stats(cf_stats_t *out);
```

Copies counters into `cf_stats_t`; NULL is a no-op. Counters reset on
`cf_connect()`. A disconnect alone does not reset the statistics.

| Field (`uint32_t`) | Counts |
| --- | --- |
| `ticks_ok` | Successful serial tick exchanges |
| `ticks_timeout` | Timed-out tick exchanges |
| `ticks_unaligned` | Failed tick alignment |
| `msgs_sent` | Normal messages whose fragments were handed to the device |
| `msgs_received` | Normal messages returned to the game |
| `msgs_dropped` | Detected normal-fragment assembly failures; not every network loss |
| `reliable_sent` | Outgoing events observed as admitted by the relay |
| `reliable_recv` | Reliable events returned to the game |
| `reliable_lost` | Pending events observed as lost/unconfirmed |

These counters do not prove that every peer processed a message. In particular,
a lost confirmation does not prove the relay never received the event.
