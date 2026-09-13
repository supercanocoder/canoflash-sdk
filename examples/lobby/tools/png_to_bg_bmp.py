#!/usr/bin/env python3
"""
Turn a screen-sized image into a background BMP that Butano will accept.

    python3 tools/png_to_bg_bmp.py /path/to/artwork.png
    python3 tools/png_to_bg_bmp.py graphics/logo.bmp --transparent FFFFFE --colors 16

Butano's importer is strict about backgrounds, and for good reason -- the
hardware is:

  * The file must be an uncompressed BMP with a 40-byte header. No PNG.
  * A regular background is 256x256 (or 512 wide, or 512 tall). The screen is
    240x160, and Butano puts the background's CENTRE at the screen's centre --
    so a 240x160 drawing has to be padded around all four sides, not off one
    corner. Pad it top-left and the top 48 rows of the drawing fall off the
    screen while 48 rows of padding march in at the bottom.
  * Palette entry 0 is the transparent colour. If your artwork happens to land
    a real colour there, that colour disappears -- and a single-colour image
    becomes an entirely invisible background.

This script handles all three: it pads the image, and it reserves entry 0 for a
colour the artwork does not use, so nothing of yours is ever transparent.

  --transparent RRGGBB[,RRGGBB...]
                        the opposite: put THESE colours at entry 0, so that
                        wherever the artwork is one of them, whatever is behind
                        shows through. For a logo drawn on a flat field. Pass
                        every colour that means "nothing" — a field the artist
                        filled in white and padding they filled in magenta are
                        the same thing to the hardware.

  --colors N            reduce the palette to N entries. Two backgrounds on
                        screen at once must share the 256 the hardware has, so
                        a 256-colour background leaves room for nothing else.
                        16 each is what lets two coexist.

Needs ImageMagick (`magick`) for the image decoding, and nothing else.
"""

import os
import struct
import subprocess
import sys

BG_SIZE = 256          # what a Butano regular background must measure
SCREEN_W, SCREEN_H = 240, 160
MAX_COLORS = 255       # 256 palette entries, minus the reserved one


def load_rgb(path):
    """The image as a flat list of (r, g, b), via ImageMagick."""
    out = subprocess.run(['magick', path, '-depth', '8', 'RGB:-'],
                         check=True, capture_output=True).stdout
    size = subprocess.run(['magick', path, '-format', '%w %h', 'info:'],
                          check=True, capture_output=True).stdout.split()
    width, height = int(size[0]), int(size[1])

    if len(out) != width * height * 3:
        raise SystemExit(f'{path}: unexpected pixel data')

    pixels = [tuple(out[i:i + 3]) for i in range(0, len(out), 3)]
    return width, height, pixels


def quantize(pixels, count):
    """The `count` colours that best cover `pixels`, weighted by how common they are.

    ImageMagick does the choosing, on a strip holding every pixel that matters,
    so a colour used once counts once and the sky counts thousands of times.
    """
    strip = b'P6\n' + f'{len(pixels)} 1\n255\n'.encode()
    strip += b''.join(bytes(pixel) for pixel in pixels)
    out = subprocess.run(['magick', 'ppm:-', '-colors', str(count), '-depth', '8',
                          'RGB:-'], input=strip, check=True,
                         capture_output=True).stdout

    chosen = []
    for i in range(0, len(out), 3):
        colour = tuple(out[i:i + 3])
        if colour not in chosen:
            chosen.append(colour)

    return chosen


def nearest(colour, palette):
    r, g, b = colour
    return min(range(len(palette)),
               key=lambda i: (r - palette[i][0]) ** 2 + (g - palette[i][1]) ** 2
                             + (b - palette[i][2]) ** 2)


