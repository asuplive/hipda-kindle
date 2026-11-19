#include "app.h"

#include "config.h"
#include "discuz.h"
#include "font.h"
#include "gbk.h"
#include "html.h"
#include "layout.h"
#include "raster.h"
#include "render.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks, g_fails;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fails++; printf("  FAIL  %s  (line %d)\n", (msg), __LINE__); } \
} while (0)

static Str load_page(const char *path) {
    Str raw, out;
    str_init(&raw); str_init(&out);
    if (file_read(path, &raw)) {
        printf("  WARN  fixture missing: %s\n", path);
        str_free(&raw);
        return out;
    }
    text_decode(raw.data, raw.len, discuz_detect_utf8(raw.data, raw.len), &out);
    str_free(&raw);
    return out;
}

static void test_gbk(void) {
    printf("[gbk / utf8]\n");
    const char gbk[] = "\xD6\xD0\xCE\xC4\xC2\xDB\xCC\xB3";   /* 中文论坛 */
    Str out; str_init(&out);
    gbk_bytes_to_utf8(gbk, 8, &out);
    CHECK(strcmp(out.data, "中文论坛") == 0, "GBK -> UTF-8 中文论坛");
    CHECK(strlen(out.data) == 12, "4 CJK chars == 12 UTF-8 bytes");
    str_free(&out);

    CHECK(utf8_cells('A') == 1, "ASCII is 1 cell");
    CHECK(utf8_cells(0x4E2D) == 2, "CJK is 2 cells");
    CHECK(utf8_cells(0x0301) == 0, "combining mark is 0 cells");
    CHECK(utf8_str_cells("A中B") == 4, "mixed string width");

    Str inv; str_init(&inv);
    const char bad[] = {(char)0xD6, (char)0x20, 'x', 0};
    text_decode(bad, 3, 0, &inv);
    CHECK(strstr(inv.data, "x") != NULL, "invalid GBK byte degrades gracefully");
    str_free(&inv);

    const char *m1 = "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=gbk\" />";
    const char *m2 = "<meta charset=\"utf-8\"><html>";
    CHECK(discuz_detect_utf8(m1, strlen(m1)) == 0, "detect GBK meta");
    CHECK(discuz_detect_utf8(m2, strlen(m2)) == 1, "detect UTF-8 meta");
}

static void test_html(void) {
    printf("[html -> text]\n");
    const char *frag =
        "<p>第一行 &amp; 第二行<br><b>粗体</b></p>"
        "<div class=\"quote\">引用内容</div>"
        "<img src=\"images/smilies/default_smile.gif\">"
        "<img src=\"images/smilies/default_biggrin.gif\">"
        "<img src=\"attachments/photo.jpg\" file=\"attachment.php?aid=1\">"
        "<script>alert('BAD')</script>尾部&nbsp;文字";
    Str out; str_init(&out);
    html_to_text(frag, strlen(frag), &out);
    CHECK(strstr(out.data, "第一行 & 第二行") != NULL, "entity decode");
    CHECK(strstr(out.data, "粗体") != NULL, "inline tags stripped");
    CHECK(strstr(out.data, "│ 引用内容") != NULL, "quote prefix");
    CHECK(strstr(out.data, ":)") != NULL, "smiley -> :)");
    CHECK(strstr(out.data, ":D") != NULL, "smiley -> :D");
    CHECK(strstr(out.data, "") != NULL, "attachment URL placeholder (-wrapped)");
    CHECK(strstr(out.data, "alert") == NULL, "script content dropped");
    CHECK(strstr(out.data, "尾部 文字") != NULL, "nbsp -> space");
    str_free(&out);

    char title[128] = {0};
    const char *tfrag = "<div id=\"threadtitle\"><h1>标题 &amp; 测试</h1></div>";
    CHECK(html_first_text(tfrag, strlen(tfrag),
                          "id=\"threadtitle\"", title, sizeof(title)) == 0,
          "html_first_text returns 0");
    CHECK(strcmp(title, "标题 & 测试") == 0, "html_first_text value");
}

static void test_real_responses(void) {
    printf("[real captured responses]\n");
    Str gate = load_page("fixtures/gate_real.html");
    if (gate.len) {
        TopicList empty;
        CHECK(discuz_is_gate(gate.data) == 1, "real forum gate detected");
        CHECK(discuz_parse_topic_list(gate.data, &empty, 2, 1) == 0,
              "gate page yields no topics");
        topic_list_free(&empty);
    }
    Str cf = load_page("fixtures/cloudflare_real.html");
    if (cf.len) CHECK(discuz_is_cloudflare(cf.data) == 1, "real Cloudflare page detected");
    Str login = load_page("fixtures/login.html");
    if (login.len) {
        char hash[64] = {0}, sid[64] = {0};
        CHECK(discuz_parse_login_form(login.data, hash, sizeof(hash), sid, sizeof(sid)) == 0,
              "real login page has formhash");
        CHECK(strlen(hash) >= 6, "formhash length");
    }
    str_free(&gate); str_free(&cf); str_free(&login);
}

