/*
 * rskybox.c - the horizon panorama: mountains, trees, city skyline.
 *
 * Ported from
 *   seg003.asm  load_skybox (113)
 *   seg012.asm  sprite_putimage_and (191), sprite_putimage_and_alt (25)
 *
 * WHAT THE PANORAMA IS
 * --------------------
 * Each track names a landscape in the byte at offset 0x384 of its element map
 * - the one byte between the two 901-byte halves of a .TRK, which is why it
 * looked like padding. `load_skybox(td14_elem_map_main[0x384])` (restunts.c:801)
 * turns it into a file name from a table of nine-byte entries:
 *
 *     0 desert   1 tropical   2 alpine   3 city   4 country
 *
 * Bit 3 of the byte means "no scenery": only the colours are refreshed. The
 * archive holds four strips, named in one packed string exactly as the cockpit
 * shapes are - "scensce2sce3sce4". For ALPINE.PVS they are 320x14, 192x26,
 * 320x16 and 192x30, and they tile side by side into a panorama 0x400 pixels
 * around, which wraps as the camera turns.
 *
 * skybox_ptr1..4 are the four strips' *heights*, and the drawing code places
 * each strip at `horizon - height` so it stands on the horizon line rather
 * than hanging from it. skybox_current is the smallest of the four and
 * word_454CE the largest; skybox_op reads word_454CE to know how far above the
 * horizon the scenery can reach.
 *
 * [RESOLVED 2026-08-17] This note used to say the panorama was blitted with
 * a bitwise AND, "unresolved", because the routine the horizon calls is
 * named sprite_putimage_and_alt.  The name is a mislabel in the
 * disassembly's annotations.  The proc bodies settle it:
 *
 *   seg012.asm 11274..11465  sprite_putimage_and       inner loop
 *                                                      `lodsb / and es:[di],al`
 *   seg012.asm 11246..11273  sprite_putimage_and_alt2  `jmp loc_338C9`,
 *                                                      which is INSIDE
 *                                                      sprite_putimage_and
 *   seg012.asm 11788..11941  sprite_putimage           the plain copy
 *   seg012.asm 11762..11787  sprite_putimage_and_alt   `jmp loc_33BF5`,
 *                                                      which is INSIDE
 *                                                      sprite_putimage
 *   seg012.asm 11734..11761  nopsub_33B98              also `jmp loc_33BF5`
 *
 * So sprite_putimage_and_alt is not a variant of sprite_putimage_and at all.
 * It is a third entry point into the ordinary copy blitter, whose only job
 * is to take the destination x/y as arguments instead of reading them out of
 * the SHAPE2D header.  Whoever annotated seg012 grouped it with
 * sprite_putimage_and by name; the jump target says otherwise.
 *
 * Every call site agrees with that reading: the 19 calls to
 * sprite_putimage_and_alt (seg000 x4, seg003 x7, seg005 x6, seg009 x8 - the
 * cockpit, the horizon, the in-race overlays and the track editor's icons)
 * all draw finished pictures, while the five genuine AND calls are
 * sprite_putimage_and (seg008 x1, seg009 x4) and sprite_putimage_and_alt2
 * (seg005 x2), which are masks.
 *
 * That closes the measurement recorded here earlier and still true: on
 * DEFAULT (sky colour index 116) `dest & src` differs from a plain copy on
 * 100% of 729600 blitted pixels and paints a flat olive band, while a plain
 * copy reproduces the reference capture.  A plain copy is what the original
 * does, and a plain copy is what is implemented below.
 *
 * (The two earlier theories stay dead, and for the record: file_load_shape2d
 * applies palmap only on the .ESH/EGA path, never to a .PVS; and
 * sprite_putimage_and is a bare `and es:[di],al` with no incnums[] lookup,
 * unlike sprite_putimage_transparent.)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "rfbsize.h"
#include "shape2d.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;
extern void far* unflip_shape(void far* shape);

/* All of these live in rdata.c with their dseg addresses recorded.
 *   skyboxes[0..3]  scen sce2 sce3 sce4
 *   skybox_ptr1..4  each strip's height
 *   skybox_current  the shortest strip
 *   word_454CE      the tallest; how far above the horizon scenery reaches */
extern void far* skyboxes[4];
extern int16_t skybox_ptr1, skybox_ptr2, skybox_ptr3, skybox_ptr4;
extern int16_t skybox_current, word_454CE;
extern int16_t skybox_wat_color;
extern int16_t skybox_sky_color, skybox_grd_color;
extern int16_t meter_needle_color, dialog_fnt_colour;
extern int16_t* material_clrlist_ptr;

static void far* s_skybox_res;
static int s_loaded;
static int8_t s_current_sel = -1;

/* The nine-byte name table at dseg aDesert. */
static const char* const skybox_names[8] = {
	"desert", "tropical", "alpine", "city", "country", "", "", ""
};

