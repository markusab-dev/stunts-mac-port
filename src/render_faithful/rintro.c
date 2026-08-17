/*
 * rintro.c - the title sequence and the credits.
 *
 * Ported from reference/restunts/src/restunts/asm/seg000.asm:
 *
 *   run_intro_looped        641.. 741   (100 lines)
 *   run_intro               742.. 827   ( 85 lines)
 *   load_intro_resources    828..1569   (741 lines)
 *
 * plus the four small helpers they need, which had no port yet:
 *
 *   input_repeat_check      seg008.asm  3415..3450
 *   input_do_checking       seg008.asm  2457..     (the SDL stand-in)
 *   sprite_1_unk2           seg012.asm 10825..10873 (falls into sprite_1_unk)
 *   sprite_blit_to_video    seg008.asm  3992..4066
 *
 * The 3D animation between them is in rintro3d.c.
 *
 * ------------------------------------------------------------------------
 * WHAT THE SEQUENCE IS
 *
 * run_intro_looped is four steps, each abortable, each feeding its return
 * value to the next:
 *
 *   1. the title song  - file_load_audiores("skidtitl","skidms","TITL")
 *   2. run_intro       - two full-screen stills out of SDTITL.PVS
 *   3. setup_intro     - the 3D logo animation (rintro3d.c)
 *   4. load_intro_resources - the credits page out of SDCRED.PES + CRED.RES
 *
 * The name of step 4 is a lie: it loads nothing anyone else uses and it is
 * the entire credits screen, animation included.
 *
 * ------------------------------------------------------------------------
 * THE TWO STILLS (run_intro)
 *
 * SDTITL.PVS holds exactly four resources:
 *
 *     !cg0 256x1     the CGA palette map
 *     !eg0  64x4     the EGA one
 *     prod 176x75    at (64,56), s2d_unk5 = 0x14 -> TRANSPOSED
 *     titl 320x200   at (0,0),   s2d_unk5 = 0x04 -> not transposed
 *
 * "prod" is the Broderbund/DSI producer logo on a cleared screen; "titl" is
 * the full-screen Stunts title.  Each is shown for input_repeat_check(0x190)
 * = 400 ticks = four seconds at the original's 100 Hz timer.
 *
 * `prod` carries the flip nibble, so it MUST go through unflip_shape() -
 * this is the third shape in the project to do so, after "gbox" and the
 * showroom backdrop, and drawing it raw gives the same diagonal streaks.
 *
 * [ODDITY] seg000:757 reads `prod`'s s2d_pos_y and sets waitflag to 0xA0
 * when it is non-zero and 0xB4 when it is zero - and then nothing in the
 * intro ever reads waitflag.  Its only reader in the whole game is
 * show_waiting (seg008:4073), which passes it to show_dialog as the "please
 * wait" box's width.  So the 160/180 is NOT how long the still is shown;
 * both stills are shown for 400 ticks.  The assignment is reproduced
 * anyway, because the value survives into the next dialog the game opens.
 *
 * [ODDITY] seg000:769 looks "prod" up a second time, through a second copy
 * of the same string literal (aProd / aProd_0).  Reproduced as written.
 *
 * ------------------------------------------------------------------------
 * THE CREDITS (load_intro_resources)
 *
 * Two archives:
 *
 *   CRED.RES    20 plain NUL-terminated strings - six under the language
 *               prefix ('e' for English) fetched with locate_text_res, and
 *               fourteen "g"-prefixed names fetched with locate_shape_alt.
 *               Decoded from the shipped file: ecre "Created by:",
 *               edes "Design:", emus "Music & Sound Fx:", epro
 *               "Programming:", eopr "Technical Support:", eart "Art:";
 *               gds0 "Distinctive Software Inc.", gds1 "Vancouver B.C.",
 *               gdon "Don Mattrick", gkev "Kevin Pickell", gbra "Brad
 *               Gour", grob "Rob Martyn", gsta "Stan Chow", gmsy "Michael
 *               J. Sokyrka", gkri "Kris Hatlelid", gbri "Brian Plank",
 *               gric "Rick Friesen", gmsm "Mike Smith", gdav "David
 *               Adams", gnic "Nicola Swaine".
 *
 *   SDCRED.PES  eleven shapes - see rpes.c for the container.  `arrw` is
 *               the arrow that slides in from the right, `arw1`..`arw8` are
 *               the eight frames it turns into, `arow` and `type` are the
 *               finished picture, and `!cg0` is the CGA map.
 *
 * Every line goes through copy_string into the 0xAC74 scratch buffer and
 * then intro_draw_text, which draws the string twice: a shadow at (x+1,y+1)
 * in one colour and the text at (x,y) in another.  The 23 lines and their
 * six colour pairs (word_407D4..word_407EA in dseg) are the table below.
 *
 * [ODDITY] seg000:1043, the "gsta" line, calls copy_string with the literal
 * 0AC74h where every other line names `resID_byte1`.  Those are the same
 * address: resID_byte1 is at dseg offset 0xAC74 (computed by summing every
 * data definition ahead of it in dseg.asm).  A disassembly artefact, not a
 * bug - "Stan Chow" really is drawn.
 *
 * Then the animation, in three parts:
 *
 *   1. `arrw` slides from x = 0x14A (330) to x = 40 at 2 pixels per tick,
 *      each step erasing a 32-pixel-wide column behind it (sprite_1_unk2,
 *      colour 0).  40 and 170 are `arrw`'s OWN s2d_pos - the data carries
 *      the layout, as it does everywhere in this game.
 *   2. `arw1`..`arw8` play at 5 ticks a frame inside the window
 *      (0, 320, arow.s2d_pos_y = 132, 200), cleared to colour 0 each frame.
 *   3. `arow` and `type` are drawn as the final picture and the screen
 *      waits for input_repeat_check(0x1F4) = 500 ticks.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] - stated plainly.  There is no oracle for a title screen, so
 * this is behaviour-exact, not instruction-exact, in exactly these places:
 *
 *  1. No sprite windows.  The original draws into an offscreen
 *     sprite_make_wnd(0x140, 0xC8, 0x0F) and copies rectangles of it to the
 *     screen; this port has one framebuffer, as every ported screen does.
 *     Where the original clears a window, draws unclipped into it and then
 *     copies a sub-rectangle out, this port sets the same sub-rectangle as
 *     the clip window and draws clipped - the same pixels reach the screen.
 *     Consequently sprite_copy_wnd_to_1, sprite_copy_2_to_1_2,
 *     sprite_putimage(wnd) and sprite_clear_shape(wnd) are all no-ops here,
 *     and sprite_clear_shape - which despite its name COPIES THE SCREEN
 *     BACK INTO the window's bitmap (seg012:13503, `rep movsb` from
 *     sprite1's bitmap into the shape) - is not needed at all.
 *
 *  2. sprite_blit_to_video's dissolve is a plain present.  With a mode
 *     other than 0xFFFE the original runs four passes of sprite_1_unk3
 *     (seg012:10985), a sparse copy that reveals every twelfth row and one
 *     pixel in four along it, so the picture fades in.  That is a copy from
 *     the offscreen window to the screen; with a single framebuffer both
 *     ends are the same bytes and the four passes are the identity.  The
 *     input polling and the return value ARE kept.
 *
 *  3. The timer is derived from SDL_GetTicks at the original's 100 Hz
 *     (word_4499C = 100 / framespersec, restunts.c:464), and
 *     input_do_checking is an SDL event poll returning 27 for Escape and 1
 *     for anything else.  There is no DOS mouse.
 *
 *  4. mouse_draw_opaque_check / mouse_draw_transparent_check bracket every
 *     drawing burst in the original because the DOS mouse cursor is drawn
 *     into the same buffer.  They are not called here.
 */
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "rfbsize.h"
#include "shape2d.h"
#include "rintro.h"
#include "rpes.h"
#include "../music_native.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;
extern void  shape2d_op_unk3(void far* shape);
extern void far* unflip_shape(void far* shape);
extern void* file_load_resource(int16_t restype, const char* filename);
extern void* file_load_resfile(const char* filename);
extern void  unload_resource(void far* resptr);
extern void* locate_shape_fatal(void* resptr, const char* shapename);
extern void* locate_shape_alt(void* resptr, const char* shapename);
extern char far* locate_text_res(char far* res, char* key);
extern struct RECTANGLE* intro_draw_text(char* str, int16_t x, int16_t y,
                                         int16_t colour_text,
                                         int16_t colour_shadow);
