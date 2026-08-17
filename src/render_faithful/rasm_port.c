/*
 * rasm_port.c - Instruction-exact C translations of the two remaining Watcom
 * inline-asm functions in the restunts renderer (shape3d.c):
 *
 *   1. rasm_draw_line_related_impl   (seg012 loc_2EB62..loc_2F30F)
 *      Clips one polygon edge against the sprite1 window using 16-bit
 *      Cohen-Sutherland-style outcodes and emits a 14-word "edge descriptor".
 *      Includes the 16-bit-overflow fallback (loc_2F253) that walks extreme
 *      off-screen endpoints toward the window in quarter-delta steps.
 *
 *   2. rasm_preRender_default_impl_helper   (seg012 loc_31B5C..loc_31F38)
 *      Merges the "second side" polygon edges into the per-scanline
 *      left/right span arrays produced by generate_poly_edges.
 *
 * Translation rules used throughout:
 *   - 16-bit registers are modelled as uint16_t/int16_t locals with the
 *     original register names in comments.
 *   - x86 carry emulation: for `add a, b` CF = (uint16_t)(a + b) < a.
 *   - jl/jg/jle/jge are signed compares (int16_t); jb/ja are unsigned.
 *   - Original label names are preserved as C labels for auditability.
 *
 * Edge descriptor layout (int16_t r[14], byte offsets in the original):
 *   r[0]  [si+00] x fraction, low word of 16.16
 *   r[1]  [si+02] x at top endpoint (integer part)
 *   r[2]  [si+04] second fraction (shallow modes)
 *   r[3]  [si+06] top y
 *   r[4]  [si+08] x at bottom endpoint
 *   r[5]  [si+0A] bottom y
 *   r[6]  [si+0C] slope step (unsigned 16-bit fraction or per-row delta)
 *   r[7]  [si+0E] count (rows, or x-steps for shallow modes)
 *   r[8]  [si+10] (unused)
 *   r[9]  [si+12] low byte: mode 0..9 (0xFF = unset); high byte: clip flags
 *   r[10] [si+14] rows clipped off above-start, left side
 *   r[11] [si+16] rows clipped off below-end,  left side
 *   r[12] [si+18] rows clipped off above-start, right side
 *   r[13] [si+1A] rows clipped off below-end,  right side
 *
 * Modes: 0/1 horizontal, 9 point, 2 vertical, 3/4 diagonal (x-- / x++),
 *        5/6 steep (16.16 x -= / += step per row),
 *        7/8 shallow (multiple x per row, row advances on fraction carry).
 */

#include <stdint.h>
#include "shape2d.h"
#include "rfbsize.h"

extern struct SPRITE sprite1;

/* ------------------------------------------------------------------ */
/* seg012 interpolation tables word_2F448 / off_2F44A                  */
/*                                                                     */
/* The original keeps 0x32 precomputed slope tables in the video       */
/* driver's code segment: table[cx][dx] for cx < 0x32.  The fallback   */
/* division path computes round((dx << 16) / cx) with round-half-down, */
/* and the tables are assumed to hold exactly those values.            */
/* [HYPOTHESIS] to be confirmed by pixel comparison against DOSBox.    */
/* ------------------------------------------------------------------ */
static uint16_t slope_round(uint16_t smaller, uint16_t larger)
{
	/* xor ax,ax ; div cx  -> ax = (smaller:0000) / larger          */
	/* mov bx,cx ; shr bx,1 ; sub bx,dx ; adc ax,0                   */
	uint32_t dividend = (uint32_t)smaller << 16;
	uint16_t q = (uint16_t)(dividend / larger);
	uint16_t rem = (uint16_t)(dividend % larger);
	uint16_t half = larger >> 1;
	if (half < rem) q++; /* borrow from sub sets carry */
	return q;
}

/* 16-bit carry helpers */
static inline int add16_carry(uint16_t a, uint16_t b)
{
	return (uint16_t)(a + b) < a;
}

