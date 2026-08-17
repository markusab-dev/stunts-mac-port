#ifndef RESTUNTS_RHIGHSCORE_H
#define RESTUNTS_RHIGHSCORE_H

#include <stdint.h>
#include <stddef.h>

/* seg000 highscore_write_a: 0 = load <track>.HIG, 1 = create a blank one.
 * Returns 1 on failure, 0 on success (the original's own polarity). */
int16_t highscore_write_a(int16_t create);

/* seg000 highscore_write_b: write the table out in display order. */
int16_t highscore_write_b(void);

/* seg000 print_highscore_entry: pack row `row` into `buf` as four
 * NUL-separated strings and report their offsets in off[0..3]. */
void print_highscore_entry(int16_t row, uint8_t off[4], char* buf, size_t bufsz);

/* seg008 hiscore_draw_text (3264..3345): `str` outlined at the four
 * diagonal neighbours in `shadow`, then drawn at (x, y) in `colour`.
 * Phase 6's results screen draws every statistic line with it. */
void hiscore_draw_text(const char* str, int16_t x, int16_t y,
                       int16_t colour, int16_t shadow);

/* seg000 highscore_text_unk: draw the whole table. */
void highscore_text_unk(void);

/* seg000 enter_hiscore, split in two so the name entry can be an SDL widget:
 * would_enter returns the row a time would land on, or -1. */
int16_t highscore_would_enter(int16_t time);
void highscore_insert(int16_t row, const char* carname,
                      int16_t parenflag, const char* opponent);
void highscore_set_name(const char* name);
int16_t highscore_highlight(void);

#endif
