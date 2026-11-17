/* Kindle backend: FBInk owns the framebuffer and all the e-ink ioctl quirks
 * (einkfb on K2/K3, mxcfb on K5+, mtk on the newest models). We only hand it
 * a finished Y8 bitmap. No FreeType, no HarfBuzz, no system fonts needed. */
#include "ui.h"
#include "util.h"
#include "fbink.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int         g_fbfd = FBFD_AUTO;
static FBInkConfig g_cfg;
static int         g_refresh_mode = 3;   /* see ui.h: 0 all / 1 partial / 2 regal / 3 hybrid */
static int         g_rotation = 0;       /* 0/90/180/270, content rotation CW */
static unsigned char *g_rot_buf = NULL;
static int         g_rot_w = 0, g_rot_h = 0, g_rot_lw = 0, g_rot_lh = 0;

void ui_set_refresh_mode(int mode) {
    if (mode >= 0 && mode <= 3) g_refresh_mode = mode;
}

void ui_set_rotation(int deg) {
    if (deg == 0 || deg == 90 || deg == 180 || deg == 270) g_rotation = deg;
}

/* Rotate the logical canvas (what the user sees) into panel orientation.
 * Mapping (content rotated clockwise by deg):
 *   90:  device(dx,dy) = logical(lx=dy, ly=lh-1-dx)
 *   180: device(dx,dy) = logical(lw-1-dx, lh-1-dy)
 *   270: device(dx,dy) = logical(lw-1-dy, lx=dx)
 * The input_touch side inverts the same mapping. */
static const unsigned char *rotated(const unsigned char *src, int lw, int lh,
                                    int *dw, int *dh) {
    if (g_rotation == 0) { *dw = lw; *dh = lh; return src; }
    int dW = (g_rotation == 90 || g_rotation == 270) ? lh : lw;
    int dH = (g_rotation == 90 || g_rotation == 270) ? lw : lh;
    if (!g_rot_buf || g_rot_w != dW || g_rot_h != dH ||
        g_rot_lw != lw || g_rot_lh != lh) {
        free(g_rot_buf);
        g_rot_buf = malloc((size_t)dW * (size_t)dH);
        if (!g_rot_buf) { *dw = lw; *dh = lh; return src; }   /* fall back */
        g_rot_w = dW; g_rot_h = dH; g_rot_lw = lw; g_rot_lh = lh;
    }
    for (int dy = 0; dy < dH; dy++) {
        unsigned char *drow = g_rot_buf + (size_t)dy * dW;
        for (int dx = 0; dx < dW; dx++) {
            int sx, sy;
            if (g_rotation == 90)       { sx = dy;          sy = lh - 1 - dx; }
            else if (g_rotation == 180) { sx = lw - 1 - dx; sy = lh - 1 - dy; }
            else                        { sx = lw - 1 - dy; sy = dx; }
            drow[dx] = src[(size_t)sy * lw + sx];
        }
    }
    *dw = dW; *dh = dH;
    return g_rot_buf;
}

/* Map a logical region to panel coordinates (bounding box after rotation). */
static void rotated_region(int lw, int lh, int x, int y, int w, int h,
                           int *rx, int *ry, int *rw, int *rh) {
    if (g_rotation == 0) { *rx = x; *ry = y; *rw = w; *rh = h; return; }
    if (g_rotation == 90)       { *rx = y;        *ry = lh - x - w; *rw = h; *rh = w; }
    else if (g_rotation == 180) { *rx = lw - x - w; *ry = lh - y - h; *rw = w; *rh = h; }
    else                        { *rx = lw - y - h; *ry = x;        *rw = h; *rh = w; }
}

/* Per-refresh waveform, four user-selectable modes:
 *   0 = full refresh every time (WFM_GC16 + flash — slowest, zero ghosting)
 *   1 = partial refresh every time (WFM_DU — fastest, ghosting builds up)
 *   2 = Regal (WFM_REAGL — Kindle-only mode, low flash, decent quality)
 *   3 = hybrid: DU per page + a GC16 full flash every N pages (default)      */
static uint32_t pick_wfm(int flashing, FBInkConfig *cfg) {
    cfg->is_flashing = false;
    switch (g_refresh_mode) {
        case 0:  cfg->is_flashing = true;  return WFM_GC16;
        case 1:  return WFM_DU;
        case 2:  return WFM_REAGL;
        default: return flashing ? WFM_GC16 : WFM_DU;
    }
}

