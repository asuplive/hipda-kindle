#!/usr/bin/env python3
"""Convert any TTF/OTF font into the HUF1 bitmap format the Kindle app uses.

Usage:
    python tools/import_font.py <font.ttf|otf> <output.bin> [--cjk] [--size N] [--label NAME]

HUF1 (v2, high-res) layout:
    "HUF1", u16 version, u8 cell_w, u8 glyph_h, u8 row_bytes, u8 rec_size,
    u32 count, u16 reserved  -> 16-byte header
    per glyph: u32 codepoint + glyph_h * row_bytes bytes (MSB-first rows)

Glyph sizing rules (must match src/font.c):
    * glyph_h must be 16 or 32; row_bytes = glyph_h / 8
    * ASCII cells occupy the LEFT HALF of the bitmap (16 of 32 columns)
      so render ASCII at a size whose advance fits that width
    * CJK cells use the full 32 columns

After generating, copy the .bin into the reader's fonts/ directory. The app
lists every *.bin there in Settings -> 中文字体 / 英文字体.
"""
import argparse
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROWS = 32
ROW_BYTES = 4
GLYPH_BYTES = ROWS * ROW_BYTES      # 128
THRESHOLD = 96

# Codepoints shipped in every font
ASCII_CPS = list(range(0x20, 0x7F))
LATIN_CPS = list(range(0xA0, 0x250))
CJK_PUNCT = list(range(0x2000, 0x207F)) + list(range(0x3000, 0x3040)) + \
            list(range(0xFF01, 0xFF5F))
GB2312_CPS = None                   # populated lazily


def gb2312_codepoints():
    global GB2312_CPS
    if GB2312_CPS is None:
        cps = []
        for row in range(0xA1, 0xF8):
            for col in range(0xA1, 0xFF):
                try:
                    cps.append(ord(bytes([row, col]).decode('gb2312')))
                except UnicodeDecodeError:
                    pass
        GB2312_CPS = cps
    return GB2312_CPS


def render_glyph(font, ch, fullwidth):
    """Render one glyph into a ROWS-row HUF bitmap (MSB-left, ROW_BYTES/row)."""
    W = ROWS                          # full cell width in columns
    img = Image.new('L', (W + 8, ROWS + 8), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 1), ch, font=font, fill=255)
    px = img.load()
    rows = []
    for y in range(ROWS + 8):
        row = 0
        for x in range(W):
            if px[x, y] > THRESHOLD:
                row |= 1 << (W - 1 - x)
        rows.append(row)
    ys = [y for y, v in enumerate(rows) if v]
    if not ys:
        return None
    top, bot = min(ys), max(ys)
    shift = (ROWS - (bot - top + 1)) // 2 - top
    out = bytearray(GLYPH_BYTES)
    for y in range(ROWS):
        v = rows[y - shift] if 0 <= y - shift < len(rows) else 0
        for b in range(ROW_BYTES):
            out[y * ROW_BYTES + b] = (v >> (8 * (ROW_BYTES - 1 - b))) & 0xFF
    return bytes(out)


def font_for_codepoint(path, size_full, size_narrow, fullwidth):
    return ImageFont.truetype(path, size_full if fullwidth else size_narrow)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('font', help='source .ttf/.otf/.ttc file')
    ap.add_argument('output', help='destination .bin (put it in fonts/)')
    ap.add_argument('--cjk', action='store_true',
                    help='include full GB2312 Hanzi set (for Chinese fonts)')
    ap.add_argument('--cjk-only', action='store_true',
                    help='only CJK + punctuation (no ASCII) - for the CN slot')
    ap.add_argument('--ascii-only', action='store_true',
                    help='only ASCII + Latin - for the EN slot')
    ap.add_argument('--size', type=int, default=32,
                    help='fullwidth glyph render size (default 32)')
    ap.add_argument('--ascii-size', type=int, default=29,
                    help='narrow ASCII render size (default 29, fits 16 cols)')
    ap.add_argument('--label', default='', help='font name shown in logs')
    ap.add_argument('--glyph-h', type=int, default=32, choices=(32, 40, 48),
                    help='raster glyph height (default 32; 40/48 = sharper on hi-DPI)')
    args = ap.parse_args()

    if not os.path.isfile(args.font):
        sys.exit('font file not found: ' + args.font)
    global ROWS, ROW_BYTES, GLYPH_BYTES
    ROWS = args.glyph_h
    ROW_BYTES = ROWS // 8
    GLYPH_BYTES = ROWS * ROW_BYTES
    if args.size < ROWS:
        args.size = ROWS
        args.ascii_size = max(args.ascii_size, ROWS * 29 // 32)
    ff = font_for_codepoint(args.font, args.size, args.ascii_size, True)
    fn = font_for_codepoint(args.font, args.size, args.ascii_size, False)

    glyphs = {}
    cps = ASCII_CPS + LATIN_CPS + CJK_PUNCT
    if args.cjk:
        cps += gb2312_codepoints()
    if args.cjk_only:
        cps = CJK_PUNCT + gb2312_codepoints()
    if args.ascii_only:
        cps = ASCII_CPS + LATIN_CPS
    for cp in cps:
        fullwidth = (0x2000 <= cp < 0x207F or 0x3000 <= cp < 0x3040 or
                     0xFF01 <= cp < 0xFF5F or cp > 0x7F)
        try:
            f = ff if fullwidth else fn
            b = render_glyph(f, chr(cp), fullwidth)
        except Exception:
            b = None
        if b and any(b):
            glyphs[cp] = b

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, 'wb') as out:
        out.write(b'HUF1')
        out.write(struct.pack('<HBBBBIH', 1, 16, ROWS, ROW_BYTES,
                              GLYPH_BYTES + 4, len(glyphs), 0))
        for cp in sorted(glyphs):
            out.write(struct.pack('<I', cp))
            out.write(glyphs[cp])
    print('%s -> %s: %d glyphs, %d bytes' %
          (args.label or os.path.basename(args.font), args.output,
           len(glyphs), os.path.getsize(args.output)))


if __name__ == '__main__':
    main()
