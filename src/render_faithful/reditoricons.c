/*
 * reditoricons.c - the track editor's icon palette, and the two routines that
 * repair a track map after it has been edited.
 *
 * Ported from reference/restunts/src/restunts/asm/:
 *
 *   preRender_icons        seg009.asm 2954..3178  (225 lines)
 *   sub_2C81C              seg009.asm 4007..4200  (194 lines)
 *   sub_2C9B4              seg009.asm 4201..4499  (299 lines)
 *   sprite_shape_to_1      seg012.asm 11970..11995 + the shared body at
 *                          loc_33D69 (11996..12069, whose second entry point
 *                          sprite_shape_to_1_alt is already in rwidgets.c)
 *   putpixel_iconMask      seg012.asm 11494..11593
 *   putpixel_iconFillings  seg012.asm 12715..12812
 *
 * ------------------------------------------------------------------------
 * WHAT preRender_icons ACTUALLY IS
 *
 * The phase brief guessed "small 3D thumbnails ... it will use
 * transformed_shape_op".  It does not.  There is no 3D anywhere in this
 * routine and therefore no polygon queue: house rule 5 (transformed_shape_op
 * only queues, get_a_poly_info rasterises) has nothing to bite on here.  The
 * palette is three 2D blits per cell, and the artwork is pre-drawn.
 *
 * The editor's piece palette is a 6x6 grid of 16x16 icons on the right of the
 * screen.  preRender_icons(page) draws one page of it:
 *
 *     x = 0xDC + col * 16      (220, 236, 252, 268, 284, 300)
 *     y = 0x24 + row * 16      ( 36,  52,  68,  84, 100, 116)
 *
 * so the block is x in [220,316), y in [36,132) - comfortably inside 320x200.
 * (Reading the pushes the other way round puts y at 220 and the whole palette
 * off the bottom of the screen; sprite_shape_to_1's frame, seg012:11979, is
 * what settles it: arg_6 is added to the line offset's x, arg_8 indexes
 * sprite_lineofs.  The last push is the callee's first argument - Borland
 * cdecl - so the FIRST push is y and the second is x.)
 *
 * Each cell is drawn three times over:
 *
 *   1. tracksmenushapes1[0] - "flat", the empty-ground tile - as a base.  A
 *      two- or four-tile piece gets the base laid under its other cells too
 *      (ss_multiTileFlag 1 = one more below, 2 = one more to the right,
 *      3 = all four).
 *   2. putpixel_iconMask(tracksmenushape2dunk2[elem])  - AND.  The "mnam"
 *      shape: it punches the icon's silhouette out of the base tile.
 *   3. putpixel_iconFillings(tracksmenushape2dunk[elem]) - OR.  The "snam"
 *      shape: it fills that silhouette in.
 *
 * Page 0 is the terrain page and is different: one blit of
 * tracksmenushapes1[terrain] and nothing else.  That is the `arg_0 == 0` test
 * at seg009:3081.
 *
 * ------------------------------------------------------------------------
 * THE LAYOUT IS IN THE DATA (house rule 2), and this is where it is
 *
 * Read out of the shipped files, not inferred:
 *
 *   TEDIT.PRE   "pbox"  396 bytes = 11 pages x 6 rows x 6 cols of element id.
 *                       0xFF / 0xFE mark cells covered by a multi-tile icon
 *                       anchored left of / above them, and preRender_icons
 *                       skips anything >= 0xFD.  Page 0 holds terrain ids
 *                       0..18, pages 1..10 element ids (max 181 in the
 *                       shipped file).
 *               "snam"  744 bytes = 186 four-character resource names, the
 *                       icon fillings.  Entry 1 is "sstn", 4 "srp0", ...
 *               "mnam"  744 bytes = 186 names, the matching masks: "mstn",
 *                       "mrp0", ...
 *   SDTEDIT.PES         all 352 icon shapes plus the nineteen terrain tiles
 *                       flat lake lak1..lak4 high goun gouw gous goue
 *                       gou1..gou8 (dseg.asm:14763, one 76-char run of
 *                       four-character names).
 *
 * The eleven-page count is not a guess either: seg009:2A8CA passes 0x0A as
 * mouse_track_op's maximum for the page selector, and 396/36 = 11.
 *
 * SDTEDIT is a .PES, so its shapes are planar and must come through
 * rpes.c's pes_locate_shape(), not locate_shape_alt().  That expansion
 * already un-transposes the planes and clears the flip nibble, so the
 * unflip_shape() call house rule 3 demands is a verified no-op here - it is
 * made anyway, because the rule exists precisely to stop someone assuming
 * that.
 *
 * ------------------------------------------------------------------------
 * WHERE THE ROW TABLES CAME FROM
 *
 * IDA lost DS in seg009's last two procedures, so the row tables appear as
 * bare displacements.  They are not ambiguous.  Running the dseg.asm layout
 * (the same running-sum method tools/extract_dseg_tables.py uses, base
 * 0x3B770) gives:
 *
 *     trkObjectList      dseg+0x2098   +0x0B = ss_multiTileFlag = 0x20A3
 *                                              = seg009's [bx+20A3h]
 *     terrainrows        dseg+0x8C3C   = seg009's [di-73C4h]
 *     trackrows          dseg+0xA5D0   = seg009's [bx-5A30h]
 *                        -5A32h = 0xA5CE = trackrows[-1]
 *                        -5A2Eh = 0xA5D2 = trackrows[+1]
 *
 * trackrows[i] = 30*(29-i) and terrainrows[i] = 30*i (seg000:1002C..10041),
 * so trackrows[row+1] is the 30 bytes *before* trackrows[row] in the element
 * map - the map is stored bottom row first.  rtrackprev.c pairs the tables
 * the same way: element map through trackrows, terrain map through
 * terrainrows.
 *
 * ------------------------------------------------------------------------
 * WHAT THE TWO HELPERS DO
 *
 * sub_2C9B4 is the multi-tile consistency pass.  It keeps a 30x30 shadow
 * array of "this cell is claimed by an anchor" flags and, for every element
 * with ss_multiTileFlag 1/2/3, checks that the cells it needs are free and
 * carry the right continuation marker (0xFE below, 0xFF right, 0xFD
 * below-right).  Anchors that cannot be satisfied are deleted; continuation
 * markers nobody claimed are deleted.
 *
 * sub_2C81C then walks the map with the terrain grid beside it.  For an
 * element on terrain 1..5 it resolves continuation cells back to their
 * anchor and demands the element be one of 0x22..0x23, 0x67..0x6C or
 * 0xAB..0xAE; on terrain 7..0x0A it demands subst_hillroad_track() have a
 * substitution; on any other non-zero, non-6 terrain it deletes outright.
 * Terrain 0 and 6 are always fine.  The return value is the code of the last
 * deletion (0x0C wrong-element-on-slope, 0x0D no hill-road substitute,
 * 0x0E element on impossible terrain) or 0, and a non-zero result makes it
 * run sub_2C9B4 a second time - deleting an anchor can orphan its
 * continuation cells.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] Where the pixels go.  The three blitters write through
 * es:[sprite_lineofs[y] + x] with es from sprite1.sprite_bitmapptr.  This
 * port has one framebuffer and has never filled sprite_lineofs or
 * sprite_bitmapptr, so the address arithmetic becomes rfb_pixels[y*W + x] -
 * or rs_rgba[]/rs_pal[] when the truecolour target is armed - and every
 * coordinate is multiplied by RFB_SCALE on the way in.  Exactly the
 * deviation rwidgets.c, rshape2d.c and rblit.c already carry.
 *
 * [DEVIATION] The originals do not clip at all.  These do, to the
 * framebuffer.  At the palette's own coordinates nothing is ever clipped, so
 * this changes no pixel; it only makes a mis-placed shape survivable.
 *
 * [ODDITY] seg012's byte-tail cases.  For a shape whose s2d_width is odd the
 * AND/OR blitters finish the row with `lodsb ; and es:[di], al`, but for
 * width == 1 they use `and es:[di], ax` - a WORD operation, one byte past the
 * shape.  Every SDTEDIT icon expands to width 16, so the case is
 * unreachable; the byte-wise translation below is what an even width does.
 *
 * [ODDITY] sub_2C9B4's flag array is exactly 0x384 = 900 bytes at bp-900, and
 * `[bp+di+var_383]` indexes it at di+1.  With di = trackrows[row]+29 that is
 * offset 900 - one past the end, landing on the original's own saved BP.  The
 * array here is 901 bytes so the overrun is contained.  It needs a two- or
 * four-tile element anchored in column 29, which the editor's own placement
 * check prevents but a hand-edited .TRK does not.
 *
 * [ODDITY] sub_2C9B4 reads trackrows[row+1] at row 29 and sub_2C81C reads
 * trackrows[row-1] at row 0.  Both are outside the 30-entry table.  In the
 * original, trackrows[30] is dseg 0xA60C = word_45D7C (zero except while
 * seg017 is using it for something unrelated) and trackrows[-1] is dseg
 * 0xA5CE, an unnamed word inside the zeroed oppresources block.  Both are
 * read as 0 here - see trackrow_ofs() - which is what the original reads on
 * every ordinary run.
 *
 * [ODDITY] sub_2C9B4 also reads td14_elem_map_main[trackrows[row]+col+1],
 * which at row 0 column 29 is element-map byte 900 - one past the 30x30
 * grid.  That byte exists: the original's track block puts the terrain map
 * at +0x2019 and the element map at +0x1C94, 0x385 = 901 bytes apart
 * (src/sim_faithful/sfdata.c:211), so there is one spare byte between them.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "shape2d.h"
#include "rfbsize.h"
#include "rpes.h"
#include "reditoricons.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;
extern uint32_t* rs_rgba;               /* rshape2d.c: truecolour target */
extern const uint32_t* rs_pal;

