#ifndef RESTUNTS_RWIDGETS_H
#define RESTUNTS_RWIDGETS_H

#include <stdint.h>

/*
 * rwidgets.h - the four small drawing widgets seg008/seg012 share between
 * the results screen, the menus, the dialogs and the track editor.
 * See rwidgets.c for provenance and deviations.
 */

/* seg008.asm 3615..3897 - bevelled panel with an optional centred label. */
void draw_button(const char far* label, int16_t x, int16_t y,
                 int16_t w, int16_t h,
                 int16_t arg_C, int16_t arg_E, int16_t arg_10,
                 int16_t arg_12);

/* seg008.asm 3453..3614 - the same bevel without a face fill; the third
 * ring is drawn in its own colour. */
void draw_lines_unk(int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t arg_8, int16_t arg_A, int16_t arg_C);

/* seg012.asm 12092 (-> the shared body at loc_33E1B) - RLE shape blit at a
 * caller-given position. */
void shape2d_op_unk5(void far* shape, int16_t x, int16_t y);

/* seg012.asm 11996 - raw shape blit at the shape's own s2d_pos. */
void sprite_shape_to_1_alt(void far* shape);

/* seg012.asm 10874 - filled rectangle. */
void sprite_1_unk(int16_t x, int16_t y, int16_t w, int16_t h, int16_t colour);

/* seg013.asm 50 - one-pixel rectangle outline, corner to corner. */
void sprite_1_unk4(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                   int16_t colour);

#endif /* RESTUNTS_RWIDGETS_H */
