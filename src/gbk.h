#ifndef HIPDA_GBK_H
#define HIPDA_GBK_H

#include "hipda.h"

/* Convert a whole response body to valid UTF-8.
 * utf8 != 0 : input is already UTF-8 (validated, invalid bytes -> '?')
 * utf8 == 0 : input is GBK (the forum's default), mapped via gbk_table.h
 */
void text_decode(const char *src, size_t len, int utf8, Str *out);
void gbk_bytes_to_utf8(const char *src, size_t len, Str *out);

/* UTF-8 helpers. utf8_next() advances p, never reads past end, and returns
 * 0xFFFD for malformed input. */
uint32_t utf8_next(const char **p, const char *end);
void     utf8_append_cp(Str *s, uint32_t cp);
int      utf8_cells(uint32_t cp);        /* 0 = zero-width, 1, 2 = wide */
int      utf8_str_cells(const char *s);
int      utf8_is_valid(const char *s, size_t len);

#endif /* HIPDA_GBK_H */