static void test_discuz(void) {
    printf("[discuz parser]\n");
    Str f = load_page("fixtures/synthetic_forum.html");
    if (f.len) {
        TopicList list;
        int n = discuz_parse_topic_list(f.data, &list, 2, 1);
        CHECK(n >= 20, "topic list has >= 20 rows");
        CHECK(list.last_page >= 5, "topic list last page >= 5");
        CHECK(n > 0 && list.v[0].tid > 0, "first topic has a tid");
        CHECK(n > 0 && list.v[0].title[0] != 0, "first topic has a title");
        CHECK(n > 0 && list.v[0].replies > 0, "first topic has reply count");
        CHECK(n > 0 && list.v[0].is_sticky == 1, "first topic is sticky");
        CHECK(n > 0 && list.v[0].replies == 3 && list.v[0].views == 120,
              "reply/view numbers parsed separately");
        topic_list_free(&list);
    }
    /* favourites: three distinct links, one duplicate tid -> 3 entries */
    {
        static const char fav_html[] =
            "<table><tr><td><a href=\"viewthread.php?tid=1111&extra=page%3D1\">"
            "收藏帖子 A</a></td></tr>"
            "<tr><td><a href=\"viewthread.php?tid=2222\">收藏帖子 B</a></td></tr>"
            "<tr><td><a href=\"viewthread.php?tid=1111\">收藏帖子 A 重复</a></td></tr>"
            "<tr><td><a href=\"thread-3333-1-1.html\">静态链接帖子 C</a></td></tr>"
            "</table>";
        TopicList fav;
        int fn = discuz_parse_favorites(fav_html, &fav);
        CHECK(fn == 3, "favorites: 3 distinct tids (duplicate removed)");
        CHECK(fn > 0 && fav.v[0].tid == 1111, "favorites: first tid parsed");
        CHECK(fn > 0 && fav.v[0].title[0] != 0, "favorites: title extracted");
        int has3333 = 0;
        for (int i = 0; i < fn; i++) if (fav.v[i].tid == 3333) has3333 = 1;
        CHECK(has3333 == 1, "favorites: thread-N style link parsed");
        topic_list_free(&fav);
    }
    Str t = load_page("fixtures/synthetic_thread.html");
    if (t.len) {
        ThreadPage thread;
        int n = discuz_parse_thread(t.data, &thread, 1579403, 1);
        CHECK(n >= 4, "thread has >= 4 posts");
        CHECK(strstr(thread.title, "Kindle") != NULL, "thread title parsed");
        CHECK(thread.last_page >= 3, "thread last page >= 3");
        CHECK(n > 0 && strstr(thread.posts[0].body.data, "整页翻页") != NULL,
              "post body keeps Chinese text");
        CHECK(n > 0 && strstr(thread.posts[0].body.data, ":)") != NULL,
              "post body contains smiley code");
        CHECK(n > 0 && strstr(thread.posts[0].body.data, "") != NULL,
              "post body contains image placeholder");
        CHECK(n > 0 && thread.posts[0].floor == 1, "floor number parsed");
        CHECK(n > 0 && strstr(thread.posts[0].author, "墨水瓶") != NULL,
              "author parsed");
        CHECK(n == 4, "post_ and pid markers are not double-counted");
        thread_page_free(&thread);
    }
    str_free(&f); str_free(&t);

    /* REAL logged-in pages, captured 2026-11-15 with the user's own cookie
     * jar. These assertions only run when the capture files exist, so the
     * regression suite still works without network access. */
    printf("[real logged-in pages]\n");
    Str lf = load_page("fixtures/live_forum.html");
    if (lf.len) {
        TopicList list;
        int n = discuz_parse_topic_list(lf.data, &list, 2, 1);
        CHECK(n >= 50, "live forum parses >= 50 topics");
        CHECK(n > 0 && list.v[0].title[0] != 0, "live topic title not empty (icon link skipped)");
        CHECK(list.last_page >= 100, "live forum pager page count >= 100");
        CHECK(n > 0 && list.v[0].author[0] != 0, "live topic author parsed");
        topic_list_free(&list);
    } else {
        printf("  SKIP  fixtures/live_forum.html not captured\n");
    }
    Str lt = load_page("fixtures/live_thread.html");
    if (lt.len) {
        ThreadPage thread;
        int n = discuz_parse_thread(lt.data, &thread, 1579403, 1);
        CHECK(n >= 40, "live thread parses >= 40 floors");
        CHECK(thread.title[0] != 0, "live thread title parsed");
        CHECK(thread.last_page >= 50, "live thread pager page count >= 50");
        int nonempty = 0;
        for (int i = 0; i < thread.nposts; i++)
            if (thread.posts[i].body.len > 40) nonempty++;
        /* This particular thread is full of "sofa"-style one-line replies;
         * a third carrying real text is already a meaningful parse signal. */
        printf("  live thread: %d floors, %d with >40-byte bodies\n", n, nonempty);
        CHECK(nonempty >= n / 3, "a third of live floors have real text");
        thread_page_free(&thread);
    } else {
        printf("  SKIP  fixtures/live_thread.html not captured\n");
    }
    str_free(&lf); str_free(&lt);
}

