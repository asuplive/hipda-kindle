/* Linux evdev input for jailbroken Kindles.
 *
 * Two hard-won lessons baked in here:
 *
 * 1. Kindle touch panels use multi-touch protocol B: contact is announced by
 *    ABS_MT_TRACKING_ID >= 0 and released with -1. They do NOT emit
 *    BTN_TOUCH, so keying only off BTN_TOUCH means no tap is ever produced.
 *    We support both protocols.
 *
 * 2. On release the driver often reports coordinates of 0,0. If you classify
 *    the gesture using the *release* coordinates, every tap looks like a huge
 *    swipe and gets discarded. So we keep the gesture origin (first contact)
 *    and the last valid sample, and classify from those.
 *
 * Gestures: vertical swipe = page turn, short touch = tap (buttons / rows).
 */
#include "input.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#define MAX_DEV 24

typedef struct {
    int fd;
    int is_touch;
    int has_abs, has_key;
    int minx, maxx, miny, maxy;
    int x, y;          /* latest raw coordinates */
    int ox, oy;        /* gesture origin (first contact) */
    int lx, ly;        /* last valid sample during this gesture */
    int have_origin;
    int pressed;
    int lift;          /* released; classify on the next SYN_REPORT */
    int press_sent;    /* ACT_PRESS already emitted for this contact */
    char name[64];
} Dev;

static Dev g_dev[MAX_DEV];
static int g_ndev = 0;
static int g_screen_w = 600, g_screen_h = 800;
static int g_tap_log = 0;      /* log the first few gestures for diagnosis */
/* Axis correction learned by the calibration screen. Some panels (or their
 * mounting) report axes swapped and/or mirrored relative to the framebuffer. */
static int g_cal_swap = 0, g_cal_flip_x = 0, g_cal_flip_y = 0;

void input_set_calib(int swap, int flip_x, int flip_y) {
    g_cal_swap = swap ? 1 : 0;
    g_cal_flip_x = flip_x ? 1 : 0;
    g_cal_flip_y = flip_y ? 1 : 0;
    log_msg("input: calib swap=%d flip_x=%d flip_y=%d",
            g_cal_swap, g_cal_flip_x, g_cal_flip_y);
}

/* Map raw panel coordinates to screen pixels, applying the calibration. */
static void map_xy(const Dev *d, int rawx, int rawy, int *px, int *py) {
    long span_x = d->maxx > d->minx ? d->maxx - d->minx : 1;
    long span_y = d->maxy > d->miny ? d->maxy - d->miny : 1;
    double fx = (double)(rawx - d->minx) / (double)span_x;
    double fy = (double)(rawy - d->miny) / (double)span_y;
    if (fx < 0) fx = 0; if (fx > 1) fx = 1;
    if (fy < 0) fy = 0; if (fy > 1) fy = 1;
    if (g_cal_swap) { double t = fx; fx = fy; fy = t; }
    if (g_cal_flip_x) fx = 1.0 - fx;
    if (g_cal_flip_y) fy = 1.0 - fy;
    int x = (int)(fx * (g_screen_w > 1 ? g_screen_w - 1 : 1));
    int y = (int)(fy * (g_screen_h > 1 ? g_screen_h - 1 : 1));
    *px = x;
    *py = y;
}

static int test_bit(int bit, const unsigned long *bits) {
    return bits[bit / (sizeof(long) * 8)] & (1UL << (bit % (sizeof(long) * 8)));
}

static void abs_caps(int fd, const char *path) {
    unsigned long absbits[ABS_MAX / (sizeof(long) * 8) + 1];
    unsigned long keybits[KEY_MAX / (sizeof(long) * 8) + 1];
    memset(absbits, 0, sizeof(absbits));
    memset(keybits, 0, sizeof(keybits));
    int aok = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0;
    int kok = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0;
    log_msg("input: caps %s abs[x=%d y=%d mtx=%d mty=%d mtid=%d] key[btn_touch=%d]",
            path,
            aok && test_bit(ABS_X, absbits),
            aok && test_bit(ABS_Y, absbits),
#ifdef ABS_MT_POSITION_X
            aok && test_bit(ABS_MT_POSITION_X, absbits),
#else
            0,
#endif
#ifdef ABS_MT_POSITION_Y
            aok && test_bit(ABS_MT_POSITION_Y, absbits),
#else
            0,
#endif
#ifdef ABS_MT_TRACKING_ID
            aok && test_bit(ABS_MT_TRACKING_ID, absbits),
#else
            0,
#endif
            kok && test_bit(BTN_TOUCH, keybits));
}

