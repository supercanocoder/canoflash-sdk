// ===========================================================================
// The lobby: who is here, and who can start.
//
// The interesting part is that the SERVER does not decide when the match
// begins. It has never seen this game and does not know what "ready" means in
// it. It picks a master —whoever arrived first— and leaves the decision there.
// ===========================================================================

#include "scenes.h"

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_string.h"

#include "canoflash.h"
#include "ui.h"
#include "lobby_name.h"
#include <cstring>

namespace scenes {

result lobby(void (*on_frame)()) {
  // Redrawn only when something changes. Rebuilding thirty sprites every frame
  // would cost more than the whole network layer.
  uint8_t last_mask = 0xFF;
  uint8_t last_away = 0xFF;
  uint8_t last_master = 0xFE;
  uint8_t last_capacity = 0;
  char last_names[4][16] = {};
  char last_code[8] = {};
  ui::sprites screen;

  for (;;) {
    if (on_frame) on_frame();

    if (cf_status() == CF_FAILED) return result::lost;

    // The master started it. Everyone else finds out here.
    if (cf_room_state() == CF_ROOM_PLAYING) return result::playing;

    uint8_t mask = cf_peer_mask();
    uint8_t away = cf_away_mask();
    uint8_t master = cf_master_slot();

    char names[4][16] = {};
    bool names_changed = false;
    for (uint8_t slot = 0; slot < 4; ++slot) {
      lobby_display_name(cf_player_name(slot), names[slot]);
      if (strcmp(names[slot], last_names[slot])) names_changed = true;
    }
    if (mask != last_mask || away != last_away || master != last_master ||
        cf_capacity() != last_capacity || names_changed || strcmp(last_code, cf_room_code())) {
      last_mask = mask;
      last_away = away;
      last_master = master;
      last_capacity = cf_capacity();
      memcpy(last_names, names, sizeof(last_names));
      strncpy(last_code, cf_room_code(), sizeof(last_code) - 1);

      screen.clear();

      // The code, big and alone: it is what the player reads out over the
      // phone, so it should be the easiest thing on the screen to find.
      bn::string<24> heading("ROOM ");
      heading.append(cf_room_code());
      ui::centred(screen, -60, heading);

      bn::string<24> count;
      count.append(char('0' + cf_peers()));
      count.append(" / ");
      count.append(char('0' + cf_capacity()));
      ui::centred(screen, -42, count);

      // One line per seat, so an empty one is as visible as a taken one.
      for (int slot = 0; slot < cf_capacity() && slot < 4; slot++) {
        bn::string<32> line;

        if (mask & (1 << slot)) {
          const char *name = names[slot];
          line.append(name[0] ? name : "player");

          if (slot == cf_slot()) line.append(" (you)");
          if (slot == master) line.append(" *");

          // "Away" is not "gone": their seat is held for about thirty seconds
          // while they try to get back, and saying so stops the others giving
          // up on them.
          if (away & (1 << slot)) line.append(" ...");
        } else {
          line.append("- empty -");
        }

        ui::left(screen, -80, -16 + slot * 16, line);
      }

      if (cf_is_master()) {
        ui::centred(screen, 48, "A start     B leave");
      } else {
        ui::centred(screen, 48, "Waiting     B leave");
      }
    }

    // Only the master can start, and starting with an empty seat is allowed on
    // purpose: three friends should not have to wait for a fourth who is not
    // coming.
    if (cf_is_master() && bn::keypad::a_pressed() && cf_peers() >= 2) {
      cf_set_room_state(CF_ROOM_PLAYING);
      // Not returning yet: the change is not real until the server confirms
      // it, and the loop above is what notices. Assuming it worked would start
      // a match the others never joined.
    }

    if (bn::keypad::b_pressed()) {
      ui::hand_over(on_frame);
      return result::back;
    }

    bn::core::update();
  }
}

} // namespace scenes
