#include "layout.h"
#include "gbk.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void geometry_compute(Geometry *g, int w, int h, int dpi, int font_scale) {
    if (dpi <= 0) dpi = 167;
    if (w <= 0) w = 600;
    if (h <= 0) h = 800;
    int scale = font_scale > 0 ? font_scale : (dpi + 50) / 100;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    g->w = w; g->h = h; g->dpi = dpi; g->scale = scale;
    g->cell_w = 8 * scale;
    g->cell_h = 16 * scale;
    g->line_h = g->cell_h + 6 * scale;   /* a little more air between lines */
    g->margin_x = 2 * g->cell_w;
    g->margin_y = g->cell_h;
    g->cols = (w - 2 * g->margin_x) / g->cell_w;
    if (g->cols < 12) g->cols = 12;
    g->rows = (h - 2 * g->margin_y - 2 * g->line_h) / g->line_h;
    if (g->rows < 4) g->rows = 4;
}

void book_init(Book *b, const Geometry *g) {
    memset(b, 0, sizeof(*b));
    b->cols = g->cols; b->rows = g->rows;
}
void book_reset(Book *b) {
    for (int i = 0; i < b->n; i++) free(b->lines[i].text);
    b->n = 0; b->np = 0;
}
void book_free(Book *b) {
    book_reset(b);
    free(b->lines); free(b->pages);
    memset(b, 0, sizeof(*b));
}
static void push_line(Book *b, LineKind kind, char *text, int indent,
                      int item_id, int forum_page) {
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 128;
        b->lines = xrealloc(b->lines, (size_t)b->cap * sizeof(Line));
    }
    Line *l = &b->lines[b->n++];
    l->kind = kind; l->text = text; l->indent = indent;
    l->item_id = item_id; l->forum_page = forum_page;
}
int book_add_line(Book *b, LineKind kind, const char *text, int indent,
                  int item_id, int forum_page) {
    push_line(b, kind, xstrdup(text ? text : ""), indent, item_id, forum_page);
    return b->n;
}

void book_add_card(Book *b, const char *title, const char *sub, int indent,
                   int item_id, int forum_page) {
    /* clip the title too: the card frame is one screen wide */
    char ttl[HIPDA_NAME_LEN];
    int tcells = b->cols > 4 ? b->cols - 5 : 20;
    int cells = 0;
    size_t used = 0;
    const char *tp = title ? title : "";
    while (*tp && used + 4 < sizeof(ttl)) {
        unsigned char c = (unsigned char)*tp;
        int w = 1, n = 1;
        if (c >= 0xF0) { w = 2; n = 4; }
        else if (c >= 0xE0) { w = 2; n = 3; }
        else if (c >= 0xC0) { w = 1; n = 2; }
        if (cells + w > tcells) break;
        memcpy(ttl + used, tp, (size_t)n);
        used += (size_t)n;
        ttl[used] = 0;
        cells += w;
        tp += n;
    }
    if (*tp) memcpy(ttl + used, "...", 4);
    push_line(b, L_CARD_T, xstrdup(ttl), indent, item_id, forum_page);
    /* clip the subtitle to the card width so it never spills past the frame;
     * the card is exactly two rows tall, no wrapping */
    char clip[HIPDA_NAME_LEN];
    int maxcells = b->cols > 4 ? b->cols - 4 : 20;
    int scells = 0;
    size_t sused = 0;
    const char *p = sub ? sub : "";
    while (*p && sused + 4 < sizeof(clip)) {
        unsigned char c = (unsigned char)*p;
        int w = 1, n = 1;
        if (c >= 0xF0) { w = 2; n = 4; }
        else if (c >= 0xE0) { w = 2; n = 3; }
        else if (c >= 0xC0) { w = 1; n = 2; }
        if (scells + w > maxcells - 2) break;
        memcpy(clip + sused, p, (size_t)n);
        sused += (size_t)n;
        clip[sused] = 0;
        scells += w;
        p += n;
    }
    if (*p) memcpy(clip + sused, "...", 4);   /* truncated */
    push_line(b, L_CARD_B, xstrdup(clip), indent, 0, forum_page);
}
/* Keep Chinese/ASCII closing punctuation attached to the previous line
 * instead of letting it dangle at the start of the next one. */
