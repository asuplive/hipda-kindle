/* Kindle network backend.
 *
 * We deliberately shell out to the system curl instead of linking libcurl:
 *   - every Kindle firmware already ships a curl/wget with working CA certs,
 *   - no OpenSSL/zlib cross-compilation for five different ABIs,
 *   - curl's Netscape cookie jar maps directly onto Discuz's cdb_auth cookie.
 * execvp() with an argv array (no shell) keeps usernames/passwords out of a
 * command line that a shell could mangle.
 */
#include "net.h"
#include "discuz.h"
#include "gbk.h"
#include "util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char g_tmp[HIPDA_PATH_LEN] = "/mnt/us/hipda/cache/resp.html";

/* Some Kindle firmwares ship a curl built without --max-time / --compressed
 * ("option --max-time=20: is unknown"). Probe once and remember what works,
 * otherwise every request fails before a single byte is fetched. */
static int g_tmo_style = -1;      /* -1 unknown, 0 none, 1 "--max-time=N", 2 "-m N" */
static int g_has_compressed = -1;
static const char *g_curl_cmd = "curl";

/* Exit code 2 from curl means "unknown option"; anything else (including a
 * connection failure) means the option was accepted. */
static int curl_accepts(const char *arg) {
    pid_t pid = fork();
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execlp(g_curl_cmd, g_curl_cmd, arg, "-s", "-o", "/dev/null",
               "http://127.0.0.1:1/", (char *)NULL);
        _exit(127);
    }
    if (pid < 0) return 0;
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return 0;
    return WIFEXITED(st) && WEXITSTATUS(st) != 2;
}

static void probe_curl(void) {
    if (g_tmo_style >= 0) return;
    if (curl_accepts("--max-time=2")) g_tmo_style = 1;
    else if (curl_accepts("-m"))      g_tmo_style = 2;
    else                              g_tmo_style = 0;
    g_has_compressed = curl_accepts("--compressed");
    log_msg("curl: timeout-style=%d compressed=%d", g_tmo_style, g_has_compressed);
}

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

int net_init(const Config *c) {
    dir_ensure(c->cache_dir);
    snprintf(g_tmp, sizeof(g_tmp), "%s/resp_%d.html", c->cache_dir, (int)getpid());
    g_curl_cmd = c->net_cmd[0] ? c->net_cmd : "curl";
    probe_curl();
    return 0;
}
void net_close(void) { unlink(g_tmp); }