uint16_t rasm_draw_line_related_impl(uint16_t arg_startX, uint16_t arg_startY,
                                     uint16_t arg_endX, uint16_t arg_endY,
                                     int16_t* arg_8, uint16_t var_4)
{
	int16_t* si = arg_8;
	int16_t sprite1_sprite_left2 = (int16_t)sprite1.sprite_left2;
	int16_t sprite1_sprite_widthsum = (int16_t)sprite1.sprite_widthsum;
	int16_t sprite1_sprite_top = (int16_t)sprite1.sprite_top;
	int16_t sprite1_sprite_height = (int16_t)sprite1.sprite_height;

	uint16_t var_2 = 0;
	int16_t ax, bx, cx, dx;
	uint8_t dl, dh, al;
	uint32_t t32;

	/* loc_2EB62: initialise descriptor */
	si[9] = 0x00FF;          /* mov word [si+12h], 0FFh (mode=FF, flags=0) */
	si[0] = 0;
	si[2] = 0;
	si[10] = 0;
	si[11] = 0;
	si[12] = 0;
	si[13] = 0;

	ax = (int16_t)arg_startY;
	bx = (int16_t)arg_endY;
	cx = (int16_t)arg_startX;
	dx = (int16_t)arg_endX;
	if (ax > bx) { /* jg loc_2EB9C */
		si[1] = dx;
		si[3] = bx;
		si[4] = cx;
		si[5] = ax;
	} else {
		si[1] = cx;
		si[3] = ax;
		si[4] = dx;
		si[5] = bx;
	}
	/* loc_2EBA8: jnz loc_2EBAD ; jmp loc_2F1A3 */
	if (ax == bx) goto loc_2F1A3;

loc_2EBAD:
	dl = 0; dh = 0;
	if (var_4 == 0) {
		/* y outcodes for start point */
		ax = si[3];
		if (ax >= sprite1_sprite_height) { dl = 8; goto loc_2EF9B; }
		if (ax < sprite1_sprite_top) dh |= 4;
		ax = si[5];
		if (ax < sprite1_sprite_top) { dl = 4; goto loc_2EF9B; }
		if (ax >= sprite1_sprite_height) dl |= 8;
		/* x outcodes */
		ax = si[1];
		if (ax < sprite1_sprite_left2) dh |= 2;
		if (ax >= sprite1_sprite_widthsum) dh |= 1;
		ax = si[4];
		if (ax < sprite1_sprite_left2) dl |= 2;
		if (ax >= sprite1_sprite_widthsum) dl |= 1;
		if (dl & dh) { dl &= dh; goto loc_2EF9B; }
	}
	/* loc_2EC1A */
	dl |= dh;
	var_2 = dl;

	/* cy = bottomY - topY, with signed-overflow check (jno) */
	{
		int32_t wide = (int32_t)si[5] - (int32_t)si[3];
		cx = (int16_t)wide;
		if (wide != (int32_t)cx) goto loc_2F253; /* jo -> overflow fallback */
	}
	{
		int32_t wide = (int32_t)si[4] - (int32_t)si[1];
		dx = (int16_t)wide;
		if (wide != (int32_t)dx) goto loc_2F253;
	}
	if (dx == 0) { /* vertical */
		cx++;
		si[7] = cx;
		si[9] = (int16_t)((si[9] & 0xFF00) | 2); /* mov byte [si+12h], 2 */
		goto loc_2ECAD;
	}
	if (dx >= 0) { /* loc_2EC41: jl loc_2EC8F when negative */
		if ((uint16_t)dx < (uint16_t)cx) { /* jb loc_2EC82 */
			si[9] = (int16_t)((si[9] & 0xFF00) | 6);
			goto loc_2EC4F;
		}
		if (dx == cx) { /* jz loc_2EC88 */
			si[9] = (int16_t)((si[9] & 0xFF00) | 4);
			goto loc_2ECA9;
		}
		si[9] = (int16_t)((si[9] & 0xFF00) | 8);
		/* loc_2EC4D: xchg cx, dx */
		{ int16_t t = cx; cx = dx; dx = t; }
		goto loc_2EC4F;
	}
	/* loc_2EC8F: negative dx */
	{
		int32_t wide = -(int32_t)dx;
		dx = (int16_t)wide;
		if (wide != (int32_t)dx) goto loc_2F253; /* neg overflow (dx was -32768) */
	}
	if ((uint16_t)dx < (uint16_t)cx) {
		si[9] = (int16_t)((si[9] & 0xFF00) | 5);
		goto loc_2EC4F;
	}
	if (dx == cx) {
		si[9] = (int16_t)((si[9] & 0xFF00) | 3);
		goto loc_2ECA9;
	}
	si[9] = (int16_t)((si[9] & 0xFF00) | 7);
	{ int16_t t = cx; cx = dx; dx = t; }

loc_2EC4F:
	/* slope = table/div: round((dx << 16) / cx) */
	si[6] = (int16_t)slope_round((uint16_t)dx, (uint16_t)cx);
	/* inc cx ; jo loc_2EC29 (overflow to -32768) */
	if (cx == 0x7FFF) goto loc_2F253;
	cx++;
	si[7] = cx;
	goto loc_2ECAD;

loc_2ECA9:
	cx++;
	si[7] = cx;

loc_2ECAD:
	bx = (int16_t)var_2;
	switch ((uint16_t)bx) {
		case 0: goto loc_2ECD9;
		case 1: goto loc_2F01F;
		case 2: case 3: goto loc_2EE78;
		case 4: case 5: case 6: case 7: goto loc_2ECE1;
		case 8: case 9: case 10: case 11: goto loc_2EDA5;
		case 12: case 13: case 14: case 15: goto loc_2ECE1;
		default: goto loc_2ECD9;
	}

loc_2ECD9:
	return 0;

loc_2ECE1:
	/* clip against top edge */
	ax = si[3];
	cx = sprite1_sprite_top;
	si[3] = cx;
	cx = (int16_t)(cx - ax);
	switch (si[9] & 0xFF) {
		case 0: case 1: break;
		case 2: goto loc_2ED0A;
		case 3: goto loc_2ED10;
		case 4: goto loc_2ED19;
		case 5: goto loc_2ED22;
		case 6: goto loc_2ED32;
		case 7: goto loc_2ED42;
		case 8: goto loc_2ED7E;
		default: break;
	}
	goto loc_2F13E;

loc_2ED0A:
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F13E;
loc_2ED10:
	si[1] = (int16_t)(si[1] - cx);
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F13E;
loc_2ED19:
	si[1] = (int16_t)(si[1] + cx);
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F13E;
loc_2ED22:
	/* mul: dx:ax = step * cx ; sub [si],ax ; sbb [si+2],dx */
	t32 = (uint32_t)(uint16_t)si[6] * (uint16_t)cx;
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t oldlo = (uint16_t)si[0];
		si[0] = (int16_t)(oldlo - lo);
		si[1] = (int16_t)((uint16_t)si[1] - hi - (oldlo < lo ? 1 : 0));
	}
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F13E;
loc_2ED32:
	t32 = (uint32_t)(uint16_t)si[6] * (uint16_t)cx;
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t oldlo = (uint16_t)si[0];
		si[0] = (int16_t)(oldlo + lo);
		si[1] = (int16_t)((uint16_t)si[1] + hi + (add16_carry(oldlo, lo) ? 1 : 0));
	}
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F13E;
loc_2ED42:
	/* shallow negative: si[3] currently top; ax = old top y */
	si[3] = ax;                      /* mov [si+6], ax (restore) */
	{
		uint16_t divlo, divrem, step = (uint16_t)si[6];
		uint32_t dividend = ((uint32_t)(uint16_t)cx << 16); /* dx:ax = cx:0 */
		divlo = (uint16_t)(dividend / step);
		divrem = (uint16_t)(dividend % step);
		{
			uint16_t half = step >> 1;
			if (half < divrem) divlo++;
		}
		si[1] = (int16_t)(si[1] - (int16_t)divlo);
		si[7] = (int16_t)(si[7] - (int16_t)divlo);
		if (si[7] <= 0) goto loc_2ED69;
		t32 = (uint32_t)divlo * step;
		{
			uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
			uint16_t oldlo = (uint16_t)si[2];
			si[2] = (int16_t)(oldlo + lo);
			si[3] = (int16_t)((uint16_t)si[3] + hi + (add16_carry(oldlo, lo) ? 1 : 0));
		}
	}
	goto loc_2F13E;
loc_2ED69:
	si[7] = 1;
	si[3] = sprite1_sprite_top;
	si[1] = si[4];
	goto loc_2F13E;