extern void font_set_fontdef(void);
extern char resID_byte1[32];

/* Two dseg globals externs.h declares and nothing had yet defined.
 * dseg.asm:33829 waitflag (show_waiting's dialog width, see the [ODDITY]
 * above) and dseg.asm:36122 slow_video_mgmt (the graphics-detail menu's
 * "slow video" switch, 0 in every configuration this port has). */
int16_t waitflag;
uint16_t slow_video_mgmt;

/* ------------------------------------------------------------------ */
/* The host: SDL window, timer, input                                  */
/* ------------------------------------------------------------------ */
static SDL_Window*   s_win;
static SDL_Renderer* s_ren;
static SDL_Texture*  s_tex;
static const stunts_palette_t* s_pal;
static uint32_t s_rgba[RFB_VIEW_W * RFB_VIEW_H];
static uint32_t s_last_ms;
static uint32_t s_carry_ms;      /* the sub-tick remainder, so no time is lost */

void rintro_present(void)
{
	int i;
	if (!s_ren || !s_tex) return;
	for (i = 0; i < RFB_VIEW_W * RFB_VIEW_H; i++) {
		stunts_color_rgba_t c = s_pal->colors[rfb_pixels[i]];
		s_rgba[i] = 0xFF000000u | ((uint32_t)c.r << 16)
		          | ((uint32_t)c.g << 8) | c.b;
	}
	SDL_UpdateTexture(s_tex, NULL, s_rgba, RFB_VIEW_W * 4);
	SDL_RenderClear(s_ren);
	SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
	SDL_RenderPresent(s_ren);
}

