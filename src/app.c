#include "app.h"

#include "config.h"
#include "discuz.h"
#include "font.h"
#include "gbk.h"
#include "html.h"
#include "input.h"
#include "layout.h"
#include "md5.h"
#include "net.h"
#include "image.h"
#include "pinyin.h"
#include "render.h"
#include "ui.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PC_BUILD
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef enum { V_FORUMS, V_TOPICS, V_THREAD, V_MESSAGE,
               V_MENU, V_TEXT, V_LOGIN, V_SETTINGS, V_CALIB,
               V_FAVLIST } View;

/* The "Hi" menu. Order matters: item_id == index + 1. */
static const char *kMenuItems[] = {
    "论坛（返回首页）",
    "搜索帖子",
    "我的帖子",
    "我的回复",
    "我的收藏",
    "收藏离线缓存",
    "短消息",
    "帖子消息",
    "设置",
    "切换主题（白底 / 黑底）",
    "退出应用"
};
#define MENU_N ((int)(sizeof(kMenuItems) / sizeof(kMenuItems[0])))
#define MENU_FAVOURITES 5
#define MENU_FAV_CACHE  6
#define MENU_PM         7
#define MENU_NOTICE     8
#define MENU_SETTINGS   9
#define MENU_THEME      10
#define MENU_QUIT       11

#define SETTINGS_N 11

/* On-screen keyboard, three pages: lower-case, upper-case, digits/symbols.
 * Each key takes 3 cells (character + 2 blanks) so the keys are far enough
 * apart to hit reliably. Page/Shift/Space/Backspace/OK/Cancel live in a
 * separate control row (item_id 100) with its own 4-cell grid. */
static const char *kKeyRows[] = {
    /* page 0: lower case */
    "a  b  c  d  e  f  g  h  i  j",
    "k  l  m  n  o  p  q  r  s  t",
    "u  v  w  x  y  z  ,  .  ?  !",
    /* page 1: upper case */
    "A  B  C  D  E  F  G  H  I  J",
    "K  L  M  N  O  P  Q  R  S  T",
    "U  V  W  X  Y  Z  ,  .  ?  !",
    /* page 2: digits and symbols */
    "0  1  2  3  4  5  6  7  8  9",
    "-  _  =  +  /  \\  :  ;  (  )",
    "@  #  $  %  &  *  [  ]  {  }"
};
#define KEY_ROWS_PER_PAGE 3
#define KEY_PAGES ((int)(sizeof(kKeyRows) / sizeof(kKeyRows[0]) / KEY_ROWS_PER_PAGE))
#define KEY_CTRL_ID 100
#define KEY_CAND_ID 101        /* pinyin candidate row */
#define KEY_PINYIN_PAGE 3      /* kb_page 3 = Chinese input (search only) */
/* control row buttons, left to right (6 buttons, 6 cells each — see layout.h) */
#define KEY_CTRL_SH 0
#define KEY_CTRL_PG 1
#define KEY_CTRL_SP 2
#define KEY_CTRL_BK 3
#define KEY_CTRL_OK 4
#define KEY_CTRL_XX 5

/* Parsed-screen snapshot. Returning from a thread restores the list instantly
 * instead of re-fetching and re-parsing it. Books are swapped, not copied. */
typedef struct {
    Book book;
    int  valid;
    int  key;          /* fid for a topic list, tid for a thread */
    int  page;         /* forum page this snapshot belongs to */
    int  last;         /* last forum page known at capture time */
    int  page_index;
    int  sel_line;
} Snap;

typedef struct {
    Config   cfg;
    Font     font;
    Canvas   canvas;
    Geometry geo;
    UiInfo   ui;
    Book     book;
    Snap     snap_topics;              /* topic list, restored on back */
    Snap     snap_thread;              /* thread, reopened instantly */
    View     view;
    View     view_before_msg;
    int      page_index;
    int      sel_line;
    int      cur_fid, cur_tid;
    int      in_fav;                   /* thread was opened from the favourites list */
    int      topic_page, topic_last;
    int      thread_page, thread_last;
    int      thread_partial;          /* 1 = showing the first chunk while the
                                       * rest of a large thread downloads */
    int      last_fetch_partial;      /* set by fetch_or_message for the caller */
    int      since_flash;
    int      export_preview;
    int      msg_action;               /* 1 = message view is a quit-confirm */
    int      refresh_phase;            /* rotates the toolbar ring */
    int      press_line;               /* row currently under the finger, or -1 */
    int      cal_step;                 /* calibration: 0..3 corners done */
    int      cal_raw[4][2];            /* calibration: raw coords per corner */
    int      input_mode;               /* 1 = login username, 2 = password, 3 = search */
    int      login_field;              /* 0 = username, 1 = password */
    int      kb_page;                 /* keyboard page: 0=lower 1=upper 2=symbol 3=pinyin */
    char     pinyin_buf[48];          /* typed pinyin letters (Chinese page) */
    int      cand_page;               /* candidate list page (7 per page)    */
    int      kb_shift_once;           /* one-shot capitalise after Shift on lower page */
    int      quit_now;                 /* set by the Quit menu item */
    char     login_user[HIPDA_SHORT_LEN];
    char     login_pass[128];
    char     search_term[128];
    Str      img_urls;                 /* URLs of the current thread's images, in order */
    Str      list_urls;                /* relative URLs of the current msg/notice list */
    int      list_kind;                /* 0 = thread list, 1 = url list (pm/notice)     */
    char     msg_title[HIPDA_NAME_LEN];
    char     msg_body[1024];
#ifdef PC_BUILD
    FILE    *preview;
    int      preview_n;
#endif
} App;

#ifdef PC_BUILD
static void pw_begin(App *a) {
    char dir[HIPDA_PATH_LEN] = "preview";
    const char *env = getenv("HIPDA_PREVIEW_DIR");
    if (env && *env) str_copy(dir, sizeof(dir), env);
    dir_ensure(dir);
    char path[HIPDA_PATH_LEN];
    snprintf(path, sizeof(path), "%s/pages.json", dir);
    a->preview = fopen(path, "wb");
    a->preview_n = 0;
    if (!a->preview) return;
    fputs("[\n", a->preview);
}
static void pw_page(App *a, const char *view) {
    if (!a->preview) return;
    const Book *b = &a->book;
    const Page *pg = &b->pages[a->page_index];
    fprintf(a->preview, "%s{\"view\": \"%s\", \"title\": ", a->preview_n ? ",\n" : "", view);
    fputc('"', a->preview);
    for (const unsigned char *p = (const unsigned char *)b->title; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', a->preview); fputc(*p, a->preview); }
        else if (*p == '\n') fputs("\\n", a->preview);
        else if (*p < 0x20) fprintf(a->preview, "\\u%04x", *p);
        else fputc(*p, a->preview);
    }
    fprintf(a->preview, "\", \"page\": %d, \"total\": %d, \"forum_page\": %d, \"png\": \"page_%03d.png\", \"lines\": [",
            a->page_index + 1, b->np, pg->forum_page, a->preview_n);
    for (int i = 0; i < pg->count; i++) {
        const Line *l = &b->lines[pg->start + i];
        const char *k = l->kind == L_ITEM ? "item" : l->kind == L_META ? "meta" :
                        l->kind == L_TITLE ? "title" : l->kind == L_SEP ? "sep" :
                        l->kind == L_NOTE ? "note" : l->kind == L_BODY ? "body" : "blank";
        fprintf(a->preview, "%s{\"kind\": \"%s\", \"text\": \"", i ? ", " : "", k);
        for (const unsigned char *p = (const unsigned char *)l->text; *p; p++) {
            if (*p == '"' || *p == '\\') { fputc('\\', a->preview); fputc(*p, a->preview); }
            else if (*p == '\n') fputs("\\n", a->preview);
            else if (*p < 0x20) fprintf(a->preview, "\\u%04x", *p);
            else fputc(*p, a->preview);
        }
        fprintf(a->preview, "\", \"item_id\": %d, \"indent\": %d}", l->item_id, l->indent);
    }
    fputs("]}", a->preview);
    a->preview_n++;
}
static void pw_end(App *a) {
    if (!a->preview) return;
    fputs("\n]\n", a->preview);
    fclose(a->preview);
    a->preview = NULL;
    log_msg("preview: %d screens -> preview/pages.json", a->preview_n);
}
#else
static void pw_begin(App *a) { (void)a; }
static void pw_page(App *a, const char *v) { (void)a; (void)v; }
static void pw_end(App *a) { (void)a; }
#endif

/* Screen self-test: no font, no network, no config parsing. Draws a pattern
 * made only of rectangles so we can tell a dead framebuffer / e-ink refresh
 * path from a font or parser problem. Also drops a log file on /mnt/us so the
 * user can email it back over USB. */
static int app_diag(void) {
    UiInfo ui;
    int rc = 1;
    char logpath[256] = "/mnt/us/extensions/hipda/diag.txt";
    if (!file_exists("/mnt/us/extensions/hipda")) {
        str_copy(logpath, sizeof(logpath), "diag.txt");
    }
    log_set_file(logpath);
    log_msg("=== HiPDA display self-test v%s ===", HIPDA_VERSION);
    log_msg("log file: %s", logpath);

    memset(&ui, 0, sizeof(ui));
    if (ui_init(&ui)) {
        log_msg("FAIL: ui_init returned error (framebuffer open/init)");
        log_close();
        return 1;
    }
    log_msg("ui: %dx%d @%ddpi", ui.w, ui.h, ui.dpi);

    Geometry g;
    geometry_compute(&g, ui.w, ui.h, ui.dpi, 0);
    log_msg("geometry: %dx%d scale=%d cols=%d rows=%d line_h=%d",
            g.w, g.h, g.scale, g.cols, g.rows, g.line_h);

    Canvas c;
    if (canvas_init(&c, g.w, g.h)) {
        log_msg("FAIL: canvas_init");
        ui_close(); log_close();
        return 1;
    }

    canvas_fill(&c, 255);
    canvas_frame(&c, 0, 0, g.w, g.h, 0, 8);
    for (int band = 1; band <= 3; band++) {
        int y = g.h * band / 4;
        canvas_rect(&c, 0, y, g.w, g.h / 32, 0);
    }
    int cy = g.h / 2;
    for (int x = 0; x < g.w; x += 32) {
        canvas_rect(&c, x, cy - 40, 16, 80, 0);
    }
    log_msg("pattern drawn: %dx%d, %d byte pixels, first=%u mid=%u last=%u",
            c.w, c.h, c.w * c.h,
            c.px[0], c.px[(size_t)(c.w * c.h) / 2], c.px[(size_t)c.w * c.h - 1]);

    int r1 = ui_present(c.px, c.w, c.h, 1);
    log_msg("ui_present(full flash) -> %d", r1);
    int r2 = ui_present(c.px, c.w, c.h, 0);
    log_msg("ui_present(partial)    -> %d", r2);
    log_msg("RESULT: %s", (r1 == 0 && r2 == 0) ? "OK - if the screen is still blank, the problem is the e-ink panel/waveform, not the app" : "FAIL - FBInk rejected the bitmap (see errors above)");

    canvas_free(&c);
    ui_close();
    log_close();
    rc = (r1 == 0 && r2 == 0) ? 0 : 1;
    return rc;
}

static const char *view_name(View v) {
    switch (v) {
        case V_FORUMS:   return "forums";
        case V_TOPICS:   return "topics";
        case V_THREAD:   return "thread";
        case V_MENU:     return "menu";
        case V_TEXT:     return "text";
        case V_LOGIN:    return "login";
        case V_SETTINGS: return "settings";
        case V_FAVLIST:  return "favlist";
        default:         return "message";
    }
}

