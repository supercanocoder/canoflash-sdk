// ===========================================================================
// CanoFlash Net — the full example
//
// Room management with an interface: create a room, join one by its
// code, or let matchmaking find you a game. Then a lobby with the players'
// names, and a match you can leave and come back from.
//
// If you are reading the SDK for the first time, start with examples/hello_world
// instead. That one is thirty lines of network code with nothing around it.
// This one shows how those calls fit into a real game, which means most of what
// follows is menus.
//
// THE SHAPE OF IT
//
//   title screen ── "Connect to CanoFlash" ──▶ cf_connect()   the session opening wait
//                                                   │
//     ┌─────────────────────────────────────────────┘
//     │
//     ├── cf_create_room()      a new room; shows its six-digit code
//     ├── cf_join_code()        someone else's room, by code
//     └── cf_matchmake()        whoever is waiting
//           │
//           ▼
//        lobby ── the master starts ──▶ match ── B ──▶ back to the rooms menu
//
// Nothing touches the dongle until the player asks it to. That is worth doing
// in your own game too: a ROM that reaches for the hardware before the title
// screen has drawn looks broken when there is no dongle to find, and gives the
// player nothing to press to try again.
//
// Once open, the session stays open. Rooms come and go inside it, which is what
// makes the rooms menu possible: leaving a room does not cost another four
// seconds of handshake.
//
// Licensed under the MIT License. See LICENSE at the repository root.
// The bundled font is Butano's, zlib licensed — see include/.
// ===========================================================================

#include "bn_core.h"
#include "bn_bg_palettes.h"
#include "bn_keypad.h"
#include "bn_string.h"
#include "bn_regular_bg_ptr.h"
#include "bn_regular_bg_items_logo.h"

#include "canoflash.h"
#include "../../canoflash_example_config.h"
#include "ui.h"
#include "scenes.h"

namespace {

// Configure examples/canoflash_example_config.local.h; see examples/README.md.
constexpr const char *API_KEY = CF_EXAMPLE_API_KEY;

/**
 * Called every frame from inside menus and message boxes.
 *
 * This is the part worth copying into your own game. A menu is not a pause:
 * the dongle powers itself down after about sixty seconds without ticks, so a
 * player reading your options would lose the connection without touching
 * anything. One call per frame prevents that.
 */
void keep_alive() {
  cf_poll(nullptr, 0);

  // THE LINK CAN BREAK WHILE THE PLAYER IS READING A MENU, and if nothing says
  // so, the next thing they press fails for a reason that looks like the game's
  // fault. So the moment the SDK says it is putting the link back together, we
  // say so and wait.
  //
  // Taking over the screen takes over the input too, and that is the point: the
  // menu underneath is not reading the keypad while this loop runs, so there is
  // nothing to press and nothing to fail. It comes back exactly where it was.
  //
  // Down at the bottom, out of the way of whatever is already drawn. Every
  // screen in the example goes through this one function, so all of them get it.
  if (cf_status() != CF_RECONNECTING) return;

  ui::sprites notice;
  ui::centred(notice, 68, "Reconnecting...");

  while (cf_status() == CF_RECONNECTING) {
    cf_poll(nullptr, 0);
    bn::core::update();
  }
}

/** Opens the session. The title screen is still on show while it works. */
bool connect_now() {
  ui::sprites screen;
  ui::centred(screen, 44, "Connecting...");

  cf_config_t cfg;
  cf_config_init(&cfg);
  cfg.api_key = API_KEY;
  cfg.tick_hz = 20; // default cadence: one tick every three frames at 60 fps

  // The callback keeps the console alive while the handshake runs. Without it
  // the screen stops advancing during the connection wait.
  return cf_connect(&cfg, [](int frames) {
    for (int f = 0; f < frames; f++) bn::core::update();
  });
}

/**
 * The title screen. Returns once there is a session.
 *
 * No on_frame anywhere in here, and that is not an oversight: there is nothing
 * to keep alive yet. Ticks only matter once cf_connect() has succeeded.
 */
void title_screen() {
  // Same artwork and placement as the multiboot welcome screen. Its 240x160
  // canvas occupies the top-left of a 256x256 bitmap; (8, 48) aligns it with
  // the display. Transparent padding shows the white backdrop.
  bn::regular_bg_ptr logo = bn::regular_bg_items::logo.create_bg(8, 48);
  logo.set_priority(3);
  logo.set_z_order(0);

  static const bn::string_view entries[] = {"Connect to CanoFlash"};

  for (;;) {
    logo.set_visible(true);
    // No title: the logo is the title. Down low, so the artwork stays clear.
    // B does nothing here either — there is nowhere further back to go.
    if (ui::menu("", entries, 1, nullptr, 44) < 0) continue;

    if (connect_now()) return;

    // Give errors a clear white screen instead of drawing through the logo.
    logo.set_visible(false);
    // Two failures, and the difference is the only useful thing we can tell
    // the player: one they can fix, the other they cannot.
    if (cf_status() == CF_DISCONNECTED) {
      ui::message("No CanoFlash found", "Turn on your dongle", nullptr);
    } else {
      ui::message("Could not connect", "Check Wi-Fi and account", nullptr);
    }
  }
}

/** Rooms, lobbies and matches, for as long as the session lasts. */
void play_session() {
  for (;;) {
    scenes::result chosen = scenes::rooms_menu(keep_alive);
    if (chosen == scenes::result::back) return;
    if (chosen == scenes::result::lost) {
      ui::message("Connection lost", "", nullptr);
      return;
    }

    // In a room. The lobby returns when the match starts, or when the player
    // backs out.
    if (scenes::lobby(keep_alive) == scenes::result::playing) {
      scenes::match(keep_alive);
    }

    // Whatever happened — the match ended, the player quit, the link died —
    // we go back to the rooms menu.
    if (cf_in_room()) cf_leave_room();

    if (cf_status() == CF_FAILED) {
      ui::message("Connection lost", "", nullptr);
      return;
    }
  }
}

} // namespace

int main() {
  bn::core::init();
  ui::init();

  // Match the multiboot: a plain white backdrop and black text. The font's
  // white edge blends into this backdrop, leaving no visible shadow.
  bn::bg_palettes::set_transparent_color(bn::color(31, 31, 31));

  for (;;) {
    title_screen(); // returns with a session open
    play_session();

    // Back to the title screen, and the dongle is released rather than left
    // holding a session nobody is using. It powers itself down.
    cf_disconnect();
  }
}
