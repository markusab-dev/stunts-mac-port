/*
 * rtrackmap2d.c - the 2D track map, the flat overhead grid the editor shows
 * beside the 3D view.
 *
 * Ported from seg009.asm draw_2DtrackMap (3179..4006), called from the track
 * editor's load_tracks_menu_shapes at seg009.asm:832..847 as
 *
 *     sprite_set_1_size(8, 0xC8, 4, 0xB3);
 *     draw_2DtrackMap(var_8, var_18C, &var_162, &var_BE);
 *     sprite_set_1_size(0, 0x140, 0, 0xC8);
 *
 * The icon tables it indexes are built by that same caller's prologue
 * (seg009.asm 139..361) and by preRender_icons (2954..3178).
 *
 * ------------------------------------------------------------------------
 * WHAT IT DRAWS - read out of the data, not invented
 * ------------------------------------------------------------------------
 * A 12-column by 11-row window onto the 30x30 grid, at 16x16 pixels a tile.
 * Nothing here is a layout decision; every number below came out of the file
 * or the caller:
 *
 *   * SDTEDIT.PES holds 352 shapes.  The terrain tiles ("flat", "lake",
 *     "high", "goun"...) are 2 bytes x 16 rows, and a .PES byte is 8 pixels,
 *     so a tile is 16x16.  The two- and four-tile element icons are 2x32,
 *     4x16 and 4x32 - i.e. 16x32, 32x16 and 32x32.  Measured with
 *     tools/dump_shape2d.c.
 *   * the caller's clip window is (left 8, right 0xC8, top 4, bottom 0xB3).
 *     0xC8 - 8 = 192 = 12 * 16 exactly; 0xB3 - 4 = 175, one pixel short of
 *     11 * 16, so the bottom row loses its last scanline.  That is the
 *     original's arithmetic, not a rounding here.
 *   * the two caches the caller passes are cleared over si = 0..0x83
 *     (seg009.asm:299), i.e. 132 = 11 * 12 bytes each.
 *
 * A cell (row, col) of the window is drawn at
 *
 *     x = col * 16 + 8        y = row * 16 + 4
 *
 * which the asm builds as `shl ax,4 / add ax,8` for the column-derived value
 * and `shl ax,4 / add ax,4` for the row-derived one.  The blitters take x
 * first (sprite_putimage_and's arg_4 goes to the variable compared against
 * sprite_left, arg_6 to the one compared against sprite_top), and the pushes
 * are the other way round because the calls are cdecl.
 *
 * ------------------------------------------------------------------------
 * THE THREE ICON TABLES
 * ------------------------------------------------------------------------
 *   tracksmenushapes1[19]       terrain, indexed by the terrain byte.
 *                               locate_many_resources over the packed name
 *                               string at dseg:14763,
 *                               "flatlakelak1lak2lak3lak4highgoungouwgous"
 *                               "gouegou1gou2gou3gou4gou5gou6gou7gou8".
 *                               19 names; the shipped tracks use terrain
 *                               ids 0..18 and no others - counted over all
 *                               39 .TRK files.
 *   tracksmenushape2dunk2[186]  the element MASK, ANDed in.  Names come from
 *                               resource "mnam" in TEDIT.PRE.
 *   tracksmenushape2dunk[186]   the element FILL, ORed in.  Names come from
 *                               resource "snam" in TEDIT.PRE.
 *
 * snam/mnam/tnam sit at byte offsets 2037/2781/3525 of TEDIT.PRE - 744 bytes
 * apart, which is 186 four-character names each.  186 == 0xBA, the loop bound
 * at seg009.asm:361.  The archive's own size field for mnam is garbage
 * (the resources are not stored in directory order), so the count comes from
 * the loop bound and the offsets, both of which agree.
 *
 * Element ids in the shipped tracks run 0..181, so 186 entries is enough with
 * four to spare.
 *
 * ------------------------------------------------------------------------
 * 0xFD / 0xFE / 0xFF - the continuation markers
 * ------------------------------------------------------------------------
 * A multi-tile element occupies its anchor cell plus one to three more, and
 * those extra cells carry a marker instead of an element id.  The routine's
 * three special branches say which marker means what, and the shipped data
 * confirms it exactly - over all 39 tracks, for every anchor whose
 * trkObjectList[].ss_multiTileFlag is 1, 2 or 3:
 *
 *     ss_multiTileFlag 1 (two tiles down)   cell (row+1, col  ) == 0xFE
 *     ss_multiTileFlag 2 (two tiles across) cell (row  , col+1) == 0xFF
 *     ss_multiTileFlag 3 (two by two)       (row, col+1) 0xFF,
 *                                           (row+1, col) 0xFE,
 *                                           (row+1, col+1) 0xFD
 *
 * zero mismatches, and the marker totals balance: 0xFD appears 649 times and
 * there are 649 four-tile anchors; 0xFE 722 = 649 + 73; 0xFF 711 = 649 + 62.
 * So 0xFF means "my anchor is one column left", 0xFE "one row up", 0xFD
 * "up and left".
 *
 * Inside the window those cells need no drawing at all - the anchor's own
 * icon covers them, and loc_2C62C (seg009.asm:3795) just stamps 0xFF into
 * both caches.  They matter only on the window's top row and left column,
 * where the anchor has scrolled off: there the routine redraws the terrain
 * under the visible part and blits the neighbour's icon at a negative
 * offset, clipped.  That is what loc_2C0CA, loc_2C235 and loc_2C6CD are
 * for, and it is why those three use the clipping blitters while the
 * ordinary path uses the unclipped ones.
 *
 * ------------------------------------------------------------------------
 * THE CACHES
 * ------------------------------------------------------------------------
 * arg_4 is the element cache and arg_6 the terrain cache, 132 bytes each,
 * indexed row*12 + col.  A cell whose element and terrain both match the
 * cache is skipped, which is what makes the editor's map cheap to keep on
 * screen while the mouse moves.  The caller clears both to 0xFF before the
 * first call, so the first call draws everything.
 *
 * ------------------------------------------------------------------------
 * [ODDITY] seg009.asm:3211 (loc_2C0CA) and :3860 (loc_2C6CD) restore two
 * terrain tiles unconditionally - the pair a FOUR-tile neighbour would cover.
 * When the neighbour is only a two-tile element (ss_multiTileFlag 1 or 2) the
 * second restore paints terrain over a cell the neighbour never reaches.  On
 * the first draw that cell is redrawn correctly a moment later; after a
 * scroll its cache entry can still match, and it is then left showing bare
 * terrain until something else dirties it.  Left as the original has it.
 *
 * Measured, so it is a known quantity rather than a worry: of the 117 windows
 * (39 tracks x 3 scroll positions) the harness draws, exactly one shows it.
 * TRY_IT at column 18, row 19 puts the 0xFE of the two-tile loop at grid
 * (18,26) on window row 0; loc_2C0CA then restores terrain at (19,26) AND at
 * (19,27), and (19,27) carries element 166, a one-tile piece whose cache
 * entry still matches.  174 pixels of it disappear on the second pass.  Every
 * other window is pixel-stable across a redraw.
 *
 * [ODDITY] seg009.asm:3298 reads `word_45D3E[bx]`, which is `trackrows[i-1]`
 * because dseg.asm:40125 and :40126 are adjacent.  This port keeps the two in
 * different translation units (sfdata.c and rdata.c), so elem_at() below takes
 * the row as a number and spells the i == 0 case out instead of relying on
 * the layout.  See the accessor block for the four such reads.
 *
 * [DEVIATION] the five seg012 blitters this routine needs were not yet in the
 * port; they are defined at the top of this file, in the port's usual style:
 * positions are in the original's 320x200 space and scaled by RFB_SCALE, and
 * the store goes to rfb_pixels[y * RFB_VIEW_W + x] rather than through
 * sprite1.sprite_lineofs, exactly as rblit.c and rskybox.c already do.  The
 * two putpixel_icon* entries are unclipped in the original; here they are
 * still unclipped against the sprite1 window but bounded by the framebuffer,
 * because an out-of-range store is a crash rather than a wrapped write.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "fileio.h"
#include "rfbsize.h"
#include "rpes.h"
#include "shape2d.h"
/* [INTEGRATION] the unclipped blitters and the three icon tables live in
 * reditoricons.c; see the note where they used to be defined below. */