static void app_draw(App *a, const char *flash_msg) {
    if (a->book.np <= 0) return;
    if (a->page_index < 0) a->page_index = 0;
    if (a->page_index >= a->book.np) a->page_index = a->book.np - 1;
    int flash = (a->since_flash >= a->cfg.full_refresh_every || a->since_flash == 0);
    if (a->view == V_MESSAGE) {
        render_message(&a->canvas, &a->font, &a->geo, a->msg_title, a->msg_body,
                       a->cfg.theme);
    } else {
        render_page(&a->canvas, &a->font, &a->geo, &a->book,
                    a->page_index, a->sel_line, a->press_line, flash_msg,
                    a->cfg.theme, a->refresh_phase);
    }
    ui_present(a->canvas.px, a->canvas.w, a->canvas.h, flash);
    a->since_flash = flash ? 1 : a->since_flash + 1;
#ifdef PC_BUILD
    if (a->export_preview) pw_page(a, view_name(a->view));
#endif
}

/* Push one row's pixels to the panel (used by press/selection feedback). */
static void present_row(App *a, int row) {
    if (row < 0 || a->book.np <= 0) return;
    if (a->page_index < 0 || a->page_index >= a->book.np) return;
    const Page *pg = &a->book.pages[a->page_index];
    if (row >= pg->count) return;
    int y = a->geo.margin_y + a->geo.line_h + row * a->geo.line_h - a->geo.scale;
    int hgt = a->geo.cell_h + 2 * a->geo.scale;
    int x = a->geo.margin_x - a->geo.scale;
    int w = a->geo.w - 2 * x;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { hgt += y; y = 0; }
    if (y + hgt > a->geo.h) hgt = a->geo.h - y;
    if (w > 0 && hgt > 0)
        ui_present_region(a->canvas.px, a->canvas.w, x, y, w, hgt, 0);
}

/* Press feedback for one row. Redraws the canvas in memory, then pushes ONLY
 * that row to the panel — a full-page refresh mid-touch is what made the UI
 * feel frozen. show_press=0 restores the row. */
static void app_repaint_row(App *a, int row, int show_press) {
    if (a->book.np <= 0) return;
    if (a->page_index < 0 || a->page_index >= a->book.np) return;
    const Page *pg = &a->book.pages[a->page_index];
    if (row < 0 || row >= pg->count) {
        a->press_line = -1;
        return;
    }
    a->press_line = show_press ? row : -1;
    render_page(&a->canvas, &a->font, &a->geo, &a->book,
                a->page_index, a->sel_line, a->press_line, NULL,
                a->cfg.theme, a->refresh_phase);
    present_row(a, row);
}

/* Selection moved: repaint just the old and new rows instead of the page. */
static void app_repaint_rows(App *a, int r1, int r2) {
    if (a->book.np <= 0) return;
    if (a->page_index < 0 || a->page_index >= a->book.np) return;
    render_page(&a->canvas, &a->font, &a->geo, &a->book,
                a->page_index, a->sel_line, a->press_line, NULL,
                a->cfg.theme, a->refresh_phase);
    present_row(a, r1);
    if (r2 != r1) present_row(a, r2);
}

static void app_message(App *a, const char *title, const char *body) {
    a->view_before_msg = a->view;
    a->view = V_MESSAGE;
    str_copy(a->msg_title, sizeof(a->msg_title), title);
    str_copy(a->msg_body, sizeof(a->msg_body), body);
    if (a->book.np > 0) {
        app_draw(a, NULL);
    } else {
        /* first screen ever (e.g. "loading..." before the first fetch): there
         * is no book yet, so paint the message directly */
        render_message(&a->canvas, &a->font, &a->geo, a->msg_title,
                       a->msg_body, a->cfg.theme);
        ui_present(a->canvas.px, a->canvas.w, a->canvas.h, 1);
    }
}

/* ------------------------------------------------------------------ async --
 * Fetching a page must not freeze the UI: on a slow forum curl can take 20s.
 * We fork a child that does the network work (and nothing else), then the
 * parent polls with a short input timeout so a tap cancels immediately.
 * The child writes the UTF-8 body to a per-parent-pid file and encodes the
 * result in its exit status.
 */
#define JOB_CANCELLED (-100)
#define JOB_PARTIAL   (-101)   /* early chunk shown; full download continues */

/* A large thread is a ~250 KB HTML download, which is the only genuinely
 * slow step (parse+layout take ~1 ms). While the child curl streams into its
 * temp file we peek at the partial bytes; once enough complete posts exist
 * to fill a screen we hand that chunk back immediately, so the reader sees
 * page 1 right away while the rest keeps downloading into the cache. */
/* child pid still filling the cache in background (int: no sys/types.h on PC) */
static int g_bg_download = -1;

static void reap_background(void) {
#ifdef PC_BUILD
    (void)0;                          /* no fork() in the PC preview build */
#else
    if (g_bg_download <= 0) return;
    int st;
    pid_t r = waitpid(g_bg_download, &st, WNOHANG);
    if (r == g_bg_download || (r < 0 && errno == ECHILD)) {
        log_msg("background download finished (pid=%d)", (int)g_bg_download);
        g_bg_download = -1;
    }
#endif
}

#ifndef PC_BUILD
/* Try the child's in-progress temp file. Returns 1 and fills `out` when it
 * already holds at least `min_posts` parseable floors. */
static int try_early_chunk(App *a, pid_t child, int min_posts, Str *out) {
    char pp[HIPDA_PATH_LEN];
    snprintf(pp, sizeof(pp), "%s/resp_%d.html", a->cfg.cache_dir, (int)child);
    Str raw; str_init(&raw);
    if (file_read(pp, &raw) || raw.len < 4096) { str_free(&raw); return 0; }
    int utf8 = discuz_detect_utf8(raw.data, raw.len);
    Str txt; str_init(&txt);
    text_decode(raw.data, raw.len, utf8, &txt);
    str_free(&raw);
    ThreadPage probe;
    int np = discuz_parse_thread(txt.data, &probe, 0, 1);
    thread_page_free(&probe);
    if (np < min_posts || !txt.len) { str_free(&txt); return 0; }
    str_clear(out);
    str_append(out, txt.data);
    str_append(out, "");
    str_free(&txt);
    log_msg("early thread chunk: %d posts ready", np);
    return 1;
}
#endif

#ifdef PC_BUILD
/* The PC preview build has no fork(): fetch synchronously. Cancellation only
 * matters on the device, where a slow curl would otherwise freeze the UI. */
static int fetch_async(App *a, const char *url, Str *html) {
    str_clear(html);
    return net_fetch(&a->cfg, url, html, 1);
}
#else
static int fetch_async(App *a, const char *url, Str *html) {
    str_clear(html);
    char path[HIPDA_PATH_LEN];
    snprintf(path, sizeof(path), "%s/job_%d.html", a->cfg.cache_dir, (int)getpid());
    unlink(path);

    pid_t pid = fork();
    if (pid == 0) {
        /* child: own process group so cancel can kill curl too */
        setpgid(0, 0);
        Config c = a->cfg;
        net_init(&c);                 /* fresh temp filename for this pid */
        Str out; str_init(&out);
        int r = net_fetch(&c, url, &out, 1);
        if (r == NET_OK && out.len) {
            FILE *f = fopen(path, "wb");
            if (!f) r = NET_ERR;
            else {
                if (fwrite(out.data, 1, out.len, f) != out.len) r = NET_ERR;
                if (fclose(f)) r = NET_ERR;
            }
        }
        str_free(&out);
        _exit(r == NET_OK ? 0 : 2 + (-r));      /* 0 ok, 3..7 = net error */
    }
    if (pid < 0) return NET_ERR;

    int st = 0;
    int result = 0;                  /* 0 = still running */
    int polls = 0;
    /* Only thread bodies benefit from an early chunk; lists must stay whole. */
    int early_ok = strstr(url, "viewthread.php") != NULL;
    /* enough posts to fill a screen: scale with available rows */
    int min_posts = a->geo.rows > 0 ? (a->geo.rows / 4) : 8;
    if (min_posts < 6) min_posts = 6;
    if (min_posts > 12) min_posts = 12;
    while (result == 0) {
        pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) {
            if (WIFEXITED(st) && WEXITSTATUS(st) == 0) result = NET_OK;
            else if (WIFEXITED(st)) result = -(WEXITSTATUS(st) - 2);
            else result = NET_ERR;
            break;
        }
        if (w < 0 && errno != EINTR) { result = NET_ERR; break; }
        InputEvent ev;
        if (input_read(&ev, 150) > 0) {
            if (ev.act == ACT_QUIT) { result = JOB_CANCELLED; break; }
            /* Ignore finger-down: only a completed tap (or a key) cancels, so
             * merely resting a finger on the screen does not abort the load. */
            if (ev.act == ACT_PRESS) continue;
            if (ev.tap || ev.act == ACT_BACK || ev.act == ACT_MENU ||
                ev.act == ACT_HOME) {
                result = JOB_CANCELLED;
                break;
            }
        }
        /* peek at the streaming download every ~600 ms; once a screenful of
         * complete posts is available, show it and keep downloading */
        if (early_ok && (++polls % 4) == 0 &&
            try_early_chunk(a, pid, min_posts, html)) {
            g_bg_download = pid;      /* reaped later in the main loop */
            log_msg("fetch: early chunk served, full download continues in bg");
            return JOB_PARTIAL;
        }
    }

    if (result == JOB_CANCELLED) {
        kill(-pid, SIGTERM);         /* child + its curl */
        kill(pid, SIGTERM);
        waitpid(pid, &st, 0);
        unlink(path);
        log_msg("fetch: cancelled by user (%s)", url);
        return JOB_CANCELLED;
    }
    if (result == NET_OK) {
        int rd = file_read(path, html);
        unlink(path);
        if (rd || !html->len) return NET_EMPTY;
        return NET_OK;
    }
    unlink(path);
    log_msg("fetch: error %d (%s)", result, url);
    return result;
}
#endif /* !PC_BUILD */

/* Show the failure screen in place (without touching view_before_msg, which
 * still points at the screen we came from). Returns -1, or -2 if cancelled. */
static int fetch_or_message(App *a, const char *url, Str *html, int refresh) {
    (void)refresh;
    a->last_fetch_partial = 0;
    int r = fetch_async(a, url, html);
    if (r == JOB_CANCELLED) return -2;
    if (r == JOB_PARTIAL) a->last_fetch_partial = 1;
    if (r == NET_OK || r == JOB_PARTIAL) return 0;
    const char *title = "无法读取论坛";
    char body[1024];
    if (r == NET_GATE) {
        title = "需要登录";
        snprintf(body, sizeof(body),
                 "论坛返回「您还未登录，无权访问该版块」。\n\n"
                 "请用顶部 Hi 菜单 → 设置 → 登录 / 切换账号。\n\n"
                 "URL: %s", url);
    } else if (r == NET_CF) {
        title = "遇到 Cloudflare 验证";
        snprintf(body, sizeof(body),
                 "论坛对本次请求返回了 JS 挑战页。\n\n"
                 "可在电脑浏览器登录论坛后，把 Netscape 格式的\n"
                 "cookie 导出为 cookie_file 指向的文件再试。\n\n"
                 "URL: %s", url);
    } else if (r == NET_OFFLINE) {
        title = "离线且无缓存";
        snprintf(body, sizeof(body), "当前是离线模式，本机没有这一页的缓存。\n\nURL: %s", url);
    } else {
        snprintf(body, sizeof(body), "网络请求失败（代码 %d）。\n请检查 WiFi 与论坛地址。\n\nURL: %s", r, url);
    }
    a->view = V_MESSAGE;
    str_copy(a->msg_title, sizeof(a->msg_title), title);
    str_copy(a->msg_body, sizeof(a->msg_body), body);
    if (a->book.np > 0) app_draw(a, NULL);
    else {
        render_message(&a->canvas, &a->font, &a->geo, a->msg_title, a->msg_body,
                       a->cfg.theme);
        ui_present(a->canvas.px, a->canvas.w, a->canvas.h, 1);
    }
    return -1;
}

