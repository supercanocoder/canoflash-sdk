#include "ui.h"

#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_optional.h"

#include "common_fixed_8x16_sprite_font.h"

namespace ui {

namespace {

bn::optional<bn::sprite_text_generator> s_text;

constexpr int LINE = 16;   // font height

} // namespace

void hand_over(void (*on_frame)()) {
  if (on_frame) on_frame();
  bn::core::update();
}

void init() {
  s_text = bn::sprite_text_generator(common::fixed_8x16_sprite_font);
  s_text->set_center_alignment();
}

bn::sprite_text_generator &text() { return *s_text; }

void centred(sprites &out, int y, const bn::string_view &line) {
  s_text->set_center_alignment();
  s_text->generate(0, y, line, out);
}

void left(sprites &out, int x, int y, const bn::string_view &line) {
  s_text->set_left_alignment();
  s_text->generate(x, y, line, out);
  s_text->set_center_alignment();
}

int menu(const bn::string_view &title, const bn::string_view *entries,
         int count, void (*on_frame)(), int first_y) {
  sprites fixed;
  if (!title.empty()) centred(fixed, first_y - 48, title);
  if (!title.empty()) centred(fixed, 52, "A select     B back");

  sprites items;
  for (int i = 0; i < count; i++) {
    centred(items, first_y + i * LINE, entries[i]);
  }

  sprites cursor;
  int selected = 0;
  int drawn_selection = -1;

  for (;;) {
    // Use the same black font for the arrow and labels. Measure glyphs, so
    // accented labels keep the cursor in the right place too.
    if (selected != drawn_selection) {
      cursor.clear();
      int half = s_text->width(entries[selected]) / 2;
      left(cursor, -half - 16, first_y + selected * LINE, ">");
      drawn_selection = selected;
    }

    if (bn::keypad::down_pressed()) {
      selected = (selected + 1) % count;
    } else if (bn::keypad::up_pressed()) {
      selected = (selected + count - 1) % count;
    } else if (bn::keypad::a_pressed()) {
      hand_over(on_frame);
      return selected;
    } else if (bn::keypad::b_pressed()) {
      hand_over(on_frame);
      return -1;
    }

    // The game keeps talking to the server while the player reads. Without
    // this the dongle powers itself down after about sixty seconds without GBA contact.
    if (on_frame) on_frame();
    bn::core::update();
  }
}

void message(const bn::string_view &line1, const bn::string_view &line2,
             void (*on_frame)()) {
  sprites text_sprites;
  centred(text_sprites, -8, line1);
  if (!line2.empty()) centred(text_sprites, 8, line2);
  centred(text_sprites, 48, "A continue");

  for (;;) {
    if (bn::keypad::a_pressed() || bn::keypad::b_pressed()) {
      hand_over(on_frame);
      return;
    }
    if (on_frame) on_frame();
    bn::core::update();
  }
}

} // namespace ui