int ui_init(UiInfo *out) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.is_quiet = true;
    /* Our canvas is a plain Y8 buffer with NO alpha channel. FBInk's docs
     * explicitly require ignore_alpha for this on Kindle; without it FBInk
     * assumes a 2-component YA input, re-interleaves the whole page through
     * a temporary buffer, and on some panels (mxcfb / inverted grayscale)
     * the result is a blank white screen. */
    g_cfg.ignore_alpha = true;
    g_fbfd = fbink_open();
    if (g_fbfd < 0) { log_msg("fbink_open failed"); return -1; }
    if (fbink_init(g_fbfd, &g_cfg) < 0) { log_msg("fbink_init failed"); return -1; }
    FBInkState st;
    memset(&st, 0, sizeof(st));
    fbink_get_state(&g_cfg, &st);
    out->w = (int)(st.view_width ? st.view_width : st.screen_width);
    out->h = (int)(st.view_height ? st.view_height : st.screen_height);
    out->dpi = (int)st.screen_dpi;
    if (out->w <= 0 || out->h <= 0) { out->w = 600; out->h = 800; }
    if (out->dpi <= 0) out->dpi = 167;
    log_msg("display: %s / %s  %dx%d @%ddpi  %dbpp  %s",
            st.device_name, st.device_platform, out->w, out->h, out->dpi,
            st.bpp, st.is_kindle_legacy ? "einkfb" : "mxcfb/mtk");
    log_msg("fbink: view=%ux%u screen=%ux%u invert=%d",
            st.view_width, st.view_height, st.screen_width, st.screen_height,
            (int)st.inverted_grayscale);
    return 0;
}

int ui_present(const unsigned char *px, int w, int h, int flashing) {
    int dw, dh;
    const unsigned char *out = rotated(px, w, h, &dw, &dh);
    FBInkConfig cfg = g_cfg;
    cfg.wfm_mode = pick_wfm(flashing, &cfg);
    cfg.no_refresh = false;
    cfg.is_cleared = false;
    int r = fbink_print_raw_data(g_fbfd, out, dw, dh, (size_t)dw * (size_t)dh, 0, 0, &cfg);
    if (r < 0) log_msg("fbink_print_raw_data failed: %d", r);
    return r < 0 ? -1 : 0;
}

int ui_present_region(const unsigned char *px, int full_w, int x, int y,
                      int w, int h, int flashing) {
    if (w <= 0 || h <= 0 || full_w <= 0) return 0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return 0;

    /* Rotated panels: rotate the whole logical page once, then crop the
     * region out of the panel-oriented copy. The rotated copy is cached, so
     * back-to-back row feedback (press highlight, selection move) skips the
     * rotation entirely. */
    int lw = full_w, lh = (full_w > 0) ? g_rot_lh : 0;
    /* recover logical height: the caller only passes full_w, so derive it
     * from the panel size we know */
    FBInkState st;
    memset(&st, 0, sizeof(st));
    fbink_get_state(&g_cfg, &st);
    int sw = (int)(st.view_width ? st.view_width : st.screen_width);
    int sh = (int)(st.view_height ? st.view_height : st.screen_height);
    if (g_rotation == 90 || g_rotation == 270) { lh = sw; lw = sh; }
    else { lw = sw; lh = sh; }

    int dw, dh;
    const unsigned char *out = rotated(px, lw, lh, &dw, &dh);

    int rx, ry, rw, rh;
    rotated_region(lw, lh, x, y, w, h, &rx, &ry, &rw, &rh);
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > dw) rw = dw - rx;
    if (ry + rh > dh) rh = dh - ry;
    if (rw <= 0 || rh <= 0) return 0;

    /* Reuse one scratch buffer: a full-page refresh builds a 2MB temp buffer
     * every time, which is exactly the kind of churn that made e-ink feel
     * frozen. A one-row region is a few dozen KB and reuses the allocation. */
    static unsigned char *buf = NULL;
    static size_t cap = 0;
    size_t need = (size_t)rw * (size_t)rh;
    if (need > cap) {
        unsigned char *nb = (unsigned char *)realloc(buf, need);
        if (!nb) { log_msg("region: out of memory (%zu bytes)", need); return -1; }
        buf = nb;
        cap = need;
    }
    for (int row = 0; row < rh; row++)
        memcpy(buf + (size_t)row * (size_t)rw,
               out + (size_t)(ry + row) * (size_t)dw + (size_t)rx,
               (size_t)rw);

    FBInkConfig cfg = g_cfg;
    cfg.wfm_mode = pick_wfm(flashing, &cfg);
    cfg.no_refresh = false;
    cfg.is_cleared = false;
    int r = fbink_print_raw_data(g_fbfd, buf, rw, rh, need, (short)rx, (short)ry, &cfg);
    if (r < 0) log_msg("region present failed: %d (x=%d y=%d w=%d h=%d)", r, rx, ry, rw, rh);
    return r < 0 ? -1 : 0;
}

void ui_close(void) {
    if (g_fbfd != FBFD_AUTO) fbink_close(g_fbfd);
    g_fbfd = FBFD_AUTO;
}