static void test_layout(void) {
    printf("[pagination / layout]\n");
    /* New SVG-proportional geometry: scale is a stroke unit (~w/600),
     * body text = 16 * w/600, cols = content width / (body/2). */
    Geometry g;
    geometry_compute(&g, 600, 800, 167, 0);
    CHECK(g.scale == 1, "600x800 -> stroke unit 1");
    CHECK(g.cell_h == 16, "600x800 -> body 16px (SVG fs16)");
    CHECK(g.cols >= 40 && g.cols <= 90, "sane line width");
    CHECK(g.rows >= 8, "sane page height");
    Geometry hd;
    geometry_compute(&hd, 1072, 1448, 300, 0);
    CHECK(hd.scale >= 2, "1072px -> stroke unit >= 2");
    CHECK(hd.cell_h > g.cell_h, "larger screen -> bigger body text");
    CHECK(hd.cols > g.cols, "larger screen -> more cells per line");
    Geometry o3;
    geometry_compute(&o3, 1264, 1680, 300, 0);
    CHECK(o3.cell_h == 33 || o3.cell_h == 34, "Oasis3 body ~= SVG fs16 * 2.1");
    CHECK(o3.top_h == 172 && o3.foot_y == 1516, "Oasis3 toolbar/footer rules");

    ThreadPage thread;
    memset(&thread, 0, sizeof(thread));
    thread.page = 1; thread.last_page = 3; thread.tid = 1;
    str_copy(thread.title, sizeof(thread.title), "测试主题");
    thread.cap = 3;
    thread.posts = xcalloc(3, sizeof(Post));
    for (int i = 0; i < 3; i++) {
        thread.posts[i].floor = i + 1;
        str_copy(thread.posts[i].author, sizeof(thread.posts[i].author), "作者");
        str_init(&thread.posts[i].body);
        for (int k = 0; k < 12; k++)
            str_append(&thread.posts[i].body,
                       "这是一段用于测试整页翻页的中文正文，包含标点，以及 ASCII words.\n");
    }
    thread.nposts = 3;
    Book b; book_init(&b, &g);
    book_from_thread(&b, &g, &thread, 0, NULL, 0);
    CHECK(b.n > g.rows, "long thread spans multiple lines");
    CHECK(b.np >= 2, "long thread spans multiple pages");
    int expected = (b.n + g.rows - 1) / g.rows;
    CHECK(b.np == expected, "page count == ceil(lines / rows)");
    CHECK(b.pages[b.np - 1].start + b.pages[b.np - 1].count == b.n,
          "last page ends at last line");
    book_free(&b);
    thread_page_free(&thread);
}

/* Feed truncated / hostile / random bytes through every parser entry point.
 * A crash, hang or sanitizer report here is a real bug: forum HTML is not
 * trustworthy, and the Kindle has no supervisor to restart us. */