/* Real screen size, used to map raw touch coordinates. Must be called before
 * input_open(); the default 600x800 is only a last-resort fallback. */
void input_set_screen(int w, int h) {
    if (w > 0) g_screen_w = w;
    if (h > 0) g_screen_h = h;
    log_msg("input: screen %dx%d (touch mapping)", g_screen_w, g_screen_h);
}

/* Read one axis range, preferring the multi-touch axis: some panels (cyttsp5
 * on Oasis 3) expose ABS_X with a bogus 0..1 range while the real pixel range
 * lives on ABS_MT_POSITION_X/Y. */
static void axis_range(int fd, int mt_code, int abs_code, int *mn, int *mx, int *ok) {
    struct input_absinfo ai;
    *ok = 0;
#ifdef ABS_MT_POSITION_X
    if (mt_code >= 0 && ioctl(fd, EVIOCGABS(mt_code), &ai) == 0 &&
        ai.maximum > ai.minimum) {
        *mn = ai.minimum; *mx = ai.maximum; *ok = 1;
        return;
    }
#else
    (void)mt_code;
#endif
    if (abs_code >= 0 && ioctl(fd, EVIOCGABS(abs_code), &ai) == 0 &&
        ai.maximum > ai.minimum) {
        *mn = ai.minimum; *mx = ai.maximum; *ok = 1;
        return;
    }
    *mn = 0;
    *mx = 0;
}

int input_open(void) {
    const char *s = getenv("HIPDA_SCREEN");
    if (s) sscanf(s, "%dx%d", &g_screen_w, &g_screen_h);
    unsigned long bits[EV_MAX / (sizeof(long) * 8) + 1];
    for (int i = 0; i < MAX_DEV && g_ndev < MAX_DEV; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        memset(bits, 0, sizeof(bits));
        if (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), bits) < 0) { close(fd); continue; }
        Dev *d = &g_dev[g_ndev];
        memset(d, 0, sizeof(*d));
        d->fd = fd;
        d->has_abs = test_bit(EV_ABS, bits);
        d->has_key = test_bit(EV_KEY, bits);
        d->is_touch = d->has_abs && d->has_key;
        if (ioctl(fd, EVIOCGNAME(sizeof(d->name)), d->name) < 0) d->name[0] = 0;
        abs_caps(fd, path);
        /* EVIOCGRAB: exclusively claim touch panels so the Amazon UI stops
         * seeing taps (otherwise each tap also pops the status bar). */
        if (d->is_touch) {
            if (ioctl(fd, EVIOCGRAB, 1) == 0)
                log_msg("input: grabbed %s (touch exclusive)", path);
            else
                log_msg("input: grab failed on %s (%s), taps may leak to UI",
                        path, strerror(errno));
        }
        /* Coordinate range: on cyttsp5 (Oasis 3) ABS_X reports 0..1 while the
         * real pixel coordinates live on ABS_MT_POSITION_X/Y. Always prefer
         * the multi-touch axes, fall back to ABS_X/Y, and finally assume the
         * panel reports screen pixels 1:1. */
        int okx = 0, oky = 0;
#ifdef ABS_MT_POSITION_X
        axis_range(fd, ABS_MT_POSITION_X, ABS_X, &d->minx, &d->maxx, &okx);
#else
        axis_range(fd, -1, ABS_X, &d->minx, &d->maxx, &okx);
#endif
#ifdef ABS_MT_POSITION_Y
        axis_range(fd, ABS_MT_POSITION_Y, ABS_Y, &d->miny, &d->maxy, &oky);
