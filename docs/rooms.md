# Rooms and matchmaking

[Documentation](README.md) · [API reference](api.md#room-operations)

## Session and room

`cf_connect()` authenticates a session. A session can stay connected while
the player reads menus. Joining a room is a separate operation.

```text
connect → connected menu → create / join code / matchmake → lobby → match
                 ↑                                         |
                 └────────── confirmed leave ──────────────┘
disconnect → offline
```

Continue polling in every connected screen. Room operations still involve
network round trips; they are not instantaneous. Their wait callbacks keep
your display advancing.

## Three ways to join

| Function | Behaviour |
| --- | --- |
| `cf_create_room()` | Always creates a new room |
| `cf_join_code()` | Joins an existing room; fails if missing/full/playing |
| `cf_matchmake()` | Joins a suitable public waiting room, or creates one |

Example fragment for a private room:

```c
cf_room_t options;
cf_room_init(&options);
options.max_players = 4;
options.visibility = CF_PRIVATE;
options.params = "wire=1;mode=coop";

const char *code = cf_create_room(&options, wait_frames);
if (!code) {
    show_join_error(cf_join_error());
}
/* Poll in the lobby and display cf_room_code() once metadata is available. */
```

The other console joins with `cf_join_code(code, wait_frames)`.
A code is six ASCII digits and only works within the same game API key.
Treat the code as a shareable invitation, not a password or access-control list.

## Matchmaking criteria

A matching room must have:

- The same game API key.
- Public visibility and waiting state.
- The same effective capacity, after relay clamping.
- An exactly equal parameter string.
- A free seat; temporarily absent players still occupy their seats.

`params` is opaque. Use shared values such as `"wire=1;mode=race"`;
do not include a nickname, timestamp or random value unique to one player.
Keep it within **31 bytes**: longer values are silently truncated by the SDK.

Matchmaking always creates public rooms. `visibility` selects privacy only
when explicitly creating a room.

Code joins do not compare parameters or requested capacity. If incompatible
game versions share one key, verify their application protocol in the lobby,
even when matchmaking parameters separate them.

## Lobby metadata

Use `cf_capacity()` rather than the requested number to draw seats. The
transport supports at most eight; the lobby example exposes two to four.

`cf_peer_mask()` identifies occupied slots. `cf_peers()` counts occupants,
including you and away players. `cf_away_mask()` distinguishes temporary
absence from a vacant seat.

`cf_player_name(slot)` supplies a UTF-8 name up to 15 bytes. It can be empty
until metadata arrives. Names and codes refresh after joining and at the
periodic status cadence: every 20 successful ticks. Supply a fallback, support
your font's character set and do not use names as identities.

## Starting a match

Only the master requests the playing state. This fragment belongs inside your
once-per-frame lobby loop, after polling:

```c
if (cf_status() == CF_CONNECTED && cf_in_room()) {
    if (cf_is_master() && cf_peers() >= cf_capacity() && cf_away_mask() == 0) {
        cf_set_room_state(CF_ROOM_PLAYING);
    }
    if (cf_room_state() == CF_ROOM_PLAYING) {
        enter_match();
    }
}
```

The SDK does not implement ready flags or application-version negotiation.
If needed, exchange reliable lobby messages and let the master start only
after checking them.

A true result from `cf_set_room_state()` is not confirmation. Wait for the
cached room state to change through polling. The relay blocks both matchmaking
and new code joins while playing. A reserved participant can still resume.

To reuse a room for another round, the master can request
`CF_ROOM_WAITING`; newcomers may join once that state is confirmed.

## Master migration

The first entrant is master. If the master leaves or becomes absent, the relay
chooses the lowest present occupied slot. If nobody is present, there may be no
master; the first participant to resume becomes master. A returning former
master does not displace the current one.

The relay transfers the role, not your game's authoritative state. Replicate
enough state for takeover or explicitly end the round if recovery is impossible.
No SDK function kicks another player; implement your game's own agreed policy
or end/leave the room.

## Leaving and failures

`cf_leave_room()` keeps the session open and asks to release the seat. It may
return before confirmation. Poll while the operation settles; a new join can
return `CF_JOIN_BUSY` in that interval.

Join failure details come from `cf_join_error()`; see the
[complete error table](api.md#cf_join_error-and-cf_join_error_t). A missing code
or full room does not itself require disconnecting.

When the outcome of a join/leave becomes uncertain because the transport dies,
the firmware can fail the session instead of guessing membership. Return to
your connection UI when `cf_status()` reports failure.
