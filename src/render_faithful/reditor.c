/*
 * reditor.c - the track editor.
 *
 * Ported from reference/restunts/src/restunts/asm/seg009.asm:
 *
 *   load_tracks_menu_shapes   54..2953   (the editor itself; the name is a
 *                                         lie, the shape loading is only
 *                                         its first 320 lines)
 *   preRender_icons         2954..3178   (the palette page)
 *   draw_2DtrackMap         3179..4006   (the map)
 *   sub_2C81C               4007..4200   (validate: terrain vs elements)
 *   sub_2C9B4               4201..4499   (clean: orphaned continuations)
 *
 * plus the five primitives it draws with, from seg012.asm:
 *
 *   sprite_shape_to_1         11970  opaque, unclipped
 *   sprite_putimage_and_alt   11762  opaque, CLIPPED  [ODDITY: the name is
 *                                    IDA's guess; the inner loop is
 *                                    `rep movsb`, not `and`]
 *   sprite_putimage_and       11274  AND,    clipped
 *   sprite_putimage_or        12505  OR,     clipped
 *   putpixel_iconMask         11494  AND,    unclipped
 *   putpixel_iconFillings     12715  OR,     unclipped
 *   sub_35B76 (seg012 17930)  XOR fill, clipped - the blinking cursor box
 *
 * ========================================================================
 * OVERLAP WITH reditoricons.c - READ THIS BEFORE INTEGRATING
 * ========================================================================
 * src/render_faithful/reditoricons.c appeared in the main tree while this
 * file was being written, and it ports three of the same routines:
 * preRender_icons, sub_2C81C and sub_2C9B4, plus the icon loading and the
 * three seg012 blitters, as GLOBAL symbols with dseg-named globals
 * (tracksmenushapes1, tracksmenushape2dunk, tracksmenushape2dunk2,
 * pboxshape).
 *
 * Nothing collides: every counterpart in this file is `static` and the
 * shape tables are members of struct REDITOR, so the two files link
 * together as they stand and both were verified working.  But the
 * duplication is real and should be resolved by whoever integrates:
 * delete this file's static preRender_icons / sub_2C81C / sub_2C9B4 /
 * ed_blit-based icon loading, include "reditoricons.h", and read the four
 * globals instead of the struct members.  That is about 250 lines.  It was
 * deliberately NOT done here, because reditoricons.c was still being
 * written by another agent and a build that depends on a file in flux is
 * a build that cannot be verified.
 *
 * ========================================================================
 * WHAT THE SCREEN IS, READ OUT OF THE DATA (house rule 2)
 * ========================================================================
 * Nothing here was invented.  tools/dump_editor_tables.c prints the three
 * tables TEDIT.PRE carries, and they are the whole layout:
 *
 *   pbox  396 bytes = 11 pages x 6 rows x 6 columns of element codes.
 *         0xFF in a cell means "the icon to my left is two tiles wide and
 *         this is its right half", 0xFE means "the icon above me is two
 *         tiles tall".  That is why the cursor keys in the palette step
 *         over cells instead of onto them.
 *   snam  186 four-character shape names - the icon FILLINGS, ORed on.
 *   mnam  186 four-character shape names - the icon MASKS, ANDed first.
 *   tnam  three characters per element - the key its name lives under, so
 *         the bottom line reads "paved road", "corkscrew up/down", ...
 *
 * and SDTEDIT.PES carries 352 shapes: flat/lake/lak1..gou8 (the 19 terrain
 * tiles), crs0..crs3 (four cursor backgrounds, 16x16 / 16x32 / 32x16 /
 * 32x32 - one per multi-tile shape), ucr0..ucr3 (the same four as
 * outlines), and the 186 icon pairs.
 *
 * [ODDITY] rpes.c's header says SDCRED.PES is "the only .PES the game
 * ships".  It is not: SDTEDIT.PES is a second one, and it is what the
 * editor's icons are stored in.  Everything else rpes.c says about the
 * format holds, and its expansion is used unchanged here.
 *
 * The geometry, all of it out of the disassembly's own literals:
 *
 *   map       12 x 11 tiles of 16x16 at (8, 4), clipped to (8,4)-(200,179)
 *   palette    6 x  6 tiles of 16x16 at (220, 36)
 *   page bar  (220, 132) 96 x 8         - palette row 6
 *   Horizon   (221, 140) 94 x 14        - palette row 7
 *   Load/New  (221, 156) / (269, 156)   - palette row 8, columns 0 and 3
 *   Save/Done (221, 172) / (269, 172)   - palette row 9, columns 0 and 3
 *   name line (8, 192)
 *
 * and the five mouse regions are dseg's own trackmenu2_buttons_* arrays.
 *
 * ========================================================================
 * THE TRACK
 * ========================================================================
 * 1802 bytes: 900 element codes, the landscape byte at 0x384, 900 terrain
 * codes, one pad.  In this port td14_elem_map_main and td15_terr_map_main
 * are the two halves of one block (sfdata_init_trackdata), exactly as
 * init_trackdata carves it in the original, which is why the editor's load
 * is one 1802-byte read into td14 and its save one 0x70A-byte write from
 * td14 - `mov ax, 70Ah` at seg009:2291.
 *
 * The two halves are addressed through different row tables and this is
 * the single easiest thing to get wrong:
 *      element (row, col) -> td14[trackrows[row]   + col]   (30*(29-row))
 *      terrain (row, col) -> td15[terrainrows[row] + col]   (30*row)
 * so the element grid runs bottom-up and the terrain grid top-down.  Every
 * access below goes through one of the two, never through row*30.
 *
 * ========================================================================
 * [DEVIATION] - five, each stated plainly.  A UI screen has no oracle.
 * ========================================================================
 *
 *  1. No sprite windows.  The original builds the chrome once into a
 *     0x140 x 0xC8 window (sprite_make_wnd), keeps four more windows for
 *     the cursor's backing store, and repaints only what changed - which
 *     is what var_32 / var_184 / var_A / var_2C / var_C2 are all for.
 *     This port redraws the whole picture every frame, as rendscreen.c,
 *     rintro.c and rtrackprev.c do.  The dirty flags are kept in struct
 *     REDITOR because the edit logic reads them, but nothing acts on them
 *     during drawing.  draw_2DtrackMap's two 132-byte caches are kept and
 *     honoured, then invalidated before each full repaint, so the routine
 *     is transcribed rather than gutted.
 *
 *  2. No modal loop and no DOS mouse.  loc_2A720..loc_2BE44 is a
 *     `while (var_188)` around input_checking / mouse_multi_hittest /
 *     mouse_track_op.  Its body is reditor_key(), reditor_click() and
 *     reditor_activate(); the six mouse_track_op calls (which are the
 *     scrollbar drags) are not ported - a scrollbar drag is a host gesture
 *     and the state it writes, var_8 / var_18C / var_C6, is settable from
 *     reditor_click() and the arrow keys.
 *
 *  3. The eight show_dialog calls become RED_ACT_* return codes plus
 *     rdialog.c.  The dialog itself IS ported (see rdialog.c for why); the
 *     editor hands the host the key of the text to show and takes the
 *     answer back through reditor_set_horizon / reditor_new_track /
 *     reditor_load_track / reditor_save_track.  do_fileselect_dialog and
 *     do_savefile_dialog (seg008 1207..1984 and 2043..2191) are the file
 *     browser and are NOT ported; the host supplies a path.
 *
 *  4. The blinking cursor is a phase the caller advances rather than a
 *     16-tick timer, and in map mode it draws ucr<n> over the tile instead
 *     of XORing and un-XORing the backing store.  Same two pictures.
 *
 *  5. sub_2EB1E, timer_get_counter_unk, g_is_busy and the audio calls are
 *     dropped; they gate on hardware this port does not have.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "reditor.h"
#include "rfbsize.h"
#include "rpes.h"
#include "rwidgets.h"
#include "shape2d.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;
extern struct TRACKOBJECT trkObjectList[215];
extern uint8_t far* td14_elem_map_main;
extern uint8_t far* td15_terr_map_main;
extern int16_t trackrows[];
extern int16_t terrainrows[];
extern int16_t track_pieces_counter;
extern uint8_t byte_45D90, byte_45E16;
extern int16_t word_45D3E;
extern int16_t dialog_fnt_colour;
extern int16_t performGraphColor;
extern int16_t fontdef_unk_0E;
extern struct GAMEINFO gameconfig;

extern void* file_load_resfile(const char* filename);
extern void* locate_shape_alt(void* resptr, const char* shapename);
extern void unload_resource(void far* resptr);
extern char far* locate_text_res(char far* res, char* key);
extern void far* unflip_shape(void far* shape);
extern void font_set_colour(uint16_t colour, uint16_t background);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern uint16_t font_op2(const char* str);
extern void font_set_fontdef(void);
extern void sprite_set_1_size(uint16_t l, uint16_t r, uint16_t t, uint16_t h);
extern void sprite_clear_1_color(uint8_t colour);
extern uint8_t subst_hillroad_track(uint8_t terr, uint8_t elem);
extern int16_t track_setup(void);

/* dseg 21094..21100 - every colour this screen uses. */
static const int16_t word_407EC = 11;   /* panel bevel, top/left         */
static const int16_t word_407EE =  9;   /* panel bevel, inner ring       */
static const int16_t word_407F0 =  1;   /* panel bevel, bottom/right     */
static const int16_t word_407F2 = 12;   /* the XORed cursor box          */
static const int16_t word_407F4 = 15;   /* button bevel light            */
static const int16_t word_407F6 =  8;   /* button bevel dark             */
static const int16_t word_407F8 =  7;   /* button face                   */