static void cache_path(const Config *c, const char *url, char *out, size_t cap) {
    uint64_t h = fnv1a(url);
    snprintf(out, cap, "%s/%08X%08X.html", c->cache_dir,
             (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
}

/* Returns 0 on success. post may be NULL for a GET. */
static int curl_raw(const Config *c, const char *url, const char *post) {
    int tmo = c->timeout > 0 ? c->timeout : 20;
    char tmo_long[32];
    snprintf(tmo_long, sizeof(tmo_long), "--max-time=%d", tmo);
    char tmo_short[16];
    snprintf(tmo_short, sizeof(tmo_short), "%d", tmo);
    char *argv[40];
    int n = 0;
    argv[n++] = (char *)g_curl_cmd;
    argv[n++] = (char *)"-sS";
    argv[n++] = (char *)"-L";
    if (g_has_compressed == 1) argv[n++] = (char *)"--compressed";
    argv[n++] = (char *)"-k";          /* old Kindles have stale CA bundles */
    if (g_tmo_style == 1) {
        argv[n++] = tmo_long;
    } else if (g_tmo_style == 2) {
        argv[n++] = (char *)"-m";
        argv[n++] = tmo_short;
    }   /* style 0: this curl has no timeout option at all */
    argv[n++] = (char *)"-A"; argv[n++] = (char *)c->user_agent;
    argv[n++] = (char *)"-b"; argv[n++] = (char *)c->cookie_file;
    argv[n++] = (char *)"-c"; argv[n++] = (char *)c->cookie_file;
    argv[n++] = (char *)"-o"; argv[n++] = g_tmp;
    if (post) { argv[n++] = (char *)"-d"; argv[n++] = (char *)post; }
    argv[n++] = (char *)url;
    argv[n] = NULL;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execvp(argv[0], argv); _exit(127); }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

/* Download a binary file (forum image) straight to out_path, refusing files
 * larger than max_bytes (curl --max-filesize) so a huge attachment cannot
 * fill the disk. Cookies are sent so login-gated attachments work. */
int net_fetch_file(const Config *c, const char *url, const char *out_path,
                   size_t max_bytes) {
    int tmo = c->timeout > 0 ? c->timeout : 20;
    char tmo_long[32];
    snprintf(tmo_long, sizeof(tmo_long), "--max-time=%d", tmo * 2);
    char tmo_short[16];
    snprintf(tmo_short, sizeof(tmo_short), "%d", tmo * 2);
    char limit[32];
    snprintf(limit, sizeof(limit), "%llu", (unsigned long long)max_bytes);

    char *argv[40];
    int n = 0;
    argv[n++] = (char *)g_curl_cmd;
    argv[n++] = (char *)"-sS";
    argv[n++] = (char *)"-L";
    argv[n++] = (char *)"-k";
    if (g_tmo_style == 1)      argv[n++] = tmo_long;
    else if (g_tmo_style == 2) { argv[n++] = (char *)"-m"; argv[n++] = tmo_short; }
    argv[n++] = (char *)"--max-filesize";
    argv[n++] = limit;
    argv[n++] = (char *)"-A"; argv[n++] = (char *)c->user_agent;
    argv[n++] = (char *)"-b"; argv[n++] = (char *)c->cookie_file;
    argv[n++] = (char *)"-o"; argv[n++] = (char *)out_path;
    argv[n++] = (char *)url;
    argv[n] = NULL;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execvp(argv[0], argv); _exit(127); }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) return -1;
    /* curl exits 0 even for an HTML error page; a zero-byte file is failure */
    return file_exists(out_path) ? 0 : -1;
}

static int decode_raw(const Config *c, const char *url, Str *out_utf8) {
    Str raw; str_init(&raw);
    if (file_read(g_tmp, &raw) || !raw.len) { str_free(&raw); return NET_ERR; }
    char cache[HIPDA_PATH_LEN];
    cache_path(c, url, cache, sizeof(cache));
    file_write(cache, raw.data, raw.len);
    int utf8 = discuz_detect_utf8(raw.data, raw.len);
    text_decode(raw.data, raw.len, utf8, out_utf8);
    str_free(&raw);
    return out_utf8->len ? NET_OK : NET_EMPTY;
}

int net_fetch(const Config *c, const char *url, Str *out_utf8, int refresh) {
    str_clear(out_utf8);
    char cache[HIPDA_PATH_LEN];
    cache_path(c, url, cache, sizeof(cache));
    if (!refresh && file_exists(cache)) {
        Str raw; str_init(&raw);
        if (!file_read(cache, &raw) && raw.len) {
            int utf8 = discuz_detect_utf8(raw.data, raw.len);
            text_decode(raw.data, raw.len, utf8, out_utf8);
            str_free(&raw);
            if (!out_utf8->len) return NET_EMPTY;
            if (discuz_is_cloudflare(out_utf8->data)) return NET_CF;
            if (discuz_is_gate(out_utf8->data)) return NET_GATE;
            return NET_OK;
        } else str_free(&raw);
    }
    if (c->offline) return NET_OFFLINE;
    log_msg("net: GET %s", url);
    if (curl_raw(c, url, NULL)) return NET_ERR;
    int r = decode_raw(c, url, out_utf8);
    if (r != NET_OK) return r;
    if (discuz_is_cloudflare(out_utf8->data)) return NET_CF;
    if (discuz_is_gate(out_utf8->data)) return NET_GATE;
    return NET_OK;
}

