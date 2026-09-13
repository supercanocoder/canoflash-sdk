// ===========================================================================
// CanoFlash Net — online multiplayer SDK for the Game Boy Advance
//
// Plain C99, no dependencies. Works with bare devkitARM, tonc, Butano, or from
// Rust through `extern "C"`. It deliberately includes no engine headers: the
// moment a library like this forces one engine on you, it locks out half the
// scene.
//
// HOW IT WORKS, IN ONE SENTENCE
//   The GBA never talks to the internet. It talks to a CanoFlash dongle over
//   the Link port, and the dongle keeps the connection to the server.
//
// THREE THINGS TO UNDERSTAND BEFORE YOU START
//
//   1. The GBA is the SLAVE on the serial port. It cannot transmit whenever it
//      likes; it can only answer when the dongle drives the clock. That is why
//      all traffic rides on a periodic fixed-size exchange, a "tick", which
//      sends and receives at the same time.
//
//   2. cf_poll() drives periodic data exchange and pending reliable work.
//      Connection and room commands also use synchronous serial I/O.
//
//   3. No dynamic allocation. Serial I/O is synchronous and can mask
//      interrupts; connect and room operations also wait for network results.
//
// MINIMAL USAGE
//
//   cf_config_t cfg;
//   cf_config_init(&cfg);            // fill in the defaults
//   cfg.api_key = "cfn_...";         // yours, from the dashboard
//
//   if (!cf_connect(&cfg, wait_frames)) return; // callback advances frames
//
//   cf_room_t room;
//   cf_room_init(&room);
//   room.max_players = 4;            // for THIS match
//   room.params = "mode=2v2";        // opaque; games with matching params meet
//
//   if (!cf_matchmake(&room, wait_frames)) return; // or create / join by code
//
//   // once per frame:
//   uint8_t buf[CF_MAX_MESSAGE];
//   uint16_t n = cf_poll(buf, sizeof(buf));
//   if (n > 0) { /* a message from another player arrived */ }
//   cf_send(&my_state, sizeof(my_state));
//
// Licensed under the MIT License. See LICENSE.
// ===========================================================================

#ifndef CANOFLASH_H
#define CANOFLASH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF_VERSION_MAJOR 1
#define CF_VERSION_MINOR 0
#define CF_VERSION_PATCH 0

// Largest payload on either channel. Normal messages above 60 bytes use
// multiple fragments; cf_poll() only returns complete messages.
#define CF_MAX_MESSAGE 480

// Legacy constant retained for source compatibility. Not a current wire limit.
// Room params: 31 bytes + NUL; codes: 6 digits; player names: 15 bytes + NUL.
#define CF_ROOM_MAX 24

typedef enum {
  CF_DISCONNECTED = 0,  // no active SDK session
  CF_CONNECTING = 1,    // the dongle is talking to the server
  CF_CONNECTED = 2,     // session ready; cf_in_room() checks membership
  CF_FAILED = 3,        // the attempt failed, or the link is gone for good

  /**
   * The link broke and is being retried. A room seat may still be reserved.
   *
   * PAUSE HERE. Do not keep simulating: nothing you send is going anywhere,
   * and when you come back the others will have moved on without you. Show
   * "reconnecting" and wait.
   *
   * It resolves on its own — into CF_CONNECTED if the link comes back within
   * the grace period, or CF_FAILED if it does not.
   */
  CF_RECONNECTING = 4
} cf_state_t;

/**
 * Whether the match has started.
 *
 * YOUR GAME DECIDES THIS, not the server. The server cannot: it has never seen
 * your game and has no idea what "ready" means in it. It only carries the flag
 * and tells everyone when it changes.
 *
 * Start when the room fills up, or give the master a "start now" button, or
 * anything else. A room that is playing is taken out of matchmaking, so nobody
 * drops into a match already underway even if there are free seats.
 */
typedef enum {
  CF_ROOM_WAITING = 0,
  CF_ROOM_PLAYING = 1
} cf_room_state_t;

