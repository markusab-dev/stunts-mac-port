/*
 * rwidgets.c - the four small drawing widgets the original shares between
 * every panelled screen: the results screen (Phase 6), the option and
 * opponent menus, show_dialog, and the track editor.  Built once, here.
 *
 * Ported from reference/restunts/src/restunts/asm/:
 *
 *   draw_button           seg008.asm 3615..3897   (282 lines)
 *   draw_lines_unk        seg008.asm 3453..3614   (161 lines)
 *   shape2d_op_unk5       seg012.asm 12092..12119 + loc_33E1B (the shared
 *                         RLE body inside shape2d_op_unk, 12119..12200)
 *   sprite_shape_to_1_alt seg012.asm 11996..12069
 *   sprite_1_unk          seg012.asm 10874..10951 (the rect fill draw_button
 *                         lays its face down with)
 *   sprite_1_unk4         seg013.asm   50..145    (the blinking selection
 *                         outline mouse_timer_sprite_unk draws)
 *
 * ------------------------------------------------------------------------
 * THE BEVEL IS TWELVE LINES, NOT FOUR FILLS
 *
 * Both draw_button and draw_lines_unk lay the same figure down with twelve
 * preRender_line calls, in this exact order and with these exact endpoints
 * (si = x + w, di = y + h):
 *
 *      1  (x,   y  ) -> (si,   y  )     top,    outer
 *      2  (x+1, y+1) -> (si-1, y+1)     top,    second
 *      3  (x+2, y+2) -> (si-2, y+2)     top,    third
 *      4  (x,   y  ) -> (x,    di )     left,   outer
 *      5  (x+1, y+1) -> (x+1,  di-1)    left,   second
 *      6  (x+2, y+2) -> (x+2,  di-2)    left,   third
 *      7  (x,   di ) -> (si,   di )     bottom, outer
 *      8  (x+1, di-1)->(si-1, di-1)     bottom, second
 *      9  (x+2, di-2)->(si-2, di-2)     bottom, third
 *     10  (si,  y  ) -> (si,   di )     right,  outer
 *     11  (si-1,y+1) -> (si-1, di-1)    right,  second
 *     12  (si-2,y+2) -> (si-2, di-2)    right,  third
 *
 * The two routines differ only in which colour each line gets.  draw_button
 * paints 1-6 in arg_C and 7-12 in arg_E - a plain raised bevel.
 * draw_lines_unk paints 1,2 and 4,5 in arg_8, 7,8 and 10,11 in arg_C, and
 * *all four* of the innermost lines 3,6,9,12 in arg_A - so the third ring is
 * a closed frame in its own colour, which is why the opponent's portrait
 * sits inside a light/dark bevel with a black hairline around the picture.
 * A four-fill approximation cannot produce that ring.  [This file replaces
 * main_native.c's earlier four-fill menu_button; that one was declared a
 * deviation and this is the real thing.]
 *
 * Note the endpoints are inclusive: a call with w = 0x140 on the 320-wide
 * screen draws its right edge at x = 320, one column off the screen, and
 * the clip below drops it.  That is the original's own arithmetic
 * (end_hiscore's two panels are draw_button(NULL, 0, 0, 0x140, 0x64) and
 * (NULL, 0, 0x65, 0x140, 0x63)), so it is reproduced rather than corrected.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] Where the pixels go.
 *
 * The original writes through es:[sprite_lineofs[y] + x] with es taken from
 * sprite1.sprite_bitmapptr, i.e. into whichever sprite window is currently
 * selected.  This port has one framebuffer, has never filled sprite_lineofs
 * or sprite_bitmapptr, and composites its menus in truecolour after the 3D
 * frame has been converted - exactly the deviation draw_filled_lines,
 * putpixel_single_maybe and rshape2d.c's blitters already carry.  So the
 * address arithmetic becomes rfb_pixels[y*RFB_W + x], or rs_rgba[] +
 * rs_pal[] when the truecolour target is armed, and every coordinate is
 * multiplied by RFB_SCALE on the way in (these are 320x200 UI coordinates,
 * as the cockpit art and the font are).  Geometry, order and colour indices
 * are the original's.
 *
 * [DEVIATION] shape2d_op_unk5 and sprite_shape_to_1_alt clip to the
 * framebuffer.  The originals do not clip at all - shape2d_op_unk5 wraps
 * its RLE stream onto the next line by counting pixels and simply runs off
 * the bottom of the sprite if the caller mis-places the shape.  end_hiscore
 * never does, but writing outside the buffer is not survivable here.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "externs.h"
#include "rfbsize.h"
#include "rwidgets.h"
#include "shape2d.h"

extern uint8_t rfb_pixels[];
extern uint32_t* rs_rgba;           /* rshape2d.c: truecolour target      */
extern const uint32_t* rs_pal;
extern uint16_t font_op2(const char* str);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern void font_set_colour(uint16_t colour, uint16_t unused);