#include "reditoricons.h"

#include "../render/stunts_palette.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;
extern void far* unflip_shape(void far* shape);
extern void* locate_shape_fatal(void* resptr, const char* shapename);
extern void  sprite_putimage_and_alt(void far* shape, int16_t x, int16_t y);
extern void  sprite_clear_1_color(uint8_t color);
extern const char* rfileio_get_data_dir(void);

extern struct TRACKOBJECT trkObjectList[215];

/* ------------------------------------------------------------------ */
/* seg012 blitters                                                     */
/*                                                                     */
/* All five take the destination position as arguments and none of     */
/* them subtracts the shape's anchor (s2d_unk1/unk2): the bodies read  */
/*     mov ax,[bp+arg_4] / mov [bp+var_2],ax                           */
/* with no `sub ax,[si+4]`, unlike nopsub_339FA next door.  var_2 is   */
/* then compared against sprite_left and var_4 against sprite_top, so  */
/* arg_4 is x and arg_6 is y.                                          */
/* ------------------------------------------------------------------ */

/* The clipped pair, seg012.asm 11274 (`lodsb / and es:[di],al`) and
 * 12505 (`lodsw / or es:[di],ax`).  Both clip to sprite1's window; the
 * signed comparisons in the original are kept by working in int16_t. */
