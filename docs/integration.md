# Integrating the SDK

[Documentation](README.md) · [API reference](api.md)

## Build and ownership

Add `include/` to compiler include paths and compile `src/canoflash.c` once
as C99. Link the object into your GBA program. The core uses standard C
headers/functions and GBA registers.

C++ callers include `canoflash.h` normally; it contains `extern "C"`.
Other languages can call the C ABI, but no Rust wrapper or other language
binding is shipped or validated here.

The SDK owns the serial port while online. Do not use another Link library or
serial interrupt handler on the same port simultaneously. It uses global
state: one session per GBA, no reentrant calls, no SDK calls from interrupts.

## One poll per frame

`cf_poll()` performs synchronous Link I/O when a tick is due. Call it once
per display frame from one place, including menus and recovery screens.

Do not repeatedly poll until zero: each call advances the frame counter.
Multiple calls per frame change the network cadence.

This C fragment assumes your game provides the drawing and simulation functions:

```c
uint8_t packet[CF_MAX_MESSAGE];
uint16_t length = cf_poll(packet, sizeof(packet));

if (cf_status() == CF_RECONNECTING) {
    draw_reconnecting();
} else if (cf_status() != CF_CONNECTED || !cf_in_room()) {
    return_to_online_menu();
} else {
    if (length) {
        receive_game_packet(cf_last_sender(), cf_last_sender_generation(),
                            cf_last_was_reliable(), packet, length);
    }
    update_game();
    draw_game();
}
```

Check recovery before advancing simulation. Reconnecting the transport does
not rewind or synchronise your game state.

`cf_poll(NULL, 0)` suits menus with no game messages to process. It does not
consume reliable events. During sustained event traffic, receive into a
full-size buffer and process/queue events in your game rather than deferring
consumption indefinitely.

## Blocking calls and wait callbacks

`cf_connect()`, `cf_create_room()`, `cf_join_code()` and `cf_matchmake()`
wait for results. Provide a callback that advances the requested number of
frames and renders a waiting screen. In Butano:

```cpp
void wait_frames(int frames) {
    for (int i = 0; i < frames; ++i) {
        bn::core::update();
    }
}
```

Do not poll, connect or join from this callback: an SDK operation is in progress.

A NULL callback is accepted, but supplies no frame pacing or rendering updates.
Use a callback in games. Serial-exchange time is additional to its frame waits.

`cf_leave_room()` waits for confirmation without a callback and may return
while leaving is pending. `cf_set_room_state()`, `cf_disconnect()` and some
polls also perform serial exchanges. This is not a fully asynchronous or
constant-frame-time API.

## Timing and interrupts

Start with the default 20 Hz. The SDK computes:

```text
requested_hz = tick_hz == 0 ? 20 : min(tick_hz, 60)
frames_per_tick = max(1, floor(60 / requested_hz))
effective_hz = actual_frames_per_second / frames_per_tick
```

At 60 fps, 20 Hz means a tick every three frames; 30 Hz, every two.
A request of 25 Hz rounds to 30 Hz; above 30 rounds to one tick per frame.
At 30 fps, the default gives 10 ticks per second.

Serial exchanges mask interrupts, which can delay audio mixing, VBlank work
and other interrupt-driven systems. There is no guaranteed sub-millisecond
bound on a complete poll, especially during retries or status/reliable
transfers. The lobby deliberately disables audio.

Keep `guard_us` at its default of 5 unless measuring a timing problem. It
controls inter-word guard time, not an audio fix. SPI timing changes need
hardware validation.

Legacy 64-byte transport measurements are not benchmarks for the current
72-byte transport. Profile your ROM with its actual player count and workload.

## Design your payloads

Both channels accept 1–480 bytes. Normal messages carry 60 bytes per fragment:
60 bytes fits one tick, 61 needs two, 480 needs eight. Eight successful ticks
at the default cadence take about 400 ms of sender transmission time, before
network and receiver scheduling.

Prefer explicit byte layouts with a type and version. The relay treats game
payloads as opaque; validate lengths, values and sender roles in your game.

This fragment encodes unsigned 16-bit coordinates:

```c
uint8_t position[5] = {
    1,  /* application message type: position */
    (uint8_t)x, (uint8_t)(x >> 8),
    (uint8_t)y, (uint8_t)(y >> 8)
};
bool queued = cf_send(position, sizeof(position));
/* If the previous update is pending, send the latest state on a later frame. */
```

Validate before reading:

```c
if (length == 5 && !cf_last_was_reliable() && packet[0] == 1) {
    uint8_t sender = cf_last_sender();
    uint16_t x = (uint16_t)packet[1] | ((uint16_t)packet[2] << 8);
    uint16_t y = (uint16_t)packet[3] | ((uint16_t)packet[4] << 8);
    if (sender < 8 && x < world_width && y < world_height) {
        apply_position(sender, cf_last_sender_generation(), x, y);
    }
}
```

Sending a struct is convenient in the examples because participants use the
same GBA build. Public protocols should specify byte order and field sizes.
Avoid pointers, compiler padding and casting an unaligned buffer to a struct;
validate length and decode fields or use `memcpy`.

Use SDK sender metadata instead of trusting a slot inside the payload. Slots
can be reused: retain the generation with player/event identity. Reliable
events admitted before their sender left may still arrive afterwards.

## Choosing state or reliable events

Normal messages retain the latest complete state per sender. Send snapshots
repeatedly; there is no receipt confirming another game used an update.

Reliable events are ordered and deduplicated within the current room session,
with bounded queues. `CF_DELIVERY_DELIVERED` confirms relay admission, not
processing by every game. See [Reliable events](reliable.md).

The two channels have no shared ordering. Include a round number, state version
or event ID when their relationship matters.

## Memory and resource use

There is no dynamic allocation. Static storage includes per-sender assembly
and completed-state buffers, outgoing state and reliable-message buffers.

A devkitARM 15.2.0 build of the current SDK object used **9,624 bytes of BSS
and 5 bytes of initialised data**, about 9.4 KiB of static RAM. This excludes
stack usage, the C runtime, your buffers and the engine. The checked Butano
lobby placed the main SDK buffers in IWRAM; placement depends on the linker.

Inspect your ELF/map rather than assuming the old approximately 1 KiB figure:

```sh
"$DEVKITARM/bin/arm-none-eabi-size" examples/hello_world/hello_world.elf
"$DEVKITARM/bin/arm-none-eabi-nm" -S --size-sort examples/hello_world/hello_world.elf
```

A full-size receive buffer needs another 480 bytes. Account for large temporary
buffers when sizing your stack.
