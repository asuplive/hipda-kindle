#include "gbk.h"
#include "util.h"
#include "gbk_table.h"

#include <string.h>

void utf8_append_cp(Str *s, uint32_t cp) {
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F)); n = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    str_append_n(s, b, (size_t)n);
}

uint32_t utf8_next(const char **p, const char *end) {
    const unsigned char *s = (const unsigned char *)*p;
    if (s >= (const unsigned char *)end) return 0;
    unsigned char c = *s;
    if (c < 0x80) { *p += 1; return c; }
    int need; uint32_t cp; uint32_t min;
    if ((c & 0xE0) == 0xC0)      { need = 1; cp = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; min = 0x10000; }
    else { *p += 1; return 0xFFFD; }
    if ((size_t)(end - *p) < (size_t)need + 1) { *p += 1; return 0xFFFD; }
    for (int i = 1; i <= need; i++) {
        unsigned char d = s[i];
        if ((d & 0xC0) != 0x80) { *p += 1; return 0xFFFD; }
        cp = (cp << 6) | (d & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *p += 1; return 0xFFFD;
    }
    *p += need + 1;
    return cp;
}

int utf8_cells(uint32_t cp) {
    if (cp == 0 || cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    /* combining marks */
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F) || cp == 0x200B || cp == 0x200C ||
        cp == 0x200D || cp == 0xFEFF) return 0;
    /* East Asian Wide / Fullwidth, plus emoji */
    if ((cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3040 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD)) return 2;
    return 1;
}

int utf8_str_cells(const char *s) {
    const char *p = s, *end = s + strlen(s);
    int n = 0;
    while (p < end) n += utf8_cells(utf8_next(&p, end));
    return n;
}

int utf8_is_valid(const char *s, size_t len) {
    const char *p = s, *end = s + len;
    while (p < end) {
        const char *q = p;
        if (utf8_next(&p, end) == 0xFFFD) return 0;
        (void)q;
    }
    return 1;
}

void gbk_bytes_to_utf8(const char *src, size_t len, Str *out) {
    const unsigned char *p = (const unsigned char *)src;
    size_t i = 0;
    while (i < len) {
        unsigned char c = p[i];
        if (c < 0x80) {
            /* LF/CR/TAB are kept, other controls become a space */
            if (c == '\n' || c == '\r' || c == '\t') str_append_n(out, (const char *)&c, 1);
            else if (c < 0x20) str_append_n(out, " ", 1);
            else str_append_n(out, (const char *)&c, 1);
            i++;
            continue;
        }
        if (c >= GBK_LEAD_MIN && c <= GBK_LEAD_MAX && i + 1 < len) {
            unsigned char t = p[i + 1];
            if (t >= GBK_TRAIL_MIN && t <= GBK_TRAIL_MAX) {
                size_t idx = (size_t)(c - GBK_LEAD_MIN) * GBK_TRAIL_SPAN +
                             (size_t)(t - GBK_TRAIL_MIN);
                uint32_t cp = gbk_to_uni[idx];
                if (cp) { utf8_append_cp(out, cp); i += 2; continue; }
            }
        }
        str_append_n(out, "?", 1);
        i++;
    }
}

void text_decode(const char *src, size_t len, int utf8, Str *out) {
    if (!utf8) { gbk_bytes_to_utf8(src, len, out); return; }
    const char *p = src, *end = src + len;
    while (p < end) {
        const char *q = p;
        uint32_t cp = utf8_next(&p, end);
        if (cp == 0xFFFD && (size_t)(p - q) == 1 && (unsigned char)*q >= 0x80) {
            str_append_n(out, "?", 1);
        } else if (cp == '\r') {
            /* drop CR; LF is kept */
        } else {
            utf8_append_cp(out, cp);
        }
    }
}