static void putimage_clipped(void far* arg_shape, int16_t px, int16_t py,
                             int or_mode)
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
		for (col = sx0; col < sx1; col++) {
			uint8_t c = srow[(col - x) / RFB_SCALE];
			if (or_mode) d[col] = (uint8_t)(d[col] | c);
			else         d[col] = (uint8_t)(d[col] & c);
		}
	}
}

/* seg012.asm 11274 sprite_putimage_and - the mask pass.  Declared in
 * shape2d.h with uint16_t coordinates; the original's compares are signed
 * and a window-edge call passes col*16-8, so they are read back as int16_t. */
void sprite_putimage_and(struct SHAPE2D far* shape, uint16_t a, uint16_t b)
{
	putimage_clipped((void far*)shape, (int16_t)a, (int16_t)b, 0);
}

/* seg012.asm 12505 sprite_putimage_or - the fill pass.  `or` leaves a zero
 * source byte alone, which is how the icon's transparent border works. */
void sprite_putimage_or(struct SHAPE2D far* shape, uint16_t a, uint16_t b)
{
	putimage_clipped((void far*)shape, (int16_t)a, (int16_t)b, 1);
}

/* The unclipped trio - seg012.asm 11970 sprite_shape_to_1 (rep movsw),
 * 11494 putpixel_iconMask (and) and 12715 putpixel_iconFillings (or) - was
 * defined here too until the Phase 11 integration.  reditoricons.c ports the
 * same three procedures and now owns them for the whole tree; they are
 * declared in reditoricons.h, included above.
 *
 * [INTEGRATION] The pairing is not arbitrary.  seg012 has two distinct
 * families and this file and reditoricons.c had each aliased one to the
 * other:
 *   sprite_putimage_and  (11274, body loc_338C9)  clips against
 *   sprite_putimage_or   (12505, same body)       sprite1.sprite_top /
 *                                                 _height / _left / _right
 *   putpixel_iconMask    (11494, body loc_33A57)  no bounds test at all
 *   putpixel_iconFillings(12715, same body)
 *   sprite_shape_to_1    (11970, body loc_33D69)  no bounds test at all
 * Different addresses, different stack frames (0Eh of locals against 0Ah)
 * and different bodies, so they are not two entry points of one proc.  The
 * clipping pair above stays here, because draw_2DtrackMap's window-edge
 * branches depend on the clip; the unclipped trio goes to reditoricons.c,
 * whose version also honours rs_rgba like the rest of the port's 2D path.
 */

/* ------------------------------------------------------------------ */
/* The icon tables                                                     */
/* dseg.asm:25628 tracksmenushape2dunk, :26370 tracksmenushape2dunk2,  */
/* :27115 tracksmenushapes1 - all three `dd 0` runs.                   */
/*                                                                     */
/* [INTEGRATION] defined in reditoricons.c, which loads the same three  */
/* from the same two archives and adds pboxshape.  Only one of the two  */
/* loaders - trackmap2d_load_icons() here, editor_load_icon_shapes()    */
/* there - may hold them at a time; each unload zeroes all three.       */
/* ------------------------------------------------------------------ */
#define MAP2D_TERRAINS   19     /* names in the packed string at dseg:14763 */
#define MAP2D_ELEMENTS  0xBA    /* the loop bound at seg009.asm:361         */

/* dseg:14763.  Passed to locate_many_resources verbatim by seg009.asm:151. */
static const char map2d_terrain_names[] =
	"flatlakelak1lak2lak3lak4highgoungouwgous"
	"gouegou1gou2gou3gou4gou5gou6gou7gou8";

