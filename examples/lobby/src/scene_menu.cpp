// ===========================================================================
// The rooms menu, and the two screens behind it.
//
// This is where the three ways into a room live, and they are genuinely
// different — see docs/rooms.md. The menu exists to make that difference
// visible to the player.
// ===========================================================================

#include "scenes.h"

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_string.h"

#include "canoflash.h"
#include "ui.h"

namespace scenes {

namespace {

/** Room parameters. Keeps game modes apart in matchmaking. */
constexpr const char *PARAMS = "demo";

/**
 * Waits while a join is in flight, drawing something that moves.
 *
 * Joining is fast compared with connecting —the session is already open— but
 * it is still a round trip to the server, and a frozen screen for half a
 * second reads as a crash.
 */
void joining_frames(int frames) {
  for (int f = 0; f < frames; f++) bn::core::update();
}

/**
 * Turns a failed join into something the player can act on.
 *
 * All three ways into a room come through here. Only one of them used to, and
 * the other two said "could not" — which is the same as saying nothing, and
 * cost real time working out which of five different things had happened.
 */
void explain_failure(void (*on_frame)()) {
  switch (cf_join_error()) {
  case CF_JOIN_NO_SUCH_ROOM:
    ui::message("No room with that code", "Check the digits", on_frame);
    break;
  case CF_JOIN_FULL:
    ui::message("That room is full", "", on_frame);
    break;
  case CF_JOIN_PLAYING:
    ui::message("They already started", "Try another room", on_frame);
    break;
  case CF_JOIN_NO_SESSION:
    ui::message("Not connected", "Go back and connect", on_frame);
    break;
  case CF_JOIN_NO_LINK:
    // Nothing to do with the room: the request never reached the dongle.
    ui::message("Lost the CanoFlash", "Check the cable", on_frame);
    break;
  case CF_JOIN_BUSY:
    ui::message("Room change in progress", "Try again in a moment", on_frame);
    break;
  case CF_JOIN_TIMEOUT:
    ui::message("Room request timed out", "Try again", on_frame);
    break;
  default:
    ui::message("Could not join", "", on_frame);
    break;
  }
}

/**
 * Asks for the six digits.
 *
 * Up and down change the digit under the cursor, left and right move between
 * them. It is the only text entry a Game Boy can offer without being painful,
 * which is exactly why room codes are six digits and not names.
 */
bool ask_for_code(char *out, void (*on_frame)()) {
  int digits[6] = {0, 0, 0, 0, 0, 0};
  int at = 0;

  for (;;) {
    ui::sprites screen;
    ui::centred(screen, -48, "ROOM CODE");
    ui::centred(screen, -20, "UP/DOWN change  LR move");

    // The digits, spaced out, with the current one marked underneath.
    bn::string<16> shown;
    for (int i = 0; i < 6; i++) {
      shown.append(char('0' + digits[i]));
      if (i < 5) shown.append(' ');
    }
    ui::centred(screen, 8, shown);

    bn::string<16> marker;
    for (int i = 0; i < at * 2; i++) marker.append(' ');
    marker.append('^');
    ui::left(screen, -44, 24, marker);

    ui::centred(screen, 52, "A join     B back");

    if (bn::keypad::up_pressed()) {
      digits[at] = (digits[at] + 1) % 10;
    } else if (bn::keypad::down_pressed()) {
      digits[at] = (digits[at] + 9) % 10;
    } else if (bn::keypad::right_pressed()) {
      at = (at + 1) % 6;
    } else if (bn::keypad::left_pressed()) {
      at = (at + 5) % 6;
    } else if (bn::keypad::a_pressed()) {
      for (int i = 0; i < 6; i++) out[i] = char('0' + digits[i]);
      out[6] = '\0';
      ui::hand_over(on_frame);
      return true;
    } else if (bn::keypad::b_pressed()) {
      ui::hand_over(on_frame);
      return false;
    }

    if (on_frame) on_frame();
    bn::core::update();
  }
}

/** Number of players and whether strangers can find the room. */
bool ask_room_options(cf_room_t *room, void (*on_frame)()) {
  static const bn::string_view players[] = {"2 players", "3 players", "4 players"};
  int chosen = ui::menu("HOW MANY?", players, 3, on_frame);
  if (chosen < 0) return false;
  room->max_players = uint8_t(2 + chosen);

  static const bn::string_view kinds[] = {"Public: anyone can join",
                                          "Private: code only"};
  int kind = ui::menu("WHAT KIND?", kinds, 2, on_frame);
  if (kind < 0) return false;
  room->visibility = (kind == 0) ? CF_PUBLIC : CF_PRIVATE;

  return true;
}

} // namespace

result rooms_menu(void (*on_frame)()) {
  static const bn::string_view entries[] = {
      "Create a room",
      "Join with a code",
      "Find a game",
  };

  // Loops, so that changing your mind or a room that would not have you both
  // land back on this menu rather than somewhere the player did not ask for.
  for (;;) {
    int chosen = ui::menu("PLAY", entries, 3, on_frame);
    if (chosen < 0) return result::back;

    // The link gave up while they were choosing. Everything below would fail
    // one call at a time, each with its own unhelpful screen.
    if (cf_status() == CF_FAILED) return result::lost;

    cf_room_t room;
    cf_room_init(&room);
    room.params = PARAMS;

    switch (chosen) {
    case 0:
      // Create. Always makes a NEW room, even if free ones are sitting empty:
      // someone about to play with a specific friend wants their own.
      if (!ask_room_options(&room, on_frame)) break;
      if (!cf_create_room(&room, joining_frames)) {
        explain_failure(on_frame);
        break;
      }
      return result::in_room;

    case 1: {
      // Join by code. Never creates one — a room made because someone mistyped
      // a digit leaves them waiting for a player who is never coming.
      char code[8];
      if (!ask_for_code(code, on_frame)) break;
      if (!cf_join_code(code, joining_frames)) {
        explain_failure(on_frame);
        break;
      }
      return result::in_room;
    }

    default:
      // Matchmaking. Public rooms only, and if there are none it starts one and
      // waits — being the first player of the day is not an error.
      room.max_players = 2;
      if (!cf_matchmake(&room, joining_frames)) {
        explain_failure(on_frame);
        break;
      }
      return result::in_room;
    }
  }
}

} // namespace scenes
