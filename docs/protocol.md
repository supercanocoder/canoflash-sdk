# Wire protocol

You do **not** need this to write a game — use `canoflash.h`. This document is
for people porting the SDK to another language or running their own server.

[Documentation](README.md) · [Compatibility](compatibility.md)

This is the current implementation contract. The standalone SDK includes the
GBA client; the firmware and relay are separate components. The low-level
serial implementation in [canoflash.c](../src/canoflash.c) is the source of
truth for transfer sequencing, retries and timing budgets.

## The shape of the thing

```
   GBA  <--- Link cable (SPI) --->  dongle  <--- WebSocket/TLS --->  server
 slave                              master
```

The GBA is the **slave** on the serial link. It cannot transmit when it wants
to; it can only leave a word in `SIODATA32` and wait for the dongle to drive
the clock. Every design decision below follows from that one fact.

## Link layer: GBA to dongle

Normal serial mode, 32-bit words, `SIOCNT = 0x1080` (32-bit, start, external
clock). `RCNT` must be 0.

### Commands

| Word | Name | Direction |
| :--- | :--- | :--- |
| `0x00000030` | `CMD_NET_TICK` (legacy 64-byte exchange; guard in upper bits) | GBA to dongle |
| `0x00000031` | `CMD_NET_OPEN` (followed by a payload packet) | GBA to dongle |
| `0x00000032` | `CMD_NET_CLOSE` | GBA to dongle |
| `0x00000033` | `CMD_NET_STATUS` (legacy, 12 bytes) | GBA to dongle |
| `0x00000133` | `CMD_NET_STATUS_V2` (16 bytes) | GBA to dongle |
| `0x00000233` | `CMD_NET_STATUS_V3` (20 bytes) | GBA to dongle |
| `0x0000003A` | `CMD_NET_TICK_V2` (72 bytes, upper bits carry guard time) | full duplex |
| `0x00000135` | `CMD_NET_RELIABLE_SEND_V2` (u32 operation + 1..480 bytes) | GBA to dongle |
| `0x00000136` | `CMD_NET_RELIABLE_GET_V2` (16-byte header + payload) | dongle to GBA |
| `0x00000034` | `CMD_NET_SET_STATE` (state in the upper bits) | GBA to dongle |
| `0x00000037` | `CMD_NET_JOIN` (48-byte payload) | GBA to dongle |
| `0x00000038` | `CMD_NET_LEAVE_ROOM` | GBA to dongle |
| `0x00000039` | `CMD_NET_ROOM_INFO` (140-byte response) | dongle to GBA |
| `0x00000035` / `0x00000036` | Legacy reliable SEND / GET | packet path |
| `0xCAFEBABE` | `SPI_SYNC_BYTE_MASTER`, the dongle's polling word | dongle to GBA |
| `0xBEEFCAFE` | `SPI_SYNC_BYTE_SLAVE` | GBA to dongle |
| `0x1500C0DE` | `ACK_OK` | either |
| `0xDEADBEEF` | `ACK_ERR` | either |
| `0x4E455421` | `NET_HELLO_MAGIC`, "I am a network game" | GBA to dongle |

### Waking the dongle

The presence probe repeatedly offers `NET_HELLO_MAGIC` in normal serial mode,
using the serial output idle/armed line as a ready signal. It must tolerate
standby probing and complete the repeated-marker detection before OPEN.
The firmware's slow probe tolerates a lost low bit of the marker. Preserve the
implemented handshake when porting; receiving one polling word is not enough
to prove that the device entered its command loop.

### Packets

A command with a payload is:

```
[command word] [size] [FNV-1a hash of payload] [payload words...] -> ACK_OK
```

The hash is FNV-1a (offset basis `2166136261`, prime `16777619`) over exactly
`size` bytes. Padding in the final word is ignored.

Disable interrupts for the whole packet. A VBlank in the middle desynchronises
it.

### The tick

The current SDK uses a 72-byte full-duplex tick: 64 bytes of normal payload
and 8 bytes of source/queue metadata. Reliable events use the packet path.
The legacy CMD_NET_TICK remains a 64-byte exchange for existing clients.

1. **Align first.** Exchange zeros until you receive `SPI_SYNC_BYTE_MASTER`.
   The GBA only captures a word correctly if it was armed *before* the dongle
   started clocking; without this step the whole tick comes out bit-shifted.
2. Send `CMD_NET_TICK_V2 | (guard_us << 8)`. The guard time travels **inside** the
   command word. Sent as a separate word it gets read as a command — a guard of
   5 was interpreted as "scan Wi-Fi", and `0x1D` would be "power off".
3. Exchange 18 words in each direction; the last two uplink words are reserved.
4. **Write 0 to `SIODATA32` when you are done.** Between ticks the GBA is not
   armed, but the dongle keeps clocking and reads whatever is left there.

### Structures

SPI integers use little-endian order. Word transfers require aligned buffers:
use uint32_t storage and copy bytes into the logical structure where needed.
Do not rely on arbitrary byte-buffer alignment or a compiler's packed layout.

