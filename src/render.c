#include "render.h"
#include "gbk.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define INK_BLACK  0
#define INK_GRAY   0x66
#define INK_FAINT  0xAA
#define INK_WHITE  255

/* Theme 0 = white paper / black ink (default e-ink look).
 * Theme 1 = inverted (night reading on a front-lit panel). */
static uint8_t ink(int theme, uint8_t v) {
    return theme ? (uint8_t)(255 - v) : v;
}

static void clip_cells(char *dst, size_t cap, const char *src, int max_cells) {
    if (max_cells < 4) max_cells = 4;
    const char *p = src, *end = src + strlen(src);
    int used = 0;
    size_t o = 0;
    while (p < end && o + 5 < cap) {
        const char *q = p;
        uint32_t cp = utf8_next(&p, end);
        int cw = utf8_cells(cp);
        if (used + cw > max_cells) break;
        size_t n = (size_t)(p - q);
        if (o + n + 1 >= cap) break;
        memcpy(dst + o, q, n);
        o += n; dst[o] = 0;
        used += cw;
    }
    if (o == 0) dst[0] = 0;
}

/* Round "refresh" affordance. `phase` rotates a gap around the ring so the
 * icon visibly moves while a page is being reloaded. */
static void draw_ring(Canvas *c, int cx, int cy, int r, int thick,
                      uint8_t v, int phase) {
    double gap_start = (double)((phase & 3) * 90) * 3.14159265 / 180.0;
    int outer = r * r;
    int inner = (r - thick) * (r - thick);
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 > outer || d2 < inner) continue;
            double ang = atan2((double)dy, (double)dx);
            if (ang < 0) ang += 6.2831853;
            double rel = ang - gap_start;
            while (rel < 0) rel += 6.2831853;
            if (rel < 1.25) continue;            /* the gap */
            canvas_pixel(c, x, y, v);
        }
    }
    double a2 = gap_start + 1.25;
    int ax = cx + (int)((r - thick) * cos(a2));
    int ay = cy + (int)((r - thick) * sin(a2));
    canvas_rect(c, ax - thick, ay - thick, thick * 2 + 1, thick * 2 + 1, v);
}

/* Toolbar hit test — must match exactly what render_page draws.
 * Returns 1 for the "Hi" menu button, 2 for the refresh ring, 0 elsewhere. */
int render_toolbar_hit(const Geometry *g, int x, int y) {
    int scale = g->scale;
    int mx = g->margin_x, my = g->margin_y;
    int top = my - 3 * scale;
    int bottom = my + g->cell_h + 3 * scale;
    if (y < top || y > bottom) return 0;

    int hi_x = mx - 2 * scale;
    int hi_w = 2 * g->cell_w + 7 * scale;
    if (x >= hi_x && x <= hi_x + hi_w) return 1;

    int r = g->cell_h / 2 + 2 * scale;
    int cx = g->w - mx - r - 2 * scale;
    if (x >= cx - r - 3 * scale && x <= cx + r + 3 * scale) return 2;
    return 0;
}

/* Footer button geometry — shared by the painter and the hit test so they can
 * never disagree. which: 0 = prev page, 1 = next page, 2 = back. */
static void footer_btn_rect(const Geometry *g, int which,
                            int *bx, int *by, int *bw, int *bh) {
    int scale = g->scale;
    int right = g->w - g->margin_x;
    int fy = g->h - g->margin_y - g->line_h;
    int gap = 4 * scale;
    int wpage = 8 * g->cell_w;
    int wback = 5 * g->cell_w;
    if (which == 0)      *bx = g->margin_x - 2 * scale;
    else if (which == 1) *bx = right - wback - gap - wpage;
    else                 *bx = right - wback;
    *by = fy - 2 * scale;
    *bw = (which == 2) ? wback : wpage;
    *bh = g->cell_h + 4 * scale;
}

