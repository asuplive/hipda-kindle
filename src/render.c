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
    int hi_w = 36 * g->w / 600;
    int hi_x = mx, hi_y = my;
    /* Hi square */
    if (x >= hi_x && x <= hi_x + hi_w && y >= hi_y && y <= hi_y + hi_w)
        return 1;
    /* page counter doubles as the refresh target (SVG shows text only) */
    if (y >= my && y <= g->top_h && x >= g->w - g->margin_x - 12 * g->cell_w)
        return 2;
    return 0;
}

/* Footer button geometry — shared by the painter and the hit test so they can
 * never disagree. which: 0 = prev page, 1 = next page, 2 = back. */
static void footer_btn_rect(const Geometry *g, int which,
                            int *bx, int *by, int *bw, int *bh) {
    int right = g->w - g->margin_x;
    int fy = g->foot_y;
    int span = right - g->margin_x;
    int wback = span * 98 / 536;               /* SVG 98px @600 */
    int wlabel = span * 120 / 536;             /* text hit strip */
    if (which == 0)      *bx = g->margin_x;
    else if (which == 1) *bx = g->margin_x + span * 118 / 536;
    else                 *bx = right - wback;
    int foot_h = g->h - fy;
    int box_h = 32 * g->h / 800;               /* SVG 32px @800 */
    if (which == 2) {
        *by = fy + (foot_h - box_h) / 2;        /* boxed 返回, centred */
        *bw = wback;
        *bh = box_h;
    } else {
        *by = fy;                               /* text hit strips */
        *bw = wlabel;
        *bh = foot_h;
    }
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
    const int span = right - mx;
    /* SVG type scale (600x800 reference), scaled to this screen */
    const int th_head  = g->head_h;    /* toolbar title   fs21 */
    const int th_title = g->title_h;   /* card titles     fs18 */
    const int th_body  = g->cell_h;    /* body text       fs16 */
    const int th_sub   = g->sub_h;     /* subtitles       fs13 */
    const int th_small = g->small_h;   /* hints           fs12 */
    const int th_foot  = g->foot_h;    /* footer text     fs15 */
    const int th_key   = th_body * 17 / 16;   /* keycaps     fs17 */
    const int th_btn   = th_body * 20 / 16;   /* big button  fs20 */
    const int th_hi    = th_body * 17 / 16;   /* "Hi" badge  fs17 */

    /* ---- toolbar (SVG 1:1): solid black [Hi] square, bold title, right-
     * aligned "page / total" text, rule at top_h. Tapping the page counter
     * refreshes (hit test keeps that region). */
    int hi_w = 36 * g->w / 600;
    int hi_x = mx, hi_y = my;
    canvas_rect(c, hi_x, hi_y, hi_w, hi_w, ink(theme, INK_BLACK));
    int hi_tw = font_text_width(font, "Hi", th_hi);
    font_draw_text(font, c, hi_x + (hi_w - hi_tw) / 2,
                   hi_y + (hi_w - th_hi) / 2, "Hi", th_hi,
                   ink(theme, INK_WHITE), 1);

    char counter[64];
    snprintf(counter, sizeof(counter), "%d / %d", page_index + 1, b->np);
    int cs = th_body * 16 / 16;
    int cw = font_text_width(font, counter, cs);
    int counter_x = right - cw;
    font_draw_text(font, c, counter_x, my + (hi_w - cs) / 2, counter, cs,
                   ink(theme, INK_GRAY), 0);

    int title_x = hi_x + hi_w + 21 * g->w / 600;   /* SVG x=78 -> title_x */
    int title_cells = (counter_x - g->cell_w - title_x) / g->cell_w;
    if (title_cells < 4) title_cells = 4;
    char head[HIPDA_NAME_LEN];
    clip_cells(head, sizeof(head), b->title, title_cells);
    int ty = my + (hi_w - th_head) / 2;
    if (ty < my) ty = my;
    font_draw_text(font, c, title_x, ty, head, th_head,
                   ink(theme, INK_BLACK), 1);

    canvas_hline(c, mx, g->top_h, span, ink(theme, INK_BLACK),
                 scale > 1 ? 2 : 1);

    /* ---- content ---- */
    int y = g->top_h + lh;
    const Page *pg = &b->pages[page_index];
    for (int i = 0; i < pg->count; i++) {
        const Line *l = &b->lines[pg->start + i];
        int x = mx + l->indent * g->cell_w;
        if (l->kind == L_SEP) {
            canvas_hline(c, mx, y + g->cell_h / 2, span,
                         ink(theme, 0xC0), 1);
        } else if (l->kind == L_KEYS) {
            /* keyboard row (SVG 46x42 keys, 8px gaps, 10 per row) */
            int press = (i == press_line);
            int kw = span * 46 / 536;
            int kg = span * 8 / 536;
            int kh = kw * 42 / 46;
            for (int k = 0; k < 10; k++) {
                int kx = mx + k * (kw + kg);
                const char *kc = l->text ? l->text + k * KEY_CHARS : "";
                char one[4] = {0};
                if (kc[0] == ' ') continue;          /* blank filler slot */
                memcpy(one, kc, 3);
                int tw = font_text_width(font, one, th_key);
                if (press) {
                    canvas_rect(c, kx, y - scale, kw, kh + 2 * scale,
                                ink(theme, INK_BLACK));
                    font_draw_text(font, c, kx + (kw - tw) / 2, y,
                                   one, th_key, ink(theme, INK_WHITE), 0);
                } else {
                    canvas_frame(c, kx, y - scale, kw, kh + 2 * scale,
                                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                    font_draw_text(font, c, kx + (kw - tw) / 2, y,
                                   one, th_key, ink(theme, INK_BLACK), 0);
                }
            }
        } else if (l->kind == L_CTRL) {
            /* control row: 6 boxed buttons (SVG 82 wide, 90 stride) */
            static const char *const ctrl_labels[6] = {
                "Sh", "Pg", "空格", "退格", "回车", "取消"
            };
            int press = (i == press_line);
            int kw = span * 82 / 536;
            int ks = span * 90 / 536;
            int kh = kw * 42 / 46;
            for (int k = 0; k < 6; k++) {
                int kx = mx + k * ks;
                int tw = font_text_width(font, ctrl_labels[k], th_key);
                if (press) {
                    canvas_rect(c, kx, y - scale, kw, kh + 2 * scale,
                                ink(theme, INK_BLACK));
                    font_draw_text(font, c, kx + (kw - tw) / 2, y,
                                   ctrl_labels[k], th_key,
                                   ink(theme, INK_WHITE), 0);
                } else {
                    canvas_frame(c, kx, y - scale, kw, kh + 2 * scale,
                                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                    font_draw_text(font, c, kx + (kw - tw) / 2, y,
                                   ctrl_labels[k], th_key,
                                   ink(theme, INK_BLACK), 0);
                }
            }
        } else if (l->kind == L_CAND) {
            /* pinyin candidates: 8 slots across the content width */
            const char *cs2 = l->text ? l->text : "";
            int n = (int)strlen(cs2) / 3;
            int press = (i == press_line);
            int kw = span / KEY_CAND_SLOTS;
            int kh = th_body + 2 * scale;
            for (int k = 0; k < KEY_CAND_SLOTS; k++) {
                int kx = mx + k * kw;
                const char *ch = (k < n) ? cs2 + k * 3 : "";
                int tw = font_text_width(font, ch, th_body);
                if (press) {
                    canvas_rect(c, kx, y - scale, kw - 2, kh,
                                ink(theme, INK_BLACK));
                    if (ch[0]) font_draw_text(font, c, kx + (kw - 2 - tw) / 2, y,
                                              ch, th_body, ink(theme, INK_WHITE), 1);
                } else {
                    canvas_frame(c, kx, y - scale, kw - 2, kh,
                                 ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                    if (ch[0]) font_draw_text(font, c, kx + (kw - 2 - tw) / 2, y,
                                              ch, th_body, ink(theme, INK_BLACK), 0);
                }
            }
        } else if (l->kind == L_BTN) {
            /* centred button (SVG 140x48 @600): frame + label, inverts */
            int tw = font_text_width(font, l->text ? l->text : "", th_btn);
            int bw = span * 140 / 536;
            int bh = 48 * g->h / 800;
            int bx = (g->w - bw) / 2;
            int by = y - scale;
            if (i == press_line) {
                canvas_rect(c, bx, by, bw, bh, ink(theme, INK_BLACK));
                font_draw_text(font, c, bx + (bw - tw) / 2,
                               by + (bh - th_btn) / 2, l->text, th_btn,
                               ink(theme, INK_WHITE), 1);
            } else {
                canvas_frame(c, bx, by, bw, bh,
                             ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
                font_draw_text(font, c, bx + (bw - tw) / 2,
                               by + (bh - th_btn) / 2, l->text, th_btn,
                               ink(theme, INK_BLACK), 1);
            }
        } else if (l->kind == L_FIELD) {
            /* SVG 06-login field card: framed row (52/800 tall), thick left
             * bar, bold label, bordered input box. text = "label\x01value". */
            const char *t = l->text ? l->text : "";
            const char *bar = strchr(t, '\x01');
            char label[HIPDA_NAME_LEN], value[HIPDA_NAME_LEN];
            size_t ln = bar ? (size_t)(bar - t) : strlen(t);
            if (ln >= sizeof(label)) ln = sizeof(label) - 1;
            memcpy(label, t, ln); label[ln] = 0;
            str_copy(value, sizeof(value), bar ? bar + 1 : "");
            int hh = 2 * lh - scale;               /* two rows tall, paints over */
            int top = y - scale;
            canvas_frame(c, mx, top, span, hh, ink(theme, INK_BLACK),
                         scale > 1 ? 2 : 1);
            canvas_rect(c, mx, top, 4 * scale, hh, ink(theme, INK_BLACK));
            int lyy = top + (hh - th_key) / 2;
            font_draw_text(font, c, mx + 20 * g->w / 600, lyy, label, th_key,
                           ink(theme, INK_BLACK), 1);
            int ix = mx + span * 118 / 536;          /* SVG x=150 */
            int iw = span * 394 / 536;               /* SVG w=394 */
            int iy = top + scale;
            int ih = hh - 2 * scale;
            int focused = (l->item_id >= 101) || (i == press_line);
            int vyy = iy + (ih - th_body) / 2;
            if (focused) {
                canvas_rect(c, ix, iy, iw, ih, ink(theme, INK_BLACK));
                font_draw_text(font, c, ix + g->cell_w / 2, vyy,
                               value, th_body, ink(theme, INK_WHITE), 0);
            } else {
                canvas_frame(c, ix, iy, iw, ih, ink(theme, INK_BLACK),
                             scale > 1 ? 2 : 1);
                font_draw_text(font, c, ix + g->cell_w / 2, vyy,
                               value, th_body, ink(theme, INK_BLACK), 0);
            }
        } else if (l->kind == L_POSTHEAD) {
            /* floor header: thick left bar + small gray "N 楼 author time" */
            canvas_rect(c, mx, y, 4 * scale, g->cell_h, ink(theme, INK_BLACK));
            font_draw_text(font, c, x + g->cell_w / 2, y,
                           l->text ? l->text : "", th_sub,
                           ink(theme, INK_GRAY), 0);
        } else if (l->kind == L_GROUP) {
            /* section caption: solid square + bold label (SVG 9px @600) */
            int sq = 9 * g->w / 600;
            canvas_rect(c, x, y + (th_body - sq) / 2, sq, sq,
                        ink(theme, INK_BLACK));
            font_draw_text(font, c, x + 18 * g->w / 600, y,
                           l->text ? l->text : "", th_sub,
                           ink(theme, INK_BLACK), 1);
        } else if (l->kind == L_CARD_T || l->kind == L_CARD_B) {
            /* Framed card over two rows: thin outline, thick left bar,
             * bold title + right chevron on top, small gray subtitle below.
             * Either row pressed/selected inverts the WHOLE card. */
            const Page *pg2 = &b->pages[page_index];
            int is_top = (l->kind == L_CARD_T);
            int mate = is_top ? i + 1 : i - 1;
            int mate_ok = mate >= 0 && mate < pg2->count &&
                b->lines[pg2->start + mate].kind == (is_top ? L_CARD_B : L_CARD_T);
            int pressed = (i == press_line) || (mate_ok && mate == press_line);
            int selected = (i == sel_line) || (mate_ok && mate == sel_line);
            int invert = pressed || selected;
            int gap = 4 * g->w / 600;              /* SVG 4px @600 */
            int ch2 = 2 * lh - gap;
            int top = y + scale;
            if (!is_top) top = y - lh + scale;
            int cx = mx, cw = span;
            uint8_t t_col = invert ? ink(theme, INK_WHITE) : ink(theme, INK_BLACK);
            uint8_t s_col = invert ? ink(theme, INK_WHITE) : ink(theme, INK_GRAY);
            if (invert) {
                canvas_rect(c, cx, top, cw, ch2, ink(theme, INK_BLACK));
            } else {
                canvas_hline(c, cx, top, cw, ink(theme, INK_BLACK),
                             scale > 1 ? 2 : 1);
                canvas_hline(c, cx, top + ch2 - 1, cw, ink(theme, INK_BLACK),
                             scale > 1 ? 2 : 1);
                canvas_rect(c, right - scale, top, scale, ch2,
                            ink(theme, INK_BLACK));
                canvas_rect(c, cx, top, 3 * scale, ch2, ink(theme, INK_BLACK));
            }
            int tx = cx + 16 * g->w / 600;         /* SVG x=48 -> 16px in */
            if (is_top) {
                font_draw_text(font, c, tx, y, l->text ? l->text : "",
                               th_title, t_col, 1);
                font_draw_text(font, c, right - 12 * g->w / 600, y,
                               "›", th_title, t_col, 0);
            } else if (l->text && l->text[0] && mate_ok) {
                int sy = y + (lh - th_sub) / 2;
                if (sy < y) sy = y;
                font_draw_text(font, c, tx, sy, l->text, th_sub, s_col, 0);
            }
        } else if (l->kind == L_NUM) {
            /* SVG 07-settings: numbered card, 50px @600 (~2 rows), thin frame
             * + thick left bar; selected/pressed inverts the whole card. */
            int pressed = (i == press_line);
            int selected = (i == sel_line);
            int gap = 6 * g->w / 600;            /* SVG card gap */
            int hh = 2 * lh - gap;
            int top = y - scale;
            if (pressed || selected) {
                canvas_rect(c, mx, top, span, hh, ink(theme, INK_BLACK));
            } else {
                canvas_frame(c, mx, top, span, hh, ink(theme, INK_BLACK),
                             scale > 1 ? 2 : 1);
                canvas_rect(c, mx, top, 3 * scale, hh, ink(theme, INK_BLACK));
            }
            uint8_t col = (pressed || selected) ? ink(theme, INK_WHITE)
                                                : ink(theme, INK_BLACK);
            int vcy = top + (hh - th_title) / 2;
            if (vcy < y) vcy = y;
            const char *t = l->text ? l->text : "";
            char num[3] = { t[0], t[1], 0 };
            font_draw_text(font, c, x + 20 * g->w / 600, vcy, num,
                           th_body * 17 / 16, col, 1);
            font_draw_text(font, c, x + 72 * g->w / 600, vcy, t + 3,
                           th_title, col, 1);
            font_draw_text(font, c, right - 12 * g->w / 600, vcy, "›",
                           th_title, col, 0);
        } else if (l->kind == L_FTITLE || l->kind == L_FDESC) {
            int th = (l->kind == L_FDESC) ? th_sub : th_title;
            int press = (i == press_line);
            int sel = (i == sel_line && l->kind == L_FTITLE);
            uint8_t col = (l->kind == L_FDESC) ? ink(theme, INK_GRAY)
                                               : ink(theme, INK_BLACK);
            int dy = y + (lh - th) / 2;
            if (dy < y) dy = y;
            if (press || sel) {
                canvas_rect(c, mx - scale, y - scale, span + 2 * scale,
                            lh + 2 * scale, ink(theme, INK_BLACK));
                font_draw_text(font, c, x, dy, l->text ? l->text : "", th,
                               ink(theme, INK_WHITE),
                               l->kind == L_FTITLE);
            } else {
                font_draw_text(font, c, x, dy, l->text ? l->text : "", th,
                               col, l->kind == L_FTITLE);
            }
        } else if (i == press_line && l->text && l->text[0]) {
            /* finger on this row: invert for instant touch feedback */
            canvas_rect(c, mx - scale, y - scale, span + 2 * scale,
                        g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            font_draw_text(font, c, x, y, l->text, th_body,
                           ink(theme, INK_WHITE), 1);
        } else if ((l->kind == L_ITEM || l->kind == L_TITLE) && i == sel_line) {
            canvas_rect(c, mx - scale, y - scale, span + 2 * scale,
                        g->cell_h + 2 * scale, ink(theme, INK_BLACK));
            font_draw_text(font, c, x, y, l->text, th_body,
                           ink(theme, INK_WHITE), 1);
        } else {
            uint8_t col = ink(theme, INK_BLACK);
            int bold = 0;
            int th = th_body;
            if (l->kind == L_META || l->kind == L_IMGPH) {
                col = ink(theme, INK_GRAY);
                th = th_sub;
            } else if (l->kind == L_NOTE) {
                col = ink(theme, INK_GRAY);
                th = th_small;
            } else if (l->kind == L_TITLE) {
                bold = 1;
                th = th_title;
            }
            font_draw_text(font, c, x, y, l->text, th, col, bold);
        }
        y += lh;
    }

    /* ---- footer (SVG 1:1): plain text labels + grey counter + boxed 返回 */
    int fy = g->foot_y;
    canvas_hline(c, mx, fy, span, ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
    int fty = fy + (g->h - fy - th_foot) / 2;
    if (fty < fy) fty = fy;
    font_draw_text(font, c, mx, fty, "‹ 上一页", th_foot,
                   ink(theme, INK_BLACK), 0);
    int nx = mx + span * 118 / 536;                /* SVG x=150 */
    font_draw_text(font, c, nx, fty, "下一页 ›", th_foot,
                   ink(theme, INK_BLACK), 0);
    char mid[64];
    snprintf(mid, sizeof(mid), "%d / %d", page_index + 1, b->np);
    int mw = font_text_width(font, mid, th_foot);
    int mcenter = mx + span * 298 / 536;           /* SVG x=330 */
    font_draw_text(font, c, mcenter - mw / 2, fty, mid, th_foot,
                   ink(theme, INK_GRAY), 0);
    int bx, by, bw, bh;
    footer_btn_rect(g, 2, &bx, &by, &bw, &bh);
    canvas_frame(c, bx, by, bw, bh, ink(theme, INK_BLACK), scale > 1 ? 2 : 1);
    int tw = font_text_width(font, "返回", th_foot);
    int bty = by + (bh - th_foot) / 2;
    font_draw_text(font, c, bx + (bw - tw) / 2, bty, "返回", th_foot,
                   ink(theme, INK_BLACK), 0);

    if (flash_msg && flash_msg[0]) {
        int fw = font_text_width(font, flash_msg, th_body);
        int fbx = (g->w - fw) / 2 - 4 * scale;
        int fby = fy - lh - 2 * scale;
        canvas_rect(c, fbx, fby, fw + 8 * scale, lh, ink(theme, INK_BLACK));
        font_draw_text(font, c, fbx + 4 * scale, fby + (lh - th_body) / 2,
                       flash_msg, th_body, ink(theme, INK_WHITE), 0);
    }
}

/* Toolbar painter shared by render_message pages (loading / dialogs). */
static void render_toolbar_static(Canvas *c, Font *font, const Geometry *g,
                                  const char *title, const char *counter,
                                  int theme) {
    int scale = g->scale;
    int mx = g->margin_x, my = g->margin_y, right = g->w - mx;
    int hi_w = 36 * g->w / 600;
    canvas_rect(c, mx, my, hi_w, hi_w, ink(theme, INK_BLACK));
    int th_hi = g->cell_h * 17 / 16;
    int tw = font_text_width(font, "Hi", th_hi);
    font_draw_text(font, c, mx + (hi_w - tw) / 2, my + (hi_w - th_hi) / 2,
                   "Hi", th_hi, ink(theme, INK_WHITE), 1);
    int cs = g->cell_h;
    if (counter && *counter) {
        int cw = font_text_width(font, counter, cs);
        font_draw_text(font, c, right - cw, my + (hi_w - cs) / 2, counter, cs,
                       ink(theme, INK_GRAY), 0);
    }
    int title_x = mx + hi_w + 21 * g->w / 600;
    int maxcells = (right - 10 * g->cell_w - title_x) / g->cell_w;
    if (maxcells < 4) maxcells = 4;
    char head[HIPDA_NAME_LEN];
    clip_cells(head, sizeof(head), title ? title : "", maxcells);
    int ty = my + (hi_w - g->head_h) / 2;
    if (ty < my) ty = my;
    font_draw_text(font, c, title_x, ty, head, g->head_h,
                   ink(theme, INK_BLACK), 1);
    canvas_hline(c, mx, g->top_h, right - mx, ink(theme, INK_BLACK),
                 scale > 1 ? 2 : 1);
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
        int th_title22 = g->cell_h * 22 / 16;      /* SVG fs22 */
        int th_sub2 = g->sub_h, th_body2 = g->cell_h;
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "正在读取论坛数据", th_title22) / 2,
                       cy + 120 * g->w / 600, "正在读取论坛数据", th_title22,
                       ink(theme, INK_BLACK), 1);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "正在从论坛读取主题列表。", th_sub2) / 2,
                       cy + 160 * g->w / 600, "正在从论坛读取主题列表。", th_sub2,
                       ink(theme, INK_GRAY), 0);
        canvas_hline(c, g->w * 12 / 100, cy + 192 * g->w / 600,
                     g->w * 76 / 100, ink(theme, 0xCC), scale > 1 ? 2 : 1);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "点任意位置可取消", th_body2) / 2,
                       cy + 232 * g->w / 600, "点任意位置可取消", th_body2,
                       ink(theme, INK_BLACK), 0);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "加载完成后自动进入板块列表", th_sub2) / 2,
                       cy + 266 * g->w / 600, "加载完成后自动进入板块列表", th_sub2,
                       ink(theme, INK_GRAY), 0);
        font_draw_text(font, c, g->w / 2 - font_text_width(font, "点任意位置返回", th_sub2) / 2,
                       g->foot_y + (g->h - g->foot_y - th_sub2) / 2,
                       "点任意位置返回", th_sub2,
                       ink(theme, INK_GRAY), 0);
        return;
    }
    /* ordinary message: toolbar + plain wrapped text */
    render_toolbar_static(c, font, g, title, NULL, theme);
    int x = mx;
    int y = g->top_h + g->line_h;
    font_draw_text(font, c, x, y, title ? title : "", g->title_h,
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
        font_draw_text(font, c, x, y, clipped, g->cell_h,
                       ink(theme, INK_BLACK), 0);
        y += g->line_h;
        if (y > g->foot_y) break;
        if (!nl) break;
        p = nl + 1;
    }
    font_draw_text(font, c, x, g->foot_y + (g->h - g->foot_y - g->sub_h) / 2,
                   "点任意位置返回", g->sub_h, ink(theme, INK_GRAY), 0);
}
