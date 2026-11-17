#ifndef HIPDA_UI_H
#define HIPDA_UI_H

typedef struct { int w, h, dpi; } UiInfo;

/* Open the display and report REAL screen geometry. Every Kindle model is
 * handled here (FBInk probes the framebuffer); higher layers never hardcode
 * a resolution. */
int  ui_init(UiInfo *out);
/* Push a complete Y8 page to the e-ink panel. flashing != 0 requests a GC16
 * full refresh to clear ghosting (done every N pages, like the stock reader). */
int  ui_present(const unsigned char *px, int w, int h, int flashing);
/* Push only a rectangle of the canvas. Used for press feedback: refreshing one
 * text row is ~20x cheaper than a full page on e-ink. `px` is the full canvas
 * (stride == full_w); (x,y,w,h) is the region to refresh. */
int  ui_present_region(const unsigned char *px, int full_w, int x, int y,
                       int w, int h, int flashing);
/* Refresh strategy, four modes:
 *   0 = full refresh every time (GC16 + flash)
 *   1 = partial refresh every time (DU)
 *   2 = Regal (WFM_REAGL, Kindle-specific)
 *   3 = hybrid: DU per page + a full GC16 flash every N pages (default)
 * In mode 3 the app sets `flashing` every N pages; the other modes ignore it. */
void ui_set_refresh_mode(int mode);
/* Content rotation, degrees clockwise (0/90/180/270). The UI layers always
 * draw on a logical canvas of geo.w×geo.h; ui_* rotates pixels into panel
 * orientation before touching the framebuffer. */
void ui_set_rotation(int deg);
void ui_close(void);

#endif /* HIPDA_UI_H */
