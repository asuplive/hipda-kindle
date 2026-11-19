/* TTF/OTF font support via stb_truetype.
 *
 * Standard font files (.ttf/.otf/.ttc) dropped into fonts/ are loaded
 * directly — NO conversion step needed. On load we rasterise the glyphs the
 * UI can use into an in-memory HUF1-shaped table, so the rest of the font
 * pipeline (font_glyph / draw_glyph / dual-font dispatch) is unchanged.
 *
 * Only stb_truetype.h is needed; no FreeType and no system font library.
 */
#include "font.h"
#include "gbk_table.h"
#include "util.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/stb_truetype.h"

#define TTF_H        32
#define TTF_ROWBYTES 4
#define TTF_GLYPH_BYTES (TTF_H * TTF_ROWBYTES)   /* 128 */
#define TTF_REC_SIZE (TTF_GLYPH_BYTES + 4)

/* Rasterise one codepoint into a 32x32, 1-bit, MSB-first glyph bitmap.
 * Returns 1 if the font actually has the glyph. */
static int ttf_render(stbtt_fontinfo *fi, uint32_t cp, uint8_t *dst, int narrow) {
    int gi = stbtt_FindGlyphIndex(fi, (int)cp);
    if (!gi) return 0;                  /* .notdef -> skip */
    /* ASCII lives in the LEFT HALF (16 columns) of the bitmap, matching
     * draw_glyph()'s srcw = W/2 for cells=1. Render it at a smaller size so
     * the advance fits 16px; CJK fills all 32 columns. */
    float scale = stbtt_ScaleForPixelHeight(fi,
                      (float)(narrow ? TTF_H * 14 / 16 : TTF_H * 28 / 32));
    int w, h, xoff, yoff;
    unsigned char *mono = stbtt_GetGlyphBitmap(
        fi, scale, scale, gi, &w, &h, &xoff, &yoff);
    if (!mono) return 0;
    memset(dst, 0, TTF_GLYPH_BYTES);
    /* centre the inked box vertically and horizontally in the 32x32 cell;
     * stb returns rows top-down, MSB = leftmost pixel */
    int x0 = (32 - w) / 2;              /* centre glyph in the cell */
    int y0 = (TTF_H - h) / 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (int y = 0; y < h; y++) {
        int ty = y0 + y;
        if (ty < 0 || ty >= TTF_H) continue;
        for (int x = 0; x < w; x++) {
            int tx = x0 + x;
            if (tx < 0 || tx >= 32) continue;
            if (mono[y * w + x] > 96)          /* antialias -> 1-bit */
                dst[ty * TTF_ROWBYTES + (31 - tx) / 8] |=
                    (uint8_t)(1u << (7 - ((31 - tx) % 8)));
        }
    }
    stbtt_FreeBitmap(mono, NULL);
    return 1;
}

/* Does this file look like a TrueType/OpenType font? */
int font_is_ttf(const uint8_t *data, size_t len) {
    if (len < 12) return 0;
    if (data[0] == 0x00 && data[1] == 0x01 && data[2] == 0x00 && data[3] == 0x00)
        return 1;                       /* TrueType */
    if (!memcmp(data, "true", 4) || !memcmp(data, "typ1", 4)) return 1;
    if (!memcmp(data, "ttcf", 4)) return 1;   /* TTC collection */
    if (!memcmp(data, "OTTO", 4)) return 1;   /* OpenType/CFF */
    return 0;
}

/* Load a TTF/OTF/TTC font. Returns 0 on success, -1 on failure.
 * `raw` is owned by the caller on failure and transferred to `f` on success. */
