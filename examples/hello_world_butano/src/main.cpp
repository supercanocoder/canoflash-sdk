// ===========================================================================
// CanoFlash Net — hello world, in Butano
//
// The same game as examples/hello_world, built with Butano instead of bare
// devkitARM.
//
// It exists to prove one claim: the SDK is plain C with no engine attached.
// The session and polling flow mirrors the C example. Coordinates and drawing
// differ, so run matching builds on both consoles.
//
//   cf_connect()      open a session with the server        once
//   cf_matchmake()    get into a room with someone          once
//   cf_room_state()   wait until the match starts           each frame
//   cf_poll()         receive, and drive everything else    each frame
//   cf_send()         send your state                       each frame
//
// Licensed under the MIT License. See LICENSE at the repository root.
// ===========================================================================

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_sprite_ptr.h"
#include "bn_vector.h"
#include "bn_sprite_items_square.h"

#include "canoflash.h"
#include "../../canoflash_example_config.h"

namespace {

// Configure examples/canoflash_example_config.local.h; see examples/README.md.
// The public default is a placeholder; a local override supplies your key.
constexpr const char *MY_API_KEY = CF_EXAMPLE_API_KEY;

// What travels over the network. Small and fixed-size is what suits this
// channel: it goes out whole, every tick, and the newest one wins.
struct player_t {
  int16_t x;
  int16_t y;
};

/**
 * Called while cf_connect() is waiting, so the console does not look crashed.
 */
void while_waiting(int frames) {
  for (int f = 0; f < frames; f++) bn::core::update();
}

/**
 * Stops here forever, blinking `count` sprites.
 *
 * Failures are COUNTED, not colour-coded: on an unlit Game Boy screen yellow,
 * orange and red look the same.
 *
 *   1  no dongle          2  no connection or no room          3  link died
 */
[[noreturn]] void stop(int count) {
  bn::vector<bn::sprite_ptr, 3> marks;
  for (int i = 0; i < count; i++) {
    // Integer arithmetic on purpose: the GBA has no floating point unit.
    marks.push_back(bn::sprite_items::square.create_sprite(
        (i * 28) - ((count - 1) * 14), 0));
  }

  for (int frame = 0;; frame++) {
    bool visible = ((frame / 60) % 2) == 0;
    for (bn::sprite_ptr &mark : marks) mark.set_visible(visible);
    bn::core::update();
  }
}

} // namespace

int main() {
  bn::core::init();

  // 1. Open the session. The slow part, and it happens once.
  cf_config_t cfg;
  cf_config_init(&cfg);
  cfg.api_key = MY_API_KEY;
  cfg.tick_hz = 20; // default cadence: one tick every three frames at 60 fps

  if (!cf_connect(&cfg, while_waiting)) {
    stop(cf_status() == CF_DISCONNECTED ? 1 : 2);
  }

  // 2. Get into a room.
  cf_room_t room;
  cf_room_init(&room);
  room.max_players = 2;

  if (!cf_matchmake(&room, while_waiting)) stop(2);

  // 3. Wait for the match to start. The server does NOT decide this: it picks
  //    a master and leaves the decision to them. Here the master starts as
  //    soon as the room is full.
  {
    bn::sprite_ptr waiting = bn::sprite_items::square.create_sprite(0, 0, 1);
    for (int frame = 0; cf_room_state() == CF_ROOM_WAITING; frame++) {
      cf_poll(nullptr, 0); // keeps the link alive and refreshes the room

      if (cf_status() == CF_FAILED) stop(3);

      if (cf_is_master() && cf_peers() >= cf_capacity()) {
        cf_set_room_state(CF_ROOM_PLAYING);
      }

      waiting.set_visible(((frame / 30) % 2) == 0);
      bn::core::update();
    }
  }

  // 4. Play.
  bn::sprite_ptr me = bn::sprite_items::square.create_sprite(0, 0);
  bn::sprite_ptr them = bn::sprite_items::square.create_sprite(0, 0, 1);
  them.set_visible(false);

  player_t my_pos = {0, 0};

  for (;;) {
    if (bn::keypad::left_held()) my_pos.x -= 2;
    if (bn::keypad::right_held()) my_pos.x += 2;
    if (bn::keypad::up_held()) my_pos.y -= 2;
    if (bn::keypad::down_held()) my_pos.y += 2;

    if (my_pos.x < -112) my_pos.x = -112;
    if (my_pos.x > 112) my_pos.x = 112;
    if (my_pos.y < -72) my_pos.y = -72;
    if (my_pos.y > 72) my_pos.y = 72;

    me.set_position(my_pos.x, my_pos.y);

    // ONCE PER FRAME, ALWAYS. cf_poll() works out internally when to talk to
    // the dongle, so there is no counting to do here.
    player_t received;
    if (cf_poll(&received, sizeof(received)) == sizeof(received)) {
      them.set_position(received.x, received.y);
      them.set_visible(true);
    }

    // May return false if the previous message is still going out. Not an
    // error: it means you are producing faster than the link can carry.
    cf_send(&my_pos, sizeof(my_pos));

    // The link broke and the SDK is getting it back. Pause, but keep calling
    // cf_poll() or the dongle gives up on us while it is reconnecting.
    if (cf_status() == CF_RECONNECTING) {
      bn::core::update();
      continue;
    }
    if (cf_status() != CF_CONNECTED) stop(3);

    bn::core::update();
  }
}
