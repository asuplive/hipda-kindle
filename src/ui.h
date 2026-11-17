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
/* Waveform strategy. 1 = fast (WFM_DU, ~260ms, pure B&W, tiny ghosting),
 * 0 = crisp (WFM_AUTO/GC16, ~450ms). Periodic full flashes always use GC16
 * so any accumulated ghosting is cleared. */
void ui_set_fast_wfm(int on);
void ui_close(void);

#endif /* HIPDA_UI_H */
