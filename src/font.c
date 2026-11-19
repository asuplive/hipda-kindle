#include "font.h"
#include "gbk.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* HUF fonts: glyph_h is 16 (legacy unifont.bin) or 32 (hi-res reading.bin).
 * Glyphs are drawn at ANY target pixel height via nearest-neighbour mapping,
 * so every UI element can match the SVG mockup type scale exactly. */

int font_load(Font *f, const char *path) {
    Font *ascii_src = f->ascii_src;   /* preserve a linked ASCII font */
    memset(f, 0, sizeof(*f));
    f->ascii_src = ascii_src;
    Str raw; str_init(&raw);
    if (file_read(path, &raw) || raw.len < 20) {
        log_msg("font: cannot read %s", path);
        str_free(&raw);
        return -1;
    }
    /* Standard TTF/OTF/TTC files placed in fonts/ work directly: rasterise
     * them into the HUF-shaped in-memory table. No conversion step needed. */
    if (font_is_ttf((const uint8_t *)raw.data, raw.len)) {
        log_msg("font: %s is a standard font, rasterising on load", path);
        if (font_load_ttf(f, &raw) == 0) return 0;
        str_free(&raw);
        return -1;
    }
    if (memcmp(raw.data, "HUF1", 4)) {
        log_msg("font: %s is neither HUF1 nor a supported TTF/OTF", path);
        str_free(&raw);
        return -1;
    }
    const uint8_t *b = (const uint8_t *)raw.data;
    f->cell_w = b[6]; f->glyph_h = b[7]; f->row_bytes = b[8];
    f->rec_size = b[9];
    memcpy(&f->count, b + 10, 4);
    f->blob = (uint8_t *)raw.data;    /* transfer ownership */
    f->size = raw.len;
    f->records = f->blob + 16;
    raw.data = NULL; raw.len = raw.cap = 0;
    int glyph_bytes = f->glyph_h * f->row_bytes;
    uint32_t want_rec = (uint32_t)glyph_bytes + 4;
    int ok = (f->glyph_h == 16 || f->glyph_h == 32 ||
              f->glyph_h == 40 || f->glyph_h == 48) &&
             f->row_bytes == f->glyph_h / 8 &&
             f->rec_size == want_rec &&
             f->count > 0 &&
             16 + (size_t)f->count * f->rec_size <= f->size;
    if (!ok) {
        log_msg("font: inconsistent HUF1 header in %s (h=%d rb=%d rs=%d)",
                path, f->glyph_h, f->row_bytes, f->rec_size);
        font_free(f);
        return -1;
    }
    /* Pre-index ASCII: it dominates UI + mixed Latin text, so a direct lookup
     * avoids ~16 binary-search comparisons per character on every repaint. */
    for (int cp = 0; cp < 128; cp++) {
        uint32_t lo = 0, hi = f->count;
        while (lo < hi) {
            uint32_t m = lo + (hi - lo) / 2;
            const uint8_t *r = f->records + (size_t)m * f->rec_size;
            uint32_t v; memcpy(&v, r, 4);
            if (v == (uint32_t)cp) { f->ascii[cp] = r + 4; break; }
            if (v < (uint32_t)cp) lo = m + 1; else hi = m;
        }
    }
    f->last_cp = 0xFFFFFFFFu;
    f->last_glyph = NULL;
    log_msg("font: %s -> %u glyphs, %dpx, %zu bytes",
            path, f->count, f->glyph_h, f->size);
    return 0;
}
void font_free(Font *f) { free(f->blob); memset(f, 0, sizeof(*f)); }

const uint8_t *font_glyph(Font *f, uint32_t cp) {
    if (!f->records) return NULL;
    /* ASCII/Latin may come from a dedicated font (user-selectable in Settings) */
    if (cp < 0x100 && f->ascii_src && f->ascii_src != f)
        return font_glyph(f->ascii_src, cp);
    if (cp < 128) return f->ascii[cp];          /* O(1) ASCII */
    if (cp == f->last_cp) return f->last_glyph; /* repeated glyph run */
    uint32_t lo = 0, hi = f->count;
    const uint8_t *found = NULL;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        const uint8_t *r = f->records + (size_t)m * f->rec_size;
        uint32_t v; memcpy(&v, r, 4);
        if (v == cp) { found = r + 4; break; }
        if (v < cp) lo = m + 1; else hi = m;
    }
    f->last_cp = cp;
    f->last_glyph = found;
    return found;
}

/* Draw one glyph scaled to target height `th`.
 * Source: glyph_h rows, row_bytes*8 columns. A cell is half the source
 * width for ASCII (cells=1) and the full width for CJK (cells=2); the
 * target advance is th/2 per cell, preserving the 1:2 width ratio. */