loc_2ED7E:
	si[3] = ax;
	{
		uint16_t divlo, divrem, step = (uint16_t)si[6];
		uint32_t dividend = ((uint32_t)(uint16_t)cx << 16);
		divlo = (uint16_t)(dividend / step);
		divrem = (uint16_t)(dividend % step);
		{
			uint16_t half = step >> 1;
			if (half < divrem) divlo++;
		}
		si[1] = (int16_t)(si[1] + (int16_t)divlo);
		si[7] = (int16_t)(si[7] - (int16_t)divlo);
		if (si[7] <= 0) goto loc_2ED69;
		t32 = (uint32_t)divlo * step;
		{
			uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
			uint16_t oldlo = (uint16_t)si[2];
			si[2] = (int16_t)(oldlo + lo);
			si[3] = (int16_t)((uint16_t)si[3] + hi + (add16_carry(oldlo, lo) ? 1 : 0));
		}
	}
	goto loc_2F13E;

loc_2EDA5:
	/* clip against bottom edge */
	cx = si[5];
	dx = (int16_t)(sprite1_sprite_height - 1);
	si[5] = dx;
	cx = (int16_t)(cx - dx);
	switch (si[9] & 0xFF) {
		case 0: case 1: break;
		case 2: goto loc_2EDCF;
		case 3: goto loc_2EDD5;
		case 4: goto loc_2EDDE;
		case 5: goto loc_2EDE7;
		case 6: goto loc_2EE0B;
		case 7: goto loc_2EE2A;
		case 8: goto loc_2EE53;
		default: break;
	}
	goto loc_2F148;

loc_2EDCF:
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F148;
loc_2EDD5:
	si[4] = (int16_t)(si[4] + cx);
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F148;
loc_2EDDE:
	si[4] = (int16_t)(si[4] - cx);
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F148;
loc_2EDE7:
	/* steep negative, clip bottom: recompute end x from top x and count */
	dx = (int16_t)(si[7] - cx);
	si[7] = dx;
	dx--;
	t32 = (uint32_t)(uint16_t)si[6] * (uint16_t)dx;
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t blo = (uint16_t)si[0], bhi = (uint16_t)si[1];
		uint16_t rlo = (uint16_t)(blo - lo);
		uint16_t rhi = (uint16_t)(bhi - hi - (blo < lo ? 1 : 0));
		/* add bx,8000h ; adc cx,0 */
		if (add16_carry(rlo, 0x8000)) rhi++;
		si[4] = (int16_t)rhi;
	}
	goto loc_2F148;
loc_2EE0B:
	dx = (int16_t)(si[7] - cx);
	si[7] = dx;
	dx--;
	t32 = (uint32_t)(uint16_t)si[6] * (uint16_t)dx;
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t blo = (uint16_t)si[0], bhi = (uint16_t)si[1];
		uint16_t rlo = (uint16_t)(lo + blo);
		uint16_t rhi = (uint16_t)(hi + bhi + (add16_carry(lo, blo) ? 1 : 0));
		if (add16_carry(rlo, 0x8000)) rhi++;
		si[4] = (int16_t)rhi;
	}
	goto loc_2F148;
loc_2EE2A:
	/* shallow negative, clip bottom.
	 * At entry dx = height-1 (set in loc_2EDA5).
	 * xor ax,ax ; sub ax,[si+4] ; sbb dx,[si+6] -> ((height-1)<<16) - yfrac32 */
	{
		uint32_t yfrac = ((uint32_t)(uint16_t)si[3] << 16) | (uint16_t)si[2];
		int32_t diff = (((int32_t)(sprite1_sprite_height - 1)) << 16) - (int32_t)yfrac;
		if (diff < 0) { ax = 0; goto loc_2EE40; }
		{
			uint16_t step = (uint16_t)si[6];
			uint32_t dividend = (uint32_t)diff;
			uint16_t q = (uint16_t)(dividend / step);
			uint16_t rem = (uint16_t)(dividend % step);
			uint16_t half = step >> 1;
			if (half < rem) q++;
			ax = (int16_t)q;
		}
	}
loc_2EE40:
	dx = (int16_t)(si[1] - ax);
	si[4] = dx;
	ax++;
	si[7] = ax;
	goto loc_2F148;
loc_2EE53:
	{
		uint32_t yfrac = ((uint32_t)(uint16_t)si[3] << 16) | (uint16_t)si[2];
		int32_t diff = (((int32_t)(sprite1_sprite_height - 1)) << 16) - (int32_t)yfrac;
		if (diff < 0) { ax = 0; }
		else {
			uint16_t step = (uint16_t)si[6];
			uint32_t dividend = (uint32_t)diff;
			uint16_t q = (uint16_t)(dividend / step);
			uint16_t rem = (uint16_t)(dividend % step);
			uint16_t half = step >> 1;
			if (half < rem) q++;
			ax = (int16_t)q;
		}
	}
	dx = (int16_t)(si[1] + ax);
	si[4] = dx;
	ax++;
	si[7] = ax;
	goto loc_2F148;

loc_2EE78:
	/* clip against left edge (start-x side) */
	switch (si[9] & 0xFF) {
		case 0: case 1: goto loc_2ECD9;
		case 2: goto loc_2EF98;
		case 3: goto loc_2EE94;
		case 4: goto loc_2EEAD;
		case 5: goto loc_2EEC6;
		case 6: goto loc_2EEFE;
		case 7: goto loc_2EF31;
		case 8: goto loc_2EF61;
		default: goto loc_2ECD9;
	}

loc_2EE94:
	cx = sprite1_sprite_left2;
	ax = si[4];
	si[4] = cx;
	cx = (int16_t)(cx - ax);
	si[11] = (int16_t)(si[11] + cx);
	si[7] = (int16_t)(si[7] - cx);
	si[5] = (int16_t)(si[5] - cx);
	goto loc_2F196;
loc_2EEAD:
	ax = si[1];
	cx = sprite1_sprite_left2;
	si[1] = cx;
	cx = (int16_t)(cx - ax);
	si[10] = (int16_t)(si[10] + cx);
	si[3] = (int16_t)(si[3] + cx);
	si[7] = (int16_t)(si[7] - cx);
	goto loc_2F196;