/* One UI pixel, to whichever target is armed - the same `put` rshape2d.c
 * uses, kept local so the two files stay independent. */
static void rw_put(int16_t x, int16_t y, uint8_t idx)
{
	int16_t dx, dy;
	int16_t px0 = (int16_t)(x * RFB_SCALE);
	int16_t py0 = (int16_t)(y * RFB_SCALE);
	for (dy = 0; dy < RFB_SCALE; dy++) {
		int16_t py = (int16_t)(py0 + dy);
		if (py < 0 || py >= RFB_VIEW_H) continue;
		for (dx = 0; dx < RFB_SCALE; dx++) {
			int16_t px = (int16_t)(px0 + dx);
			int32_t o;
			if (px < 0 || px >= RFB_VIEW_W) continue;
			o = (int32_t)py * RFB_VIEW_W + px;
			if (rs_rgba) rs_rgba[o] = rs_pal[idx];
			else         rfb_pixels[o] = idx;
		}
	}
}

/* preRender_line's Bresenham (rblit.c), in UI coordinates.  Every line the
 * two bevel routines draw is axis-aligned, so this plots exactly the pixels
 * the original plots. */
static void rw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    int16_t colour)
{
	int16_t dx = (int16_t)(x1 - x0), sx = dx >= 0 ? 1 : -1;
	int16_t dy = (int16_t)(y1 - y0), sy = dy >= 0 ? 1 : -1;
	int16_t adx = (int16_t)(dx >= 0 ? dx : -dx);
	int16_t ady = (int16_t)(dy >= 0 ? dy : -dy);
	int16_t err = (int16_t)((adx > ady ? adx : -ady) / 2), e2;

	for (;;) {
		rw_put(x0, y0, (uint8_t)colour);
		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -adx) { err = (int16_t)(err - ady); x0 = (int16_t)(x0 + sx); }
		if (e2 < ady)  { err = (int16_t)(err + adx); y0 = (int16_t)(y0 + sy); }
	}
}

/* seg012 sprite_1_unk(x, y, w, h, colour) - filled rectangle, exclusive of
 * x+w and y+h.  The original bails out when w <= 0 or h <= 0 (loc_335CF). */
void sprite_1_unk(int16_t x, int16_t y, int16_t w, int16_t h, int16_t colour)
{
	int16_t i, j;
	if (w <= 0 || h <= 0) return;
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++)
			rw_put((int16_t)(x + i), (int16_t)(y + j), (uint8_t)colour);
}

/*
 * seg013 sprite_1_unk4(x1, y1, x2, y2, colour) - a one-pixel outline.
 * Instruction for instruction:
 *     var_2 = x2 - x1 + 1;   var_4 = y2 - y1;              (note: no +1)
 *     if (var_2 > 0) { fill(x1, y1, var_2, 1); fill(x1, y2, var_2, 1); }
 *     if (var_4 > 0) { fill(x1, y1, 1, var_4); fill(x2, y1, 1, var_4); }
 * The asymmetry between the two lengths is the original's; it leaves the
 * two bottom corners covered by the horizontal runs and the verticals one
 * pixel short.  [ODDITY - faithful]
 */
void sprite_1_unk4(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                   int16_t colour)
{
	int16_t var_2 = (int16_t)(x2 - x1 + 1);
	int16_t var_4 = (int16_t)(y2 - y1);
	if (var_2 > 0) {
		sprite_1_unk(x1, y1, var_2, 1, colour);
		sprite_1_unk(x1, y2, var_2, 1, colour);
	}
	if (var_4 > 0) {
		sprite_1_unk(x1, y1, 1, var_4, colour);
		sprite_1_unk(x2, y1, 1, var_4, colour);
	}
}

