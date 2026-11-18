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
    L_NOTE,        /* "forum page 2" marker, hints */
    L_KEYS,        /* on-screen keyboard: one row of 10 boxed keys */
    L_CTRL,        /* keyboard control row: Sh/Pg/Space/Back/Enter/Cancel */
    L_BTN,         /* wide centred button ("登 录") */
    L_IMGPH,       /* image placeholder row; item_id = URL hash */
    L_CAND,        /* pinyin candidate row: up to 8 boxed UTF-8 chars */
    L_FTITLE,      /* forum-home section title: bold, item_id = fid */
    L_FDESC,       /* forum-home description: small + faint, item_id = 0 */
    L_GROUP,       /* section caption: small solid square + bold label */
    L_CARD_T,      /* card top row: bold title + right chevron, item_id=id */
    L_CARD_B,      /* card bottom row: small gray subtitle (pairs with T) */
    L_NUM,         /* numbered item row: "NN" + title + chevron (menu/settings) */
    L_POSTHEAD,    /* post floor header: gray text with a thick left bar */
    L_FIELD        /* login/search input: "label|value", boxed field card */
} LineKind;

/* Keyboard geometry contract shared by painter and hit-test:
 * an L_KEYS row holds 10 keys, each in a KEY_CHARS-char slot ("x  ");
 * an L_CTRL row paints 6 buttons, each 6 cells wide. */
#define KEY_CHARS 3
#define KEY_COLS 10
#define KEY_CTRL_COLS 6
#define KEY_CTRL_STRIDE 6
#define KEY_CAND_STRIDE 5      /* each candidate slot is 5 cells wide */
#define KEY_CAND_SLOTS 8

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

/* Add a framed card occupying two rows (title + subtitle). item_id on the
 * title row makes the whole card tappable; the subtitle row has id 0 and
 * hit-testing falls through to the title above it. */
void book_add_card(Book *b, const char *title, const char *sub, int indent,
                   int item_id, int forum_page);


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
                       int append, Str *img_urls, int img_mode);

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