/* ------------------------------------------------------------------ */
/* seg012 sprite_putimage_and / _alt                                    */
/*                                                                      */
/* AND the shape into the framebuffer at a caller-given position,        */
/* clipped to the sprite1 window. The position is in the original's      */
/* 320x200 space and is scaled here, as every other 2D blit in the port  */
/* does, so the panorama keeps pace with the world at RFB_SCALE > 1.     */
/* ------------------------------------------------------------------ */
void sprite_putimage_and_alt(void far* arg_shape, int16_t px, int16_t py)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h, x, y, row, col;
	int16_t clip_l, clip_r, clip_t, clip_b;
	int16_t sx0, sy0, sx1, sy1;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	if (w <= 0 || h <= 0) return;
	x = (int16_t)(px * RFB_SCALE);
	y = (int16_t)(py * RFB_SCALE);
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
	sx1 = (int16_t)(x + w * RFB_SCALE) < clip_r
	      ? (int16_t)(x + w * RFB_SCALE) : clip_r;
	sy1 = (int16_t)(y + h * RFB_SCALE) < clip_b
	      ? (int16_t)(y + h * RFB_SCALE) : clip_b;
	if (sx0 >= sx1 || sy0 >= sy1) return;

	for (row = sy0; row < sy1; row++) {
		const uint8_t far* srow = src + (int32_t)((row - y) / RFB_SCALE) * w;
		uint8_t* d = &rfb_pixels[(int32_t)row * RFB_VIEW_W];
		for (col = sx0; col < sx1; col++)
			d[col] = srow[(col - x) / RFB_SCALE];
	}
}

/* ------------------------------------------------------------------ */
/* seg003 load_skybox / unload_skybox                                   */
/* ------------------------------------------------------------------ */
void unload_skybox(void)
{
	if (s_skybox_res) mmgr_release(s_skybox_res);
	s_skybox_res = NULL;
	s_loaded = 0;
	s_current_sel = -1;
	skyboxes[0] = skyboxes[1] = skyboxes[2] = skyboxes[3] = NULL;
}

void load_skybox(char arg_sel)
{
	int16_t lo, hi;

	if ((arg_sel & 8) == 0) {
		/* already the right landscape? then only the colours below */
		if (!(s_loaded && s_current_sel == arg_sel)) {
			const char* name;
			unload_skybox();
			s_current_sel = arg_sel;
			s_loaded = 1;
			/* The original calls file_load_shape2d_fatal, which picks
			 * the extension by video mode; for VGA that is .PVS, the
			 * same route the cockpit archives take. Not .RES - the
			 * landscapes are shape archives, not resource files. */
			name = skybox_names[arg_sel & 7];
			if (*name) {
				char fn[32];
				snprintf(fn, sizeof(fn), "%s.pvs", name);
				s_skybox_res = file_load_resource(3, fn);
			} else {
				s_skybox_res = NULL;
			}
			if (s_skybox_res) {
				int k;
				locate_many_resources((char far*)s_skybox_res,
					"scensce2sce3sce4", (char far**)skyboxes);
				/* Any of them can be stored transposed, exactly as
				 * gbox and the showroom backdrop are. */
				for (k = 0; k < 4; k++) unflip_shape(skyboxes[k]);

				skybox_ptr1 = skyboxes[0]
					? (int16_t)((struct SHAPE2D far*)skyboxes[0])->s2d_height : 0;
				skybox_ptr2 = skyboxes[1]
					? (int16_t)((struct SHAPE2D far*)skyboxes[1])->s2d_height : 0;
				skybox_ptr3 = skyboxes[2]
					? (int16_t)((struct SHAPE2D far*)skyboxes[2])->s2d_height : 0;
				skybox_ptr4 = skyboxes[3]
					? (int16_t)((struct SHAPE2D far*)skyboxes[3])->s2d_height : 0;

				lo = skybox_ptr1;
				if (skybox_ptr2 < lo) lo = skybox_ptr2;
				if (skybox_ptr3 < lo) lo = skybox_ptr3;
				if (skybox_ptr4 < lo) lo = skybox_ptr4;
				skybox_current = lo;

				hi = skybox_ptr1;
				if (skybox_ptr2 > hi) hi = skybox_ptr2;
				if (skybox_ptr3 > hi) hi = skybox_ptr3;
				if (skybox_ptr4 > hi) hi = skybox_ptr4;
				word_454CE = hi;
			}
		}
	}

	/* loc_1D88E: the colours are refreshed either way */
	skybox_sky_color = material_clrlist_ptr[0x22 / 2];
	skybox_grd_color = material_clrlist_ptr[0x20 / 2];
	skybox_wat_color = material_clrlist_ptr[0xC8 / 2];
	meter_needle_color = dialog_fnt_colour;
}