/* dseg 14716: the ten F-key scancodes that jump straight to a page. */
static const uint16_t word_3ECBE[10] = {
	0x3B00, 0x3C00, 0x3D00, 0x3E00, 0x3F00, 0x4000, 0x4100, 0x4200,
	0x4300, 0x4400
};
/* dseg 14759/14761: the two cursor limits, indexed by var_34.
 * byte_3ECFE = column limit (map 30, palette 6);
 * byte_3ED00 = row    limit (map 29, palette 9). */
static const uint8_t byte_3ECFE[2] = { 30, 6 };
static const uint8_t byte_3ED00[2] = { 29, 9 };

/* dseg 14664 aEokenseieemseedewwefuenpestejsejdeteewa... - fifteen
 * three-character keys, one per track_setup verdict.  locate_text_res
 * prepends the language byte, so key "eok" fetches resource "eeok",
 * "Track ok."  Transcribed from the dseg bytes, not guessed. */
static const char* const s_verdict_keys[15] = {
	"eok", "ens", "eie", "ems", "eed", "eww", "efu", "enp",
	"est", "ejs", "ejd", "ete", "ewa", "eft", "eat"
};

/* dseg 14763..14877 - the three runs of four-character resource names
 * locate_many_resources walks. */
static const char s_names1[] =
	"flatlakelak1lak2lak3lak4highgoungouwgousgouegou1gou2gou3gou4gou5"
	"gou6gou7gou8";
static const char s_names2[] = "crs0crs1crs2crs3";
static const char s_names3[] = "ucr0ucr1ucr2ucr3";

/* dseg trackmenu2_buttons_x1/x2/y1/y2 - the five regions
 * mouse_multi_hittest tests, in the original's order. */
static const int16_t trackmenu2_buttons_x1[5] = {   9, 202, 220,   8, 220 };
static const int16_t trackmenu2_buttons_x2[5] = { 199, 206, 315, 199, 315 };
static const int16_t trackmenu2_buttons_y1[5] = { 181,   4, 132,   4,  36 };
static const int16_t trackmenu2_buttons_y2[5] = { 187, 179, 139, 179, 187 };

/* ------------------------------------------------------------------ */
/* The blitters.  [DEVIATION 1]: all five write straight into the one   */
/* framebuffer, clipped to sprite1's window, with each source pixel     */
/* becoming an RFB_SCALE block - the same deviation rshape2d.c and      */
/* rwidgets.c already carry.  At RFB_SCALE 1 these plot exactly the     */
/* pixels the originals plot.                                          */
/* ------------------------------------------------------------------ */
#define ED_OP_COPY 0
#define ED_OP_AND  1
#define ED_OP_OR   2
#define ED_OP_XOR  3

static void ed_put(int32_t off, uint8_t v, int op)
{
	switch (op) {
	case ED_OP_AND: rfb_pixels[off] = (uint8_t)(rfb_pixels[off] & v); break;
	case ED_OP_OR:  rfb_pixels[off] = (uint8_t)(rfb_pixels[off] | v); break;
	case ED_OP_XOR: rfb_pixels[off] = (uint8_t)(rfb_pixels[off] ^ v); break;
	default:        rfb_pixels[off] = v;                              break;
	}
}

static void ed_clip(int16_t* l, int16_t* r, int16_t* t, int16_t* b)
{
	*l = (int16_t)sprite1.sprite_left;
	*r = (int16_t)sprite1.sprite_right;
	*t = (int16_t)sprite1.sprite_top;
	*b = (int16_t)sprite1.sprite_height;
	if (*l < 0) *l = 0;
	if (*t < 0) *t = 0;
	if (*r > RFB_VIEW_W) *r = RFB_VIEW_W;
	if (*b > RFB_VIEW_H) *b = RFB_VIEW_H;
}

/* One shape at (px, py) in 320x200 coordinates.  No anchor subtraction:
 * every entry point this file uses (sprite_shape_to_1, the two _alt forms
 * and the two putpixel_icon* forms) stores the argument unchanged. */
static void ed_blit(const void* arg_shape, int16_t px, int16_t py, int op)
{
	const struct SHAPE2D* shape = (const struct SHAPE2D*)arg_shape;
	const uint8_t* src;
	int16_t w, h, x, y, row, col, cl, cr, ct, cb, sx0, sy0, sx1, sy1;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	if (w <= 0 || h <= 0) return;
	src = (const uint8_t*)arg_shape + sizeof(struct SHAPE2D);
	x = (int16_t)(px * RFB_SCALE);
	y = (int16_t)(py * RFB_SCALE);

	ed_clip(&cl, &cr, &ct, &cb);
	sx0 = x > cl ? x : cl;
	sy0 = y > ct ? y : ct;
	sx1 = (int16_t)(x + w * RFB_SCALE) < cr ? (int16_t)(x + w * RFB_SCALE) : cr;
	sy1 = (int16_t)(y + h * RFB_SCALE) < cb ? (int16_t)(y + h * RFB_SCALE) : cb;
	if (sx0 >= sx1 || sy0 >= sy1) return;

	for (row = sy0; row < sy1; row++) {
		const uint8_t* srow = src + (int32_t)((row - y) / RFB_SCALE) * w;
		int32_t base = (int32_t)row * RFB_VIEW_W;
		for (col = sx0; col < sx1; col++)
			ed_put(base + col, srow[(col - x) / RFB_SCALE], op);
	}
}

/* seg025 sub_3702E: a one-pixel rectangle outline drawn with sub_35B76,
 * which is an XOR fill - so drawing it twice restores the picture, which
 * is exactly how the original blinks the palette cursor.
 *      var_2 = x2 - x1 + 1 ; var_4 = y2 - y1 - 1
 *      if (var_2 > 0) { xorfill(x1, y1, var_2, 1); xorfill(x1, y2, var_2, 1); }
 *      if (var_4 > 0) { si = y1 + 1;
 *                       xorfill(x1, si, 1, var_4); xorfill(x2, si, 1, var_4); }
 * The -1 on the height and the +1 on si are the original's; they keep the
 * verticals from painting over the two horizontals. */
static void ed_xor_fill(int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t colour)
{
	int16_t i, j, cl, cr, ct, cb;
	ed_clip(&cl, &cr, &ct, &cb);
	for (j = 0; j < h * RFB_SCALE; j++) {
		int16_t py = (int16_t)(y * RFB_SCALE + j);
		if (py < ct || py >= cb) continue;
		for (i = 0; i < w * RFB_SCALE; i++) {
			int16_t px = (int16_t)(x * RFB_SCALE + i);
			if (px < cl || px >= cr) continue;
			ed_put((int32_t)py * RFB_VIEW_W + px, (uint8_t)colour, ED_OP_XOR);
		}
	}
}

static void sub_3702E(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      int16_t colour)
{
	int16_t var_2 = (int16_t)(x2 - x1 + 1);
	int16_t var_4 = (int16_t)(y2 - y1 - 1);
	if (var_2 > 0) {
		ed_xor_fill(x1, y1, var_2, 1, colour);
		ed_xor_fill(x1, y2, var_2, 1, colour);
	}
	if (var_4 > 0) {
		int16_t si = (int16_t)(y1 + 1);
		ed_xor_fill(x1, si, 1, var_4, colour);
		ed_xor_fill(x2, si, 1, var_4, colour);
	}
}

/*
 * seg008 mouse_track_op (2640..2945), drawing half only - loc_28B78.
 *
 *   mouse_track_op(mode, x, w, y, h, value, page, range)
 *
 * The routine is a scrollbar: mode 0 paints it, mode 1 runs the drag loop.
 * The drag loop needs the DOS mouse and is [DEVIATION 2]; the painting is
 * twenty instructions and the editor's three sliders look broken without
 * it, so it is here.  Whether the bar is horizontal or vertical is decided
 * by which of w and h is larger - there is no flag.
 *
 *      var_6  = max(w, h)                 the long axis
 *      var_16 = var_6 - 1
 *      si     = (var_16 * value)          * 4 / (range * 4)
 *      di     = ((value + page) * var_16) * 4 / (range * 4)
 * The multiply-by-four and the divide by range*4 cancel; they are the
 * original's way of keeping a rounding bit, and are transcribed as they
 * stand rather than simplified.
 */
