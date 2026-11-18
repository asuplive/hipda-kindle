#!/usr/bin/env python3
"""Generate fonts/reading.bin — a HIGH-RES (32px) HUF bitmap font.

Matched to the SVG mockups: every glyph is rendered at 32px from real TTFs,
so the runtime can scale to any target height (fs * w/600) with near-zero
quality loss.

  * ASCII 0x20-0x7E : Consolas 32px   (16 x 32, crisp monospace)
  * CJK (GB2312 L1+L2): SimSun 32px   (32 x 32, serif Song for long reading)
  * other symbols   : Microsoft YaHei 32px fallback, then Unifont 16px 2x

HUF format v2: glyph_h=32, row_bytes=4, rec_size=132 (4-byte cp + 64B bitmap).
"""
import os, struct, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, 'fonts', 'reading.bin')
ROWS, ROW_BYTES, GLYPH_BYTES = 32, 4, 128
COLS = 32
THR = 96

SIMSUN = 'C:/Windows/Fonts/simsun.ttc'
CONSOLAS = 'C:/Windows/Fonts/consola.ttf'
YAHEI = 'C:/Windows/Fonts/msyh.ttc'


def render_ttf(font, ch, wide):
    """Render one glyph into a 32-row HUF bitmap (row-major, MSB left)."""
    img = Image.new('L', (40, 40), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 1), ch, font=font, fill=255)
    px = img.load()
    rows = []
    for y in range(40):
        row = 0
        for x in range(COLS):
            if px[x, y] > THR:
                row |= 1 << (COLS - 1 - x)
        rows.append(row)
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
        for b in range(ROW_BYTES):
            out[y * ROW_BYTES + b] = (v >> (8 * (ROW_BYTES - 1 - b))) & 0xFF
    return bytes(out)


def load_unifont32():
    """16px Unifont glyphs upscaled 2x for symbol coverage."""
    src = os.path.join(ROOT, '.cache', 'unifont.hex')
    if not os.path.exists(src):
        return {}
    g = {}
    with open(src, 'rb') as fh:
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
            if cp > 0xFFFF or len(hx) not in (16, 32):
                continue
            try:
                rb = bytes.fromhex(hx.decode('ascii'))
            except ValueError:
                continue
            srcrows = []
            if len(rb) == 16:            # narrow 8x16
                for r in range(16):
                    srcrows.append(rb[r])
            else:                        # wide 16x16
                for r in range(16):
                    srcrows.append((rb[r * 2] << 8) | rb[r * 2 + 1])
            out = bytearray(GLYPH_BYTES)
            for y in range(ROWS):
                if len(rb) == 16:
                    # narrow 8x16: widen columns 8->16, keep in the LEFT half
                    # of the 32-column cell (rows doubled 16->32)
                    v8 = srcrows[y // 2]
                    v = ((v8 | (v8 << 8)) & 0xFFFF) << 16
                else:
                    # wide 16x16: columns doubled 16->32, rows doubled
                    v16 = srcrows[y // 2]
                    v = 0
                    for c in range(16):
                        if v16 & (1 << (15 - c)):
                            v |= (1 << (31 - 2 * c)) | (1 << (30 - 2 * c))
                out[y * ROW_BYTES + 0] = (v >> 24) & 0xFF
                out[y * ROW_BYTES + 1] = (v >> 16) & 0xFF
            g[cp] = bytes(out)
    return g


def main():
    glyphs = load_unifont32()
    print('unifont32 base glyphs:', len(glyphs))

    # ASCII: Consolas 32
    fa = ImageFont.truetype(CONSOLAS, 32)
    for cp in range(0x20, 0x7F):
        b = render_ttf(fa, chr(cp), wide=False)
        if b:
            glyphs[cp] = b
    print('ASCII -> Consolas done')

    # CJK level-1: SimSun 32
    fc = ImageFont.truetype(SIMSUN, 32)
    n = 0
    for row in range(0xA1, 0xF8):
        for col in range(0xA1, 0xFF):
            try:
                ch = bytes([row, col]).decode('gb2312')
            except UnicodeDecodeError:
                continue
            b = render_ttf(fc, ch, wide=True)
            if b:
                glyphs[ord(ch)] = b
                n += 1
    print('CJK -> SimSun done:', n)

    # other symbols: fullwidth punctuation at 32px (fills its cell);
    # narrow symbols rendered at a size that fits the 16-column cell
    fy32 = ImageFont.truetype(YAHEI, 32)
    fy29 = ImageFont.truetype(YAHEI, 29)
    n2 = 0
    sym_cps = (list(range(0x2000, 0x207F)) + list(range(0x3000, 0x3040)) +
               list(range(0xA0, 0x250)) + list(range(0xFF01, 0xFF5F)))
    for cp in sym_cps:
        ch = chr(cp)
        fullwidth = 0x3000 <= cp < 0x3040 or 0xFF01 <= cp < 0xFF5F
        try:
            b = render_ttf(fy32 if fullwidth else fy29, ch, wide=fullwidth)
        except Exception:
            continue
        if b and any(b):
            glyphs[cp] = b
            n2 += 1
    print('symbols -> YaHei done:', n2)

    os.makedirs(os.path.dirname(DST), exist_ok=True)
    with open(DST, 'wb') as out:
        out.write(b'HUF1')
        out.write(struct.pack('<HBBBBIH', 1, 16, ROWS, ROW_BYTES,
                              GLYPH_BYTES + 4, len(glyphs), 0))
        for cp in sorted(glyphs):
            out.write(struct.pack('<I', cp))
            out.write(glyphs[cp])
    print('wrote %s: %d glyphs, %d bytes' % (DST, len(glyphs), os.path.getsize(DST)))
    for probe, name in [(0x41, 'A'), (0x67, 'g'), (0x4E2D, '中'), (0x00B7, '·')]:
        print('  U+%04X %s %s' % (probe, name, 'present' if probe in glyphs else 'MISSING'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