extern struct TRACKOBJECT trkObjectList[215];        /* rdata.c */
extern uint8_t subst_hillroad_track(uint8_t a, uint8_t b);  /* rframe_helpers.c */

extern void far* unflip_shape(void far* shape);      /* rshape2d.c */
extern void* locate_shape_alt(void* resptr, const char* shapename);
extern void far* file_load_resfile(const char* filename);
extern void unload_resource(void far* resptr);
extern void sprite_set_1_size(uint16_t left, uint16_t right, uint16_t top,
                              uint16_t height);
extern void sprite_clear_1_color(uint8_t color);

/* ------------------------------------------------------------------ */
/* dseg globals                                                        */
/* ------------------------------------------------------------------ */
void far* tracksmenushapes1[19];       /* dseg 0x70CC, 19 dd */
void far* tracksmenushape2dunk[186];   /* dseg 0x6AE8, 186 dd - "snam" */
void far* tracksmenushape2dunk2[186];  /* dseg 0x6DD4, 186 dd - "mnam" */
uint8_t far* pboxshape;                /* dseg 0x6DD0, dd     - "pbox" */

/* dseg.asm:14763 - one run of four-character names, no separators. */
static const char aFlatlakelak1lak2lak3lak4highg[] =
	"flatlakelak1lak2lak3lak4highgoungouwgousgouegou1gou2gou3gou4gou5"
	"gou6gou7gou8";