/* Cancelled mid-fetch: drop back to the screen we came from. */
static void resume_after_cancel(App *a) {
    a->view = a->view_before_msg;
    if (a->book.np > 0) app_draw(a, "已取消");
}

/* ---- parsed-screen snapshots -------------------------------------------
 * Books are swapped in/out rather than copied: the live Book takes the
 * snapshot's buffers and the snapshot takes the old ones. Zero allocation,
 * so going back is instant even for a long thread.
 */
static void snap_put(Snap *s, Book *live, int key, int page, int last,
                     int page_index, int sel_line) {
    if (live->n <= 0 || live->n > 8000) {   /* too big to keep around */
        s->valid = 0;
        return;
    }
    Book t = *live;
    *live = s->book;
    s->book = t;
    s->key = key;
    s->page = page;
    s->last = last;
    s->page_index = page_index;
    s->sel_line = sel_line;
    s->valid = 1;
}

static int snap_take(Snap *s, Book *live, int key, int page,
                     int *last, int *page_index, int *sel_line) {
    if (!s->valid || s->key != key || s->page != page) return 0;
    Book t = *live;
    *live = s->book;
    s->book = t;
    s->valid = 0;                 /* consumed; the old buffers are scratch now */
    *last = s->last;
    *page_index = s->page_index;
    *sel_line = s->sel_line;
    return 1;
}

static void snap_invalidate_all(App *a) {
    a->snap_topics.valid = 0;
    a->snap_thread.valid = 0;
}

static void show_forums(App *a) {
    book_from_forums(&a->book, &a->geo, &a->cfg);
    a->view = V_FORUMS;
    a->page_index = 0; a->sel_line = 0;
    app_draw(a, NULL);
}

static int show_topics(App *a, int fid, int page, int append, int refresh) {
    /* Going back to a list we already parsed: swap it in, no fetch, no parse. */
    if (!append && !refresh) {
        int last, pi, sel;
        if (snap_take(&a->snap_topics, &a->book, fid, page, &last, &pi, &sel)) {
            a->view = V_TOPICS;
            a->cur_fid = fid;
            a->topic_page = page;
            a->topic_last = last;
            a->page_index = pi;
            a->sel_line = sel;
            app_draw(a, NULL);
            return 0;
        }
    }
    if (!append)
        app_message(a, "正在加载…", "正在从论坛读取主题列表。\n点任意位置可取消。");
    char url[HIPDA_URL_LEN];
    net_url_forum(url, sizeof(url), &a->cfg, fid, page);
    Str html; str_init(&html);
    int fr = fetch_or_message(a, url, &html, refresh);
    if (fr) {
        str_free(&html);
        if (fr == -2 && !append) resume_after_cancel(a);
        return -1;
    }
    TopicList list;
    int n = discuz_parse_topic_list(html.data, &list, fid, page);
    str_free(&html);
    if (n <= 0) { topic_list_free(&list); app_message(a, "没有解析到主题", "论坛页面结构可能已改版。\n用 --dump 抓一页对照 src/discuz.c 的规则表。"); return -1; }
    const Forum *f = forum_by_fid(fid);
    book_from_topics(&a->book, &a->geo, &list, f, append);
    a->cur_fid = fid;
    a->topic_page = page;
    a->topic_last = list.last_page;
    topic_list_free(&list);
    a->view = V_TOPICS;
    if (!append) { a->page_index = 0; a->sel_line = 1; }
    app_draw(a, NULL);
    return 0;
}

static int show_thread(App *a, int tid, int page, int append, int refresh) {
    /* Re-opening a thread we already parsed: instant, straight from memory. */
    if (!append && !refresh) {
        int last, pi, sel;
        if (snap_take(&a->snap_thread, &a->book, tid, page, &last, &pi, &sel)) {
            a->view = V_THREAD;
            a->cur_tid = tid;
            a->thread_page = page;
            a->thread_last = last;
            a->page_index = pi;
            a->sel_line = sel;
            app_draw(a, NULL);
            return 0;
        }
    }
    int from_topics = (!append && a->view == V_TOPICS);
    int from_fav = (!append && a->view == V_FAVLIST);
    (void)from_fav;
    /* Keep the list so Back is instant — but only once the loading screen is
     * painted (or skipped entirely on a cache hit), because the swap leaves
     * a scratch Book in a->book. Favourites rebuild their own list on back. */
    if (from_topics)
        snap_put(&a->snap_topics, &a->book, a->cur_fid, a->topic_page,
                 a->topic_last, a->page_index, a->sel_line);
    char url[HIPDA_URL_LEN];
    net_url_thread(url, sizeof(url), &a->cfg, tid, page);
    Str html; str_init(&html);
    /* cache hit = instant open, no loading flash; refresh (top-right ring)
     * always goes to the network */
    int from_cache = (!refresh && net_peek(&a->cfg, url, &html) == NET_OK);
    int fr = from_cache ? 0 : fetch_or_message(a, url, &html, refresh);
    if (fr) {
        str_free(&html);
        if (fr == -2 && !append) resume_after_cancel(a);
        return -1;
    }
    /* A cache hit is always the complete page; only a live early chunk is
     * partial. Keep downloading in the background so the next page turn can
     * use the finished cache. */
    int is_partial = (!from_cache && a->last_fetch_partial);
    ThreadPage thread;
    int n = discuz_parse_thread(html.data, &thread, tid, page);
    str_free(&html);
    if (n <= 0) {
        thread_page_free(&thread);
        app_message(a, "没有解析到正文", "可能需要登录，或论坛模板已改版。\n用 --dump 抓一页对照 src/discuz.c 的规则表。");
        return -1;
    }
    if (!append) str_clear(&a->img_urls);   /* fresh thread: restart the list */
    book_from_thread(&a->book, &a->geo, &thread, append,
                     &a->img_urls, a->cfg.img_mode);
    a->cur_tid = tid;
    a->thread_page = page;
    a->thread_last = thread.last_page;
    thread_page_free(&thread);
    a->view = V_THREAD;
    if (!append) { a->page_index = 0; a->sel_line = -1; }
    if (!append) a->thread_partial = is_partial;
    if (is_partial) {
        /* partial HTML has no footer, so last_page is unreliable — allow at
         * least one more turn, which swaps in the completed cached page */
        if (a->thread_last <= page) a->thread_last = page + 1;
        book_wrap_text(&a->book, &a->geo, L_NOTE,
                       "—— 后文正在后台加载，翻页即可继续 ——", 0, 0, page);
        book_finalize(&a->book, &a->geo);
        log_msg("thread shown from early chunk (%d posts), bg download pid=%d",
                n, (int)g_bg_download);
    }
    app_draw(a, NULL);
    return 0;
}

/* Generic "fetch a forum path and show it as wrapped text". Used for search
 * results, my posts / replies / favourites, PMs and notices — all of which
 * are plain Discuz pages whose exact markup varies by version. */
static int show_url_text(App *a, const char *title, const char *path) {
    char url[HIPDA_URL_LEN];
    snprintf(url, sizeof(url), "%s%s%s", a->cfg.forum_server, HIPDA_FORUM_PATH, path);
    app_message(a, "正在加载…", "正在从论坛读取页面。\n点任意位置可取消。");
    Str html; str_init(&html);
    int fr = fetch_or_message(a, url, &html, 1);
    if (fr) {
        str_free(&html);
        if (fr == -2) resume_after_cancel(a);
        return -1;
    }
    Str txt; str_init(&txt);
    html_to_text(html.data, html.len, &txt);
    str_free(&html);
    book_from_text(&a->book, &a->geo, title, txt.data);
    str_free(&txt);
    a->view = V_TEXT;
    a->page_index = 0; a->sel_line = -1;
    a->cur_fid = 0; a->cur_tid = 0;
    app_draw(a, NULL);
    return 0;
}

static void show_menu_view(App *a) {
    book_from_menu(&a->book, &a->geo, "Hi 菜单", NULL, kMenuItems, MENU_N);
    a->view = V_MENU;
    a->page_index = 0; a->sel_line = 0;
    a->msg_action = 0;
    app_draw(a, NULL);
}

static void build_login_book(App *a);
static void build_search_book(App *a);
static void build_settings_book(App *a);
static void show_settings(App *a);
static void show_calib(App *a);
static void draw_calib(App *a);
static void show_fav_cache(App *a);

static void show_login(App *a) {
    a->view = V_LOGIN;
    a->input_mode = 0;
    a->login_field = a->login_user[0] || a->cfg.username[0] ? 1 : 0;
    if (!a->login_user[0])
        str_copy(a->login_user, sizeof(a->login_user), a->cfg.username);
    a->page_index = 0; a->sel_line = 1;
    build_login_book(a);
    app_draw(a, NULL);
}

static void show_search(App *a) {
    a->view = V_LOGIN;              /* reuse input view for typing */
    a->input_mode = 3;
    a->login_field = 2;             /* 2 = editing the search box */
    a->page_index = 0; a->sel_line = 1;
    build_search_book(a);
    app_draw(a, NULL);
}

static void show_settings(App *a) {
    a->view = V_SETTINGS;
    a->page_index = 0; a->sel_line = 0;
    build_settings_book(a);
    app_draw(a, NULL);
}

static void toggle_theme(App *a) {
    a->cfg.theme = a->cfg.theme ? 0 : 1;
    config_save(&a->cfg);
    log_msg("theme -> %d", a->cfg.theme);
    if (a->view == V_MENU) {
        book_from_menu(&a->book, &a->geo, "HiPDA",
                       a->cfg.theme ? "夜间模式（黑底白字）" : "日间模式（白底黑字）",
                       kMenuItems, MENU_N);
        app_draw(a, a->cfg.theme ? "已切换：黑底白字" : "已切换：白底黑字");
    } else if (a->view == V_SETTINGS) {
        build_settings_book(a);
        app_draw(a, NULL);
    } else {
        app_draw(a, NULL);
    }
}

/* Rotation: the UI always draws on a LOGICAL canvas (geo.w×geo.h); 90/270
 * swap the panel's width/height. Rebuild geometry accordingly. */
static void apply_rotation(App *a) {
    int swap = (a->cfg.rotation == 90 || a->cfg.rotation == 270);
    int lw = swap ? a->ui.h : a->ui.w;
    int lh = swap ? a->ui.w : a->ui.h;
    geometry_compute(&a->geo, lw, lh, a->ui.dpi, a->cfg.font_scale);
    ui_set_rotation(a->cfg.rotation);
    log_msg("rotation %d -> logical %dx%d", a->cfg.rotation, lw, lh);
}

/* Rebuild the drawing canvas after a geometry change (font size, rotation). */
static int canvas_rebuild(App *a) {
    canvas_free(&a->canvas);
    return canvas_init(&a->canvas, a->geo.w, a->geo.h);
}

/* Touch panel reports PANEL coordinates; invert the rotation mapping so taps
 * land on the logical (rotated-content) coordinates the UI works in.
 * Exact inverses of the pixel mapping in ui_fbink.c::rotated(). */
static void rotate_touch(App *a, int *x, int *y) {
    int deg = a->cfg.rotation;
    if (deg == 0) return;
    int lw = a->geo.w, lh = a->geo.h;
    if (deg == 90)       { int lx = *y,        ly = lh - 1 - *x; *x = lx; *y = ly; }
    else if (deg == 180) { *x = lw - 1 - *x;   *y = lh - 1 - *y; }
    else                 { int lx = lw - 1 - *y, ly = *x; *x = lx; *y = ly; }
}

static void apply_font_scale(App *a, int scale) {
    a->cfg.font_scale = scale;
    config_save(&a->cfg);
    apply_rotation(a);   /* re-derives geometry for the current rotation */
    /* snapshots were wrapped for the old column width: they must be dropped */
    snap_invalidate_all(a);
    canvas_rebuild(a);
    log_msg("font_scale -> %d (cols=%d rows=%d)", scale, a->geo.cols, a->geo.rows);
}

