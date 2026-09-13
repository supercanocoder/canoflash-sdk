# Reliable events

[Documentation](README.md) · [API reference](api.md#reliable-messages)

Use `cf_send()` for replaceable state, such as positions, and
`cf_send_reliable()` for events such as a turn ending or a phase changing.
Both copy the payload locally; `cf_poll()` performs the transport work.

```c
/* Call once for this event. A nonzero ticket belongs to the SDK's retry loop. */
uint8_t event[] = { 1, 2 }; /* application type: phase change; phase: 2 */
uint8_t ticket = cf_send_reliable(event, sizeof(event));
```

## Admission and delivery

A nonzero ticket means the SDK has accepted a copy for transmission. Keep
calling `cf_poll()`; it sends the event with a stable operation identifier.
Losing an SPI packet ACK or the following status response retries that same
operation, so it cannot create a second event.

| Result | Meaning |
| :--- | :--- |
| `CF_DELIVERY_PENDING` | Awaiting confirmed relay admission, including local SPI submission |
| `CF_DELIVERY_DELIVERED` | The relay accepted the event and owns its delivery backlog |
| `CF_DELIVERY_LOST` | Admission was not confirmed, or the pending session/room ended; the remote outcome may be unknown |
| `CF_DELIVERY_NONE` | Not the current ticket, or no event has been queued |

There is one outgoing event in flight. Until it resolves, another call returns
0. Invalid lengths, null data and lack of an active room also return 0. Only
retry an application submission that returned 0; a nonzero ticket is already
owned by the SDK. Retrying a `LOST` event as a new application event can execute
it twice if the relay received the original but its ACK was lost. Games that
need recovery across failed sessions must attach durable application event IDs
and reconcile state.

Tickets are 1..255 and only the latest is tracked. They are short handles, not
permanent event IDs. The internal SPI operation is a separate 32-bit counter.

## Ordering, retransmission and reconnection

The relay targets the other occupants present or reserved at admission time.
Players joining afterwards do not receive earlier events. Each recipient has
one head event in flight, in relay admission order; later events wait for its
ACK. Recipients share the relative order of events addressed to them, but there
is no global barrier between recipients.

The ESP32 ACKs only after admitting the complete message to its four-event
queue. If that queue is full, the relay retains the event and retries it. The
receiver deduplicates using the sender's seat generation and sequence; SPI
receipts also prevent repeating an event if the GBA's packet ACK was lost.
The SDK exposes `cf_last_sender()` and `cf_last_sender_generation()` after a
successful poll. Generation distinguishes successive occupants of one slot.
An admitted event can still arrive after its sender leaves, so games whose
logic depends on seat ownership should retain that generation with the event.

Temporarily absent recipients retain their events throughout the room's grace
period (30 seconds by default). Absence does not spend retransmission attempts.
RESUME requires the authenticated account, room code and a random seat token,
so two dongles of one account cannot exchange seats. The firmware stores that
token; games do not need to handle it.

A present recipient has up to ten transmission attempts, 500 ms apart. If it
cannot accept its head event, the relay sends error 10, removes its seat and
notifies the others. The affected SDK reaches `CF_FAILED`; that seat cannot
resume with a gap. Expired or voluntarily departed recipients stop being
required targets. The relay permits at most 128 pending events per room; if a
new event cannot be admitted it is not ACKed and the submitting session fails.

These are in-memory guarantees within the current room session. They do not
survive a relay restart, device reset or permanently lost session, and do not
mean the game has already processed an event. If a phase requires everyone to
finish applying it, collect application acknowledgements before advancing.

## Receiving without losing events in menus

```c
uint8_t incoming[CF_MAX_MESSAGE];
uint16_t len = cf_poll(incoming, sizeof(incoming));
if (len) {
    uint8_t sender = cf_last_sender();
    if (cf_last_was_reliable()) {
        apply_event(sender, incoming, len);
    } else {
        update_state(sender, incoming, len);
    }
}
```

Reliable messages have priority. Normal fragments received in the same tick
are still assembled and retained separately per sender.

`cf_poll(NULL, 0)` keeps the connection active without consuming reliable
events. Messages are never truncated: an output buffer that is too small keeps
the complete event pending until a large enough buffer is supplied. Menus
cannot defer consumption indefinitely under sustained event traffic: once the
bounded receive queue fills, the relay's retry/removal policy applies.

## Application acknowledgements

If every player must apply a change before the next phase, define a barrier
in your game protocol:

1. The master sends a reliable event containing a round ID and event ID.
2. Each required participant validates and applies it, then queues a reliable
   application ACK carrying the same IDs.
3. The master collects ACKs by sender slot **and generation**.
4. Advance only when the required set has acknowledged, or resolve a timeout
   with an explicit game policy.

Queue ACKs in your game when `cf_send_reliable()` returns 0; a previous outgoing
event may still be pending. Keep polling and consuming events while waiting.
Do not count a transport ticket as an application ACK, and do not silently add
new room occupants to an already-running barrier.

Game payloads are broadcast. If an ACK is logically for the master, encode that
meaning in its type; other recipients can validate and ignore it. There is no
public unicast-send API.

## Compatibility

Use the updated relay first, firmware second, then ROMs rebuilt with this SDK.
The SDK requires STATUS_V3, TICK_V2 and reliable SEND/GET_V2. Legacy SPI commands
retain their layouts, but old SDK binaries do not gain these guarantees.
See [protocol.md](protocol.md) for packet layouts.
