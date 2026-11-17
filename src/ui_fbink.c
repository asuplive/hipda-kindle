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
static int         g_fast_wfm = 1;

void ui_set_fast_wfm(int on) { g_fast_wfm = on ? 1 : 0; }

/* Per-refresh waveform. DU is ~260ms and pure black & white, which suits our
 * 1-bit bitmap font; GC16 costs ~450ms but wipes ghosting, so it is reserved
 * for the periodic full flash. */
static uint32_t pick_wfm(int flashing, FBInkConfig *cfg) {
    cfg->is_flashing = false;
    if (flashing) return WFM_GC16;
    return g_fast_wfm ? WFM_DU : WFM_AUTO;
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
    FBInkConfig cfg = g_cfg;
    cfg.wfm_mode = pick_wfm(flashing, &cfg);
    cfg.no_refresh = false;
    cfg.is_cleared = false;
    int r = fbink_print_raw_data(g_fbfd, px, w, h, (size_t)w * (size_t)h, 0, 0, &cfg);
    if (r < 0) log_msg("fbink_print_raw_data failed: %d", r);
    return r < 0 ? -1 : 0;
}

int ui_present_region(const unsigned char *px, int full_w, int x, int y,
                      int w, int h, int flashing) {
    if (w <= 0 || h <= 0 || full_w <= 0) return 0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return 0;

    /* Reuse one scratch buffer: a full-page refresh builds a 2MB temp buffer
     * every time, which is exactly the kind of churn that made e-ink feel
     * frozen. A one-row region is a few dozen KB and reuses the allocation. */
    static unsigned char *buf = NULL;
    static size_t cap = 0;
    size_t need = (size_t)w * (size_t)h;
    if (need > cap) {
        unsigned char *nb = (unsigned char *)realloc(buf, need);
        if (!nb) { log_msg("region: out of memory (%zu bytes)", need); return -1; }
        buf = nb;
        cap = need;
    }
    for (int row = 0; row < h; row++)
        memcpy(buf + (size_t)row * (size_t)w,
               px + (size_t)(y + row) * (size_t)full_w + (size_t)x,
               (size_t)w);

    FBInkConfig cfg = g_cfg;
    cfg.wfm_mode = pick_wfm(flashing, &cfg);
    cfg.no_refresh = false;
    cfg.is_cleared = false;
    int r = fbink_print_raw_data(g_fbfd, buf, w, h, need, (short)x, (short)y, &cfg);
    if (r < 0) log_msg("region present failed: %d (x=%d y=%d w=%d h=%d)", r, x, y, w, h);
    return r < 0 ? -1 : 0;
}

void ui_close(void) {
    if (g_fbfd != FBFD_AUTO) fbink_close(g_fbfd);
    g_fbfd = FBFD_AUTO;
}
