#ifndef HIPDA_LAYOUT_H
#define HIPDA_LAYOUT_H

#include "hipda.h"
#include "discuz.h"

/* Every screen in this app is a "book": a flat stream of lines sliced into
 * fixed pages. Nothing ever scrolls. That is the whole point -- reading a
 * thread feels exactly like turning a page in a Kindle book. */
typedef enum {
    L_BLANK = 0,
    L_TITLE,       /* thread/forum title, bold */
    L_META,        /* author, floor, time, counts (gray) */
    L_BODY,        /* post text */
    L_ITEM,        /* selectable list row (forum / topic) */
    L_SEP,         /* thin rule between posts */
    L_NOTE         /* "forum page 2" marker, hints */
} LineKind;

typedef struct {
    LineKind kind;
    char    *text;
    int      indent;      /* leading cells */
    int      item_id;     /* fid / tid for L_ITEM, 0 otherwise */
    int      forum_page;  /* which Discuz page this line came from */
} Line;

typedef struct {
    int start, count;
    int forum_page;
} Page;

typedef struct {
    Line *lines;
    int   n, cap;
    Page *pages;
    int   np, pcap;
    char  title[HIPDA_NAME_LEN];
    char  subtitle[HIPDA_NAME_LEN];
    int   fid, tid;
    int   cols, rows;
} Book;

void geometry_compute(Geometry *g, int w, int h, int dpi, int font_scale);

void  book_init(Book *b, const Geometry *g);
void  book_free(Book *b);
void  book_reset(Book *b);              /* keep geometry + title, drop lines */
int   book_add_line(Book *b, LineKind kind, const char *text, int indent,
                    int item_id, int forum_page);
int   book_wrap_text(Book *b, const Geometry *g, LineKind kind, const char *text,
                     int indent, int item_id, int forum_page);
void  book_finalize(Book *b, const Geometry *g);   /* (re)slice lines into pages */

int   book_from_forums(Book *b, const Geometry *g, const Config *cfg);
int   book_from_topics(Book *b, const Geometry *g, const TopicList *list,
                       const Forum *forum, int append);
int   book_from_thread(Book *b, const Geometry *g, const ThreadPage *thread,
                       int append);

/* Generic list screen: every item is a selectable L_ITEM whose item_id is
 * 1..n (the caller maps it back to an action). */
int   book_from_menu(Book *b, const Geometry *g, const char *title,
                     const char *subtitle, const char *const *items, int n);
/* Plain text screen (search results, PMs, "my posts" etc.). */
int   book_from_text(Book *b, const Geometry *g, const char *title,
                     const char *text);

/* PC-side preview data (real layout, consumed by preview/index.html). */
int   book_write_json(Book *b, const char *path);

#endif /* HIPDA_LAYOUT_H */
