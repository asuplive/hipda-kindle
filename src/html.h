#ifndef HIPDA_HTML_H
#define HIPDA_HTML_H

#include "hipda.h"
#include <stddef.h>

/* A deliberately small tag-soup reader. It never builds a DOM: it scans tags
 * and hands back the text run immediately preceding each tag. Discuz! 7.2
 * output is well-formed enough for this, and it keeps the binary tiny. */
typedef struct {
    const char *p;            /* next scan position */
    const char *end;
    char        tag[32];      /* lower-case tag name */
    const char *attrs;        /* raw attribute slice */
    const char *attrs_end;
    const char *text;         /* text before this tag */
    size_t      text_len;
    int         closing;
    int         self_closing;
    int         is_comment;
} HtmlIter;

void html_iter_init(HtmlIter *it, const char *data, size_t len);
int  html_next(HtmlIter *it);                 /* 0 at end of document */
int  html_attr(const HtmlIter *it, const char *name, char *out, size_t cap);
int  html_has_class(const HtmlIter *it, const char *cls);
int  html_attr_has(const HtmlIter *it, const char *name, const char *needle);

/* Extract plain text from an HTML fragment, with e-ink friendly substitutions:
 *   <br>, <p>, <div>, </tr>  -> newline
 *   smiley images             -> their text code (e.g. ":)")
 *   attachment images         -> "[图片]"
 *   quotes                    -> "│ " prefix on each line
 *   script/style contents     -> dropped
 * The input must already be UTF-8; entities are decoded.
 */
void html_to_text(const char *html, size_t len, Str *out);

/* Return the text of the first element whose opening tag contains `marker`.
 * Used for titles/authors where only one value is needed. */
int  html_first_text(const char *html, size_t len, const char *marker,
                     char *out, size_t cap);

/* Find the substring that starts at `start_marker` and ends when the nesting
 * depth of `tag` returns to zero. Returns malloc'd slice or NULL. */
char *html_element_slice(const char *html, const char *start_marker,
                         const char *tag);

#endif /* HIPDA_HTML_H */