loc_2EEC6:
	/* steep negative crossing left edge */
	{
		uint16_t alo = (uint16_t)si[0];
		int16_t ahi = si[1];
		cx = sprite1_sprite_left2;
		si[4] = cx;
		dx = (int16_t)(ahi - cx);
		if (dx < 0) { ax = 1; goto loc_2EEE4; }
		{
			uint16_t step = (uint16_t)si[6];
			uint32_t dividend = ((uint32_t)(uint16_t)dx << 16) | alo;
			uint16_t q = (uint16_t)(dividend / step);
			uint16_t rem = (uint16_t)(dividend % step);
			uint16_t half = step >> 1;
			if (half < rem) q++;
			ax = (int16_t)(q + 1);
		}
	}
loc_2EEE4:
	cx = si[7];
	si[7] = ax;
	cx = (int16_t)(cx - ax);
	si[11] = (int16_t)(si[11] + cx);
	ax--;
	ax = (int16_t)(ax + si[3]);
	si[5] = ax;
	goto loc_2F196;
loc_2EEFE:
	/* steep positive entering from left */
	{
		uint32_t frac = ((uint32_t)(uint16_t)si[1] << 16) | (uint16_t)si[0];
		int32_t diff = ((int32_t)sprite1_sprite_left2 << 16) - (int32_t)frac;
		if (diff < 0) goto loc_2EF98;
		{
			uint16_t step = (uint16_t)si[6];
			uint32_t dividend = (uint32_t)diff;
			uint16_t q = (uint16_t)(dividend / step);
			uint16_t rem = (uint16_t)(dividend % step);
			uint16_t half = step >> 1;
			if (half < rem) q++;
			ax = (int16_t)q;
		}
		si[3] = (int16_t)(si[3] + ax);
		si[10] = (int16_t)(si[10] + ax);
		si[7] = (int16_t)(si[7] - ax);
		if (si[7] <= 0) goto loc_2EF98;
		t32 = (uint32_t)(uint16_t)ax * (uint16_t)si[6];
		{
			uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
			uint16_t oldlo = (uint16_t)si[0];
			si[0] = (int16_t)(oldlo + lo);
			si[1] = (int16_t)((uint16_t)si[1] + hi + (add16_carry(oldlo, lo) ? 1 : 0));
		}
	}
	goto loc_2F196;
loc_2EF31:
	/* shallow negative crossing left edge */
	ax = si[1];
	dx = sprite1_sprite_left2;
	si[4] = dx;
	ax = (int16_t)(ax - dx);
	cx = (int16_t)(ax + 1);
	si[7] = cx;
	t32 = (uint32_t)(uint16_t)ax * (uint16_t)si[6];
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t alo = (uint16_t)(lo + (uint16_t)si[2]);
		uint16_t ahi = (uint16_t)(hi + (uint16_t)si[3] + (add16_carry(lo, (uint16_t)si[2]) ? 1 : 0));
		if (add16_carry(alo, 0x8000)) ahi++;
		ax = si[5];
		ax = (int16_t)(ax - (int16_t)ahi);
		si[5] = (int16_t)ahi;
		si[11] = (int16_t)(si[11] + ax);
	}
	goto loc_2F196;
loc_2EF61:
	/* shallow positive entering from left */
	cx = si[1];
	ax = sprite1_sprite_left2;
	si[1] = ax;
	ax = (int16_t)(ax - cx);
	si[7] = (int16_t)(si[7] - ax);
	t32 = (uint32_t)(uint16_t)ax * (uint16_t)si[6];
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t blo = (uint16_t)si[2], bhi = (uint16_t)si[3];
		uint16_t nlo = (uint16_t)(lo + blo);
		uint16_t nhi = (uint16_t)(hi + bhi + (add16_carry(lo, blo) ? 1 : 0));
		uint16_t rb_hi = bhi;
		uint16_t rn_hi = nhi;
		si[2] = (int16_t)nlo;
		si[3] = (int16_t)nhi;
		if (add16_carry(blo, 0x8000)) rb_hi++;
		if (add16_carry(nlo, 0x8000)) rn_hi++;
		dx = (int16_t)(rn_hi - rb_hi);
		si[10] = (int16_t)(si[10] + dx);
	}
	goto loc_2F196;

loc_2EF98:
	dl = 2;
loc_2EF9B:
	/* fully rejected: record clip rows, count = 0 */
	si[9] = (int16_t)((si[9] & 0x00FF) | ((int16_t)dl << 8)); /* [si+13h] = dl */
	si[7] = 0;
	al = (uint8_t)(si[9] >> 8);
	if (al & 4) {
		bx = sprite1_sprite_top;
		si[3] = bx;
		si[2] = 0;
		bx--;
		si[5] = bx;
		goto loc_2F017;
	}
	if (al & 8) {
		bx = sprite1_sprite_height;
		si[3] = bx;
		si[2] = 0;
		goto loc_2F017;
	}
	cx = si[5];
	if (cx >= sprite1_sprite_height) cx = (int16_t)(sprite1_sprite_height - 1);
	dx = si[3];
	if (add16_carry((uint16_t)si[2], 0x8000)) dx++;
	if (dx < sprite1_sprite_top) dx = sprite1_sprite_top;
	si[3] = dx;
	si[2] = 0;
	cx = (int16_t)(cx - dx);
	dx--;
	cx++;
	si[5] = dx;
	if (al & 2) si[11] = (int16_t)(si[11] + cx);
	else       si[13] = (int16_t)(si[13] + cx);
loc_2F017:
	return (uint16_t)(uint8_t)(si[9] >> 8); /* xor ah,ah ; al = flags */

loc_2F01F:
	/* clip against right edge */
	switch (si[9] & 0xFF) {
		case 0: case 1: goto loc_2ECD9;
		case 2: goto loc_2F03D;
		case 3: goto loc_2F043;
		case 4: goto loc_2F05C;
		case 5: goto loc_2F076;
		case 6: goto loc_2F0A3;
		case 7: goto loc_2F0D7;
		case 8: goto loc_2F110;
		default: goto loc_2ECD9;
	}

loc_2F03D:
	dl = 1;
	goto loc_2EF9B;