static void far* s_sdtedit;
static void far* s_tedit;
static int s_icons_loaded;

/* seg009.asm 139..361.  file_load_shape2d_fatal("sdtedit") resolves to
 * SDTEDIT.PES - shape2d.c:562 walks shapeexts[] and takes the first
 * extension that exists on disk - so the shapes are planar and have to come
 * through pes_locate_shape, not locate_shape_alt.  file_load_resfile("tedit")
 * finds TEDIT.PRE, which is where the two name tables live. */
int trackmap2d_load_icons(void)
{
	const char far* snam;
	const char far* mnam;
	int16_t si;
	int k;

	if (s_icons_loaded) return 1;

	s_sdtedit = file_load_resource_pes("sdtedit");
	if (!s_sdtedit) {
		fprintf(stderr, "rtrackmap2d: kan inte ladda SDTEDIT.PES\n");
		return 0;
	}
	s_tedit = file_load_resfile("tedit");
	if (!s_tedit) {
		fprintf(stderr, "rtrackmap2d: kan inte ladda TEDIT.PRE\n");
		return 0;
	}

	/* seg009.asm:151 - locate_many_resources into tracksmenushapes1. */
	pes_locate_many(s_sdtedit, map2d_terrain_names, tracksmenushapes1);

	/* House rule: every 2D shape goes through unflip_shape.  For a .PES the
	 * planes were already un-transposed inside pes_locate_shape, which
	 * clears the flip nibble afterwards, so this is a no-op - but a silent
	 * one, and cheaper than finding out the hard way. */
	for (k = 0; k < MAP2D_TERRAINS; k++) unflip_shape(tracksmenushapes1[k]);

	/* seg009.asm:271/279 - the two four-character name tables.  The original
	 * copies each name into resID_byte1..4 and passes that; a local buffer
	 * is the same thing without the global. */
	snam = (const char far*)locate_shape_fatal(s_tedit, "snam");
	mnam = (const char far*)locate_shape_fatal(s_tedit, "mnam");

	for (si = 0; si < MAP2D_ELEMENTS; si++) {
		char nm[5];
		nm[4] = 0;
		memcpy(nm, snam + si * 4, 4);
		tracksmenushape2dunk[si] = pes_locate_shape(s_sdtedit, nm);
		unflip_shape(tracksmenushape2dunk[si]);
		memcpy(nm, mnam + si * 4, 4);
		tracksmenushape2dunk2[si] = pes_locate_shape(s_sdtedit, nm);
		unflip_shape(tracksmenushape2dunk2[si]);
	}

	s_icons_loaded = 1;
	return 1;
}

void trackmap2d_unload_icons(void)
{
	if (!s_icons_loaded) return;
	pes_release(s_sdtedit);
	unload_resource(s_sdtedit);
	unload_resource(s_tedit);
	s_sdtedit = NULL;
	s_tedit = NULL;
	s_icons_loaded = 0;
	memset(tracksmenushapes1, 0, sizeof tracksmenushapes1);
	memset(tracksmenushape2dunk, 0, sizeof tracksmenushape2dunk);
	memset(tracksmenushape2dunk2, 0, sizeof tracksmenushape2dunk2);
}

/* ------------------------------------------------------------------ */
/* seg009.asm draw_2DtrackMap (3179..4006)                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* [DEVIATION] Five guarded accessors, and the reason for each.        */
/*                                                                     */
/* The original indexes off the ends of four different arrays, and in  */
/* DOS every one of those reads lands on the next dseg object rather   */
/* than out of the address space:                                      */
/*                                                                     */
/*   `(terrainrows+2)[bx]` is terrainrows[row+1], and row can be 29 -   */
/*        the table has 30 entries.  Reachable: a 0xFF marker in the    */
/*        window's left column at grid row 29 (loc_2C6CD).              */
/*   `word_45D3E[bx]`      is trackrows[row-1], and row can be 0.       */
/*        dseg.asm:40125 puts word_45D3E immediately before trackrows,  */
/*        so DOS reads that word; it is never written and stays 0.      */
/*        This port keeps the two in different translation units.       */
/*   the grid index `+ col + 1` or `+ col - 1` runs off the row, which  */
/*        in DOS wraps into the neighbouring row of the same 900-byte   */
/*        map (or one byte past it, at row 29).                         */
/*   `tracksmenushape*[id]` past the ids the loader filled.             */
/*                                                                     */
/* None of the four is reachable with a valid track: a marker only ever */
/* appears where its anchor exists, terrain ids run 0..18 and element   */
/* ids 0..181 across all 39 shipped .TRK files, and every window drawn  */
/* by the harness (39 tracks x 3 scroll positions, under ASan) stays    */
/* inside.  They are guarded anyway, because a stray read is a crash    */
/* here rather than the harmless stale word it was in DOS.  An          */
/* out-of-range cell reads as empty and an out-of-range id as NULL;     */
/* every blitter above no-ops on NULL.                                  */
/* ------------------------------------------------------------------ */
#define MAP2D_GRID 30

