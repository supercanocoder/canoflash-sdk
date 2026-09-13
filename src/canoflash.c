// ===========================================================================
// CanoFlash Net — SDK implementation. See canoflash.h for the API.
//
// This file contains everything: register access, the dongle protocol, and
// fragmentation. It deliberately includes nothing from the CanoFlash project
// so that it can be dropped into any GBA project without dragging anything in.
//
// Licensed under the MIT License. See LICENSE.
// ===========================================================================

#include "canoflash.h"

#include <string.h>

// ---------------------------------------------------------------------------
// GBA serial port registers
// ---------------------------------------------------------------------------
#define REG_RCNT (*(volatile uint16_t *)0x04000134)
#define REG_SIOCNT (*(volatile uint16_t *)0x04000128)
#define REG_SIODATA32 (*(volatile uint32_t *)0x04000120)
#define REG_IME (*(volatile uint16_t *)0x04000208)

#define SIO_32BIT 0x1000
#define SIO_START 0x0080

// SO's level while NOT transferring. Setting it makes "not armed" read high, so
// that arming -- which drops the line -- becomes a signal the dongle can see.
// See cf_dongle_present().
#define SIO_SO_HIGH 0x0008

// ---------------------------------------------------------------------------
// Dongle protocol.
//
// CAREFUL: these values are DUPLICATED from the firmware's protocol.h. That is
// the price of the SDK not depending on the project repository. Change one and
// you must change the other: a mismatch here produces no compile error, just a
// session that never opens and a wasted day.
// ---------------------------------------------------------------------------
#define CMD_NET_TICK 0x0000003Au
#define CMD_NET_OPEN 0x00000031u
#define CMD_NET_CLOSE 0x00000032u
#define CMD_NET_STATUS 0x00000033u
#define CMD_NET_STATUS_V3 0x00000233u
#define CMD_NET_SET_STATE 0x00000034u
#define CMD_NET_RELIABLE_SEND 0x00000135u
#define CMD_NET_RELIABLE_GET 0x00000136u
#define CMD_NET_JOIN 0x00000037u
#define CMD_NET_LEAVE_ROOM 0x00000038u
#define CMD_NET_ROOM_INFO 0x00000039u

#define SPI_SYNC_BYTE_MASTER 0xCAFEBABEu
#define NET_HELLO_MAGIC 0x4E455421u // "NET!"
#define SPI_SYNC_BYTE_SLAVE 0xBEEFCAFEu
#define ACK_OK 0x1500C0DEu
#define ACK_ERR 0xDEADBEEFu

#define NET_TICK_BYTES 64u
#define NET_TICK_V2_BYTES 72u
#define NET_TICK_WORDS (NET_TICK_V2_BYTES / 4u)

#define NET_STATUS_IDLE 0u
#define NET_STATUS_CONNECTING 1u
#define NET_STATUS_CONNECTED 2u
#define NET_STATUS_FAILED 3u

#define NET_OPEN_MODE_RELAY 1u
#define NET_API_KEY_MAX 64u
#define NET_ROOM_PARAMS_MAX 32u
#define NET_ROOM_CODE_MAX 8u
#define NET_NAME_MAX 16u
#define NET_MAX_SLOTS 8u

// Mirrors ProtocolNetOpen. mode is 32 bits where 8 would do, ON PURPOSE:
// it puts a uint32_t first, which gives the struct alignment 4. A struct that
// starts with loose bytes has alignment 1, and packets are written 32 bits at a
// time — the ARM7 would then write to the neighbouring aligned address, with no
// error and no warning.
// Mirrors ProtocolNetOpen. Opens the SESSION, not a room.
typedef struct {
  uint32_t mode;
  uint8_t key_len;
  uint8_t padding[3];
  char api_key[NET_API_KEY_MAX];
} cf_open_req_t;

// Mirrors ProtocolNetJoin.
typedef struct {
  uint32_t max_players;
  uint8_t mode;
  uint8_t visibility;
  uint8_t code_len;
  uint8_t params_len;
  char code[NET_ROOM_CODE_MAX];
  char params[NET_ROOM_PARAMS_MAX];
} cf_join_req_t;

// Mirrors ProtocolNetRoomInfo.
typedef struct {
  uint32_t room_id;
  char code[NET_ROOM_CODE_MAX];
  char names[NET_MAX_SLOTS][NET_NAME_MAX];
} cf_room_info_t;

// Mirrors ProtocolNetStatusV3.
typedef struct {
  uint8_t status;
  uint8_t slot;
  uint8_t peers;
  uint8_t peer_mask;
  uint8_t capacity;
  uint8_t room_state;
  uint8_t master_slot;
  uint8_t reliable_state;
  uint8_t reliable_ticket;
  uint8_t reliable_pending;
  uint8_t away_mask;
  uint8_t in_room;
  uint8_t join_error;
  uint8_t join_pending;
  uint8_t leaving_room;
  uint8_t version;
  uint32_t reliable_operation;
} cf_status_resp_t;

// Mirrors ProtocolNetReliableV2. The uint32_t comes first for alignment, same
// reason as everywhere else in this file.
typedef struct {
  uint32_t len;
  uint8_t from_slot;
  uint8_t padding[3];
  uint32_t generation;
  uint32_t receipt;
  uint8_t data[CF_MAX_MESSAGE];
} cf_reliable_msg_t;

// ---------------------------------------------------------------------------
// Fragmentation
// ---------------------------------------------------------------------------
// A 4-byte header inside every tick. Four rather than three so that the data
// stays 32-bit aligned and the fast copy path applies.
//
//   [0..1] seq     little-endian message number, 1..65535; 0 means empty.
//   [2] high nibble: fragment index; low nibble: fragment count (1..8)
//   [3] len         useful bytes in this fragment
#define CF_FRAG_HEADER 4u
#define CF_FRAG_BYTES (NET_TICK_BYTES - CF_FRAG_HEADER) // 60
#define CF_MAX_FRAGS ((CF_MAX_MESSAGE + CF_FRAG_BYTES - 1u) / CF_FRAG_BYTES)

// ---------------------------------------------------------------------------
// Wait budgets, expressed in loop iterations rather than microseconds: reading
// a timer every turn would slow down the very loop we want tight. The rough
// conversion, measured against the firmware, is ~0.3-0.5 us per iteration.
//
// The first word waits longer because the dongle polls the bus every ~100 us
// and needs time to come round.
// ---------------------------------------------------------------------------
#define CF_FIRST_WORD_BUDGET 60000 // ~20-30 ms
#define CF_WORD_BUDGET 8000        // ~2.5-4 ms

// Budget for the alignment attempts, more generous than a single word. The
// reason: the dongle only tightens its loop while it sees recent ticks (half a
// second). At low rates — a turn-based game at 1-2 Hz — it drops back to its
// normal loop between ticks, where it polls the bus every ~10 ms. With the
// short budget we would never catch its probe word and every tick would fail.
#define CF_ALIGN_BUDGET 24000 // ~8-12 ms