static void do_login(App *a) {
    if (!a->login_user[0]) {
        app_message(a, "请填写用户名", "在登录页点“用户名”一行，用屏幕键盘输入。");
        return;
    }
    if (!a->login_pass[0]) {
        app_message(a, "请填写密码", "在登录页点“密码”一行，用屏幕键盘输入。");
        return;
    }
    md5_hex_str(a->login_pass, a->cfg.password_md5);
    str_copy(a->cfg.username, sizeof(a->cfg.username), a->login_user);
    config_save(&a->cfg);
    app_message(a, "正在登录…", "请稍候，正在向论坛提交账号。");
    int r = net_login(&a->cfg);
    if (r == NET_OK) show_forums(a);
    else {
        char body[512];
        snprintf(body, sizeof(body),
                 "登录未完成（代码 %d）。\n\n请检查用户名与密码，或稍后重试。", r);
        app_message(a, "登录失败", body);
    }
}

static void run_search(App *a) {
    char enc[HIPDA_URL_LEN];
    Str e; str_init(&e);
    url_encode_str(a->search_term, &e);
    Str path; str_init(&path);
    str_append(&path, "search.php?srchtxt=");
    str_append(&path, e.data);
    str_append(&path, "&searchsubmit=yes");
    str_copy(enc, sizeof(enc), path.data);
    str_free(&e); str_free(&path);
    show_url_text(a, "搜索结果", enc);
}

/* Cache all favourited threads for offline reading.
 * net_fetch(refresh=0) writes every successful response into cache_dir under
 * its URL hash, so simply fetching each thread URL once fills the offline
 * cache; the normal "open thread" path then reuses those files with no network.
 * A tap anywhere aborts, and a plain "back" restores the menu. */
static void show_fav_cache(App *a) {
    const char *base = "my.php?item=favorites&type=thread";
    char url[HIPDA_URL_LEN];
    snprintf(url, sizeof(url), "%s%s%s", a->cfg.forum_server, HIPDA_FORUM_PATH, base);

    app_message(a, "收藏离线缓存", "正在读取收藏列表…\n点屏幕可中止。");
    Str html; str_init(&html);
    int fr = fetch_or_message(a, url, &html, 1);
    if (fr) {
        str_free(&html);
        if (fr == -2) resume_after_cancel(a);
        return;
    }
    TopicList fav;
    int n = discuz_parse_favorites(html.data, &fav);
    str_free(&html);
    if (n <= 0) {
        app_message(a, "没有可缓存的收藏",
                    "收藏列表为空，或页面结构已改版。\n（需要登录才能看到收藏）");
        a->view_before_msg = V_MENU;
        return;
    }

    /* page through the favourites list; each page adds more tids */
    int total_pages = fav.last_page > 0 ? fav.last_page : 1;
    if (total_pages > 50) total_pages = 50;      /* guard against a crazy value */
    for (int page = 2; page <= total_pages; page++) {
        char page_url[HIPDA_URL_LEN];
        snprintf(page_url, sizeof(page_url), "%s%s%s&page=%d",
                 a->cfg.forum_server, HIPDA_FORUM_PATH, base, page);
        char pmsg[256];
        snprintf(pmsg, sizeof(pmsg),
                 "正在读取收藏列表第 %d / %d 页…\n点屏幕可中止。", page, total_pages);
        app_message(a, "收藏离线缓存", pmsg);
        Str more; str_init(&more);
        int r = fetch_or_message(a, page_url, &more, 1);
        if (r == -2) { str_free(&more); resume_after_cancel(a); topic_list_free(&fav); return; }
        if (r == 0) {
            TopicList tmp;
            if (discuz_parse_favorites(more.data, &tmp) > 0) {
                for (int i = 0; i < tmp.n; i++) {
                    int dup = 0;
                    for (int j = 0; j < fav.n; j++)
                        if (fav.v[j].tid == tmp.v[i].tid) { dup = 1; break; }
                    if (!dup) topic_list_push(&fav, &tmp.v[i]);
                }
            }
            topic_list_free(&tmp);
        }
        str_free(&more);
    }

    /* fetch each thread body into the cache */
    int done = 0, failed = 0, cancelled = 0;
    for (int i = 0; i < fav.n; i++) {
        char prog[512];
        snprintf(prog, sizeof(prog),
                 "正在缓存收藏 %d / %d…\n%s\n\n点屏幕可中止。",
                 i + 1, fav.n, fav.v[i].title[0] ? fav.v[i].title : "（无标题）");
        app_message(a, "收藏离线缓存", prog);

        InputEvent ev;
        if (input_read(&ev, 0) > 0 && (ev.tap || ev.act == ACT_BACK)) {
            cancelled = 1;
            break;
        }

        char turl[HIPDA_URL_LEN];
        net_url_thread(turl, sizeof(turl), &a->cfg, fav.v[i].tid, 1);
        Str body; str_init(&body);
        /* refresh=0: use the disk cache if present, otherwise fetch+cache */
        int r = net_fetch(&a->cfg, turl, &body, 0);
        str_free(&body);
        if (r == NET_OK) done++; else failed++;
    }
    topic_list_free(&fav);

    char report[1024];
    if (cancelled)
        snprintf(report, sizeof(report), "已中止。\n本轮已缓存 %d 篇。", done);
    else
        snprintf(report, sizeof(report),
                 "缓存完成：%d 篇成功，%d 篇失败。\n\n"
                 "之后在帖子里点开这些收藏，直接读本机缓存，无需联网。\n"
                 "要更新内容，点右上角刷新键即可重新下载。", done, failed);
    app_message(a, "收藏离线缓存", report);
    a->view_before_msg = V_MENU;
}

/* Generic "my threads" list (favourites / my posts / my replies): one line
 * per thread, tap to read in full. Disk cache is used first so re-opening is
 * instant; the top-right refresh ring always refetches. */
static void show_my_list(App *a, const char *title, const char *path) {
    a->in_fav = 0;
    char url[HIPDA_URL_LEN];
    snprintf(url, sizeof(url), "%s%s%s", a->cfg.forum_server, HIPDA_FORUM_PATH, path);

    char prog[128];
    snprintf(prog, sizeof(prog), "正在读取%s…\n点屏幕可中止。", title);
    Str html; str_init(&html);
    int cached = net_peek(&a->cfg, url, &html) == NET_OK;
    int fr = 0;
    if (!cached) {
        app_message(a, title, prog);
        fr = fetch_or_message(a, url, &html, 1);
        if (fr) {
            str_free(&html);
            if (fr == -2) resume_after_cancel(a);
            return;
        }
    }
    TopicList list;
    int n = discuz_parse_favorites(html.data, &list);
    str_free(&html);
    if (n <= 0) {
        char body[256];
        snprintf(body, sizeof(body),
                 "没有解析到条目。\n（需要登录；若已登录则可能页面为空）");
        app_message(a, title, body);
        a->view_before_msg = V_MENU;
        return;
    }

    book_reset(&a->book);
    str_copy(a->book.title, sizeof(a->book.title), title);
    a->book.subtitle[0] = 0;
    a->book.fid = 0; a->book.tid = 0;
    char note[96];
    snprintf(note, sizeof(note), "共 %d 条 · 点条目阅读（缓存离线可看）", n);
    book_wrap_text(&a->book, &a->geo, L_NOTE, note, 0, 0, 1);
    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    for (int i = 0; i < n; i++) {
        char row[HIPDA_NAME_LEN];
        const char *t = list.v[i].title[0] ? list.v[i].title : "（无标题）";
        snprintf(row, sizeof(row), "· %s", t);
        book_wrap_text(&a->book, &a->geo, L_ITEM, row, 0, list.v[i].tid, 1);
    }
    book_finalize(&a->book, &a->geo);
    topic_list_free(&list);

    a->view = V_FAVLIST;
    a->list_kind = 0;
    a->page_index = 0; a->sel_line = -1;
    a->cur_fid = 0; a->cur_tid = 0;
    app_draw(a, NULL);
}

/* The favourites list, ZhihuReader-style: one line per saved thread, tap to
 * read it in full (offline cache is used whenever present). */
static void show_fav_list(App *a) {
    show_my_list(a, "我的收藏", "my.php?item=favorites&type=thread");
}

/* PM / notice lists: one line per message, tap to read the message text. */
static void show_msg_list(App *a, const char *title, const char *path) {
    a->in_fav = 0;
    char url[HIPDA_URL_LEN];
    snprintf(url, sizeof(url), "%s%s%s", a->cfg.forum_server, HIPDA_FORUM_PATH, path);

    char prog[128];
    snprintf(prog, sizeof(prog), "正在读取%s…\n点屏幕可中止。", title);
    Str html; str_init(&html);
    int cached = net_peek(&a->cfg, url, &html) == NET_OK;
    int fr = 0;
    if (!cached) {
        app_message(a, title, prog);
        fr = fetch_or_message(a, url, &html, 1);
        if (fr) {
            str_free(&html);
            if (fr == -2) resume_after_cancel(a);
            return;
        }
    }
    TopicList list;
    int n = discuz_parse_msglist(html.data, &list, &a->list_urls);
    str_free(&html);
    if (n <= 0) {
        char body[256];
        snprintf(body, sizeof(body),
                 "没有解析到条目。\n（需要登录；若已登录则可能列表为空）");
        app_message(a, title, body);
        a->view_before_msg = V_MENU;
        return;
    }

    book_reset(&a->book);
    str_copy(a->book.title, sizeof(a->book.title), title);
    a->book.subtitle[0] = 0;
    a->book.fid = 0; a->book.tid = 0;
    char note[96];
    snprintf(note, sizeof(note), "共 %d 条 · 点条目查看", n);
    book_wrap_text(&a->book, &a->geo, L_NOTE, note, 0, 0, 1);
    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    for (int i = 0; i < n; i++) {
        char row[HIPDA_NAME_LEN];
        const char *t = list.v[i].title[0] ? list.v[i].title : "（无标题）";
        snprintf(row, sizeof(row), "· %s", t);
        book_wrap_text(&a->book, &a->geo, L_ITEM, row, 0, list.v[i].tid, 1);
    }
    book_finalize(&a->book, &a->geo);
    topic_list_free(&list);

    a->view = V_FAVLIST;
    a->list_kind = 1;
    a->page_index = 0; a->sel_line = -1;
    a->cur_fid = 0; a->cur_tid = 0;
    app_draw(a, NULL);
}

static void menu_action(App *a, int idx) {
    switch (idx) {
        case 1: show_forums(a); break;
        case 2: show_search(a); break;
        case 3: show_my_list(a, "我的帖子", "my.php?item=threads"); break;
        case 4: show_my_list(a, "我的回复", "my.php?item=posts"); break;
        case MENU_FAVOURITES: show_fav_list(a); break;
        case MENU_FAV_CACHE:  show_fav_cache(a); break;
        case MENU_PM:         show_msg_list(a, "短消息", "pm.php"); break;
        case MENU_NOTICE:     show_msg_list(a, "帖子消息", "notice.php"); break;
        case MENU_SETTINGS:   show_settings(a); break;
        case MENU_THEME:      toggle_theme(a); break;
        case MENU_QUIT:
            str_copy(a->msg_title, sizeof(a->msg_title), "HiPDA 已退出");
            str_copy(a->msg_body, sizeof(a->msg_body),
                     "触摸已交还给系统。\n点屏幕任意位置返回 Kindle。");
            a->view = V_MESSAGE;
            app_draw(a, NULL);
            a->quit_now = 1;        /* main loop exits right after this */
            break;
        default: break;
    }
}

/* Draw the two login fields plus the keyboard into one book. */
/* UTF-8 display width in cells: ASCII/CJK punctuation = 1, CJK ideograph = 2.
 * Used to pad input boxes so the right bracket lines up. */
