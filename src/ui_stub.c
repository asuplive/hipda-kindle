/* PC backend: writes the exact same Y8 bytes as PGM files so that the browser
 * preview is a pixel-accurate rendering of what the Kindle would show. */
#include "ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int    g_n = 0;
static int    g_w, g_h;
static char   g_dir[HIPDA_PATH_LEN] = "preview";

int ui_init(UiInfo *out) {
    const char *dir = getenv("HIPDA_PREVIEW_DIR");
    if (dir && *dir) str_copy(g_dir, sizeof(g_dir), dir);
    const char *screen = getenv("HIPDA_SCREEN");   /* e.g. 758x1024@212 */
    int w = 600, h = 800, dpi = 167;
    if (screen) sscanf(screen, "%dx%d@%d", &w, &h, &dpi);
    g_w = w; g_h = h;
    out->w = w; out->h = h; out->dpi = dpi;
    dir_ensure(g_dir);
    return 0;
}

int ui_present(const unsigned char *px, int w, int h, int flashing) {
    (void)flashing;
    char path[HIPDA_PATH_LEN];
    snprintf(path, sizeof(path), "%s/page_%03d.pgm", g_dir, g_n++);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    size_t n = (size_t)w * (size_t)h;
    size_t wrote = fwrite(px, 1, n, f);
    if (fclose(f)) return -1;
    return wrote == n ? 0 : -1;
}
/* PC preview has no e-ink cost, so a partial refresh is a no-op. */
int ui_present_region(const unsigned char *px, int full_w, int x, int y,
                      int w, int h, int flashing) {
    (void)px; (void)full_w; (void)x; (void)y; (void)w; (void)h; (void)flashing;
    return 0;
}
void ui_set_refresh_mode(int mode) { (void)mode; }
void ui_set_rotation(int deg) { (void)deg; }
void ui_close(void) { log_msg("preview: wrote %d pages to %s/", g_n, g_dir); }