loc_2F043:
	cx = si[1];
	ax = (int16_t)(sprite1_sprite_widthsum - 1);
	si[1] = ax;
	cx = (int16_t)(cx - ax);
	si[3] = (int16_t)(si[3] + cx);
	si[7] = (int16_t)(si[7] - cx);
	si[12] = (int16_t)(si[12] + cx);
	goto loc_2ECD9;
loc_2F05C:
	cx = (int16_t)(sprite1_sprite_widthsum - 1);
	ax = si[4];
	si[4] = cx;
	ax = (int16_t)(ax - cx);
	si[13] = (int16_t)(si[13] + ax);
	si[7] = (int16_t)(si[7] - ax);
	si[5] = (int16_t)(si[5] - ax);
	goto loc_2ECD9;
loc_2F076:
	/* steep negative entering from right */
	{
		uint16_t alo = (uint16_t)si[0];
		dx = (int16_t)(si[1] - sprite1_sprite_widthsum);
		dx++;
		{
			uint16_t step = (uint16_t)si[6];
			uint32_t dividend = ((uint32_t)(uint16_t)dx << 16) | alo;
			uint16_t q = (uint16_t)(dividend / step);
			uint16_t rem = (uint16_t)(dividend % step);
			uint16_t half = step >> 1;
			if (half < rem) q++;
			ax = (int16_t)q;
		}
		si[7] = (int16_t)(si[7] - ax);
		if (si[7] <= 0) goto loc_2F03D;
		si[3] = (int16_t)(si[3] + ax);
		si[12] = (int16_t)(si[12] + ax);
		t32 = (uint32_t)(uint16_t)ax * (uint16_t)si[6];
		{
			uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
			uint16_t oldlo = (uint16_t)si[0];
			si[0] = (int16_t)(oldlo - lo);
			si[1] = (int16_t)((uint16_t)si[1] - hi - (oldlo < lo ? 1 : 0));
		}
	}
	goto loc_2ECD9;
loc_2F0A3:
	/* steep positive crossing right edge.
	 * mov dx,widthsum ; dec dx ; mov [si+8],dx ;
	 * xor ax,ax ; sub ax,[si] ; sbb dx,[si+2] -> ((widthsum-1)<<16) - xfrac32 */
	{
		dx = (int16_t)(sprite1_sprite_widthsum - 1);
		si[4] = dx;
		uint32_t frac = ((uint32_t)(uint16_t)si[1] << 16) | (uint16_t)si[0];
		int32_t diff = (((int32_t)(sprite1_sprite_widthsum - 1)) << 16) - (int32_t)frac;
		if (diff < 0) goto loc_2F03D;
		{
			uint16_t step = (uint16_t)si[6];
			uint32_t dividend = (uint32_t)diff;
			uint16_t q = (uint16_t)(dividend / step);
			uint16_t rem = (uint16_t)(dividend % step);
			uint16_t half = step >> 1;
			if (half < rem) q++;
			ax = (int16_t)(q + 1);
		}
		cx = si[7];
		si[7] = ax;
		cx = (int16_t)(cx - ax);
		si[13] = (int16_t)(si[13] + cx);
		ax--;
		ax = (int16_t)(ax + si[3]);
		si[5] = ax;
	}
	goto loc_2ECD9;
loc_2F0D7:
	/* shallow negative entering from right */
	ax = si[1];
	cx = (int16_t)(sprite1_sprite_widthsum - 1);
	ax = (int16_t)(ax - cx);
	si[1] = cx;
	si[7] = (int16_t)(si[7] - ax);
	t32 = (uint32_t)(uint16_t)ax * (uint16_t)si[6];
	{
		uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
		uint16_t blo = (uint16_t)si[2], bhi = (uint16_t)si[3];
		uint16_t nlo = (uint16_t)(lo + blo);
		uint16_t nhi = (uint16_t)(hi + bhi + (add16_carry(lo, blo) ? 1 : 0));
		uint16_t rb_hi = bhi;
		uint16_t rn_hi = nhi;
		si[2] = (int16_t)nlo;
		si[3] = (int16_t)nhi;
		if (add16_carry(blo, 0x8000)) rb_hi++;
		if (add16_carry(nlo, 0x8000)) rn_hi++;
		dx = (int16_t)(rn_hi - rb_hi);
		si[12] = (int16_t)(si[12] + dx);
	}
	goto loc_2ECD9;
loc_2F110:
	/* shallow positive crossing right edge */
	{
		ax = sprite1_sprite_widthsum;
		cx = (int16_t)(ax - 1);
		si[4] = cx;
		ax = (int16_t)(ax - si[1]);
		si[7] = ax;
		ax--;
		t32 = (uint32_t)(uint16_t)ax * (uint16_t)si[6];
		{
			uint16_t lo = (uint16_t)t32, hi = (uint16_t)(t32 >> 16);
			uint16_t nlo = (uint16_t)(lo + (uint16_t)si[2]);
			uint16_t nhi = (uint16_t)(hi + (uint16_t)si[3] + (add16_carry(lo, (uint16_t)si[2]) ? 1 : 0));
			if (add16_carry(nlo, 0x8000)) nhi++;
			ax = si[5];
			ax = (int16_t)(ax - (int16_t)nhi);
			si[5] = (int16_t)nhi;
			si[13] = (int16_t)(si[13] + ax);
		}
	}
	goto loc_2ECD9;

loc_2F13E:
	if (var_2 & 8) goto loc_2EDA5;
	/* fallthrough */
loc_2F148:
	/* recompute x outcodes of clipped endpoints */
	dl = 0; dh = 0;
	ax = si[1];
	if (ax < sprite1_sprite_left2) dh |= 2;
	{
		uint16_t blo = (uint16_t)si[0];
		int16_t ahi = ax;
		if (add16_carry(blo, 0x8000)) ahi++;
		if (ahi >= sprite1_sprite_widthsum) dh |= 1;
	}
	ax = si[4];
	if (ax < sprite1_sprite_left2) dl |= 2;
	if (ax >= sprite1_sprite_widthsum) dl |= 1;
	if (dl & dh) { dl &= dh; goto loc_2EF9B; }
	dl |= dh;
	if (dl == 0) goto loc_2ECD9;
	var_2 = dl;
	goto loc_2ECAD;

loc_2F196:
	if (var_2 & 1) goto loc_2F01F;
	goto loc_2ECD9;

