#ifndef HIPDA_INPUT_H
#define HIPDA_INPUT_H

typedef enum {
    ACT_NONE = 0,
    ACT_PRESS,       /* finger just touched down: draw press feedback */
    ACT_NEXT,        /* turn one page forward  */
    ACT_PREV,        /* turn one page back     */
    ACT_MENU,        /* open the small menu    */
    ACT_BACK,        /* back to parent view    */
    ACT_SELECT,      /* activate a tapped row / selected line */
    ACT_UP,
    ACT_DOWN,
    ACT_REFRESH,
    ACT_PAGE_PREV,   /* previous forum page (footer button) */
    ACT_PAGE_NEXT,   /* next forum page (footer button)     */
    ACT_IMGV,        /* open the tapped image placeholder   */
    ACT_HOME,
    ACT_QUIT
} Action;

typedef struct {
    Action act;
    int    x, y;         /* tap coordinates, mapped to screen pixels */
    int    raw_x, raw_y; /* untransformed panel values (for calibration) */
    int    tap;          /* 1 = this is a touch event, 0 = key event */
} InputEvent;

int  input_open(void);
/* Tell the input layer the real screen size BEFORE input_open(). Touch panels
 * report raw coordinates and the mapping needs the actual resolution; without
 * this the tap space defaults to 600x800 and every click lands in the wrong
 * place (which looks like "the sides turn pages"). */
void input_set_screen(int w, int h);
/* Axis correction for panels whose coordinates are swapped or mirrored.
 * Learned once by the on-device calibration screen and persisted in config.ini. */
void input_set_calib(int swap, int flip_x, int flip_y);
int  input_read(InputEvent *ev, int timeout_ms);
void input_close(void);

#endif /* HIPDA_INPUT_H */
