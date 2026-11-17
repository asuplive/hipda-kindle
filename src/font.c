#include "font.h"
#include "gbk.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int font_load(Font *f, const char *path) {
    memset(f, 0, sizeof(*f));
    Str raw; str_init(&raw);
    if (file_read(path, &raw) || raw.len < 20 || memcmp(raw.data, "HUF1", 4)) {
        log_msg("font: cannot load %s (bad or missing HUF1 file)", path);
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
    if (f->rec_size != 36 || f->glyph_h != 16 || f->row_bytes != 2 ||
        f->count == 0 || 16 + (size_t)f->count * f->rec_size > f->size) {
        log_msg("font: inconsistent HUF1 header in %s", path);
        font_free(f);
        return -1;
    }
    log_msg("font: %s -> %u glyphs, %zu bytes", path, f->count, f->size);
    return 0;
}
void font_free(Font *f) { free(f->blob); memset(f, 0, sizeof(*f)); }

const uint8_t *font_glyph(Font *f, uint32_t cp) {
    if (!f->records) return NULL;
    uint32_t lo = 0, hi = f->count;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        const uint8_t *r = f->records + (size_t)m * f->rec_size;
        uint32_t v; memcpy(&v, r, 4);
        if (v == cp) return r + 4;
        if (v < cp) lo = m + 1; else hi = m;
    }
    return NULL;
}

static void draw_glyph(Font *f, Canvas *c, int x, int y, uint32_t cp,
                       int cells, int scale, uint8_t ink) {
    const uint8_t *bmp = font_glyph(f, cp);
    if (!bmp) bmp = font_glyph(f, '?');
    if (!bmp) {
        canvas_frame(c, x + scale, y + scale, cells * 8 * scale - 2 * scale,
                     16 * scale - 2 * scale, ink, scale);
        return;
    }
    int maxcol = cells * 8;
    if (maxcol > 16) maxcol = 16;
    for (int row = 0; row < 16; row++) {
        uint16_t bits = (uint16_t)((bmp[row * 2] << 8) | bmp[row * 2 + 1]);
        if (!bits) continue;
        for (int col = 0; col < maxcol; col++) {
            if (!(bits & (uint16_t)(0x8000u >> col))) continue;
            if (scale == 1) canvas_pixel(c, x + col, y + row, ink);
            else canvas_rect(c, x + col * scale, y + row * scale, scale, scale, ink);
        }
    }
}

int font_draw_text(Font *f, Canvas *c, int x, int y, const char *s,
                   int scale, uint8_t ink, int bold) {
    if (!s || scale < 1) return x;
    const char *p = s, *end = s + strlen(s);
    int px = x;
    while (p < end) {
        uint32_t cp = utf8_next(&p, end);
        int cells = utf8_cells(cp);
        if (cells <= 0) continue;
        if (cp == ' ') { px += cells * 8 * scale; continue; }
        draw_glyph(f, c, px, y, cp, cells, scale, ink);
        if (bold && scale > 1) draw_glyph(f, c, px + 1, y, cp, cells, scale, ink);
        px += cells * 8 * scale;
    }
    return px;
}

int font_text_width(Font *f, const char *s, int scale) {
    (void)f;
    return utf8_str_cells(s) * 8 * scale;
}
