/* hipda-kindle -- a native, KUAL-launched HiPDA / Discuz! 7.2 reader
 * for jailbroken Kindles. Shared data types.
 */
#ifndef HIPDA_H
#define HIPDA_H

#include <stddef.h>
#include <stdint.h>

#define HIPDA_VERSION       "0.9.0"
#define HIPDA_FORUM_PATH    "/forum/"
#define HIPDA_NAME_LEN      256
#define HIPDA_SHORT_LEN     96
#define HIPDA_URL_LEN       1024
#define HIPDA_PATH_LEN      512

/* Growable UTF-8 string. Always NUL terminated. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Str;

typedef struct {
    int  fid;
    char name[64];
} Forum;

typedef struct {
    int  tid;
    char title[HIPDA_NAME_LEN];
    char author[HIPDA_SHORT_LEN];
    char dateline[48];
    char lastpost[64];
    char type[48];
    int  replies;
    int  views;
    int  is_sticky;
    int  has_attach;
} Topic;

typedef struct {
    Topic *v;
    int    n, cap;
    int    fid;
    int    page, last_page;
} TopicList;

typedef struct {
    int  floor;
    char author[HIPDA_SHORT_LEN];
    char dateline[48];
    char pid[32];
    Str  body;               /* UTF-8, LF separated */
} Post;

typedef struct {
    int   tid;
    char  title[HIPDA_NAME_LEN];
    Post *posts;
    int   nposts, cap;
    int   page, last_page;
} ThreadPage;

typedef struct {
    char forum_server[HIPDA_NAME_LEN];
    char image_host[HIPDA_NAME_LEN];
    char username[HIPDA_SHORT_LEN];
    char password_md5[64];
    char cookie_file[HIPDA_PATH_LEN];
    char font_file[HIPDA_PATH_LEN];
    char cache_dir[HIPDA_PATH_LEN];
    char config_path[HIPDA_PATH_LEN];
    char net_cmd[32];
    char user_agent[HIPDA_SHORT_LEN];
    int  font_scale;         /* 0 = derive from screen DPI */
    int  full_refresh_every; /* pages between GC16 full flashes */
    int  timeout;
    int  forums[64];
    int  nforums;
    int  offline;
    int  no_login;
    int  theme;              /* 0 = white paper / black ink, 1 = inverted */
    int  cal_swap;           /* touch calibration: axes swapped         */
    int  cal_flip_x;         /* touch calibration: horizontal mirrored  */
    int  cal_flip_y;         /* touch calibration: vertical mirrored    */
    int  refresh_mode;       /* 0 all / 1 partial / 2 regal / 3 hybrid  */
    int  rotation;           /* content rotation CW: 0/90/180/270      */
    int  img_mode;           /* 0 = hide images, 1 = tap-to-view       */
} Config;

/* Screen geometry, filled by the active UI backend. All type sizes follow
 * the SVG mockups (600x800 reference) scaled to the real screen: the body
 * text height is 16 * w/600, and every other size is its SVG ratio of that. */
typedef struct {
    int w, h;
    int dpi;
    int scale;       /* line-stroke unit: ~w/400, kept for rules/borders */
    int cell_w;      /* half-width of one body character = body_h / 2   */
    int cell_h;      /* body text height                                */
    int line_h;      /* cell_h + leading (SVG 25/16 ratio)              */
    int margin_x;
    int margin_y;
    int cols;        /* printable cells per line */
    int rows;        /* printable lines per page (footer reserved) */
    int top_h;       /* toolbar rule y  = 82 * h/800  */
    int foot_y;      /* footer rule y   = 722 * h/800 */
    int head_h;      /* toolbar title height (SVG fs21) */
    int title_h;     /* card/list title height (SVG fs18) */
    int sub_h;       /* subtitle / meta height (SVG fs13) */
    int small_h;     /* hints (SVG fs12)                */
    int foot_h;      /* footer text height (SVG fs15)   */
} Geometry;

#endif /* HIPDA_H */
