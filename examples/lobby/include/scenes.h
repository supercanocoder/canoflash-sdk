// ===========================================================================
// The screens.
//
// Each one owns its sprites, blocks until the player is done, and returns what
// happened. `on_frame` is called every frame so the caller can keep the
// network alive — see keep_alive() in main.cpp.
// ===========================================================================

#ifndef LOBBY_SCENES_H
#define LOBBY_SCENES_H

namespace scenes {

enum class result {
  back,     // the player went back
  in_room,  // we are now in a room
  playing,  // the match started
  lost      // the link died
};

/**
 * Create, join by code, or find a game. Needs a session already open.
 *
 * Loops on its own: a cancelled or failed attempt comes back here. It returns
 * `in_room` when the player is in one, or `back` when they press B to leave
 * the session behind.
 */
result rooms_menu(void (*on_frame)());

/** Waiting for players. The master can start. */
result lobby(void (*on_frame)());

/** The match itself. Returns when it ends or the player leaves. */
result match(void (*on_frame)());

} // namespace scenes

#endif