/* ------------------------------------------------------------------ */
/* One UI pixel, to whichever target is armed.  Same body as            */
/* rwidgets.c's rw_put; kept local so the files stay independent.       */
/* ------------------------------------------------------------------ */
static void ei_put(int16_t x, int16_t y, uint8_t idx)
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

static uint8_t ei_get(int16_t x, int16_t y)
{
	int16_t px = (int16_t)(x * RFB_SCALE), py = (int16_t)(y * RFB_SCALE);
	if (px < 0 || px >= RFB_VIEW_W || py < 0 || py >= RFB_VIEW_H) return 0;
	return rfb_pixels[(int32_t)py * RFB_VIEW_W + px];
}

/* ------------------------------------------------------------------ */
/* seg012 loc_33D69 - the shared body of the three blitters.           */
/*                                                                     */
/*   dx = s2d_height          rows                                     */
/*   ax = s2d_width           bytes per row, one byte per pixel        */
/*   si = shape + 10h         the pixels                               */
/*   di = lineofs[y] + x      destination                              */
/*   bx = sprite_pitch - ax   the step to the next row                 */
/*                                                                     */
/* op 0 = copy (sprite_shape_to_1), 1 = AND (putpixel_iconMask),        */
/* 2 = OR (putpixel_iconFillings).                                      */
/* ------------------------------------------------------------------ */
static void blit_shape(void far* arg_shape, uint16_t arg_6x, uint16_t arg_8y,
                       int op)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h, x, y, row, col;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	x = (int16_t)arg_6x;
	y = (int16_t)arg_8y;
	src = (const uint8_t far*)arg_shape + sizeof(struct SHAPE2D);

	for (row = 0; row < h; row++) {
		for (col = 0; col < w; col++) {
			uint8_t s = src[(int32_t)row * w + col];
			int16_t px = (int16_t)(x + col), py = (int16_t)(y + row);
			switch (op) {
			case 1:  ei_put(px, py, (uint8_t)(ei_get(px, py) & s)); break;
			case 2:  ei_put(px, py, (uint8_t)(ei_get(px, py) | s)); break;
			default: ei_put(px, py, s); break;
			}
		}
	}
}

/* seg012:11970 - raw copy at an explicit position. */
void sprite_shape_to_1(void far* shape, uint16_t x, uint16_t y)
{
	blit_shape(shape, x, y, 0);
}

/* seg012:11494 - AND, no bounds test.  The name is the one seg009 calls it
 * by. */
void putpixel_iconMask(void far* shape, uint16_t x, uint16_t y)
{
	blit_shape(shape, x, y, 1);
}

/* seg012:12715 - OR, no bounds test. */
void putpixel_iconFillings(void far* shape, uint16_t x, uint16_t y)
{
	blit_shape(shape, x, y, 2);
}

/* [INTEGRATION] shape2d.h's sprite_putimage_and / sprite_putimage_or used to
 * be defined here as aliases of the two above.  That was wrong, and the
 * disassembly is unambiguous: sprite_putimage_and is seg012.asm:11274 with a
 * body at loc_338C9 that clips against sprite1.sprite_top / _height / _left /
 * _right and reserves 0Eh of locals, while putpixel_iconMask is :11494 with
 * the body at loc_33A57 - 0Ah of locals and `and es:[di],ax` with no bounds
 * test whatsoever.  Two procedures, not two entry points.  The clipping pair
 * is ported in rtrackmap2d.c, where draw_2DtrackMap's window-edge branches
 * need the clip; nothing in this file calls them. */

