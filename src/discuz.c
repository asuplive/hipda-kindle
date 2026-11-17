#include "discuz.h"
#include "html.h"
#include "util.h"

/* defined further below; used by title extraction in the topic-list parser */
static void strip_img_marks(char *s);

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* From GreenSkinMonster/hipda -> HiUtils.java FORUMS[] */
const Forum kForums[] = {
    { 2,  "Discovery" },
    { 6,  "Buy & Sell" },
    { 7,  "Geek Talks" },
    { 59, "E-INK" },
    { 12, "PalmOS" },
    { 57, "疑似机器人" },
    { 63, "已完成交易" },
    { 62, "Joggler" },
    { 5,  "站务与公告" },
    { 9,  "Smartphone" },
    { 56, "iPhone, iPod Touch, iPad" },
    { 60, "Android, Chrome, & Google" },
    { 14, "Windows Mobile, PocketPC, HPC" },
    { 22, "麦客爱苹果" },
    { 50, "DC,NB,MP3,Gadgets" },
    { 24, "意欲蔓延" },
    { 23, "随笔与个人文集" },
    { 25, "吃喝玩乐" },
    { 51, "La Femme" },
    { 65, "改版建议" },
    { 64, "只讨论2.0" },
};
const int kForumsCount = (int)(sizeof(kForums) / sizeof(kForums[0]));

const Forum *forum_by_fid(int fid) {
    for (int i = 0; i < kForumsCount; i++)
        if (kForums[i].fid == fid) return &kForums[i];
    return NULL;
}

int discuz_detect_utf8(const char *raw, size_t len) {
    size_t n = len < 2048 ? len : 2048;
    for (size_t i = 0; i + 8 < n; i++) {
        if (raw[i] != '<' || !str_ncaseeq(raw + i, "<meta", 5)) continue;
        const char *gt = memchr(raw + i, '>', n - i);
        if (!gt) break;
        size_t m = (size_t)(gt - (raw + i));
        for (size_t k = 0; k + 7 < m; k++) {
            if (str_ncaseeq(raw + i + k, "charset", 7)) {
                const char *c = raw + i + k + 7;
                while (c < gt && (*c == ' ' || *c == '=' || *c == '"' || *c == '\'')) c++;
                if (c + 3 <= gt && str_ncaseeq(c, "utf", 3)) return 1;
                if (c + 2 <= gt && str_ncaseeq(c, "gb", 2)) return 0;
            }
        }
        i += m;
    }
    return 0;   /* forum default is GBK (verified 2026-11-15) */
}

int discuz_is_cloudflare(const char *html) {
    return html && (strstr(html, "Just a moment") ||
                    strstr(html, "cf-browser-verification") ||
                    strstr(html, "cf_chl_"));
}
int discuz_is_gate(const char *html) {
    /* A real gate page contains BOTH the login form and an error message.
     * Looking for the message alone would misclassify any post that quotes
     * the forum's own "not logged in" wording (this really happened). */
    if (!html) return 0;
    int form = strstr(html, "id=\"loginform\"") ||
               strstr(html, "class=\"gateform\"") ||
               strstr(html, "name=\"login\"");
    int msg  = strstr(html, "您无权进行当前操作") ||
               strstr(html, "无权访问该版块") ||
               strstr(html, "您还未登录");
    return form && msg;
}
int discuz_login_ok(const char *html) {
    return html && (strstr(html, "欢迎您回来") || strstr(html, "登录成功") ||
                    strstr(html, "succeed") || strstr(html, "window.location"));
}

static int hidden_value(const char *html, const char *name, char *out, size_t cap) {
    char key[64];
    snprintf(key, sizeof(key), "name=\"%s\"", name);
    const char *p = strstr(html, key);
    if (!p) return -1;
    const char *v = strstr(p + strlen(key), "value=");
    if (!v) return -1;
    v += 6;
    while (*v == ' ' || *v == '\t') v++;
    char q = (*v == '"' || *v == '\'') ? *v++ : 0;
    size_t n = 0;
    while (v[n] && n + 1 < cap && (q ? v[n] != q : !isspace((unsigned char)v[n]) && v[n] != '>')) n++;
    memcpy(out, v, n); out[n] = 0;
    return 0;
}