#else
        axis_range(fd, -1, ABS_Y, &d->miny, &d->maxy, &oky);
#endif
        /* A range far smaller than the screen (e.g. cyttsp5's bogus 0..1) is
         * useless for mapping; these panels report screen pixels directly. */
        if (okx && g_screen_w > 100 && d->maxx < g_screen_w / 2) okx = 0;
        if (oky && g_screen_h > 100 && d->maxy < g_screen_h / 2) oky = 0;
        if (!okx) { d->minx = 0; d->maxx = g_screen_w > 1 ? g_screen_w - 1 : 65535; }
        if (!oky) { d->miny = 0; d->maxy = g_screen_h > 1 ? g_screen_h - 1 : 65535; }
        if (d->maxx <= d->minx) d->maxx = d->minx + 1;
        if (d->maxy <= d->miny) d->maxy = d->miny + 1;
        log_msg("input: %s (%s) %s range x[%d..%d]%s y[%d..%d]%s", path,
                d->name[0] ? d->name : "?", d->is_touch ? "touch" : "keys",
                d->minx, d->maxx, okx ? "" : "(assumed pixels)",
                d->miny, d->maxy, oky ? "" : "(assumed pixels)");
        g_ndev++;
    }
    return g_ndev ? 0 : -1;
}

static Action map_key(unsigned int code) {
    switch (code) {
        case KEY_PAGEDOWN: case KEY_RIGHT: case KEY_SPACE: case KEY_NEXTSONG:
            return ACT_NEXT;
        case KEY_PAGEUP: case KEY_LEFT: case KEY_PREVIOUSSONG:
            return ACT_PREV;
        case KEY_UP:     return ACT_UP;
        case KEY_DOWN:   return ACT_DOWN;
        case KEY_ENTER: case KEY_OK: case KEY_SELECT: case KEY_REPLY:
            return ACT_SELECT;
        case KEY_BACK: case KEY_ESC: case KEY_BACKSPACE:
            return ACT_BACK;
        case KEY_MENU: case KEY_SEARCH: case KEY_COMPOSE:
            return ACT_MENU;
        case KEY_HOME:   return ACT_HOME;
        case KEY_REFRESH: case KEY_F5: return ACT_REFRESH;
        case KEY_Q:      return ACT_QUIT;
        default:         return ACT_NONE;
    }
}

/* Classify a finished gesture. Swipe page-turning is DISABLED: a drag is
 * simply discarded (returning 0 = "no action") because pushing a whole page
 * to e-ink on every gesture made scrolling feel frozen. Only short touches
 * produce events now.
 * Uses origin + last valid sample, never the (often zeroed) release sample.
 * All coordinates go through map_xy() so the calibration is applied. */
static int classify(Dev *d, InputEvent *ev) {
    int rx0 = d->have_origin ? d->ox : d->lx;
    int ry0 = d->have_origin ? d->oy : d->ly;
    int x0, y0, xe, ye;
    map_xy(d, rx0, ry0, &x0, &y0);
    map_xy(d, d->lx, d->ly, &xe, &ye);

    long dx = (long)xe - x0, dy = (long)ye - y0;
    long adx = dx < 0 ? -dx : dx;
    long ady = dy < 0 ? -dy : dy;

    if (g_tap_log < 12) {
        log_msg("gesture: raw(%d,%d)->screen(%d,%d) drag dx=%ld dy=%ld screen=%dx%d",
                rx0, ry0, x0, y0, (long)dx, (long)dy, g_screen_w, g_screen_h);
        g_tap_log++;
    }

    /* A drag is not a tap: drop it, and do NOT emit any page-turn action. */
    if (adx > g_screen_w / 12 || ady > g_screen_h / 14) {
        if (g_tap_log < 12) {
            log_msg("gesture: drag ignored (no page turn)");
            g_tap_log++;
        }
        return 0;
    }

    ev->act = ACT_SELECT;
    ev->tap = 1;
    ev->x = x0;
    ev->y = y0;
    ev->raw_x = rx0;
    ev->raw_y = ry0;
    if (g_tap_log < 12) {
        log_msg("tap: screen(%d,%d) raw(%d,%d)", x0, y0, rx0, ry0);
        g_tap_log++;
    }
    return 1;
}