static void draw_glyph(Font *f, Canvas *c, int x, int y, uint32_t cp,
                       int cells, int th, uint8_t ink) {
    const uint8_t *bmp = font_glyph(f, cp);
    if (!bmp) bmp = font_glyph(f, '?');
    int H = f->glyph_h ? f->glyph_h : 16;
    int W = (f->row_bytes ? f->row_bytes : 2) * 8;
    int gw = th / 2 * cells;                 /* target glyph width */
    if (gw < 1) gw = 1;
    if (!bmp) {
        canvas_frame(c, x, y, gw, th, ink, 1);
        return;
    }
    int srcw = W / 2 * cells;                /* source columns in use */
    if (srcw > W) srcw = W;
    /* Find the glyph's real ink rows. Font cells reserve ascender/descender
     * space, so a plain cell-centred draw looks too high for lowercase text
     * and too low for CJK. Align the INK centre to the target box centre. */
    int ink_top = H, ink_bot = -1;
    for (int r = 0; r < H; r++) {
        const uint8_t *rp = bmp + (size_t)r * f->row_bytes;
        int any = 0;
        for (int b = 0; b < f->row_bytes; b++) if (rp[b]) { any = 1; break; }
        if (any) { if (r < ink_top) ink_top = r; if (r > ink_bot) ink_bot = r; }
    }
    /* Translate only: keep the glyph's natural proportions. The ink centre
     * in source space is mapped to the target box centre as a small shift. */
    int y_shift = 0;                         /* pixel offset within target box */
    if (ink_bot >= ink_top) {
        double ink_center = (ink_top + ink_bot + 1) / 2.0;
        double cell_center = H / 2.0;
        y_shift = (int)((cell_center - ink_center) * th / H);
        if (y_shift > th / 4) y_shift = th / 4;
        if (y_shift < -th / 4) y_shift = -th / 4;
    }
    int draw_h = th;
    for (int row = 0; row < draw_h; row++) {
        int srow = row * H / th;
        const uint8_t *rp = bmp + (size_t)srow * f->row_bytes;
        uint32_t bits = 0;
        for (int b = 0; b < f->row_bytes && b < 4; b++)
            bits = (bits << 8) | rp[b];
        if (W < 32) bits <<= (32 - W);   /* MSB-align for narrow fonts */
        if (!bits) continue;
        int yy = y + y_shift + row;
        if (yy < 0 || yy >= c->h) continue;
        /* read a source column directly from the row bytes (works for
         * glyphs wider than 32 columns too) */
#define GLYPH_BIT(rp, rb, sc) ((rp[(sc) / 8] >> (7 - ((sc) % 8))) & 1u)
        int col = 0;
        while (col < gw) {
            int sc = col * srcw / gw;
            if (!GLYPH_BIT(rp, f->row_bytes, sc)) { col++; continue; }
            int start = col;
            while (col < gw) {
                sc = col * srcw / gw;
                if (!GLYPH_BIT(rp, f->row_bytes, sc)) break;
                col++;
            }
#undef GLYPH_BIT
            /* horizontal run [start, col) at one source row */
            int bx = x + start, bw = col - start;
            if (bx < 0) { bw += bx; bx = 0; }
            if (bx + bw > c->w) bw = c->w - bx;
            if (bw > 0) {
                int y1 = yy + 1;
                if (y1 > c->h) y1 = c->h;
                memset(c->px + (size_t)yy * c->w + bx, ink, (size_t)bw);
                (void)y1;
            }
        }
    }
}

/* Draw one UTF-8 string at target height th. Returns x after the last glyph. */
int font_draw_text(Font *f, Canvas *c, int x, int y, const char *s,
                   int th, uint8_t ink, int bold) {
    if (!s || th < 4) return x;
    const char *p = s, *end = s + strlen(s);
    int px = x;
    int bold_off = th / 24 > 0 ? th / 24 : 1;
    while (p < end) {
        uint32_t cp = utf8_next(&p, end);
        int cells = utf8_cells(cp);
        if (cells <= 0) continue;
        if (cp == ' ') { px += cells * th / 2; continue; }
        draw_glyph(f, c, px, y, cp, cells, th, ink);
        if (bold) draw_glyph(f, c, px + bold_off, y, cp, cells, th, ink);
        if (bold >= 2) {                       /* triple-strike for inverted */
            draw_glyph(f, c, px - bold_off, y, cp, cells, th, ink);
            draw_glyph(f, c, px, y - 1, cp, cells, th, ink);
        }
        px += cells * th / 2;
    }
    return px;
}

int font_text_width(Font *f, const char *s, int th) {
    (void)f;
    if (!s || th < 4) return 0;
    return utf8_str_cells(s) * th / 2;
}