/* ------------------------------------------------------------------ */
/* seg009 preRender_icons (2954..3178)                                 */
/* ------------------------------------------------------------------ */
void preRender_icons(int16_t arg_0)
{
	uint8_t var_6 = 0;   /* the element (or terrain) id in this cell */
	uint8_t var_4;       /* the column, 0..5 */
	uint8_t var_2;       /* the row,    0..5 */
	int16_t si = 0;

	/* Port-only guard.  The original cannot reach preRender_icons without
	 * load_tracks_menu_shapes having filled these; a NULL here is a host
	 * integration mistake (editor_load_icon_shapes() was not called), not a
	 * data problem, and a segfault would say so far less clearly. */
	if (!pboxshape || !tracksmenushapes1[0]) return;

	var_2 = 0;
	goto loc_2C095;

loc_2BEC4:
	if (var_6 < 0xFD) goto loc_2BECD;
	goto loc_2BFAC;

loc_2BECD:
	/* the empty-ground tile under the icon */
	sprite_shape_to_1(tracksmenushapes1[0],
	                  (uint16_t)(var_4 * 16 + 0xDC),
	                  (uint16_t)(var_2 * 16 + 0x24));
	/* mov al, trkObjectList.ss_multiTileFlag[bx] ; cbw */
	switch ((int16_t)(int8_t)trkObjectList[var_6].ss_multiTileFlag) {
	case 1:  goto loc_2BF22;
	case 2:  goto loc_2C01A;
	case 3:  goto loc_2C034;
	default: goto loc_2BF4A;   /* loc_2BF20 */
	}

loc_2BF22:                     /* two tiles, the second below */
	sprite_shape_to_1(tracksmenushapes1[0],
	                  (uint16_t)(var_4 * 16 + 0xDC),
	                  (uint16_t)(var_2 * 16 + 0x34));
	goto loc_2BF4A;

loc_2C01A:                     /* two tiles, the second to the right */
	sprite_shape_to_1(tracksmenushapes1[0],
	                  (uint16_t)(var_4 * 16 + 0xEC),
	                  (uint16_t)(var_2 * 16 + 0x24));
	goto loc_2BF4A;

loc_2C034:                     /* four tiles */
	sprite_shape_to_1(tracksmenushapes1[0],
	                  (uint16_t)(var_4 * 16 + 0xEC),
	                  (uint16_t)(var_2 * 16 + 0x24));
	sprite_shape_to_1(tracksmenushapes1[0],
	                  (uint16_t)(var_4 * 16 + 0xDC),
	                  (uint16_t)(var_2 * 16 + 0x34));
	sprite_shape_to_1(tracksmenushapes1[0],
	                  (uint16_t)(var_4 * 16 + 0xEC),
	                  (uint16_t)(var_2 * 16 + 0x34));
	/* falls through */

loc_2BF4A:
	/* "renders the surroundings" */
	putpixel_iconMask(tracksmenushape2dunk2[var_6],
	                  (uint16_t)(var_4 * 16 + 0xDC),
	                  (uint16_t)(var_2 * 16 + 0x24));
	/* "renders the filling of the icons" */
	putpixel_iconFillings(tracksmenushape2dunk[var_6],
	                      (uint16_t)(var_4 * 16 + 0xDC),
	                      (uint16_t)(var_2 * 16 + 0x24));

loc_2BFAC:                     /* loc_2BFA9 is the shared `add sp, 8` */
	var_4++;
loc_2BFAF:
	if (var_4 >= 6) goto loc_2C092;
loc_2BFB8:
	si = var_4;
	/* mov al, 24h ; mul [bp+arg_0] -> 36 * page, + 6 * row + col */
	var_6 = pboxshape[36 * (uint8_t)arg_0 + var_2 * 6 + si];
	if ((uint8_t)arg_0 != 0) goto loc_2BEC4;
loc_2BFEC:
	/* page 0: the terrain palette, one blit and no mask */
	sprite_shape_to_1(tracksmenushapes1[var_6],
	                  (uint16_t)(si * 16 + 0xDC),
	                  (uint16_t)(var_2 * 16 + 0x24));
	goto loc_2BFAC;

loc_2C092:
	var_2++;
loc_2C095:
	if (var_2 >= 6) goto loc_2C0A2;
	var_4 = 0;
	goto loc_2BFAF;

loc_2C0A2:
	return;
}

/* ------------------------------------------------------------------ */
/* The row tables, with the two out-of-table indices the helpers use.  */
/* See the [ODDITY] at the top of the file.                            */
/* ------------------------------------------------------------------ */
static int16_t trackrow_ofs(int16_t r)
{
	if (r >= 0 && r < 30) return trackrows[r];
	return 0;   /* dseg 0xA5CE / 0xA60C, both zero on an ordinary run */
}