/* td15_terr_map_main[terrainrows[r] + c] */
static uint8_t terr_at(int16_t r, int16_t c)
{
	if (r < 0 || r >= MAP2D_GRID || c < 0 || c >= MAP2D_GRID) return 0;
	return td15_terr_map_main[terrainrows[r] + c];
}

/* td14_elem_map_main[trackrows[r] + c]; r == -1 is the `word_45D3E[0]` case */
static uint8_t elem_at(int16_t r, int16_t c)
{
	if (r < 0 || r >= MAP2D_GRID || c < 0 || c >= MAP2D_GRID) return 0;
	return td14_elem_map_main[trackrows[r] + c];
}

static void far* terr_icon(uint8_t i)
{
	return i < MAP2D_TERRAINS ? tracksmenushapes1[i] : NULL;
}
static void far* elem_mask(uint8_t i)
{
	return i < MAP2D_ELEMENTS ? tracksmenushape2dunk2[i] : NULL;
}
static void far* elem_fill(uint8_t i)
{
	return i < MAP2D_ELEMENTS ? tracksmenushape2dunk[i] : NULL;
}

void draw_2DtrackMap(uint8_t arg_0, uint8_t arg_2,
                     uint8_t* arg_4, uint8_t* arg_6)
{
	int16_t var_E;    /* (arg_2 + var_6) * 2 - the grid row as a BYTE      */
	                  /* offset into the two word row tables, which is why */
	                  /* every use below halves it again                   */
	uint8_t var_C;    /* the element byte under this cell   */
	uint8_t var_A;    /* the terrain byte under this cell   */
	int8_t  var_8;    /* column within the window, 0..11    */
	int8_t  var_6;    /* row within the window, 0..10       */
	int16_t var_4;    /* var_2 + var_8 - the cache index    */
	int16_t var_2;    /* var_6 * 12                         */
	int16_t si, di;   /* si is always var_8; di is var_6 in every block     */
	                  /* EXCEPT loc_2C64A, where the asm loads arg_0 into   */
	                  /* it instead - kept, so the reader can follow it     */
	int16_t row;      /* arg_2 + var_6, the grid row        */

	var_6 = 0;
	goto loc_2C7F7;

loc_2C0B8:
	if (var_C != 0xFE) goto loc_2C21A;
	if (var_6 != 0) goto loc_2C21A;
loc_2C0CA:
	/* 0xFE: the anchor is one row up, off the top of the window.  Put back
	 * the two terrain tiles its icon covers here, then draw the icon from
	 * one row above so its bottom half lands in the window. */
	si = var_8; di = var_6; row = (int16_t)(arg_2 + di);
	sprite_putimage_and_alt(
		terr_icon(terr_at(row, (int16_t)(si + arg_0))),
		(int16_t)(si * 16 + 8), (int16_t)(di * 16 + 4));
	sprite_putimage_and_alt(
		terr_icon(terr_at(row, (int16_t)(si + arg_0 + 1))),
		(int16_t)(si * 16 + 24), (int16_t)(di * 16 + 4));
	sprite_putimage_and(
		elem_mask(elem_at((int16_t)(row - 1), (int16_t)(si + arg_0))),
		(uint16_t)(si * 16 + 8), (uint16_t)(di * 16 - 12));
	sprite_putimage_or(
		elem_fill(elem_at((int16_t)(row - 1), (int16_t)(si + arg_0))),
		(uint16_t)(si * 16 + 8), (uint16_t)(di * 16 - 12));
	goto loc_2C63E;

loc_2C21A:
	if (var_C != 0xFD) goto loc_2C63E;
	if (var_6 != 0) goto loc_2C63E;
	if (var_8 != 0) goto loc_2C63E;
loc_2C235:
	/* 0xFD at the window's top-left corner: the anchor is up AND left, so
	 * only one quarter of a four-tile icon is visible. */
	si = var_8; di = var_6; row = (int16_t)(arg_2 + di);
	sprite_putimage_and_alt(
		terr_icon(terr_at(row, (int16_t)(si + arg_0))),
		(int16_t)(si * 16 + 8), (int16_t)(di * 16 + 4));
	sprite_putimage_and(
		elem_mask(elem_at((int16_t)(row - 1), (int16_t)(si + arg_0 - 1))),
		(uint16_t)(si * 16 - 8), (uint16_t)(di * 16 - 12));
	sprite_putimage_or(
		elem_fill(elem_at((int16_t)(row - 1), (int16_t)(si + arg_0 - 1))),
		(uint16_t)(si * 16 - 8), (uint16_t)(di * 16 - 12));
	goto loc_2C63E;

loc_2C31E:
	if (var_C != 0) goto loc_2C382;
	/* empty cell: terrain only, and only when something changed */
	if (arg_4[var_4] == 0 && arg_6[var_4] == var_A) goto loc_2C63E;
loc_2C33C:
	sprite_shape_to_1(terr_icon(var_A),
	                  (int16_t)(var_8 * 16 + 8), (int16_t)(var_6 * 16 + 4));
	arg_4[var_4] = 0;
	arg_6[var_4] = var_A;
	goto loc_2C63E;

loc_2C382:
	if (var_C >= 0xFD) goto loc_2C62C;
	if (arg_4[var_4] == var_C && arg_6[var_4] == var_A) goto loc_2C63E;
loc_2C3A5:
	arg_4[var_4] = var_C;
	arg_6[var_4] = var_A;
	/* the terrain under the element first, then mask, then fill */
	sprite_shape_to_1(terr_icon(var_A),
	                  (int16_t)(var_8 * 16 + 8), (int16_t)(var_6 * 16 + 4));
	switch ((int8_t)trkObjectList[var_C].ss_multiTileFlag) {
	case 0:  goto loc_2C41A;
	case 1:  goto loc_2C478;
	case 2:  goto loc_2C516;
	case 3:  goto loc_2C558;
	default: goto loc_2C63E;   /* loc_2C416 */
	}

loc_2C41A:
	/* one tile: the 16x16 icon fits inside the window, so the original
	 * uses the unclipped blitters here. */
	putpixel_iconMask(elem_mask(var_C),
	                  (int16_t)(var_8 * 16 + 8), (int16_t)(var_6 * 16 + 4));
	putpixel_iconFillings(elem_fill(var_C),
	                      (int16_t)(var_8 * 16 + 8), (int16_t)(var_6 * 16 + 4));
	goto loc_2C63E;   /* loc_2C214 */

loc_2C478:
	/* two tiles down: put the terrain of the row below back first, because
	 * the 16x32 icon reaches into it. */
	si = var_8; di = var_6; row = (int16_t)(arg_2 + di);
	sprite_putimage_and_alt(
		terr_icon(terr_at((int16_t)(row + 1), (int16_t)(si + arg_0))),
		(int16_t)(si * 16 + 8), (int16_t)(di * 16 + 20));
	goto loc_2C4B5;

loc_2C516:
	/* two tiles across: same, for the column to the right. */
	si = var_8; di = var_6; row = (int16_t)(arg_2 + di);
	sprite_putimage_and_alt(
		terr_icon(terr_at(row, (int16_t)(si + arg_0 + 1))),
		(int16_t)(si * 16 + 24), (int16_t)(di * 16 + 4));
	goto loc_2C4B5;

loc_2C558:
	/* two by two: three more terrain tiles. */
	si = var_8; di = var_6; row = (int16_t)(arg_2 + di);
	sprite_putimage_and_alt(
		terr_icon(terr_at(row, (int16_t)(si + arg_0 + 1))),
		(int16_t)(si * 16 + 24), (int16_t)(di * 16 + 4));
	sprite_putimage_and_alt(
		terr_icon(terr_at((int16_t)(row + 1), (int16_t)(si + arg_0))),
		(int16_t)(si * 16 + 8), (int16_t)(di * 16 + 20));
	sprite_putimage_and_alt(
		terr_icon(terr_at((int16_t)(row + 1), (int16_t)(si + arg_0 + 1))),
		(int16_t)(si * 16 + 24), (int16_t)(di * 16 + 20));
	goto loc_2C4B5;

loc_2C4B5:
	/* the shared tail of the three multi-tile cases: the icon itself, drawn
	 * with the CLIPPING blitters because it can hang out of the window. */
	sprite_putimage_and(elem_mask(var_C),
	                    (uint16_t)(var_8 * 16 + 8), (uint16_t)(var_6 * 16 + 4));
	sprite_putimage_or(elem_fill(var_C),
	                   (uint16_t)(var_8 * 16 + 8), (uint16_t)(var_6 * 16 + 4));
	goto loc_2C63E;   /* loc_2C214 */

loc_2C62C:
	/* a continuation cell in the interior: nothing to draw, the anchor's
	 * icon already covers it.  Both caches are poisoned so that whatever
	 * lands here next is redrawn. */
	arg_4[var_4] = 0xFF;
	arg_6[var_4] = 0xFF;

loc_2C63E:
	var_8++;
loc_2C641:
	if (var_8 >= 12) goto loc_2C7F4;
loc_2C64A:
	si = var_8; di = arg_0;
	var_E = (int16_t)((var_6 + arg_2) * 2);
	var_C = elem_at((int16_t)(var_E / 2), (int16_t)(si + di));
	var_A = terr_at((int16_t)(var_E / 2), (int16_t)(si + di));
	var_4 = (int16_t)(var_2 + si);
	if (var_C < 0xFD) goto loc_2C31E;
loc_2C6A5:
	/* a continuation marker.  Only the top row and the left column can have
	 * lost their anchor off-window; anywhere else there is nothing to do. */
	if (var_6 == 0) goto loc_2C6B2;
	if (var_8 == 0) goto loc_2C6B2;
	goto loc_2C31E;
loc_2C6B2:
	arg_4[var_4] = 0xFF;
	if (var_C != 0xFF) goto loc_2C0B8;
	if (var_8 != 0) goto loc_2C0B8;
loc_2C6CD:
	/* 0xFF in the left column: the anchor is one column left, off-window.
	 * Restore the two terrain tiles a four-tile neighbour would cover, then
	 * draw its icon from x - 16 so its right half lands in the window. */
	si = var_8; di = var_6; row = (int16_t)(arg_2 + di);
	sprite_putimage_and_alt(
		terr_icon(terr_at(row, (int16_t)(si + arg_0))),
		(int16_t)(si * 16 + 8), (int16_t)(di * 16 + 4));
	sprite_putimage_and_alt(
		terr_icon(terr_at((int16_t)(row + 1), (int16_t)(si + arg_0))),
		(int16_t)(si * 16 + 8), (int16_t)(di * 16 + 20));
	sprite_putimage_and(
		elem_mask(elem_at(row, (int16_t)(si + arg_0 - 1))),
		(uint16_t)(si * 16 - 8), (uint16_t)(di * 16 + 4));
	sprite_putimage_or(
		elem_fill(elem_at(row, (int16_t)(si + arg_0 - 1))),
		(uint16_t)(si * 16 - 8), (uint16_t)(di * 16 + 4));
	goto loc_2C63E;

loc_2C7F4:
	var_6++;
loc_2C7F7:
	if (var_6 >= 11) goto loc_2C816;
	var_2 = (int16_t)(var_6 * 12);
	var_8 = 0;
	goto loc_2C641;

loc_2C816:
	return;
}