/* restunts.c:73 timer_get_delta, on a 100 Hz timer. */
int16_t rintro_delta(void)
{
	uint32_t now = SDL_GetTicks();
	uint32_t ms, ticks;
	if (s_last_ms == 0) { s_last_ms = now; return 0; }
	ms = now - s_last_ms + s_carry_ms;
	ticks = ms / 10;
	s_carry_ms = ms % 10;
	s_last_ms = now;
	if (ticks > 0x7FFF) ticks = 0x7FFF;
	return (int16_t)ticks;
}

/* seg008 input_do_checking, as much of it as a native port has: the DOS
 * version also services the mouse, the joystick and the audio driver's
 * timer, none of which exist here. */
int16_t rintro_input(int16_t ticks)
{
	SDL_Event ev;
	int16_t r = 0;
	(void)ticks;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) {
			SDL_Event q = ev;
			SDL_PushEvent(&q);   /* the caller's loop must still see it */
			return 27;
		}
		if (ev.type == SDL_KEYDOWN) {
			if (ev.key.keysym.sym == SDLK_ESCAPE) return 27;
			r = 1;
		} else if (ev.type == SDL_MOUSEBUTTONDOWN) {
			r = 1;
		}
	}
	return r;
}

/*
 * seg008 input_repeat_check(arg_0):
 *     di = 0; timer_get_delta_alt();
 *     while (arg_0 > di) { d = timer_get_delta_alt(); di += d;
 *                          si = input_do_checking(d); if (si) return si; }
 *     return 0;
 * The original spins; this one presents a frame each time round, which is
 * what paces it (vsync) and what keeps the window responsive.
 */
int16_t rintro_repeat_check(int16_t ticks)
{
	int16_t di = 0;
	rintro_delta();
	while (ticks > di) {
		int16_t d, si;
		rintro_present();
		d = rintro_delta();
		di = (int16_t)(di + d);
		si = rintro_input(d);
		if (si) return si;
	}
	return 0;
}

/*
 * seg008 sprite_blit_to_video(sprite, mode).  See [DEVIATION] 2: the
 * dissolve degenerates, the polling does not.  mode 0xFFFE is the plain
 * copy; every other value runs the four passes.
 */