static int display_cells(const char *s) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { n += 1; p++; }
        else if ((*p & 0xE0) == 0xC0) { n += 1; p += 2; }
        else if ((*p & 0xF0) == 0xE0) { n += 2; p += 3; }
        else { n += 2; p += 4; }
    }
    return n;
}

/* Draw an input field like a normal text box: a bordered box with the value
 * and a caret at the end of the active field. */
static void input_field(char *out, size_t cap, const char *label,
                        const char *value, int active, int mask) {
    char shown[HIPDA_NAME_LEN];
    size_t n = strlen(value);
    if (n >= sizeof(shown) - 2) n = sizeof(shown) - 2;
    if (mask) {
        for (size_t i = 0; i < n; i++) shown[i] = '*';
    } else {
        memcpy(shown, value, n);
    }
    shown[n] = active ? '_' : 0;         /* caret on the active field */
    shown[n + (active ? 1 : 0)] = 0;
    const int box = 18;                  /* fits every supported device */
    int pad = box - display_cells(shown);
    if (pad < 0) pad = 0;
    snprintf(out, cap, "%s[%s%*s]", label, shown, pad, "");
}

static void build_input_book(App *a, const char *title, const char *field0,
                             const char *field1, int editing,
                             const char *btn_label) {
    book_reset(&a->book);
    str_copy(a->book.title, sizeof(a->book.title), title);
    a->book.subtitle[0] = 0;
    a->book.fid = a->book.tid = 0;

    /* Spacious layout on big screens; tight enough to stay on one page on a
     * 600x800 Kindle 4/5 (rows < 20). */
    int roomy = a->geo.rows >= 20;

    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    char l0[HIPDA_NAME_LEN], l1[HIPDA_NAME_LEN];
    snprintf(l0, sizeof(l0), "%s%s", field0, editing == 0 ? "" : "");
    snprintf(l1, sizeof(l1), "%s%s", field1, editing == 1 ? "" : "");
    book_wrap_text(&a->book, &a->geo, editing == 0 ? L_ITEM : L_META, l0, 0, 1, 1);
    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    book_wrap_text(&a->book, &a->geo, editing == 1 ? L_ITEM : L_META, l1, 0, 2, 1);
    /* the login button sits directly under the two fields; Enter ([回车] on
     * the control row) does the same thing */
    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    book_wrap_text(&a->book, &a->geo, L_BTN, btn_label, 0, 91, 1);
    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    book_wrap_text(&a->book, &a->geo, L_NOTE,
                   "[Sh]大小写 [Pg]数字符号 [回车]=登录", 0, 0, 1);
    if (roomy) book_add_line(&a->book, L_BLANK, "", 0, 0, 1);

    /* Chinese input page (search only): typed pinyin + candidates */
    if (a->kb_page == KEY_PINYIN_PAGE && a->input_mode == 3) {
        char pyline[HIPDA_NAME_LEN];
        snprintf(pyline, sizeof(pyline), "拼音：%s_", a->pinyin_buf);
        book_wrap_text(&a->book, &a->geo, L_META, pyline, 0, 0, 1);
        if (a->pinyin_buf[0]) {
            char cand[KEY_CAND_SLOTS * 3 + 1] = {0};
            char slots[7][4];
            int start = a->cand_page * 7;
            int total = pinyin_candidates(a->pinyin_buf, start, slots, 7);
            for (int k = 0; k < total; k++) memcpy(cand + k * 3, slots[k], 3);
            if (total == 7 && start + 7 < total)
                memcpy(cand + 7 * 3, "\xe2\x80\xa6", 3);   /* U+2026 = more */
            book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
            book_wrap_text(&a->book, &a->geo, L_CAND, cand, 0, KEY_CAND_ID, 1);
        } else {
            book_wrap_text(&a->book, &a->geo, L_NOTE,
                           "输入拼音 · 空格选首字 · [Pg] 回英文", 0, 0, 1);
        }
        book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
        if (roomy) book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    }

    /* current keyboard page (pinyin page shows the lower-case letters) */
    int base = (a->kb_page == KEY_PINYIN_PAGE) ? 0 : a->kb_page * KEY_ROWS_PER_PAGE;
    for (int r = 0; r < KEY_ROWS_PER_PAGE; r++) {
        book_wrap_text(&a->book, &a->geo, L_KEYS, kKeyRows[base + r], 0, 0, 1);
        if (roomy) book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    }
    /* control row: Shift / Page / Space / Backspace / Enter / Cancel */
    book_add_line(&a->book, L_BLANK, "", 0, 0, 1);
    book_wrap_text(&a->book, &a->geo, L_CTRL, "", 0, KEY_CTRL_ID, 1);
    book_finalize(&a->book, &a->geo);
}

static void build_login_book(App *a) {
    char f0[HIPDA_NAME_LEN / 2], f1[HIPDA_NAME_LEN / 2];
    input_field(f0, sizeof(f0), "用户名：", a->login_user, a->login_field == 0, 0);
    input_field(f1, sizeof(f1), "密码：  ", a->login_pass, a->login_field == 1, 1);
    build_input_book(a, "登录 4d4y 论坛", f0, f1, a->login_field, "登  录");
}

static void build_search_book(App *a) {
    char f0[HIPDA_NAME_LEN / 2], f1[HIPDA_NAME_LEN / 2];
    input_field(f0, sizeof(f0), "关键字：", a->search_term, 1, 0);
    str_copy(f1, sizeof(f1), "（点 [回车] 或下方按钮开始搜索）");
    build_input_book(a, "搜索帖子", f0, f1, 2, "搜  索");
}

static void build_settings_book(App *a) {
    char buf[SETTINGS_N][HIPDA_NAME_LEN];
    const char *items[SETTINGS_N];
    const char *scale_txt = a->cfg.font_scale == 0 ? "自动"
                          : a->cfg.font_scale == 1 ? "小"
                          : a->cfg.font_scale == 2 ? "中" : "大";
    snprintf(buf[0], HIPDA_NAME_LEN, "字号：%s", scale_txt);
    const char *fname = strstr(a->cfg.font_file, "reading")
                        ? "阅读体（宋体+等宽）" : "Unifont（经典点阵）";
    snprintf(buf[1], HIPDA_NAME_LEN, "字体：%s", fname);
    snprintf(buf[2], HIPDA_NAME_LEN, "主题：%s",
             a->cfg.theme ? "黑底白字（夜间）" : "白底黑字（日间）");
    static const char *const rm_names[4] = {
        "全刷（最清晰，最慢）", "局部刷（最快，有残影）",
        "Regal（Kindle 特有）", "智能（局部刷N页+全刷）"
    };
    snprintf(buf[3], HIPDA_NAME_LEN, "刷屏模式：%s",
             rm_names[a->cfg.refresh_mode & 3]);
    snprintf(buf[4], HIPDA_NAME_LEN, "每几页全刷：%d", a->cfg.full_refresh_every);
    snprintf(buf[5], HIPDA_NAME_LEN, "屏幕旋转：%d°%s",
             a->cfg.rotation, a->cfg.rotation == 0 ? "（默认）" : "");
    snprintf(buf[6], HIPDA_NAME_LEN, "图片显示：%s",
             a->cfg.img_mode ? "点按显示（自动缓存）" : "不显示");
    snprintf(buf[7], HIPDA_NAME_LEN, "登录账号：%s",
             a->cfg.username[0] ? a->cfg.username : "（未登录）");
    snprintf(buf[8], HIPDA_NAME_LEN, "触摸校准（swap=%d 镜像=%d/%d）",
             a->cfg.cal_swap, a->cfg.cal_flip_x, a->cfg.cal_flip_y);
    str_copy(buf[9], HIPDA_NAME_LEN, "清理缓存（离线内容与已读页面）");
    str_copy(buf[10], HIPDA_NAME_LEN, "返回");
    for (int i = 0; i < SETTINGS_N; i++) items[i] = buf[i];
    book_from_menu(&a->book, &a->geo, "设置", "点一行修改", items, SETTINGS_N);
}

/* ---------------- touch calibration ---------------- */
static void draw_calib(App *a) {
    Canvas *c = &a->canvas;
    Geometry *g = &a->geo;
    uint8_t bg = a->cfg.theme ? 0 : 255;
    uint8_t ink = a->cfg.theme ? 255 : 0;
    canvas_fill(c, bg);

    int sz = 72;
    int m = 40;
    int xs[4] = { m, g->w - m - sz, g->w - m - sz, m };
    int ys[4] = { m, m, g->h - m - sz, g->h - m - sz };
    for (int i = 0; i < 4; i++) {
        if (i == a->cal_step)
            canvas_rect(c, xs[i], ys[i], sz, sz, ink);            /* solid target */
        else
            canvas_frame(c, xs[i], ys[i], sz, sz, ink, 3);        /* outline */
    }
    static const char *names[4] = { "左上", "右上", "右下", "左下" };
    char msg[160];
    if (a->cal_step < 4)
        snprintf(msg, sizeof(msg), "触摸校准 %d/4 ：点【%s】角的方块",
                 a->cal_step + 1, names[a->cal_step]);
    else
        str_copy(msg, sizeof(msg), "校准完成");

    int tw = font_text_width(&a->font, msg, g->scale);
    font_draw_text(&a->font, c, (g->w - tw) / 2, g->h / 2 - g->line_h, msg,
                   g->scale, ink, 1);
    const char *h1 = "依次点完四个角，自动保存";
    int w1 = font_text_width(&a->font, h1, g->scale);
    font_draw_text(&a->font, c, (g->w - w1) / 2, g->h / 2 + g->line_h, h1,
                   g->scale, ink, 0);
    const char *h2 = "点屏幕底部取消";
    int w2 = font_text_width(&a->font, h2, g->scale);
    font_draw_text(&a->font, c, (g->w - w2) / 2, g->h - g->margin_y - g->line_h,
                   h2, g->scale, ink, 0);
    ui_present(c->px, c->w, c->h, 1);
}

static void calib_tap(App *a, const InputEvent *ev) {
    if (a->cal_step >= 4) return;
    a->cal_raw[a->cal_step][0] = ev->raw_x;
    a->cal_raw[a->cal_step][1] = ev->raw_y;
    log_msg("calib: corner %d raw=(%d,%d)", a->cal_step, ev->raw_x, ev->raw_y);
    a->cal_step++;
    if (a->cal_step < 4) { draw_calib(a); return; }

    int tlx = a->cal_raw[0][0], tly = a->cal_raw[0][1];
    int trx = a->cal_raw[1][0], tr_y = a->cal_raw[1][1];
    int blx = a->cal_raw[3][0], bly = a->cal_raw[3][1];

    int hdx = trx - tlx; if (hdx < 0) hdx = -hdx;
    int hdy = tr_y - tly; if (hdy < 0) hdy = -hdy;
    int swap = (hdy > hdx);          /* horizontal move dominates raw Y -> swapped */

    int flip_x, flip_y;
    if (!swap) {
        flip_x = (tlx > trx);
        flip_y = (tly > bly);
    } else {
        flip_x = (tly > tr_y);
        flip_y = (tlx > blx);
    }
    a->cfg.cal_swap = swap;
    a->cfg.cal_flip_x = flip_x;
    a->cfg.cal_flip_y = flip_y;
    config_save(&a->cfg);
    input_set_calib(swap, flip_x, flip_y);
    log_msg("calib: saved swap=%d flip_x=%d flip_y=%d", swap, flip_x, flip_y);
    app_message(a, "校准完成",
                "已保存到 config.ini。\n点任意位置返回。");
    a->view_before_msg = V_SETTINGS;
}

static void show_calib(App *a) {
    a->view = V_CALIB;
    a->cal_step = 0;
    a->press_line = -1;
    memset(a->cal_raw, 0, sizeof(a->cal_raw));
    draw_calib(a);
}