def build_palette(pixels, transparent, limit):
    """Palette with entry 0 reserved, and a colour -> index map."""
    opaque = [pixel for pixel in pixels if pixel not in transparent]
    unique = []
    seen = set()

    for pixel in opaque:
        if pixel not in seen:
            seen.add(pixel)
            unique.append(pixel)

    room = min(limit, MAX_COLORS + 1) - 1   # entry 0 is spoken for

    if len(unique) > room:
        # Chosen from the artwork alone: the transparent field is often most of
        # the image, and letting it vote would spend the palette on it.
        unique = quantize(opaque, room)
        index_of = {}
        for colour in seen:
            index_of[colour] = nearest(colour, unique) + 1
        print(f'{len(seen)} colours reduced to {len(unique)}')
    else:
        index_of = {colour: i + 1 for i, colour in enumerate(unique)}

    if transparent:
        entry0 = next(iter(transparent))
    else:
        # Entry 0 is transparent, so it must be a colour that appears nowhere.
        # Magenta unless the artwork uses it; anything unused would do.
        entry0 = (255, 0, 255)
        while entry0 in seen:
            entry0 = (entry0[0], entry0[1] + 1, entry0[2])

    palette = [entry0] + unique
    for colour in transparent:
        index_of[colour] = 0

    return palette, index_of


def write_bmp(path, palette, indices):
    """An 8bpp BMP, uncompressed, bottom-up: exactly what Butano reads."""
    pixels_offset = 14 + 40 + 256 * 4
    body = bytearray()

    for y in range(BG_SIZE - 1, -1, -1):     # BMP rows run bottom to top
        body += bytes(indices[y * BG_SIZE:(y + 1) * BG_SIZE])

    header = struct.pack('<2sIHHI', b'BM', pixels_offset + len(body), 0, 0,
                         pixels_offset)
    dib = struct.pack('<IiiHHIIiiII', 40, BG_SIZE, BG_SIZE, 1, 8, 0, len(body),
                      2835, 2835, 256, 256)
    colours = bytearray()

    for i in range(256):
        r, g, b = palette[i] if i < len(palette) else (0, 0, 0)
        colours += bytes((b, g, r, 0))

    with open(path, 'wb') as bmp:
        bmp.write(header + dib + colours + body)


def main():
    args = sys.argv[1:]
    transparent = set()
    limit = 256

    while len(args) > 1:
        if args[-2] == '--transparent':
            for field in args[-1].split(','):
                transparent.add(tuple(int(field[i:i + 2], 16) for i in (0, 2, 4)))
        elif args[-2] == '--colors':
            limit = int(args[-1])
        else:
            break
        args = args[:-2]

    if len(args) != 1:
        raise SystemExit(__doc__)

    source = args[0]
    width, height, pixels = load_rgb(source)

    if width > BG_SIZE or height > BG_SIZE:
        raise SystemExit(f'{source} is {width}x{height}; the most a regular '
                         f'background holds is {BG_SIZE}x{BG_SIZE}.')

    if width < SCREEN_W or height < SCREEN_H:
        print(f'warning: {width}x{height} does not fill the {SCREEN_W}x'
              f'{SCREEN_H} screen')

    palette, index_of = build_palette(pixels, transparent, limit)

    # CENTRED, not tucked into a corner. Butano lines the background's centre up
    # with the screen's, so a 240x160 drawing padded at the top-left would show
    # its middle band and none of its top.
    left = (BG_SIZE - width) // 2
    top = (BG_SIZE - height) // 2

    # The padding is off-screen either way. It copies the corner pixel rather
    # than reaching for entry 0, which for a background with no transparent
    # colour keeps entry 0 unused.
    filler = index_of[pixels[0]]
    indices = [filler] * (BG_SIZE * BG_SIZE)

    for y in range(height):
        for x in range(width):
            indices[(y + top) * BG_SIZE + x + left] = index_of[pixels[y * width + x]]

    target = os.path.splitext(source)[0] + '.bmp'
    write_bmp(target, palette, indices)
    print(f'{source} ({width}x{height}) -> {target} ({BG_SIZE}x{BG_SIZE}, '
          f'{len(palette)} palette entries'
          f'{", entry 0 transparent" if transparent else ""})')


if __name__ == '__main__':
    main()
