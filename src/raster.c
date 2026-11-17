#include "raster.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int canvas_init(Canvas *c, int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    c->w = w; c->h = h;
    c->px = xmalloc((size_t)w * (size_t)h);
    canvas_fill(c, 255);
    return 0;
}
void canvas_free(Canvas *c) { free(c->px); c->px = NULL; c->w = c->h = 0; }
void canvas_fill(Canvas *c, uint8_t v) {
    if (c->px) memset(c->px, v, (size_t)c->w * (size_t)c->h);
}
void canvas_pixel(Canvas *c, int x, int y, uint8_t v) {
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->px[y * c->w + x] = v;
}
void canvas_rect(Canvas *c, int x, int y, int w, int h, uint8_t v) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w;
    int y1 = y + h > c->h ? c->h : y + h;
    for (int yy = y0; yy < y1; yy++)
        memset(c->px + (size_t)yy * c->w + x0, v, (size_t)(x1 - x0));
}
void canvas_hline(Canvas *c, int x, int y, int w, uint8_t v, int thick) {
    canvas_rect(c, x, y, w, thick < 1 ? 1 : thick, v);
}
void canvas_frame(Canvas *c, int x, int y, int w, int h, uint8_t v, int thick) {
    int t = thick < 1 ? 1 : thick;
    canvas_rect(c, x, y, w, t, v);
    canvas_rect(c, x, y + h - t, w, t, v);
    canvas_rect(c, x, y, t, h, v);
    canvas_rect(c, x + w - t, y, t, h, v);
}
int canvas_write_pgm(Canvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", c->w, c->h);
    size_t n = (size_t)c->w * (size_t)c->h;
    size_t w = fwrite(c->px, 1, n, f);
    if (fclose(f)) return -1;
    return w == n ? 0 : -1;
}
