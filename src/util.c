#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
# include <direct.h>
# define MKDIR(p) _mkdir(p)
#else
# define MKDIR(p) mkdir(p, 0775)
#endif

static FILE *g_log = NULL;

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory (%zu bytes)\n", n); exit(1); }
    return p;
}
void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}
void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "out of memory\n"); exit(1); }
    return q;
}
char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

void str_init(Str *s) { s->data = xmalloc(64); s->data[0] = 0; s->len = 0; s->cap = 64; }
void str_clear(Str *s) { s->len = 0; if (s->data) s->data[0] = 0; }
void str_free(Str *s) { free(s->data); s->data = NULL; s->len = s->cap = 0; }

static void str_reserve(Str *s, size_t extra) {
    if (s->len + extra + 1 <= s->cap) return;
    size_t want = s->cap ? s->cap : 64;
    while (want < s->len + extra + 1) want *= 2;
    s->data = xrealloc(s->data, want);
    s->cap = want;
}
void str_append_n(Str *s, const char *p, size_t n) {
    if (!p || !n) return;
    str_reserve(s, n);
    memcpy(s->data + s->len, p, n);
    s->len += n;
    s->data[s->len] = 0;
}
void str_append_bytes(Str *s, const char *p, size_t n) { str_append_n(s, p, n); }
void str_append(Str *s, const char *p) { if (p) str_append_n(s, p, strlen(p)); }
void str_appendf(Str *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(cp); return; }
    str_reserve(s, (size_t)n);
    vsnprintf(s->data + s->len, s->cap - s->len, fmt, cp);
    va_end(cp);
    s->len += (size_t)n;
    s->data[s->len] = 0;
}
void str_shrink(Str *s, size_t n) {
    if (n >= s->len) return;
    s->len = n; s->data[n] = 0;
}

int file_read(const char *path, Str *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) str_append_n(out, buf, n);
    int bad = ferror(f);
    fclose(f);
    return bad ? -1 : 0;
}
int file_write(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, len, f);
    int bad = (n != len) || ferror(f);
    if (fclose(f)) bad = 1;
    return bad ? -1 : 0;
}
int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}
int dir_ensure(const char *path) {
    char tmp[HIPDA_PATH_LEN];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return -1;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = 0;
            if (tmp[1] != 0 && !file_exists(tmp)) MKDIR(tmp);
            tmp[i] = c;
        }
    }
    /* file_exists() works for directories on most libcs too; ignore EEXIST */
    if (!file_exists(tmp)) MKDIR(tmp);
    return 0;
}

static const char HEX[] = "0123456789ABCDEF";
void url_encode(const char *s, size_t len, Str *out) {
    static const unsigned char safe[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1, 1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0
    };
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ') { str_append_n(out, "+", 1); continue; }
        if (c < 128 && safe[c]) { str_append_n(out, (const char *)&c, 1); continue; }
        char esc[3] = { '%', HEX[c >> 4], HEX[c & 15] };
        str_append_n(out, esc, 3);
    }
}
void url_encode_str(const char *s, Str *out) { url_encode(s, strlen(s), out); }

static void append_utf8_cp(Str *out, uint32_t cp);

void html_unescape_n(const char *s, size_t len, Str *out) {
    size_t i = 0;
    while (i < len) {
        if (s[i] != '&') { str_append_n(out, s + i, 1); i++; continue; }
        size_t j = i + 1;
        while (j < len && j - i < 12 && s[j] != ';' && s[j] != '&' &&
               (isalnum((unsigned char)s[j]) || s[j] == '#' || s[j] == 'x')) j++;
        if (j >= len || s[j] != ';') { str_append_n(out, s + i, 1); i++; continue; }
        size_t n = j - i - 1;
        const char *name = s + i + 1;
        if (n == 2 && name[0] == 'l' && name[1] == 't') str_append_n(out, "<", 1);
        else if (n == 2 && name[0] == 'g' && name[1] == 't') str_append_n(out, ">", 1);
        else if (n == 3 && !strncmp(name, "amp", 3)) str_append_n(out, "&", 1);
        else if (n == 4 && !strncmp(name, "quot", 4)) str_append_n(out, "\"", 1);
        else if (n == 4 && !strncmp(name, "apos", 4)) str_append_n(out, "'", 1);
        else if (n == 4 && str_ncaseeq(name, "nbsp", 4)) str_append_n(out, " ", 1);
        else if (n > 1 && name[0] == '#') {
            uint32_t cp = 0;
            if (name[1] == 'x' || name[1] == 'X') {
                for (size_t k = 2; k < n; k++) {
                    char c = name[k]; uint32_t d;
                    if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
                    else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
                    else { cp = 0; break; }
                    cp = cp * 16 + d;
                }
            } else {
                for (size_t k = 1; k < n; k++) {
                    if (!isdigit((unsigned char)name[k])) { cp = 0; break; }
                    cp = cp * 10 + (uint32_t)(name[k] - '0');
                }
            }
            if (cp == 160) str_append_n(out, " ", 1);
            else if (cp && cp < 0x110000) append_utf8_cp(out, cp);
            else str_append_n(out, "?", 1);
        } else {
            str_append_n(out, s + i, j - i + 1);
        }
        i = j + 1;
    }
}

/* forward: implemented in gbk.c (keeps UTF-8 writing in one place) */
extern void utf8_append_cp(Str *s, uint32_t cp);
static void append_utf8_cp(Str *out, uint32_t cp) { utf8_append_cp(out, cp); }

char *str_trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}
void str_copy(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}
int str_caseeq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}
int str_ncaseeq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}
int parse_int(const char *s, int fallback) {
    if (!s || !*s) return fallback;
    char *e; long v = strtol(s, &e, 10);
    return (e == s) ? fallback : (int)v;
}
int url_param(const char *url, const char *name, char *out, size_t cap) {
    if (!url || !name || !cap) return -1;
    size_t nl = strlen(name);
    const char *p = url;
    while ((p = strstr(p, name)) != NULL) {
        if (p != url && p[-1] != '?' && p[-1] != '&' && p[-1] != ';' && p[-1] != '=') {
            p += nl; continue;
        }
        const char *q = p + nl;
        if (*q != '=') { p += nl; continue; }
        q++;
        const char *e = q;
        while (*e && *e != '&' && *e != '#' && *e != '"' && *e != '\'') e++;
        size_t n = (size_t)(e - q);
        if (n >= cap) n = cap - 1;
        memcpy(out, q, n); out[n] = 0;
        return 0;
    }
    return -1;
}
void log_set_file(const char *path) {
    if (g_log) fclose(g_log);
    g_log = path ? fopen(path, "a") : NULL;
}
void log_close(void) { if (g_log) { fclose(g_log); g_log = NULL; } }
void log_msg(const char *fmt, ...) {
    FILE *f = g_log ? g_log : stderr;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
    va_end(ap);
}