int render_footer_btn_hit(const Geometry *g, int x, int y) {
    for (int i = 0; i < 3; i++) {
        int bx, by, bw, bh;
        footer_btn_rect(g, i, &bx, &by, &bw, &bh);
        if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) return i + 1;
    }
    return 0;
}

void render_page(Canvas *c, Font *font, const Geometry *g, const Book *b,
                 int page_index, int sel_line, int press_line,
                 const char *flash_msg, int theme, int refresh_phase) {
    canvas_fill(c, ink(theme, INK_WHITE));
    const int scale = g->scale;
    const int lh = g->line_h;
    const int mx = g->margin_x;
    const int my = g->margin_y;
    const int right = g->w - mx;

    /* ---- toolbar: [Hi] title .... page (o) ---- */
    int hi_x = mx - 2 * scale;
    int hi_w = 2 * g->cell_w + 7 * scale;
    int hi_h = g->cell_h + 5 * scale;
    int hi_y = my - 2 * scale;
    canvas_frame(c, hi_x, hi_y, hi_w, hi_h, ink(theme, INK_BLACK),
                 scale > 1 ? 2 : 1);
    font_draw_text(font, c, hi_x + 3 * scale, my, "Hi", scale,
                   ink(theme, INK_BLACK), 1);

    int r = g->cell_h / 2 + 2 * scale;
    int ring_cx = right - r - 2 * scale;
    int ring_cy = my + g->cell_h / 2;
    draw_ring(c, ring_cx, ring_cy, r, scale > 1 ? 3 : 2,
              ink(theme, INK_BLACK), refresh_phase);

    /* page counter just left of the ring */
    char counter[64];
    snprintf(counter, sizeof(counter), "%d/%d", page_index + 1, b->np);
    int cw = font_text_width(font, counter, scale);
    font_draw_text(font, c, ring_cx - r - 5 * scale - cw, my, counter, scale,
                   ink(theme, INK_GRAY), 0);

    /* title fills what is left between the button and the counter */
    int title_cells = (ring_cx - r - 8 * scale - cw - (hi_x + hi_w + 4 * scale))
                      / g->cell_w;
    if (title_cells < 4) title_cells = 4;
    char head[HIPDA_NAME_LEN];
    clip_cells(head, sizeof(head), b->title, title_cells);
    font_draw_text(font, c, hi_x + hi_w + 4 * scale, my, head, scale,
                   ink(theme, INK_BLACK), 1);

    canvas_hline(c, mx, my + g->cell_h + 4 * scale, right - mx,
                 ink(theme, INK_FAINT), scale > 1 ? 2 : 1);

    /* ---- content ---- */
    int y = my + lh;
    const Page *pg = &b->pages[page_index];
    for (int i = 0; i < pg->count; i++) {
        const Line *l = &b->lines[pg->start + i];
        int x = mx + l->indent * g->cell_w;
        if (l->kind == L_SEP) {
            canvas_hline(c, mx, y + g->cell_h / 2, right - mx,
                         ink(theme, 0xC0), scale > 1 ? 1 : 1);
        } else if (i == press_line && l->text && l->text[0]) {
            /* finger is on this row right now: invert it so the touch is
             * visibly acknowledged even on a slow e-ink screen */
            canvas_rect(c, mx - scale, y - scale, right - mx + 2 * scale,
                        g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            font_draw_text(font, c, x, y, l->text, scale,
                           ink(theme, INK_WHITE), 1);
        } else if (l->kind == L_ITEM && i == sel_line) {
            canvas_rect(c, mx - scale, y - scale, right - mx + 2 * scale,
                        g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            font_draw_text(font, c, x, y, l->text, scale,
                           ink(theme, INK_WHITE), 1);
        } else {
            uint8_t col = ink(theme, INK_BLACK);
            int bold = 0;
            if (l->kind == L_META || l->kind == L_NOTE) col = ink(theme, INK_GRAY);
            if (l->kind == L_TITLE) bold = 1;
            font_draw_text(font, c, x, y, l->text, scale, col, bold);
        }
        y += lh;
    }

    /* ---- footer: [‹ 上一页] .. page .. [下一页 ›] [返回] ---- */
    int fy = g->h - my - lh;
    canvas_hline(c, mx, fy - 3 * scale, right - mx, ink(theme, INK_FAINT),
                 scale > 1 ? 2 : 1);

    static const char *const btn_text[3] = { "‹ 上一页", "下一页 ›", "返回" };
    for (int i = 0; i < 3; i++) {
        int bx, by, bw, bh;
        footer_btn_rect(g, i, &bx, &by, &bw, &bh);
        canvas_frame(c, bx, by, bw, bh, ink(theme, INK_GRAY), scale > 1 ? 2 : 1);
        int tw = font_text_width(font, btn_text[i], scale);
        font_draw_text(font, c, bx + (bw - tw) / 2, fy, btn_text[i], scale,
                       ink(theme, INK_GRAY), 0);
    }

    /* page counter centred between the prev and next buttons */
    char mid[64];
    snprintf(mid, sizeof(mid), "%d / %d", page_index + 1, b->np);
    int p0x, p0y, p0w, p0h, p1x, p1y, p1w, p1h;
    footer_btn_rect(g, 0, &p0x, &p0y, &p0w, &p0h);
    footer_btn_rect(g, 1, &p1x, &p1y, &p1w, &p1h);
    int mw = font_text_width(font, mid, scale);
    int mcenter = (p0x + p0w + p1x) / 2;
    font_draw_text(font, c, mcenter - mw / 2, fy, mid, scale,
                   ink(theme, INK_GRAY), 0);

    if (flash_msg && flash_msg[0]) {
        int fw = font_text_width(font, flash_msg, scale);
        int bx = (g->w - fw) / 2 - 4 * scale;
        int by = fy - lh - 2 * scale;
        canvas_rect(c, bx, by, fw + 8 * scale, lh, ink(theme, INK_BLACK));
        font_draw_text(font, c, bx + 4 * scale, by + 2 * scale, flash_msg,
                       scale, ink(theme, INK_WHITE), 0);
    }

    /* ---- progress bar ---- */
    int py = g->h - my / 2 - scale;
    canvas_hline(c, mx, py, right - mx, ink(theme, 0xDD), scale);
    int pct = b->np > 1 ? (page_index * 100) / (b->np - 1) : 100;
    canvas_hline(c, mx, py, (right - mx) * pct / 100, ink(theme, INK_BLACK), scale);
}

void render_message(Canvas *c, Font *font, const Geometry *g,
                    const char *title, const char *body, int theme) {
    canvas_fill(c, ink(theme, INK_WHITE));
    int scale = g->scale;
    int x = g->margin_x;
    int y = g->margin_y + 2 * g->line_h;
    font_draw_text(font, c, x, y, title ? title : "", scale,
                   ink(theme, INK_BLACK), 1);
    y += 2 * g->line_h;
    canvas_hline(c, x, y - 3 * scale, g->w - 2 * g->margin_x,
                 ink(theme, INK_FAINT), scale > 1 ? 2 : 1);
    char line[HIPDA_NAME_LEN];
    const char *p = body ? body : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n); line[n] = 0;
        char clipped[HIPDA_NAME_LEN];
        clip_cells(clipped, sizeof(clipped), line, g->cols);
        font_draw_text(font, c, x, y, clipped, scale, ink(theme, INK_BLACK), 0);
        y += g->line_h;
        if (y > g->h - g->margin_y - g->line_h) break;
        if (!nl) break;
        p = nl + 1;
    }
    const char *hint = "点任意位置返回";
    font_draw_text(font, c, x, g->h - g->margin_y - g->line_h,
                   hint, scale, ink(theme, INK_GRAY), 0);
}
