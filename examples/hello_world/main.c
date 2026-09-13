// ===========================================================================
// CanoFlash Net — hello world
//
// The smallest thing that works: two squares, one per player, moving on two
// consoles at once. If you read one file to understand the SDK, read this one.
//
// Every network call is here, in the order you use them:
//
//   cf_connect()      open a session with the server        once
//   cf_matchmake()    get into a room with someone          once
//   cf_room_state()   wait until the match starts           each frame
//   cf_poll()         receive, and drive everything else    each frame
//   cf_send()         send your state                       each frame
//
// Everything else in this file is Game Boy plumbing, kept as small as it can
// be: a bitmap mode, a rectangle, and waiting for the screen. No engine, no
// assets, no sprites — so that nothing distracts from the five calls above.
//
// Rooms with codes, private games, player names and a real menu are covered in
// examples/lobby and docs/rooms.md.
//
// Licensed under the MIT License. See LICENSE at the repository root.
// ===========================================================================

#include <stddef.h>

#include "canoflash.h"
#include "../canoflash_example_config.h"

// --- Your game -------------------------------------------------------------

// Configure examples/canoflash_example_config.local.h; see examples/README.md.
// The public default is a placeholder. A local override supplies your game key.
#define MY_API_KEY CF_EXAMPLE_API_KEY

// What travels over the network. Small and fixed-size is exactly what suits
// this channel: it goes out whole, every tick, and the newest one wins.
typedef struct {
  int16_t x;
  int16_t y;
} player_t;

// --- Game Boy plumbing -----------------------------------------------------

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_VCOUNT (*(volatile uint16_t *)0x04000006)
#define REG_KEYINPUT (*(volatile uint16_t *)0x04000130)
#define VRAM ((volatile uint16_t *)0x06000000)

#define MODE3 0x0003
#define BG2_ON 0x0400
#define SCREEN_W 240
#define SCREEN_H 160
#define SIDE 12

#define RGB(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))
#define COL_BACKGROUND RGB(8, 10, 20) // clearly not black: a black screen means
                                      // the ROM is not running at all
#define COL_ME RGB(31, 24, 4)
#define COL_THEM RGB(6, 28, 20)
#define COL_NOTICE RGB(31, 31, 31)

// Keys read inverted: a 0 bit means pressed.
#define KEY_RIGHT 0x0010
#define KEY_LEFT 0x0020
#define KEY_UP 0x0040
#define KEY_DOWN 0x0080

static void wait_vblank(void) {
  while (REG_VCOUNT >= SCREEN_H) {}
  while (REG_VCOUNT < SCREEN_H) {}
}

static void fill(int x, int y, int w, int h, uint16_t colour) {
  for (int j = 0; j < h; j++) {
    int py = y + j;
    if (py < 0 || py >= SCREEN_H) continue;
    for (int i = 0; i < w; i++) {
      int px = x + i;
      if (px < 0 || px >= SCREEN_W) continue;
      VRAM[py * SCREEN_W + px] = colour;
    }
  }
}

/**
 * Stops here forever, blinking `count` white squares.
 *
 * Failures are COUNTED, not colour-coded. On an unlit Game Boy screen yellow,
 * orange and red look the same, and a player should not have to guess which
 * one they are seeing.
 *
 *   1  no dongle: switched off, asleep, or no cable
 *   2  dongle is there, but it could not get online or into a room
 *   3  the link died mid-match
 */
static void stop(int count) {
  const int w = 20, gap = 10;
  const int x0 = (SCREEN_W - (count * w + (count - 1) * gap)) / 2;
  const int y0 = (SCREEN_H - w) / 2;

  fill(0, 0, SCREEN_W, SCREEN_H, COL_BACKGROUND);

  for (int frame = 0;; frame++) {
    wait_vblank();
    uint16_t c = ((frame / 60) % 2 == 0) ? COL_NOTICE : COL_BACKGROUND;
    for (int i = 0; i < count; i++) fill(x0 + i * (w + gap), y0, w, w, c);
  }
}

/**
 * Called while cf_connect() is waiting, so the console does not look crashed.
 *
 * The bar MOVES on purpose: a still one is indistinguishable from a frozen
 * screen, which is exactly what the player needs to be able to rule out.
 */
static void while_waiting(int frames) {
  static int t = 0;
  for (int f = 0; f < frames; f++) {
    wait_vblank();
    t = (t + 3) % SCREEN_W;
    fill(0, 0, SCREEN_W, 6, COL_BACKGROUND);
    fill(t, 0, 32, 6, COL_NOTICE);
  }
}

// --- The actual example ----------------------------------------------------