/* ------------------------------------------------------------------ */
/* seg009 sub_2C9B4 (4201..4499)                                       */
/* ------------------------------------------------------------------ */
void sub_2C9B4(void)
{
	/* var_384 = byte ptr -900, var_383 = byte ptr -899 - the same array,
	 * one byte on.  901 rather than 900 entries: see the [ODDITY]. */
	uint8_t var_384[0x384 + 1];
	uint8_t var_38C;      /* the row */
	uint8_t var_38A;      /* the element in this cell */
	uint8_t var_388;      /* the column */
	int16_t var_38E = 0, var_392 = 0;
	int16_t si = 0, di = 0, ax = 0;

	for (si = 0; si < 0x384; si++) var_384[si] = 0;
	var_384[0x384] = 0;
	var_38C = 0;
	goto loc_2CC3C;

loc_2C9D4:
	switch ((int16_t)(int8_t)trkObjectList[var_38A].ss_multiTileFlag) {
	case 1:  goto loc_2CA02;
	case 2:  goto loc_2CABC;
	case 3:  goto loc_2CB30;
	default: goto loc_2CA79;   /* loc_2CA00 */
	}

loc_2CA02:                     /* two tiles tall: needs the cell below */
	di = var_388;
	ax = (int16_t)(var_38C * 2);
	var_38E = ax;
	if (var_384[trackrow_ofs((int16_t)var_38C + 1) + di] == 0) goto loc_2CA2A;
	goto loc_2CB7E;            /* already claimed - delete the anchor */

loc_2CA2A:
	di = var_388;
	ax = (int16_t)(var_38C * 2);
	var_38E = ax;              /* loc_2CA38 */
	if (td14_elem_map_main[trackrow_ofs((int16_t)var_38C + 1) + di] == 0xFE)
		goto loc_2CA60;
	td14_elem_map_main[trackrow_ofs(var_38C) + di] = 0;   /* loc_2CA5A */
	goto loc_2CA79;

loc_2CA60:
	di = trackrow_ofs((int16_t)var_38C + 1);
	di = (int16_t)(di + var_388);                        /* loc_2CA6C */
	var_384[di] = 1;

loc_2CA79:
	var_388++;
loc_2CA7D:
	if (var_388 >= 0x1E) goto loc_2CC38;
loc_2CA87:
	di = (int16_t)(trackrow_ofs(var_38C) + var_388);
	var_38A = td14_elem_map_main[di];
	if (var_38A == 0) goto loc_2CA79;
	if (var_38A >= 0xFD) goto loc_2CAB1;
	goto loc_2C9D4;
loc_2CAB1:
	/* a continuation marker: keep it only if an anchor claimed it */
	if (var_384[di] != 0) goto loc_2CA79;
loc_2CAB7:
	td14_elem_map_main[di] = 0;
	goto loc_2CA79;

loc_2CABC:                     /* two tiles wide: needs the cell to the right */
	di = (int16_t)(trackrow_ofs(var_38C) + var_388);
	if (var_384[di + 1] == 0) goto loc_2CADE;
	td14_elem_map_main[di] = 0;                          /* -> loc_2CA5A */
	goto loc_2CA79;
loc_2CADE:
	if (td14_elem_map_main[di + 1] == 0xFF) goto loc_2CB14;
	td14_elem_map_main[di] = 0;                          /* loc_2CB0D */
	goto loc_2CA79;
loc_2CB14:
	di = trackrow_ofs(var_38C);
	di = (int16_t)(di + var_388);                        /* loc_2CB20 */
	var_384[di + 1] = 1;
	goto loc_2CA79;

loc_2CB30:                     /* four tiles: right, below and below-right */
	di = var_388;
	ax = (int16_t)(var_38C * 2);
	var_392 = ax;
	ax = (int16_t)(var_384[trackrow_ofs((int16_t)var_38C + 1) + di + 1]
	             + var_384[trackrow_ofs(var_38C) + di + 1]
	             + var_384[trackrow_ofs((int16_t)var_38C + 1) + di]);
	if (ax == 0) goto loc_2CB8E;
loc_2CB7E:
	td14_elem_map_main[trackrow_ofs(var_38C) + di] = 0;  /* -> loc_2CA5A */
	goto loc_2CA79;

loc_2CB8E:
	di = var_388;
	var_392 = (int16_t)(var_38C * 2);
	if (td14_elem_map_main[trackrow_ofs(var_38C) + di + 1] != 0xFF)
		goto loc_2CBD6;
	if (td14_elem_map_main[trackrow_ofs((int16_t)var_38C + 1) + di] != 0xFE)
		goto loc_2CBD6;
	if (td14_elem_map_main[trackrow_ofs((int16_t)var_38C + 1) + di + 1] == 0xFD)
		goto loc_2CBF2;
loc_2CBD6:
	td14_elem_map_main[trackrow_ofs(var_38C) + var_388] = 0;  /* -> loc_2CAB7 */
	goto loc_2CA79;

loc_2CBF2:
	di = var_388;
	var_392 = (int16_t)(var_38C * 2);
	var_384[trackrow_ofs(var_38C) + di + 1] = 1;              /* loc_2CC0E */
	var_384[trackrow_ofs((int16_t)var_38C + 1) + di] = 1;     /* loc_2CC1F */
	var_384[trackrow_ofs((int16_t)var_38C + 1) + di + 1] = 1; /* loc_2CC30 */
	goto loc_2CA79;

loc_2CC38:
	var_38C++;
loc_2CC3C:
	if (var_38C >= 0x1E) goto loc_2CC4C;
	var_388 = 0;               /* loc_2CC43 */
	goto loc_2CA7D;

loc_2CC4C:
	(void)var_38E;
	(void)var_392;
	return;
}

