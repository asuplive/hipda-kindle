#!/usr/bin/env python3
"""Generate fonts/reading.bin — a HUF1 bitmap font tuned for long e-ink reading.

Design ("好看的搭配"):
  * ASCII 0x20-0x7E : Consolas 8x16  — modern, full-height monospace, crisp
  * CJK (GB2312 L1) : SimSun 16x16   — serif Song, comfortable for long reading
  * everything else : GNU Unifont    — keeps full coverage (symbols, CJK ext...)

Output is the same HUF1 format as fonts/unifont.bin (16 rows, 2 bytes/row,
narrow=left 8 columns, wide=all 16). Runtime picks advance width from the
codepoint via utf8_cells(); the font file itself stays format-compatible.

Input : .cache/unifont.hex  (from tools/fetch_assets.sh)
        C:/Windows/Fonts/simsun.ttc, consola.ttf
Output: fonts/reading.bin
"""
import io, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, '.cache', 'unifont.hex')
DST = os.path.join(ROOT, 'fonts', 'reading.bin')
ROWS, ROW_BYTES, GLYPH_BYTES = 16, 2, 32
THR = 96

SIMSUN = 'C:/Windows/Fonts/simsun.ttc'
CONSOLAS = 'C:/Windows/Fonts/consola.ttf'


def load_unifont():
    g = {}
    with open(SRC, 'rb') as fh:
        for raw in fh:
            line = raw.strip()
            if not line or b':' not in line:
                continue
            cps, hx = line.split(b':', 1)
            if b'-' in cps:
                continue
            try:
                cp = int(cps, 16)
            except ValueError:
                continue
            if cp > 0xFFFF or len(hx) not in (ROWS * 2, ROWS * 4):
                continue
            try:
                raw_b = bytes.fromhex(hx.decode('ascii'))
            except ValueError:
                continue
            if len(raw_b) == ROWS:
                bmp = bytearray(GLYPH_BYTES)
                for r in range(ROWS):
                    bmp[r * ROW_BYTES] = raw_b[r]
            elif len(raw_b) == GLYPH_BYTES:
                bmp = bytearray(raw_b)
            else:
                continue
            g[cp] = bytes(bmp)
    return g


def render(path, ch, wide, size=16, xoff=0):
    """Render one glyph into a 16-row, 2-byte-per-row HUF1 bitmap."""
    f = ImageFont.truetype(path, size=size)
    img = Image.new('L', (20, 24), 0)
    d = ImageDraw.Draw(img)
    d.text((xoff, 1), ch, font=f, fill=255)
    px = img.load()
    cols = 16 if wide else 8
    rows = []
    for y in range(24):
        row = 0
        for x in range(cols):
            if px[x, y] > THR:
                row |= 1 << (cols - 1 - x)
        rows.append(row)
    # Find ink band, then centre it vertically in the 16-row cell.
    ys = [y for y, v in enumerate(rows) if v]
    if not ys:
        return None
    top, bot = min(ys), max(ys)
    h = bot - top + 1
    shift = (ROWS - h) // 2 - top
    out = bytearray(GLYPH_BYTES)
    for y in range(ROWS):
        sy = y - shift
        v = rows[sy] if 0 <= sy < len(rows) else 0
        if wide:
            out[y * 2] = (v >> 8) & 0xFF
            out[y * 2 + 1] = v & 0xFF
        else:
            out[y * 2] = v & 0xFF     # narrow: left byte only
            out[y * 2 + 1] = 0
    return bytes(out)


def main():
    if not os.path.exists(SRC):
        print('missing %s -- run tools/fetch_assets.sh first' % SRC, file=sys.stderr)
        return 1
    if not os.path.exists(SIMSUN) or not os.path.exists(CONSOLAS):
        print('missing SimSun / Consolas fonts', file=sys.stderr)
        return 1

    glyphs = load_unifont()
    print('unifont glyphs:', len(glyphs))

    # 1) ASCII: Consolas (full-height, crisp) instead of Unifont's small caps
    for cp in range(0x20, 0x7F):
        b = render(CONSOLAS, chr(cp), wide=False, size=16)
        if b:
            glyphs[cp] = b
    print('ASCII -> Consolas done')

    # 2) CJK level-1 (GB2312 order): SimSun Song serif, best for reading
    n = 0
    for row in range(0xB0, 0xD8):
        for col in range(0xA1, 0xFF):
            try:
                ch = bytes([row, col]).decode('gb2312')
            except UnicodeDecodeError:
                continue
            b = render(SIMSUN, ch, wide=True, size=16)
            if b:
                glyphs[ord(ch)] = b
                n += 1
    print('CJK -> SimSun done:', n)

    os.makedirs(os.path.dirname(DST), exist_ok=True)
    with open(DST, 'wb') as out:
        out.write(b'HUF1')
        out.write(struct.pack('<HBBBBIH', 1, 8, ROWS, ROW_BYTES,
                              GLYPH_BYTES + 4, len(glyphs), 0))
        for cp in sorted(glyphs):
            out.write(struct.pack('<I', cp))
            out.write(glyphs[cp])
    print('wrote %s: %d glyphs, %d bytes' % (DST, len(glyphs), os.path.getsize(DST)))
    for probe, name in [(0x41, 'A'), (0x67, 'g'), (0x4E2D, '中'), (0x6587, '文')]:
        print('  U+%04X %s %s' % (probe, name, 'present' if probe in glyphs else 'MISSING'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
