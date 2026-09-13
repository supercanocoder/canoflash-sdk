# Reconnection and game recovery

[Documentation](README.md) · [Reliable events](reliable.md)

## Handle recovery in the game loop

The firmware retries a broken relay connection. The SDK reports
`CF_RECONNECTING` while it waits. Keep polling, show a recovery screen and
pause simulation before applying new local inputs.

This is an integration fragment; the rendering and reconciliation functions
belong to your game:

```c
uint8_t message[CF_MAX_MESSAGE];
uint16_t length = cf_poll(message, sizeof(message));

switch (cf_status()) {
case CF_CONNECTED:
    if (!cf_in_room()) {
        show_online_menu();
        break;
    }
    if (length) {
        handle_message(cf_last_sender(), cf_last_sender_generation(),
                       cf_last_was_reliable(), message, length);
    }
    update_if_game_state_is_synchronised();
    draw_game();
    break;
case CF_RECONNECTING:
    draw_reconnecting();
    break;
default:
    show_connection_lost();
    break;
}
```

Do not call `cf_connect()` every recovery frame: it resets local session
state. Let recovery resolve; explicitly disconnect/reconnect when starting a
new session after failure.

## What can be resumed

A dropped relay connection reserves its occupied seat for **30 seconds by
default**, measured by the relay after it detects the disconnection. This is
a server setting, not a guaranteed duration controlled by the game.

The firmware retains the room code and a random seat token and authenticates
with the same account. Games do not handle the token. Successful resume
preserves room, slot and seat generation.

If the seat expired, the device reset, the relay restarted, or reliable delivery
invalidated the seat, the old session cannot be resumed. A failed resume does
not silently create a replacement room.

A connected menu can recover without a room. Recovery while a new join or
leave is unresolved can fail deliberately because membership is uncertain.

## Timing is not one fixed timeout

The firmware retries with backoff, for up to 60 seconds during recovery. The
relay's seat reservation can expire sooner. A successful Wi-Fi association
does not imply that TLS, authentication and room resume have completed.

TLS handshakes can delay serial servicing. The SDK treats consecutive failed
ticks as trouble after 30 attempts and failure after 600. At 20 successful
frame-paced attempts per second those correspond nominally to 1.5 and 30
seconds, but actual elapsed time depends on frame rate and exchange delays.

Do not promise a fixed recovery countdown based on those thresholds.
The status returned by the SDK determines whether play can continue.

## Restore application state

Normal updates can be lost while away. Admitted reliable events for a reserved
recipient are retained within the live session, subject to bounded queues,
but this is not a saved game or a complete state snapshot.

After reconnecting, have your game reconcile a round/state version with the
current master. Apply queued events in the appropriate round and request a
fresh snapshot if needed. Continue polling and consuming packets during this
process.

Admission acknowledgement is not proof that all games processed an event.
Use application event IDs and acknowledgements when that distinction matters.

## Other players going away

`cf_away_mask()` identifies reserved, temporarily absent seats:

```c
uint8_t away = cf_away_mask();
if (away & (1u << other_slot)) {
    show_player_reconnecting(other_slot);
}
```

An away occupant still counts in `cf_peers()` and `cf_peer_mask()`.
Your game can pause for them or continue with a defined resynchronisation
policy. The SDK provides no kick API.

If the master drops, another present player can become master immediately.
If all occupants drop, the room retains their seats for the grace period;
the first valid resume acquires the role if no master remains.

## Cable loss and deliberate exit

A serial cable interruption is different from a Wi-Fi interruption. The SDK
continues its tick attempts, but a device reset or a broken serial session may
require a fresh connection. Do not assume every physical interruption resumes.

A successfully processed voluntary leave releases the seat without a grace
reservation. `cf_disconnect()` also attempts a voluntary close, but cannot
guarantee remote receipt if the physical or network connection is already gone.

Keep polling while online: current firmware treats roughly 60 seconds without
GBA contact as a dead Link session. This differs from the relay's default
30-second seat grace and the device's ordinary standby timeout.