/* ------------------------------------------------------------------ */
/* seg009 sub_2C81C (4007..4200)                                       */
/* ------------------------------------------------------------------ */
int16_t sub_2C81C(void)
{
	uint8_t var_A;   /* the repair code, 0 = nothing was changed */
	uint8_t var_8;   /* the terrain under this cell */
	uint8_t var_6;   /* the row */
	uint8_t var_4;   /* the element in this cell */
	uint8_t var_2;   /* the column */
	int16_t si = 0, bx = 0, ax = 0;
	uint8_t al = 0;

	sub_2C9B4();
	var_A = 0;
	var_6 = 0;
	var_4 = 0;
	var_8 = 0;
	var_2 = 0;
	goto loc_2C993;

loc_2C834:
	if (var_4 != 0xFF) goto loc_2C848;
loc_2C83A:
	si = trackrow_ofs(var_6);          /* the anchor is one cell left */
	goto loc_2C87B;

loc_2C848:
	if (var_4 != 0xFE) goto loc_2C86A;
	bx = (int16_t)(trackrow_ofs((int16_t)var_6 - 1) + var_2);
	al = td14_elem_map_main[bx];       /* the anchor is one row up */
	goto loc_2C88A;

loc_2C86A:
	if (var_4 != 0xFD) goto loc_2C88D;
	si = trackrow_ofs((int16_t)var_6 - 1);
loc_2C87B:
	si = (int16_t)(si + var_2);
	/* si-1 is inside the map: sub_2C9B4 has already deleted every
	 * continuation marker without an anchor, so column 0 cannot hold one. */
	al = (si >= 1) ? td14_elem_map_main[si - 1] : 0;
loc_2C88A:
	var_4 = al;

loc_2C88D:
	ax = var_4;
	if (ax <  0x22) goto loc_2C8B0;
	if (ax <= 0x23) goto loc_2C903;
	if (ax <  0x67) goto loc_2C8B0;    /* loc_2C89F */
	if (ax <= 0x6C) goto loc_2C903;    /* loc_2C8A1 */
	if (ax <  0xAB) goto loc_2C8B0;
	if (ax <= 0xAE) goto loc_2C903;    /* loc_2C8AB */
loc_2C8B0:
	td14_elem_map_main[trackrow_ofs(var_6) + var_2] = 0;
	var_A = 0x0C;
	goto loc_2C903;

loc_2C8D0:
	/* push elem ; push terr ; call subst_hillroad_track - the last push is
	 * the first parameter, so terrain first, exactly as rtrackprev.c. */
	if (subst_hillroad_track(var_8, var_4) != 0) goto loc_2C903;
	td14_elem_map_main[trackrow_ofs(var_6) + var_2] = 0;
	var_A = 0x0D;

loc_2C903:
	var_2++;
loc_2C906:
	if (var_2 >= 0x1E) goto loc_2C990;
loc_2C90F:
	si = var_2;
	var_8 = td15_terr_map_main[terrainrows[var_6] + si];
	var_4 = td14_elem_map_main[trackrows[var_6] + si];
	if (var_4 == 0) goto loc_2C903;
	if (var_8 == 0) goto loc_2C903;
	if (var_8 == 6) goto loc_2C903;
	if (var_8 <  1) goto loc_2C96D;
	if (var_8 >  5) goto loc_2C960;
	goto loc_2C834;
loc_2C960:
	if (var_8 <    7) goto loc_2C96D;
	if (var_8 > 0x0A) goto loc_2C96D;
	goto loc_2C8D0;
loc_2C96D:
	var_A = 0x0E;
	td14_elem_map_main[trackrows[var_6] + var_2] = 0;
	goto loc_2C903;

loc_2C990:
	var_6++;
loc_2C993:
	if (var_6 >= 0x1E) goto loc_2C9A0;
	var_2 = 0;
	goto loc_2C906;

loc_2C9A0:
	if (var_A != 0) sub_2C9B4();
loc_2C9AA:
	return (int16_t)(int8_t)var_A;   /* mov al, [bp+var_A] ; cbw */
}

/* ------------------------------------------------------------------ */
/* seg009 load_tracks_menu_shapes 2054..2270, the part that fills the  */
/* four globals preRender_icons reads.                                 */
/*                                                                     */
/* The original walks "snam" and "mnam" copying four bytes at a time    */
/* into resID_byte1..4 and calling locate_shape_fatal on that buffer;   */
/* here the four bytes go into a local, and the lookup goes through     */
/* pes_locate_shape because SDTEDIT is a .PES (see rpes.h).             */
/* ------------------------------------------------------------------ */
static void far* s_sdtedit;   /* [bp+var_2A/var_28] in the original */
static void far* s_tedit;     /* [bp+var_22/var_20] */

int editor_load_icon_shapes(void)
{
	const uint8_t far* snam;
	const uint8_t far* mnam;
	int si;

	if (s_sdtedit) return 1;

	s_sdtedit = file_load_resource_pes("sdtedit");
	if (!s_sdtedit) return 0;
	s_tedit = file_load_resfile("tedit");
	if (!s_tedit) return 0;

	/* mov ax, offset tracksmenushapes1 ; ... ; call locate_many_resources */
	pes_locate_many(s_sdtedit, aFlatlakelak1lak2lak3lak4highg,
	                tracksmenushapes1);
	for (si = 0; si < 19; si++)
		unflip_shape(tracksmenushapes1[si]);   /* house rule 3 */

	pboxshape = (uint8_t far*)locate_shape_alt(s_tedit, "pbox");
	snam = (const uint8_t far*)locate_shape_alt(s_tedit, "snam");
	mnam = (const uint8_t far*)locate_shape_alt(s_tedit, "mnam");
	if (!pboxshape || !snam || !mnam) return 0;

	/* cmp si, 0BAh - 186 names in each table */
	for (si = 0; si < 186; si++) {
		char name[5];
		memcpy(name, snam + si * 4, 4); name[4] = 0;
		tracksmenushape2dunk[si] = pes_locate_shape(s_sdtedit, name);
		unflip_shape(tracksmenushape2dunk[si]);
		memcpy(name, mnam + si * 4, 4); name[4] = 0;
		tracksmenushape2dunk2[si] = pes_locate_shape(s_sdtedit, name);
		unflip_shape(tracksmenushape2dunk2[si]);
	}
	return 1;
}

void editor_free_icon_shapes(void)
{
	if (!s_sdtedit) return;
	pes_release(s_sdtedit);
	unload_resource(s_sdtedit);
	unload_resource(s_tedit);
	s_sdtedit = NULL;
	s_tedit = NULL;
	pboxshape = NULL;
	memset(tracksmenushapes1, 0, sizeof tracksmenushapes1);
	memset(tracksmenushape2dunk, 0, sizeof tracksmenushape2dunk);
	memset(tracksmenushape2dunk2, 0, sizeof tracksmenushape2dunk2);
}