int discuz_parse_login_form(const char *html, char *formhash, size_t hcap,
                            char *sid, size_t scap) {
    if (formhash && hcap) formhash[0] = 0;
    if (sid && scap) sid[0] = 0;
    if (!html) return -1;
    if (formhash) hidden_value(html, "formhash", formhash, hcap);
    if (sid) hidden_value(html, "sid", sid, scap);
    return (formhash && formhash[0]) ? 0 : -1;
}

int discuz_last_page(const char *html) {
    int last = 1;
    if (!html) return 1;
    /* Only trust the pager block(s). Post bodies quote links to OTHER
     * threads whose page= numbers would poison a whole-document scan
     * (verified against a 105-page live thread on 2026-11-15). */
    const char *p = html;
    while ((p = strstr(p, "class=\"pages\"")) != NULL) {
        char *block = html_element_slice(p, "class=\"pages\"", "div");
        const char *scope = block ? block : p;
        const char *q = scope;
        while ((q = strstr(q, "page=")) != NULL) {
            q += 5;
            int v = 0, digits = 0;
            while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; digits++; }
            if (digits && v > last && v < 100000) last = v;
        }
        /* current page <strong>N</strong> (the last page has no link) */
        q = scope;
        while (q && (q = strstr(q, "<strong>"))) {
            q += 8;
            int v = 0, digits = 0;
            while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; digits++; }
            if (digits && v > last && v < 100000) last = v;
        }
        free(block);
        p += 13;
    }
    if (last == 1) {
        /* template without class="pages": fall back to the whole document */
        const char *q = html;
        while ((q = strstr(q, "page=")) != NULL) {
            q += 5;
            int v = 0, digits = 0;
            while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; digits++; }
            if (digits && v > last && v < 100000) last = v;
        }
    }
    return last;
}

static const char *back_to_lt(const char *base, const char *p) {
    while (p > base && *p != '<') p--;
    return p;
}

/* Read the plain text / first integer inside the next <tag> after p. */
static int tag_content(const char *p, const char *tag, char *out, size_t cap,
                       int want_number) {
    char key[32];
    snprintf(key, sizeof(key), "<%s", tag);
    const char *s = strstr(p, key);
    if (!s) return -1;
    const char *gt = strchr(s, '>');
    if (!gt) return -1;
    const char *e = strchr(gt + 1, '<');
    if (!e || e <= gt + 1) return -1;
    if (want_number) {
        const char *q = gt + 1;
        while (q < e && !isdigit((unsigned char)*q)) q++;
        if (q >= e || !isdigit((unsigned char)*q)) return -1;
        int v = parse_int(q, -1);
        if (v < 0) return -1;
        if (out && cap) snprintf(out, cap, "%d", v);
        return v;
    }
    Str tmp; str_init(&tmp);
    html_to_text(gt + 1, (size_t)(e - (gt + 1)), &tmp);
    str_copy(out, cap, str_trim(tmp.data));
    str_free(&tmp);
    return out && out[0] ? 0 : -1;
}

/* Walk every occurrence of `marker` and return the first link that actually
 * HAS text. The live template puts an icon-only <a href="viewthread.php?tid=...">
 * (the folder image) before the title link, so taking the first match blindly
 * yields an empty title on real pages. */
static int extract_a_text(const char *p, const char *marker, char *out, size_t cap) {
    const char *a = p;
    while ((a = strstr(a, marker)) != NULL) {
        const char *gt = strchr(a, '>');
        if (!gt) break;
        const char *e = strstr(gt + 1, "</a>");
        if (!e) break;
        Str tmp; str_init(&tmp);
        html_to_text(gt + 1, (size_t)(e - (gt + 1)), &tmp);
        str_copy(out, cap, str_trim(tmp.data));
        str_free(&tmp);
        if (out[0]) return 0;
        a = gt + 1;
    }
    return -1;
}

static int extract_element_text(const char *p, const char *marker,
                                const char *tag, char *out, size_t cap) {
    char *slice = html_element_slice(p, marker, tag);
    if (!slice) return -1;
    Str tmp; str_init(&tmp);
    html_to_text(slice, strlen(slice), &tmp);
    str_copy(out, cap, str_trim(tmp.data));
    str_free(&tmp);
    free(slice);
    return out[0] ? 0 : -1;
}

void topic_list_push(TopicList *t, const Topic *item) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 32;
        t->v = xrealloc(t->v, (size_t)t->cap * sizeof(Topic));
    }
    t->v[t->n++] = *item;
}

