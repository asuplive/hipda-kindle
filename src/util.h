#ifndef HIPDA_UTIL_H
#define HIPDA_UTIL_H

#include "hipda.h"

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

void  str_init(Str *s);
void  str_free(Str *s);
void  str_clear(Str *s);
void  str_append_n(Str *s, const char *p, size_t n);
void  str_append(Str *s, const char *p);
void  str_appendf(Str *s, const char *fmt, ...);
void  str_append_bytes(Str *s, const char *p, size_t n); /* raw, may contain NUL */
void  str_shrink(Str *s, size_t n);

/* Read/write an entire file. Returns 0 on success, -1 on error. */
int   file_read(const char *path, Str *out);
int   file_write(const char *path, const void *data, size_t len);
int   file_exists(const char *path);
int   dir_ensure(const char *path);

/* Percent-encode arbitrary bytes (used for GBK-encoded form values). */
void  url_encode(const char *s, size_t len, Str *out);
void  url_encode_str(const char *s, Str *out);

/* Decode HTML character/numeric entities into UTF-8. */
void  html_unescape_n(const char *s, size_t len, Str *out);

char *str_trim(char *s);                 /* in place, returns s */
void  str_copy(char *dst, size_t cap, const char *src);
int   str_caseeq(const char *a, const char *b);
int   str_ncaseeq(const char *a, const char *b, size_t n);
int   parse_int(const char *s, int fallback);

/* Query-param extractor: find "name=" in a URL and copy its raw value. */
int   url_param(const char *url, const char *name, char *out, size_t cap);

void  log_msg(const char *fmt, ...);
void  log_set_file(const char *path);
void  log_close(void);

#endif /* HIPDA_UTIL_H */