static int is_closing_punct(uint32_t cp) {
    if (cp == ',' || cp == '.' || cp == ';' || cp == ':' || cp == '!' ||
        cp == '?' || cp == ')' || cp == ']' || cp == '}') return 1;
    switch (cp) {
        case 0xFF0C: case 0xFF0E: case 0x3002: case 0xFF1B: case 0xFF1A:
        case 0xFF01: case 0xFF1F: case 0xFF09: case 0x300B: case 0x300D:
        case 0x300F: case 0x201D: case 0x2019: case 0x3001: case 0x300A:
            return 1;
        default: return 0;
    }
}

int book_wrap_text(Book *b, const Geometry *g, LineKind kind, const char *text,
                   int indent, int item_id, int forum_page) {
    if (!text) text = "";
    int avail = g->cols - indent;
    if (avail < 4) avail = 4;
    Str line; str_init(&line);
    int used = 0;
    const char *p = text, *end = text + strlen(text);
    int produced = 0;
    while (p < end) {
        const char *q = p;
        uint32_t cp = utf8_next(&p, end);
        if (cp == '\n') {
            push_line(b, kind, xstrdup(line.data), indent, item_id, forum_page);
            produced++;
            str_clear(&line); used = 0;
            continue;
        }
        int cw = utf8_cells(cp);
        if (cw <= 0) continue;
        if (used + cw > avail && used > 0 && !is_closing_punct(cp)) {
            push_line(b, kind, xstrdup(line.data), indent, item_id, forum_page);
            produced++;
            str_clear(&line); used = 0;
        }
        str_append_n(&line, q, (size_t)(p - q));
        used += cw;
    }
    push_line(b, kind, xstrdup(line.data), indent, item_id, forum_page);
    produced++;
    str_free(&line);
    return produced;
}

void book_finalize(Book *b, const Geometry *g) {
    b->np = 0;
    b->rows = g->rows;
    int rows = g->rows < 1 ? 1 : g->rows;
    for (int i = 0; i < b->n; ) {
        if (b->np == b->pcap) {
            b->pcap = b->pcap ? b->pcap * 2 : 16;
            b->pages = xrealloc(b->pages, (size_t)b->pcap * sizeof(Page));
        }
        int count = b->n - i < rows ? b->n - i : rows;
        /* never split a L_CARD_T / L_CARD_B pair across pages */
        if (count > 0 && count < rows &&
            b->lines[i + count - 1].kind == L_CARD_T) count--;
        if (count == 0) count = rows < b->n - i ? rows : b->n - i;
        Page *pg = &b->pages[b->np++];
        pg->start = i;
        pg->count = count;
        pg->forum_page = b->lines[i].forum_page;
        i += count;
    }
    if (b->np == 0) {
        b->pcap = 1;
        b->pages = xrealloc(b->pages, sizeof(Page));
        b->pages[0].start = 0; b->pages[0].count = 0; b->pages[0].forum_page = 1;
        b->np = 1;
    }
}

/* Forum home, curated 2026-11-18 per the current board list. Order matters:
 * Discovery and Buy & Sell get the most traffic and lead the page; the rest
 * follow in reading order. Each section is a bold title over a smaller,
 * fainter description line (tapping either opens the board — the desc row
 * has item_id 0 and hit-testing falls through to the title above it). */