/* ------------------------------------------------------------------ */
/* Test hook: STUNTS_MAP2D_SHOT=<path.bmp>                             */
/*                                                                     */
/* Draws one map into rfb_pixels with the caller's own window and       */
/* writes a 24-bit BMP of the whole 320x200 frame.  Returns the number  */
/* of non-black pixels inside the map rectangle, so a caller (and       */
/* tools/verify.sh) can assert on ink rather than on a file existing -  */
/* a map that draws nothing is a perfectly valid BMP.                   */
/*                                                                     */
/* STUNTS_MAP2D_COL / STUNTS_MAP2D_ROW scroll the window, 0..18 and     */
/* 0..19, so the top-row and left-column branches can be exercised.     */
/* STUNTS_MAP2D_CACHECHECK draws a second pass over the warm cache and  */
/* reports how many pixels it changed; the answer has to be zero.       */
/* ------------------------------------------------------------------ */
#define MAP2D_WIN_L    8
#define MAP2D_WIN_R  0xC8
#define MAP2D_WIN_T    4
#define MAP2D_WIN_B  0xB3

static uint8_t s_elemcache[11 * 12];
static uint8_t s_terrcache[11 * 12];

static void map2d_write_bmp(const char* path)
{
	stunts_palette_t pal;
	char p[600];
	uint32_t rowb = (uint32_t)RFB_VIEW_W * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)RFB_VIEW_H;
	uint8_t hdr[54];
	int32_t w = RFB_VIEW_W, h = RFB_VIEW_H;
	uint32_t fsz = 54 + img;
	uint8_t* buf;
	FILE* f;
	int y, x;

	snprintf(p, sizeof p, "%s/SDMAIN.PVS", rfileio_get_data_dir());
	if (!stunts_palette_load(p, &pal)) stunts_palette_init_default(&pal);

	buf = (uint8_t*)calloc(1, img);
	if (!buf) return;
	for (y = 0; y < RFB_VIEW_H; y++) {
		uint8_t* r = buf + (uint32_t)(RFB_VIEW_H - 1 - y) * stride;
		for (x = 0; x < RFB_VIEW_W; x++) {
			stunts_color_rgba_t c =
				pal.colors[rfb_pixels[(int32_t)y * RFB_VIEW_W + x]];
			r[x * 3 + 0] = c.b;
			r[x * 3 + 1] = c.g;
			r[x * 3 + 2] = c.r;
		}
	}
	memset(hdr, 0, sizeof hdr);
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4); hdr[10] = 54; hdr[14] = 40;
	memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
	hdr[26] = 1; hdr[28] = 24; memcpy(hdr + 34, &img, 4);
	f = fopen(path, "wb");
	if (f) { fwrite(hdr, 1, 54, f); fwrite(buf, 1, img, f); fclose(f); }
	free(buf);
}