/**
 * How a reliable message is doing. See cf_send_reliable().
 */
typedef enum {
  CF_DELIVERY_NONE = 0,      // no such ticket, or nothing has been sent
  CF_DELIVERY_PENDING = 1,   // on its way
  CF_DELIVERY_DELIVERED = 2, // relay admission confirmed; not game processing
  CF_DELIVERY_LOST = 3       // unconfirmed/failed; remote outcome may be unknown
} cf_delivery_t;

typedef struct {
  // Your game's API key, from the CanoFlash dashboard. Required.
  //
  // It IDENTIFIES your game; it does not authorise it. The key ships compiled
  // inside your ROM and anyone can pull it out of a .gba with a hex editor, so
  // do not treat it as a secret. What authorises the connection is the
  // dongle's own credential, managed by firmware and sent to the service
  // over TLS.
  //
  // What it is actually for: keeping your matches apart from other games',
  // letting a broken game be disabled, and making a mistyped key fail with a
  // clear error instead of dropping the player into a ghost room where nobody
  // will ever arrive.
  const char *api_key;

  // Requested ticks per second at 60 fps. 0 selects 20; values above 60 are
  // clamped. The interval is floor(60 / hz), so 25 requests 30 effective Hz
  // and values above 30 tick every frame. Actual frame rate scales the result.
  // Start at 20 Hz and profile your game; old transport benchmarks are not
  // guarantees for the current protocol. See docs/integration.md.
  uint8_t tick_hz;

  // Microseconds of guard time between words of a tick. 0 means 5, which is
  // the default. Nonzero values are clamped to 1..500. Tune only with hardware
  // measurements; increasing this is not a general fix for audio/interrupt load.
  uint16_t guard_us;
} cf_config_t;

/** Whether a room can be found by matchmaking. */
typedef enum {
  CF_PRIVATE = 0, // only reachable with its code
  CF_PUBLIC = 1   // matchmaking can send strangers here
} cf_visibility_t;

/**
 * What kind of match you want. This is asked for per match, not per game: one
 * game can have a 1v1 mode and an 8-player mode without registering twice.
 */
typedef struct {
  // How many players THIS match needs, 2..8. The server has a hard cap as
  // a safety net; if you ask for more you get the cap, and cf_capacity() tells
  // you what you actually got.
  uint8_t max_players;

  // Only meaningful when creating. A private room is reachable only by its
  // code — that is how "play with my friend" works.
  cf_visibility_t visibility;

  // Opaque matchmaking parameters. NULL or "" is fine. Players whose params
  // match EXACTLY, with the same game key and effective capacity, can meet.
  // Maximum 31 bytes; longer strings are silently truncated.
  //
  // Put whatever your game needs to keep separate: "mode=2v2", "map=3;laps=5".
  //
  // CAREFUL: exact means exact. Anything that varies between players — a
  // timestamp, a nickname, a random seed — means they never meet, and it looks
  // like the server is broken. Keep it to things both sides agree on.
  const char *params;
} cf_room_t;

// Fill `cfg` with the defaults: 20 Hz and the normal guard time. Always use
// this instead of `= {0}`, which is more fragile if the struct ever grows and
// also trips -Wextra.
void cf_config_init(cf_config_t *cfg);

// Fill `room` with defaults: two players, public visibility, no parameters.
void cf_room_init(cf_room_t *room);

// Open a session, without joining a room. Resets local state and statistics.
// cfg and its nonempty api_key are required (maximum 63 key bytes).
// The presence probe allows 360 callback frames; connection adds repeated
// waits and synchronous serial I/O. Expect seconds, not a fixed deadline.
// wait_frames(n) should draw/advance n frames without calling SDK functions.
// NULL is accepted but provides no rendering or frame pacing.
// Returns false with CF_DISCONNECTED for invalid configuration or no device,
// or CF_FAILED for OPEN/network/authentication/compatibility failures.
// Requires STATUS_V3, TICK_V2 and reliable V2 firmware; see docs/compatibility.md.
bool cf_connect(const cf_config_t *cfg, void (*wait_frames)(int n));