static const struct {
    int         fid;
    const char *name;
    const char *desc;
} kHomeForums[] = {
    { 2,  "Discovery",                  "发现与分享 · 新主题集散地" },
    { 6,  "Buy & Sell 交易服务区",       "最IN数码产品热卖场，易手产品交易区，Hot!" },
    { 0,  NULL, NULL },                 /* separator: hot boards above */
    { 7,  "Geek Talks · 奇客怪谈",       "业界、Bluetooth、WiFi、嵌入Linux、看法、非主流设备。" },
    { 9,  "Smartphone",                 "智能手机：Windows Mobile/Windows Phone, Android, iOS, webOS, Symbian, and other" },
    { 56, "iPhone, iPod Touch，iPad",   "可以用两根手指头同时摸的手机和上网设备" },
    { 60, "Android, Chrome, & Google",  "Cloud Computing of Titan" },
    { 12, "PalmOS，Treo",               "PalmOS的各种软件、硬件，Treo手机" },
    { 22, "麦客爱苹果",                  "G4，Powerbook，iMac，iPod，OS X...Think Different，Live Elegant" },
    { 14, "Windows Mobile，PocketPC，HPC", "WinCE/PPC上的各种硬件评测，软件使用，选择，比较，问题与解答。" },
    { 50, "DC,NB,MP3,Gadgets...",       "笔记本电脑、数码相机、MP3，各种电子玩具、游戏机" },
    { 24, "意欲蔓延",                    "电影、音乐、读书、美术、摄影、English" },
    { 23, "随笔与个人文集",              "在岁月的书签上，留下我的痕迹" },
    { 25, "吃喝玩乐",                    "美食、旅行~~~" },
    { 51, "La Femme",                   "三月三日天气新，HP里多丽人" },
};

int book_from_forums(Book *b, const Geometry *g, const Config *cfg) {
    (void)cfg;                            /* home layout is curated above */
    book_reset(b);
    str_copy(b->title, sizeof(b->title), "HiPDA 论坛");
    str_copy(b->subtitle, sizeof(b->subtitle), "选择板块");
    b->fid = 0; b->tid = 0;
    /* The page header already shows b->title; do not repeat it in the body. */
    book_wrap_text(b, g, L_NOTE, "按类别浏览板块 · 点卡片进入", 0, 0, 1);
    for (unsigned i = 0; i < sizeof(kHomeForums) / sizeof(kHomeForums[0]); i++) {
        if (!kHomeForums[i].name) {       /* group caption */
            book_add_line(b, L_BLANK, "", 0, 0, 1);
            continue;
        }
        /* a group caption precedes the hot boards and the 苹果 board */
        if (kHomeForums[i].fid == 2)
            book_add_line(b, L_GROUP, "热门推荐", 0, 0, 1);
        if (kHomeForums[i].fid == 7)
            book_add_line(b, L_GROUP, "数码与系统", 0, 0, 1);
        if (kHomeForums[i].fid == 56)
            book_add_line(b, L_GROUP, "苹果专区", 0, 0, 1);
        if (kHomeForums[i].fid == 50)
            book_add_line(b, L_GROUP, "其他", 0, 0, 1);
        book_add_card(b, kHomeForums[i].name,
                      kHomeForums[i].desc ? kHomeForums[i].desc : "进入板块",
                      0, kHomeForums[i].fid, 1);
    }
    book_finalize(b, g);
    return b->n;
}

/* Start an appended Discuz page on a fresh book page. Without this, the
 * trailing partial page would be silently filled with new posts and the
 * reader would skip them when turning the page. */
static void pad_to_page_boundary(Book *b, const Geometry *g, int forum_page) {
    if (!b->n) return;
    int rows = g->rows > 0 ? g->rows : 1;
    int pad = (rows - (b->n % rows)) % rows;
    for (int i = 0; i < pad; i++)
        book_add_line(b, L_BLANK, "", 0, 0, forum_page);
}

