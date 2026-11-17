#ifndef HIPDA_RASTER_H
#define HIPDA_RASTER_H

#include <stdint.h>

/* 8bpp Y8 framebuffer image. 0 = black, 255 = white.
 * On the Kindle this buffer is handed to fbink_print_raw_data(); on the PC
 * the very same bytes are written as a PGM and converted to PNG for preview.
 * This is why the PC preview is pixel-identical to the device screen. */
typedef struct {
    int  w, h;
    uint8_t *px;       /* stride == w */
} Canvas;

int  canvas_init(Canvas *c, int w, int h);
void canvas_free(Canvas *c);
void canvas_fill(Canvas *c, uint8_t v);
void canvas_pixel(Canvas *c, int x, int y, uint8_t v);
void canvas_rect(Canvas *c, int x, int y, int w, int h, uint8_t v);
void canvas_hline(Canvas *c, int x, int y, int w, uint8_t v, int thick);
void canvas_frame(Canvas *c, int x, int y, int w, int h, uint8_t v, int thick);
int  canvas_write_pgm(Canvas *c, const char *path);

#endif /* HIPDA_RASTER_H */