static void test_malformed(void) {
    printf("[malformed / fuzz input]\n");
    static const char *cases[] = {
        "", "<", "<<<>>>", "<div", "<div class=\"quote\">", "<img src=",
        "&amp", "&#x", "&#99999999999;", "<script>x</script>",
        "<td class=\"t_msgfont\">", "id=\"postlist\"", "id=\"post_",
        "<tbody id=\"normalthread_", "<a href=\"viewthread.php?tid=",
        "\x81\x40", "\xD6\xD0", "\xFF\xFF\xFF\xFF",
        "<meta charset=gbk><div id=threadtitle><h1>x",
        "<div class=\"pages\"><a href=\"?page=999999\">9</a></div>",
    };
    int done = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t n = strlen(cases[i]);
        Str o; str_init(&o);
        html_to_text(cases[i], n, &o); str_free(&o);
        Str u; str_init(&u);
        gbk_bytes_to_utf8(cases[i], n, &u); str_free(&u);
        TopicList tl;
        discuz_parse_topic_list(cases[i], &tl, 2, 1);
        topic_list_free(&tl);
        ThreadPage tp;
        discuz_parse_thread(cases[i], &tp, 1, 1);
        thread_page_free(&tp);
        done++;
    }
    CHECK(done == (int)(sizeof(cases) / sizeof(cases[0])), "all fixed malformed cases survived");

    srand(20261115u);
    Geometry g;
    geometry_compute(&g, 600, 800, 167, 0);
    Book b; book_init(&b, &g);
    for (int i = 0; i < 256; i++) {
        char buf[513];
        int n = 1 + (rand() % 512);
        for (int j = 0; j < n; j++) {
            /* Bias towards markup characters to hit parser branches. */
            static const char alphabet[] = "<>\"'=/&;# \t\nabc0123idclasspost_\x81\xD6\xFF";
            size_t k = (size_t)rand() % (sizeof(alphabet) - 1);
            buf[j] = alphabet[k];
        }
        buf[n] = 0;
        Str o; str_init(&o);
        html_to_text(buf, (size_t)n, &o);
        Str u; str_init(&u);
        gbk_bytes_to_utf8(buf, (size_t)n, &u);
        book_wrap_text(&b, &g, L_BODY, u.data, 0, 0, 1);
        str_free(&o); str_free(&u);
        TopicList tl;
        discuz_parse_topic_list(buf, &tl, 2, 1);
        topic_list_free(&tl);
        ThreadPage tp;
        discuz_parse_thread(buf, &tp, 1, 1);
        thread_page_free(&tp);
        if (b.n > 4000) book_reset(&b);
    }
    book_finalize(&b, &g);
    CHECK(b.np >= 1, "fuzz layout still produces pages");
    book_free(&b);
    CHECK(1, "256 random inputs survived without crashing");
}

static void test_font_ttf_detect(void) {
    printf("[font / standard TTF detection]\n");
    const uint8_t truetype[] = { 0x00,0x01,0x00,0x00, 0,1,2,3,4,5,6,7 };
    const uint8_t opentype[] = { 'O','T','T','O', 0,1,2,3,4,5,6,7 };
    const uint8_t ttc[]      = { 't','t','c','f', 0,1,2,3,4,5,6,7 };
    const uint8_t huf[]      = { 'H','U','F','1', 1,16,16,2,36,0,1,0 };
    CHECK(font_is_ttf(truetype, sizeof(truetype)) == 1, "TrueType header detected");
    CHECK(font_is_ttf(opentype, sizeof(opentype)) == 1, "OpenType header detected");
    CHECK(font_is_ttf(ttc, sizeof(ttc)) == 1, "TTC collection detected");
    CHECK(font_is_ttf(huf, sizeof(huf)) == 0, "HUF1 not mistaken for TTF");
    CHECK(font_is_ttf(truetype, 2) == 0, "short buffer rejected");
}

static void test_font_raster(void) {
    printf("[font / raster]\n");
    Font f; memset(&f, 0, sizeof(f));
    if (font_load(&f, "fonts/unifont.bin") == 0) {
        CHECK(font_glyph(&f, 'A') != NULL, "ASCII glyph present");
        CHECK(font_glyph(&f, 0x4E2D) != NULL, "CJK glyph present");
        Canvas c;
        CHECK(canvas_init(&c, 320, 240) == 0, "canvas init");
        font_draw_text(&f, &c, 8, 8, "HiPDA 中文", 16, 0, 1);
        int dark = 0;
        for (size_t i = 0; i < (size_t)c.w * c.h; i++) if (c.px[i] < 100) dark++;
        CHECK(dark > 200, "glyphs actually painted pixels");
        /* Use a unique name: a stale locked selftest.pgm once made this
         * assertion fail for environmental reasons on Windows. */
        char pgm[HIPDA_PATH_LEN];
        const char *env = getenv("HIPDA_PGM_OUT");
        if (env && *env) str_copy(pgm, sizeof(pgm), env);
        else snprintf(pgm, sizeof(pgm), ".cache/selftest_%lld.pgm",
                      (long long)time(NULL) * 1000LL + (long long)(clock() & 1023));
        CHECK(canvas_write_pgm(&c, pgm) == 0, "PGM export");
        CHECK(file_exists(pgm) == 1, "PGM file actually written");
        canvas_free(&c);
        font_free(&f);
    } else {
        printf("  SKIP  fonts/unifont.bin not built (run tools/gen_font.py)\n");
    }
}

int app_selftest(void) {
    printf("hipda-kindle selftest (%s)\n", HIPDA_VERSION);
    dir_ensure(".cache");
    test_gbk();
    test_html();
    test_real_responses();
    test_discuz();
    test_layout();
    test_malformed();
    test_font_ttf_detect();
    test_font_raster();
    printf("-------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