loc_2F1A3:
	/* horizontal line (startY == endY); ax = startY, cx = startX, dx = endX */
	si[9] = (int16_t)((si[9] & 0xFF00) | 1);
	if (cx == dx) si[9] = (int16_t)((si[9] & 0xFF00) | 9);
	if (cx > dx) {
		si[9] = (int16_t)(si[9] & 0xFF00); /* mode 0 */
		si[1] = dx;
		si[4] = cx;
		{ int16_t t = cx; cx = dx; dx = t; }
	}
	if (var_4 != 0) {
		dx = (int16_t)(dx - cx);
		dx++;
		si[7] = dx;
		goto loc_2ECD9;
	}
	bx = sprite1_sprite_top;
	if (ax < bx) {
		al = 4;
		si[3] = bx;
		si[5] = bx;
		goto loc_2F1D4;
	}
	bx = sprite1_sprite_height;
	if (ax >= bx) {
		al = 8;
		si[3] = bx;
		si[5] = bx;
		goto loc_2F1D4;
	}
	ax = (int16_t)(dx - cx);
	ax++;
	si[7] = ax;
	if (dx < sprite1_sprite_left2) {
		si[5] = (int16_t)(si[5] - 1);
		si[11] = 1;
		al = 2;
		goto loc_2F1D4;
	}
	if (cx >= sprite1_sprite_widthsum) {
		si[5] = (int16_t)(si[5] - 1);
		si[13] = 1;
		al = 1;
		goto loc_2F1D4;
	}
	ax = sprite1_sprite_left2;
	bx = ax;
	ax = (int16_t)(ax - cx);
	if (ax > 0) {
		si[1] = bx;
		si[7] = (int16_t)(si[7] - ax);
	}
	ax = dx;
	bx = (int16_t)(sprite1_sprite_widthsum - 1);
	ax = (int16_t)(ax - bx);
	if (ax > 0) {
		si[7] = (int16_t)(si[7] - ax);
		si[4] = bx;
	}
	goto loc_2ECD9;

loc_2F1D4:
	si[9] = (int16_t)((si[9] & 0x00FF) | ((int16_t)al << 8));
	si[7] = 0;
	return (uint16_t)al;

loc_2F253:
	/* 16-bit overflow fallback: walk extreme endpoints toward the window in
	 * quarter-delta steps, accumulating clipped row counts. */
	cx = (int16_t)(si[5] >> 1);
	ax = (int16_t)(si[3] >> 1);
	cx = (int16_t)(cx - ax);
	cx = (int16_t)(cx >> 1);
	dx = (int16_t)(si[4] >> 1);
	ax = (int16_t)(si[1] >> 1);
	dx = (int16_t)(dx - ax);
	dx = (int16_t)(dx >> 1);
loc_2F26F:
	if (si[3] > (int16_t)0xC180) goto loc_2F2B0; /* > -16000 */
loc_2F276:
	{
		ax = (int16_t)(si[3] + cx);
		si[3] = ax;
		bx = ax;
		ax = (int16_t)(ax - sprite1_sprite_top);
		if (ax > 0) {
			if (ax > cx) ax = cx;
			bx = (int16_t)(bx - sprite1_sprite_height);
			if (bx > 0) {
				ax = (int16_t)(ax - bx);
				if (ax <= 0) goto loc_2F2AB;
			}
			bx = si[1];
			if (bx < sprite1_sprite_left2) si[10] = (int16_t)(si[10] + ax);
			else                           si[12] = (int16_t)(si[12] + ax);
		}
	}
loc_2F2AB:
	si[1] = (int16_t)(si[1] + dx);
	goto loc_2F26F;
loc_2F2B0:
	if (si[5] < (int16_t)0x3E80) { /* < 16000 */
		if (si[1] <= (int16_t)0xC180) goto loc_2F276;
		if (si[1] >= (int16_t)0x3E80) goto loc_2F276;
		if (si[4] <= (int16_t)0xC180) goto loc_2F2D6;
		if (si[4] >= (int16_t)0x3E80) goto loc_2F2D6;
		goto loc_2EBAD; /* endpoints now sane: restart normal path */
	}
loc_2F2D6:
	{
		ax = (int16_t)(si[5] - cx);
		si[5] = ax;
		bx = ax;
		ax = (int16_t)(ax - sprite1_sprite_height);
		ax++;
		if (ax < 0) {
			ax = (int16_t)(-ax);
			if (ax > cx) ax = cx;
			bx = (int16_t)(bx - sprite1_sprite_top);
			bx++;
			if (bx < 0) {
				ax = (int16_t)(ax + bx);
				if (ax <= 0) goto loc_2F30F;
			}
			bx = si[4];
			if (bx < sprite1_sprite_left2) si[11] = (int16_t)(si[11] + ax);
			else                           si[13] = (int16_t)(si[13] + ax);
		}
	}
loc_2F30F:
	si[4] = (int16_t)(si[4] - dx);
	goto loc_2F2B0;
}