/* Current input buffer, shared by the keyboard handlers. */
static char *input_buf(App *a) {
    return (a->input_mode == 3) ? a->search_term
         : (a->login_field == 1 ? a->login_pass : a->login_user);
}
static size_t input_cap(App *a) {
    return (a->input_mode == 3) ? sizeof(a->search_term)
         : (a->login_field == 1 ? sizeof(a->login_pass) : sizeof(a->login_user));
}
static void input_rebuild(App *a) {
    if (a->input_mode == 3) build_search_book(a); else build_login_book(a);
    app_draw(a, NULL);
}
/* Append one ASCII character; uppercase page honours the one-shot Shift. */
static void input_append(App *a, char ch) {
    char *buf = input_buf(a);
    size_t cap = input_cap(a);
    size_t n = strlen(buf);
    if (n + 2 >= cap) return;
    buf[n] = ch;
    buf[n + 1] = 0;
    /* one-shot shift: use it once then drop back to lower case */
    if (a->kb_shift_once && a->kb_page == 1) {
        a->kb_shift_once = 0;
        a->kb_page = 0;
    }
}
/* Append a UTF-8 string (a Chinese character picked from candidates). */
static void input_append_utf8(App *a, const char *ch) {
    char *buf = input_buf(a);
    size_t cap = input_cap(a);
    size_t n = strlen(buf);
    size_t L = strlen(ch);
    if (n + L + 1 < cap) {
        memcpy(buf + n, ch, L);
        buf[n + L] = 0;
    }
}

/* Pick the candidate at the given absolute slot (0-based across pages). */
static void pinyin_pick(App *a, int slot) {
    char one[1][4];
    if (pinyin_candidates(a->pinyin_buf, slot, one, 1) > slot) {
        input_append_utf8(a, one[0]);
        a->pinyin_buf[0] = 0;
        a->cand_page = 0;
    }
}

static void input_backspace(App *a) {
    char *buf = input_buf(a);
    size_t n = strlen(buf);
    while (n > 0) {
        n--;
        if (((unsigned char)buf[n] & 0xC0) != 0x80) break;
    }
    buf[n] = 0;
}

/* Handle one tap/key inside the login or search keyboard view. */
static void input_view_action(App *a, Action act, int line_index, int tap_x) {
    if (act == ACT_QUIT) return;
    if (act == ACT_BACK || act == ACT_HOME || act == ACT_MENU) {
        show_menu_view(a);
        return;
    }
    if (line_index < 0) return;

    /* item_id encodes what was tapped: 1/2 = the input fields, 90/91/92 =
     * backspace / ok / cancel, 0 = a keyboard row. */
    const Page *pg = &a->book.pages[a->page_index];
    int idx = pg->start + line_index;
    if (idx < 0 || idx >= a->book.n) return;
    int id = a->book.lines[idx].item_id;

    if (id == 1) {
        a->login_field = (a->input_mode == 3) ? 2 : 0;
        if (a->input_mode == 3) build_search_book(a); else build_login_book(a);
        app_draw(a, NULL);
        return;
    }
    if (id == 2) {
        if (a->input_mode == 3) { build_search_book(a); app_draw(a, NULL); return; }
        a->login_field = 1;
        build_login_book(a);
        app_draw(a, NULL);
        return;
    }
    if (id == 92) { show_menu_view(a); return; }
    if (id == 91) {
        if (a->input_mode == 3) run_search(a);
        else do_login(a);
        return;
    }
    if (id == 90) {                    /* backspace (legacy id, kept safe) */
        input_backspace(a);
        input_rebuild(a);
        return;
    }

    const Line *l = &a->book.lines[idx];

    /* ---- control row: Sh / Pg / 空格 / 退格 / 回车 / 取消 ---- */
    if (id == KEY_CTRL_ID) {
        int step = a->geo.cell_w * KEY_CTRL_STRIDE;
        if (step < 1) step = 1;
        int col = (tap_x - a->geo.margin_x) / step;
        if (col < 0 || col > 5) return;
        switch (col) {
            case KEY_CTRL_SH:          /* Shift: lower <-> upper, one-shot */
                if (a->kb_page == 0) { a->kb_page = 1; a->kb_shift_once = 0; }
                else if (a->kb_page == 1) a->kb_page = 0;
                else { a->kb_page = 1; a->kb_shift_once = 1; }
                input_rebuild(a);
                return;
            case KEY_CTRL_PG:          /* Page: cycle letters/symbols/pinyin */
                if (a->input_mode == 3)
                    a->kb_page = (a->kb_page + 1) % 4;
                else
                    a->kb_page = (a->kb_page + 1) % 3;
                a->kb_shift_once = 0;
                a->cand_page = 0;
                input_rebuild(a);
                return;
            case KEY_CTRL_SP:          /* space = pick first candidate */
                if (a->kb_page == KEY_PINYIN_PAGE && a->pinyin_buf[0]) {
                    pinyin_pick(a, a->cand_page * 7);
                    input_rebuild(a);
                    return;
                }
                input_append(a, ' ');
                input_rebuild(a);
                return;
            case KEY_CTRL_BK:          /* backspace: pinyin first, then text */
                if (a->kb_page == KEY_PINYIN_PAGE && a->pinyin_buf[0]) {
                    size_t n = strlen(a->pinyin_buf);
                    a->pinyin_buf[n - 1] = 0;
                    if (!a->pinyin_buf[0]) a->cand_page = 0;
                    input_rebuild(a);
                    return;
                }
                input_backspace(a);
                input_rebuild(a);
                return;
            case KEY_CTRL_OK:          /* Enter — same as tapping 登录 */
                if (a->input_mode == 3) run_search(a); else do_login(a);
                return;
            case KEY_CTRL_XX:
                show_menu_view(a);
                return;
            default: return;
        }
    }

    /* ---- pinyin candidate row: 8 slots, 5 cells apart ---- */
    if (id == KEY_CAND_ID) {
        const char *cs = l->text ? l->text : "";
        int n = (int)strlen(cs) / 3;
        int step = a->geo.cell_w * KEY_CAND_STRIDE;
        if (step < 1) step = 1;
        int col = (tap_x - a->geo.margin_x) / step;
        if (col < 0 || col > 7) return;
        if (col == 7 && n == 8 &&
            memcmp(cs + 21, "\xe2\x80\xa6", 3) == 0) {
            a->cand_page++;                       /* "..." slot: next page */
            input_rebuild(a);
            return;
        }
        if (col >= n) return;
        char one[4];
        memcpy(one, cs + col * 3, 3);
        one[3] = 0;
        input_append_utf8(a, one);
        a->pinyin_buf[0] = 0;
        a->cand_page = 0;
        input_rebuild(a);
        return;
    }

    /* ---- character rows: pick the key by x, matching the painted grid ---- */
    int row = -1;
    for (int r = 0; r < KEY_PAGES * KEY_ROWS_PER_PAGE; r++)
        if (l->text && !strcmp(l->text, kKeyRows[r])) { row = r; break; }
    if (row < 0) return;
    int step = a->geo.cell_w * KEY_CHARS;
    if (step < 1) step = 1;
    int col = (tap_x - a->geo.margin_x) / step;
    if (col < 0 || col >= KEY_COLS) return;
    const char *keys = kKeyRows[row];
    int len = (int)strlen(keys);
    int pos = col * KEY_CHARS;         /*每键 1 字符 + 2 空格 */
    if (pos < 0 || pos >= len) return;
    char ch = keys[pos];
    if (ch == ' ') return;                 /* blank cell between keys */
    if (a->kb_page == KEY_PINYIN_PAGE) {
        /* letters feed the pinyin buffer, not the search term directly */
        if (ch >= 'a' && ch <= 'z') {
            size_t n = strlen(a->pinyin_buf);
            if (n + 1 < sizeof(a->pinyin_buf)) {
                a->pinyin_buf[n] = ch;
                a->pinyin_buf[n + 1] = 0;
                a->cand_page = 0;
            }
            input_rebuild(a);
        }
        return;
    }
    input_append(a, ch);
    input_rebuild(a);
}

/* ---- tap-to-view images -------------------------------------------------
 * Placeholders in a thread carry the URL hash as item_id; the URL list lives
 * in a.img_urls. Downloaded once into cache_dir/imgs/, decoded with
 * stb_image, scaled to fit the screen, then shown full-screen; a tap returns
 * to the thread. Cached files load instantly on the next tap. */
static void img_open(App *a, int id) {
    char url[HIPDA_URL_LEN] = {0};
    const char *p = a->img_urls.data;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char cand[HIPDA_URL_LEN];
        if (n >= sizeof(cand)) n = sizeof(cand) - 1;
        memcpy(cand, p, n);
        cand[n] = 0;
        int h = (int)(fnv1a(cand) & 0x7FFFFFFF);
        if (h == 0) h = 1;
        if (h == id) { str_copy(url, sizeof(url), cand); break; }
        p = nl ? nl + 1 : NULL;
    }
    if (!url[0]) { app_draw(a, "图片链接已失效，请刷新后再试"); return; }

    /* relative paths: forum pages reference /data/attachment/... or the
     * image host directly */
    char full[HIPDA_URL_LEN];
    if (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)) {
        str_copy(full, sizeof(full), url);
    } else if (url[0] == '/') {
        snprintf(full, sizeof(full), "%s%s", a->cfg.forum_server, url);
    } else {
        const char *host = a->cfg.image_host[0] ? a->cfg.image_host
                                                : a->cfg.forum_server;
        snprintf(full, sizeof(full), "%s/%s", host, url);
    }

    char dir[HIPDA_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/imgs", a->cfg.cache_dir);
    dir_ensure(dir);
    char path[HIPDA_PATH_LEN];
    snprintf(path, sizeof(path), "%s/imgs/%08llX.img", a->cfg.cache_dir,
             fnv1a(full) & 0xFFFFFFFFULL);

    if (!file_exists(path)) {
        app_message(a, "下载图片", "正在下载图片…\n点屏幕可中止。");
        if (net_fetch_file(&a->cfg, full, path, 10u * 1024 * 1024) != 0) {
            unlink(path);
            app_draw(a, "图片下载失败（网络或大小限制）");
            return;
        }
    }

    unsigned char *px = NULL;
    int iw = 0, ih = 0;
    int max_w = a->geo.w - 2 * a->geo.margin_x;
    int max_h = a->geo.h - 2 * a->geo.margin_y - 2 * a->geo.line_h;
    if (img_load_y8(path, max_w, max_h, &px, &iw, &ih) != 0) {
        app_draw(a, "图片解码失败（格式不支持或文件损坏）");
        return;
    }

    /* full-screen viewer on the shared canvas */
    uint8_t bg = a->cfg.theme ? 0 : 255;
    uint8_t fg = a->cfg.theme ? 255 : 0;
    canvas_fill(&a->canvas, bg);
    int ix = (a->geo.w - iw) / 2;
    int iy = a->geo.margin_y + a->geo.line_h;
    for (int row = 0; row < ih; row++)
        memcpy(a->canvas.px + (size_t)(iy + row) * (size_t)a->canvas.w + (size_t)ix,
               px + (size_t)row * (size_t)iw, (size_t)iw);
    char hint[64];
    snprintf(hint, sizeof(hint), "%dx%d · 点任意位置返回", iw, ih);
    font_draw_text(&a->font, &a->canvas, a->geo.margin_x,
                   a->geo.h - a->geo.margin_y - a->geo.line_h,
                   hint, a->geo.scale, fg, 0);
    ui_present(a->canvas.px, a->canvas.w, a->canvas.h, 1);
    free(px);

    InputEvent ev;
    for (;;) {
        if (input_read(&ev, 300000) > 0 &&
            (ev.tap || ev.act == ACT_BACK || ev.act == ACT_MENU ||
             ev.act == ACT_QUIT || ev.act == ACT_HOME)) break;
    }
    app_draw(a, NULL);          /* back to the thread page */
}

/* Delete only cached pages (*.html) inside cache_dir. Cookies and config are
 * left alone — clearing them would silently log the user out. */
