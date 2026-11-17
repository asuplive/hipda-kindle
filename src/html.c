#include "html.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void html_iter_init(HtmlIter *it, const char *data, size_t len) {
    memset(it, 0, sizeof(*it));
    it->p = data;
    it->end = data + len;
    it->attrs = it->attrs_end = data;
}

int html_next(HtmlIter *it) {
    const char *p = it->p, *end = it->end;
    it->text_len = 0;
    it->text = p;
    it->tag[0] = 0;
    it->attrs = it->attrs_end = end;
    it->closing = it->self_closing = it->is_comment = 0;
    if (p >= end) return 0;

    const char *lt = (const char *)memchr(p, '<', (size_t)(end - p));
    if (!lt) {
        it->text_len = (size_t)(end - p);
        it->p = end;
        return 1;                       /* trailing text, no tag */
    }
    it->text_len = (size_t)(lt - p);

    const char *q = lt + 1;
    if (q + 3 <= end && !strncmp(q, "!--", 3)) {
        const char *e = q + 3;
        while (e + 2 < end && !(!strncmp(e, "-->", 3))) e++;
        it->p = (e + 2 < end) ? e + 3 : end;
        it->is_comment = 1;
        return 1;                       /* text before the comment */
    }
    if (q < end && (*q == '!' || *q == '?')) {
        const char *gt = (const char *)memchr(q, '>', (size_t)(end - q));
        it->p = gt ? gt + 1 : end;
        it->is_comment = 1;
        return 1;
    }
    if (q < end && *q == '/') { it->closing = 1; q++; }

    size_t n = 0;
    while (q < end && n + 1 < sizeof(it->tag) &&
           (isalnum((unsigned char)*q) || *q == ':' || *q == '_' || *q == '-')) {
        it->tag[n++] = (char)tolower((unsigned char)*q);
        q++;
    }
    it->tag[n] = 0;
    it->attrs = q;

    char quote = 0;
    const char *gt = q;
    while (gt < end) {
        char c = *gt;
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '>') break;
        gt++;
    }
    it->attrs_end = gt;
    if (gt > q && gt[-1] == '/') it->self_closing = 1;
    it->p = (gt < end) ? gt + 1 : end;
    return 1;
}

int html_attr(const HtmlIter *it, const char *name, char *out, size_t cap) {
    if (!cap || !name) return -1;
    out[0] = 0;
    const char *p = it->attrs, *end = it->attrs_end;
    size_t nl = strlen(name);
    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;
        const char *ks = p;
        while (p < end && !isspace((unsigned char)*p) && *p != '=' &&
               *p != '>' && *p != '/') p++;
        size_t kl = (size_t)(p - ks);
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && isspace((unsigned char)*p)) p++;
            char qc = 0;
            if (p < end && (*p == '"' || *p == '\'')) { qc = *p; p++; }
            const char *vs = p;
            if (qc) { while (p < end && *p != qc) p++; }
            else { while (p < end && !isspace((unsigned char)*p) && *p != '>') p++; }
            size_t vl = (size_t)(p - vs);
            if (qc && p < end) p++;
            if (kl == nl && str_ncaseeq(ks, name, nl)) {
                if (vl >= cap) vl = cap - 1;
                /* decode the few entities that matter inside attributes */
                size_t o = 0;
                for (size_t i = 0; i < vl && o + 1 < cap; i++) {
                    if (i + 4 < vl && str_ncaseeq(vs + i, "&amp;", 5)) {
                        out[o++] = '&'; i += 4;
                    } else if (i + 5 < vl && str_ncaseeq(vs + i, "&quot;", 6)) {
                        out[o++] = '"'; i += 5;
                    } else if (i + 3 < vl && str_ncaseeq(vs + i, "&lt;", 4)) {
                        out[o++] = '<'; i += 3;
                    } else if (i + 3 < vl && str_ncaseeq(vs + i, "&gt;", 4)) {
                        out[o++] = '>'; i += 3;
                    } else out[o++] = vs[i];
                }
                out[o] = 0;
                return 0;
            }
            continue;
        }
        if (p < end && *p == '/') p++;
        else if (p == ks) p++;          /* pathological, avoid infinite loop */
    }
    return -1;
}