```c
typedef struct {           // CMD_NET_OPEN payload, 72 bytes
  uint32_t mode;           // 1 = relay
  uint8_t key_len;
  uint8_t padding[3];
  char api_key[64];
} ProtocolNetOpen;

typedef struct {           // CMD_NET_JOIN payload, 48 bytes
  uint32_t max_players;
  uint8_t mode;            // game API uses 0 create, 1 code, 2 match
  uint8_t visibility;      // 0 private, 1 public
  uint8_t code_len;
  uint8_t params_len;
  char code[8];
  char params[32];
} ProtocolNetJoin;

typedef struct {           // CMD_NET_STATUS response, 12 bytes
  uint8_t status;          // 0 idle, 1 connecting, 2 connected, 3 failed
  uint8_t slot;
  uint8_t peers;
  uint8_t peer_mask;
  uint8_t capacity;
  uint8_t room_state;      // 0 waiting, 1 playing
  uint8_t master_slot;     // 0xFF = unknown
  uint8_t reliable_state;
  uint8_t reliable_ticket;
  uint8_t reliable_pending;
  uint8_t away_mask;
  uint8_t in_room;
} ProtocolNetStatus;

typedef struct {           // CMD_NET_STATUS_V2 response, 16 bytes
  ProtocolNetStatus session;
  uint8_t join_error;      // relay code: 0 OK, 3 missing, 8 full, 9 playing
  uint8_t join_pending;    // queued JOIN or awaiting its result
  uint8_t leaving_room;   // awaiting LEFT_ROOM, including cancellation
  uint8_t version;        // 2
} ProtocolNetStatusV2;
```

STATUS_V3 is STATUS_V2 with `version=3`, followed by a little-endian u32
`reliable_operation`; total 20 bytes. The new SDK uses `CMD_NET_STATUS_V3`. The legacy command keeps its 12-byte
response, so existing clients do not receive a packet larger than their buffer.
A game built with the new SDK needs firmware implementing the new command.

The room state is set with `CMD_NET_SET_STATE`, and the new state rides in the
upper bits of the command word for the same reason the guard time does.

`CMD_NET_ROOM_INFO` returns 140 bytes: little-endian u32 room ID, an 8-byte
NUL-terminated code field, then eight 16-byte NUL-terminated UTF-8 name fields.

Opening happens in two stages: send `CMD_NET_OPEN`, then poll `CMD_NET_STATUS_V3`.
The TLS/authentication work happens on the device; command receipt alone does
not confirm a usable relay session.

## Session layer: dongle to server

Binary frames over a WebSocket with TLS. Client messages start at `0x01`,
server messages at `0x81`.

```
HELLO     0x01  [u16 tokenLen][token][u8 keyLen][key][u8 nameLen][name][u8 protocolVersion=2]
DATA      0x02  [payload]
LEAVE     0x03
SET_STATE 0x04  [u8 state]                       master only
RELIABLE  0x05  [u16 seq][payload]
RELIABLE_ACK 0x06 [u8 fromSlot][u16 seq]
JOIN      0x07  [u8 mode][u8 maxPlayers][u8 visibility]
                [u8 codeLen][code][u8 paramsLen][params]
LEAVE_ROOM 0x08
HEARTBEAT 0x09
RELIABLE_ACK_V2 0x0a [u8 fromSlot][u16 seq][u32 generation]

WELCOME   0x81  [u8 slot][u8 players][u8 capacity][u8 state][u8 isMaster]
                [u32 roomId][u8 codeLen][code][12-byte resume token]
PEER      0x82  [u8 slot][u8 presence][u8 nameLen][name][u32 generation]
RELAY     0x83  [u8 fromSlot][payload]
MASTER    0x84  [u8 masterSlot]
STATE     0x85  [u8 state]
SERVER_ACK 0x86 [u16 seq]
RELIABLE_IN 0x87 [u8 fromSlot][u16 seq][payload]
SESSION_OK 0x88 [u8 protocolVersion=2]
LEFT_ROOM 0x89
HEARTBEAT_ACK 0x8a
RELIABLE_IN_V2 0x8b [u8 fromSlot][u16 seq][u32 generation][payload]
ERROR     0x8f  [u8 code]
```

Integers wider than one byte in WebSocket messages use network byte order.
`presence` is 0 gone, 1 present, 2 away with a reserved seat. Reliable payloads
are 1..480 bytes; room capacity is at most 8. Normal SDK frames are 64 bytes.
The updated firmware requires SESSION_OK version 2; legacy HELLO clients remain
supported but cannot resume version-2 seats without their token.

RESUME uses JOIN mode 3, with the room code and the 12-byte seat token encoded as
24 lowercase hex characters in `params`. The relay checks the account as well.
The token is retained in firmware across transport reconnections.

HELLO opens a session; JOIN is a separate operation. The dongle waits for
SESSION_OK before sending JOIN, and for WELCOME before sending room data.
During recovery it remains CONNECTING until RESUME receives WELCOME. A pending
reliable message is retained, but cannot be retried before that confirmation.