static int cache_clear(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    char path[HIPDA_PATH_LEN];
    int n = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        size_t len = strlen(e->d_name);
        if (len < 6 || strcmp(e->d_name + len - 5, ".html") != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (unlink(path) == 0) n++;
    }
    closedir(d);
    return n;
}

static void settings_action(App *a, int line_index) {
    const Page *pg = &a->book.pages[a->page_index];
    int idx = pg->start + line_index;
    if (idx < 0 || idx >= a->book.n) return;
    int id = a->book.lines[idx].item_id;
    switch (id) {
        case 1: {
            int s = a->cfg.font_scale;
            s = (s + 1) % 4;           /* 0 auto, 1..3 fixed */
            apply_font_scale(a, s);
            if (a->view == V_SETTINGS) build_settings_book(a);
            app_draw(a, NULL);
            break;
        }
        case 2: {
            /* toggle font: reading (Song + mono) <-> classic Unifont */
            font_free(&a->font);
            const char *base = a->cfg.font_file;
            const char *other = strstr(base, "reading")
                ? (strstr(base, "/mnt/us/") ? "/mnt/us/hipda/fonts/unifont.bin"
                                            : "fonts/unifont.bin")
                : (strstr(base, "/mnt/us/") ? "/mnt/us/hipda/fonts/reading.bin"
                                            : "fonts/reading.bin");
            if (font_load(&a->font, other) == 0) {
                str_copy(a->cfg.font_file, sizeof(a->cfg.font_file), other);
                config_save(&a->cfg);
                log_msg("font switched to %s", other);
            } else {
                /* keep the old font usable */
                const char *back = strstr(base, "reading")
                    ? "fonts/reading.bin" : "fonts/unifont.bin";
                font_load(&a->font, back);
                app_draw(a, "字体文件缺失，已保留原字体");
                return;
            }
            snap_invalidate_all(a);   /* wrapped pages must be re-laid out */
            build_settings_book(a);
            app_draw(a, NULL);
            break;
        }
        case 3: toggle_theme(a); break;
        case 4:
            a->cfg.refresh_mode = (a->cfg.refresh_mode + 1) % 4;
            ui_set_refresh_mode(a->cfg.refresh_mode);
            config_save(&a->cfg);
            build_settings_book(a);
            app_draw(a, NULL);
            break;
        case 5:
            a->cfg.full_refresh_every = a->cfg.full_refresh_every >= 12 ? 3
                                       : a->cfg.full_refresh_every + 3;
            config_save(&a->cfg);
            build_settings_book(a);
            app_draw(a, NULL);
            break;
        case 6: {
            /* rotation: 0 -> 90 -> 180 -> 270 -> 0, rebuild everything that
             * depends on geometry (canvas, snapshots, selection) */
            a->cfg.rotation = (a->cfg.rotation + 90) % 360;
            apply_rotation(a);
            snap_invalidate_all(a);
            canvas_rebuild(a);
            a->sel_line = -1; a->page_index = 0;
            config_save(&a->cfg);
            build_settings_book(a);
            app_draw(a, NULL);
            break;
        }
        case 7:
            a->cfg.img_mode = a->cfg.img_mode ? 0 : 1;
            config_save(&a->cfg);
            snap_invalidate_all(a);   /* placeholders re-render per mode */
            build_settings_book(a);
            app_draw(a, NULL);
            break;
        case 8: show_login(a); break;
        case 9: show_calib(a); break;
        case 10: {
            int n = cache_clear(a->cfg.cache_dir);
            snap_invalidate_all(a);
            log_msg("cache: cleared %d file(s) from %s", n, a->cfg.cache_dir);
            char body[256];
            if (n < 0)
                snprintf(body, sizeof(body), "无法打开缓存目录：\n%s", a->cfg.cache_dir);
            else
                snprintf(body, sizeof(body),
                         "已删除 %d 个缓存页面。\n\nCookie 与登录状态已保留，无需重新登录。", n);
            app_message(a, "清理缓存", body);
            a->view_before_msg = V_SETTINGS;
            break;
        }
        case 11: show_menu_view(a); break;
        default: break;
    }
}

/* Turn one page. At the end of the current Discuz page, fetch the next one
 * and append it to the book so reading never hits a "next thread page" button. */
static void turn_next(App *a) {
    if (a->view == V_THREAD && a->thread_partial &&
        a->page_index + 1 >= a->book.np) {
        /* We only had the first chunk of this forum page. Wait for the
         * background download to finish, then rebuild the FULL page from the
         * cache and resume at the same page index — the remaining floors of
         * this forum page appear seamlessly before any next-page fetch. */
        int waited = 0;
        while (g_bg_download > 0 && waited < 200) {
            reap_background();
#ifndef PC_BUILD
            if (g_bg_download > 0) { usleep(100000); waited++; }
#else
            break;
#endif
        }
        a->thread_partial = 0;
        int keep = a->page_index;
        if (!show_thread(a, a->cur_tid, a->thread_page, 0, 0)) {
            if (keep < a->book.np) a->page_index = keep;
            app_draw(a, NULL);
        }
        return;
    }
    if (a->page_index + 1 < a->book.np) { a->page_index++; app_draw(a, NULL); return; }
    if (a->view == V_THREAD && a->thread_page < a->thread_last) {
        int next = a->thread_page + 1;
        if (!show_thread(a, a->cur_tid, next, 1, 0)) {
            a->page_index++;
            app_draw(a, NULL);
        }
        return;
    }
    if (a->view == V_TOPICS && a->topic_page < a->topic_last) {
        int next = a->topic_page + 1;
        if (!show_topics(a, a->cur_fid, next, 1, 0)) {
            a->page_index++;
            app_draw(a, NULL);
        }
        return;
    }
    app_draw(a, "已经是最后一页");
}

static void turn_prev(App *a) {
    if (a->page_index > 0) { a->page_index--; app_draw(a, NULL); }
    else app_draw(a, "已经是第一页");
}

/* Resolve a tapped line to the closest selectable row. In list views a topic
 * block is "title" (item) + "author/meta" (kind=0), so tapping the meta line
 * should still open the thread. The match is capped at distance 1: wider gaps
 * are spacing on purpose, and tapping them must fall through to page-turn
 * instead of opening whatever entry happens to be nearest. */
static int hit_item_line(App *a, int line_index) {
    const Page *pg = &a->book.pages[a->page_index];
    if (line_index < 0 || line_index >= pg->count) return -1;
    if (a->book.lines[pg->start + line_index].item_id > 0) return line_index;
    if (a->view != V_FORUMS && a->view != V_TOPICS && a->view != V_FAVLIST)
        return -1;
    for (int d = 1; d <= 1; d++) {
        int up = line_index - d, dn = line_index + d;
        if (up >= 0 && a->book.lines[pg->start + up].item_id > 0) return up;
        if (dn < pg->count && a->book.lines[pg->start + dn].item_id > 0) return dn;
    }
    return -1;
}

static int activate_is_item(App *a, int line_index) {
    if (a->view != V_FORUMS && a->view != V_TOPICS &&
        a->view != V_MENU && a->view != V_SETTINGS &&
        a->view != V_FAVLIST) return 0;
    return hit_item_line(a, line_index) >= 0;
}

static void activate(App *a, int line_index) {
    int hit = hit_item_line(a, line_index);
    if (hit < 0) return;
    const Page *pg = &a->book.pages[a->page_index];
    int id = a->book.lines[pg->start + hit].item_id;
    if (id <= 0) return;
    if (a->view == V_MENU)        menu_action(a, id);
    else if (a->view == V_SETTINGS) settings_action(a, hit);
    else if (a->view == V_FORUMS) show_topics(a, id, 1, 0, 0);
    else if (a->view == V_TOPICS) show_thread(a, id, 1, 0, 0);
    else if (a->view == V_FAVLIST) {
        if (a->list_kind == 1) {
            /* pm/notice list: open the tapped message as plain text */
            char path[HIPDA_URL_LEN] = {0};
            const char *p = a->list_urls.data;
            for (int i = 1; p && *p && i <= id; i++) {
                const char *nl = strchr(p, '\n');
                size_t n = nl ? (size_t)(nl - p) : strlen(p);
                if (i == id && n < sizeof(path)) {
                    memcpy(path, p, n); path[n] = 0; break;
                }
                p = nl ? nl + 1 : NULL;
            }
            if (path[0]) show_url_text(a, "消息内容", path);
        } else {
            a->in_fav = 1;
            show_thread(a, id, 1, 0, 0);
        }
    }
}

static void move_sel(App *a, int dir) {
    const Page *pg = &a->book.pages[a->page_index];
    int best = a->sel_line;
    for (int step = 1; step <= pg->count; step++) {
        int i = a->sel_line + dir * step;
        if (i < 0 || i >= pg->count) break;
        if (a->book.lines[pg->start + i].item_id > 0) { best = i; break; }
    }
    if (best != a->sel_line) {
        int old = a->sel_line;
        a->sel_line = best;
        app_repaint_rows(a, old, best);   /* two rows, not a whole page */
    }
}

/* Which content row sits under this screen y (or -1). Shares the geometry used
 * by render_page so the press highlight lands exactly on the touched line. */
static int line_from_y(App *a, int y) {
    if (a->geo.line_h <= 0 || a->book.np <= 0) return -1;
    if (a->page_index < 0 || a->page_index >= a->book.np) return -1;
    int row = (y - (a->geo.margin_y + a->geo.line_h)) / a->geo.line_h;
    const Page *pg = &a->book.pages[a->page_index];
    if (row < 0 || row >= pg->count) return -1;
    return row;
}

static Action map_tap(App *a, const InputEvent *ev, int *line_index) {
    int x = ev->x, y = ev->y;
    int h = a->geo.h;

    /* Toolbar first: the Hi button opens the menu, the ring refreshes. */
    int tb = render_toolbar_hit(&a->geo, x, y);
    if (tb == 1) return ACT_MENU;
    if (tb == 2) return ACT_REFRESH;
    /* Footer buttons: previous/next forum page, and back. */
    int fb = render_footer_btn_hit(&a->geo, x, y);
    if (fb == 3) return ACT_BACK;
    if (fb == 1) return ACT_PAGE_PREV;
    if (fb == 2) return ACT_PAGE_NEXT;

    /* List/input views use the full width for rows — no page-turn strips,
     * otherwise the left edge would steal taps meant for the keyboard. */
    if (a->view == V_LOGIN || a->view == V_MENU || a->view == V_SETTINGS) {
        if (y > h * 90 / 100) return ACT_BACK;
        int row = (y - (a->geo.margin_y + a->geo.line_h)) / a->geo.line_h;
        *line_index = row;
        return ACT_SELECT;
    }

    /* Tap-to-turn: a tap on a real entry always wins, otherwise the outer
     * thirds of the screen turn pages (Kindle's own habit). Swipe page-turning
     * is disabled — it forced a full e-ink refresh mid-gesture and felt frozen. */
    int row = (y - (a->geo.margin_y + a->geo.line_h)) / a->geo.line_h;
    if (row >= 0 && hit_item_line(a, row) >= 0) {
        *line_index = row;
        return ACT_SELECT;
    }
    /* image placeholders in a thread are tappable on their own */
    if (row >= 0 && a->view == V_THREAD && a->book.np > 0 &&
        a->page_index < a->book.np) {
        const Page *pg = &a->book.pages[a->page_index];
        if (row < pg->count &&
            a->book.lines[pg->start + row].kind == L_IMGPH) {
            *line_index = row;
            return ACT_IMGV;
        }
    }
    if (y > h * 88 / 100) return ACT_BACK;
    if (x >= a->geo.w * 62 / 100) return ACT_NEXT;
    if (x <  a->geo.w * 30 / 100) return ACT_PREV;
    *line_index = row;
    return ACT_SELECT;
}