// Consecutive failed ticks before assuming the link is in trouble.
//
// It does NOT mean the session is dead. A dongle busy reconnecting blocks for
// several seconds on the TLS handshake and cannot service the link meanwhile,
// which looks exactly like this. Measured on hardware: the game used to give
// up after 1.5 s while the dongle was three seconds from being back.
#define CF_FAILS_BEFORE_TROUBLE 30

// And this many before giving up for good — about thirty seconds at 20 Hz.
//
// It has to outlast the dongle's whole recovery: it retries the connection for
// up to a minute, and the server holds the seat for thirty seconds. Declaring
// death sooner would abandon a session that was about to come back.
#define CF_FAILS_BEFORE_DEAD 600

// Wait for a single exchange of the packet protocol. About twenty milliseconds,
// and the number that matters is not how long that is but how long INTERRUPTS
// ARE OFF, because a packet is exchanged with them disabled: it has to stay
// atomic or a vertical blank lands between two words and desynchronises it.
//
// It used to be 150 ms, and the sound gave it away. The audio hardware is fed
// by an interrupt; hold the console longer than a frame and the mixing buffer
// is replayed as it stands, so a stalled exchange came out as the last button
// press stuttering nine times like a machine gun. Reported from hardware,
// 2 September 2026.
//
// Patience did not have to go with it: what was one long wait is now several
// short ones, with interrupts back on in between (see cf_send_packet). Same
// total retry count. These are per-exchange budgets, not a hard bound on the
// duration of a complete packet or its effect on audio.
//
// The CanoFlash menu uses 15 s here because it assumes the dongle exists; an
// SDK cannot assume that, and with such a value a GBA with no dongle sits
// frozen on a black screen for minutes.
#define CF_CMD_BUDGET 40000

// How many times a packet is offered before giving up. Deliberately generous:
// each attempt is short now, and the console breathes between them.
#define CF_PACKET_ATTEMPTS 12

// THE PRESENCE PROBE, and why it costs the console nothing any more.
//
// The GBA is a slave: it cannot call the dongle, only park a word and wait. For
// a long time this SDK dealt with that by staying armed continuously with
// interrupts off -- fifteen seconds of a frozen console, silent audio and a
// game engine that never got a frame to run its own housekeeping.
//
// None of that is necessary, because parking a word is not something you have
// to keep doing. The hardware holds it indefinitely, and the moment it is
// parked THE CONSOLE'S SO LINE GOES LOW. The dongle can see that without
// sending a single clock, so it stops guessing whether anyone is listening and
// simply waits.
//
// Measured on hardware, 2 September 2026, with the console holding each state
// for a known number of seconds: idle with SIO_SO_HIGH reads high, armed reads
// low -- whatever the word contains. It is not the top data bit leaking out; it
// is a ready line, and it costs nothing to use.
//
// So the probe arms once and gets out of the way. Each frame it asks whether
// the transfer has happened, which is one register read. The game runs at full
// speed, with interrupts on, for as long as the search takes.

// How long to keep offering the marker, in frames. The dongle asleep looks at
// the bus every couple of seconds, so this has to span a few of those cycles.
#define CF_PROBE_FRAMES 360

// Roughly one frame of the delay loop, for a game that passes no callback. The
// documentation tells them not to do that.
#define CF_FRAME_ITERATIONS (CF_CMD_BUDGET / 9)

// How long the line is held high before each arming, in loop iterations. It
// only has to be long enough for the dongle to see it: the dongle samples every
// 20 us and this is a few hundred.
#define CF_RELEASE_ITERATIONS (CF_CMD_BUDGET / 500)

// How long cf_connect() waits for the dongle to open the session, in polls of
// six frames (about 100 ms each). Thirty-five seconds, and the number is not
// ours: the dongle retries a first connection for up to thirty (its
// FIRST_CONNECT_GIVE_UP_MS), so this has to outlast that. Give up sooner and a
// dongle that was about to succeed opens a session nobody wants; then the
// player's next press finds one already open.
//
// Most connections take about four seconds. The rest of this budget only gets
// spent when the network is dropping packets, and it is what turns "Could not
// connect, press again" into a longer wait that ends well.
#define CF_CONNECT_ATTEMPTS 350

// How many more words the marker keeps being presented AFTER the dongle has
// been seen. The asymmetry is not accidental: the GBA is convinced by a single
// word, but the dongle demands several before trusting a coincidence. Stop at
// the first hit and the dongle is left halfway through its count, detecting
// nothing -- and the symptom would be the worst kind: the GBA believing there
// is a dongle while the dongle has no idea there is a game.
#define CF_HELLO_REPEATS 12

// Consecutive exchanges with no clock at all before giving up on the
// handshake. Bounds the case of the dongle powering off mid-operation.
#define CF_MAX_SILENT_EXCHANGES 100

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static cf_state_t s_state = CF_DISCONNECTED;
static uint8_t s_slot = 0;
static uint8_t s_peers = 0;
static uint8_t s_peer_mask = 0;
static uint8_t s_capacity = 0;
static uint8_t s_master_slot = 0xFF;
static uint8_t s_away_mask = 0;
static bool s_in_room = false;
static bool s_join_pending = false;
static bool s_leaving_room = false;
static uint8_t s_remote_join_error = 0;
static cf_join_error_t s_join_error = CF_JOIN_OK;
static char s_room_code[NET_ROOM_CODE_MAX] = "";
static char s_names[NET_MAX_SLOTS][NET_NAME_MAX];
static cf_room_state_t s_room_state = CF_ROOM_WAITING;

// How many ticks between status refreshes. The room's membership, its master
// and its state only change through this poll, so without it a game would
// never notice anyone arriving.
//
// It is not free: a status request runs the slow packet path with interrupts
// off. Once a second is plenty for a lobby and cheap enough not to matter.
#define CF_STATUS_EVERY_TICKS 20
static uint8_t s_ticks_since_status = 0;

// --- Reliable messages ---
static uint8_t s_rel_ticket = 0;
static cf_delivery_t s_rel_state = CF_DELIVERY_NONE;
static uint8_t s_rel_waiting = 0;
static bool s_last_reliable = false;

static uint16_t s_guard_us = 5;
static uint8_t s_frames_per_tick = 3; // 20 Hz over 60 fps
static uint8_t s_frame_counter = 0;

static uint8_t s_tx_buf[CF_MAX_MESSAGE];
static uint16_t s_tx_len = 0;
static uint16_t s_tx_seq = 0;
static uint8_t s_tx_frag = 0;
static uint8_t s_tx_count = 0; // 0 = nothing pending