int book_from_topics(Book *b, const Geometry *g, const TopicList *list,
                     const Forum *forum, int append) {
    if (!append) book_reset(b);
    else pad_to_page_boundary(b, g, list->page);
    const char *fname = forum ? forum->name : "主题列表";
    if (!append) {
        snprintf(b->title, sizeof(b->title), "%s", fname);
        snprintf(b->subtitle, sizeof(b->subtitle), "第 %d / %d 页",
                 list->page, list->last_page);
        b->fid = list->fid; b->tid = 0;
    }
    if (append) {
        char note[128];
        snprintf(note, sizeof(note), "—— 论坛第 %d 页 ——", list->page);
        book_add_line(b, L_NOTE, note, 0, 0, list->page);
        book_add_line(b, L_BLANK, "", 0, 0, list->page);
    }
    for (int i = 0; i < list->n; i++) {
        const Topic *t = &list->v[i];
        char row[HIPDA_NAME_LEN];
        snprintf(row, sizeof(row), "%s%s", t->is_sticky ? "〔顶〕" : "",
                 t->title[0] ? t->title : "(无标题)");
        char meta[256];
        snprintf(meta, sizeof(meta), "%s  ·  回复 %d%s",
                 t->author[0] ? t->author : "匿名", t->replies,
                 t->has_attach ? "  · 有图" : "");
        book_add_card(b, row, meta, 0, t->tid, list->page);
    }
    book_finalize(b, g);
    return b->n;
}

/* Add one post body, splitting image placeholders (\x02URL\x02 lines) into
 * tappable rows. img_urls collects every URL in encounter order; the row's
 * item_id is the URL hash so taps can find the URL even after a snapshot
 * round-trip. img_mode: 0 = plain "〔图片〕" text, 1 = tappable placeholder. */
static void book_add_body(Book *b, const Geometry *g, const char *body,
                          int forum_page, Str *img_urls, int img_mode) {
    const char *p = body;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;

        char *m = line[0] == '\x02' ? line : NULL;
        if (m && len >= 2 && line[len - 1] == '\x02') {
            line[len - 1] = 0;
            const char *url = line + 1;
            if (img_mode == 1 && url[0]) {
                unsigned long long h = fnv1a(url);
                int id = (int)(h & 0x7FFFFFFF);
                if (id == 0) id = 1;
                if (img_urls) str_append(img_urls, url), str_append(img_urls, "\n");
                char ph[HIPDA_NAME_LEN];
                snprintf(ph, sizeof(ph), "[ 图片 · 点按显示 ]");
                book_add_line(b, L_IMGPH, ph, 0, id, forum_page);
            } else {
                book_add_line(b, L_BODY, "〔图片〕", 0, 0, forum_page);
            }
        } else if (len > 0) {
            book_wrap_text(b, g, L_BODY, line, 0, 0, forum_page);
        }
        p = nl ? nl + 1 : NULL;
    }
}

int book_from_thread(Book *b, const Geometry *g, const ThreadPage *thread,
                     int append, Str *img_urls, int img_mode) {
    if (!append) book_reset(b);
    else pad_to_page_boundary(b, g, thread->page);
    if (!append) {
        str_copy(b->title, sizeof(b->title),
                 thread->title[0] ? thread->title : "帖子正文");
        snprintf(b->subtitle, sizeof(b->subtitle), "论坛第 %d / %d 页",
                 thread->page, thread->last_page);
        b->fid = 0; b->tid = thread->tid;
    } else {
        char note[128];
        snprintf(note, sizeof(note), "—— 论坛第 %d 页 ——", thread->page);
        book_add_line(b, L_NOTE, note, 0, 0, thread->page);
        book_add_line(b, L_BLANK, "", 0, 0, thread->page);
    }
    for (int i = 0; i < thread->nposts; i++) {
        const Post *p = &thread->posts[i];
        char meta[HIPDA_NAME_LEN];
        snprintf(meta, sizeof(meta), "%d 楼  %s  %s", p->floor,
                 p->author[0] ? p->author : "匿名", p->dateline);
        if (i > 0 || append) book_add_line(b, L_SEP, "", 0, 0, thread->page);
        book_add_line(b, L_POSTHEAD, meta, 0, 0, thread->page);
        book_add_body(b, g, p->body.data, thread->page, img_urls, img_mode);
        book_add_line(b, L_BLANK, "", 0, 0, thread->page);
    }
    book_finalize(b, g);
    return b->n;
}