int font_load_ttf(Font *f, Str *raw) {
    stbtt_fontinfo fi;
    const unsigned char *data = (const unsigned char *)raw->data;
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    if (off < 0 || !stbtt_InitFont(&fi, data, off)) {
        log_msg("ttf: InitFont failed");
        return -1;
    }

    /* Collect the codepoints worth rasterising. */
    uint32_t cps[16384];
    int n = 0;
    for (uint32_t cp = 0x20; cp < 0x100 && n < 16384; cp++) cps[n++] = cp;
    /* CJK punctuation + fullwidth forms */
    for (uint32_t cp = 0x2000; cp < 0x207F && n < 16384; cp++) cps[n++] = cp;
    for (uint32_t cp = 0x3000; cp < 0x3040 && n < 16384; cp++) cps[n++] = cp;
    for (uint32_t cp = 0xFF01; cp < 0xFF5F && n < 16384; cp++) cps[n++] = cp;
    /* GB2312 Hanzi via the app's existing mapping table (same indexing as
     * gbk.c: lead 0x81..0xFE, trail 0x40..0xFE, span 191). */
    for (int hi = GBK_LEAD_MIN; hi <= GBK_LEAD_MAX && n < 16384; hi++) {
        for (int lo = GBK_TRAIL_MIN; lo <= GBK_TRAIL_MAX && n < 16384; lo++) {
            size_t idx = (size_t)(hi - GBK_LEAD_MIN) * GBK_TRAIL_SPAN +
                         (size_t)(lo - GBK_TRAIL_MIN);
            if (idx >= 24066) continue;
            uint16_t u = gbk_to_uni[idx];
            if (u && u != 0xFFFF) cps[n++] = (uint32_t)u;
        }
    }
    /* sorted + dedup */
    for (int i = 1; i < n; i++) {
        uint32_t v = cps[i];
        int j = i - 1;
        while (j >= 0 && cps[j] > v) { cps[j + 1] = cps[j]; j--; }
        cps[j + 1] = v;
    }
    int m = 0;
    for (int i = 0; i < n; i++)
        if (i == 0 || cps[i] != cps[i - 1]) cps[m++] = cps[i];
    n = m;

    size_t total = 16 + (size_t)n * TTF_REC_SIZE;
    uint8_t *blob = (uint8_t *)malloc(total);
    if (!blob) return -1;
    memset(blob, 0, total);
    memcpy(blob, "HUF1", 4);
    blob[4] = 1; blob[5] = 16; blob[6] = 16;
    blob[7] = TTF_H; blob[8] = TTF_ROWBYTES; blob[9] = TTF_REC_SIZE;
    uint32_t cnt = 0;
    memcpy(blob + 10, &n, 4);
    uint8_t glyph[TTF_GLYPH_BYTES];
    for (int i = 0; i < n; i++) {
        int narrow = (cps[i] < 0x100);    /* ASCII/Latin -> left half */
        if (!ttf_render(&fi, cps[i], glyph, narrow)) continue;
        uint8_t *rec = blob + 16 + (size_t)cnt * TTF_REC_SIZE;
        memcpy(rec, &cps[i], 4);
        memcpy(rec + 4, glyph, TTF_GLYPH_BYTES);
        cnt++;
    }
    memcpy(blob + 10, &cnt, 4);

    /* Hand the generated HUF-shaped blob to the normal loader. */
    Str fake; str_init(&fake);
    fake.data = (char *)blob; fake.len = fake.cap = (size_t)total;
    Font *ascii_src = f->ascii_src;
    memset(f, 0, sizeof(*f));
    f->ascii_src = ascii_src;
    f->blob = blob; f->size = total;
    f->cell_w = 16; f->glyph_h = TTF_H; f->row_bytes = TTF_ROWBYTES;
    f->rec_size = TTF_REC_SIZE; f->count = cnt;
    f->records = f->blob + 16;
    str_free(raw);                      /* original TTF file no longer needed */
    fake.data = NULL;
    for (int cp = 0; cp < 128; cp++) {
        uint32_t lo = 0, hi = f->count;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            const uint8_t *r = f->records + (size_t)mid * f->rec_size;
            uint32_t v; memcpy(&v, r, 4);
            if (v == (uint32_t)cp) { f->ascii[cp] = r + 4; break; }
            if (v < (uint32_t)cp) lo = mid + 1; else hi = mid;
        }
    }
    f->last_cp = 0xFFFFFFFFu;
    f->last_glyph = NULL;
    log_msg("ttf: rasterised %u glyphs from font file", cnt);
    return 0;
}