/* ------------------------------------------------------------------ */
/* preRender_default_impl_helper: merge second-side edges into the     */
/* left/right span arrays.                                             */
/*   var_18[0..479]   = left x per scanline  ("L", asm [di])           */
/*   var_18[RFB_SPANROWS..959] = right x per scanline ("R", asm [di+3C0h])      */
/* var_A: winding selector. var_C: nonzero when the poly was clipped.  */
/* ------------------------------------------------------------------ */
void rasm_preRender_default_impl_helper(int16_t* regsi, uint16_t var_A,
                                        uint16_t var_C, int16_t* var_18)
{
	const int16_t* r = regsi;
	int16_t sprite1_sprite_left2 = (int16_t)sprite1.sprite_left2;
	int16_t sprite1_sprite_widthsum = (int16_t)sprite1.sprite_widthsum;
	int16_t ax, bx, cx, dx;
	int16_t* di; /* points into the L array; R is di[RFB_SPANROWS] */

	cx = r[7];
	if (cx == 0) goto loc_31EB9; /* or cx,cx ; ja -> jnz */

	di = &var_18[r[3]];
	bx = (int16_t)(r[9] & 0xFF);

	if (var_A != 0) goto loc_31B7F;

	/* var_A == 0 dispatch */
	switch ((uint16_t)bx) {
		case 2: goto loc_31D0B;
		case 3: goto loc_31D31;
		case 4: goto loc_31D5C;
		case 5: goto loc_31D87;
		case 6: goto loc_31DCA;
		case 7: goto loc_31E0D;
		case 8: goto loc_31E61;
		default: return; /* modes 0,1,9,FF: retn */
	}

loc_31B7F:
	switch ((uint16_t)bx) {
		case 2: goto loc_31B98;
		case 3: goto loc_31BB7;
		case 4: goto loc_31BDB;
		case 5: goto loc_31BFF;
		case 6: goto loc_31C37;
		case 7: goto loc_31C6F;
		case 8: goto loc_31CB1;
		default: return;
	}

	/* ---- var_A != 0 family: prefer storing into R ---- */
loc_31B98:
	ax = r[1];
	do {
		if (di[RFB_SPANROWS] < ax) { di[RFB_SPANROWS] = ax; di++; }
		else if (di[0] <= ax) { di++; }
		else { *di++ = ax; }
	} while (--cx);
	goto loc_31EB9;

loc_31BB7:
	cx = r[7];
	ax = r[1];
	do {
		if (di[RFB_SPANROWS] < ax) { di[RFB_SPANROWS] = ax; di++; ax--; }
		else if (di[0] <= ax) { di++; ax--; }
		else { *di++ = ax; ax--; }
	} while (--cx);
	goto loc_31EB9;

loc_31BDB:
	cx = r[7];
	ax = r[1];
	do {
		if (di[RFB_SPANROWS] < ax) { di[RFB_SPANROWS] = ax; di++; ax++; }
		else if (di[0] <= ax) { di++; ax++; }
		else { *di++ = ax; ax++; }
	} while (--cx);
	goto loc_31EB9;

loc_31BFF:
	cx = r[7];
	ax = r[1];
	dx = (int16_t)((uint16_t)r[0] + 0x8000);
	if (add16_carry((uint16_t)r[0], 0x8000)) ax++;
	bx = r[6];
	do {
		int16_t store = ax;
		if (di[RFB_SPANROWS] < store) { di[RFB_SPANROWS] = store; di++; }
		else if (di[0] <= store) { di++; }
		else { *di++ = store; }
		/* sub dx,bx ; sbb ax,0 */
		{
			uint16_t od = (uint16_t)dx;
			dx = (int16_t)(od - (uint16_t)bx);
			if (od < (uint16_t)bx) ax--;
		}
	} while (--cx);
	goto loc_31EB9;

loc_31C37:
	cx = r[7];
	ax = r[1];
	dx = (int16_t)((uint16_t)r[0] + 0x8000);
	if (add16_carry((uint16_t)r[0], 0x8000)) ax++;
	bx = r[6];
	do {
		int16_t store = ax;
		if (di[RFB_SPANROWS] < store) { di[RFB_SPANROWS] = store; di++; }
		else if (di[0] <= store) { di++; }
		else { *di++ = store; }
		if (add16_carry((uint16_t)dx, (uint16_t)bx)) ax++;
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
	} while (--cx);
	goto loc_31EB9;

loc_31C6F:
	/* shallow negative, R-preferring */
	cx = r[7];
	ax = r[1];
	bx = r[6];
	dx = (int16_t)((uint16_t)r[2] + 0x8000);
	if (add16_carry((uint16_t)r[2], 0x8000)) di++;
loc_31C84:
	if (di[RFB_SPANROWS] < ax) di[RFB_SPANROWS] = ax;
loc_31C8E:
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (carry) {
			if (di[0] > ax) di[0] = ax;
			di++;
			ax--;
			if (--cx) goto loc_31C84;
			di--;
			goto loc_31EB9;
		}
		ax--;
		if (--cx) goto loc_31C8E;
		ax++;
		if (di[0] > ax) di[0] = ax;
		goto loc_31EB9;
	}

loc_31CB1:
	/* shallow positive, R-preferring */
	cx = r[7];
	ax = r[1];
	bx = r[6];
	dx = (int16_t)((uint16_t)r[2] + 0x8000);
	if (add16_carry((uint16_t)r[2], 0x8000)) di++;
loc_31CC6:
	if (di[0] > ax) di[0] = ax;
loc_31CCC:
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (carry) {
			if (di[RFB_SPANROWS] < ax) di[RFB_SPANROWS] = ax;
			di++;
			ax++;
			if (--cx) goto loc_31CC6;
			di--;
			goto loc_31EB9;
		}
		/* loc_31CE6: x-advance without row-advance repeats only the
		 * fraction step (loop loc_31CCC), skipping the L-store retest */
		ax++;
		if (--cx) goto loc_31CCC;
		ax--;
		if (di[RFB_SPANROWS] < ax) di[RFB_SPANROWS] = ax;
		goto loc_31EB9;
	}

	/* ---- var_A == 0 family: sticky fill after threshold ---- */
loc_31D0B:
	cx = r[7];
	ax = r[1];
loc_31D11:
	if (di[0] > ax) { /* jg loc_31D23 */
		while (cx--) *di++ = ax; /* rep stosw */
		goto loc_31EB9;
	}
	if (di[RFB_SPANROWS] < ax) { /* jl loc_31D28 */
		di += 480;
		while (cx--) *di++ = ax;
		goto loc_31EB9;
	}
	di++;
	if (--cx) goto loc_31D11;
	goto loc_31EB9;

loc_31D31:
	cx = r[7];
	ax = r[1];
loc_31D37:
	if (di[0] > ax) {
		do { *di++ = ax; ax--; } while (--cx);
		goto loc_31EB9;
	}
	if (di[RFB_SPANROWS] < ax) {
		di += 480;
		do { *di++ = ax; ax--; } while (--cx);
		goto loc_31EB9;
	}
	di++;
	ax--;
	if (--cx) goto loc_31D37;
	goto loc_31EB9;

loc_31D5C:
	cx = r[7];
	ax = r[1];
loc_31D62:
	if (di[0] > ax) {
		do { *di++ = ax; ax++; } while (--cx);
		goto loc_31EB9;
	}
	if (di[RFB_SPANROWS] < ax) {
		di += 480;
		do { *di++ = ax; ax++; } while (--cx);
		goto loc_31EB9;
	}
	di++;
	ax++;
	if (--cx) goto loc_31D62;
	goto loc_31EB9;