/* seg008 draw_lines_unk - the twelve-line frame, third ring in arg_A. */
void draw_lines_unk(int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t arg_8, int16_t arg_A, int16_t arg_C)
{
	int16_t si = (int16_t)(x + w);
	int16_t di = (int16_t)(y + h);

	rw_line(x, y, si, y, arg_8);                                    /*  1 */
	rw_line((int16_t)(x + 1), (int16_t)(y + 1),
	        (int16_t)(si - 1), (int16_t)(y + 1), arg_8);            /*  2 */
	rw_line((int16_t)(x + 2), (int16_t)(y + 2),
	        (int16_t)(si - 2), (int16_t)(y + 2), arg_A);            /*  3 */
	rw_line(x, y, x, di, arg_8);                                    /*  4 */
	rw_line((int16_t)(x + 1), (int16_t)(y + 1),
	        (int16_t)(x + 1), (int16_t)(di - 1), arg_8);            /*  5 */
	rw_line((int16_t)(x + 2), (int16_t)(y + 2),
	        (int16_t)(x + 2), (int16_t)(di - 2), arg_A);            /*  6 */
	rw_line(x, di, si, di, arg_C);                                  /*  7 */
	rw_line((int16_t)(x + 1), (int16_t)(di - 1),
	        (int16_t)(si - 1), (int16_t)(di - 1), arg_C);           /*  8 */
	rw_line((int16_t)(x + 2), (int16_t)(di - 2),
	        (int16_t)(si - 2), (int16_t)(di - 2), arg_A);           /*  9 */
	rw_line(si, y, si, di, arg_C);                                  /* 10 */
	rw_line((int16_t)(si - 1), (int16_t)(y + 1),
	        (int16_t)(si - 1), (int16_t)(di - 1), arg_C);           /* 11 */
	rw_line((int16_t)(si - 2), (int16_t)(y + 2),
	        (int16_t)(si - 2), (int16_t)(di - 2), arg_A);           /* 12 */
}

/*
 * seg008 draw_button(label, x, y, w, h, arg_C, arg_E, arg_10, arg_12).
 *
 * arg_C  = top/left bevel colour     (word_407F4 = 15 everywhere in seg000)
 * arg_E  = bottom/right bevel colour (word_407F6 =  8)
 * arg_10 = face colour               (word_407F8 =  7)
 * arg_12 = label colour, passed to font_set_unk(arg_12, 0)
 *
 * The label is split on ']' into lines of eight pixels:
 *     lines = 1 + count(']')
 *     y0    = (h - lines*8) / 2 + 1                (C truncating divide)
 *     line i at y + y0 + i*8, x = x_button + (w - font_op2(line)) / 2
 * The loop runs to strlen INCLUSIVE, so the terminating NUL flushes the
 * last line; a label that ends in ']' therefore emits one empty line, and
 * an empty label emits one too.  [ODDITY - faithful]
 *
 * [DEVIATION] the original copies the label into its 0xAC74 scratch area
 * (resID_byte1) with copy_string and works from there.  resID_byte1 is 32
 * bytes in this port and the scratch area is about 83 in dseg, so the copy
 * lands in a local buffer instead; nothing else reads it between the copy
 * and the last font_draw_text.
 */
void draw_button(const char far* label, int16_t x, int16_t y,
                 int16_t w, int16_t h,
                 int16_t arg_C, int16_t arg_E, int16_t arg_10,
                 int16_t arg_12)
{
	int16_t si = (int16_t)(x + w);
	int16_t di = (int16_t)(y + h);
	char buf[96];
	char seg[96];
	int16_t var_5C, var_62, var_60;
	int16_t var_8, var_2, var_6;
	char var_4;

	sprite_1_unk(x, y, w, h, arg_10);

	rw_line(x, y, si, y, arg_C);                                    /*  1 */
	rw_line((int16_t)(x + 1), (int16_t)(y + 1),
	        (int16_t)(si - 1), (int16_t)(y + 1), arg_C);            /*  2 */
	rw_line((int16_t)(x + 2), (int16_t)(y + 2),
	        (int16_t)(si - 2), (int16_t)(y + 2), arg_C);            /*  3 */
	rw_line(x, y, x, di, arg_C);                                    /*  4 */
	rw_line((int16_t)(x + 1), (int16_t)(y + 1),
	        (int16_t)(x + 1), (int16_t)(di - 1), arg_C);            /*  5 */
	rw_line((int16_t)(x + 2), (int16_t)(y + 2),
	        (int16_t)(x + 2), (int16_t)(di - 2), arg_C);            /*  6 */
	rw_line(x, di, si, di, arg_E);                                  /*  7 */
	rw_line((int16_t)(x + 1), (int16_t)(di - 1),
	        (int16_t)(si - 1), (int16_t)(di - 1), arg_E);           /*  8 */
	rw_line((int16_t)(x + 2), (int16_t)(di - 2),
	        (int16_t)(si - 2), (int16_t)(di - 2), arg_E);           /*  9 */
	rw_line(si, y, si, di, arg_E);                                  /* 10 */
	rw_line((int16_t)(si - 1), (int16_t)(y + 1),
	        (int16_t)(si - 1), (int16_t)(di - 1), arg_E);           /* 11 */
	rw_line((int16_t)(si - 2), (int16_t)(y + 2),
	        (int16_t)(si - 2), (int16_t)(di - 2), arg_E);           /* 12 */

	if (!label) return;                     /* loc_29466: or ax,[arg_2] */

	font_set_colour((uint16_t)arg_12, 0);   /* font_set_unk(arg_12, 0)  */
	snprintf(buf, sizeof buf, "%s", (const char*)label);

	var_5C = 1;
	var_62 = (int16_t)strlen(buf);
	for (var_8 = 0; var_8 < var_62; var_8++)
		if (buf[var_8] == ']') var_5C++;

	var_2 = 0;
	var_6 = 0;
	var_60 = (int16_t)((h - var_5C * 8) / 2 + 1);

	for (var_8 = 0; var_8 <= var_62; var_8++) {
		var_4 = buf[var_8];
		if (var_4 != ']' && var_4 != 0) {
			if (var_2 < (int16_t)sizeof seg - 1) seg[var_2++] = var_4;
			continue;
		}
		seg[var_2] = 0;
		{
			int16_t ty = (int16_t)(var_6 * 8 + y + var_60);
			int16_t cx = (int16_t)font_op2(seg);
			font_draw_text(seg, (int16_t)(x + (w - cx) / 2), ty);
		}
		var_6++;
		var_2 = 0;
	}
}

