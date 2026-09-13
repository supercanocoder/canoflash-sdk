#pragma once
#include <stddef.h>
#include <stdint.h>

// The bundled font has printable ASCII and these Spanish characters. Keep
// supported accents; represent any other UTF-8 character with one '?'. Limit
// visible length so the name plus '(you)', master and away markers fits a row.
inline void lobby_display_name(const char *input, char (&output)[16]) {
  size_t read = 0, written = 0, glyphs = 0;
  while (input && input[read] && written < 15 && glyphs < 12) {
    uint8_t c = (uint8_t)input[read];
    size_t width = c < 0x80 ? 1 : (c >= 0xc2 && c <= 0xdf ? 2 :
                   (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 1)));
    size_t consumed = 1;
    while (consumed < width && input[read + consumed] &&
           ((uint8_t)input[read + consumed] & 0xc0) == 0x80) ++consumed;
    bool supported = c >= 0x20 && c <= 0x7e;
    if (width == 2 && consumed == 2) {
      uint8_t d = (uint8_t)input[read + 1];
      supported = (c == 0xc2 && (d == 0xa1 || d == 0xbf)) ||
                  (c == 0xc3 && (d == 0x81 || d == 0x89 || d == 0x8d ||
                   d == 0x93 || d == 0x9a || d == 0x9c || d == 0x91 ||
                   d == 0xa1 || d == 0xa9 || d == 0xad || d == 0xb3 ||
                   d == 0xba || d == 0xbc || d == 0xb1));
    }
    if (supported) {
      if (written + consumed > 15) break;
      for (size_t i = 0; i < consumed; ++i) output[written++] = input[read + i];
    } else output[written++] = '?';
    read += consumed;
    ++glyphs;
  }
  output[written] = 0;
}