static void mouse_track_op_draw(int16_t arg_2, int16_t arg_4, int16_t arg_6,
                                int16_t arg_8, int16_t arg_A, int16_t arg_C,
                                int16_t arg_E)
{
	int16_t var_A = (arg_4 > arg_8) ? 0 : 1;
	int16_t var_6 = (arg_4 > arg_8) ? arg_4 : arg_8;
	int16_t var_14 = (int16_t)(arg_E << 2);
	int16_t var_16 = (int16_t)(var_6 - 1);
	int16_t si, di, var_12;

	if (var_14 == 0) return;
	si = (int16_t)((int32_t)((int16_t)(var_16 * arg_A) << 2) / var_14);
	di = (int16_t)((int32_t)((int16_t)(((int16_t)(arg_A + arg_C)) * var_16)
	                         << 2) / var_14);
	var_12 = (int16_t)(di - si);

	sprite_1_unk(arg_2, arg_6, arg_4, arg_8, 0);         /* the trough   */
	if (var_A == 0)                                      /* loc_28B78    */
		sprite_1_unk((int16_t)(arg_2 + si), arg_6, var_12, arg_8,
		             dialog_fnt_colour);
	else                                                 /* loc_28BAA    */
		sprite_1_unk(arg_2, (int16_t)(arg_6 + si), arg_4, var_12,
		             dialog_fnt_colour);
}

/* ------------------------------------------------------------------ */
/* Map accessors.  Every read and write of the two grids goes through   */
/* these four, so the trackrows / terrainrows pairing cannot drift.     */
/* ------------------------------------------------------------------ */
static uint8_t ed_elem(int16_t row, int16_t col)
{
	if (row < 0 || row > 29 || col < 0 || col > 29) return 0;
	return td14_elem_map_main[trackrows[row] + col];
}
static void ed_set_elem(int16_t row, int16_t col, uint8_t v)
{
	if (row < 0 || row > 29 || col < 0 || col > 29) return;
	td14_elem_map_main[trackrows[row] + col] = v;
}
static uint8_t ed_terr(int16_t row, int16_t col)
{
	if (row < 0 || row > 29 || col < 0 || col > 29) return 0;
	return td15_terr_map_main[terrainrows[row] + col];
}
static void ed_set_terr(int16_t row, int16_t col, uint8_t v)
{
	if (row < 0 || row > 29 || col < 0 || col > 29) return;
	td15_terr_map_main[terrainrows[row] + col] = v;
}

static uint8_t ed_multiflag(uint8_t elem)
{
	return (uint8_t)trkObjectList[elem].ss_multiTileFlag;
}

/* pbox[page][row][col] - 0x24 bytes per page, six per row. */
static uint8_t ed_pbox(const struct REDITOR* ed, int16_t page,
                       int16_t row, int16_t col)
{
	int32_t i;
	if (!ed->pboxshape) return 0;
	if (page < 0 || page > 10 || row < 0 || row > 5 || col < 0 || col > 5)
		return 0;
	i = (int32_t)page * 0x24 + (int32_t)row * 6 + col;
	return ed->pboxshape[i];
}

/* ------------------------------------------------------------------ */
/* seg009 132..375 - reditor_open                                       */
/* ------------------------------------------------------------------ */
int reditor_open(struct REDITOR* ed)
{
	int16_t si;

	memset(ed, 0, sizeof *ed);

	/* file_load_shape2d_fatal_thunk("sdtedit").  SDTEDIT.PES is planar,
	 * so it goes through rpes.c exactly as SDCRED.PES does. */
	ed->sdtedit = file_load_resource_pes("sdtedit");
	if (!ed->sdtedit) return 0;
	ed->tedit = file_load_resfile("tedit");
	if (!ed->tedit) return 0;

	pes_locate_many(ed->sdtedit, s_names1, ed->tracksmenushapes1);
	pes_locate_many(ed->sdtedit, s_names2, ed->tracksmenushapes2);
	pes_locate_many(ed->sdtedit, s_names3, ed->tracksmenushapes3);
	/* House rule 3: every 2D shape through unflip_shape().  A no-op for a
	 * .PES result - rpes.c clears the transpose nibble on expansion - but
	 * a shape stored transposed renders as diagonal streaks and no size
	 * check can tell, so it is done rather than reasoned about. */
	for (si = 0; si < 19; si++) unflip_shape(ed->tracksmenushapes1[si]);
	for (si = 0; si < 4; si++)  unflip_shape(ed->tracksmenushapes2[si]);
	for (si = 0; si < 4; si++)  unflip_shape(ed->tracksmenushapes3[si]);

	ed->pboxshape = (const uint8_t*)locate_shape_alt(ed->tedit, "pbox");
	ed->snam      = (const uint8_t*)locate_shape_alt(ed->tedit, "snam");
	ed->mnam      = (const uint8_t*)locate_shape_alt(ed->tedit, "mnam");
	ed->tnam      = (const uint8_t*)locate_shape_alt(ed->tedit, "tnam");
	if (!ed->pboxshape || !ed->snam || !ed->mnam || !ed->tnam) return 0;

	/* loc_2A43E: both draw_2DtrackMap caches start "nothing drawn yet". */
	memset(ed->var_162, 0xFF, sizeof ed->var_162);
	memset(ed->var_BE,  0xFF, sizeof ed->var_BE);

	/* loc_2A451: 186 icon pairs, named four characters at a time out of
	 * snam and mnam. */
	for (si = 0; si < 186; si++) {
		char name[5];
		memcpy(name, ed->snam + si * 4, 4); name[4] = 0;
		ed->tracksmenushape2dunk[si] = pes_locate_shape(ed->sdtedit, name);
		unflip_shape(ed->tracksmenushape2dunk[si]);
		memcpy(name, ed->mnam + si * 4, 4); name[4] = 0;
		ed->tracksmenushape2dunk2[si] = pes_locate_shape(ed->sdtedit, name);
		unflip_shape(ed->tracksmenushape2dunk2[si]);
	}

	/* loc_2A50D - the initial state, assignment for assignment. */
	ed->var_DA   = 0xFF;
	ed->var_14   = 1;
	ed->var_6    = 1;
	ed->var_188  = 1;
	ed->var_C6   = 1;
	ed->var_30   = 1;
	ed->var_38   = 0;
	ed->var_34   = 0;
	ed->var_190  = 0;
	ed->var_18D  = 0;
	ed->var_8    = 0;
	ed->var_18C  = 0;
	ed->var_12   = 0;
	ed->var_18E  = byte_45D90;
	ed->var_180  = byte_45E16;
	ed->var_17F  = 7;
	ed->var_C4   = 0;
	ed->var_D4   = 0;
	return 1;
}

void reditor_close(struct REDITOR* ed)
{
	if (ed->sdtedit) { pes_release(ed->sdtedit); unload_resource(ed->sdtedit); }
	if (ed->tedit)   unload_resource(ed->tedit);
	ed->sdtedit = NULL;
	ed->tedit = NULL;
}

const char* reditor_text(struct REDITOR* ed, const char* key)
{
	char far* p;
	if (!ed->tedit) return "";
	p = locate_text_res((char far*)ed->tedit, (char*)key);
	return p ? (const char*)p : "";
}

const char* reditor_verdict_key(int16_t verdict)
{
	if (verdict < 0 || verdict > 14) return s_verdict_keys[2];  /* "eie" */
	return s_verdict_keys[verdict];
}

/* ------------------------------------------------------------------ */
/* seg009 2954..3178 - preRender_icons(page)                            */
/*                                                                      */
/* Six rows by six columns at (220 + col*16, 36 + row*16).  Page 0 is    */
/* the terrain page and draws tracksmenushapes1[code] straight; every    */
/* other page draws the blank tile first, then the mask, then the        */
/* filling, and for a multi-tile icon lays extra blanks down so the      */
/* two- or four-tile picture has a ground under all of it.               */
/* ------------------------------------------------------------------ */
static void preRender_icons(struct REDITOR* ed, uint8_t arg_0)
{
	int16_t var_2, var_4;                    /* row, column              */
	uint8_t var_6;

	for (var_2 = 0; var_2 < 6; var_2++) {                /* loc_2C095    */
		for (var_4 = 0; var_4 < 6; var_4++) {            /* loc_2BFAF    */
			int16_t x = (int16_t)(var_4 * 16 + 0xDC);
			int16_t y = (int16_t)(var_2 * 16 + 0x24);
			var_6 = ed_pbox(ed, arg_0, var_2, var_4);    /* loc_2BFB8    */

			if (arg_0 == 0) {                            /* loc_2BFEC    */
				ed_blit(ed->tracksmenushapes1[var_6 < 19 ? var_6 : 0],
				        x, y, ED_OP_COPY);
				continue;
			}
			if (var_6 >= 0xFD) continue;                 /* loc_2BEC4    */

			/* the blank ground under the icon */
			ed_blit(ed->tracksmenushapes1[0], x, y, ED_OP_COPY);
			switch (ed_multiflag(var_6)) {
			case 1:                                      /* loc_2BF22    */
				ed_blit(ed->tracksmenushapes1[0], x,
				        (int16_t)(y + 16), ED_OP_COPY);
				break;
			case 2:                                      /* loc_2C01A    */
				ed_blit(ed->tracksmenushapes1[0], (int16_t)(x + 16),
				        y, ED_OP_COPY);
				break;
			case 3:                                      /* loc_2C034    */
				ed_blit(ed->tracksmenushapes1[0], (int16_t)(x + 16),
				        y, ED_OP_COPY);
				ed_blit(ed->tracksmenushapes1[0], x,
				        (int16_t)(y + 16), ED_OP_COPY);
				ed_blit(ed->tracksmenushapes1[0], (int16_t)(x + 16),
				        (int16_t)(y + 16), ED_OP_COPY);
				break;
			default:
				break;
			}
			/* loc_2BF4A: mask then filling, always at the cell itself */
			ed_blit(ed->tracksmenushape2dunk2[var_6], x, y, ED_OP_AND);
			ed_blit(ed->tracksmenushape2dunk[var_6], x, y, ED_OP_OR);
		}
	}
}