void topic_list_free(TopicList *t) {
    free(t->v); t->v = NULL; t->n = t->cap = 0;
}

int discuz_parse_topic_list(const char *html, TopicList *out, int fid, int page) {
    if (!html || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->fid = fid;
    out->page = page;
    out->last_page = discuz_last_page(html);

    static const char *M_NORMAL = "id=\"normalthread_";
    static const char *M_STICK  = "id=\"stickthread_";
    const char *p = html;
    for (;;) {
        const char *a = strstr(p, M_NORMAL);
        const char *b = strstr(p, M_STICK);
        const char *hit = a && (!b || a < b) ? a : b;
        if (!hit) break;
        int sticky = (hit == b);
        const char *start = back_to_lt(html, hit);
        char *slice = html_element_slice(start, sticky ? M_STICK : M_NORMAL, "tbody");
        if (!slice) slice = xstrdup(start);
        Topic item;
        memset(&item, 0, sizeof(item));
        item.is_sticky = sticky;
        char tidbuf[32] = {0};
        if (!url_param(slice, "tid", tidbuf, sizeof(tidbuf))) item.tid = parse_int(tidbuf, 0);
        if (item.tid <= 0) {
            /* Discuz 7.2 sometimes uses thread-123-1-1.html style links */
            const char *th = strstr(slice, "thread-");
            if (th) item.tid = parse_int(th + 7, 0);
        }
        if (item.tid > 0) {
            extract_a_text(slice, "viewthread.php?tid=", item.title, sizeof(item.title));
            if (!item.title[0]) extract_a_text(slice, "thread-", item.title, sizeof(item.title));
            strip_img_marks(item.title);
            extract_a_text(slice, "space.php?uid=", item.author, sizeof(item.author));
            if (!item.author[0]) extract_a_text(slice, "space-uid-", item.author, sizeof(item.author));
            const char *author_cell = strstr(slice, "class=\"author\"");
            if (author_cell) tag_content(author_cell, "em", item.dateline,
                                         sizeof(item.dateline), 0);
            const char *nums_cell = strstr(slice, "class=\"nums\"");
            if (nums_cell) {
                int r = tag_content(nums_cell, "strong", NULL, 0, 1);
                int v = tag_content(nums_cell, "em", NULL, 0, 1);
                if (r >= 0) item.replies = r;
                if (v >= 0) item.views = v;
            }
            const char *last_cell = strstr(slice, "class=\"lastpost\"");
            if (last_cell) {
                char lastpost[128] = {0};
                if (tag_content(last_cell, "em", lastpost, sizeof(lastpost), 0) == 0)
                    str_copy(item.lastpost, sizeof(item.lastpost), lastpost);
            }
            item.has_attach = strstr(slice, "image_s.gif") || strstr(slice, "class=\"attach\"");
            char *nl = strpbrk(item.dateline, "\r\n");
            if (nl) *nl = 0;
            topic_list_push(out, &item);
        }
        free(slice);
        p = hit + (sticky ? strlen(M_STICK) : strlen(M_NORMAL));
    }
    return out->n;
}

/* Favourites (`my.php?item=favorites&type=thread`) is a plain table of links.
 * Unlike a forum list it has no per-row id markers, so scan every thread link
 * and de-duplicate by tid. This is deliberately tolerant of both
 * `viewthread.php?tid=123` and `thread-123-1-1.html` style URLs. */
int discuz_parse_favorites(const char *html, TopicList *out) {
    if (!html || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->page = 1;
    out->last_page = discuz_last_page(html);

    const char *p = html;
    static const char *M_VIEW = "viewthread.php?tid=";
    static const char *M_REST = "thread-";
    while (*p) {
        /* nearest of the two link styles */
        const char *a = strstr(p, M_VIEW);
        const char *b = strstr(p, M_REST);
        const char *hit = a && (!b || a < b) ? a : b;
        if (!hit) break;

        /* parse tid from the link; url_param scans just this href fragment */
        int tid = 0;
        if (hit == a) {
            char tbuf[32] = {0};
            /* url_param returns 0 on success (not 1!) */
            if (url_param(hit, "tid", tbuf, sizeof(tbuf)) == 0)
                tid = parse_int(tbuf, 0);
        } else {
            tid = parse_int(hit + strlen(M_REST), 0);
        }

        /* end of this <a> element: always advance past it so the same link
         * can never be examined twice */
        const char *end = strstr(hit, "</a>");
        const char *next = end ? end + 4 : hit + strlen(hit == a ? M_VIEW : M_REST);

        if (tid > 0) {
            int dup = 0;
            for (int i = 0; i < out->n; i++)
                if (out->v[i].tid == tid) { dup = 1; break; }
            if (!dup) {
                Topic item;
                memset(&item, 0, sizeof(item));
                item.tid = tid;
                extract_a_text(hit, hit == a ? M_VIEW : M_REST,
                               item.title, sizeof(item.title));
                topic_list_push(out, &item);
            }
        }
        p = next;
        if (p <= html) break;
    }
    return out->n;
}

/* Remove \x02...\x02 image-URL spans that html_to_text leaves inside anchor
 * text (title links often wrap a folder icon together with the title). */
static void strip_img_marks(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if ((unsigned char)*r == 0x02) {
            r++;
            while (*r && (unsigned char)*r != 0x02) r++;
            if (*r) r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

/* PM / notice lists: walk every <a href="...">anchor</a> whose href points
 * at a concrete message (pm.php...pmid=N / notice.php...id=N), keep the
 * anchor text, and record the relative URL. Index (1-based) lands in tid so
 * the app can map a tapped row back to its URL. */
int discuz_parse_msglist(const char *html, TopicList *out, Str *urls) {
    if (!html || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->page = 1;
    str_clear(urls);

    const char *p = html;
    int idx = 0;
    while ((p = strstr(p, "href=\"")) != NULL) {
        const char *e = strchr(p + 6, '"');
        if (!e) break;
        size_t ulen = (size_t)(e - (p + 6));
        if (ulen >= 512) { p = e; continue; }
        char href[512];
        memcpy(href, p + 6, ulen);
        href[ulen] = 0;

        int is_msg = (strstr(href, "pm.php") && strstr(href, "pmid=")) ||
                     (strstr(href, "notice.php") && strstr(href, "id="));
        if (!is_msg) { p = e; continue; }

        const char *ae = strstr(e + 1, "</a>");
        if (!ae) break;
        Str text; str_init(&text);
        html_to_text(e + 1, (size_t)(ae - (e + 1)), &text);
        char *t = str_trim(text.data);
        if (t[0]) {
            Topic item;
            memset(&item, 0, sizeof(item));
            item.tid = ++idx;
            str_copy(item.title, sizeof(item.title), t);
            topic_list_push(out, &item);
            str_append(urls, href);
            str_append(urls, "\n");
        }
        str_free(&text);
        p = ae + 4;
    }
    return out->n;
}

static int extract_floor(const char *block) {
    const char *p = block;
    while ((p = strstr(p, "<em")) != NULL) {
        const char *gt = strchr(p, '>');
        if (!gt) break;
        const char *e = strstr(gt + 1, "</em>");
        if (!e || (size_t)(e - (gt + 1)) > 12) { p = gt + 1; continue; }
        char buf[24];
        size_t n = (size_t)(e - (gt + 1));
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, gt + 1, n); buf[n] = 0;
        char *t = str_trim(buf);
        if (*t == '#') t++;
        if (isdigit((unsigned char)*t)) {
            int v = parse_int(t, 0);
            if (v > 0 && v < 100000) return v;
        }
        p = e + 4;
    }
    return 0;
}

static void thread_push(ThreadPage *t, const Post *p) {
    if (t->nposts == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 16;
        t->posts = xrealloc(t->posts, (size_t)t->cap * sizeof(Post));
    }
    t->posts[t->nposts++] = *p;
}

void thread_page_free(ThreadPage *t) {
    if (!t) return;
    for (int i = 0; i < t->nposts; i++) str_free(&t->posts[i].body);
    free(t->posts); t->posts = NULL; t->nposts = t->cap = 0;
}

static int parse_post_block(const char *block, Post *post, int ordinal) {
    memset(post, 0, sizeof(*post));
    str_init(&post->body);
    char pid[32] = {0};
    if (!url_param(block, "pid", pid, sizeof(pid)) && *pid)
        str_copy(post->pid, sizeof(post->pid), pid);
    post->floor = extract_floor(block);
    if (!post->floor) post->floor = ordinal;

    extract_a_text(block, "space.php?uid=", post->author, sizeof(post->author));
    if (!post->author[0]) extract_a_text(block, "space-uid-", post->author, sizeof(post->author));
    if (!post->author[0]) extract_element_text(block, "class=\"postauthor\"", "td",
                                               post->author, sizeof(post->author));
    char info[160] = {0};
    if (!extract_element_text(block, "class=\"authorinfo\"", "div", info, sizeof(info)))
        str_copy(post->dateline, sizeof(post->dateline), info);
    if (!post->dateline[0])
        extract_element_text(block, "class=\"postinfo\"", "div",
                             post->dateline, sizeof(post->dateline));
    char *nl = strpbrk(post->dateline, "\r\n");
    if (nl) *nl = 0;

    /* Body: Discuz 7.2 wraps post text in <td class="t_msgfont">;
     * HiPDA·NG uses div.defaultpost div.postmessage div.t_msgfontfix td.t_msgfont */
    char *body = html_element_slice(block, "class=\"t_msgfont\"", "td");
    if (!body) body = html_element_slice(block, "id=\"postmessage_", "div");
    if (!body) body = html_element_slice(block, "class=\"t_msgfontfix\"", "div");
    if (!body) return -1;
    html_to_text(body, strlen(body), &post->body);
    free(body);
    if (!post->body.len) return -1;
    return 0;
}

int discuz_parse_thread(const char *html, ThreadPage *out, int tid, int page) {
    if (!html || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->tid = tid;
    out->page = page;
    out->last_page = discuz_last_page(html);
    str_copy(out->title, sizeof(out->title), "");

    if (discuz_is_gate(html) || discuz_is_cloudflare(html)) return -1;

    char title[HIPDA_NAME_LEN] = {0};
    if (!extract_element_text(html, "id=\"threadtitle\"", "div", title, sizeof(title))) {
        char *h = strchr(title, '\n');
        if (h) *h = 0;
        strip_img_marks(title);
        str_copy(out->title, sizeof(out->title), str_trim(title));
    }
    if (!out->title[0]) {
        const char *t = strstr(html, "<title>");
        if (t) {
            const char *e = strstr(t + 7, "</title>");
            if (e && e > t + 7) {
                Str tmp; str_init(&tmp);
                html_to_text(t + 7, (size_t)(e - (t + 7)), &tmp);
                str_copy(out->title, sizeof(out->title), str_trim(tmp.data));
                str_free(&tmp);
            }
        }
    }

    /* Only look inside #postlist when present, to avoid sidebars/nav. */
    char *list = html_element_slice(html, "id=\"postlist\"", "div");
    const char *scope = list ? list : html;
    const char *p = scope;
    int ordinal = 0;
    /* Discuz 7.2 templates use <table id="pidNNN">; HiPDA's markup also has
     * <div id="post_NNN"> wrapping it. Pick ONE convention, never both, or
     * every floor would be parsed twice. */
    int use_post = strstr(scope, "id=\"post_") != NULL;
    for (;;) {
        const char *a = use_post ? strstr(p, "id=\"post_") : NULL;
        const char *b = use_post ? NULL : strstr(p, "id=\"pid");
        const char *hit = a ? a : b;
        if (!hit) break;
        const char *start = back_to_lt(scope, hit);
        const char *tag = use_post ? "div" : "table";
        char *block = html_element_slice(start,
                                         use_post ? "id=\"post_" : "id=\"pid", tag);
        if (block) {
            Post post;
            if (!parse_post_block(block, &post, ++ordinal)) {
                if (page > 1 && post.floor < 100) {
                    /* keep as-is; the UI shows the forum page number too */
                }
                thread_push(out, &post);
            } else {
                str_free(&post.body);
            }
            free(block);
        }
        p = hit + 5;
    }
    free(list);

    /* Loose fallback: forum template changed, but t_msgfont cells still exist. */
    if (out->nposts == 0) {
        const char *q = html;
        int n = 0;
        while ((q = strstr(q, "class=\"t_msgfont\"")) != NULL) {
            const char *start = back_to_lt(html, q);
            char *body = html_element_slice(start, "class=\"t_msgfont\"", "td");
            if (body) {
                Post post; memset(&post, 0, sizeof(post));
                str_init(&post.body);
                html_to_text(body, strlen(body), &post.body);
                str_copy(post.author, sizeof(post.author), "");
                post.floor = ++n;
                if (post.body.len) thread_push(out, &post);
                else str_free(&post.body);
                free(body);
            }
            q += 18;
        }
    }
    return out->nposts;
}