loc_31D87:
	cx = r[7];
	ax = r[1];
	dx = (int16_t)((uint16_t)r[0] + 0x8000);
	if (add16_carry((uint16_t)r[0], 0x8000)) ax++;
	bx = r[6];
loc_31D99:
	if (di[0] > ax) {
		do {
			*di++ = ax;
			{
				uint16_t od = (uint16_t)dx;
				dx = (int16_t)(od - (uint16_t)bx);
				if (od < (uint16_t)bx) ax--;
			}
		} while (--cx);
		goto loc_31EB9;
	}
	if (di[RFB_SPANROWS] < ax) {
		di += 480;
		do {
			*di++ = ax;
			{
				uint16_t od = (uint16_t)dx;
				dx = (int16_t)(od - (uint16_t)bx);
				if (od < (uint16_t)bx) ax--;
			}
		} while (--cx);
		goto loc_31EB9;
	}
	di++;
	{
		uint16_t od = (uint16_t)dx;
		dx = (int16_t)(od - (uint16_t)bx);
		if (od < (uint16_t)bx) ax--;
	}
	if (--cx) goto loc_31D99;
	goto loc_31EB9;

loc_31DCA:
	cx = r[7];
	ax = r[1];
	dx = (int16_t)((uint16_t)r[0] + 0x8000);
	if (add16_carry((uint16_t)r[0], 0x8000)) ax++;
	bx = r[6];
loc_31DDC:
	if (di[0] > ax) {
		do {
			*di++ = ax;
			if (add16_carry((uint16_t)dx, (uint16_t)bx)) ax++;
			dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		} while (--cx);
		goto loc_31EB9;
	}
	if (di[RFB_SPANROWS] < ax) {
		di += 480;
		do {
			*di++ = ax;
			if (add16_carry((uint16_t)dx, (uint16_t)bx)) ax++;
			dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		} while (--cx);
		goto loc_31EB9;
	}
	di++;
	if (add16_carry((uint16_t)dx, (uint16_t)bx)) ax++;
	dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
	if (--cx) goto loc_31DDC;
	goto loc_31EB9;

loc_31E0D:
	/* shallow negative, sticky */
	cx = r[7];
	ax = r[1];
	bx = r[6];
	dx = (int16_t)((uint16_t)r[2] + 0x8000);
	if (add16_carry((uint16_t)r[2], 0x8000)) di++;
loc_31E22:
	if (di[RFB_SPANROWS] < ax) goto loc_31E39;
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (carry) {
			if (di[0] > ax) goto loc_31E51;
			di++;
		}
	}
	/* loc_31E33 */
	ax--;
	if (--cx) goto loc_31E22;
	goto loc_31EB9;
loc_31E39:
	di += 480;
loc_31E3D:
	*di++ = ax;
loc_31E3E:
	ax--;
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (carry) {
			if (--cx) goto loc_31E3D;
			goto loc_31EB9;
		}
	}
	if (--cx) goto loc_31E3E;
	goto loc_31EB9;
loc_31E4D:
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (!carry) goto loc_31E58;
	}
loc_31E51:
	*di++ = ax;
	ax--;
	if (--cx) goto loc_31E4D;
	goto loc_31EB9;
loc_31E58:
	ax--;
	if (--cx) goto loc_31E4D;
	ax++;
	di[0] = ax;
	goto loc_31EB9;

loc_31E61:
	/* shallow positive, sticky */
	cx = r[7];
	ax = r[1];
	bx = r[6];
	dx = (int16_t)((uint16_t)r[2] + 0x8000);
	if (add16_carry((uint16_t)r[2], 0x8000)) di++;
loc_31E76:
	if (di[0] > ax) goto loc_31EB2;
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (carry) {
			if (di[RFB_SPANROWS] < ax) goto loc_31E8D;
			di++;
		}
	}
	/* loc_31E87 */
	ax++;
	if (--cx) goto loc_31E76;
	goto loc_31EB9;
loc_31E8D:
	di += 480;
loc_31E91:
	di[0] = ax;
loc_31E93:
	ax++;
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (carry) {
			di++;
			if (--cx) goto loc_31E91;
			ax--;
			di[0] = ax;
			goto loc_31EB9;
		}
	}
	if (--cx) goto loc_31E93;
	di++;
	ax--;
	di[0] = ax;
	goto loc_31EB9;
loc_31EAE:
	{
		int carry = add16_carry((uint16_t)dx, (uint16_t)bx);
		dx = (int16_t)((uint16_t)dx + (uint16_t)bx);
		if (!carry) goto loc_31EB3;
	}
loc_31EB2:
	*di++ = ax;
loc_31EB3:
	ax++;
	if (--cx) goto loc_31EAE;
	goto loc_31EB9;

loc_31EB9:
	if (var_C == 0) return;
	/* loc_31EC0: fill fully clipped-off rows with window bounds */
	cx = r[10];
	if (cx > 0) {
		int16_t idx = r[3];
		if (add16_carry((uint16_t)r[2], 0x8000)) idx++;
		idx = (int16_t)(idx - cx);
		{
			int16_t* p = &var_18[idx];
			ax = sprite1_sprite_left2;
			while (cx--) *p++ = ax;
		}
	}
	cx = r[12];
	if (cx > 0) {
		int16_t idx = r[3];
		if (add16_carry((uint16_t)r[2], 0x8000)) idx++;
		idx = (int16_t)(idx - cx);
		{
			int16_t* p = &var_18[RFB_SPANROWS + idx];
			ax = (int16_t)(sprite1_sprite_widthsum - 1);
			while (cx--) *p++ = ax;
		}
	}
	cx = r[11];
	if (cx > 0) {
		int16_t idx = (int16_t)(r[5] + 1);
		int16_t* p = &var_18[idx];
		ax = sprite1_sprite_left2;
		while (cx--) *p++ = ax;
	}
	cx = r[13];
	if (cx > 0) {
		int16_t idx = (int16_t)(r[5] + 1);
		int16_t* p = &var_18[RFB_SPANROWS + idx];
		ax = (int16_t)(sprite1_sprite_widthsum - 1);
		while (cx--) *p++ = ax;
	}
}