/**
 * Make a NEW room and wait in it.
 *
 * Always creates one, even if there are free rooms sitting empty: that is what
 * someone about to play with a specific friend expects.
 *
 * Returns the room's six-digit code to show on screen, or NULL if it could not
 * be created. The code is also available later from cf_room_code().
 */
const char *cf_create_room(const cf_room_t *room, void (*wait_frames)(int n));

/**
 * Join an existing room by its six-digit code.
 *
 * **Never creates one.** If the code does not exist you get false, not an
 * empty room — making a room because someone mistyped a digit leaves them
 * waiting for a player who is never coming.
 *
 * Works for private rooms too: the code is all you need.
 *
 * On false, cf_join_error() distinguishes a room rejection from a connection
 * failure, a pending room operation, or a timeout.
 */
bool cf_join_code(const char *code, void (*wait_frames)(int n));

/**
 * Find a public room with space, or start one.
 *
 * Only looks at public rooms, and only at those with matching `params`, so
 * game modes never get mixed. If there are none it creates one and waits —
 * with few players around, an error would just look like a broken system when
 * really you are the first one today.
 */
bool cf_matchmake(const cf_room_t *room, void (*wait_frames)(int n));

/** Why the last join attempt failed. */
typedef enum {
  CF_JOIN_OK = 0,
  CF_JOIN_NO_SUCH_ROOM = 1, // wrong code, or that room is gone
  CF_JOIN_FULL = 2,         // it exists, but every seat is taken
  CF_JOIN_PLAYING = 3,      // it exists, but the match already started
  CF_JOIN_NO_SESSION = 4,   // not connected
  CF_JOIN_NO_LINK = 5,      // the dongle did not answer
  CF_JOIN_BUSY = 6,         // already in a room, or a room operation is pending
  CF_JOIN_TIMEOUT = 7,      // no result in time; cancellation requested
  CF_JOIN_FAILED = 8        // another rejection from the relay
} cf_join_error_t;

cf_join_error_t cf_join_error(void);

/**
 * Leave the room and go back to your menu. The session stays open.
 *
 * A confirmed leave frees the seat without a resume grace period.
 *
 * Keep calling cf_poll() in your menu afterwards. It is what keeps the dongle
 * awake. Current firmware considers about 60 s without GBA contact a dead
 * Link session.
 * If the confirmation is delayed, cf_in_room() stays true until LEFT_ROOM.
 * During that wait sends are refused and another JOIN reports CF_JOIN_BUSY.
 */
void cf_leave_room(void);

/** True while you are in a room. A session can be open without one. */
bool cf_in_room(void);

/** The six-digit code of the room you are in, or "" if you are not in one. */
const char *cf_room_code(void);

/**
 * The name of the player in that seat, or "" if the seat is empty.
 *
 * Names come from the dongle, which already knows its owner's, so your game
 * never has to ask the player to type one. The SDK refreshes room metadata
 * after joining and every 20 successful ticks (about 1 s at the default rate).
 * A name may be empty while its snapshot is pending. Names use UTF-8, up to
 * 15 bytes plus the terminator; adapt unsupported characters to your font.
 */
const char *cf_player_name(uint8_t slot);

// Attempt a voluntary session close, then clear local state. No remote ACK.
// Call when leaving online mode; use cf_leave_room() between matches.
// Firmware treats about 60 s without GBA contact as a dead Link session.
void cf_disconnect(void);

// Current state. Does not query the dongle: it returns what is already known,
// refreshed on every cf_poll(). Calling it is free.
//
// It moves on its own when things go wrong mid-match:
//
//   CF_RECONNECTING  the link broke and is being retried. Pause the game.
//   CF_FAILED        it is not coming back. Return to your menu.
//
// Check it every frame during play. The SDK recovers the connection for you,
// but only your game can decide what the player sees while that happens.
cf_state_t cf_status(void);

