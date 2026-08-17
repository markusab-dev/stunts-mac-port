/*
 * rexplode.c - explosions and the turn arrows.
 *
 * Ported from
 *   seg012.asm  shape_op_explosion (442), sprite_putimage_transparent (192)
 *   seg003.asm  load_sdgame2_shapes
 *
 * SDGAME2.PVS holds five shapes, named in one packed string as everything else
 * in this game is - "ex01ex02ex03leftrigh":
 *
 *   ex01 32x19  ex02 32x19  ex03 32x21   the three explosion frames
 *   left 24x15  righ 24x15               the turn arrows
 *
 * shape_op_explosion is a *scaled* transparent blit. Its first argument is a
 * 16.8 fixed-point scale, and the shape's s2d_unk1 / s2d_unk2 are an anchor
 * point - the centre of the blast - so the draw position is
 *
 *     x - (anchor_x * scale >> 8),  y - (anchor_y * scale >> 8)
 *
 * which keeps the anchor pinned to the projected 3D point while the picture
 * grows around it. seg003 walks the three frames in turn as the explosion
 * animates.
 *
 * sprite_putimage_transparent is the plain version of the same thing: no
 * scaling, the background left alone.
 *
 * The transparent colour is 255, and that is the original's own choice, not a
 * guess: sprite_putimage_transparent sets `mov ah, 0FFh` and compares every
 * source byte against it (seg012, just above its inner loop). An earlier note
 * here called this an oddity on the strength of a `lodsb; jz` in a *different*
 * blitter - the opaque sprite_putimage - which does not apply. It matches: the
 * shapes all carry 255 in their corners.
 *
 * The same loop also indexes a 256-byte table, `cs:incnums[]`, before the
 * comparison, so the original remaps every pixel as it blits. That table is
 * declared `extrn` in seg012.inc and defined nowhere restunts disassembled, so
 * its contents are unknown here; drawing the raw value gives the right picture,
 * which says it is the identity for everything these shapes use.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "rfbsize.h"
#include "shape2d.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;
extern void far* unflip_shape(void far* shape);

void far* sdgame2shapes[5];   /* ex01 ex02 ex03 left righ */
extern int16_t sdgame2_widths[3];   /* rdata.c */

#define TRANSPARENT 0xFF   /* seg012: mov ah, 0FFh */

static void far* s_sdgame2_res;

/* seg003 load_sdgame2_shapes (restunts.c:800, right beside the skybox). */
void load_sdgame2_shapes(void)
{
	int k;
	if (s_sdgame2_res) return;
	s_sdgame2_res = file_load_resource(3, "sdgame2.pvs");
	if (!s_sdgame2_res) return;
	locate_many_resources((char far*)s_sdgame2_res, "ex01ex02ex03leftrigh",
	                      (char far**)sdgame2shapes);
	/* like every other 2D asset in this game, any of these can be stored
	 * transposed - see unflip_shape */
	for (k = 0; k < 5; k++) unflip_shape(sdgame2shapes[k]);
	/* seg003 loc_1D908: frame.c scales the explosion so it covers the car's
	 * bounding box, and needs the three frames' widths to do it. */
	for (k = 0; k < 3; k++)
		sdgame2_widths[k] = sdgame2shapes[k]
			? (int16_t)((struct SHAPE2D far*)sdgame2shapes[k])->s2d_width : 1;
}

/* ------------------------------------------------------------------ */
/* The shared inner blit. The destination rectangle is given in         */
/* FRAMEBUFFER units, because the two callers do not agree on units:    */
/* the arrows pass positions in the original's 320x200 space, while the */
/* explosion is placed from a screen rectangle frame.c has already      */
/* computed, and its scale factor already encodes the framebuffer size  */
/* (frame.c derives it as (bbox_pixels << 8) / shape_width). Scaling    */
/* inside here would double-apply RFB_SCALE for one of them.            */
/* Colour 0 is left alone - `lodsb; jz skip; stosb`.                    */
/* ------------------------------------------------------------------ */
static void blit_scaled(void far* arg_shape, int16_t x, int16_t y,
                        int16_t dw, int16_t dh)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h, row, col;
	int16_t clip_l, clip_r, clip_t, clip_b;
	int16_t sx0, sy0, sx1, sy1;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	if (w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
	src = (const uint8_t far*)arg_shape + sizeof(struct SHAPE2D);

	clip_l = (int16_t)sprite1.sprite_left;
	clip_r = (int16_t)sprite1.sprite_right;
	clip_t = (int16_t)sprite1.sprite_top;
	clip_b = (int16_t)sprite1.sprite_height;
	if (clip_l < 0) clip_l = 0;
	if (clip_t < 0) clip_t = 0;
	if (clip_r > RFB_VIEW_W) clip_r = RFB_VIEW_W;
	if (clip_b > RFB_VIEW_H) clip_b = RFB_VIEW_H;

	sx0 = x > clip_l ? x : clip_l;
	sy0 = y > clip_t ? y : clip_t;
	sx1 = (int16_t)(x + dw) < clip_r ? (int16_t)(x + dw) : clip_r;
	sy1 = (int16_t)(y + dh) < clip_b ? (int16_t)(y + dh) : clip_b;
	if (sx0 >= sx1 || sy0 >= sy1) return;

	for (row = sy0; row < sy1; row++) {
		int16_t sy = (int16_t)(((int32_t)(row - y) * h) / dh);
		const uint8_t far* srow = src + (int32_t)sy * w;
		uint8_t* d = &rfb_pixels[(int32_t)row * RFB_VIEW_W];
		for (col = sx0; col < sx1; col++) {
			uint8_t v = srow[((int32_t)(col - x) * w) / dw];
			if (v != TRANSPARENT) d[col] = v;
		}
	}
}

/* seg012 sprite_putimage_transparent. Position and size are in the
 * original's 320x200 space, so both scale. */
void sprite_putimage_transparent(void far* arg_shape, int16_t x, int16_t y)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	if (!shape) return;
	blit_scaled(arg_shape, (int16_t)(x * RFB_SCALE), (int16_t)(y * RFB_SCALE),
	            (int16_t)((int16_t)shape->s2d_width * RFB_SCALE),
	            (int16_t)((int16_t)shape->s2d_height * RFB_SCALE));
}

/* seg012 shape_op_explosion. arg_scale is 16.8 fixed point and already in
 * framebuffer terms, and the shape's anchor stays pinned to (x, y) while the
 * picture grows around it. */
void shape_op_explosion(int16_t arg_scale, void far* arg_shape,
                        int16_t x, int16_t y)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	int16_t ax, ay, dw, dh;

	if (!shape || arg_scale <= 0) return;
	ax = (int16_t)(x - (((int32_t)(int16_t)shape->s2d_unk1 * arg_scale) >> 8));
	ay = (int16_t)(y - (((int32_t)(int16_t)shape->s2d_unk2 * arg_scale) >> 8));
	dw = (int16_t)(((int32_t)(int16_t)shape->s2d_width * arg_scale) >> 8);
	dh = (int16_t)(((int32_t)(int16_t)shape->s2d_height * arg_scale) >> 8);
	blit_scaled(arg_shape, ax, ay, dw, dh);
}
