#ifndef HIPDA_DISCUZ_H
#define HIPDA_DISCUZ_H

#include "hipda.h"

/* HiPDA·NG HiUtils.FORUMS[] (fid -> display name). index.php is behind a
 * Cloudflare JS challenge, so the catalog is baked in instead of scraped. */
extern const Forum kForums[];
extern const int   kForumsCount;
const Forum *forum_by_fid(int fid);

/* Response classification. All inputs are UTF-8. */
int  discuz_detect_utf8(const char *raw, size_t len);
int  discuz_is_cloudflare(const char *html);
int  discuz_is_gate(const char *html);
int  discuz_login_ok(const char *html);
int  discuz_parse_login_form(const char *html, char *formhash, size_t hcap,
                             char *sid, size_t scap);
int  discuz_last_page(const char *html);

int  discuz_parse_topic_list(const char *html, TopicList *out, int fid, int page);
int  discuz_parse_thread(const char *html, ThreadPage *out, int tid, int page);

/* Favourites page: collect distinct thread ids + titles for offline caching. */
int  discuz_parse_favorites(const char *html, TopicList *out);
/* PM / notice lists: message links (pmid=..., notice id=...) with anchor
 * text; out->v[i].tid = 1-based index, `urls` gets one relative path/line. */
int  discuz_parse_msglist(const char *html, TopicList *out, Str *urls);
/* Append an item to a TopicList (for aggregating parsed pages). */
void topic_list_push(TopicList *t, const Topic *item);

void topic_list_free(TopicList *t);
void thread_page_free(ThreadPage *t);

#endif /* HIPDA_DISCUZ_H */
