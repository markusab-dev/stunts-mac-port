/*
 * rblit.c - Native 320x200 8-bit framebuffer and span blitters.
 *
 * In the original game these routines live in the loadable video driver
 * (MCGA.COD): "draws multiple sequential variable length horizontal lines".
 * The renderer core (shape3d.c) hands them per-scanline left/right x arrays
 * produced by the polygon edge walker:
 *
 *   spritefunc(&spans[top_y], &spans[480 + top_y], top_y, count, color)
 *
 * fills `count` scanlines starting at `top_y`; scanline i covers
 * x in [leftxs[i] .. rightxs[i]] inclusive (empty when right < left,
 * which is how clipped-off rows are encoded: L=left2, R=left2-1).
 *
 * Patterned fills use the 16-bit pattern word word_4031E as a 4x4 dither
 * matrix: bit index (y&3)*4 + (x&3), MSB first.
 * [HYPOTHESIS] Bit order to be confirmed by pixel comparison against DOSBox.
 */
#include <stdint.h>
#include <string.h>
#include "shape2d.h"

#ifndef RFB_SCALE
#define RFB_SCALE 1          /* 1 = originalets 320x200; 2 och 4 renderar skarpare */
#endif
#define RFB_W (320 * RFB_SCALE)
#define RFB_H (200 * RFB_SCALE)

uint8_t rfb_pixels[RFB_W * RFB_H];

/* Render-target window descriptors (seg012 data in the original). */
struct SPRITE sprite1;
struct SPRITE sprite2;

/* Pattern state set by preRender_patterned / preRender_unk. */
uint16_t word_4031E;
uint16_t word_40320;

/* Current drawing function pointers (seg012 data in the original). */
void (*spritefunc)(int16_t*, int16_t*, uint16_t, uint16_t, uint16_t);
void (*imagefunc)(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);

void rblit_init(void)
{
	memset(rfb_pixels, 0, sizeof(rfb_pixels));
	memset(&sprite1, 0, sizeof(sprite1));
	memset(&sprite2, 0, sizeof(sprite2));
	/* Full-screen render target window. */
	sprite1.sprite_left = 0;
	sprite1.sprite_right = RFB_W;
	sprite1.sprite_top = 0;
	sprite1.sprite_height = RFB_H;
	sprite1.sprite_pitch = RFB_W;
	sprite1.sprite_left2 = 0;
	sprite1.sprite_width2 = RFB_W;
	sprite1.sprite_widthsum = RFB_W;
	sprite2 = sprite1;
}

static inline void put_span(int16_t y, int16_t xl, int16_t xr, uint8_t color)
{
	if (y < 0 || y >= RFB_H) return;
	if (xl < 0) xl = 0;
	if (xr >= RFB_W) xr = RFB_W - 1;
	if (xl > xr) return;
	memset(&rfb_pixels[(int32_t)y * RFB_W + xl], color, (size_t)(xr - xl + 1));
}

void draw_filled_lines(int16_t* leftxs, int16_t* rightxs, uint16_t start_y,
                       uint16_t count, uint16_t color)
{
	uint16_t i;
	for (i = 0; i < count; i++)
		put_span((int16_t)(start_y + i), leftxs[i], rightxs[i], (uint8_t)color);
}

void draw_patterned_lines(int16_t* leftxs, int16_t* rightxs, uint16_t start_y,
                          uint16_t count, uint16_t color)
{
	uint16_t i;
	for (i = 0; i < count; i++) {
		int16_t y = (int16_t)(start_y + i);
		int16_t xl = leftxs[i], xr = rightxs[i];
		int16_t x;
		if (y < 0 || y >= RFB_H) continue;
		if (xl < 0) xl = 0;
		if (xr >= RFB_W) xr = RFB_W - 1;
		for (x = xl; x <= xr; x++) {
			uint16_t bit = (uint16_t)(((y & 3) * 4) + (x & 3));
			if ((word_4031E >> (15 - bit)) & 1)
				rfb_pixels[(int32_t)y * RFB_W + x] = (uint8_t)color;
		}
	}
}

void draw_unknown_lines(int16_t* leftxs, int16_t* rightxs, uint16_t start_y,
                        uint16_t count, uint16_t color)
{
	/* preRender_unk guards this with fatal_error("untested") upstream;
	 * fall back to a plain fill if it is ever reached. */
	draw_filled_lines(leftxs, rightxs, start_y, count, color);
}

/* ------------------------------------------------------------------ */
/* putpixel_single_maybe - seg012.asm 17884-17929                      */
/*                                                                     */
/* One clipped pixel: polyinfo primitive type 5 (a particle), and the  */
/* degenerate case of preRender_sphere.  The clip test is the original */
/* one, against sprite1's window, and note that it uses sprite_left /  */
/* sprite_right where preRender_sphere uses sprite_left2 /             */
/* sprite_widthsum-1.                                                  */
/*                                                                     */
/* [DEVIATION] the original's store is                                 */
/*     es:[sprite_lineofs[y] + x] = al, es = sprite_bitmapptr's seg    */
/* This port has a single framebuffer and never fills sprite_lineofs   */
/* or sprite_bitmapptr, exactly as draw_filled_lines above does not,   */
/* so the address arithmetic becomes rfb_pixels[y*RFB_W + x].          */
/* ------------------------------------------------------------------ */
void putpixel_single_maybe(uint16_t arg_0x, uint16_t arg_2y, uint16_t arg_4col)
{
	int16_t ax;

	ax = (int16_t)arg_0x;
	if (ax < (int16_t)sprite1.sprite_left)    goto loc_35B4D;  /* jl */
	if (ax >= (int16_t)sprite1.sprite_right)  goto loc_35B4D;  /* jge */
	ax = (int16_t)arg_2y;
	if (ax < (int16_t)sprite1.sprite_top)     goto loc_35B4D;  /* jl */
	if (ax < (int16_t)sprite1.sprite_height)  goto loc_35B56;  /* jl */
loc_35B4D:
	return;
loc_35B56:
	rfb_pixels[(int32_t)(int16_t)arg_2y * RFB_W + (int16_t)arg_0x] =
		(uint8_t)arg_4col;
}

/* Straight 1px line used for degenerate polygons (imagefunc). */
void preRender_line(uint16_t ax0, uint16_t ay0, uint16_t ax1, uint16_t ay1,
                    uint16_t color)
{
	int16_t x0 = (int16_t)ax0, y0 = (int16_t)ay0;
	int16_t x1 = (int16_t)ax1, y1 = (int16_t)ay1;
	int16_t dx = (int16_t)(x1 - x0), sx = dx >= 0 ? 1 : -1;
	int16_t dy = (int16_t)(y1 - y0), sy = dy >= 0 ? 1 : -1;
	int16_t adx = (int16_t)(dx >= 0 ? dx : -dx);
	int16_t ady = (int16_t)(dy >= 0 ? dy : -dy);
	int16_t err, e2;

	err = (int16_t)((adx > ady ? adx : -ady) / 2);
	for (;;) {
		if ((uint16_t)x0 < RFB_W && (uint16_t)y0 < RFB_H)
			rfb_pixels[(int32_t)y0 * RFB_W + x0] = (uint8_t)color;
		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -adx) { err = (int16_t)(err - ady); x0 = (int16_t)(x0 + sx); }
		if (e2 < ady)  { err = (int16_t)(err + adx); y0 = (int16_t)(y0 + sy); }
	}
}