// Independent assembly and latest complete state per sender. A reliable event
// or a menu poll cannot destroy a normal fragment received in the same tick.
typedef struct {
  uint8_t data[CF_MAX_MESSAGE];
  uint8_t ready[CF_MAX_MESSAGE];
  uint16_t len, ready_len;
  uint32_t generation;
  uint16_t seq, last_seq;
  uint8_t next_frag, count, last_frag;
} cf_rx_t;
static cf_rx_t s_rx[NET_MAX_SLOTS];
static uint8_t s_next_rx_slot;
static uint8_t s_last_sender = 0xFF;
static uint32_t s_last_sender_generation;
static uint32_t s_rel_operation;
static bool s_rel_submit_pending;
static struct { uint32_t operation; uint8_t data[CF_MAX_MESSAGE]; } s_rel_submit;
static uint16_t s_rel_submit_len;
static cf_reliable_msg_t s_rel_in;
static bool s_rel_in_valid;
static uint32_t s_rel_last_receipt;

// 16 bits, not 8: the "really dead" threshold is 600 ticks and that does not
// fit in a byte. The compiler catches it with -Wtype-limits, which is exactly
// why this builds with warnings turned on.
static uint16_t s_consecutive_fails = 0;

static cf_stats_t s_stats;
static void cf_clear_room_payloads(void);

// ---------------------------------------------------------------------------
// Low level
// ---------------------------------------------------------------------------

static void cf_delay_loop(int iterations) {
  for (int i = 0; i < iterations; i++) {
    __asm__ volatile("");
  }
}

// A bounded exchange. Returns false if it timed out, cancelling the transfer
// so the register is not left armed.
static bool cf_exchange_bounded(uint32_t out, uint32_t *in, int budget) {
  REG_SIODATA32 = out;
  REG_SIOCNT = SIO_32BIT | SIO_START;

  while (REG_SIOCNT & SIO_START) {
    if (--budget <= 0) {
      REG_SIOCNT = 0;
      return false;
    }
  }
  *in = REG_SIODATA32;
  return true;
}

// True if the last cf_exchange() gave up without ever seeing a clock. Lets us
// tell "the dongle answered something odd" from "there is no dongle".
static bool s_last_exchange_silent = false;

// One exchange of the packet protocol. Returns ACK_ERR if there was no clock.
static uint32_t cf_exchange(uint32_t data_out) {
  uint32_t in = 0;
  REG_RCNT = 0;
  if (!cf_exchange_bounded(data_out, &in, CF_CMD_BUDGET)) {
    s_last_exchange_silent = true;
    return ACK_ERR;
  }
  s_last_exchange_silent = false;
  return in;
}

// Is anyone on the other end of the cable? Checked BEFORE attempting anything:
// without it, a GBA with no dongle — or with a sleeping one — walks the whole
// protocol on expired waits and takes minutes to give up, screen frozen, with
// no chance for the game to say anything.
//
// A COMPLETED EXCHANGE IS NOT ENOUGH. With the dongle powered off the cable
// lines float, and electrical noise produces enough edges to complete a 32-bit
// transfer full of garbage. The register drains, the wait does not expire, and
// the GBA concludes there is a dongle where there is none. You have to look at
// WHAT arrives: while listening, the dongle sends its probe word every round.
// Park a word and let the console get on with its life. Arming drops SO, which
// is how the dongle knows we are here.
static void cf_arm(uint32_t word) {
  REG_SIODATA32 = word;
  REG_SIOCNT = SIO_SO_HIGH | SIO_32BIT | SIO_START;
}

// Still waiting for someone to clock us?
static bool cf_armed(void) { return (REG_SIOCNT & SIO_START) != 0; }

static bool cf_dongle_present(void (*wait_frames)(int n)) {
  REG_RCNT = 0;

  // High while not armed, so that arming means something. Without this the line
  // sits low all the time and "ready" is indistinguishable from "a console that
  // happens to be plugged in".
  REG_SIOCNT = SIO_SO_HIGH;

  // We offer NET_HELLO_MAGIC, not a zero. This is the missing half: the dongle
  // asleep already looks at the bus every couple of seconds for a GBA waiting
  // in multiboot; recognising this word is what lets a cartridge game wake it
  // without anyone pressing the button.
  cf_arm(NET_HELLO_MAGIC);

  bool found = false;
  int offered = 0;

  for (int frame = 0; frame < CF_PROBE_FRAMES; frame++) {
    // The game's frame, run in full. No interrupts are disabled anywhere in
    // here and none need to be: the transfer is done by the hardware, and the
    // only thing that could disturb it would be writing to those registers
    // mid-flight.
    if (wait_frames) {
      wait_frames(1);
    } else {
      cf_delay_loop(CF_FRAME_ITERATIONS);
    }

    if (!cf_armed()) {
      uint32_t in = REG_SIODATA32;

      // The last bit of a word is not reliable at the probe's clock speed: the
      // console releases its line before the dongle samples it. Thirty-one bits
      // of marker still make a chance match impossible.
      if ((in | 1u) == (SPI_SYNC_BYTE_MASTER | 1u)) found = true;

      // Seeing the dongle does not mean the dongle has seen us.
      if (found && ++offered >= CF_HELLO_REPEATS) break;
    }

    // A FRESH EDGE, EVERY FRAME. What the dongle waits for is the line going
    // high and then low, not the line being low: an ordinary GBA sitting there
    // doing nothing holds it low all the time, and one that never moves is
    // indistinguishable from nothing at all.
    //
    // So the line is released before every arming. Staying armed forever would
    // be cheaper and would never be seen.
    REG_SIOCNT = SIO_SO_HIGH;
    cf_delay_loop(CF_RELEASE_ITERATIONS);
    cf_arm(NET_HELLO_MAGIC);
  }

  REG_SIOCNT = 0;
  REG_SIODATA32 = 0;
  return found;
}

