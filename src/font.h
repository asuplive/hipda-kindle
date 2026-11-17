#ifndef HIPDA_FONT_H
#define HIPDA_FONT_H

#include "hipda.h"
#include "raster.h"

typedef struct {
    uint8_t *blob;             /* whole HUF1 file, kept mapped in RAM */
    size_t   size;
    uint32_t count;
    const uint8_t *records;    /* blob + 16 */
    uint32_t rec_size;
    uint8_t  cell_w, glyph_h, row_bytes;
} Font;

int  font_load(Font *f, const char *path);
void font_free(Font *f);
const uint8_t *font_glyph(Font *f, uint32_t cp);  /* 32 bytes or NULL */

/* Draw one UTF-8 string. Returns the x coordinate after the last glyph.
 * `ink` is a Y8 gray value (0 = black). Drawing is clipped to the canvas. */
int  font_draw_text(Font *f, Canvas *c, int x, int y, const char *s,
                    int scale, uint8_t ink, int bold);
int  font_text_width(Font *f, const char *s, int scale);

#endif /* HIPDA_FONT_H */