/*
 * seg012 shape2d_op_unk5(shape, x, y).  The proc itself is nine
 * instructions that load ds:si from the far pointer, copy the two
 * coordinates into var_2/var_4 and `jmp short loc_33E1B` - the body of
 * shape2d_op_unk, which is the RLE decoder:
 *
 *     lodsb; al > 0 -> a run of `al` copies of the next byte
 *            al < 0 -> `-al` literal bytes
 *            al = 0 -> end of shape
 *
 * with a pixel counter (dx, from the shape's width) that steps to the next
 * scanline whenever it runs out, so runs may straddle rows.
 *
 * The shapes end_hiscore feeds it - op01..op08 in OPP<n>WIN/LOSE.PVS - are
 * raw, not RLE: every one is exactly width*height+16 bytes on disk, because
 * file_load_resource(3, ...) has already run parse_shape2d over the
 * archive.  Same situation rshape2d.c records for the cockpit "roof".  So
 * the raw path is what actually runs; the RLE decoder is transcribed and
 * kept behind the same size test, and if a shape ever arrives compressed it
 * will be decoded rather than smeared.
 */
void shape2d_op_unk5(void far* arg_shape, int16_t x, int16_t y)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	src = (const uint8_t far*)arg_shape + sizeof(struct SHAPE2D);
	if (w <= 0 || h <= 0) return;

	{
		/* mmgr_get_chunk_size_bytes is not available for a sub-resource,
		 * so "is it raw?" is decided the way rshape2d.c decides it: the
		 * archive stores width*height bytes for an expanded shape. Try
		 * the RLE stream only if decoding it terminates cleanly on
		 * exactly width*height pixels. */
		int32_t total = (int32_t)w * h;
		int32_t decoded = 0;
		const uint8_t far* p = src;
		int rle_ok = 0;
		for (;;) {
			int8_t n = (int8_t)*p++;
			if (n == 0) { rle_ok = (decoded == total); break; }
			if (n > 0) { decoded += n; p++; }
			else       { decoded += -n; p += -n; }
			if (decoded > total) break;
			if (p - src > total + 16) break;
		}

		if (!rle_ok) {                       /* the raw path, as shipped */
			int16_t row, col;
			for (row = 0; row < h; row++)
				for (col = 0; col < w; col++)
					rw_put((int16_t)(x + col), (int16_t)(y + row),
					       src[(int32_t)row * w + col]);
			return;
		}

		{   /* loc_33E1B, transcribed */
			int16_t cx = 0, cy = 0;
			p = src;
			for (;;) {
				int8_t n = (int8_t)*p++;
				int16_t k, run;
				uint8_t v = 0;
				if (n == 0) break;
				if (n > 0) { run = n; v = *p++; }
				else       { run = (int16_t)(-n); }
				for (k = 0; k < run; k++) {
					uint8_t c = (n > 0) ? v : *p++;
					if (cy < h) rw_put((int16_t)(x + cx),
					                   (int16_t)(y + cy), c);
					if (++cx >= w) { cx = 0; cy++; }
				}
			}
		}
	}
}

/*
 * seg012 sprite_shape_to_1_alt(shape) - the raw `rep movsw` blit at the
 * shape's own s2d_pos_x / s2d_pos_y.  (sprite_shape_to_1 is the same body
 * entered with the position as arguments; shape2d_op_unk3 in rshape2d.c is
 * the clipped variant this port already had.)
 */
void sprite_shape_to_1_alt(void far* arg_shape)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h, x, y, row, col;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	x = (int16_t)shape->s2d_pos_x;
	y = (int16_t)shape->s2d_pos_y;
	src = (const uint8_t far*)arg_shape + sizeof(struct SHAPE2D);

	for (row = 0; row < h; row++)
		for (col = 0; col < w; col++)
			rw_put((int16_t)(x + col), (int16_t)(y + row),
			       src[(int32_t)row * w + col]);
}