static int16_t rintro_blit_to_video(int16_t mode)
{
	int16_t si;
	rintro_present();
	if (mode == (int16_t)0xFFFE) return 0;
	for (si = 0; si < 4; si++) {
		int16_t d = rintro_delta();
		int16_t di = rintro_input(d);
		if (di) { rintro_present(); return di; }
		rintro_present();
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* seg012 sprite_1_unk2 (10825..10873): the clipped filled rectangle.   */
/* The proc is only the clip prologue; it falls through into            */
/* sprite_1_unk's body at loc_335D7, which is the unclipped fill.       */
/* [DEVIATION] the rectangle is in the original's 320x200 coordinates,  */
/* so it scales by RFB_SCALE like every other 2D primitive here.        */
/* ------------------------------------------------------------------ */
static void sprite_1_unk2(int16_t x, int16_t y, int16_t w, int16_t h,
                          int16_t colour)
{
	int16_t ax, row, col;

	x = (int16_t)(x * RFB_SCALE); y = (int16_t)(y * RFB_SCALE);
	w = (int16_t)(w * RFB_SCALE); h = (int16_t)(h * RFB_SCALE);

	ax = (int16_t)((int16_t)sprite1.sprite_left - x);        /* loc_33580 */
	if (ax > 0) {
		x = (int16_t)sprite1.sprite_left;
		w = (int16_t)(w - ax);
		if (w <= 0) return;
	}
	ax = (int16_t)(x + w - (int16_t)sprite1.sprite_right);   /* loc_33591 */
	if (ax > 0) { w = (int16_t)(w - ax); if (w <= 0) return; }
	ax = (int16_t)((int16_t)sprite1.sprite_top - y);         /* loc_335A3 */
	if (ax > 0) {
		h = (int16_t)(h - ax);
		if (h <= 0) return;
		y = (int16_t)sprite1.sprite_top;
	}
	ax = (int16_t)(y + h - (int16_t)sprite1.sprite_height);  /* loc_335B8 */
	if (ax > 0) { h = (int16_t)(h - ax); if (h <= 0) return; }

	if (w <= 0 || h <= 0) return;                            /* loc_335D7 */
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > RFB_VIEW_W) w = (int16_t)(RFB_VIEW_W - x);
	if (y + h > RFB_VIEW_H) h = (int16_t)(RFB_VIEW_H - y);
	if (w <= 0 || h <= 0) return;
	for (row = y; row < y + h; row++)
		for (col = x; col < x + w; col++)
			rfb_pixels[(int32_t)row * RFB_VIEW_W + col] = (uint8_t)colour;
}

/* ------------------------------------------------------------------ */
/* One 24-bit BMP of the framebuffer, for the STUNTS_*_SHOT hooks.     */
/* ------------------------------------------------------------------ */
void rintro_write_bmp(const char* path)
{
	uint32_t rowb = (uint32_t)RFB_VIEW_W * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)RFB_VIEW_H;
	uint8_t hdr[54] = {0};
	int32_t w = RFB_VIEW_W, h = RFB_VIEW_H;
	uint32_t fsz = 54 + img;
	uint8_t* buf = (uint8_t*)calloc(1, img);
	FILE* f;
	int y, x;
	if (!buf) return;
	for (y = 0; y < RFB_VIEW_H; y++) {
		uint8_t* row = buf + (uint32_t)(RFB_VIEW_H - 1 - y) * stride;
		for (x = 0; x < RFB_VIEW_W; x++) {
			stunts_color_rgba_t c =
				s_pal->colors[rfb_pixels[(int32_t)y * RFB_VIEW_W + x]];
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

/* ------------------------------------------------------------------ */
/* seg000 run_intro (742..827): the two stills                          */
/* ------------------------------------------------------------------ */
static int16_t run_intro(void far* tempdataptr)
{
	int16_t si;
	struct SHAPE2D far* shape;
	const char* shot;

	/* mouse_draw_opaque_check / sprite_copy_2_to_1_clear /
	 * mouse_draw_transparent_check / sprite_copy_wnd_to_1_clear */
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	sprite_clear_1_color(0);

	/* seg000:753 - the first lookup exists only to read s2d_pos_y. */
	shape = (struct SHAPE2D far*)locate_shape_fatal(tempdataptr, "prod");
	waitflag = shape->s2d_pos_y != 0 ? 0xA0 : 0xB4;   /* [ODDITY], see top */

	shape = (struct SHAPE2D far*)locate_shape_fatal(tempdataptr, "prod");
	unflip_shape(shape);           /* `prod` IS transposed - house rule    */
	shape2d_op_unk3(shape);        /* == sprite_shape_to_1_alt, clipped    */
	shot = getenv("STUNTS_INTRO_PROD_SHOT");
	if (shot) { rintro_present(); rintro_write_bmp(shot); exit(0); }

	si = rintro_blit_to_video((int16_t)0xFFFF);
	if (si) return si;
	si = rintro_repeat_check(0x190);
	if (si) return si;

	/* seg000:786 - sprite_copy_wnd_to_1_clear before the second still. */
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	sprite_clear_1_color(0);
	waitflag = 0xB4;
	shape = (struct SHAPE2D far*)locate_shape_fatal(tempdataptr, "titl");
	unflip_shape(shape);
	shape2d_op_unk3(shape);
	shot = getenv("STUNTS_INTRO_TITL_SHOT");
	if (shot) { rintro_present(); rintro_write_bmp(shot); exit(0); }

	si = rintro_blit_to_video((int16_t)0xFFFF);
	if (si) return si;
	return rintro_repeat_check(0x190);
}

/* ------------------------------------------------------------------ */
/* seg000 load_intro_resources (828..1569): the credits                 */
/* ------------------------------------------------------------------ */

/* The 23 intro_draw_text calls, in the order seg000 makes them.
 * `text` selects locate_text_res (which prepends the language byte) over
 * locate_shape_alt.  The colours are word_407D4..word_407EA in dseg. */
struct credit_line {
	const char* key;
	uint8_t     text;      /* 1 = locate_text_res, 0 = locate_shape_alt */
	int16_t     x, y;
	int16_t     col, shadow;
};
static const struct credit_line s_credits[23] = {
	{ "cre",  1, 0x78, 0x00, 11,  3 },   /* seg000  878 */
	{ "gds0", 0, 0x3C, 0x0C, 15,  8 },   /*         903 */
	{ "gds1", 0, 0x68, 0x14, 15,  8 },   /*         928 */
	{ "des",  1, 0x14, 0x20, 12,  4 },   /*         953 */
	{ "gdon", 0, 0x14, 0x2C, 15,  8 },   /*         978 */
	{ "gkev", 0, 0x14, 0x34, 15,  8 },   /*        1003 */
	{ "gbra", 0, 0x14, 0x3C, 15,  8 },   /*        1018 */
	{ "grob", 0, 0x14, 0x44, 15,  8 },   /*        1033 */
	{ "gsta", 0, 0x14, 0x4C, 15,  8 },   /*        1043 [ODDITY] 0AC74h */
	{ "mus",  1, 0x14, 0x5C, 13,  5 },   /*        1068 */
	{ "gmsy", 0, 0x14, 0x68, 15,  8 },   /*        1083 */
	{ "gkri", 0, 0x14, 0x70, 15,  8 },   /*        1098 */
	{ "gbri", 0, 0x14, 0x78, 15,  8 },   /*        1113 */
	{ "pro",  1, 0xAC, 0x20,  9,  1 },   /*        1128 */
	{ "gkev", 0, 0xAC, 0x2C, 15,  8 },   /*        1143 */
	{ "opr",  1, 0xAC, 0x38,  9,  1 },   /*        1158 */
	{ "gbra", 0, 0xAC, 0x40, 15,  8 },   /*        1173 */
	{ "gric", 0, 0xAC, 0x48, 15,  8 },   /*        1188 */
	{ "art",  1, 0xAC, 0x54, 10,  2 },   /*        1203 */
	{ "gmsm", 0, 0xAC, 0x60, 15,  8 },   /*        1218 */
	{ "gdav", 0, 0xAC, 0x68, 15,  8 },   /*        1233 */
	{ "gnic", 0, 0xAC, 0x70, 15,  8 },   /*        1248 */
	{ "gkev", 0, 0xAC, 0x78, 15,  8 },   /*        1263 */
};

/* seg008 copy_string(dst, src) - strcpy into the 0xAC74 scratch area. */
static void copy_string(char* dst, const char far* src)
{
	if (!src) { dst[0] = 0; return; }
	snprintf(dst, sizeof resID_byte1, "%s", (const char*)src);
}

static int16_t load_intro_resources(void far* sdcred)
{
	void far* cred;
	void far* shapes[12];        /* var_34..var_C, eleven entries + guard  */
	int16_t var_2, var_4, var_3E, var_44, var_40, var_46, var_36;
	int16_t si, di, i;
	int16_t slideshot = -1;
	const char* shot;
	const char* animdir;

	memset(shapes, 0, sizeof shapes);

	cred = file_load_resfile("cred");                     /* seg000:848 */
	pes_locate_many(sdcred,
	                "arowarrwarw1arw2arw3arw4arw5arw6arw7arw8type",
	                shapes);                              /* seg000:857 */
	waitflag = 0x96;
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	sprite_clear_1_color(0);                              /* wnd_to_1_clear */

	if (!shapes[0] || !shapes[1] || !shapes[10]) {
		fprintf(stderr, "eftertexter: SDCRED.PES saknar former\n");
		return 1;
	}

	/* seg000:869 - the layout comes out of `arrw`'s own header. */
	{
		const struct SHAPE2D far* arrw = (const struct SHAPE2D far*)shapes[1];
		var_2  = (int16_t)arrw->s2d_pos_x;                /* 40  */
		var_4  = (int16_t)arrw->s2d_pos_y;                /* 170 */
		var_3E = (int16_t)(arrw->s2d_width * video_flag1_is1);
		var_44 = (int16_t)arrw->s2d_height;               /* 29  */
	}

	/* seg000:200 selected FONTDEF at start-up and nothing has changed it. */
	font_set_fontdef();

	for (i = 0; i < 23; i++) {
		const struct credit_line* c = &s_credits[i];
		char far* p = c->text
			? locate_text_res((char far*)cred, (char*)c->key)
			: (char far*)locate_shape_alt(cred, c->key);
		copy_string(resID_byte1, p);
		intro_draw_text(resID_byte1, c->x, c->y, c->col, c->shadow);
	}

	unload_resource(cred);                                /* seg000:1278 */

	shot = getenv("STUNTS_CREDITS_SHOT");
	if (shot) { rintro_present(); rintro_write_bmp(shot); exit(0); }

	var_46 = rintro_blit_to_video((int16_t)0xFFFF);        /* seg000:1284 */
	rintro_delta();
	si = 0x14A;

	animdir = getenv("STUNTS_CREDITS_ANIM");

	/* ---- 1. the arrow slides in, seg000 loc_10D77 / loc_10DA0 ------ */
	for (;;) {
		var_40 = rintro_delta();
		si = (int16_t)(si - var_40 * 2);
		/* seg000:1305 `cmp [bp+var_2], si / jle loc_10DA0` - draw while the
		 * arrow is still RIGHT of its resting place.  Written the other way
		 * round first, and the slide then finished in one frame; the
		 * animation screenshots are what caught it. */
		if (var_2 <= si) {
			sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
			/* sprite_putimage_and_alt(shape, x, y): despite the name a
			 * plain copy at a caller-given position - see the note in
			 * rskybox.c.  shape2d_op_unk3 draws at the shape's own
			 * position, so place it by hand. */
			{
				struct SHAPE2D far* a = (struct SHAPE2D far*)shapes[1];
				uint16_t ox = a->s2d_pos_x, oy = a->s2d_pos_y;
				a->s2d_pos_x = (uint16_t)si;
				a->s2d_pos_y = (uint16_t)var_4;
				shape2d_op_unk3(a);
				a->s2d_pos_x = ox; a->s2d_pos_y = oy;
			}
			sprite_1_unk2((int16_t)(var_3E + si), var_4, 0x20, var_44, 0);
			rintro_present();
			if (animdir && si / 60 != slideshot) {
				char p[600];
				slideshot = si / 60;
				snprintf(p, sizeof p, "%s/credits_slide%d.bmp",
				         animdir, slideshot);
				rintro_write_bmp(p);
			}
			var_46 = rintro_input(var_40);
			if (var_46) break;
		} else {
			break;
		}
	}

	/* ---- 2. the eight frames, seg000 loc_10D88 / loc_10DEC --------- */
	var_4 = (int16_t)((const struct SHAPE2D far*)shapes[0])->s2d_pos_y; /* 132 */
	var_36 = 0;
	si = 0;
	for (di = 2; di < 0x0A && var_46 == 0; di++) {
		sprite_set_1_size(0, RFB_VIEW_W, (uint16_t)(var_4 * RFB_SCALE),
		                  RFB_VIEW_H);
		sprite_clear_1_color(0);
		if (shapes[di]) {
			unflip_shape(shapes[di]);   /* house rule; a no-op here, see rpes.c */
			shape2d_op_unk3(shapes[di]);
		}
		rintro_present();
		if (animdir) {
			char p[600];
			snprintf(p, sizeof p, "%s/credits_arw%d.bmp", animdir, di - 1);
			rintro_write_bmp(p);
		}
		var_36 = (int16_t)(var_36 + 5);
		/* [ODDITY] loc_10E66 does NOT leave this wait when a key arrives -
		 * it reassigns var_46 every time round, so a press during the five
		 * ticks is lost unless it is the last thing that happened.  Only
		 * loc_10E83, after the wait, looks at it.  Reproduced. */
		while (var_36 > si) {                             /* loc_10E66 */
			var_40 = rintro_delta();
			var_46 = rintro_input(var_40);
			si = (int16_t)(si + var_40);
			rintro_present();
		}
	}

	/* ---- 3. the finished picture, seg000 loc_10E91 ----------------- */
	sprite_set_1_size(0, RFB_VIEW_W, (uint16_t)(var_4 * RFB_SCALE), RFB_VIEW_H);
	sprite_clear_1_color(0);
	unflip_shape(shapes[0]);
	shape2d_op_unk3(shapes[0]);          /* arow */
	unflip_shape(shapes[10]);
	shape2d_op_unk3(shapes[10]);         /* type */
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	if (animdir) {
		char p[600];
		snprintf(p, sizeof p, "%s/credits_final.bmp", animdir);
		rintro_present();
		rintro_write_bmp(p);
		exit(0);
	}
	var_46 = rintro_blit_to_video(0);
	if (var_46) return 1;
	if (rintro_repeat_check(0x1F4)) return 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* seg000 run_intro_looped (641..741)                                   */
/* ------------------------------------------------------------------ */
int rintro_run_looped(struct SDL_Window* win, struct SDL_Renderer* ren,
                     struct SDL_Texture* tex, const stunts_palette_t* pal,
                     const char* data_dir)
{
	void far* tempdataptr;
	int16_t si;

	s_win = (SDL_Window*)win;
	s_ren = (SDL_Renderer*)ren;
	s_tex = (SDL_Texture*)tex;
	s_pal = pal;
	s_last_ms = 0;
	s_carry_ms = 0;
	(void)s_win;

	/* seg000:648 file_load_audiores("skidtitl", "skidms", "TITL").  Phase 8
	 * replaced the DOS driver; music_native_init is idempotent and the play
	 * is silent when no audio device is open (see rintro.h). */
	music_native_init(data_dir, 44100);
	music_native_play(MUSIC_SONG_TITLE);

	tempdataptr = file_load_resource(2, "sdtitl.pvs");     /* seg000:660 */
	if (!tempdataptr) {
		fprintf(stderr, "intro: kan inte ladda SDTITL.PVS\n");
		music_native_stop();
		return 0;
	}
	/* sprite_make_wnd(0x140, 0xC8, 0x0F) - no windows here, [DEVIATION] 1 */
	si = run_intro(tempdataptr);
	unload_resource(tempdataptr);                          /* seg000:686 */

	if (si == 0) {
		si = rintro_setup_intro(data_dir);                 /* seg000:692 */
		if (si == 0) {
			void far* sdcred = file_load_resource_pes("sdcred");
			if (sdcred) {
				sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
				sprite_clear_1_color(0);
				rintro_blit_to_video(0);
				si = load_intro_resources(sdcred);
				pes_release(sdcred);
				unload_resource(sdcred);
			} else {
				fprintf(stderr, "intro: kan inte ladda SDCRED.PES\n");
			}
		}
	}

	music_native_stop();                                   /* audio_unload */
	return si;
}
