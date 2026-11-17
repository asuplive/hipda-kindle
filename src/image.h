#ifndef HIPDA_IMAGE_H
#define HIPDA_IMAGE_H

/* Decode an image file into a Y8 (grayscale) bitmap, scaled so it fits
 * max_w x max_h. Nearest-neighbour resample; JPEG/PNG/GIF/BMP/TGA via
 * stb_image (single header, no external deps). 0 = success. */
int img_load_y8(const char *path, int max_w, int max_h,
                unsigned char **out, int *ow, int *oh);

#endif /* HIPDA_IMAGE_H */