int main(void) {
  REG_DISPCNT = MODE3 | BG2_ON;
  fill(0, 0, SCREEN_W, SCREEN_H, COL_BACKGROUND);

  // Draw something BEFORE connecting: cf_connect() blocks for seconds and
  // draws nothing, so without this there is no way to tell "connecting" from
  // "the ROM never started".
  fill(0, 0, SCREEN_W, 6, COL_NOTICE);
  wait_vblank();

  // 1. Open the session. This is the slow part — a few seconds — and it
  //    happens once. Rooms come and go afterwards without paying it again.
  cf_config_t cfg;
  cf_config_init(&cfg);
  cfg.api_key = MY_API_KEY;
  cfg.tick_hz = 20; // default cadence: one tick every three frames at 60 fps

  if (!cf_connect(&cfg, while_waiting)) {
    // Two failures, and the player can act on the difference: one means "turn
    // your dongle on", the other means "no connection".
    stop(cf_status() == CF_DISCONNECTED ? 1 : 2);
  }

  // 2. Get into a room. Matchmaking joins whoever is waiting, or starts a room
  //    and waits for someone. For rooms you can share by code, see the lobby
  //    example.
  cf_room_t room;
  cf_room_init(&room);
  room.max_players = 2;

  if (!cf_matchmake(&room, while_waiting)) stop(2);

  // 3. Wait for the match to start.
  //
  //    The server does NOT decide this — it has never seen your game and does
  //    not know what "ready" means in it. It picks a master (whoever arrived
  //    first) and leaves the decision to them. Here the master starts as soon
  //    as the room is full.
  fill(0, 0, SCREEN_W, SCREEN_H, COL_BACKGROUND);
  while (cf_room_state() == CF_ROOM_WAITING) {
    wait_vblank();
    cf_poll(NULL, 0); // keeps the link alive and refreshes who is in the room

    if (cf_status() == CF_FAILED) stop(3);

    if (cf_is_master() && cf_peers() >= cf_capacity()) {
      cf_set_room_state(CF_ROOM_PLAYING);
    }

    // A bar that grows with the players present, so the wait shows progress.
    fill(0, SCREEN_H - 8, SCREEN_W, 8, COL_BACKGROUND);
    if (cf_capacity() > 0) {
      fill(0, SCREEN_H - 8, (SCREEN_W * cf_peers()) / cf_capacity(), 8, COL_THEM);
    }
  }

  // 4. Play.
  player_t me = {SCREEN_W / 2, SCREEN_H / 2};
  player_t them = {0, 0};
  bool have_them = false;

  // Where each square was last frame, so only what moved gets erased.
  //
  // Clearing the whole screen every frame looks natural and is a trap: 38400
  // pixels do not fit in one frame, the game drops to ~25 fps, and the network
  // rate collapses with it because cf_poll() counts FRAMES. At 25 fps, asking
  // for 20 Hz gets you 8, and it looks exactly like network lag.
  player_t me_prev = me, them_prev = them;

  fill(0, 0, SCREEN_W, SCREEN_H, COL_BACKGROUND);

  for (;;) {
    wait_vblank();

    uint16_t keys = ~REG_KEYINPUT;
    if (keys & KEY_LEFT) me.x -= 2;
    if (keys & KEY_RIGHT) me.x += 2;
    if (keys & KEY_UP) me.y -= 2;
    if (keys & KEY_DOWN) me.y += 2;

    if (me.x < 0) me.x = 0;
    if (me.y < 0) me.y = 0;
    if (me.x > SCREEN_W - SIDE) me.x = SCREEN_W - SIDE;
    if (me.y > SCREEN_H - SIDE) me.y = SCREEN_H - SIDE;

    // ONCE PER FRAME, ALWAYS. cf_poll() works out internally when to talk to
    // the dongle; there is no counting to do here.
    player_t received;
    if (cf_poll(&received, sizeof(received)) == sizeof(received)) {
      them = received;
      have_them = true;
    }

    // May return false if the previous message is still going out. That is not
    // an error — it means you are producing faster than the link can carry.
    cf_send(&me, sizeof(me));

    // The link broke and the SDK is getting it back. Pause: anything sent now
    // goes nowhere, and cf_poll() must keep being called or the dongle gives
    // up on us while it is reconnecting.
    if (cf_status() == CF_RECONNECTING) continue;
    if (cf_status() != CF_CONNECTED) stop(3);

    fill(me_prev.x, me_prev.y, SIDE, SIDE, COL_BACKGROUND);
    if (have_them) fill(them_prev.x, them_prev.y, SIDE, SIDE, COL_BACKGROUND);

    if (have_them) fill(them.x, them.y, SIDE, SIDE, COL_THEM);
    fill(me.x, me.y, SIDE, SIDE, COL_ME);

    me_prev = me;
    them_prev = them;
  }
}
