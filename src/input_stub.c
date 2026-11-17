/* PC input backend: keyboard for the CLI demo.
 *   n / space / >  -> next page      p / <  -> previous page
 *   b -> back       m -> menu        r -> refresh      q -> quit
 *   ENTER -> select                 t X Y -> simulate a tap at (X,Y)
 */
#include "input.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int input_open(void) { return 0; }
void input_set_screen(int w, int h) { (void)w; (void)h; }
void input_set_calib(int swap, int flip_x, int flip_y) {
    (void)swap; (void)flip_x; (void)flip_y;
}
void input_close(void) {}

int input_read(InputEvent *ev, int timeout_ms) {
    (void)timeout_ms;
    memset(ev, 0, sizeof(*ev));
    char line[128];
    if (!fgets(line, sizeof(line), stdin)) { ev->act = ACT_QUIT; return 1; }
    switch (line[0]) {
        case 'n': case ' ': case '>': case '\n': ev->act = ACT_NEXT; break;
        case 'p': case '<':           ev->act = ACT_PREV; break;
        case 'b':                     ev->act = ACT_BACK; break;
        case 'm':                     ev->act = ACT_MENU; break;
        case 'r':                     ev->act = ACT_REFRESH; break;
        case 'q': case 'x':           ev->act = ACT_QUIT; break;
        case 'h':                     ev->act = ACT_HOME; break;
        case 'u':                     ev->act = ACT_UP; break;
        case 'd':                     ev->act = ACT_DOWN; break;
        case 's': case '\r':          ev->act = ACT_SELECT; break;
        case 't': {
            int x = 0, y = 0;
            if (sscanf(line + 1, "%d %d", &x, &y) == 2) {
                ev->act = ACT_SELECT; ev->tap = 1; ev->x = x; ev->y = y;
            }
            break;
        }
        default: ev->act = ACT_NONE; break;
    }
    return 1;
}