/* Refresh: one frame with the ring rotated (cheap visual ack), then reload.
 * Three animated frames used to mean three full e-ink refreshes in a row. */
static void do_refresh(App *a) {
    if (a->book.np > 0) {
        a->refresh_phase = 2;
        app_draw(a, NULL);
    }
    a->refresh_phase = 0;
    /* Only the screen being refreshed loses its snapshot; the other one stays
     * cached so Back is still instant. */
    if (a->view == V_THREAD) {
        a->snap_thread.valid = 0;
        show_thread(a, a->cur_tid, a->thread_page, 0, 1);
    } else if (a->view == V_TOPICS) {
        a->snap_topics.valid = 0;
        show_topics(a, a->cur_fid, a->topic_page, 0, 1);
    } else if (a->view == V_FORUMS) {
        snap_invalidate_all(a);
        show_forums(a);
    } else {
        app_draw(a, NULL);
    }
}

static void reload(App *a) { do_refresh(a); }

static int export_sequence(App *a) {
    pw_begin(a);
    show_forums(a);
    if (show_topics(a, 2, 1, 0, 0)) { pw_end(a); return -1; }
    for (int i = 1; i < a->book.np; i++) { a->page_index = i; app_draw(a, NULL); }
    show_menu_view(a);
    for (int i = 1; i < a->book.np; i++) { a->page_index = i; app_draw(a, NULL); }
    /* preview the real first-run state: no account baked in */
    a->login_user[0] = 0;
    a->login_pass[0] = 0;
    show_login(a);
    for (int i = 1; i < a->book.np; i++) { a->page_index = i; app_draw(a, NULL); }
    show_settings(a);
    for (int i = 1; i < a->book.np; i++) { a->page_index = i; app_draw(a, NULL); }
    if (show_thread(a, 1579403, 1, 0, 0)) { pw_end(a); return -1; }
    for (int i = 1; i < a->book.np; i++) { a->page_index = i; app_draw(a, NULL); }
    if (a->thread_page < a->thread_last) {
        show_thread(a, a->cur_tid, 2, 1, 0);
        for (int i = a->page_index + 1; i < a->book.np; i++) {
            a->page_index = i; app_draw(a, NULL);
        }
    }
    pw_end(a);
    return 0;
}

int app_run(const AppOptions *opt) {
    App a;
    memset(&a, 0, sizeof(a));
    config_defaults(&a.cfg,
#ifdef PC_BUILD
                    1
#else
                    0
#endif
                    );
    config_load(&a.cfg, opt->config_path);
    if (opt->font_path && *opt->font_path)
        str_copy(a.cfg.font_file, sizeof(a.cfg.font_file), opt->font_path);
    if (opt->offline) a.cfg.offline = 1;
    if (opt->no_login) a.cfg.no_login = 1;
    a.export_preview = opt->export_preview;
#ifdef PC_BUILD
    if (opt->screen && *opt->screen) {
        static char screen_env[128];
        snprintf(screen_env, sizeof(screen_env), "HIPDA_SCREEN=%s", opt->screen);
        putenv(screen_env);
    }
#endif
    config_dump(&a.cfg);

    if (opt->diag) return app_diag();

    if (ui_init(&a.ui)) { log_msg("display init failed"); return 1; }
    ui_set_refresh_mode(a.cfg.refresh_mode);
    apply_rotation(&a);
    log_msg("geometry: %dx%d@%d scale=%d cols=%d rows=%d",
            a.geo.w, a.geo.h, a.geo.dpi, a.geo.scale, a.geo.cols, a.geo.rows);
    if (canvas_init(&a.canvas, a.geo.w, a.geo.h)) return 1;
    if (font_load(&a.font, a.cfg.font_file)) {
        static const char *cands[] = {
            "/mnt/us/extensions/hipda/fonts/unifont.bin",
            "/mnt/us/hipda/fonts/unifont.bin",
            "fonts/unifont.bin",
            "unifont.bin"
        };
        for (unsigned i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
            if (!strcmp(cands[i], a.cfg.font_file)) continue;
            if (!font_load(&a.font, cands[i])) {
                log_msg("font: recovered from fallback %s", cands[i]);
                str_copy(a.cfg.font_file, sizeof(a.cfg.font_file), cands[i]);
                break;
            }
        }
    }
    if (!a.font.records) {
        log_msg("FATAL: no usable HUF1 font; draw test pattern instead of blank screen");
        canvas_fill(&a.canvas, 255);
        canvas_frame(&a.canvas, 0, 0, a.canvas.w, a.canvas.h, 0, 8);
        for (int y = 0; y < a.canvas.h; y += a.canvas.h / 10)
            canvas_rect(&a.canvas, 0, y, a.canvas.w, a.canvas.h / 60, 0);
        ui_present(a.canvas.px, a.canvas.w, a.canvas.h, 1);
        return 1;
    }
    book_init(&a.book, &a.geo);
    book_init(&a.snap_topics.book, &a.geo);
    book_init(&a.snap_thread.book, &a.geo);
    str_init(&a.img_urls);
    str_init(&a.list_urls);
    net_init(&a.cfg);
    input_set_screen(a.geo.w, a.geo.h);
    input_set_calib(a.cfg.cal_swap, a.cfg.cal_flip_x, a.cfg.cal_flip_y);
    input_open();
    a.press_line = -1;

    if (opt->dump_target) {
        char url[HIPDA_URL_LEN];
        if (strstr(opt->dump_target, "://"))
            str_copy(url, sizeof(url), opt->dump_target);
        else {
            char base[HIPDA_URL_LEN];
            snprintf(base, sizeof(base), "%s%s", a.cfg.forum_server, HIPDA_FORUM_PATH);
            snprintf(url, sizeof(url), "%s%s", base, opt->dump_target);
        }
        Str html; str_init(&html);
        int r = net_fetch(&a.cfg, url, &html, 1);
        fwrite(html.data, 1, html.len, stdout);
        log_msg("dump: status=%d bytes=%zu", r, html.len);
        str_free(&html);
        goto done;
    }

    if (!a.cfg.no_login && a.cfg.username[0]) {
        int r = net_login(&a.cfg);
        if (r != NET_OK) log_msg("login: status %d", r);
    }

    if (a.export_preview) { export_sequence(&a); goto done; }

    /* No credentials yet: start at the login screen so anyone can sign in. */
    if (!a.cfg.username[0] && !a.cfg.no_login) {
        show_login(&a);
    } else {
        show_forums(&a);
    }

    for (;;) {
        InputEvent ev;
        reap_background();            /* collect finished partial-download children */
        /* No event (timeout) or a discarded drag must NOT repaint: the old
         * code redrew the whole page on every empty poll, which turned any
         * gesture into a storm of full e-ink refreshes. Poll a little faster
         * only while a background download is finishing. */
        if (input_read(&ev, g_bg_download > 0 ? 1000 : 300000) <= 0) continue;
        /* touch panel speaks panel coordinates; rotate into logical space
         * before anything looks at ev.x/ev.y */
        if (a.cfg.rotation != 0 && ev.tap) rotate_touch(&a, &ev.x, &ev.y);
        Action act = ev.act;
        int line_index = -1;

        /* Finger-down feedback: invert just the touched row (partial refresh). */
        if (ev.tap && act == ACT_PRESS) {
            if (a.view != V_CALIB) {
                int row = line_from_y(&a, ev.y);
                if (row >= 0 && row != a.press_line)
                    app_repaint_row(&a, row, 1);
                else if (row < 0 && a.press_line >= 0)
                    app_repaint_row(&a, a.press_line, 0);
            }
            continue;
        }
        if (ev.tap) act = map_tap(&a, &ev, &line_index);

        if (a.view == V_CALIB) {
            if (act == ACT_QUIT) break;
            if (act == ACT_BACK || act == ACT_MENU) { show_settings(&a); continue; }
            if (act == ACT_SELECT && ev.tap) calib_tap(&a, &ev);
            continue;
        }

        if (a.view == V_MESSAGE) {
            if (act == ACT_QUIT) break;
            if (a.msg_action == 2) goto done;      /* quit confirmed */
            if (a.msg_action == 1) {
                if (act == ACT_BACK) {
                    str_copy(a.msg_title, sizeof(a.msg_title), "HiPDA 已退出");
                    str_copy(a.msg_body, sizeof(a.msg_body),
                             "触摸已交还给系统。\n点屏幕任意位置返回 Kindle。");
                    a.view_before_msg = V_FORUMS;
                    app_draw(&a, NULL);
                    goto done;
                }
                a.msg_action = 0;
            }
            a.view = a.view_before_msg;
            app_draw(&a, NULL);
            continue;
        }

        if (a.view == V_LOGIN) {
            if (act == ACT_QUIT) break;
            if (act == ACT_NEXT || act == ACT_PREV) continue;
            input_view_action(&a, act, line_index, ev.x);
            continue;
        }

        switch (act) {
            case ACT_QUIT:    goto done;
            case ACT_NEXT:    a.press_line = -1; turn_next(&a); break;
            case ACT_PREV:    a.press_line = -1; turn_prev(&a); break;
            case ACT_BACK:
                if (a.view == V_THREAD) {
                    /* park the thread so re-opening it is instant */
                    snap_put(&a.snap_thread, &a.book, a.cur_tid, a.thread_page,
                             a.thread_last, a.page_index, a.sel_line);
                    if (a.in_fav) { a.in_fav = 0; show_fav_list(&a); }
                    else show_topics(&a, a.cur_fid, a.topic_page, 0, 0);
                }
                else if (a.view == V_TOPICS) show_forums(&a);
                else if (a.view == V_FAVLIST) show_menu_view(&a);
                else if (a.view == V_MENU || a.view == V_TEXT ||
                         a.view == V_SETTINGS) show_menu_view(&a);
                else {
                    app_message(&a, "退出 HiPDA？",
                                "用顶部 Hi 菜单 → 退出应用\n或再点一次屏幕底部退出。");
                    a.msg_action = 1;
                }
                break;
            /* Footer page buttons = plain book page turns. Turning past the
             * last page auto-fetches the next forum page (topic lists and
             * threads) — the ZhihuReader-style infinite flow, no more
             * "1/8 but nothing happens" confusion. */
            case ACT_PAGE_PREV: turn_prev(&a); break;
            case ACT_PAGE_NEXT: turn_next(&a); break;
            case ACT_IMGV: {
                int iid = 0;
                if (line_index >= 0 && a.book.np > 0 &&
                    a.page_index < a.book.np) {
                    const Page *pg = &a.book.pages[a.page_index];
                    if (line_index < pg->count)
                        iid = a.book.lines[pg->start + line_index].item_id;
                }
                if (iid > 0) img_open(&a, iid);
                break;
            }
            case ACT_HOME:    show_menu_view(&a); break;
            case ACT_MENU:    show_menu_view(&a); break;
            case ACT_REFRESH: reload(&a); break;
            case ACT_UP:      move_sel(&a, -1); break;
            case ACT_DOWN:    move_sel(&a, 1); break;
            case ACT_SELECT: {
                int old = a.press_line;
                a.press_line = -1;
                if (line_index >= 0) {
                    if (activate_is_item(&a, line_index)) activate(&a, line_index);
                    else if (old >= 0) app_repaint_row(&a, old, 0);  /* clear only */
                } else if (a.sel_line >= 0 && activate_is_item(&a, a.sel_line)) {
                    activate(&a, a.sel_line);      /* physical OK key */
                } else if (old >= 0) {
                    app_repaint_row(&a, old, 0);
                }
                break;
            }
            default: break;
        }
        if (a.quit_now) goto done;
    }

done:
    pw_end(&a);
    input_close();
    net_close();
    book_free(&a.book);
    book_free(&a.snap_topics.book);
    book_free(&a.snap_thread.book);
    str_free(&a.img_urls);
    str_free(&a.list_urls);
    canvas_free(&a.canvas);
    font_free(&a.font);
    ui_close();
    log_close();
    return 0;
}