// Your slot in the room (0..7). Identifies each player in a single byte.
uint8_t cf_slot(void);

// How many players are in the room, including you.
uint8_t cf_peers(void);

// Bitmask of occupied slots: bit N set means slot N is in the room. Use it to
// know WHO is present, not just how many.
//
// A player who has dropped stays in this mask while their seat is held — it is
// still theirs. Use cf_away_mask() to tell them apart.
uint8_t cf_peer_mask(void);

/**
 * Bitmask of players who have dropped and are trying to get back.
 *
 * Their seats are held for about 30 seconds, so they still count in cf_peers()
 * and cf_peer_mask(). They are not gone; they are away.
 *
 * WHAT TO DO ABOUT IT IS YOUR GAME'S CALL. The server only tells you. A
 * turn-based game can carry on and skip their turn; an action game probably
 * wants to pause. Only you know which.
 *
 *   if (cf_away_mask() & (1 << their_slot)) show("Player 2 reconnecting...");
 */
uint8_t cf_away_mask(void);

// Seats in this room. May be lower than you asked for, if the server capped it.
uint8_t cf_capacity(void);

/**
 * Are you the master of this room?
 *
 * The first player in is the master. If they leave, it passes to the lowest
 * present occupied slot and everyone is told.
 *
 * MIGRATION HANDS OVER THE CROWN, NOT THE STATE. If the master was holding
 * authoritative game state — whose turn it is, the deck, the score — that left
 * with them, and rebuilding it is your game's problem. The server cannot help:
 * it never knew what was in there. Design for this or the first disconnect
 * will surprise you.
 */
bool cf_is_master(void);

// Which slot is master right now, or 0xFF if it is not known yet.
uint8_t cf_master_slot(void);

// Whether the match has started. See cf_room_state_t.
cf_room_state_t cf_room_state(void);

/**
 * Ask the server to change the room state. MASTER ONLY.
 *
 * Returns false if the master precondition fails. True means the command
 * was attempted, not that the relay received it. The change is only confirmed
 * when cf_room_state() reports it after polling. Do not assume it
 * worked, or your game will start a match the others never joined.
 */
bool cf_set_room_state(cf_room_state_t state);

// Queue a message for the rest of the room. Nothing is transmitted here: it
// goes out over the following ticks, fragmented if needed.
//
// Returns false if `len` exceeds CF_MAX_MESSAGE, if there is no active room, or if
// the previous message is still on its way out. That last case is not an
// error: it is the signal that you are producing faster than the link can
// carry. Retry next frame, or send less.
bool cf_send(const void *data, uint16_t len);

// True if cf_send() would accept a message right now.
bool cf_can_send(void);

/**
 * Queue a reliable event within the live room session.
 *
 * The ordinary channel retains the latest complete payload per sender. That
 * is right for positions — an old position is worthless once a newer one
 * exists — and exactly wrong for events: a phase change, a card played, the
 * end of a turn. Those go through here.
 *
 * WHAT IT GUARANTEES. The relay retains admitted events for the recipients
 * occupying the room when submitted, including temporarily disconnected seats.
 * Each recipient receives them in relay admission order. Retransmissions are
 * deduplicated within that room session. A recipient that cannot accept events
 * loses its seat and cannot resume with missing history.
 * CF_DELIVERY_DELIVERED means SERVER ADMISSION, not game processing. A relay
 * restart, lost session or device reset ends this in-memory guarantee.
 *
 * WHAT IT DOES NOT GUARANTEE. That everyone has *processed* it before you
 * carry on. That is a barrier, and a barrier stalls on the slowest or the
 * crashed player. If your game needs one, build it on top: send the event, and
 * have each player reply with another reliable message. Then it is YOUR game
 * deciding what to do about whoever does not answer, which is where that
 * decision belongs.
 *
 * ONE IN FLIGHT AT A TIME. Until the previous one is confirmed, this returns
 * 0. Queueing without limit would mean you never know how long your message
 * will take to leave; refusing is a more honest answer. If you need to send
 * two in a row, queue them yourself.
 *
 * Returns a ticket from 1 to 255 to pass to cf_delivery(), or 0 if the message
 * was not accepted: no session, a bad length, or the previous one still going.
 *
 * This queues a copy locally. cf_poll() sends it and retries the SAME operation
 * if an SPI acknowledgement is lost. Do not create a new event to retry an
 * uncertain delivery. CF_DELIVERY_LOST can mean an unknown server outcome;
 * recovery requiring persistence belongs in the game's protocol.
 */
