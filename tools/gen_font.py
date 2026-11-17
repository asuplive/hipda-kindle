#!/usr/bin/env python3
"""Generate fonts/unifont.bin from GNU Unifont .hex data.

Input  : .cache/unifont.hex  (Unifont 13.0.04, shipped in NiLuJe/FBInk/fonts)
Output : fonts/unifont.bin   (HUF1 sparse bitmap font, sorted by codepoint)

HUF1 layout (all little-endian):
    offset 0   : magic       4 bytes  "HUF1"
    offset 4   : version     uint16   1
    offset 6   : cell_w      uint8    8      (width of one monospace cell)
    offset 7   : glyph_h     uint8    16     (rows per glyph)
    offset 8   : row_bytes   uint8    2      (bytes per row, 16 bits of pixels)
    offset 9   : rec_size    uint8    36     (4-byte codepoint + 32-byte bitmap)
    offset 10  : count       uint32
    offset 14  : reserved    uint16
    then count records: uint32 codepoint + 32 bytes bitmap
                        (row-major, MSB is the leftmost pixel)

Wide (CJK/fullwidth) glyphs use all 16 columns of the bitmap; narrow glyphs
only use the leftmost 8 columns. The renderer decides the advance width from
the codepoint via wide_char(), not from the bitmap.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, ".cache", "unifont.hex")
DST = os.path.join(ROOT, "fonts", "unifont.bin")
GLYPH_BYTES = 32
ROW_BYTES = 2
ROWS = 16


def main() -> int:
    if not os.path.exists(SRC):
        print("missing %s -- run tools/fetch_assets.sh first" % SRC, file=sys.stderr)
        return 1

    glyphs = {}
    with open(SRC, "rb") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or b":" not in line:
                continue
            cps, hexbits = line.split(b":", 1)
            # Unifont .hex may contain ranges ("xxxx-yyyy"); skip those, they
            # do not occur in the BMP build we use.
            if b"-" in cps:
                continue
            try:
                cp = int(cps, 16)
            except ValueError:
                continue
            if cp > 0xFFFF or len(hexbits) not in (ROWS * 2, ROWS * 4):
                continue
            try:
                raw = bytes.fromhex(hexbits.decode("ascii"))
            except ValueError:
                continue
            # Narrow glyphs are 8x16 (16 one-byte rows); widen them into the
            # 16x16 storage by interleaving an empty right-hand byte.
            if len(raw) == ROWS:
                bitmap = bytearray(GLYPH_BYTES)
                for row in range(ROWS):
                    bitmap[row * ROW_BYTES] = raw[row]
            elif len(raw) == GLYPH_BYTES:
                bitmap = bytearray(raw)
            else:
                continue
            glyphs[cp] = bytes(bitmap)

    if len(glyphs) < 10000:
        print("suspiciously few glyphs: %d" % len(glyphs), file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(DST), exist_ok=True)
    with open(DST, "wb") as out:
        out.write(b"HUF1")
        out.write(struct.pack("<HBBBBIH", 1, 8, ROWS, ROW_BYTES,
                              GLYPH_BYTES + 4, len(glyphs), 0))
        for cp in sorted(glyphs):
            out.write(struct.pack("<I", cp))
            out.write(glyphs[cp])

    size = os.path.getsize(DST)
    print("wrote %s: %d glyphs, %d bytes" % (DST, len(glyphs), size))
    for probe in (0x0041, 0x4E2D, 0x6587, 0xFF21):
        bmp = glyphs.get(probe)
        print("  U+%04X %s" % (probe, "present" if bmp else "MISSING"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