int input_read(InputEvent *ev, int timeout_ms) {
    memset(ev, 0, sizeof(*ev));
    ev->act = ACT_NONE;
    fd_set set;
    int maxfd = -1;
    FD_ZERO(&set);
    for (int i = 0; i < g_ndev; i++) {
        FD_SET(g_dev[i].fd, &set);
        if (g_dev[i].fd > maxfd) maxfd = g_dev[i].fd;
    }
    if (maxfd < 0) return 0;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(maxfd + 1, &set, NULL, NULL, timeout_ms >= 0 ? &tv : NULL);
    if (r <= 0) return 0;
    for (int i = 0; i < g_ndev; i++) {
        Dev *d = &g_dev[i];
        if (!FD_ISSET(d->fd, &set)) continue;
        struct input_event ie;
        while (read(d->fd, &ie, sizeof(ie)) == sizeof(ie)) {
            if (ie.type == EV_ABS) {
                int is_x = 0, is_y = 0;
                if (ie.code == ABS_X) { d->x = ie.value; is_x = 1; }
                else if (ie.code == ABS_Y) { d->y = ie.value; is_y = 1; }
#ifdef ABS_MT_POSITION_X
                else if (ie.code == ABS_MT_POSITION_X) { d->x = ie.value; is_x = 1; }
#endif
#ifdef ABS_MT_POSITION_Y
                else if (ie.code == ABS_MT_POSITION_Y) { d->y = ie.value; is_y = 1; }
#endif
#ifdef ABS_MT_TRACKING_ID
                else if (ie.code == ABS_MT_TRACKING_ID) {
                    if (ie.value >= 0) {
                        d->pressed = 1; d->have_origin = 0; d->lift = 0;
                        d->press_sent = 0;
                    } else if (d->pressed) {
                        d->pressed = 0; d->lift = 1;
                    }
                }
#endif
                if ((is_x || is_y) && d->pressed) {
                    if (!d->have_origin) {
                        /* first sample of this contact becomes the origin */
                        d->ox = d->x; d->oy = d->y; d->have_origin = 1;
                    }
                    d->lx = d->x; d->ly = d->y;
                }
            } else if (ie.type == EV_KEY) {
                if (ie.code == BTN_TOUCH || ie.code == BTN_LEFT) {
                    if (ie.value == 1) {
                        d->pressed = 1; d->have_origin = 0; d->lift = 0;
                        d->press_sent = 0;
                    } else if (ie.value == 0 && d->pressed) {
                        d->pressed = 0; d->lift = 1;
                    }
                } else if (ie.value == 1) {
                    Action a = map_key(ie.code);
                    if (a != ACT_NONE) { ev->act = a; ev->tap = 0; return 1; }
                }
            } else if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
                if (d->pressed && !d->have_origin) {
                    d->ox = d->x; d->oy = d->y; d->have_origin = 1;
                    d->lx = d->x; d->ly = d->y;
                }
                if (d->lift) {
                    d->lift = 0;
                    d->press_sent = 0;
                    if (classify(d, ev)) return 1;
                } else if (d->pressed && !d->press_sent && d->have_origin) {
                    /* Finger down: hand the UI a press event right away so it
                     * can invert the touched row (visible feedback). */
                    d->press_sent = 1;
                    int px, py;
                    map_xy(d, d->ox, d->oy, &px, &py);
                    ev->act = ACT_PRESS;
                    ev->tap = 1;
                    ev->x = px;
                    ev->y = py;
                    ev->raw_x = d->ox;
                    ev->raw_y = d->oy;
                    return 1;
                }
            }
        }
    }
    return 0;
}

void input_close(void) {
    for (int i = 0; i < g_ndev; i++) {
        ioctl(g_dev[i].fd, EVIOCGRAB, 0);   /* release before close */
        close(g_dev[i].fd);
    }
    g_ndev = 0;
}