static uint32_t cf_fnv1a(const void *data, uint32_t size) {
  uint32_t hash = 2166136261u;
  const uint8_t *p = (const uint8_t *)data;
  for (uint32_t i = 0; i < size; i++) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

// Sends a command with a payload and waits for the dongle's ACK. A direct port
// of the path the menu uses: same format, same retries.
static bool cf_send_packet(uint32_t cmd, const void *payload, uint32_t size) {
  uint32_t crc = cf_fnv1a(payload, size);

  // Several short attempts rather than a few long ones. Between them interrupts
  // are on, which is the whole point: that is where the console catches up on
  // its sound and its screen.
  for (int attempt = 0; attempt < CF_PACKET_ATTEMPTS; attempt++) {
    bool failed = false;
    uint32_t response = 0;

    uint16_t saved_ime = REG_IME;
    REG_IME = 0; // without this a VBlank mid-packet desynchronises it

    cf_exchange(cmd);

    uint32_t resp = cf_exchange(size);
    if (resp == ACK_ERR || resp == SPI_SYNC_BYTE_MASTER) failed = true;

    if (!failed) {
      cf_exchange(crc);

      const uint8_t *src = (const uint8_t *)payload;
      const bool aligned = (((uintptr_t)src) & 3u) == 0u;
      uint32_t words = (size + 3u) / 4u;

      for (uint32_t i = 0; i < words; i++) {
        uint32_t offset = i * 4u;
        uint32_t remaining = size - offset;
        uint32_t word;

        // Reading 32 bits from an unaligned address returns rotated data on
        // the ARM7, and the last word would read up to 3 bytes past the
        // caller's buffer. The padding goes to zero: the receiver hashes over
        // `size` and discards the rest.
        if (aligned && remaining >= 4u) {
          word = *(const uint32_t *)(src + offset);
        } else {
          word = 0;
          memcpy(&word, src + offset, (remaining < 4u) ? remaining : 4u);
        }

        resp = cf_exchange(word);
        if (resp == ACK_ERR || resp == SPI_SYNC_BYTE_MASTER) {
          failed = true;
          break;
        }
      }
    }

    // The ACK must be read with interrupts still disabled: the dongle sends it
    // blindly, without waiting for the GBA to be ready.
    if (!failed) response = cf_exchange(0);

    REG_IME = saved_ime;

    if (!failed && response == ACK_OK) return true;

    cf_delay_loop(10000); // give the dongle time to recover
  }
  return false;
}

// Requests data from the dongle. `out` receives at most `max_size` bytes.
static bool cf_request_packet(uint32_t cmd, void *out, uint32_t max_size,
                              uint32_t *actual_size) {
  for (int attempt = 0; attempt < 3; attempt++) {
    uint32_t val = 0;
    uint32_t size = 0;
    uint32_t expected_crc = 0;
    bool failed = false;

    uint16_t saved_ime = REG_IME;
    REG_IME = 0;

    cf_exchange(cmd);

    // Handshake: we send our sync word until the dongle answers with its own.
    // It can take seconds if the dongle is busy with the network.
    int timeout = 0;
    int silent = 0;
    while (val != SPI_SYNC_BYTE_MASTER) {
      val = cf_exchange(SPI_SYNC_BYTE_SLAVE);
      // Two separate brakes: one for a dongle that answers but never syncs,
      // and one for a dongle that has stopped clocking altogether.
      if (s_last_exchange_silent) {
        if (++silent > CF_MAX_SILENT_EXCHANGES) break;
      } else {
        silent = 0;
      }
      if (++timeout > 5000000) break;
    }
    if (val != SPI_SYNC_BYTE_MASTER) failed = true;

    if (!failed) {
      size = cf_exchange(0);
      if (size == ACK_ERR || size > 32768u) failed = true;
    }

    if (!failed) {
      expected_crc = cf_exchange(0);

      uint32_t words = (size + 3u) / 4u;
      uint8_t *dst = (uint8_t *)out;
      const bool aligned = (((uintptr_t)dst) & 3u) == 0u;

      if (size > max_size) {
        // Drain the bus anyway: leaving half-read words desynchronises the
        // next command.
        for (uint32_t i = 0; i < words; i++) cf_exchange(0);
        failed = true;
      } else {
        for (uint32_t i = 0; i < words; i++) {
          uint32_t word = cf_exchange(0);
          uint32_t offset = i * 4u;
          uint32_t remaining = size - offset;
          uint32_t n = (remaining < 4u) ? remaining : 4u;

          // Same care as when sending, plus: `words` rounds up, so writing 4
          // bytes into a 1-byte destination would trample 3 bytes of stack
          // that are not ours.
          if (aligned && n == 4u) {
            *(uint32_t *)(dst + offset) = word;
          } else {
            memcpy(dst + offset, &word, n);
          }
        }
      }
    }

    REG_IME = saved_ime;

    if (failed) {
      cf_exchange(ACK_ERR);
      cf_delay_loop(10000);
      continue;
    }

    if (cf_fnv1a(out, size) == expected_crc) {
      cf_exchange(ACK_OK);
      if (actual_size) *actual_size = size;
      return true;
    }

    cf_exchange(ACK_ERR);
    cf_delay_loop(10000);
  }
  return false;
}

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------

// Exchanges 72 bytes in each direction. Returns true if it completed whole.
static bool cf_tick_exchange(const uint32_t *out_words, uint32_t *in_words) {
  REG_RCNT = 0;

  uint16_t saved_ime = REG_IME;
  REG_IME = 0;

  bool ok = true;

  // Align with the dongle's polling before sending anything.
  //
  // The GBA is the slave: it only captures a word correctly if it was armed
  // BEFORE the dongle started clocking. If the dongle lands a pulse while the
  // GBA has not armed yet, the register ends up shifted and the whole tick
  // comes out skewed. We exchange zeros (which the dongle discards) until we
  // see its probe word: at that moment we know we are on a word boundary and
  // that it has just finished a round.
  bool aligned = false;
  for (int attempt = 0; attempt < 8 && !aligned; attempt++) {
    uint32_t probe = 0;
    if (!cf_exchange_bounded(0, &probe, CF_ALIGN_BUDGET)) break;
    if (probe == SPI_SYNC_BYTE_MASTER) aligned = true;
  }

  if (!aligned) {
    REG_SIODATA32 = 0;
    REG_IME = saved_ime;
    s_stats.ticks_unaligned++;
    return false;
  }

  // The guard time travels INSIDE the command, in the upper 24 bits. On its
  // own it does not work: the dongle reads any loose word as a command, and a
  // guard of 5 was being interpreted as "scan Wi-Fi".
  uint32_t dummy = 0;
  if (!cf_exchange_bounded(CMD_NET_TICK | ((uint32_t)s_guard_us << 8), &dummy,
                           CF_FIRST_WORD_BUDGET)) {
    ok = false;
  }

  if (ok) {
    for (uint32_t i = 0; i < NET_TICK_WORDS; i++) {
      uint32_t from_dongle = 0;
      if (!cf_exchange_bounded(out_words[i], &from_dongle, CF_WORD_BUDGET)) {
        ok = false;
        break;
      }
      in_words[i] = from_dongle;
    }
  }

  // Leave the register at zero. Between ticks the GBA is not armed, but the
  // dongle keeps clocking and reads whatever is left in the register: leaving
  // the last tick word there would have it interpreted as a command.
  REG_SIODATA32 = 0;
  REG_IME = saved_ime;

  if (ok) s_stats.ticks_ok++;
  else s_stats.ticks_timeout++;
  return ok;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void cf_reset_state(void) {
  s_state = CF_DISCONNECTED;
  s_slot = 0;
  s_peers = 0;
  s_peer_mask = 0;
  s_tx_len = 0;
  s_tx_seq = 0;
  s_tx_frag = 0;
  s_tx_count = 0;
  memset(s_rx,0,sizeof(s_rx));
  s_next_rx_slot=0;
  s_last_sender=0xFF;
  s_last_sender_generation=0;
  s_rel_in_valid=false;
  s_rel_last_receipt=0;
  s_rel_submit_pending=false;
  s_frame_counter = 0;
  s_consecutive_fails = 0;
  s_capacity = 0;
  s_away_mask = 0;
  s_in_room = false;
  s_join_pending = false;
  s_leaving_room = false;
  s_remote_join_error = 0;
  s_join_error = CF_JOIN_OK;
  s_room_code[0] = '\0';
  memset(s_names, 0, sizeof(s_names));
  s_master_slot = 0xFF;
  s_room_state = CF_ROOM_WAITING;
  s_ticks_since_status = 0;
  s_rel_ticket = 0;
  s_rel_operation = 0;
  s_rel_state = CF_DELIVERY_NONE;
  s_rel_waiting = 0;
  s_last_reliable = false;
}

// Reads the room status from the dongle into the local copy. Shared by the
// connect loop and the periodic refresh so both see exactly the same fields.
static bool cf_read_status(uint8_t *out_status) {
  // The response is ALWAYS received into uint32_t words, never straight into
  // the struct. Word-aligned transport storage keeps the transfer independent
  // of structure layout (legacy status consisted only of byte fields).
  // Version 3 also returns the last admitted reliable operation.
  uint32_t raw[5] = {0};
  uint32_t actual = 0;
  if (!cf_request_packet(CMD_NET_STATUS_V3, raw, sizeof(raw), &actual)) return false;
  if (actual < sizeof(cf_status_resp_t)) return false;

  cf_status_resp_t st;
  memcpy(&st, raw, sizeof(st));
  if (st.version != 3) return false;

  if ((s_in_room || s_join_pending || s_leaving_room) &&
      !st.in_room && !st.join_pending && !st.leaving_room) {
    cf_clear_room_payloads();
    s_room_code[0] = '\0';
    memset(s_names, 0, sizeof(s_names));
  }

  if (st.status == NET_STATUS_CONNECTING && s_state == CF_CONNECTED) {
    memset(s_rx,0,sizeof(s_rx)); s_tx_count=0; s_tx_len=0;
  }
  // Also update the connection state during synchronous JOIN/LEAVE waits.
  if (st.status == NET_STATUS_CONNECTED) s_state = CF_CONNECTED;
  else if (st.status == NET_STATUS_FAILED) s_state = CF_FAILED;
  else if (st.status == NET_STATUS_IDLE) s_state = CF_DISCONNECTED;
  else if (st.status == NET_STATUS_CONNECTING && s_state != CF_CONNECTING)
    s_state = CF_RECONNECTING;
  s_join_pending = st.join_pending != 0;
  s_leaving_room = st.leaving_room != 0;
  s_remote_join_error = st.join_error;

  s_slot = st.slot;
  s_peers = st.peers;
  // A departed or newly occupied seat must not display its previous name.
  for (uint8_t slot = 0; slot < NET_MAX_SLOTS; ++slot) {
    uint8_t bit = (uint8_t)(1u << slot);
    if (!(st.peer_mask & bit) || !(s_peer_mask & bit))
      memset(s_names[slot], 0, NET_NAME_MAX);
  }
  s_peer_mask = st.peer_mask;
  s_away_mask = st.away_mask;
  s_in_room = st.in_room != 0;
  s_capacity = st.capacity;
  s_master_slot = st.master_slot;
  s_room_state = (st.room_state == 1) ? CF_ROOM_PLAYING : CF_ROOM_WAITING;

  if (s_rel_operation && st.reliable_operation == s_rel_operation) {
    s_rel_submit_pending=false;
    cf_delivery_t was=s_rel_state;
    s_rel_state=(cf_delivery_t)st.reliable_state;
    if (was != CF_DELIVERY_DELIVERED && s_rel_state == CF_DELIVERY_DELIVERED) s_stats.reliable_sent++;
    if (was != CF_DELIVERY_LOST && s_rel_state == CF_DELIVERY_LOST) s_stats.reliable_lost++;
  }
  if (s_state == CF_FAILED || s_state == CF_DISCONNECTED) {
    s_rel_submit_pending=false;
    if (s_rel_state == CF_DELIVERY_PENDING) { s_rel_state=CF_DELIVERY_LOST; s_stats.reliable_lost++; }
  }
  s_rel_waiting = st.reliable_pending;

  if (out_status) *out_status = st.status;
  return true;
}

void cf_config_init(cf_config_t *cfg) {
  if (!cfg) return;
  memset(cfg, 0, sizeof(*cfg));
}

void cf_room_init(cf_room_t *room) {
  if (!room) return;
  memset(room, 0, sizeof(*room));
  room->max_players = 2;
  // Publica por defecto: es lo que quiere quien no ha pensado en el tema, y
  // una sala privada sin codigo que compartir no le sirve a nadie.
  room->visibility = CF_PUBLIC;
}

// Copies a C string into a fixed field, truncating. Returns what was copied.
static uint8_t cf_copy_field(char *dst, uint32_t cap, const char *src) {
  uint32_t i = 0;
  if (src) {
    while (i < cap - 1u && src[i]) { dst[i] = src[i]; i++; }
  }
  dst[i] = '\0';
  return (uint8_t)i;
}

// Reads a room metadata snapshot after joining and at the status cadence.
// Periodic reads also catch slot replacements whose presence mask stays equal.
static void cf_refresh_room_info(void) {
  static cf_room_info_t info;
  uint32_t actual = 0;

  if (!cf_request_packet(CMD_NET_ROOM_INFO, &info, sizeof(info), &actual)) return;
  if (actual < sizeof(info)) return;

  memcpy(s_room_code, info.code, NET_ROOM_CODE_MAX);
  s_room_code[NET_ROOM_CODE_MAX - 1] = '\0';
  memcpy(s_names, info.names, sizeof(s_names));
  for (uint32_t i = 0; i < NET_MAX_SLOTS; i++) {
    s_names[i][NET_NAME_MAX - 1] = '\0';
  }
}

bool cf_connect(const cf_config_t *cfg, void (*wait_frames)(int n)) {
  cf_reset_state();
  memset(&s_stats, 0, sizeof(s_stats));

  if (!cfg || !cfg->api_key || !cfg->api_key[0]) return false;

  // With no dongle there is nothing to attempt, and saying so quickly matters:
  // the game needs to be able to draw "connect your CanoFlash" instead of
  // locking up. The state stays CF_DISCONNECTED, which is how the game tells
  // this case apart from "there is a dongle but the connection failed"
  // (CF_FAILED).
  if (!cf_dongle_present(wait_frames)) return false;

  s_guard_us = cfg->guard_us ? cfg->guard_us : 5;
  if (s_guard_us < 1) s_guard_us = 1;
  if (s_guard_us > 500) s_guard_us = 500;

  uint8_t hz = cfg->tick_hz ? cfg->tick_hz : 20;
  if (hz > 60) hz = 60;
  s_frames_per_tick = (uint8_t)(60u / hz);
  if (s_frames_per_tick == 0) s_frames_per_tick = 1;

  cf_open_req_t req;
  memset(&req, 0, sizeof(req));
  req.mode = NET_OPEN_MODE_RELAY;
  req.key_len = cf_copy_field(req.api_key, NET_API_KEY_MAX, cfg->api_key);

  if (!cf_send_packet(CMD_NET_OPEN, &req, sizeof(req))) {
    s_state = CF_FAILED;
    return false;
  }

  s_state = CF_CONNECTING;

  // Opening is slow — the TLS handshake takes about 4 s — so it happens in two
  // stages: request, then poll. Requesting and waiting in a single call made
  // the GBA give up too early, retry, and the dongle open several sessions at
  // once.
  for (int attempt = 0; attempt < CF_CONNECT_ATTEMPTS; attempt++) {
    if (wait_frames) wait_frames(6);

    uint8_t status = 0;
    if (cf_read_status(&status)) {
      if (status == NET_STATUS_CONNECTED) {
        s_state = CF_CONNECTED;
        return true;
      }
      if (status == NET_STATUS_FAILED) {
        s_state = CF_FAILED;
        return false;
      }
    }
  }

  s_state = CF_FAILED;
  return false;
}

/**
 * Manda la peticion de sala y espera a estar dentro.
 *
 * Los tres modos comparten todo salvo lo que piden, asi que comparten codigo:
 * tener tres copias de esta espera seria tres sitios donde arreglar el mismo
 * fallo.
 */
static bool cf_join_internal(uint8_t mode, const cf_room_t *room,
                             const char *code, void (*wait_frames)(int n)) {
  s_join_error = CF_JOIN_OK;

  if (s_state != CF_CONNECTED) {
    s_join_error = CF_JOIN_NO_SESSION;
    return false;
  }

  // Refresh before deciding: a previous leave may still be in flight.
  if (!cf_read_status(NULL)) {
    s_join_error = CF_JOIN_NO_LINK;
    return false;
  }
  if (s_state != CF_CONNECTED) {
    s_join_error = CF_JOIN_NO_SESSION;
    return false;
  }
  if (s_in_room || s_join_pending || s_leaving_room) {
    s_join_error = CF_JOIN_BUSY;
    return false;
  }
  cf_clear_room_payloads();

  cf_join_req_t req;
  memset(&req, 0, sizeof(req));
  req.mode = mode;
  req.max_players = (room && room->max_players >= 2) ? room->max_players : 2;
  req.visibility = room ? (uint8_t)room->visibility : 1;
  req.code_len = cf_copy_field(req.code, NET_ROOM_CODE_MAX, code);
  req.params_len = cf_copy_field(req.params, NET_ROOM_PARAMS_MAX,
                                 room ? room->params : NULL);

  // The request never made it onto the cable, so nothing is going to answer.
  // Worth its own error: to the player it looks the same as a room that will
  // not have them, and it is nothing of the sort.
  if (!cf_send_packet(CMD_NET_JOIN, &req, sizeof(req))) {
    s_join_error = CF_JOIN_NO_LINK;
    return false;
  }
  s_join_pending = true;

  // Entrar es rapido comparado con conectar —la sesion ya esta abierta— pero
  // sigue siendo un viaje de ida y vuelta al servidor.
  for (int attempt = 0; attempt < 60; attempt++) {
    if (wait_frames) wait_frames(6);
    if (!cf_read_status(NULL)) continue;

    if (s_state != CF_CONNECTED) {
      s_join_error = CF_JOIN_NO_SESSION;
      return false;
    }
    if (s_remote_join_error != 0) {
      switch (s_remote_join_error) {
      case 3: s_join_error = CF_JOIN_NO_SUCH_ROOM; break;
      case 8: s_join_error = CF_JOIN_FULL; break;
      case 9: s_join_error = CF_JOIN_PLAYING; break;
      default: s_join_error = CF_JOIN_FAILED; break;
      }
      return false;
    }
    if (s_in_room && !s_join_pending && !s_leaving_room) {
      // Initial code/name snapshot. cf_poll refreshes these 140 bytes again
      // at the periodic status cadence, including same-mask seat replacement.
      cf_refresh_room_info();
      return true;
    }
  }

  // Unknown result is not "wrong code". Cancel the outstanding operation;
  // ordered JOIN/LEAVE on the socket also releases a late WELCOME.
  cf_leave_room();
  s_join_error = CF_JOIN_TIMEOUT;
  return false;
}

const char *cf_create_room(const cf_room_t *room, void (*wait_frames)(int n)) {
  if (!cf_join_internal(0 /* CREATE */, room, NULL, wait_frames)) return NULL;
  return s_room_code;
}

bool cf_join_code(const char *code, void (*wait_frames)(int n)) {
  if (!code || !code[0]) {
    s_join_error = CF_JOIN_NO_SUCH_ROOM;
    return false;
  }
  return cf_join_internal(1 /* CODE */, NULL, code, wait_frames);
}

bool cf_matchmake(const cf_room_t *room, void (*wait_frames)(int n)) {
  return cf_join_internal(2 /* MATCH */, room, NULL, wait_frames);
}

cf_join_error_t cf_join_error(void) { return s_join_error; }

static void cf_clear_room_payloads(void) {
  s_tx_count = 0;
  s_tx_len = 0;
  s_tx_frag = 0;
  memset(s_rx,0,sizeof(s_rx));
  s_next_rx_slot=0;
  s_last_sender=0xFF;
  s_last_sender_generation=0;
  s_rel_in_valid=false;
  s_rel_last_receipt=0;
  s_rel_submit_pending=false;
  s_rel_waiting = 0;
  s_last_reliable = false;
  if (s_rel_state == CF_DELIVERY_PENDING) { s_rel_state=CF_DELIVERY_LOST; s_stats.reliable_lost++; }
}

void cf_leave_room(void) {
  if (s_state != CF_CONNECTED ||
      (!s_in_room && !s_join_pending && !s_leaving_room)) return;

  uint16_t saved_ime = REG_IME;
  REG_IME = 0;
  cf_exchange(CMD_NET_LEAVE_ROOM);
  REG_SIODATA32 = 0;
  REG_IME = saved_ime;

  // Se espera a que el servidor lo confirme en vez de darlo por hecho: si se
  // asumiera, el juego volveria al menu creyendo que ha salido de una sala en
  // la que sigue, y sus companeros lo verian ahi parado.
  for (int attempt = 0; attempt < 30; attempt++) {
    if (cf_read_status(NULL) && !s_in_room && !s_join_pending && !s_leaving_room) break;
    if (s_state != CF_CONNECTED) return;
  }
  // If still pending, preserve the truth. A new JOIN is refused until LEFT_ROOM.
  if (s_in_room || s_join_pending || s_leaving_room) return;
  cf_clear_room_payloads();

  s_peer_mask = 0;
  s_away_mask = 0;
  s_room_code[0] = '\0';
  memset(s_names, 0, sizeof(s_names));
}

bool cf_in_room(void) { return s_in_room; }
const char *cf_room_code(void) { return s_room_code; }

const char *cf_player_name(uint8_t slot) {
  if (slot >= NET_MAX_SLOTS) return "";
  return s_names[slot];
}

void cf_disconnect(void) {
  if (s_state != CF_DISCONNECTED) {
    uint16_t saved_ime = REG_IME;
    REG_IME = 0;
    cf_exchange(CMD_NET_CLOSE);
    REG_SIODATA32 = 0;
    REG_IME = saved_ime;
  }
  cf_reset_state();
}

cf_state_t cf_status(void) { return s_state; }
uint8_t cf_slot(void) { return s_slot; }
uint8_t cf_peers(void) { return s_peers; }
uint8_t cf_peer_mask(void) { return s_peer_mask; }
uint8_t cf_capacity(void) { return s_capacity; }
uint8_t cf_away_mask(void) { return s_away_mask; }
uint8_t cf_master_slot(void) { return s_master_slot; }
cf_room_state_t cf_room_state(void) { return s_room_state; }

bool cf_is_master(void) {
  // 0xFF means "not known yet", and cannot be confused with a real slot
  // because there are no 256-player rooms.
  return s_state == CF_CONNECTED && s_in_room && !s_leaving_room &&
         s_master_slot != 0xFF &&
         s_master_slot == s_slot;
}

bool cf_set_room_state(cf_room_state_t state) {
  if (!cf_is_master()) return false;

  // The state rides INSIDE the command, in the upper bits. On its own it does
  // not work: the dongle reads any loose word as if it were a command.
  uint32_t cmd = CMD_NET_SET_STATE | ((uint32_t)(state & 0xFF) << 8);

  uint16_t saved_ime = REG_IME;
  REG_IME = 0;
  uint32_t r = cf_exchange(cmd);
  REG_SIODATA32 = 0;
  REG_IME = saved_ime;
  (void)r;

  // s_room_state is NOT touched here. The change is only believed once the
  // server confirms it through the status poll: if the relay refused it, the
  // game would have started a match nobody else joined.
  return true;
}

bool cf_can_send(void) {
  return s_state == CF_CONNECTED && s_in_room && !s_join_pending &&
         !s_leaving_room && s_tx_count == 0;
}

bool cf_send(const void *data, uint16_t len) {
  if (!cf_can_send()) return false;
  if (!data || len == 0 || len > CF_MAX_MESSAGE) return false;

  memcpy(s_tx_buf, data, len);
  s_tx_len = len;
  s_tx_frag = 0;
  s_tx_count = (uint8_t)((len + CF_FRAG_BYTES - 1u) / CF_FRAG_BYTES);

  // 0 is reserved for "this tick carries nothing", so the sequence runs 1..65535.
  s_tx_seq = (uint16_t)(s_tx_seq == 65535 ? 1 : s_tx_seq + 1);
  return true;
}

uint8_t cf_send_reliable(const void *data, uint16_t len) {
  if (s_state != CF_CONNECTED || !s_in_room || s_join_pending || s_leaving_room) return 0;
  if (!data || !len || len > CF_MAX_MESSAGE || s_rel_operation == 0xffffffffu) return 0;
  if (s_rel_state == CF_DELIVERY_PENDING) return 0;
  // Assign identity BEFORE any SPI I/O. Lost packet/status ACKs cannot turn a
  // retry into a second game event. cf_poll owns retries of this same operation.
  ++s_rel_operation;
  s_rel_ticket=(uint8_t)((s_rel_operation-1)%255+1);
  s_rel_submit.operation=s_rel_operation;
  memcpy(s_rel_submit.data,data,len);
  s_rel_submit_len=len;
  s_rel_submit_pending=true;
  s_rel_state=CF_DELIVERY_PENDING;
  return s_rel_ticket;
}

cf_delivery_t cf_delivery(uint8_t ticket) {
  if (ticket == 0 || ticket != s_rel_ticket) return CF_DELIVERY_NONE;
  return s_rel_state;
}

uint8_t cf_reliable_waiting(void) { return s_rel_waiting + (s_rel_in_valid ? 1 : 0); }
bool cf_last_was_reliable(void) { return s_last_reliable; }

uint8_t cf_last_sender(void) { return s_last_sender; }
uint32_t cf_last_sender_generation(void) { return s_last_sender_generation; }

static uint16_t cf_take_reliable(void *out, uint16_t max_len) {
  if (!out || !max_len) return 0;
  if (!s_rel_in_valid) {
    uint32_t actual=0;
    if (!cf_request_packet(CMD_NET_RELIABLE_GET,&s_rel_in,sizeof(s_rel_in),&actual)) return 0;
    if (actual < 16 || !s_rel_in.len || s_rel_in.len > CF_MAX_MESSAGE ||
        actual != 16+s_rel_in.len || s_rel_in.from_slot >= NET_MAX_SLOTS || !s_rel_in.receipt) return 0;
    if (s_rel_waiting) --s_rel_waiting;
    // The ESP may repeat a response when our packet ACK was lost on the wire.
    if (s_rel_in.receipt <= s_rel_last_receipt) return 0;
    s_rel_in_valid=true;
  }
  if (s_rel_in.len > max_len) return 0; // preserve whole event until a large enough buffer is provided
  memcpy(out,s_rel_in.data,s_rel_in.len);
  s_last_sender=s_rel_in.from_slot;
  s_last_sender_generation=s_rel_in.generation;
  s_rel_last_receipt=s_rel_in.receipt;
  s_rel_in_valid=false;
  s_stats.reliable_recv++;
  return (uint16_t)s_rel_in.len;
}

static void cf_accept_normal(const uint8_t *rx) {
  const uint8_t from=rx[NET_TICK_BYTES];
  if (from >= NET_MAX_SLOTS) return;
  uint32_t generation;
  memcpy(&generation,rx+NET_TICK_BYTES+4,4);
  if (!generation) return;
  cf_rx_t *r=&s_rx[from];
  if (r->generation != generation) {
    memset(r,0,sizeof(*r)); r->generation=generation;
  }
  uint16_t seq=(uint16_t)rx[0]|((uint16_t)rx[1]<<8);
  uint8_t frag=rx[2]>>4, count=rx[2]&15, len=rx[3];
  if (!seq || !count || count > CF_MAX_FRAGS || frag >= count || !len || len > CF_FRAG_BYTES) return;
  // Only the last fragment may be short, and the full message must fit.
  if ((frag+1<count && len != CF_FRAG_BYTES) || (uint16_t)frag*CF_FRAG_BYTES+len > CF_MAX_MESSAGE) return;
  if (seq==r->last_seq && frag==r->last_frag) return;
  if (!frag) { r->seq=seq; r->count=count; r->next_frag=0; r->len=0; }
  if (!r->count || seq!=r->seq || count!=r->count || frag!=r->next_frag) {
    r->count=0; s_stats.msgs_dropped++; return;
  }
  r->last_seq=seq; r->last_frag=frag;
  memcpy(r->data+r->len,rx+CF_FRAG_HEADER,len);
  r->len+=len; ++r->next_frag;
  if (r->next_frag==r->count) {
    memcpy(r->ready,r->data,r->len); r->ready_len=r->len; r->count=0;
  }
}
static uint16_t cf_take_normal(void *out, uint16_t max_len) {
  if (!out || !max_len) return 0;
  for (uint8_t i=0;i<NET_MAX_SLOTS;++i) {
    uint8_t slot=(s_next_rx_slot+i)%NET_MAX_SLOTS;
    cf_rx_t *r=&s_rx[slot];
    if (!r->ready_len || r->ready_len>max_len) continue;
    uint16_t len=r->ready_len; r->ready_len=0;
    memcpy(out,r->ready,len);
    s_last_sender=slot; s_last_sender_generation=r->generation;
    s_next_rx_slot=(slot+1)%NET_MAX_SLOTS;
    s_stats.msgs_received++;
    return len;
  }
  return 0;
}

uint16_t cf_poll(void *out, uint16_t max_len) {
  // Keep ticking while reconnecting. The link to the DONGLE is fine; the one
  // that broke is between the dongle and the server. Stop ticking and the
  // dongle would give us up for dead after 60 s, right while it is recovering.
  if (s_state != CF_CONNECTED && s_state != CF_RECONNECTING) return 0;

  // Pacing: the game calls every frame, but we only tick every N. That way the
  // developer never has to keep count or remember to send keepalives.
  if (++s_frame_counter < s_frames_per_tick) return 0;
  s_frame_counter = 0;

  // uint32_t buffers so they are aligned: the exchange works in words and a
  // byte buffer could land anywhere.
  uint32_t out_words[NET_TICK_WORDS];
  uint32_t in_words[NET_TICK_WORDS];
  memset(out_words, 0, sizeof(out_words));
  memset(in_words, 0, sizeof(in_words));

  uint8_t *tx = (uint8_t *)out_words;

  if (s_tx_count > 0) {
    uint16_t offset = (uint16_t)(s_tx_frag * CF_FRAG_BYTES);
    uint16_t remaining = (uint16_t)(s_tx_len - offset);
    uint8_t n = (uint8_t)(remaining < CF_FRAG_BYTES ? remaining : CF_FRAG_BYTES);

    tx[0] = (uint8_t)s_tx_seq;
    tx[1] = (uint8_t)(s_tx_seq >> 8);
    tx[2] = (uint8_t)((s_tx_frag << 4) | s_tx_count);
    tx[3] = n;
    memcpy(tx + CF_FRAG_HEADER, s_tx_buf + offset, n);
  }

  bool ok = cf_tick_exchange(out_words, in_words);

  if (ok && s_rel_submit_pending && s_state==CF_CONNECTED && s_in_room && !s_leaving_room) {
    cf_send_packet(CMD_NET_RELIABLE_SEND,&s_rel_submit,4+s_rel_submit_len);
    cf_read_status(NULL);
  }

  // Room status refresh. It goes AFTER the tick, never before: the tick is the
  // one thing with a frame budget, and it cannot wait on a query that uses the
  // slow path.
  if (ok && ++s_ticks_since_status >= CF_STATUS_EVERY_TICKS) {
    s_ticks_since_status = 0;

    uint8_t link = 0;
    if (cf_read_status(&link)) {
      // The dongle keeps working on a broken link behind our back: it retries
      // with backoff and asks for the same seat. All the game needs is to know
      // that it is happening, so it can pause instead of playing against
      // nobody.
      if (link == NET_STATUS_CONNECTING) {
        s_state = CF_RECONNECTING;
      } else if (link == NET_STATUS_CONNECTED) {
        s_state = CF_CONNECTED;
        if (s_in_room && !s_join_pending && !s_leaving_room)
          cf_refresh_room_info();
      } else if (link == NET_STATUS_FAILED) {
        s_state = CF_FAILED;
      }
    }
  }

  // One fragment per tick. If the exchange failed we do NOT advance: the same
  // fragment is retried next time, which is more useful than declaring the
  // message lost.
  if (ok && s_tx_count > 0) {
    s_tx_frag++;
    if (s_tx_frag >= s_tx_count) {
      s_tx_count = 0;
      s_tx_len = 0;
      s_stats.msgs_sent++;
    }
  }

  if (!ok) {
    // Two thresholds, and the difference matters.
    //
    // A short streak means the dongle is not answering RIGHT NOW — most often
    // because it is blocked on a TLS handshake trying to get the connection
    // back. Reporting that as CF_RECONNECTING lets the game pause instead of
    // abandoning a session that is three seconds from returning.
    //
    // A long streak means it really is gone: switched off, flat battery, cable
    // pulled.
    s_consecutive_fails++;
    if (s_consecutive_fails >= CF_FAILS_BEFORE_DEAD) {
      s_state = CF_FAILED;
    } else if (s_consecutive_fails >= CF_FAILS_BEFORE_TROUBLE &&
               s_state == CF_CONNECTED) {
      s_state = CF_RECONNECTING;
    }
    return 0;
  }

  // The link answered. If we had written it off as trouble, it is back — but
  // the room state comes from the status poll, so CF_CONNECTED is only
  // restored there, not here. Guessing would have the game resume a match the
  // server has not confirmed.
  s_consecutive_fails = 0;

  if (s_state != CF_CONNECTED || !s_in_room || s_leaving_room) return 0;
  const uint8_t *rx=(const uint8_t*)in_words;
  s_rel_waiting=rx[NET_TICK_BYTES+1];
  cf_accept_normal(rx); // process even when reliable delivery wins this poll
  if (s_rel_waiting || s_rel_in_valid) {
    uint16_t n=cf_take_reliable(out,max_len);
    if (n) { s_last_reliable=true; return n; }
  }
  uint16_t n=cf_take_normal(out,max_len);
  if (n) s_last_reliable=false;
  return n;
}

void cf_get_stats(cf_stats_t *out) {
  if (out) *out = s_stats;
}