/* ------------------------------------------------------------------ */
/* seg009 3179..4006 - draw_2DtrackMap(colscroll, rowscroll, ec, tc)    */
/* ------------------------------------------------------------------ */
static void ed_tile(struct REDITOR* ed, int16_t row, int16_t col,
                    int16_t sx, int16_t sy, int op)
{
	uint8_t t = ed_terr(row, col);
	ed_blit(ed->tracksmenushapes1[t < 19 ? t : 0], sx, sy, op);
}

static void draw_2DtrackMap(struct REDITOR* ed, uint8_t arg_0, uint8_t arg_2,
                            uint8_t* arg_4, uint8_t* arg_6)
{
	int16_t var_6, var_8;

	for (var_6 = 0; var_6 < 0x0B; var_6++) {             /* loc_2C7F7    */
		int16_t var_2 = (int16_t)(var_6 * 12);
		for (var_8 = 0; var_8 < 0x0C; var_8++) {         /* loc_2C641    */
			int16_t mrow = (int16_t)(var_6 + arg_2);
			int16_t mcol = (int16_t)(var_8 + arg_0);
			int16_t x = (int16_t)(var_8 * 16 + 8);
			int16_t y = (int16_t)(var_6 * 16 + 4);
			int16_t var_4 = (int16_t)(var_2 + var_8);
			uint8_t var_C = ed_elem(mrow, mcol);         /* element      */
			uint8_t var_A = ed_terr(mrow, mcol);         /* terrain      */

			if (var_C < 0xFD) {                          /* loc_2C31E    */
				if (var_C == 0) {
					if (arg_4[var_4] == 0 && arg_6[var_4] == var_A)
						continue;
					ed_blit(ed->tracksmenushapes1[var_A < 19 ? var_A : 0],
					        x, y, ED_OP_COPY);
					arg_4[var_4] = 0;
					arg_6[var_4] = var_A;
					continue;
				}
				/* loc_2C38B */
				if (arg_4[var_4] == var_C && arg_6[var_4] == var_A)
					continue;
				arg_4[var_4] = var_C;
				arg_6[var_4] = var_A;
				ed_blit(ed->tracksmenushapes1[var_A < 19 ? var_A : 0],
				        x, y, ED_OP_COPY);
				switch (ed_multiflag(var_C)) {
				case 0:                                  /* loc_2C41A    */
					ed_blit(ed->tracksmenushape2dunk2[var_C], x, y,
					        ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[var_C], x, y,
					        ED_OP_OR);
					break;
				case 1:                                  /* loc_2C478    */
					ed_tile(ed, (int16_t)(mrow + 1), mcol, x,
					        (int16_t)(y + 16), ED_OP_COPY);
					ed_blit(ed->tracksmenushape2dunk2[var_C], x, y,
					        ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[var_C], x, y,
					        ED_OP_OR);
					break;
				case 2:                                  /* loc_2C516    */
					ed_tile(ed, mrow, (int16_t)(mcol + 1),
					        (int16_t)(x + 16), y, ED_OP_COPY);
					ed_blit(ed->tracksmenushape2dunk2[var_C], x, y,
					        ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[var_C], x, y,
					        ED_OP_OR);
					break;
				case 3:                                  /* loc_2C558    */
					ed_tile(ed, mrow, (int16_t)(mcol + 1),
					        (int16_t)(x + 16), y, ED_OP_COPY);
					ed_tile(ed, (int16_t)(mrow + 1), mcol, x,
					        (int16_t)(y + 16), ED_OP_COPY);
					ed_tile(ed, (int16_t)(mrow + 1), (int16_t)(mcol + 1),
					        (int16_t)(x + 16), (int16_t)(y + 16),
					        ED_OP_COPY);
					ed_blit(ed->tracksmenushape2dunk2[var_C], x, y,
					        ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[var_C], x, y,
					        ED_OP_OR);
					break;
				default:
					break;
				}
				continue;
			}

			/* loc_2C6A5: a continuation byte.  Only the top row and the
			 * left column need anything drawn - everywhere else the tile
			 * that owns it has already covered this one. */
			if (var_6 != 0 && var_8 != 0) {              /* loc_2C62C    */
				arg_4[var_4] = 0xFF;
				arg_6[var_4] = 0xFF;
				continue;
			}
			arg_4[var_4] = 0xFF;                         /* loc_2C6B2    */

			if (var_C == 0xFF && var_8 == 0) {           /* loc_2C6CD    */
				/* the left half is scrolled off: two terrain tiles and
				 * the owning element, drawn from one column left */
				ed_tile(ed, mrow, mcol, x, y, ED_OP_COPY);
				ed_tile(ed, (int16_t)(mrow + 1), mcol, x,
				        (int16_t)(y + 20 - 4), ED_OP_COPY);
				{
					uint8_t owner = ed_elem(mrow, (int16_t)(mcol - 1));
					ed_blit(ed->tracksmenushape2dunk2[owner],
					        (int16_t)(x - 16), y, ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[owner],
					        (int16_t)(x - 16), y, ED_OP_OR);
				}
				continue;
			}
			if (var_C == 0xFE && var_6 == 0) {           /* loc_2C0CA    */
				/* the top half is scrolled off */
				ed_tile(ed, mrow, mcol, x, y, ED_OP_COPY);
				ed_tile(ed, mrow, (int16_t)(mcol + 1),
				        (int16_t)(x + 16), y, ED_OP_COPY);
				{
					uint8_t owner = ed_elem((int16_t)(mrow - 1), mcol);
					ed_blit(ed->tracksmenushape2dunk2[owner], x,
					        (int16_t)(y - 16), ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[owner], x,
					        (int16_t)(y - 16), ED_OP_OR);
				}
				continue;
			}
			if (var_C == 0xFD && var_6 == 0 && var_8 == 0) {
				/* loc_2C235: the up-left quarter is off on both axes */
				ed_tile(ed, mrow, mcol, x, y, ED_OP_COPY);
				{
					uint8_t owner = ed_elem((int16_t)(mrow - 1),
					                        (int16_t)(mcol - 1));
					ed_blit(ed->tracksmenushape2dunk2[owner],
					        (int16_t)(x - 16), (int16_t)(y - 16),
					        ED_OP_AND);
					ed_blit(ed->tracksmenushape2dunk[owner],
					        (int16_t)(x - 16), (int16_t)(y - 16),
					        ED_OP_OR);
				}
				continue;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* seg009 4201..4499 - sub_2C9B4: drop continuation bytes whose owner    */
/* is gone, and the owners whose continuations are gone.  Runs over the  */
/* whole 30x30 grid with a 900-byte "this cell is claimed" scratch.      */
/* ------------------------------------------------------------------ */
static void sub_2C9B4(void)
{
	uint8_t var_384[0x385];          /* claimed-below / claimed-right    */
	int16_t var_38C, var_388;        /* row, column                      */

	memset(var_384, 0, sizeof var_384);

	for (var_38C = 0; var_38C < 0x1E; var_38C++) {       /* loc_2CC3C    */
		for (var_388 = 0; var_388 < 0x1E; var_388++) {   /* loc_2CA7D    */
			int16_t here = (int16_t)(trackrows[var_38C] + var_388);
			uint8_t var_38A = td14_elem_map_main[here];  /* loc_2CA87    */

			if (var_38A == 0) continue;
			if (var_38A >= 0xFD) {                       /* loc_2CAB1    */
				/* a continuation nobody claimed - erase it */
				if (var_384[here] == 0) td14_elem_map_main[here] = 0;
				continue;
			}
			switch (ed_multiflag(var_38A)) {             /* loc_2C9D4    */
			case 1: {                                    /* loc_2CA02    */
				int16_t below = (int16_t)(trackrows[var_38C + 1] + var_388);
				if (var_38C + 1 > 29) { td14_elem_map_main[here] = 0; break; }
				if (var_384[below] != 0) {               /* already taken */
					td14_elem_map_main[here] = 0;
					break;
				}
				if (td14_elem_map_main[below] != 0xFE) { /* loc_2CA38    */
					td14_elem_map_main[here] = 0;
					break;
				}
				var_384[below] = 1;                      /* loc_2CA60    */
				break;
			}
			case 2: {                                    /* loc_2CABC    */
				if (var_388 + 1 > 29) { td14_elem_map_main[here] = 0; break; }
				if (var_384[here + 1] != 0) {            /* var_383 = +1  */
					td14_elem_map_main[here] = 0;
					break;
				}
				if (td14_elem_map_main[here + 1] != 0xFF) {
					td14_elem_map_main[here] = 0;        /* loc_2CB0D    */
					break;
				}
				var_384[here + 1] = 1;                   /* loc_2CB14    */
				break;
			}
			case 3: {                                    /* loc_2CB30    */
				int16_t below, belowr;
				if (var_38C + 1 > 29 || var_388 + 1 > 29) {
					td14_elem_map_main[here] = 0;
					break;
				}
				below  = (int16_t)(trackrows[var_38C + 1] + var_388);
				belowr = (int16_t)(below + 1);
				/* loc_2CB42: none of the three may be claimed already */
				if (var_384[belowr] + var_384[here + 1] + var_384[below]) {
					td14_elem_map_main[here] = 0;
					break;
				}
				if (td14_elem_map_main[here + 1] != 0xFF ||
				    td14_elem_map_main[below]    != 0xFE ||
				    td14_elem_map_main[belowr]   != 0xFD) {
					td14_elem_map_main[here] = 0;        /* loc_2CBD6    */
					break;
				}
				var_384[here + 1] = 1;                   /* loc_2CBF2    */
				var_384[below]    = 1;
				var_384[belowr]   = 1;
				break;
			}
			default:
				break;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* seg009 4007..4200 - sub_2C81C: is every element legal on the terrain  */
/* under it?  Returns 0 for "nothing wrong", or 12/13/14 - the water,    */
/* straight-edge and angled-edge verdicts.  Anything it rejects is       */
/* erased, and the clean-up above is run again afterwards.               */
/* ------------------------------------------------------------------ */
static int16_t sub_2C81C(void)
{
	uint8_t var_A = 0, var_8, var_4;
	int16_t var_6, var_2;

	sub_2C9B4();

	for (var_6 = 0; var_6 < 0x1E; var_6++) {             /* loc_2C993    */
		for (var_2 = 0; var_2 < 0x1E; var_2++) {         /* loc_2C906    */
			int16_t here = (int16_t)(trackrows[var_6] + var_2);
			var_8 = td15_terr_map_main[terrainrows[var_6] + var_2];
			var_4 = td14_elem_map_main[here];            /* loc_2C90F    */
			if (var_4 == 0) continue;
			if (var_8 == 0) continue;
			if (var_8 == 6) continue;                    /* plain hill   */

			if (var_8 >= 1 && var_8 <= 5) {              /* loc_2C834    */
				/* water: follow the continuation back to its owner */
				/* `mov bx, word_45D3E[bx]` with bx = row*2 is
				 * trackrows[row-1]; at row 0 it is word_45D3E
				 * itself, which dseg 40125 leaves 0. */
				int16_t up = (int16_t)(var_6 == 0 ? word_45D3E
				                                  : trackrows[var_6 - 1]);
				if (var_4 == 0xFF)
					var_4 = td14_elem_map_main[here - 1];
				else if (var_4 == 0xFE)
					var_4 = td14_elem_map_main[up + var_2];
				else if (var_4 == 0xFD)
					var_4 = td14_elem_map_main[up + var_2 - 1];
				/* loc_2C88D: the three ranges that survive on water -
				 * 0x22..0x23, 0x67..0x6C and 0xAB..0xAE, which are the
				 * overpass stilts, the overpass span and the boat. */
				if ((var_4 >= 0x22 && var_4 <= 0x23) ||
				    (var_4 >= 0x67 && var_4 <= 0x6C) ||
				    (var_4 >= 0xAB && var_4 <= 0xAE))
					continue;
				td14_elem_map_main[here] = 0;            /* loc_2C8B0    */
				var_A = 0x0C;
				continue;
			}
			if (var_8 >= 7 && var_8 <= 0x0A) {           /* loc_2C8D0    */
				/* a sloped or angled terrain edge: only what
				 * subst_hillroad_track has a substitute for */
				if (subst_hillroad_track(var_8, var_4) != 0) continue;
				td14_elem_map_main[here] = 0;
				var_A = 0x0D;
				continue;
			}
			td14_elem_map_main[here] = 0;                /* loc_2C96D    */
			var_A = 0x0E;
		}
	}
	if (var_A != 0) sub_2C9B4();                         /* loc_2C9A0    */
	return (int16_t)(int8_t)var_A;
}

/* ------------------------------------------------------------------ */
/* seg009 loc_2A720..loc_2A851 - keep the cursor inside the window, and  */
/* loc_2AB1C..loc_2AD2B - the cursor rectangle and the name line's code. */
/* ------------------------------------------------------------------ */
void reditor_update(struct REDITOR* ed)
{
	int16_t cx, cy;

	/* loc_2A72F: the cursor's size follows the selected element. */
	ed->var_14 = 1;
	ed->var_6 = 1;
	ed->var_C4 = 0;
	if (ed->var_C6 != 0) {
		switch (ed_multiflag(ed->var_190)) {
		case 1: ed->var_6  = 2; ed->var_C4 = 1; break;   /* loc_2A76C    */
		case 2: ed->var_C4 = 2; ed->var_14 = 2; break;   /* loc_2A7C0    */
		case 3: ed->var_14 = 2; ed->var_6  = 2;
		        ed->var_C4 = 3; break;                   /* loc_2A7CC    */
		default: break;
		}
	}

	if (ed->var_34 == 0) {
		/* loc_2A77E: a two-tile shape at the last row or column is
		 * pulled back one so it still fits. */
		if (ed->var_18E == 0x1D && ed->var_14 == 2) ed->var_18E--;
		if (ed->var_180 == 0x1D && ed->var_6  == 2) ed->var_180--;

		/* loc_2A7A0 / loc_2A7DC: scroll so the cursor is visible, using
		 * the same >12 / <0 tests the original uses. */
		while ((int16_t)ed->var_18E - (int16_t)ed->var_8
		       + (int16_t)ed->var_14 > 0x0C) ed->var_8++;
		while ((int16_t)ed->var_18E < (int16_t)ed->var_8) ed->var_8--;
		while ((int16_t)ed->var_180 - (int16_t)ed->var_18C
		       + (int16_t)ed->var_6 > 0x0B) ed->var_18C++;
		while ((int16_t)ed->var_180 < (int16_t)ed->var_18C) ed->var_18C--;
	}

	/* loc_2A866: a palette cell holding 0xFF/0xFE is a continuation;
	 * step back onto the icon that owns it. */
	if (ed->var_17F < 6) {
		int guard = 12;
		for (;;) {
			uint8_t v = ed_pbox(ed, ed->var_C6, ed->var_17F, ed->var_18D);
			if (v < 0xFE || guard-- <= 0) break;
			if (v == 0xFF) { if (ed->var_18D) ed->var_18D--; else break; }
			else           { if (ed->var_17F) ed->var_17F--; else break; }
		}
	}

	if (ed->var_34 == 0) {
		/* loc_2AB1C - the cursor over the map. */
		ed->var_2E = (int16_t)(ed->var_14 << 4);
		ed->var_1A = (int16_t)(ed->var_6 << 4);
		ed->var_CA = (int16_t)((((int16_t)ed->var_18E
		                         - (int16_t)ed->var_8) << 4) + 8);
		ed->var_DC = (int16_t)((((int16_t)ed->var_180
		                         - (int16_t)ed->var_18C) << 4) + 4);
		/* the element the name line describes: follow a continuation to
		 * the tile that owns it (loc_2AB98 / loc_2ABBC / loc_2ABDA) */
		{
			uint8_t e = ed_elem(ed->var_180, ed->var_18E);
			if (e == 0xFD)      e = ed_elem((int16_t)(ed->var_180 - 1),
			                                (int16_t)(ed->var_18E - 1));
			else if (e == 0xFE) e = ed_elem((int16_t)(ed->var_180 - 1),
			                                ed->var_18E);
			else if (e == 0xFF) e = ed_elem(ed->var_180,
			                                (int16_t)(ed->var_18E - 1));
			ed->var_182 = e;
		}
	} else {
		/* loc_2ABEA - the cursor over the palette. */
		ed->var_2E = 0x10;
		ed->var_1A = 0x10;
		ed->var_DC = (int16_t)((ed->var_17F << 4) + 0x24);
		if (ed->var_17F == 6) {                          /* the page bar */
			ed->var_CA = 0xDC;
			ed->var_1A = 8;
			ed->var_2E = 0x60;
			ed->var_182 = 0;
		} else if (ed->var_17F == 7) {                   /* Horizon      */
			ed->var_DC = (int16_t)(ed->var_DC - 8);
			ed->var_18D = 0;
			ed->var_CA = 0xDC;
			ed->var_2E = 0x60;
			ed->var_182 = 0;
		} else if (ed->var_17F > 7) {                    /* the four     */
			ed->var_DC = (int16_t)(ed->var_DC - 8);
			ed->var_18D = (uint8_t)(ed->var_18D >= 3 ? 3 : 0);
			ed->var_CA = (int16_t)((ed->var_18D << 4) + 0xDC);
			ed->var_2E = 0x30;
			ed->var_182 = 0;
		} else {                                         /* loc_2AC6E    */
			ed->var_CA = (int16_t)((ed->var_18D << 4) + 0xDC);
			/* a two-tile icon gets a two-tile cursor - the tests are on
			 * the NEIGHBOURING pbox cells, not on the icon's flag */
			if (ed->var_17F < 5 &&
			    ed_pbox(ed, ed->var_C6, (int16_t)(ed->var_17F + 1),
			            ed->var_18D) == 0xFE)
				ed->var_1A = 0x20;
			if (ed->var_18D < 5 &&
			    ed_pbox(ed, ed->var_C6, ed->var_17F,
			            (int16_t)(ed->var_18D + 1)) == 0xFF)
				ed->var_2E = 0x20;
			{
				uint8_t v = ed_pbox(ed, ed->var_C6, ed->var_17F,
				                    ed->var_18D);
				ed->var_182 = (uint8_t)(v < 0xFD ? v : 0);
			}
		}
		if (ed->var_C6 == 0) ed->var_182 = 0;            /* loc_2AD1F    */
	}

	cx = ed->var_CA;
	cy = ed->var_DC;
	(void)cx; (void)cy;
}

/* ------------------------------------------------------------------ */
/* One complete picture.                                                */
/* ------------------------------------------------------------------ */
void reditor_draw(struct REDITOR* ed)
{
	const char* label;

	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	sprite_clear_1_color(0);
	font_set_fontdef();

	/* loc_2A50D - the chrome, in the original's own order. */
	label = reditor_text(ed, "bti");
	draw_button(label, 0xD9, 3, 0x66, 0x16,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_lines_unk(5, 0, 0xCE, 0xBE, word_407EC, word_407EE, word_407F0);
	draw_lines_unk(0xD9, 0x20, 0x66, 0x9E,
	               word_407EC, word_407EE, word_407F0);
	draw_button(reditor_text(ed, "bsc"), 0xDD, 0x8C, 0x5E, 0x0E,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_button(reditor_text(ed, "blo"), 0xDD, 0x9C, 0x2E, 0x0E,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_button(reditor_text(ed, "bsa"), 0xDD, 0xAC, 0x2E, 0x0E,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_button(reditor_text(ed, "bcl"), 0x10D, 0x9C, 0x2E, 0x0E,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_button(reditor_text(ed, "bex"), 0x10D, 0xAC, 0x2E, 0x0E,
	            word_407F4, word_407F6, word_407F8, 0);

	/* loc_2A92B / loc_2A95F - the two scrollbars, and loc_2A8CA - the
	 * palette's page bar.  Every literal is the original's. */
	mouse_track_op_draw(9, 0xC0, 0xB5, 5, (int16_t)ed->var_8, 0x0C, 0x1E);
	mouse_track_op_draw(0xCA, 5, 4, 0xB0, (int16_t)ed->var_18C, 0x0B, 0x1E);
	if (ed->var_C6 == 0)                                 /* loc_2A8B2    */
		mouse_track_op_draw(0xDD, 0x5F, 0x85, 5, 0, 1, 1);
	else                                                 /* loc_2A8CA    */
		mouse_track_op_draw(0xDD, 0x5F, 0x85, 5,
		                    (int16_t)(ed->var_C6 - 1), 1, 0x0A);

	/* loc_2A983 - the map, inside its own window. */
	memset(ed->var_162, 0xFF, sizeof ed->var_162);   /* [DEVIATION 1]    */
	memset(ed->var_BE,  0xFF, sizeof ed->var_BE);
	sprite_set_1_size(8, 0xC8, 4, 0xB3);
	draw_2DtrackMap(ed, ed->var_8, ed->var_18C, ed->var_162, ed->var_BE);
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);

	/* loc_2A8B2 - the palette page. */
	preRender_icons(ed, ed->var_C6);

	/*
	 * loc_2AE7C - the cursor.  Over the map it alternates between ucr<n>
	 * (loc_2AEA9) and a PREVIEW of the piece about to be placed
	 * (loc_2AEB4, which blits the cursor window the block at loc_2A9D6
	 * composed): on the terrain page the terrain tile itself inside a
	 * one-pixel box, on any other page crs<n> with the icon's mask and
	 * filling over it.  Over the palette it is sub_3702E's XOR box.
	 */
	if (ed->var_34 == 0) {
		int16_t x = ed->var_CA, y = ed->var_DC;
		sprite_set_1_size(8, 0xC8, 4, 0xB3);
		if (ed->blink & 1) {
			ed_blit(ed->tracksmenushapes3[ed->var_C4], x, y, ED_OP_COPY);
		} else if (ed->var_C6 == 0) {                    /* loc_2AA01    */
			ed_blit(ed->tracksmenushapes1[ed->var_190 < 19
			                              ? ed->var_190 : 0],
			        x, y, ED_OP_COPY);
			/* four preRender_line calls, (1,0)-(15,0), (1,14)-(15,14),
			 * (1,0)-(1,14) and (15,0)-(15,14), in performGraphColor */
			sprite_1_unk((int16_t)(x + 1), y, 15, 1, performGraphColor);
			sprite_1_unk((int16_t)(x + 1), (int16_t)(y + 14), 15, 1,
			             performGraphColor);
			sprite_1_unk((int16_t)(x + 1), y, 1, 15, performGraphColor);
			sprite_1_unk((int16_t)(x + 15), y, 1, 15, performGraphColor);
		} else {                                         /* loc_2AA8E    */
			ed_blit(ed->tracksmenushapes2[ed->var_C4], x, y, ED_OP_COPY);
			if (ed->var_190 != 0) {
				ed_blit(ed->tracksmenushape2dunk2[ed->var_190], x, y,
				        ED_OP_AND);
				ed_blit(ed->tracksmenushape2dunk[ed->var_190], x, y,
				        ED_OP_OR);
			}
		}
		sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	} else if (ed->blink & 1) {
		sub_3702E(ed->var_CA, (int16_t)(ed->var_DC - 1),
		          (int16_t)(ed->var_2E + ed->var_CA),
		          (int16_t)(ed->var_1A + ed->var_DC - 1), word_407F2);
	}

	/* loc_2AD38 - the name of whatever is under the cursor, at (8, 192). */
	{
		char key[4];
		const char* name;
		key[0] = (char)ed->tnam[ed->var_182 * 3 + 0];
		key[1] = (char)ed->tnam[ed->var_182 * 3 + 1];
		key[2] = (char)ed->tnam[ed->var_182 * 3 + 2];
		key[3] = 0;
		name = reditor_text(ed, key);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text(name, 8, 0xC0);
		ed->var_38 = (int16_t)font_op2(name);
	}
}

/* ------------------------------------------------------------------ */
/* seg009 loc_2B49A..loc_2BB46 - Enter.                                 */
/* ------------------------------------------------------------------ */
static void ed_place(struct REDITOR* ed)
{
	uint8_t var_1C;

	if (ed->var_C6 == 0) {
		/* loc_2B8F8 - the terrain page.  Pressing Enter twice on the
		 * same tile puts back what was there, which is the undo. */
		if (ed->var_18E == ed->var_DA && ed->var_180 == ed->var_C0) {
			var_1C = ed->var_190;
			ed->var_190 = ed->var_192;
			ed->var_192 = var_1C;
		} else {                                         /* loc_2B928    */
			ed->var_192 = ed_terr(ed->var_180, ed->var_18E);
			ed->var_DA = ed->var_18E;
			ed->var_C0 = ed->var_180;
		}
		ed_set_terr(ed->var_C0, ed->var_DA, ed->var_190);/* loc_2B95B    */
		ed->var_D4 = 1;
		ed->var_30 = 1;
		return;
	}

	/* loc_2B98E - an element.  A two-tile shape may not start on the
	 * last row or the last column. */
	if ((ed_multiflag(ed->var_190) & 1) && ed->var_180 > 0x1C) return;
	if ((ed_multiflag(ed->var_190) & 2) && ed->var_18E > 0x1C) return;

	if (ed->var_18E == ed->var_DA && ed->var_180 == ed->var_C0) {
		var_1C = ed->var_190;                            /* loc_2B9D4    */
		ed->var_190 = ed->var_192;
		ed->var_192 = var_1C;
	} else {                                             /* loc_2BA04    */
		uint8_t e = ed_elem(ed->var_180, ed->var_18E);
		ed->var_192 = (uint8_t)(e < 0xFD ? e : 0);
		ed->var_DA = ed->var_18E;
		ed->var_C0 = ed->var_180;
	}

	ed_set_elem(ed->var_C0, ed->var_DA, ed->var_190);    /* loc_2BA40    */
	ed->var_D4 = 1;
	ed->var_30 = 1;
	switch (ed_multiflag(ed->var_190)) {
	case 1:                                              /* loc_2BA98    */
		ed_set_elem((int16_t)(ed->var_C0 + 1), ed->var_DA, 0xFE);
		break;
	case 2:                                              /* loc_2BABC    */
		ed_set_elem(ed->var_C0, (int16_t)(ed->var_DA + 1), 0xFF);
		break;
	case 3:                                              /* loc_2BAE0    */
		ed_set_elem(ed->var_C0, (int16_t)(ed->var_DA + 1), 0xFF);
		ed_set_elem((int16_t)(ed->var_C0 + 1), ed->var_DA, 0xFE);
		ed_set_elem((int16_t)(ed->var_C0 + 1), (int16_t)(ed->var_DA + 1),
		            0xFD);
		break;
	default:
		break;
	}
}

int reditor_activate(struct REDITOR* ed)
{
	if (ed->var_34 == 0) {                               /* loc_2B8EE    */
		ed_place(ed);
		return RED_ACT_NONE;
	}
	if (ed->var_17F < 6) {                               /* loc_2B4AD    */
		/* pick the icon under the palette cursor */
		ed->var_190 = ed_pbox(ed, ed->var_C6, ed->var_17F, ed->var_18D);
		if (ed->var_C6 != 0) {
			/* loc_2B4E5: a two-tile shape picked while the map cursor is
			 * at the far edge pulls the map cursor back one */
			if ((ed_multiflag(ed->var_190) & 1) &&
			    (int16_t)ed->var_180 - (int16_t)ed->var_18C == 0x0A)
				ed->var_180--;
			if ((ed_multiflag(ed->var_190) & 2) &&
			    (int16_t)ed->var_18E - (int16_t)ed->var_8 == 0x0B)
				ed->var_18E--;
		}
		ed->var_34 = 0;                                  /* loc_2B53A    */
		return RED_ACT_NONE;
	}
	ed->var_30 = 1;                                      /* loc_2B544    */
	if (ed->var_17F == 6) {                              /* the page bar */
		ed->var_C6++;
		if (ed->var_C6 > 0x0A) ed->var_C6 = 1;
		return RED_ACT_NONE;
	}
	if (ed->var_17F == 7) return RED_ACT_HORIZON;        /* loc_2B566    */
	if (ed->var_17F == 8)
		return ed->var_18D != 0 ? RED_ACT_NEW : RED_ACT_LOAD;
	return ed->var_18D != 0 ? RED_ACT_EXIT : RED_ACT_SAVE;
}

/* ------------------------------------------------------------------ */
void reditor_set_horizon(struct REDITOR* ed, uint8_t horizon)
{
	td14_elem_map_main[0x384] = horizon;                 /* loc_2B5BA    */
	ed->var_D4 = 1;
	ed->var_30 = 1;
}

void reditor_new_track(struct REDITOR* ed, uint8_t terrain)
{
	char name[8];
	const uint8_t* ter;
	int16_t si;

	for (si = 0; si < 0x384; si++) td14_elem_map_main[si] = 0;

	/* loc_2B62E: `mov byte ptr aTer0+3, al` patches the resource name in
	 * place - "ter0".."ter4", 0x385 bytes each, straight over the terrain
	 * half.  The five presets are data, not code. */
	memcpy(name, "ter0", 5);
	name[3] = (char)('0' + terrain);
	ter = (const uint8_t*)locate_shape_alt(ed->tedit, name);
	if (ter)
		for (si = 0; si < 0x385; si++) td15_terr_map_main[si] = ter[si];

	gameconfig.game_trackname[0] = 0;                    /* loc_2B662    */
	ed->var_D4 = 1;
	ed->var_30 = 1;
}

int16_t reditor_check(struct REDITOR* ed)
{
	int16_t si = track_setup();                          /* loc_2B3EA    */
	ed->var_12 = (uint8_t)si;
	if (si > 1) {
		ed->var_34 = 0;
		if (track_pieces_counter == 0) {                 /* loc_2B3F2    */
			ed->var_18E = byte_45D90;
			ed->var_180 = byte_45E16;
		} else {                                         /* loc_2B448    */
			ed->var_18E = (uint8_t)td21_col_from_path[0];
			ed->var_180 = (uint8_t)td22_row_from_path[0];
			ed->var_190 = ed_elem(ed->var_180, ed->var_18E);
		}
	}
	return si;
}

/* ------------------------------------------------------------------ */
/* seg009 loc_2B725 / loc_2B817 - the whole 1802-byte track image.       */
/*                                                                      */
/* The read is file_read_fatal(g_path_buf, td14_elem_map_main) with no    */
/* length: fileio reads the whole file, and the file is 1802 bytes.  The  */
/* write is file_write_fatal(g_path_buf, td14_elem_map_main, 0x70A) -     */
/* 1802 - which is the writer this phase had to add, because nothing else */
/* in the game ever writes a track.                                       */
/* ------------------------------------------------------------------ */
int reditor_load_track(struct REDITOR* ed, const char* path)
{
	FILE* f = fopen(path, "rb");
	size_t n;
	if (!f) return 0;
	n = fread(td14_elem_map_main, 1, 0x70A, f);
	fclose(f);
	if (n != 0x70A) return 0;
	track_setup();
	ed->var_34 = 0;
	ed->var_180 = byte_45E16;
	ed->var_18E = byte_45D90;
	ed->var_D4 = 0;
	ed->var_30 = 1;
	return 1;
}

int reditor_save_track(struct REDITOR* ed, const char* path)
{
	FILE* f = fopen(path, "wb");
	size_t n;
	if (!f) return 0;
	n = fwrite(td14_elem_map_main, 1, 0x70A, f);
	fclose(f);
	if (n != 0x70A) return 0;
	ed->var_D4 = 0;                                      /* loc_2B88C    */
	return 1;
}

/* ------------------------------------------------------------------ */
/* seg009 loc_2B0E2 / loc_2B1B0 - the mouse.                            */
/* ------------------------------------------------------------------ */
static int16_t ed_hittest(int16_t mx, int16_t my)
{
	int16_t i;
	for (i = 0; i < 5; i++)
		if (trackmenu2_buttons_x1[i] <= mx && trackmenu2_buttons_x2[i] >= mx &&
		    trackmenu2_buttons_y1[i] <= my && trackmenu2_buttons_y2[i] >= my)
			return i;
	return -1;
}

/* `sar ax, 4` after `cwd / xor ax,dx / sub ax,dx` is a magnitude shift:
 * the original divides the distance from the region's origin by 16 with
 * the sign taken out and put back, so a click one pixel left of the
 * origin lands on tile 0, not on tile -1. */
static int16_t ed_div16(int16_t v)
{
	int16_t neg = v < 0;
	if (neg) v = (int16_t)-v;
	v = (int16_t)(v >> 4);
	return neg ? (int16_t)-v : v;
}

int reditor_click(struct REDITOR* ed, int16_t mx, int16_t my)
{
	int16_t region = ed_hittest(mx, my);
	int16_t var_D6, var_174;

	switch (region) {
	case 0:                                              /* loc_2AF70    */
		/* the horizontal scrollbar: 190 pixels for 30 columns */
		{
			int16_t v = (int16_t)(((int32_t)(mx - 9) * 0x1E) / 0xBE);
			if (v < 0) v = 0;
			if (v > 0x12) v = 0x12;
			ed->var_18E = (uint8_t)((int16_t)ed->var_18E + v - ed->var_8);
			ed->var_8 = (uint8_t)v;
		}
		return 1;
	case 1:                                              /* loc_2B042    */
		{
			int16_t v = (int16_t)(((int32_t)(my - 4) * 0x1E) / 0xB0);
			if (v < 0) v = 0;
			if (v > 0x13) v = 0x13;
			ed->var_180 = (uint8_t)((int16_t)ed->var_180 + v - ed->var_18C);
			ed->var_18C = (uint8_t)v;
		}
		return 1;
	case 2:                                              /* loc_2B088    */
		ed->var_34 = 1;
		ed->var_17F = 6;
		return 1;
	case 3:                                              /* loc_2B0E2    */
		var_D6  = ed_div16((int16_t)(mx - 8));
		var_174 = ed_div16((int16_t)(my - 4));
		if (ed->var_C6 != 0) {
			/* a two-tile shape reaching past the edge of the window is
			 * placed one tile back */
			if (var_174 == 0x0A && (ed_multiflag(ed->var_190) & 1))
				var_174--;
			if (var_D6 == 0x0B && (ed_multiflag(ed->var_190) & 2))
				var_D6--;
		}
		ed->var_34 = 0;                                  /* loc_2B15A    */
		ed->var_18E = (uint8_t)(var_D6 + ed->var_8);
		ed->var_180 = (uint8_t)(var_174 + ed->var_18C);
		return 1;
	case 4:                                              /* loc_2B1B0    */
		var_D6  = ed_div16((int16_t)(mx - 0xDC));
		var_174 = ed_div16((int16_t)(my - 0x24));
		if (var_174 < 6) {
			/* step off a continuation cell onto its owner */
			if (ed_pbox(ed, ed->var_C6, var_174, var_D6) == 0xFE) var_174--;
			if (ed_pbox(ed, ed->var_C6, var_174, var_D6) == 0xFF) var_D6--;
		} else {                                         /* loc_2B274    */
			var_174 = ed_div16((int16_t)(my - 0x1C));
			if (var_174 == 7)      var_D6 = 0;
			else if (var_D6 < 3)   var_D6 = 0;
			else                   var_D6 = 3;
		}
		if (var_D6 < 0) var_D6 = 0;
		if (var_174 < 0) var_174 = 0;
		ed->var_18D = (uint8_t)var_D6;
		ed->var_17F = (uint8_t)var_174;
		ed->var_34 = 1;
		return 1;
	default:
		return 0;
	}
}

/* ------------------------------------------------------------------ */
/* seg009 loc_2B382..loc_2BE3A - the keyboard.                          */
/* ------------------------------------------------------------------ */
int reditor_key(struct REDITOR* ed, int16_t key)
{
	int16_t si;
	uint8_t* col = ed->var_34 ? &ed->var_18D : &ed->var_18E;
	uint8_t* row = ed->var_34 ? &ed->var_17F : &ed->var_180;

	/* loc_2B356: F1..F10 jump straight to a palette page. */
	for (si = 0; si < 10; si++)
		if (word_3ECBE[si] == (uint16_t)key) {
			ed->var_C6 = (uint8_t)(si + 1);
			return ed->var_188 != 0;
		}

	switch ((uint16_t)key) {
	case 0x20:                                           /* loc_2B3B0    */
	case 0x5200:                                         /* Insert       */
		ed->var_34 ^= 1;
		break;
	case 0x2D:                                           /* loc_2B3B8    */
		if (ed->var_C6 > 1) ed->var_C6--;
		break;
	case 0x2B:                                           /* loc_2B3CA    */
		if (ed->var_C6 < 0x0A) ed->var_C6++;
		break;
	case 0x5400:                                         /* loc_2B3DC    */
		ed->var_C6 = 0;
		ed->var_190 = 0;
		break;
	case 0x63:                                           /* 'c'          */
	case 0x43:                                           /* 'C'          */
		reditor_check(ed);
		break;
	case 0x0D:                                           /* loc_2B49A    */
		reditor_activate(ed);
		break;
	case 0x4700:                                         /* Home         */
		/* loc_2BB46: in the palette it goes to the top row; on the map it
		 * jumps to the scroll origin, or back to 0,0 if it is there. */
		if (ed->var_34) { ed->var_17F = 0; ed->var_18D = 0; break; }
		if (ed->var_180 == ed->var_18C && ed->var_18E == ed->var_8) {
			ed->var_8 = 0;
			ed->var_18C = 0;
		}
		ed->var_180 = ed->var_18C;
		ed->var_18E = ed->var_8;
		break;
	case 0x4800:                                         /* up           */
		if (*row == 0) break;                            /* loc_2BB82    */
		ed->var_DA = 0xFF;
		(*row)--;
		if (ed->var_34 && ed->var_17F < 6) {
			int guard = 12;
			for (;;) {
				uint8_t v = ed_pbox(ed, ed->var_C6, ed->var_17F,
				                    ed->var_18D);
				if (v < 0xFE || guard-- <= 0) break;
				if (v == 0xFF) { if (ed->var_18D) ed->var_18D--; else break; }
				else           { if (ed->var_17F) ed->var_17F--; else break; }
			}
		}
		break;
	case 0x5000:                                         /* down         */
		if (*row >= byte_3ED00[ed->var_34]) break;       /* loc_2BC06    */
		ed->var_DA = 0xFF;
		(*row)++;
		if (ed->var_34 && ed->var_17F < 6) {             /* loc_2BC41    */
			uint8_t v = ed_pbox(ed, ed->var_C6, ed->var_17F, ed->var_18D);
			if (v == 0xFF)      { if (ed->var_18D) ed->var_18D--; }
			else if (v == 0xFE) { ed->var_17F++; }
		}
		break;
	case 0x4B00:                                         /* left         */
		if (ed->var_34 && ed->var_17F == 6) {            /* loc_2BC8A    */
			if (ed->var_C6 > 1) ed->var_C6--;
			break;
		}
		if (*col == 0) break;                            /* loc_2BC9A    */
		ed->var_DA = 0xFF;
		(*col)--;
		if (ed->var_34) {
			if (ed->var_17F > 5) { ed->var_18D = 0; break; }
			{
				int guard = 12;
				for (;;) {
					uint8_t v = ed_pbox(ed, ed->var_C6, ed->var_17F,
					                    ed->var_18D);
					if (v < 0xFE || guard-- <= 0) break;
					if (v == 0xFF) { if (ed->var_18D) ed->var_18D--;
					                 else break; }
					else           { if (ed->var_17F) ed->var_17F--;
					                 else break; }
				}
			}
		}
		break;
	case 0x4D00:                                         /* right        */
		if (ed->var_34 && ed->var_17F == 6) {            /* loc_2BD20    */
			if (ed->var_C6 < 0x0A) ed->var_C6++;
			break;
		}
		{
			int16_t var_178 = 1;                         /* loc_2BD30    */
			if (ed->var_34) {
				if (ed->var_17F > 5) {
					var_178 = 3;                         /* the four     */
				} else {
					/* loc_2BD4E: step over as many continuation cells as
					 * the icon to the right is wide */
					int guard = 8;
					for (;;) {
						uint8_t v = ed_pbox(ed, ed->var_C6, ed->var_17F,
						                    (int16_t)(ed->var_18D + var_178));
						if (v < 0xFE || guard-- <= 0) break;
						if (v == 0xFF)      var_178++;
						else if (ed->var_17F) ed->var_17F--;
						else break;
						if (ed->var_18D + var_178 >= byte_3ECFE[1]) break;
					}
				}
			}
			if (*col + var_178 >= byte_3ECFE[ed->var_34]) break;
			ed->var_DA = 0xFF;                           /* loc_2BDF3    */
			*col = (uint8_t)(*col + var_178);
		}
		break;
	default:
		break;
	}
	return ed->var_188 != 0;
}

/* ------------------------------------------------------------------ */
/* STUNTS_EDITOR_SHOT=<path> - open the editor on whatever track is      */
/* loaded, draw one frame and write it out.  The palette comes from the  */
/* caller because it belongs to the host, not to the renderer.           */
/* ------------------------------------------------------------------ */
struct ed_pal_rgba { uint8_t r, g, b, a; };

static void reditor_write_bmp(const char* path, const void* pal)
{
	const struct ed_pal_rgba* colors = (const struct ed_pal_rgba*)pal;
	uint32_t rowb = (uint32_t)RFB_VIEW_W * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)RFB_VIEW_H;
	uint8_t hdr[54];
	int32_t w = RFB_VIEW_W, h = RFB_VIEW_H;
	uint32_t fsz = 54 + img;
	uint8_t* buf = (uint8_t*)calloc(1, img);
	FILE* f;
	int y, x;

	if (!buf) return;
	memset(hdr, 0, sizeof hdr);
	for (y = 0; y < RFB_VIEW_H; y++) {
		uint8_t* row = buf + (uint32_t)(RFB_VIEW_H - 1 - y) * stride;
		for (x = 0; x < RFB_VIEW_W; x++) {
			struct ed_pal_rgba c =
				colors[rfb_pixels[(int32_t)y * RFB_VIEW_W + x]];
			row[x * 3 + 0] = c.b;
			row[x * 3 + 1] = c.g;
			row[x * 3 + 2] = c.r;
		}
	}
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4); hdr[10] = 54; hdr[14] = 40;
	memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
	hdr[26] = 1; hdr[28] = 24; memcpy(hdr + 34, &img, 4);
	f = fopen(path, "wb");
	if (f) { fwrite(hdr, 1, 54, f); fwrite(buf, 1, img, f); fclose(f); }
	free(buf);
	printf("skrev %s\n", path);
}

int reditor_shot(const void* pal)
{
	const char* path = getenv("STUNTS_EDITOR_SHOT");
	static struct REDITOR ed;
	if (!path) return 0;
	if (!reditor_open(&ed)) {
		fprintf(stderr, "banredigeraren: kan inte ladda SDTEDIT/TEDIT\n");
		return 0;
	}
	ed.blink = 1;
	reditor_update(&ed);
	reditor_draw(&ed);
	reditor_write_bmp(path, pal);
	reditor_close(&ed);
	return 1;
}
