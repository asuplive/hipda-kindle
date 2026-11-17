#ifndef HIPDA_NET_H
#define HIPDA_NET_H

#include "hipda.h"

#define NET_OK       0
#define NET_ERR     (-1)
#define NET_CF      (-2)   /* Cloudflare "Just a moment" challenge */
#define NET_GATE    (-3)   /* forum says "you are not logged in"    */
#define NET_OFFLINE (-4)
#define NET_EMPTY   (-5)

int  net_init(const Config *c);
void net_close(void);

void net_url_forum(char *out, size_t cap, const Config *c, int fid, int page);
void net_url_thread(char *out, size_t cap, const Config *c, int tid, int page);
void net_url_login(char *out, size_t cap, const Config *c);

/* Fetch a URL and return its body converted to UTF-8. Cached responses are
 * used when refresh == 0; offline mode only reads the cache. */
int  net_fetch(const Config *c, const char *url, Str *out_utf8, int refresh);

/* Download a binary file (image) to out_path with a hard size cap. 0 = OK. */
int  net_fetch_file(const Config *c, const char *url, const char *out_path,
                    size_t max_bytes);

/* logging.php formhash -> POST (MD5 password) -> cdb_auth cookie. */
int  net_login(const Config *c);
int  net_has_auth_cookie(const Config *c);

#endif /* HIPDA_NET_H */
