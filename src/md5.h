#ifndef HIPDA_MD5_H
#define HIPDA_MD5_H

#include <stddef.h>

/* Lowercase hex MD5 of a NUL-terminated string (32 chars + NUL).
 * The forum's logging.php uses JS pwmd5() == MD5(password) in hex, so the
 * device can log in without ever storing the plaintext password. */
void md5_hex_str(const char *s, char out[33]);

#endif /* HIPDA_MD5_H */
