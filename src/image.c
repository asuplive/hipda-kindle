/* Image decoding for the tap-to-view feature. stb_image handles JPEG/PNG/
 * GIF(first frame)/BMP/TGA/PSD — enough for every attachment format the
 * forum actually serves. Output is plain Y8 (one byte per pixel) which is
 * what the canvas and FBInk both speak. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int img_load_y8(const char *path, int max_w, int max_h,
                unsigned char **out, int *ow, int *oh) {
    if (!path || !out || !ow || !oh) return -1;
    *out = NULL;
    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load(path, &w, &h, &comp, 1);   /* force grey */
    if (!px) return -1;
    if (w < 1 || h < 1) { stbi_image_free(px); return -1; }

    /* Clamp to the visible area: images bigger than the screen are scaled
     * down (long edge wins), tiny images pass through untouched. */
    double sc = 1.0;
    if (w > max_w) sc = (double)max_w / (double)w;
    if ((double)h * sc > (double)max_h) sc = (double)max_h / (double)h;
    int tw = (int)((double)w * sc + 0.5);
    int th = (int)((double)h * sc + 0.5);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;

    unsigned char *dst = (unsigned char *)malloc((size_t)tw * (size_t)th);
    if (!dst) { stbi_image_free(px); return -1; }
    for (int dy = 0; dy < th; dy++) {
        int sy = (int)((double)dy * h / th);
        if (sy >= h) sy = h - 1;
        const unsigned char *srow = px + (size_t)sy * (size_t)w;
        unsigned char *drow = dst + (size_t)dy * (size_t)tw;
        if (tw == w) {
            memcpy(drow, srow, (size_t)tw);
        } else {
            for (int dx = 0; dx < tw; dx++) {
                int sx = (int)((double)dx * w / tw);
                if (sx >= w) sx = w - 1;
                drow[dx] = srow[sx];
            }
        }
    }
    stbi_image_free(px);
    *out = dst;
    *ow = tw;
    *oh = th;
    return 0;
}