/* Returns the ink count, or -1 if the icons could not be loaded. */
int trackmap2d_shot(const char* path)
{
	static uint8_t before[RFB_VIEW_W * RFB_VIEW_H];
	int col = 0, row = 0, x, y;
	long ink = 0, drift = -1;
	const char* e;

	if (!trackmap2d_load_icons()) return -1;

	if ((e = getenv("STUNTS_MAP2D_COL")) != NULL) col = atoi(e);
	if ((e = getenv("STUNTS_MAP2D_ROW")) != NULL) row = atoi(e);
	if (col < 0) col = 0;
	if (col > 18) col = 18;
	if (row < 0) row = 0;
	if (row > 19) row = 19;

	/* a black page, so anything on it came from the map */
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	sprite_clear_1_color(0);

	/* the caller's own initialisation, seg009.asm:1477 */
	memset(s_elemcache, 0xFF, sizeof s_elemcache);
	memset(s_terrcache, 0xFF, sizeof s_terrcache);

	sprite_set_1_size(MAP2D_WIN_L * RFB_SCALE, MAP2D_WIN_R * RFB_SCALE,
	                  MAP2D_WIN_T * RFB_SCALE, MAP2D_WIN_B * RFB_SCALE);
	draw_2DtrackMap((uint8_t)col, (uint8_t)row, s_elemcache, s_terrcache);

	/* A second pass over a warm cache is what the editor does on every
	 * mouse move, and it must not change a pixel: the cells whose element
	 * and terrain both still match are skipped, and the window-edge cells,
	 * which are always redrawn, redraw the same thing.  Off by default
	 * because it costs a whole extra pass. */
	if (getenv("STUNTS_MAP2D_CACHECHECK")) {
		memcpy(before, rfb_pixels, sizeof before);
		draw_2DtrackMap((uint8_t)col, (uint8_t)row, s_elemcache, s_terrcache);
		drift = 0;
		for (x = 0; x < RFB_VIEW_W * RFB_VIEW_H; x++)
			if (before[x] != rfb_pixels[x]) drift++;
	}
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);

	for (y = MAP2D_WIN_T * RFB_SCALE; y < MAP2D_WIN_B * RFB_SCALE; y++)
		for (x = MAP2D_WIN_L * RFB_SCALE; x < MAP2D_WIN_R * RFB_SCALE; x++)
			if (rfb_pixels[(int32_t)y * RFB_VIEW_W + x]) ink++;

	if (path && *path) map2d_write_bmp(path);
	printf("2D-kartan: kolumn %d, rad %d, %ld malade pixlar av %d",
	       col, row, ink,
	       (MAP2D_WIN_R - MAP2D_WIN_L) * (MAP2D_WIN_B - MAP2D_WIN_T)
	       * RFB_SCALE * RFB_SCALE);
	if (drift >= 0) printf(", andra passet andrade %ld pixlar", drift);
	printf("\n");
	return (int)ink;
}