/* ------------------------------------------------------------------ */
/* STUNTS_ICONS_SHOT=<dir>                                             */
/*                                                                     */
/* Not part of the original.  Renders every page of the palette, writes */
/* each 320x200 frame plus one contact sheet of the eleven palette      */
/* blocks, and - house rule 6 - asserts on what is actually in the      */
/* pixels, not on a file having appeared.                              */
/* ------------------------------------------------------------------ */
#define PAL_X0 0xDC
#define PAL_Y0 0x24
#define PAL_W  (6 * 16)
#define PAL_H  (6 * 16)

static void write_bmp(const char* path, const uint8_t* pix, int w, int h,
                      const stunts_palette_t* pal)
{
	uint32_t rowb = (uint32_t)w * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)h;
	uint8_t hdr[54];
	int32_t iw = w, ih = h;
	uint32_t fsz = 54 + img;
	uint8_t* buf = (uint8_t*)calloc(1, img);
	FILE* f;
	int y, x;
	if (!buf) return;
	memset(hdr, 0, sizeof hdr);
	for (y = 0; y < h; y++) {
		uint8_t* row = buf + (uint32_t)(h - 1 - y) * stride;
		for (x = 0; x < w; x++) {
			stunts_color_rgba_t c = pal->colors[pix[(int32_t)y * w + x]];
			row[x * 3 + 0] = c.b;
			row[x * 3 + 1] = c.g;
			row[x * 3 + 2] = c.r;
		}
	}
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4); hdr[10] = 54; hdr[14] = 40;
	memcpy(hdr + 18, &iw, 4); memcpy(hdr + 22, &ih, 4);
	hdr[26] = 1; hdr[28] = 24; memcpy(hdr + 34, &img, 4);
	f = fopen(path, "wb");
	if (f) { fwrite(hdr, 1, 54, f); fwrite(buf, 1, img, f); fclose(f); }
	free(buf);
}

