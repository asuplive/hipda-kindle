#ifndef HIPDA_RENDER_H
#define HIPDA_RENDER_H

#include "hipda.h"
#include "font.h"
#include "layout.h"
#include "raster.h"

/* Paint one whole book page: toolbar (Hi / title / page / refresh ring),
 * content lines, footer and progress bar.
 * `sel_line` is a line index inside the page, or -1 for no highlight.
 * `press_line` is the row under the finger right now (drawn inverted), or -1.
 * `theme` 0 = white paper, 1 = inverted. `refresh_phase` rotates the ring. */
void render_page(Canvas *c, Font *font, const Geometry *g, const Book *b,
                 int page_index, int sel_line, int press_line,
                 const char *flash_msg, int theme, int refresh_phase);

/* Simple message screen (loading / errors / login hints). */
void render_message(Canvas *c, Font *font, const Geometry *g,
                    const char *title, const char *body, int theme);

/* Toolbar hit test: 1 = "Hi" menu button, 2 = refresh ring, 0 = elsewhere. */
int  render_toolbar_hit(const Geometry *g, int x, int y);
/* Footer buttons: 1 = "上一页" (previous forum page), 2 = "下一页",
 * 3 = "返回", 0 = none. */
int  render_footer_btn_hit(const Geometry *g, int x, int y);

#endif /* HIPDA_RENDER_H */