uint8_t cf_send_reliable(const void *data, uint16_t len);

/**
 * How the reliable message with this ticket is doing.
 *
 * Only the most recent ticket is tracked, since only one can be in flight.
 * Noncurrent tickets return CF_DELIVERY_NONE. Tickets wrap at 255, so they
 * must not be retained as permanent event IDs.
 *
 *   uint8_t ticket = cf_send_reliable(&phase, sizeof(phase));
 *   ...
 *   switch (cf_delivery(ticket)) {
 *     case CF_DELIVERY_DELIVERED: start_phase();     break;
 *     case CF_DELIVERY_LOST:      abandon_match();   break;
 *     default:                    keep_waiting();    break;
 *   }
 */
cf_delivery_t cf_delivery(uint8_t ticket);

/**
 * How many reliable messages have arrived and are waiting to be read.
 *
 * You do not normally need this: cf_poll() hands them over on its own. It is
 * here for a debug screen.
 */
uint8_t cf_reliable_waiting(void);

// Source of the last message actually returned by cf_poll(), for either channel.
// Slot is assigned by the relay, never taken from untrusted game payload bytes.
uint8_t cf_last_sender(void);
// Changes when a slot gets a new occupant; preserved when that occupant resumes.
uint32_t cf_last_sender_generation(void);

// Call this ONCE PER FRAME, always, even with nothing to send. It drives
// everything: it decides when a tick is due based on tick_hz, transmits what
// is pending, receives, and reassembles.
//
// Returns the size of the message written to `out`, or 0 if nothing completed
// this frame. At most one is returned per due successful tick. Messages are never
// truncated: use a CF_MAX_MESSAGE buffer. NULL/0 keeps polling without consuming
// reliable events. Normal messages retain the latest complete state per sender.
//
// Reliable messages come out of here too, and they take priority: if one is
// waiting it is handed over before any ordinary payload. Use cf_last_was_reliable()
// if your game needs to tell them apart.
//
// NOTE: it counts FRAMES, not milliseconds. If your game does not hold 60 fps,
// your network rate drops with it — at 25 fps, asking for 20 Hz gets you 8.
// The symptom looks exactly like network lag and is not. If you see stutter,
// check your real frame rate first.
uint16_t cf_poll(void *out, uint16_t max_len);

/**
 * Whether the message cf_poll() just returned came through the reliable
 * channel. Only meaningful right after a cf_poll() that returned non-zero.
 *
 * Many games do not need this — put a type byte in your own messages and you
 * will never ask. It is here for the ones that do.
 */
bool cf_last_was_reliable(void);

// --- Diagnostics. None of this is needed to play. ---

typedef struct {
  uint32_t ticks_ok;
  uint32_t ticks_timeout;   // the dongle did not answer in time
  uint32_t ticks_unaligned; // could not synchronise with the dongle
  uint32_t msgs_sent;
  uint32_t msgs_received;
  uint32_t msgs_dropped;  // lost fragments: incomplete messages thrown away
  uint32_t reliable_sent; // reliable messages the server confirmed
  uint32_t reliable_recv; // reliable messages received
  uint32_t reliable_lost; // lost/unconfirmed admission, possibly unknown outcome
} cf_stats_t;

void cf_get_stats(cf_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif // CANOFLASH_H