int html_has_class(const HtmlIter *it, const char *cls) {
    char buf[512];
    if (html_attr(it, "class", buf, sizeof(buf))) return 0;
    size_t cl = strlen(cls);
    const char *p = buf;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *s = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if ((size_t)(p - s) == cl && str_ncaseeq(s, cls, cl)) return 1;
    }
    return 0;
}

int html_attr_has(const HtmlIter *it, const char *name, const char *needle) {
    char buf[HIPDA_URL_LEN];
    if (html_attr(it, name, buf, sizeof(buf))) return 0;
    if (!needle || !*needle) return 1;
    size_t nl = strlen(needle), bl = strlen(buf);
    for (size_t i = 0; i + nl <= bl; i++)
        if (str_ncaseeq(buf + i, needle, nl)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Smiley filename -> text code (from HiPDA·NG Emoji resources)         */
/* ------------------------------------------------------------------ */
typedef struct { const char *file; const char *code; } Smiley;
static const Smiley kSmileys[] = {
    { "default_smile", ":)" },       { "default_sad", ":(" },
    { "default_biggrin", ":D" },     { "default_shocked", ":o" },
    { "default_tongue", ":P" },      { "default_sweat", ":sweat:" },
    { "default_huffy", ":huffy:" },  { "default_cry", ":cry:" },
    { "default_titter", ":titter:" },{ "default_handshake", ":handshake:" },
    { "default_victory", ":victory:" }, { "default_curse", ":curse:" },
    { "default_dizzy", ":dizzy:" },  { "default_shutup", ":shutup:" },
    { "default_funk", ":funk:" },    { "default_loveliness", ":loveliness:" },
    { "default_cool", ":cool:" },    { "default_mad", ":mad:" },
    { "default_lol", ":lol:" },      { "default_shy", ":shy:" },
    { "default_sleepy", ":sleepy:" },
    { "coolmonkey_01", "{:2_41:}" }, { "coolmonkey_02", "{:2_42:}" },
    { "coolmonkey_03", "{:2_43:}" }, { "coolmonkey_04", "{:2_44:}" },
    { "coolmonkey_05", "{:2_45:}" }, { "coolmonkey_06", "{:2_46:}" },
    { "coolmonkey_07", "{:2_47:}" }, { "coolmonkey_08", "{:2_48:}" },
    { "coolmonkey_09", "{:2_49:}" }, { "coolmonkey_10", "{:2_50:}" },
    { "coolmonkey_11", "{:2_51:}" }, { "coolmonkey_12", "{:2_52:}" },
    { "coolmonkey_13", "{:2_53:}" }, { "coolmonkey_14", "{:2_54:}" },
    { "coolmonkey_15", "{:2_55:}" }, { "coolmonkey_16", "{:2_56:}" },
};

typedef struct {
    Str  *out;
    int   at_line_start;
    int   newline_run;
    int   quote;
    int   div_depth;
    int   quote_depth;
    int   skip;
} TextCtx;

static void tc_newline(TextCtx *c) {
    if (c->newline_run >= 2) return;
    if (c->out->len == 0) return;
    str_append_n(c->out, "\n", 1);
    c->newline_run++;
    c->at_line_start = 1;
}
static void tc_prefix(TextCtx *c) {
    if (c->at_line_start) {
        if (c->quote) str_append(c->out, "│ ");
        c->at_line_start = 0;
    }
}
static void tc_text(TextCtx *c, const char *s, size_t n) {
    if (c->skip || !n) return;
    Str tmp; str_init(&tmp);
    html_unescape_n(s, n, &tmp);
    int space_pending = 0;
    const char *p = tmp.data, *end = tmp.data + tmp.len;
    while (p < end) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n' || ch == '\r') { space_pending = 0; tc_newline(c); p++; continue; }
        if (ch == 0xC2 && p + 1 < end && (unsigned char)p[1] == 0xA0) {
            /* U+00A0 NBSP */
            space_pending = 1; p += 2; continue;
        }
        if (ch == ' ' || ch == '\t') {
            space_pending = 1; p++; continue;
        }
        if (space_pending) {
            if (c->out->len && c->out->data[c->out->len - 1] != '\n') {
                tc_prefix(c); str_append_n(c->out, " ", 1);
            }
            space_pending = 0;
        }
        tc_prefix(c);
        /* copy one UTF-8 sequence */
        int need = (ch < 0x80) ? 1 : (ch & 0xE0) == 0xC0 ? 2 :
                   (ch & 0xF0) == 0xE0 ? 3 : (ch & 0xF8) == 0xF0 ? 4 : 1;
        if ((size_t)(end - p) < (size_t)need) need = 1;
        str_append_n(c->out, p, (size_t)need);
        c->newline_run = 0;
        p += need;
    }
    if (space_pending && c->out->len && c->out->data[c->out->len - 1] != '\n')
        str_append_n(c->out, " ", 1);
    str_free(&tmp);
}

static void tc_image(TextCtx *c, const HtmlIter *it) {
    char src[HIPDA_URL_LEN], file[HIPDA_URL_LEN], alt[256];
    int have_src = !html_attr(it, "src", src, sizeof(src));
    int have_file = !html_attr(it, "file", file, sizeof(file));
    (void)have_file;
    int have_alt = !html_attr(it, "alt", alt, sizeof(alt));
    if (!have_src) src[0] = 0;
    const char *leaf = strrchr(src, '/');
    leaf = leaf ? leaf + 1 : src;
    if (strstr(src, "images/smilies/") || strstr(src, "smilies/")) {
        char stem[128]; str_copy(stem, sizeof(stem), leaf);
        char *dot = strrchr(stem, '.');
        if (dot) *dot = 0;
        for (size_t i = 0; i < sizeof(kSmileys) / sizeof(kSmileys[0]); i++) {
            if (str_caseeq(stem, kSmileys[i].file)) {
                tc_prefix(c); str_append(c->out, kSmileys[i].code);
                c->newline_run = 0; return;
            }
        }
        tc_prefix(c); str_append(c->out, "[表情]"); c->newline_run = 0;
        return;
    }
    /* Real image (attachment or inline): emit a \x02-wrapped URL on its own
     * line. layout turns it into a tappable placeholder row; images whose
     * only purpose is decoration (smilies above) never get here.
     * Forum UI icons (images/common, images/default) and GIFs (tiny
     * decorations or animations e-ink can't show) are skipped entirely so
     * list rows and titles stay clean. */
    const char *dot = strrchr(leaf, '.');
    if (dot && str_caseeq(dot, ".gif")) return;
    if (strstr(src, "images/common/") || strstr(src, "images/default/")) return;
    if (src[0]) {
        tc_prefix(c);
        str_append(c->out, "\n\x02");
        str_append(c->out, src);
        str_append(c->out, "\x02\n");
        c->newline_run = 1;
        return;
    }
    if (have_alt && alt[0] && !str_caseeq(alt, "attach")) {
        tc_prefix(c); str_append(c->out, alt); c->newline_run = 0;
    }
}

void html_to_text(const char *html, size_t len, Str *out) {
    HtmlIter it; html_iter_init(&it, html, len);
    TextCtx c;
    memset(&c, 0, sizeof(c));
    c.out = out;
    c.at_line_start = 1;
    while (html_next(&it)) {
        if (it.text_len) tc_text(&c, it.text, it.text_len);
        if (!it.tag[0] || it.is_comment) continue;
        if (!strcmp(it.tag, "script") || !strcmp(it.tag, "style") ||
            !strcmp(it.tag, "noscript")) {
            c.skip = it.closing ? 0 : 1;
            continue;
        }
        if (c.skip) continue;
        if (!strcmp(it.tag, "div")) {
            if (it.closing) {
                c.div_depth--;
                if (c.quote && c.div_depth <= c.quote_depth) c.quote = 0;
            } else {
                c.div_depth++;
                if (!it.self_closing && html_has_class(&it, "quote")) {
                    c.quote = 1; c.quote_depth = c.div_depth - 1;
                }
                tc_newline(&c);
            }
        } else if (!strcmp(it.tag, "br")) {
            tc_newline(&c);
        } else if (!strcmp(it.tag, "p") || !strcmp(it.tag, "tr") ||
                   !strcmp(it.tag, "li") || !strcmp(it.tag, "blockquote") ||
                   !strcmp(it.tag, "h1") || !strcmp(it.tag, "h2") ||
                   !strcmp(it.tag, "h3") || !strcmp(it.tag, "table")) {
            tc_newline(&c);
        } else if (!strcmp(it.tag, "td") || !strcmp(it.tag, "th")) {
            if (!it.closing) { tc_prefix(&c); str_append_n(c.out, " ", 1); }
        } else if (!strcmp(it.tag, "img")) {
            tc_image(&c, &it);
        }
    }
    /* trim trailing blank lines/spaces */
    while (out->len && (out->data[out->len - 1] == '\n' ||
                        out->data[out->len - 1] == ' ')) str_shrink(out, out->len - 1);
}

int html_first_text(const char *html, size_t len, const char *marker,
                    char *out, size_t cap) {
    if (!out || !cap) return -1;
    out[0] = 0;
    const char *p = strstr(html, marker);
    if (!p) return -1;
    const char *gt = (const char *)memchr(p, '>', (size_t)(len - (size_t)(p - html)));
    if (!gt) return -1;
    gt++;
    const char *e = gt;
    int depth = 0;
    while (e < html + len && *e) {
        if (*e == '<') {
            const char *t = e + 1;
            int clos = (*t == '/');
            if (clos) t++;
            depth += clos ? -1 : 1;
            if (depth < 0) break;
            const char *close = (const char *)memchr(t, '>', (size_t)(html + len - t));
            e = close ? close + 1 : html + len;
            continue;
        }
        e++;
    }
    Str tmp; str_init(&tmp);
    html_to_text(gt, (size_t)(e - gt), &tmp);
    str_copy(out, cap, str_trim(tmp.data));
    str_free(&tmp);
    return out[0] ? 0 : -1;
}

char *html_element_slice(const char *html, const char *start_marker,
                         const char *tag) {
    const char *p = strstr(html, start_marker);
    if (!p) return NULL;
    /* Start at the opening '<', not at the marker inside its attribute list.
     * Otherwise html_to_text() would see `class="t_msgfont">` as plain text
     * and paste raw markup into the post body. */
    const char *start = p;
    while (start > html && *start != '<') start--;
    if (*start != '<') start = p;
    const char *gt = strchr(start, '>');
    if (!gt) return NULL;
    const char *scan = gt + 1;
    int depth = 1;
    size_t tl = strlen(tag);
    while (*scan) {
        if (*scan != '<') { scan++; continue; }
        const char *t = scan + 1;
        int clos = (*t == '/');
        if (clos) t++;
        if ((size_t)(strchr(t, '>') ? (size_t)(strchr(t, '>') - t) : 0) >= tl &&
            str_ncaseeq(t, tag, tl) &&
            (t[tl] == ' ' || t[tl] == '>' || t[tl] == '\r' || t[tl] == '\n' || t[tl] == '/')) {
            const char *close = strchr(t, '>');
            int self_close = (close && close > t && close[-1] == '/');
            if (clos) {
                depth--;
                if (depth <= 0) {
                    size_t n = (size_t)(close + 1 - start);
                    char *out = xmalloc(n + 1);
                    memcpy(out, start, n); out[n] = 0;
                    return out;
                }
            } else if (!self_close) depth++;
            scan = close ? close + 1 : scan + 1;
            continue;
        }
        const char *close = strchr(scan, '>');
        scan = close ? close + 1 : scan + 1;
    }
    size_t n = strlen(start);
    char *out = xmalloc(n + 1);
    memcpy(out, start, n); out[n] = 0;
    return out;
}