The dongle sends HEARTBEAT every five seconds, even without a room. The relay
accepts it only after authentication and answers HEARTBEAT_ACK. No ACK within
15 seconds triggers recovery. These messages require the updated relay: deploy
the relay before updating firmware, then rebuild the game with the SDK.

Error codes: 1 bad frame, 2 auth, 3 no room, 4 too fast, 5 too big,
6 unknown game, 7 not master, 8 room full, 9 room playing,
10 reliable delivery/admission failure (seat removed, session failed).

A normal JOIN rejected with 3, 8 or 9 leaves the session usable and exposes that
result in status V3 (and V2 for legacy consumers). A rejected RESUME ends the previous match. Errors 1, 2, 4
and 6 are terminal at the relay; the dongle may refresh credentials once when
HELLO is rejected with 2. Size and master-permission errors are nonfatal.

LEAVE_ROOM is idempotent and also cancels a pending JOIN. The dongle blocks room
traffic while waiting for LEFT_ROOM and clears room payload queues on leaving.
If a connection dies before the result of a new JOIN or LEAVE is known, it fails
the session instead of repeating CREATE or guessing which room is current.
Voluntary session close sends LEAVE before closing the socket.

`MASTER` goes to the whole room, not just to the new master: everyone else
needs to know who is in charge so they stop waiting on someone who left. It
hands over the crown and nothing else — any state the old master held is gone,
because the server never saw inside the packets.

The HELLO token is the device JWT. Its account identity is distinct from the
per-seat resume token; the relay checks both account and seat-token ownership
on a version-2 resume.

The API key is separate from the token and does a different job: it identifies
the **game**, while the token authorises the **player**. The key travels inside
the ROM and is not a secret. Validating it is optional for a self-hosted
server.

HELLO uses a 16-bit token length because device JWTs can exceed 255 bytes.

Payloads are opaque to the server. It does not parse, validate or version game
data — that is the developer's business.

## Normal messages and source metadata

Normal fragments keep a four-byte header within the 64-byte payload:

| Offset | Meaning |
| :--- | :--- |
| 0..1 | u16 little-endian sequence, 1..65535; zero is empty |
| 2 | High nibble: fragment index 0..7; low nibble: count 1..8 |
| 3 | Useful fragment length 1..60; only the final fragment can be short |
| 4..63 | Payload bytes; complete messages are limited to 480 bytes |

The downstream tick appends `ProtocolNetTickSource`: byte 64 is the relay's
sender slot (0xFF for no normal data), byte 65 is the number of reliable events
waiting in the dongle, bytes 66..67 are reserved, and bytes 68..71 are the
sender's little-endian u32 seat generation. The generation comes from PEER and
changes on slot reuse; it is preserved on RESUME. The SDK API exposes both.

The ESP keeps a bounded FIFO per sender and chooses senders in round-robin
order. A new single-fragment state may replace an older queued single-fragment
state; fragmented messages are not collapsed into their last fragment. Queue
pressure can lose normal fragments; incomplete messages are discarded whole.
The SDK assembles independently per sender/generation and stores each sender's
latest complete message, including during polls with no output buffer.

The fragment header changed from the legacy SDK. Recompile all participants of
a game together, or use a separate game API key/matchmaking namespace while
migrating. Keeping legacy SPI commands does not make old and new game protocols
interchangeable.

## Reliable packet path

SEND_V2 carries `[u32 operation][payload]` in little-endian SPI order. The SDK
allocates its operation before I/O and retries that same operation until
STATUS_V3 reports it. Operations increase within OPEN..CLOSE, including across
room changes. The firmware never creates a second network event for an already
accepted operation. Tickets remain 1..255 public handles.

GET_V2 returns `[u32 len][u8 fromSlot][3 reserved bytes][u32 generation]
[u32 receipt][payload]`. Empty queues return len=0 and the 16-byte header.
Receipts increase during OPEN..CLOSE. The ESP removes the head only after the
packet ACK; the SDK deduplicates repeated receipts if that ACK was lost.

The relay admits at most 128 pending events per room. SERVER_ACK confirms
admission, not game processing. Per recipient, only its earliest unacknowledged
event is sent. Away seats retain their backlog for the entire grace interval
without spending retries. Present recipients retry at 500 ms, up to ten
attempts; failure removes the seat and sends error 10, preventing a resume with
missing history. Accepted events from a departing sender retain its generation.

The ESP acknowledges only complete reliable events it admitted to its queue of
four. Full queues apply backpressure. The SDK deduplicates transport receipts,
never truncates events, and `cf_poll(NULL,0)` does not consume them. Reliable
priority does not discard normal fragments received on the same tick.
These guarantees are confined to the live room session, with no persistence
across process/device resets. See [reliable.md](reliable.md).

## Resource and timing notes

See [Integration](integration.md#timing-and-interrupts) for frame-based pacing,
interrupt masking and current memory measurements. Historical 64-byte tick
benchmarks are not performance guarantees for this protocol.

Do not shorten serial budgets based on host-only tests. Low-level timing
changes must be checked on the actual GBA/CanoFlash hardware.
