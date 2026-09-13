// ===========================================================================
// The match.
//
// Deliberately trivial —a square you move— because the point of this example
// is everything around it. What matters here is the shape of the loop:
// cf_poll() once per frame, cf_send() once per frame, and a check on the link.
// ===========================================================================

#include "scenes.h"

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_string.h"
#include "bn_sprite_ptr.h"
#include "bn_vector.h"
#include "bn_sprite_items_selector.h"

#include "canoflash.h"
#include "ui.h"

namespace scenes {

namespace {

/** What travels. Small and fixed-size, which is what this channel likes. */
struct player_state {
  int16_t x;
  int16_t y;
};

/** The pause menu. Returns true if the player wants out. */
bool paused(void (*on_frame)()) {
  static const bn::string_view entries[] = {"Resume", "Leave the match"};
  int chosen = ui::menu("PAUSED", entries, 2, on_frame);
  return chosen == 1;
}

} // namespace

result match(void (*on_frame)()) {
  bn::sprite_ptr me = bn::sprite_items::selector.create_sprite(0, 0);

  // One sprite per other seat. Created hidden and shown when something arrives
  // from that slot, so nothing appears in the corner before it should.
  bn::vector<bn::sprite_ptr, 3> others;
  for (int i = 0; i < 3; i++) {
    bn::sprite_ptr s = bn::sprite_items::selector.create_sprite(0, 0);
    s.set_visible(false);
    others.push_back(s);
  }

  player_state mine = {0, 0};

  // Which sprite belongs to which slot. Our own seat is skipped.
  int sprite_for_slot[8];
  for (int i = 0, next = 0; i < 8; i++) {
    sprite_for_slot[i] = (i == cf_slot() || next >= 3) ? -1 : next++;
  }

  ui::sprites hud;
  ui::left(hud, -110, -70, "START pause");

  // THE MATCH STEPS ASIDE while another screen is up. A menu drawn over the
  // players and the HUD is unreadable, and the GBA has no window to put one in
  // — sprites simply share the screen with whatever else is on it. So the match
  // hides itself and comes back exactly as it was, which for the other seats
  // means remembering which of them had appeared: showing them all would put
  // squares on the field for players who have not sent anything yet.
  bn::vector<bool, 3> was_shown;

  auto step_aside = [&]() {
    was_shown.clear();
    for (bn::sprite_ptr &other : others) {
      was_shown.push_back(other.visible());
      other.set_visible(false);
    }
    me.set_visible(false);
    for (bn::sprite_ptr &sprite : hud) sprite.set_visible(false);
  };

  auto step_back = [&]() {
    for (int i = 0; i < 3; i++) others[i].set_visible(was_shown[i]);
    me.set_visible(true);
    for (bn::sprite_ptr &sprite : hud) sprite.set_visible(true);
  };

  for (;;) {
    if (bn::keypad::left_held()) mine.x -= 2;
    if (bn::keypad::right_held()) mine.x += 2;
    if (bn::keypad::up_held()) mine.y -= 2;
    if (bn::keypad::down_held()) mine.y += 2;

    if (mine.x < -112) mine.x = -112;
    if (mine.x > 112) mine.x = 112;
    if (mine.y < -60) mine.y = -60;
    if (mine.y > 72) mine.y = 72;

    me.set_position(mine.x, mine.y);

    // ONCE PER FRAME, ALWAYS. cf_poll() works out internally when to talk to
    // the dongle; there is no counting to do here.
    player_state got;
    if (cf_poll(&got, sizeof(got)) == sizeof(got) && cf_last_sender() < 8) {
      int which = sprite_for_slot[cf_last_sender()];
      if (which >= 0) {
        others[which].set_position(got.x, got.y);
        others[which].set_visible(true);
      }
    }

    // May return false while the previous message is still going out. Not an
    // error: it means we are producing faster than the link can carry.
    cf_send(&mine, sizeof(mine));

    // The link broke and the SDK is getting it back. Pause, but keep calling
    // cf_poll() or the dongle gives up on us while it is reconnecting.
    if (cf_status() == CF_RECONNECTING) {
      step_aside();
      ui::sprites notice;
      ui::centred(notice, 0, "Reconnecting...");
      while (cf_status() == CF_RECONNECTING) {
        if (on_frame) on_frame();
        bn::core::update();
      }
      step_back();
      continue;
    }

    if (cf_status() != CF_CONNECTED) return result::lost;

    if (bn::keypad::start_pressed()) {
      ui::hand_over(on_frame);
      step_aside();
      bool leaving = paused(on_frame);
      step_back();
      if (leaving) return result::back;
    }

    bn::core::update();
  }
}

} // namespace scenes
