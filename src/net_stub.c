/* PC/offline network backend. Serves the checked-in fixtures so the parser,
 * pagination and raster renderer can be exercised and previewed without a
 * Kindle, without a forum account, and without network access. */
#include "net.h"
#include "discuz.h"
#include "gbk.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int net_init(const Config *c) { (void)c; return 0; }
void net_close(void) {}

static void base_url(const Config *c, char *out, size_t cap) {
    size_t n = strlen(c->forum_server);
    while (n > 0 && c->forum_server[n - 1] == '/') n--;
    snprintf(out, cap, "%.*s%s", (int)n, c->forum_server, HIPDA_FORUM_PATH);
}
void net_url_forum(char *out, size_t cap, const Config *c, int fid, int page) {
    char base[HIPDA_URL_LEN]; base_url(c, base, sizeof(base));
    snprintf(out, cap, "%sforumdisplay.php?fid=%d&page=%d", base, fid, page);
}
void net_url_thread(char *out, size_t cap, const Config *c, int tid, int page) {
    char base[HIPDA_URL_LEN]; base_url(c, base, sizeof(base));
    snprintf(out, cap, "%sviewthread.php?tid=%d&page=%d", base, tid, page);
}
void net_url_login(char *out, size_t cap, const Config *c) {
    char base[HIPDA_URL_LEN]; base_url(c, base, sizeof(base));
    snprintf(out, cap, "%slogging.php?action=login", base);
}
int net_fetch_file(const Config *c, const char *url, const char *out_path,
                   size_t max_bytes) {
    (void)c; (void)url; (void)out_path; (void)max_bytes;
    return -1;   /* PC preview: images stay placeholders */
}

int net_has_auth_cookie(const Config *c) { (void)c; return 1; }
int net_login(const Config *c) { (void)c; return NET_OK; }

static const char *fixture_path(const char *url, char *buf, size_t cap) {
    const char *fixed = getenv("HIPDA_FIXTURE");
    if (fixed && *fixed) { str_copy(buf, cap, fixed); return buf; }
    const char *dir = getenv("HIPDA_FIXTURES");
    if (!dir || !*dir) dir = "fixtures";
    int page = 1;
    char tmp[32];
    if (!url_param(url, "page", tmp, sizeof(tmp))) page = parse_int(tmp, 1);
    /* HIPDA_LIVE=1 exercises the parser against REAL logged-in pages
     * captured with the user's own cookie jar (fixtures/live_*.html). */
    const char *live = getenv("HIPDA_LIVE");
    const char *name;
    if (live && *live) {
        if (strstr(url, "viewthread.php")) name = page > 1 ? "live_thread_p2.html" : "live_thread.html";
        else name = page > 1 ? "live_forum_p2.html" : "live_forum.html";
    } else if (strstr(url, "viewthread.php")) {
        name = page > 1 ? "synthetic_thread_p2.html" : "synthetic_thread.html";
    } else if (strstr(url, "forumdisplay.php")) {
        name = page > 1 ? "synthetic_forum_p2.html" : "synthetic_forum.html";
    } else if (strstr(url, "logging.php")) {
        name = "login.html";
    } else {
        name = "synthetic_forum.html";
    }
    snprintf(buf, cap, "%s/%s", dir, name);
    return buf;
}

int net_fetch(const Config *c, const char *url, Str *out_utf8, int refresh) {
    (void)c; (void)refresh;
    str_clear(out_utf8);
    char path[HIPDA_PATH_LEN];
    fixture_path(url, path, sizeof(path));
    Str raw; str_init(&raw);
    if (file_read(path, &raw)) {
        log_msg("fixture: %s not found", path);
        str_free(&raw);
        return NET_ERR;
    }
    log_msg("fixture: %s (%zu bytes)", path, raw.len);
    text_decode(raw.data, raw.len, discuz_detect_utf8(raw.data, raw.len), out_utf8);
    str_free(&raw);
    if (discuz_is_cloudflare(out_utf8->data)) return NET_CF;
    if (discuz_is_gate(out_utf8->data)) return NET_GATE;
    return out_utf8->len ? NET_OK : NET_EMPTY;
}
