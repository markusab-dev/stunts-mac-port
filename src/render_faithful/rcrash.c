/*
 * rcrash.c - what you see when it goes wrong: the cracked windscreen and
 * the car sinking into water.
 *
 * Ported from
 *   seg003.asm  init_crak (227), do_sinking (65)
 *
 * THE CRACKS ARE LINES, NOT A BITMAP
 * ----------------------------------
 * GAME.PRE carries two resources for this:
 *
 *   crak   four-word records - x0, y0, x1, y1 - one per crack segment. They
 *          all start from the same impact point, (131, 84), and fan out:
 *          (131,84)-(159,66), (131,84)-(185,91), (131,84)-(142,107), ...
 *   cinf   [0] = how many stages there are, then one running total per stage.
 *          Here: 4 stages, at 6, 18, 42 and 77 segments.
 *
 * So the windscreen shatters in four steps as the frames tick by, and which
 * step you are on is `framecount / (framespersec / 7)`, clamped to the last.
 *
 * Each segment is drawn three times: black one pixel above, black one pixel
 * below, and dialog_fnt_colour in between. That is what gives the cracks their
 * depth - they are not flat white lines.
 *
 * Only the y coordinates are scaled, by `arg_height / 200`: the cracks were
 * authored for a 200-tall screen and the windshield is shorter than that once
 * the dashboard is showing. x is left alone because the width is always 320 -
 * so at RFB_SCALE > 1 it is scaled here instead, while y needs no extra work
 * because the height it is divided into already arrives scaled.
 *
 * do_sinking is simpler: a band of skybox_wat_color rising from the bottom of
 * the windshield over four seconds, `(height * frames) / (framespersec * 4)`
 * tall.
 */
#include <stdint.h>
#include <string.h>

#include "externs.h"
#include "memmgr.h"
#include "rfbsize.h"

extern int16_t dialog_fnt_colour;
extern void far* gameresptr;
extern int16_t skybox_wat_color;
extern struct RECTANGLE cliprect_unk;
extern struct RECTANGLE rect_ingame_text;
extern void preRender_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                           uint16_t colour);
extern void sprite_set_1_size(uint16_t left, uint16_t right, uint16_t top,
                              uint16_t height);
extern void sprite_clear_1_color(uint8_t colour);

static int16_t ldw(const void far* p, int32_t off)
{
	const uint8_t far* b = (const uint8_t far*)p;
	return (int16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1] << 8));
}

struct RECTANGLE* init_crak(int16_t arg_framecount, int16_t arg_top,
                            int16_t arg_height)
{
	void far* crak;
	void far* cinf;
	int16_t stage, count, per_stage, si;

	rect_ingame_text = cliprect_unk;

	crak = locate_shape_alt((char far*)gameresptr, "crak");
	cinf = locate_shape_alt((char far*)gameresptr, "cinf");
	if (!crak || !cinf) return &rect_ingame_text;

	/* which of the four stages the shatter has reached */
	per_stage = (int16_t)(framespersec / 7);
	if (per_stage <= 0) per_stage = 1;
	stage = (int16_t)(arg_framecount / per_stage);
	if (stage >= ldw(cinf, 0)) stage = (int16_t)(ldw(cinf, 0) - 1);
	if (stage < 0) stage = 0;
	count = ldw(cinf, 2 + stage * 2);

	for (si = 0; si < count; si++) {
		int32_t rec = (int32_t)si * 8;
		int16_t x0 = ldw(crak, rec + 0);
		int16_t y0 = ldw(crak, rec + 2);
		int16_t x1 = ldw(crak, rec + 4);
		int16_t y1 = ldw(crak, rec + 6);

		y0 = (int16_t)(((int32_t)y0 * arg_height) / 0xC8);
		y1 = (int16_t)(((int32_t)y1 * arg_height) / 0xC8);
		x0 = (int16_t)(x0 * RFB_SCALE);
		x1 = (int16_t)(x1 * RFB_SCALE);

		/* black above, black below, the bright line between them */
		preRender_line((uint16_t)x0, (uint16_t)(y0 + arg_top - 1),
		               (uint16_t)x1, (uint16_t)(y1 + arg_top - 1), 0);
		preRender_line((uint16_t)x0, (uint16_t)(y0 + arg_top + 1),
		               (uint16_t)x1, (uint16_t)(y1 + arg_top + 1), 0);
		preRender_line((uint16_t)x0, (uint16_t)(y0 + arg_top),
		               (uint16_t)x1, (uint16_t)(y1 + arg_top),
		               (uint16_t)dialog_fnt_colour);

		if (rect_ingame_text.left > x0)  rect_ingame_text.left = x0;
		if (rect_ingame_text.left > x1)  rect_ingame_text.left = x1;
		if (rect_ingame_text.right < x0) rect_ingame_text.right = x0;
		if (rect_ingame_text.right < x1) rect_ingame_text.right = x1;
	}
	return &rect_ingame_text;
}

struct RECTANGLE* do_sinking(int16_t arg_framecount, int16_t arg_top,
                             int16_t arg_height)
{
	int16_t risen, bottom, top;

	if (arg_framecount > (int16_t)framespersec)
		arg_framecount = (int16_t)framespersec;

	/* four seconds from the surface to gone */
	risen = (int16_t)(((int32_t)arg_height * arg_framecount) /
	                  ((int32_t)framespersec * 4));

	bottom = (int16_t)(arg_top + arg_height);
	top = (int16_t)(bottom - risen);

	rect_ingame_text.left = 0;
	rect_ingame_text.right = RFB_VIEW_W;
	rect_ingame_text.top = top;
	rect_ingame_text.bottom = bottom;

	sprite_set_1_size(0, RFB_VIEW_W, (uint16_t)top, (uint16_t)bottom);
	sprite_clear_1_color((uint8_t)skybox_wat_color);
	return &rect_ingame_text;
}