int net_has_auth_cookie(const Config *c) {
    Str jar; str_init(&jar);
    int ok = 0;
    if (!file_read(c->cookie_file, &jar)) {
        /* Netscape cookie jar: a real cookie line has 7 tab-separated fields
         * and the auth cookie name (cdb_auth / <prefix>_auth).
         * curl stores HttpOnly cookies as "#HttpOnly_<domain>\t..." comment
         * lines -- real login sessions live there, so strip that prefix. */
        char *save = NULL;
        for (char *line = jar.data; line; line = save) {
            char *nl = strchr(line, '\n');
            if (nl) { *nl = 0; save = nl + 1; } else save = NULL;
            const char *q = line;
            if (!strncmp(q, "#HttpOnly_", 10)) q += 10;
            else if (*q == '#') continue;
            if (strchr(q, '\t') && strstr(q, "auth")) ok = 1;
        }
    }
    str_free(&jar);
    return ok;
}

int net_login(const Config *c) {
    if (!c->username[0] || !c->password_md5[0]) {
        log_msg("login: no credentials in config.ini (guest mode)");
        return NET_GATE;
    }
    if (net_has_auth_cookie(c)) {
        log_msg("login: reusing cdb_auth cookie");
        return NET_OK;
    }
    char url[HIPDA_URL_LEN], base[HIPDA_URL_LEN];
    net_url_login(url, sizeof(url), c);
    base_url(c, base, sizeof(base));
    log_msg("login: fetching formhash");
    if (curl_raw(c, url, NULL)) return NET_ERR;
    Str raw; str_init(&raw);
    if (file_read(g_tmp, &raw)) { str_free(&raw); return NET_ERR; }
    Str page; str_init(&page);
    text_decode(raw.data, raw.len, discuz_detect_utf8(raw.data, raw.len), &page);
    str_free(&raw);
    if (discuz_is_cloudflare(page.data)) { str_free(&page); return NET_CF; }
    char formhash[64] = {0}, sid[64] = {0};
    discuz_parse_login_form(page.data, formhash, sizeof(formhash), sid, sizeof(sid));
    str_free(&page);
    if (!formhash[0]) { log_msg("login: formhash not found"); return NET_GATE; }

    Str post, val;
    str_init(&post); str_init(&val);
    str_append(&post, "formhash="); url_encode_str(formhash, &val); str_append(&post, val.data); str_clear(&val);
    if (sid[0]) {
        str_append(&post, "&sid="); url_encode_str(sid, &val);
        str_append(&post, val.data); str_clear(&val);
    }
    str_append(&post, "&loginfield=username");
    str_append(&post, "&username=");
    url_encode_str(c->username, &val); str_append(&post, val.data); str_clear(&val);
    str_append(&post, "&password=");
    url_encode_str(c->password_md5, &val); str_append(&post, val.data); str_clear(&val);
    str_append(&post, "&questionid=0&answer=&cookietime=2592000");
    str_append(&post, "&referer=");
    char referer[HIPDA_URL_LEN];
    snprintf(referer, sizeof(referer), "%sindex.php", base);
    url_encode_str(referer, &val); str_append(&post, val.data); str_clear(&val);

    char login_url[HIPDA_URL_LEN];
    snprintf(login_url, sizeof(login_url),
             "%slogging.php?action=login&loginsubmit=yes&inajax=1", base);
    log_msg("login: POST %s as %s", login_url, c->username);
    int rc = curl_raw(c, login_url, post.data);
    str_free(&post); str_free(&val);
    if (rc) return NET_ERR;
    Str result; str_init(&result);
    if (file_read(g_tmp, &result)) { str_free(&result); return NET_ERR; }
    /* Verified against the live forum (2026-11-15): the login reply can say
     * "欢迎您回来" yet carry NO Set-Cookie at all (stale formhash/session).
     * Only a stored cdb_auth cookie proves the session will persist, so that
     * is the sole success criterion. */
    int ok = net_has_auth_cookie(c);
    if (!ok && discuz_login_ok(result.data))
        log_msg("login: server accepted the credentials but stored no "
                "cdb_auth cookie -- session will not persist, try again");
    str_free(&result);
    log_msg("login: %s", ok ? "ok (cdb_auth stored)" : "failed");
    return ok ? NET_OK : NET_GATE;
}
