#include "render.h"
#include "layout.h"
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

/* Toolbar hit test — must match exactly what render_page draws.
 * Returns 1 for the "Hi" menu button, 2 for the refresh ring, 0 elsewhere. */
int render_toolbar_hit(const Geometry *g, int x, int y) {
    int mx = g->margin_x, my = g->margin_y;
    int hi_w = 3 * g->cell_w;
    int hi_x = mx - g->cell_w / 2;
    int hi_y = my - g->cell_w / 2;
    /* Hi square */
    if (x >= hi_x && x <= hi_x + hi_w && y >= hi_y && y <= hi_y + hi_w)
        return 1;
    /* page counter doubles as the refresh target (SVG shows text only) */
    int bottom = my + hi_w + g->cell_w;
    if (y >= hi_y && y <= bottom && x >= g->w - mx - 10 * g->cell_w)
        return 2;
    return 0;
}

/* Footer button geometry — shared by the painter and the hit test so they can
 * never disagree. which: 0 = prev page, 1 = next page, 2 = back. */
static void footer_btn_rect(const Geometry *g, int which,
                            int *bx, int *by, int *bw, int *bh) {
    int scale = g->scale;
    int right = g->w - g->margin_x;
    int fy = g->h - g->margin_y - g->line_h;
    int span = right - g->margin_x;
    int wback = 5 * g->cell_w;                 /* SVG 98px @600 */
    int wlabel = span * 120 / 536;             /* text hit strip */
    if (which == 0)      *bx = g->margin_x;
    else if (which == 1) *bx = g->margin_x + span * 118 / 536;
    else                 *bx = right - wback;
    *by = fy - 2 * scale;
    *bw = (which == 2) ? wback : wlabel;
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
    (void)refresh_phase;
    canvas_fill(c, ink(theme, INK_WHITE));
    const int scale = g->scale;
    const int lh = g->line_h;
    const int mx = g->margin_x;
    const int my = g->margin_y;
    const int right = g->w - mx;

    /* ---- toolbar (SVG 1:1): solid black [Hi] square, bold title, right-
     * aligned "page / total" text, then a rule. No refresh ring in the mockup;
     * tapping the page counter still refreshes (hit test keeps that region). */
    int hi_w = 3 * g->cell_w;                 /* SVG 36px @600 -> ~3 cells */
    int hi_h = 3 * g->cell_w;
    int hi_x = mx - g->cell_w / 2;
    int hi_y = my - g->cell_w / 2;
    canvas_rect(c, hi_x, hi_y, hi_w, hi_h, ink(theme, INK_BLACK));
    int hi_scale = scale > 2 ? scale - 1 : scale;
    int hi_tw = font_text_width(font, "Hi", hi_scale);
    font_draw_text(font, c, hi_x + (hi_w - hi_tw) / 2,
                   hi_y + (hi_h - 16 * hi_scale) / 2, "Hi", hi_scale,
                   ink(theme, INK_WHITE), 1);

    char counter[64];
    snprintf(counter, sizeof(counter), "%d / %d", page_index + 1, b->np);
    int cw = font_text_width(font, counter, hi_scale);
    int counter_x = right - cw;
    font_draw_text(font, c, counter_x, my, counter, hi_scale,
                   ink(theme, INK_GRAY), 0);

    /* title fills the space between the Hi square and the page counter */
    int title_x = hi_x + hi_w + g->cell_w;
    int title_cells = (counter_x - g->cell_w - title_x) / g->cell_w;
    if (title_cells < 4) title_cells = 4;
    char head[HIPDA_NAME_LEN];
    clip_cells(head, sizeof(head), b->title, title_cells);
    font_draw_text(font, c, title_x, my, head, scale,
                   ink(theme, INK_BLACK), 1);

    canvas_hline(c, mx, my + hi_h - g->cell_w / 2, right - mx,
                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);

    /* ---- content ---- */
    int y = my + lh;
    const Page *pg = &b->pages[page_index];
    for (int i = 0; i < pg->count; i++) {
        const Line *l = &b->lines[pg->start + i];
        int x = mx + l->indent * g->cell_w;
        if (l->kind == L_SEP) {
            canvas_hline(c, mx, y + g->cell_h / 2, right - mx,
                         ink(theme, 0xC0), scale > 1 ? 1 : 1);
        } else if (l->kind == L_KEYS) {
            /* keyboard row: 10 boxed keys, 3 cells apart, pressed row inverts */
            int press = (i == press_line);
            for (int k = 0; k < 10; k++) {
                int kx = mx + k * 3 * g->cell_w + 2;
                int kw = 3 * g->cell_w - 4;
                int kh = g->cell_h + 2 * scale;
                if (press) {
                    canvas_rect(c, kx, y - scale, kw, kh, ink(theme, INK_BLACK));
                    font_draw_text(font, c, kx + kw / 2 - g->cell_w / 2, y,
                                   l->text ? l->text + k * KEY_CHARS : "",
                                   scale, ink(theme, INK_WHITE), 1);
                } else {
                    canvas_frame(c, kx, y - scale, kw, kh,
                                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                    font_draw_text(font, c, kx + kw / 2 - g->cell_w / 2, y,
                                   l->text ? l->text + k * KEY_CHARS : "",
                                   scale, ink(theme, INK_BLACK), 1);
                }
            }
        } else if (l->kind == L_CTRL) {
            /* control row: 6 boxed buttons, 6 cells apart. Labels are baked
             * into the text at fixed 6-char slots so hit-test matches paint. */
            static const char *const ctrl_labels[6] = {
                "Sh", "Pg", "空格", "退格", "回车", "取消"
            };
            int press = (i == press_line);
            for (int k = 0; k < 6; k++) {
                int kx = mx + k * 6 * g->cell_w + 2;
                int kw = 6 * g->cell_w - 4;
                int kh = g->cell_h + 2 * scale;
                int tw = font_text_width(font, ctrl_labels[k], scale);
                if (press) {
                    canvas_rect(c, kx, y - scale, kw, kh, ink(theme, INK_BLACK));
                    font_draw_text(font, c, kx + (kw - tw) / 2, y, ctrl_labels[k],
                                   scale, ink(theme, INK_WHITE), 1);
                } else {
                    canvas_frame(c, kx, y - scale, kw, kh,
                                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                    font_draw_text(font, c, kx + (kw - tw) / 2, y, ctrl_labels[k],
                                   scale, ink(theme, INK_BLACK), 0);
                }
            }
        } else if (l->kind == L_CAND) {
            /* pinyin candidates: up to 8 boxed UTF-8 slots, 5 cells apart.
             * Each char is 3 UTF-8 bytes; slot 7 may hold U+2026 = more. */
            const char *cs = l->text ? l->text : "";
            int n = (int)strlen(cs) / 3;
            int press = (i == press_line);
            for (int k = 0; k < KEY_CAND_SLOTS; k++) {
                int kx = mx + k * KEY_CAND_STRIDE * g->cell_w + 2;
                int kw = KEY_CAND_STRIDE * g->cell_w - 4;
                int kh = g->cell_h + 2 * scale;
                const char *ch = (k < n) ? cs + k * 3 : "";
                int tw = font_text_width(font, ch, scale);
                if (press) {
                    canvas_rect(c, kx, y - scale, kw, kh, ink(theme, INK_BLACK));
                    if (ch[0]) font_draw_text(font, c, kx + (kw - tw) / 2, y,
                                              ch, scale, ink(theme, INK_WHITE), 1);
                } else {
                    canvas_frame(c, kx, y - scale, kw, kh,
                                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                    if (ch[0]) font_draw_text(font, c, kx + (kw - tw) / 2, y,
                                              ch, scale, ink(theme, INK_BLACK), 0);
                }
            }
        } else if (l->kind == L_BTN) {
            /* wide centred button (登 录): frame + label, inverts on press */
            int tw = font_text_width(font, l->text ? l->text : "", scale);
            int bw = tw + 10 * scale;
            int bh = g->cell_h + 4 * scale;
            int bx = (g->w - bw) / 2;
            if (i == press_line) {
                canvas_rect(c, bx, y - 2 * scale, bw, bh, ink(theme, INK_BLACK));
                font_draw_text(font, c, bx + 5 * scale, y, l->text, scale,
                               ink(theme, INK_WHITE), 1);
            } else {
                canvas_frame(c, bx, y - 2 * scale, bw, bh,
                             ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                font_draw_text(font, c, bx + 5 * scale, y, l->text, scale,
                               ink(theme, INK_BLACK), 1);
            }
        } else if (l->kind == L_FIELD) {
            /* SVG 06-login field card: framed row, thick left bar, label on
             * the left, a bordered input box on the right. text = "label\x01value". */
            const char *t = l->text ? l->text : "";
            const char *bar = strchr(t, '\x01');
            char label[HIPDA_NAME_LEN], value[HIPDA_NAME_LEN];
            size_t ln = bar ? (size_t)(bar - t) : strlen(t);
            if (ln >= sizeof(label)) ln = sizeof(label) - 1;
            memcpy(label, t, ln); label[ln] = 0;
            str_copy(value, sizeof(value), bar ? bar + 1 : "");
            int hh = g->cell_h + 3 * scale;
            int top = y - scale;
            canvas_frame(c, mx, top, right - mx, hh, ink(theme, INK_BLACK),
                         scale > 1 ? 2 : 1);
            canvas_rect(c, mx, top, 3 * scale, hh, ink(theme, INK_BLACK));
            int ls = scale > 2 ? scale - 1 : scale;
            font_draw_text(font, c, mx + g->cell_w / 2,
                           y + (g->cell_h - 16 * ls) / 2, label, ls,
                           ink(theme, INK_BLACK), 1);
            /* input box: SVG x=150/600 -> 28% of content width */
            int ix = mx + (right - mx) * 118 / 536;
            int iw = right - ix - scale;
            int iy = top + scale;
            int ih = hh - 2 * scale;
            int focused = (l->item_id >= 101) || (i == press_line);
            if (focused) {
                canvas_rect(c, ix, iy, iw, ih, ink(theme, INK_BLACK));
                font_draw_text(font, c, ix + scale, y + (g->cell_h - 16 * ls) / 2,
                               value, ls, ink(theme, INK_WHITE), 0);
            } else {
                canvas_frame(c, ix, iy, iw, ih, ink(theme, INK_BLACK),
                             scale > 1 ? 2 : 1);
                font_draw_text(font, c, ix + scale, y + (g->cell_h - 16 * ls) / 2,
                               value, ls, ink(theme, INK_BLACK), 0);
            }
        } else if (l->kind == L_POSTHEAD) {
            /* floor header: a thick left bar + small gray "N 楼 author time" */
            canvas_rect(c, mx, y, 3 * scale, g->cell_h, ink(theme, INK_BLACK));
            font_draw_text(font, c, x + g->cell_w / 2, y, l->text ? l->text : "",
                           scale > 1 ? scale - 1 : scale,
                           ink(theme, INK_GRAY), 0);
        } else if (l->kind == L_GROUP) {
            /* section caption: solid square + bold label (SVG 9px @600) */
            canvas_rect(c, x, y + scale, g->cell_w, g->cell_w,
                        ink(theme, INK_BLACK));
            font_draw_text(font, c, x + 2 * g->cell_w, y, l->text ? l->text : "",
                           scale, ink(theme, INK_BLACK), 1);
        } else if (l->kind == L_CARD_T || l->kind == L_CARD_B) {
            /* Framed card over two rows: thin outline, thick left bar,
             * bold title + right chevron on top, small gray subtitle below.
             * Either row pressed/selected inverts the WHOLE card. */
            const Page *pg = &b->pages[page_index];
            int is_top = (l->kind == L_CARD_T);
            int mate = is_top ? i + 1 : i - 1;
            int mate_ok = mate >= 0 && mate < pg->count &&
                b->lines[pg->start + mate].kind == (is_top ? L_CARD_B : L_CARD_T);
            int pressed = (i == press_line) || (mate_ok && mate == press_line);
            int selected = (i == sel_line) || (mate_ok && mate == sel_line);
            int invert = pressed || selected;
            int dscale = scale > 1 ? scale - 1 : scale;
            /* leave a small gap between cards, like the reference design */
            int gap = 2 * scale;
            int th = 2 * lh - gap;                 /* card spans two rows */
            int top = y + scale;                   /* T row */
            if (!is_top) top = y - lh + scale;     /* B row: find card top */
            int cx = mx, cw = right - mx;
            uint8_t line_col = invert ? ink(theme, INK_WHITE)
                                      : ink(theme, INK_BLACK);
            uint8_t t_col = invert ? ink(theme, INK_WHITE) : ink(theme, INK_BLACK);
            uint8_t s_col = invert ? ink(theme, INK_WHITE) : ink(theme, INK_GRAY);
            if (invert) {
                canvas_rect(c, cx, top, cw, th, ink(theme, INK_BLACK));
            } else {
                /* frame: top + bottom + right; thick left bar */
                canvas_hline(c, cx, top, cw, line_col, scale > 1 ? 2 : 1);
                canvas_hline(c, cx, top + th - 1, cw, line_col, scale > 1 ? 2 : 1);
                canvas_rect(c, right - scale, top, scale, th, line_col);
                canvas_rect(c, cx, top, 3 * scale, th, line_col);
            }
            int tx = cx + 5 * g->cell_w / 2;
            if (is_top) {
                font_draw_text(font, c, tx, y, l->text ? l->text : "",
                               scale, t_col, 1);
                /* right chevron */
                char chev[2] = { '>', 0 };
                int cwch = font_text_width(font, chev, scale);
                font_draw_text(font, c, right - 2 * g->cell_w - cwch, y,
                               chev, scale, t_col, 0);
            } else if (l->text && l->text[0] && mate_ok) {
                int sy = y + (g->cell_h - 16 * dscale) / 2;
                if (sy < y) sy = y;
                font_draw_text(font, c, tx, sy, l->text, dscale, s_col, 0);
            }
        } else if (l->kind == L_NUM) {
            /* numbered row: "NN" + title + chevron, whole row inverts on
             * press / keyboard selection */
            int pressed = (i == press_line);
            int selected = (i == sel_line);
            if (pressed || selected) {
                canvas_rect(c, mx - scale, y - scale, right - mx + 2 * scale,
                            g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            } else {
                canvas_hline(c, mx, y + g->cell_h - scale, right - mx,
                             ink(theme, INK_FAINT), scale > 1 ? 2 : 1);
            }
            uint8_t col = (pressed || selected) ? ink(theme, INK_WHITE)
                                                : ink(theme, INK_BLACK);
            const char *t = l->text ? l->text : "";
            /* first two bytes are the 2-digit number, then a space */
            char num[3] = { t[0], t[1], 0 };
            font_draw_text(font, c, x, y, num, scale, col, 1);
            font_draw_text(font, c, x + 4 * g->cell_w, y, t + 3, scale, col, 1);
            char chev[2] = { '>', 0 };
            int cwch = font_text_width(font, chev, scale);
            font_draw_text(font, c, right - 2 * g->cell_w - cwch, y, chev, scale,
                           col, 0);
        } else if (l->kind == L_FTITLE || l->kind == L_FDESC) {
            /* forum home: bold section title over a smaller, fainter
             * description. Both invert on press; the title also inverts
             * when keyboard-selected. */
            int dscale = (l->kind == L_FDESC && scale > 1) ? scale - 1 : scale;
            int press = (i == press_line);
            int sel = (i == sel_line && l->kind == L_FTITLE);
            uint8_t col = (l->kind == L_FDESC) ? ink(theme, INK_GRAY)
                                               : ink(theme, INK_BLACK);
            /* small desc sits vertically centred in the row */
            int dy = y + (g->cell_h - 16 * dscale) / 2;
            if (dy < y) dy = y;
            if (press || sel) {
                canvas_rect(c, mx - scale, y - scale, right - mx + 2 * scale,
                            g->cell_h + 2 * scale, ink(theme, INK_BLACK));
                font_draw_text(font, c, x, dy, l->text ? l->text : "", dscale,
                               ink(theme, INK_WHITE),
                               l->kind == L_FTITLE);
            } else {
                font_draw_text(font, c, x, dy, l->text ? l->text : "", dscale,
                               col, l->kind == L_FTITLE);
            }
        } else if (i == press_line && l->text && l->text[0]) {
            /* finger is on this row right now: invert it so the touch is
             * visibly acknowledged even on a slow e-ink screen */
            canvas_rect(c, mx - scale, y - scale, right - mx + 2 * scale,
                        g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            font_draw_text(font, c, x, y, l->text, scale,
                           ink(theme, INK_WHITE), 1);
        } else if ((l->kind == L_ITEM || l->kind == L_TITLE) && i == sel_line) {
            canvas_rect(c, mx - scale, y - scale, right - mx + 2 * scale,
                        g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            font_draw_text(font, c, x, y, l->text, scale,
                           ink(theme, INK_WHITE), 1);
        } else {
            uint8_t col = ink(theme, INK_BLACK);
            int bold = 0;
            if (l->kind == L_META || l->kind == L_NOTE || l->kind == L_IMGPH)
                col = ink(theme, INK_GRAY);
            if (l->kind == L_TITLE) bold = 1;
            font_draw_text(font, c, x, y, l->text, scale, col, bold);
        }
        y += lh;
    }

    /* ---- footer (SVG 1:1): "‹ 上一页" and "下一页 ›" as plain text,
     * a grey page counter in the middle, and a boxed "返回" at right. ---- */
    int fy = g->h - my - lh;
    canvas_hline(c, mx, fy - 3 * scale, right - mx, ink(theme, INK_BLACK),
                 scale > 1 ? 2 : 1);
    int fs = scale > 2 ? scale - 1 : scale;    /* SVG fs15 -> smaller */
    font_draw_text(font, c, mx, fy, "‹ 上一页", fs,
                   ink(theme, INK_BLACK), 0);
    int nx = mx + (right - mx) * 118 / 600;    /* SVG x=150 -> (150-32)/536 */
    font_draw_text(font, c, nx, fy, "下一页 ›", fs,
                   ink(theme, INK_BLACK), 0);
    /* page counter centred between the two page-turn labels */
    char mid[64];
    snprintf(mid, sizeof(mid), "%d / %d", page_index + 1, b->np);
    int mw = font_text_width(font, mid, fs);
    int mcenter = mx + (right - mx) * 298 / 536;  /* SVG x=330 */
    font_draw_text(font, c, mcenter - mw / 2, fy, mid, fs,
                   ink(theme, INK_GRAY), 0);
    /* boxed back button at the right */
    int bx, by, bw, bh;
    footer_btn_rect(g, 2, &bx, &by, &bw, &bh);
    canvas_frame(c, bx, by, bw, bh, ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
    int tw = font_text_width(font, "返回", fs);
    font_draw_text(font, c, bx + (bw - tw) / 2, fy, "返回", fs,
                   ink(theme, INK_BLACK), 0);

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

static void render_toolbar_static(Canvas *c, Font *font, const Geometry *g,
                                  const char *title, const char *counter,
                                  int theme) {
    int scale = g->scale;
    int mx = g->margin_x, my = g->margin_y, right = g->w - mx;
    int hi_w = 3 * g->cell_w;
    int hi_x = mx - g->cell_w / 2, hi_y = my - g->cell_w / 2;
    canvas_rect(c, hi_x, hi_y, hi_w, hi_w, ink(theme, INK_BLACK));
    int hs = scale > 2 ? scale - 1 : scale;
    int tw = font_text_width(font, "Hi", hs);
    font_draw_text(font, c, hi_x + (hi_w - tw) / 2,
                   hi_y + (hi_w - 16 * hs) / 2, "Hi", hs,
                   ink(theme, INK_WHITE), 1);
    if (counter && *counter) {
        int cw = font_text_width(font, counter, hs);
        font_draw_text(font, c, right - cw, my, counter, hs,
                       ink(theme, INK_GRAY), 0);
    }
    int title_x = hi_x + hi_w + g->cell_w;
    int maxcells = (right - 10 * g->cell_w - title_x) / g->cell_w;
    if (maxcells < 4) maxcells = 4;
    char head[HIPDA_NAME_LEN];
    clip_cells(head, sizeof(head), title ? title : "", maxcells);
    font_draw_text(font, c, title_x, my, head, scale,
                   ink(theme, INK_BLACK), 1);
    canvas_hline(c, mx, my + hi_w - g->cell_w / 2, right - mx,
                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
}

void render_message(Canvas *c, Font *font, const Geometry *g,
                    const char *title, const char *body, int theme) {
    canvas_fill(c, ink(theme, INK_WHITE));
    int scale = g->scale;
    int mx = g->margin_x, right = g->w - mx;
    int loading = title && (strstr(title, "正在加载") || strstr(title, "正在读取"));
    if (loading) {
        /* SVG 02-loading 1:1: toolbar, big framed panel, thick left bar,
         * 6 progress squares, centred title + explanation + cancel hint. */
        render_toolbar_static(c, font, g, title, "···", theme);
        int cy = g->h * 25 / 100;             /* SVG y=200/800 */
        int ch = g->h * 375 / 1000;           /* SVG h=300/800 */
        canvas_frame(c, mx, cy, right - mx, ch, ink(theme, INK_BLACK),
                     scale > 1 ? 2 : 1);
        canvas_rect(c, mx, cy, 3 * scale, ch, ink(theme, INK_BLACK));
        /* progress squares: SVG x=220..404 step 32, y=248 */
        int bs = 24 * g->w / 600;
        int step = 32 * g->w / 600;
        int sy = cy + 48 * g->w / 600;
        int sx = g->w / 2 - step * 3 + (step - bs) / 2;
        for (int k = 0; k < 6; k++) {
            int px = sx + k * step;
            if (k < 3) canvas_rect(c, px, sy, bs, bs, ink(theme, INK_BLACK));
            else canvas_frame(c, px, sy, bs, bs, ink(theme, INK_BLACK),
                              scale > 1 ? 2 : 1);
        }
        int hs = scale > 2 ? scale - 1 : scale;
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "正在读取论坛数据", scale) / 2,
                       cy + 120 * g->w / 600, "正在读取论坛数据", scale,
                       ink(theme, INK_BLACK), 1);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "正在从论坛读取主题列表。", hs) / 2,
                       cy + 160 * g->w / 600, "正在从论坛读取主题列表。", hs,
                       ink(theme, INK_GRAY), 0);
        canvas_hline(c, g->w * 12 / 100, cy + 192 * g->w / 600,
                     g->w * 76 / 100, ink(theme, 0xCC), scale > 1 ? 2 : 1);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "点任意位置可取消", hs) / 2,
                       cy + 232 * g->w / 600, "点任意位置可取消", hs,
                       ink(theme, INK_BLACK), 0);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "加载完成后自动进入板块列表", hs) / 2,
                       cy + 266 * g->w / 600, "加载完成后自动进入板块列表", hs,
                       ink(theme, INK_GRAY), 0);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "点任意位置返回", hs) / 2,
                       g->h - g->margin_y - g->line_h, "点任意位置返回", hs,
                       ink(theme, INK_GRAY), 0);
        return;
    }
    /* ordinary message: toolbar + plain wrapped text */
    render_toolbar_static(c, font, g, title, NULL, theme);
    int x = mx;
    int y = g->margin_y + 2 * g->line_h;
    font_draw_text(font, c, x, y, title ? title : "", scale,
                   ink(theme, INK_BLACK), 1);
    y += 2 * g->line_h;
    canvas_hline(c, x, y - 3 * scale, right - mx,
                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
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
    font_draw_text(font, c, x, g->h - g->margin_y - g->line_h,
                   "点任意位置返回", scale, ink(theme, INK_GRAY), 0);
}