static const char *kind_name(LineKind k) {
    switch (k) {
        case L_TITLE: return "title";
        case L_META:  return "meta";
        case L_BODY:  return "body";
        case L_ITEM:  return "item";
        case L_SEP:   return "sep";
        case L_NOTE:  return "note";
        case L_KEYS:  return "keys";
        case L_CTRL:  return "ctrl";
        case L_BTN:   return "btn";
        case L_IMGPH: return "imgph";
        case L_FTITLE: return "ftitle";
        case L_FDESC: return "fdesc";
        default:      return "blank";
    }
}
static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (*p < 0x20) fprintf(f, "\\u%04x", *p);
                else fputc(*p, f);
        }
    }
    fputc('"', f);
}
int book_write_json(Book *b, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "{\n\"title\": "); json_str(f, b->title);
    fprintf(f, ",\n\"subtitle\": "); json_str(f, b->subtitle);
    fprintf(f, ",\n\"fid\": %d, \"tid\": %d, \"cols\": %d, \"rows\": %d,\n",
            b->fid, b->tid, b->cols, b->rows);
    fputs("\"pages\": [\n", f);
    for (int pi = 0; pi < b->np; pi++) {
        Page *pg = &b->pages[pi];
        fprintf(f, "{\"index\": %d, \"forum_page\": %d, \"png\": \"page_%03d.png\", \"lines\": [",
                pi, pg->forum_page, pi);
        for (int i = 0; i < pg->count; i++) {
            Line *l = &b->lines[pg->start + i];
            fprintf(f, "%s{\"kind\": \"%s\", \"text\": ", i ? ", " : "", kind_name(l->kind));
            json_str(f, l->text);
            fprintf(f, ", \"indent\": %d, \"item_id\": %d, \"forum_page\": %d}",
                    l->indent, l->item_id, l->forum_page);
        }
        fprintf(f, "]},\n");
    }
    fseek(f, -2, SEEK_CUR);     /* drop trailing ",\n" (safe: np >= 1) */
    fputs("\n]\n}\n", f);
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- menus -- */

int book_from_menu(Book *b, const Geometry *g, const char *title,
                   const char *subtitle, const char *const *items, int n) {
    book_reset(b);
    str_copy(b->title, sizeof(b->title), title && *title ? title : "菜单");
    str_copy(b->subtitle, sizeof(b->subtitle), subtitle ? subtitle : "");
    b->fid = 0; b->tid = 0;
    for (int i = 0; i < n; i++) {
        char row[HIPDA_NAME_LEN];
        /* "NN " + title: the L_NUM painter reads the first two chars as the
         * index badge and the rest as the bold label. */
        snprintf(row, sizeof(row), "%02d %s", i + 1, items[i] ? items[i] : "");
        book_add_line(b, L_NUM, row, 0, i + 1, 1);
    }
    book_finalize(b, g);
    return b->n;
}

int book_from_text(Book *b, const Geometry *g, const char *title,
                   const char *text) {
    book_reset(b);
    str_copy(b->title, sizeof(b->title), title && *title ? title : "内容");
    b->subtitle[0] = 0;
    b->fid = 0; b->tid = 0;
    const char *p = text ? text : "";
    if (!*p) book_add_line(b, L_NOTE, "（这里没有内容）", 0, 0, 1);
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char stackbuf[1024];
        if (n >= sizeof(stackbuf)) n = sizeof(stackbuf) - 1;
        memcpy(stackbuf, p, n);
        stackbuf[n] = 0;
        char *trimmed = str_trim(stackbuf);
        if (*trimmed) book_wrap_text(b, g, L_BODY, trimmed, 0, 0, 1);
        else book_add_line(b, L_BLANK, "", 0, 0, 1);
        if (!nl) break;
        p = nl + 1;
    }
    book_finalize(b, g);
    return b->n;
}
