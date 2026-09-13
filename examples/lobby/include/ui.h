// ===========================================================================
// Shared bits of interface.
//
// Deliberately small: this example is about the SDK, not about building a UI
// toolkit. Everything here is text, a cursor and a couple of boxes.
// ===========================================================================

#ifndef LOBBY_UI_H
#define LOBBY_UI_H

#include "bn_string_view.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_vector.h"

namespace ui {

/**
 * Hands the frame on before a screen leaves.
 *
 * A screen that returns the instant A goes down leaves the button still down as
 * far as Butano is concerned: the keypad only advances inside bn::core::update().
 * The next screen opens on that same frame, sees the same press and takes it as
 * its own — three menus flash past on one tap.
 *
 * Call it after acting on a key and before returning. Every screen here does.
 */
void hand_over(void (*on_frame)());

/** Sets up the text generator. Call once at startup. */
void init();

bn::sprite_text_generator &text();

/**
 * Sprites for one screenful of text.
 *
 * 64 is generous for what this example draws and keeps the type simple. A
 * lobby of four players with a title and three options is around 30.
 */
using sprites = bn::vector<bn::sprite_ptr, 64>;

/** Writes centred text at `y`, appending the sprites to `out`. */
void centred(sprites &out, int y, const bn::string_view &line);

/** Writes left-aligned text at `x, y`. */
void left(sprites &out, int x, int y, const bn::string_view &line);

/**
 * A menu the player moves through with up/down.
 *
 * Returns the chosen entry, or -1 if they pressed B. Blocks until then,
 * calling `on_frame` every frame — which is how the caller keeps the network
 * alive while the player reads the screen.
 *
 * `first_y` is where the first entry sits; the title goes above it. Pass an
 * empty title when the screen already says what it is, as the logo does.
 */
int menu(const bn::string_view &title, const bn::string_view *entries,
         int count, void (*on_frame)(), int first_y = -16);

/** A message with an OK prompt. Blocks until A or B. */
void message(const bn::string_view &line1, const bn::string_view &line2,
             void (*on_frame)());

} // namespace ui

#endif