int editor_icons_shot(const stunts_palette_t* pal)
{
	const char* dir = getenv("STUNTS_ICONS_SHOT");
	/* the contact sheet: 4 columns of 96x96 blocks, 4px gutters */
	const int cols = 4, gut = 4;
	const int rows = (EDITOR_ICON_PAGES + cols - 1) / cols;
	const int sw = cols * PAL_W * RFB_SCALE + (cols + 1) * gut;
	const int sh = rows * PAL_H * RFB_SCALE + (rows + 1) * gut;
	uint8_t* sheet;
	char path[1024];
	int page, written = 0, bad = 0;
	int maxelem = 0;
	int totchecked = 0;

	if (!dir || !*dir) return 0;
	if (!editor_load_icon_shapes()) {
		fprintf(stderr, "STUNTS_ICONS_SHOT: kan inte ladda SDTEDIT.PES/TEDIT\n");
		return -1;
	}

	/* Every id the palette will hand to trkObjectList[] and to the two
	 * 186-entry name tables, checked before it is used. */
	for (page = 0; page < EDITOR_ICON_PAGES * 36; page++) {
		int v = pboxshape[page];
		if (v >= 0xFD) continue;
		if (page >= 36 && v > maxelem) maxelem = v;
		if (page < 36 && v >= 19) {
			fprintf(stderr, "pbox: terrangid %d utanfor tracksmenushapes1[19]\n", v);
			bad++;
		}
	}
	if (maxelem >= 186 || maxelem >= 215) {
		fprintf(stderr, "pbox: elementid %d utanfor tabellerna\n", maxelem);
		bad++;
	}
	printf("pbox: %d sidor, hogsta elementid %d\n", EDITOR_ICON_PAGES, maxelem);

	/* ---- ss_multiTileFlag against the shipped pbox layout -----------
	 * If 1/2/3 did not mean below / right / 2x2, the icons this routine
	 * lays extra ground tiles under would not line up with the 0xFD..0xFF
	 * cells the artists put in pbox.  Checked here, both ways round:
	 * every anchor must cover exactly the marker cells, and every marker
	 * cell must be covered by exactly one anchor. */
	{
		int covered[36], p, r, c, k, mism = 0;
		for (p = 1; p < EDITOR_ICON_PAGES; p++) {
			const uint8_t* pg = pboxshape + p * 36;
			for (k = 0; k < 36; k++) covered[k] = 0;
			for (r = 0; r < 6; r++) for (c = 0; c < 6; c++) {
				int e = pg[r * 6 + c];
				int fl;
				if (e == 0 || e >= 0xFD) continue;
				fl = (int8_t)trkObjectList[e].ss_multiTileFlag;
				if ((fl == 1 || fl == 3) && r + 1 < 6) covered[(r+1)*6+c]++;
				if ((fl == 2 || fl == 3) && c + 1 < 6) covered[r*6+c+1]++;
				if (fl == 3 && r + 1 < 6 && c + 1 < 6)  covered[(r+1)*6+c+1]++;
			}
			for (k = 0; k < 36; k++) {
				int marker = pg[k] >= 0xFD;
				if (marker != (covered[k] == 1)) {
					fprintf(stderr, "  FEL: sida %d ruta %d,%d: pbox=%u men "
					        "%d ankare tacker den\n",
					        p, k / 6, k % 6, pg[k], covered[k]);
					mism++;
				}
			}
		}
		printf("multiTileFlag mot pbox: %d avvikelser\n", mism);
		if (mism) bad++;
	}

	sheet = (uint8_t*)calloc(1, (size_t)sw * sh);
	if (!sheet) return -1;

	for (page = 0; page < EDITOR_ICON_PAGES; page++) {
		int x, y, nonzero = 0, ncol = 0;
		uint8_t seen[256];

		sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
		sprite_clear_1_color(0);
		preRender_icons((int16_t)page);

		memset(seen, 0, sizeof seen);
		for (y = 0; y < PAL_H * RFB_SCALE; y++) {
			for (x = 0; x < PAL_W * RFB_SCALE; x++) {
				uint8_t v = rfb_pixels[(int32_t)(PAL_Y0 * RFB_SCALE + y)
				                       * RFB_VIEW_W
				                       + PAL_X0 * RFB_SCALE + x];
				if (v) nonzero++;
				if (!seen[v]) { seen[v] = 1; ncol++; }
				sheet[(int32_t)(gut + (page / cols) * (PAL_H * RFB_SCALE + gut) + y)
				      * sw + gut + (page % cols) * (PAL_W * RFB_SCALE + gut) + x] = v;
			}
		}

		snprintf(path, sizeof path, "%s/icons_page%02d.bmp", dir, page);
		write_bmp(path, rfb_pixels, RFB_VIEW_W, RFB_VIEW_H, pal);
		written++;

		printf("sida %2d: %5d satta pixlar av %d, %2d farger  %s\n",
		       page, nonzero, PAL_W * PAL_H * RFB_SCALE * RFB_SCALE, ncol,
		       path);

		/* House rule 6: assert on content.  A page of the palette that
		 * came out blank, or in a single colour, is the all-black-BMP
		 * failure this rule exists for. */
		if (nonzero < 512) {
			fprintf(stderr, "  FEL: sida %d nastan tom (%d pixlar)\n",
			        page, nonzero);
			bad++;
		}
		if (ncol < 4) {
			fprintf(stderr, "  FEL: sida %d har bara %d farger\n", page, ncol);
			bad++;
		}

		/* ---- the composite, pixel for pixel ----------------------
		 * For a one-tile icon the whole cell must be exactly
		 *     (flat & mnam) | snam
		 * so this checks the two coordinates, the AND-then-OR order
		 * and both blitters at once, against the shape bytes rather
		 * than against a screenshot.  (Multi-tile icons are skipped:
		 * their mask and filling are wider than the cell and the
		 * extra ground tiles land in neighbouring cells.) */
		if (page > 0) {
			const struct SHAPE2D far* flat =
				(const struct SHAPE2D far*)tracksmenushapes1[0];
			const uint8_t far* fp =
				(const uint8_t far*)flat + sizeof(struct SHAPE2D);
			int r, c, wrong = 0, checked = 0;
			for (r = 0; r < 6; r++) for (c = 0; c < 6; c++) {
				int e = pboxshape[page * 36 + r * 6 + c];
				const struct SHAPE2D far* msk;
				const struct SHAPE2D far* fil;
				const uint8_t far* mp;
				const uint8_t far* sp;
				int fl, dr, dc, i, j;
				if (e == 0 || e >= 0xFD) continue;
				msk = (const struct SHAPE2D far*)tracksmenushape2dunk2[e];
				fil = (const struct SHAPE2D far*)tracksmenushape2dunk[e];
				if (!flat || !msk || !fil) continue;
				mp = (const uint8_t far*)msk + sizeof(struct SHAPE2D);
				sp = (const uint8_t far*)fil + sizeof(struct SHAPE2D);
				fl = (int8_t)trkObjectList[e].ss_multiTileFlag;
				dr = (fl == 1 || fl == 3);
				dc = (fl == 2 || fl == 3);
				for (i = 0; i < 16 * (1 + dr); i++)
					for (j = 0; j < 16 * (1 + dc); j++) {
						/* the ground tile, laid once per covered cell */
						uint8_t want = fp[(i % 16) * flat->s2d_width + (j % 16)];
						uint8_t got;
						/* The masks cover the whole icon; the fillings are
						 * cropped to the rows that carry ink (element 5's
						 * "srp1" is 16x10), so each shape's own rectangle
						 * decides where it applies. */
						if (i < msk->s2d_height && j < msk->s2d_width)
							want &= mp[i * msk->s2d_width + j];
						if (i < fil->s2d_height && j < fil->s2d_width)
							want |= sp[i * fil->s2d_width + j];
						got = rfb_pixels[
							(int32_t)((PAL_Y0 + r * 16 + i) * RFB_SCALE)
							* RFB_VIEW_W + (PAL_X0 + c * 16 + j) * RFB_SCALE];
						checked++;
						if (want != got) wrong++;
					}
			}
			totchecked += checked;
			if (wrong) {
				fprintf(stderr, "  FEL: sida %d, %d av %d pixlar avviker fran "
				        "(flat & mnam) | snam\n", page, wrong, checked);
				bad++;
			} else {
				printf("          %5d pixlar = (flat & mnam) | snam\n", checked);
			}
		}
	}

	if (totchecked < 30000) {
		fprintf(stderr, "FEL: bara %d pixlar kontrollerades mot formeln\n",
		        totchecked);
		bad++;
	}

	snprintf(path, sizeof path, "%s/icons_all.bmp", dir);
	write_bmp(path, sheet, sw, sh, pal);
	written++;
	printf("kontaktkarta %s (%dx%d)\n", path, sw, sh);
	free(sheet);

	return bad ? -1 : written;
}
