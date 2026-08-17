/*
 * rendscreen.c - the post-race results screen.
 *
 * Ported from reference/restunts/src/restunts/asm/seg000.asm:
 *
 *   end_hiscore            5126..7089   (1942 lines)
 *
 * plus the four shared widgets it needs, which live in rwidgets.c, and the
 * high-score family Phase 3 already ported into rhighscore.c.  restunts2
 * carries the same body as `end_hiscore_asm_` (1922 lines) and was checked
 * against this transcription; it adds nothing here.
 *
 * ------------------------------------------------------------------------
 * WHAT THE SCREEN IS
 *
 * Two bevelled panels, drawn by draw_button with no label:
 *
 *     draw_button(NULL, 0, 0,    0x140, 0x64, 15, 8, 7, 0)   rows   0..100
 *     draw_button(NULL, 0, 0x65, 0x140, 0x63, 15, 8, 7, 0)   rows 101..200
 *
 * The LOWER panel holds the statistics, centred, starting at y = 0x6B (107)
 * and stepping 10 pixels a line.  Every label is a MISC.PRE text resource
 * under the language prefix, and every line is assembled in the 0xAC74
 * scratch area by copy_string / strcat / format_frame_as_string:
 *
 *     eelt "Elapsed time: "  + (total - penalty), or + ednf "DNF"
 *                            + econ " (cont)" when byte_43966 bit 1 is set
 *     eppt "Penalty time: "  + penalty              (only when non-zero)
 *     eowt "Opponent Winning time: " / eolt "Opponent time: " + field_144,
 *                            or eolt + ednf         (only with an opponent)
 *     eavs "Average speed: " + travDist/(pEndFrame+elapsed_time1) >> 8 + emph
 *     eimp "Impact speed: "  + impactSpeed >> 8 + emph  (only when non-zero)
 *     etop "Top speed: "     + topSpeed    >> 8 + emph
 *     ejum "Jumps: "         + jumpCount                (only when non-zero)
 *
 * The UPPER panel holds one of two pages, toggled by the leftmost button:
 *
 *   * the evaluation - the opponent's portrait on the right at
 *     x = 0x138 - width, y = (0x63 - height) / 2, inside a draw_lines_unk
 *     frame three pixels out, and up to three lines of taunt on the left,
 *     word-wrapped in FONTN into (portrait_x - 16) pixels starting at (8,8);
 *   * the fastest-times table, highscore_text_unk() from rhighscore.c.
 *
 * and four buttons across the bottom at y = 0xAF, w = 0x46, h = 0x15, from
 * the dseg tables word_3BCEC (x1 = 4, 84, 164, 244) and word_3BCF6
 * (x2 = 75, 155, 235, 315), with hiscore_buttons_y1/_y2 = 174/197:
 *
 *     0  ebev "View]Eval" / ebhi "View]High"   only when there is both an
 *                                              opponent and a usable table
 *     1  ebrp "View]Replay"
 *     2  ebra "Race" (with an opponent) / ebdr "Drive"
 *     3  ebmm "Main]Menu"
 *
 * When button 0 is suppressed the other three shift left by 36 pixels
 * (var_9C = 0xFFDC), which is what centres them.  end_hiscore returns
 * selectedmenu - 1, i.e. 0 = view replay, 1 = race again, 2 = main menu;
 * seg000:104C0 maps those onto "replay", "race the same track again" and
 * "back to the main menu".
 *
 * ------------------------------------------------------------------------
 * THE PORTRAIT AND THE TAUNTS ARE DATA, AND THIS IS THE FORMAT
 *
 * `winn` and `lose` in OPP<n>.PRE are frame-index scripts: a NUL-terminated
 * run of 1-based frame numbers.  Each byte plus '0' patches the fourth
 * character of the resource name "op01", and locate_shape_fatal pulls that
 * frame out of OPP<n>WIN.PVS / OPP<n>LOSE.PVS.  Decoded from the shipped
 * files:
 *
 *     OPP1 winn 1 1 1 1 2 3 3 3 3
 *          lose 1 1 1 1 2 3
 *     OPP2 winn 1 1 1 1 2 3 4 5 6 6 6 6
 *          lose 1 1 1 1 2 3 4 5 6 6 6 6
 *     OPP3 winn 1 1 1 1 2 3 4 5 6 7 7 7 7
 *          lose 1 1 1 1 2 3 4 4 4
 *     OPP4 winn 1 1 1 1 2 3 4 5 6 7 8 8 8 8
 *          lose 1 1 1 1 2 3 4 5 6 5 6 5 4 3 2 2 2 2
 *     OPP5 winn 1 1 1 1 2 3 4 5 6 7 8 8 8 8
 *          lose 1 1 1 1 2 3 4 5 6 7 7 7 7
 *     OPP6 winn 1 1 1 1 2 3 4 5 6 7 8 8 8 8
 *          lose 1 1 1 1 2 3 4 5 6 7 8 7 6 5 4 3 2 2 2 2
 *
 * The frames step every 0x1E ticks; their size varies by opponent (120x79,
 * 120x87, 128x79, 112x76, 104x74), which is why the picture is placed from
 * the header rather than from a constant.  "win" and "lose" are
 * from the OPPONENT's point of view: var_18 == 1 means the opponent won,
 * and that is the branch that loads OPP<n>WIN.PVS and the ev* lines.
 *
 * A taunt line id is three characters - 'v' (0x76) or 'd' (0x64), a
 * sequence digit '1'..'3', and 'a' + a random 0..3 - so the ids are
 * ev1a..ev3c and ed1a..ed3c, which is exactly what the archives carry.
 * The one-line case (var_18 == 2, nobody finished) uses the literal "d4a".
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] - stated plainly, because a UI screen has no oracle.  This is
 * behaviour-exact, not instruction-exact, in these four places and nowhere
 * else:
 *
 *  1. No sprite windows.  The original renders into an offscreen
 *     sprite_make_wnd(0x140, 0xC8, 0x0F), blits it with
 *     sprite_blit_to_video, and repaints only the strip that changed.  This
 *     port redraws the whole picture into the presented frame every time,
 *     as every other ported menu does.  Same pixels, no incremental logic.
 *     The video_flag5_is0 double-buffered portrait path (loc_13E0F and
 *     loc_142B0) is therefore dead here, as it is in the original whenever
 *     that flag is clear - which it is, game_init sets it to 0.
 *  2. No DOS mouse and no event loop.  mouse_timer_sprite_unk's blink is
 *     kept (word_407CE = 5 / word_407D0 = 14 every 30 ticks out of 60) and
 *     its selection outline is sprite_1_unk4 as in the original;
 *     mouse_multi_hittest is reproduced against the same rectangles.  The
 *     loop itself is SDL, in main_native.c.
 *  3. No show_dialog.  The name entry (enter_hiscore -> show_dialog(3,...)
 *     -> call_read_line) is SDL_TEXTINPUT, as Phase 3 already had it, and
 *     the "ihd" disk dialog on a missing .TRK is skipped - a missing track
 *     file simply means no high score, which is the branch the dialog
 *     leads to when it is cancelled.
 *  4. get_super_random (seg008 4465) is rand() + get_kevinrandom() +
 *     timer_get_counter() + gState_frame, made positive.  There is no PIT
 *     counter here, so the timer term is dropped; the other three are the
 *     original's and the value is only ever taken modulo 3 or 4.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "rfbsize.h"
#include "rendscreen.h"
#include "rhighscore.h"
#include "rwidgets.h"
#include "shape2d.h"

extern char far* td11_highscores;
extern uint8_t far* td14_elem_map_main;
extern struct GAMEINFO gameconfig;
extern uint16_t elapsed_time1;
extern int16_t dialog_fnt_colour;
extern void far* fontnptr;
extern void far* fontdefptr;
extern void far* miscptr;
extern int16_t video_flag1_is1;
extern char byte_43966;

extern char far* locate_text_res(char far* res, char* key);
extern void* file_load_resource(int16_t restype, const char* filename);
extern void* file_load_resfile(const char* filename);
extern void* locate_shape_nofatal(void* resptr, const char* name);
extern void unload_resource(void far* resptr);
extern void far* unflip_shape(void far* shape);

extern void font_set_fontdef2(void far* data);
extern void font_set_fontdef(void);
extern void font_set_colour(uint16_t colour, uint16_t unused);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern uint16_t font_op2(const char* str);
extern void format_frame_as_string(char* buf, int16_t frames, int16_t cs);
extern void print_int_as_string_maybe(char* dst, int16_t value,
                                      int16_t pad, int16_t width);

extern int16_t get_kevinrandom(void);

/* statecrs.c's copy of the finished race, seg000's own names. */
extern int16_t gState_144, gState_impactSpeed, gState_jumpCount;
extern int16_t gState_oEndFrame, gState_pEndFrame, gState_penalty;
extern int16_t gState_topSpeed, gState_total_finish_time;
extern int32_t gState_travDist;
extern uint16_t gState_frame;

/* music_native.h's enum, without dragging SDL's include path in here. */
#define RS_SONG_GAMEOVER 2
#define RS_SONG_VICTORY  3

/* dseg 21077..21105 and 2807..2843 - every literal this screen uses. */
static const int16_t word_407F4 = 15;   /* bevel light   */
static const int16_t word_407F6 =  8;   /* bevel dark    */
static const int16_t word_407F8 =  7;   /* button face   */
static const int16_t word_407D2 =  8;   /* portrait frame, bottom/right */
static const int16_t word_407CE =  5;   /* selection blink, colour A    */
static const int16_t word_407D0 = 14;   /* selection blink, colour B    */
static const int16_t word_3BCEC[4] = {   4,  84, 164, 244 };
static const int16_t word_3BCF6[4] = {  75, 155, 235, 315 };
static const int16_t hiscore_buttons_y1 = 174;
static const int16_t hiscore_buttons_y2 = 197;
/* the two anti-repeat permutations at dseg 2807 / 2812 */
static const int16_t word_3BCDE[3] = { 2, 0, 1 };
static const int16_t word_3BCE4[4] = { 1, 0, 3, 2 };

/* dseg 0x40D3A.. - these persist between races, which is the whole point of
 * the "if the roll repeats, take the table entry instead" step. */
static int16_t word_40D3A, word_40D3C, word_40D3E;
static int16_t word_40D40, word_40D44;
static int16_t end_hiscore_random;

/* The original's 0xAC74 scratch line buffer.  resID_byte1 is 32 bytes in
 * this port; dseg gives the area about 83, and the wrap loop fills up to
 * 80. */
static char rs_line[96];

/* ------------------------------------------------------------------ */
/* seg008 4465 ported_get_super_random_                                */
/* ------------------------------------------------------------------ */
static int16_t get_super_random(void)
{
	int16_t ax = (int16_t)((int16_t)rand() + get_kevinrandom()
	                       + (int16_t)gState_frame);
	return ax >= 0 ? ax : (int16_t)-ax;
}

/* seg008 font_op2_alt - the x that centres a string on the 320-wide
 * screen. */
static int16_t font_op2_alt(const char* s)
{
	return (int16_t)((0x140 - (int16_t)font_op2(s)) / 2);
}

static const char* res_of(void far* res, const char* key)
{
	char far* p = res ? locate_text_res((char far*)res, (char*)key) : NULL;
	return p ? (const char*)p : "";
}

/* ------------------------------------------------------------------ */
/* seg000 131C0..1379A - the statistics block.                         */
/*                                                                     */
/* Rebuilt from the gState_* copy on every repaint rather than once     */
/* into a window sprite; the arithmetic is the original's, instruction  */
/* by instruction, including the two `shr ax, 8` (logical, not          */
/* arithmetic) and the 32-bit unsigned divide behind the average speed. */
/* ------------------------------------------------------------------ */
static void endscreen_stats(struct ENDSCREEN* es, int draw)
{
	void far* misc = es->var_4E;
	int16_t var_70 = 0x6B;
	char var_12[16];

#define RS_SHOW(s)  do { if (draw) hiscore_draw_text((s), font_op2_alt(s), \
                             var_70, dialog_fnt_colour, 0); } while (0)

	font_set_fontdef2(fontdefptr);

	/* --- elapsed time, or DNF --- */
	snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "elt"));
	if (gState_total_finish_time != 0) {
		format_frame_as_string(var_12,
			(int16_t)(gState_total_finish_time - gState_penalty), 1);
		strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
		if ((byte_43966 & 2) != 0)                       /* loc_132A0 */
			strncat(rs_line, res_of(misc, "con"),
			        sizeof rs_line - strlen(rs_line) - 1);
		RS_SHOW(rs_line);
		var_70 += 0x0A;
		/* --- penalty, only when there is one --- */
		if (gState_penalty != 0) {                       /* loc_1330D */
			snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "ppt"));
			format_frame_as_string(var_12, gState_penalty, 1);
			strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
			RS_SHOW(rs_line);
			var_70 += 0x0A;
		}
	} else {                                             /* loc_13354 */
		strncat(rs_line, res_of(misc, "dnf"),
		        sizeof rs_line - strlen(rs_line) - 1);
		RS_SHOW(rs_line);
		var_70 += 0x0A;
	}

	/* --- the opponent's time, and with it the verdict --- */
	es->var_18 = 2;                                      /* loc_133A7 */
	if (gameconfig.game_opponenttype != 0) {
		if (gState_144 == 0) {                           /* loc_133B5 */
			snprintf(rs_line, sizeof rs_line, "%s%s",
			         res_of(misc, "olt"), res_of(misc, "dnf"));
			if (gState_total_finish_time != 0) es->var_18 = 0;
		} else if (gState_total_finish_time == 0 ||
		           (uint16_t)gState_144 <
		           (uint16_t)gState_total_finish_time) {  /* loc_1341C */
			snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "owt"));
			format_frame_as_string(var_12, gState_144, 1);
			strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
			es->var_18 = 1;
		} else {                                          /* loc_13466 */
			snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "olt"));
			format_frame_as_string(var_12, gState_144, 1);
			strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
			if (gState_total_finish_time != 0) es->var_18 = 0;
		}
		RS_SHOW(rs_line);
		var_70 += 0x0A;
	}

	/* --- average speed (loc_1351D) ---
	 * di = (uint32)travDist / (uint16)(pEndFrame + elapsed_time1), then
	 * the four register moves that follow __aFuldiv are a 32-bit >> 8 with
	 * a 16-bit result. */
	{
		uint16_t div = (uint16_t)((uint16_t)gState_pEndFrame + elapsed_time1);
		int16_t di = 0;
		if (div != 0)
			di = (int16_t)((uint16_t)(((uint32_t)gState_travDist / div) >> 8));
		snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "avs"));
		print_int_as_string_maybe(var_12, di, 0, 3);
		strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
		strncat(rs_line, res_of(misc, "mph"),
		        sizeof rs_line - strlen(rs_line) - 1);
		RS_SHOW(rs_line);
		var_70 += 0x0A;
	}

	/* --- impact speed, only after a crash (loc_135ED) --- */
	if (gState_impactSpeed != 0) {
		snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "imp"));
		print_int_as_string_maybe(var_12,
			(int16_t)((uint16_t)gState_impactSpeed >> 8), 0, 3);
		strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
		strncat(rs_line, res_of(misc, "mph"),
		        sizeof rs_line - strlen(rs_line) - 1);
		RS_SHOW(rs_line);
		var_70 += 0x0A;
	}

	/* --- top speed (loc_1368B) --- */
	snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "top"));
	print_int_as_string_maybe(var_12,
		(int16_t)((uint16_t)gState_topSpeed >> 8), 0, 3);
	strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
	strncat(rs_line, res_of(misc, "mph"), sizeof rs_line - strlen(rs_line) - 1);
	RS_SHOW(rs_line);
	var_70 += 0x0A;

	/* --- jumps, only when there were any (loc_136FE); note the original
	 * does NOT advance var_70 after this, it is the last line. --- */
	if (gState_jumpCount != 0) {
		snprintf(rs_line, sizeof rs_line, "%s", res_of(misc, "jum"));
		print_int_as_string_maybe(var_12, gState_jumpCount, 0, 3);
		strncat(rs_line, var_12, sizeof rs_line - strlen(rs_line) - 1);
		RS_SHOW(rs_line);
	}
#undef RS_SHOW
}

/* ------------------------------------------------------------------ */
/* seg000 137AD..13801 - roll the two outer taunt lines.               */
/* ------------------------------------------------------------------ */
static void endscreen_roll(struct ENDSCREEN* es)
{
	if ((byte_43966 & 4) != 0) return;              /* loc_137A3 */

	word_40D3A = word_40D40;
	word_40D3C = end_hiscore_random;
	word_40D3E = word_40D44;

	word_40D40 = (int16_t)(get_super_random() % 3);
	if (word_40D40 == word_40D3A) word_40D40 = word_3BCDE[word_40D40];

	word_40D44 = (int16_t)(get_super_random() % 3);
	if (word_40D44 == word_40D3E) word_40D44 = word_3BCDE[word_40D44];

	/* loc_13801.  This value is overwritten a few instructions later by
	 * the get_kevinrandom draw at loc_1387C / loc_138EE, in every path
	 * that reaches it - transcribed anyway, because the anti-repeat step
	 * below reads and rewrites word_40D3C. */
	if (es->var_18 == 1)
		end_hiscore_random = (int16_t)(gState_total_finish_time != 0
			? get_super_random() % 2 + 2
			: get_super_random() % 2);
	else
		end_hiscore_random = (int16_t)(get_super_random() % 4);
	if (end_hiscore_random == word_40D3C)
		end_hiscore_random = word_3BCE4[end_hiscore_random];
}

/* ------------------------------------------------------------------ */
/* seg000 13A4B..13D06 - the evaluation page.                          */
/* ------------------------------------------------------------------ */
static void far* portrait_frame(struct ENDSCREEN* es, int16_t idx)
{
	char name[8];
	uint8_t f;
	if (!es->var_1C || !es->var_5A) return NULL;
	f = (uint8_t)es->var_5A[idx];
	if (f == 0) return NULL;
	name[0] = 'o'; name[1] = 'p'; name[2] = '0';
	name[3] = (char)(f + '0');
	name[4] = 0;
	return unflip_shape(locate_shape_nofatal(es->var_1C, name));
}

/* the three taunt line ids, loc_13B36 / loc_13BD0 / loc_13BE0 */
static void taunt_id(const struct ENDSCREEN* es, int16_t di, char id[4])
{
	if (di == 0 && es->var_18 == 2) {           /* the literal "d4a" */
		id[0] = 'd'; id[1] = '4'; id[2] = 'a'; id[3] = 0;
		return;
	}
	id[0] = (char)es->var_6A;
	id[1] = (char)('1' + di);
	id[2] = (char)('a' + (di == 0 ? (uint8_t)word_40D40
	                    : di == 1 ? (uint8_t)end_hiscore_random
	                              : (uint8_t)word_40D44));
	id[3] = 0;
}

static void endscreen_eval(struct ENDSCREEN* es)
{
	void far* shape;
	const struct SHAPE2D far* hdr;
	int16_t var_70, var_40, var_50, var_80, var_8A, var_5C, di;
	char var_3C[64];
	char var_3E;

	/* loc_13A4B: frame 1 sizes and places the picture, whichever frame is
	 * actually showing. */
	{
		char name[8];
		name[0] = 'o'; name[1] = 'p'; name[2] = '0'; name[3] = '1'; name[4] = 0;
		shape = es->var_1C ? unflip_shape(locate_shape_nofatal(es->var_1C, name))
		                   : NULL;
	}
	if (!shape) return;
	hdr = (const struct SHAPE2D far*)shape;
	var_70 = (int16_t)((int16_t)hdr->s2d_width * video_flag1_is1);
	es->var_8C = (int16_t)(0x138 - var_70);
	es->var_90 = (int16_t)((0x63 - (int16_t)hdr->s2d_height) >> 1);

	draw_lines_unk((int16_t)(es->var_8C - 3), (int16_t)(es->var_90 - 3),
	               (int16_t)(var_70 + 5),
	               (int16_t)((int16_t)hdr->s2d_height + 5),
	               dialog_fnt_colour, 0, word_407D2);

	{
		void far* f = portrait_frame(es, es->var_8E);
		if (f) shape2d_op_unk5(f, es->var_8C, es->var_90);
	}
	es->var_6C = es->var_8E;

	/* loc_13B0B: the taunt, word-wrapped into (var_8C - 16) pixels. */
	font_set_colour(0, 0);                    /* font_set_unk(0, 0) */
	var_70 = 8;
	var_40 = 0;
	var_50 = 0;
	var_80 = 0;
	es->var_7A = (int16_t)(es->var_18 == 2 ? 1 : 3);

	for (di = 0; di < es->var_7A; di++) {
		char id[4];
		const char* p;
		taunt_id(es, di, id);
		p = res_of(es->var_68, id);
		font_set_fontdef2(fontnptr);
		for (;;) {
			var_3E = *p++;
			if (var_3E != ' ' && var_3E != 0) {
				if (var_80 < (int16_t)sizeof var_3C - 1)
					var_3C[var_80++] = var_3E;      /* loc_13C92 */
				continue;
			}
			var_3C[var_80] = 0;                     /* loc_13B97 */
			var_8A = (int16_t)font_op2(var_3C);
			if (var_8A + var_50 < es->var_8C - 0x10 && var_40 + var_80 < 0x50) {
				for (var_5C = 0; var_5C < var_80; var_5C++)
					rs_line[var_40++] = var_3C[var_5C];
				var_50 += var_8A;
			} else {                                /* loc_13C1A */
				rs_line[var_40] = 0;
				font_draw_text(rs_line, 8, var_70);
				var_70 += 8;
				var_5C = (int16_t)(var_3C[0] == ' ' ? 1 : 0);
				var_40 = 0;
				for (; var_5C < var_80; var_5C++)
					rs_line[var_40++] = var_3C[var_5C];
				rs_line[var_40] = 0;
				var_50 = (int16_t)font_op2(rs_line);
			}
			var_80 = 1;                             /* loc_13C86 */
			var_3C[0] = ' ';
			if (var_3E == 0) break;
		}
		font_set_fontdef();
	}
	if (var_40 != 0) {                              /* loc_13CD0 */
		font_set_fontdef2(fontnptr);
		rs_line[var_40] = 0;
		font_draw_text(rs_line, 8, var_70);
		font_set_fontdef();
	}
}

/* ------------------------------------------------------------------ */
/* seg000 13190..13A0F - open the screen.                              */
/* ------------------------------------------------------------------ */
void endscreen_open(struct ENDSCREEN* es, const char* trackpath)
{
	char base[16];

	memset(es, 0, sizeof *es);
	es->y1 = hiscore_buttons_y1;
	es->y2 = hiscore_buttons_y2;
	es->hiscore_row = -1;

	/* loc_1317A: the misc resource, and the opponent's own file. */
	es->var_4E = miscptr ? miscptr : file_load_resfile("misc");
	if (gameconfig.game_opponenttype != 0) {
		snprintf(base, sizeof base, "opp%d", (int)gameconfig.game_opponenttype);
		es->var_68 = file_load_resfile(base);
	}

	/* The statistics block is where the original decides var_18, so it is
	 * run once here with drawing switched off; endscreen_draw runs it
	 * again, drawing, on every repaint. */
	endscreen_stats(es, 0);

	/* loc_134DC: the song.  file_load_audiores("skidvict"/"skidover",
	 * "skidms", "VICT"/"OVER"); Phase 8 owns the sequencer, so the caller
	 * starts it. */
	es->song = (int16_t)(es->var_18 == 0 ? RS_SONG_VICTORY : RS_SONG_GAMEOVER);

	/* loc_1350B */
	es->var_16 = gameconfig.game_opponenttype;
	if (es->var_18 == 2 && gState_pEndFrame != gState_oEndFrame)
		es->var_16 = 0;

	if (es->var_16 != 0) {
		endscreen_roll(es);                       /* loc_137A3 */
		if (es->var_18 == 1) {                    /* loc_1384B */
			snprintf(base, sizeof base, "opp%dwin.pvs",
			         (int)gameconfig.game_opponenttype);
			es->var_1C = file_load_resource(3, base);
			/* the script lives in OPP<n>.PRE (var_68), not in the .PVS:
			 * seg000:1386F passes var_68/var_66 to locate_shape_alt. */
			es->var_5A = es->var_68
				? (const uint8_t far*)locate_shape_nofatal(es->var_68, "winn")
				: NULL;
			end_hiscore_random = (int16_t)
				(((get_kevinrandom() + (int16_t)gState_frame) & 1)
				 + (gState_total_finish_time != 0 ? 2 : 0));
			es->var_6A = 0x76;                    /* 'v' */
		} else {                                  /* loc_138B6 */
			snprintf(base, sizeof base, "opp%dlose.pvs",
			         (int)gameconfig.game_opponenttype);
			es->var_1C = file_load_resource(3, base);
			es->var_5A = es->var_68
				? (const uint8_t far*)locate_shape_nofatal(es->var_68, "lose")
				: NULL;
			end_hiscore_random = (int16_t)
				((get_kevinrandom() + (int16_t)gState_frame) & 3);
			es->var_6A = 0x64;                    /* 'd' */
		}
		if (!es->var_5A) es->var_16 = 0;   /* nothing to animate */
	}

	/* loc_138FF: is this still the track the .HIG belongs to?  The
	 * original reloads <track>.TRK and compares 0x385 bytes with
	 * td14_elem_map_main; anything else means the track has been edited
	 * since, and the run does not count. */
	es->var_6E = 0;
	{
		FILE* f = trackpath ? fopen(trackpath, "rb") : NULL;
		if (!f) {
			es->var_6E = 0xFF;                    /* loc_139B6 */
		} else {
			uint8_t buf[0x385];
			size_t n = fread(buf, 1, sizeof buf, f);
			fclose(f);
			if (n != sizeof buf ||
			    memcmp(buf, td14_elem_map_main, sizeof buf) != 0)
				es->var_6E = 0xFF;
		}
	}

	/* loc_139BA */
	if (es->var_6E != 0xFF) {
		if (highscore_write_a(0) != 0 && highscore_write_a(1) != 0)
			es->var_6E = 0xFF;
	}

	/* loc_139E1: does the time make the table? */
	if (es->var_6E == 0 && gState_total_finish_time != 0) {
		es->var_88 = gState_total_finish_time;
		if ((byte_43966 & 6) == 0 &&
		    (uint16_t)((uint8_t)td11_highscores[0x16A] |
		               ((uint16_t)(uint8_t)td11_highscores[0x16B] << 8))
		    > (uint16_t)gState_total_finish_time)
			es->var_6E = 1;
	}

	/* loc_13A0F */
	es->var_8E = 0;
	es->var_6C = -1;
	es->var_42 = 0x1E;
	es->var_14 = 1;
	es->var_selectedmenu = 1;

	/* loc_13FDA decides the button set, and it can only ever see var_6E
	 * as 0 or 0xFF by the time it runs. */
	es->nbuttons = (int16_t)((es->var_16 != 0 && es->var_6E != 0xFF) ? 4 : 3);
	es->var_9C = (int16_t)(es->nbuttons == 4 ? 0 : -36);
	for (int i = 0; i < 4; i++) {
		es->x1[i] = (int16_t)(word_3BCEC[i] + es->var_9C);
		es->x2[i] = (int16_t)(word_3BCF6[i] + es->var_9C);
	}

	/* Which page the top panel starts on, straight off the original's
	 * control flow:
	 *   var_16 == 0            -> loc_13F48 leaves var_14 = 1, so the
	 *                             table (or "hna") is what shows;
	 *   var_16 != 0, var_6E==1 -> loc_13D06 clears var_14, then loc_13D13
	 *                             sets it back to 1 before enter_hiscore,
	 *                             so after the name entry the table shows;
	 *   var_16 != 0, otherwise -> loc_13D06 clears var_14: the eval page.
	 */
	if (es->var_16 != 0 && es->var_6E != 1) es->var_14 = 0;
}

/* seg000 13D13: the eval page with a single "Continue" button while the
 * portrait animates, shown before the name entry.  Reached only when there
 * is an opponent AND a high score to enter. */
void endscreen_draw_continue(struct ENDSCREEN* es)
{
	int16_t saved = es->var_14;
	es->var_14 = 0;                       /* force the eval page */
	draw_button(NULL, 0, 0, 0x140, 0x64,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_button(NULL, 0, 0x65, 0x140, 0x63,
	            word_407F4, word_407F6, word_407F8, 0);
	endscreen_stats(es, 1);
	endscreen_eval(es);
	draw_button(res_of(es->var_4E, "bct"), 0x81, 0xAF, 0x46, 0x15,
	            word_407F4, word_407F6, word_407F8, 0);
	es->var_14 = saved;
}

/* The animation runs during the Continue wait too (loc_13D83), where
 * neither the var_14 nor the var_18 gate of the main loop applies. */
int endscreen_advance_continue(struct ENDSCREEN* es, int16_t ticks)
{
	if (!es->var_5A) return 0;
	es->var_42 = (int16_t)(es->var_42 + ticks);
	while (es->var_42 >= 0x1E) {
		es->var_42 -= 0x1E;
		es->var_8E++;
		if (es->var_5A[es->var_8E] == 0) es->var_8E = 0;
	}
	if (es->var_8E == es->var_6C) return 0;
	es->var_6C = es->var_8E;
	return 1;
}

void endscreen_close(struct ENDSCREEN* es)
{
	if (es->var_1C) unload_resource(es->var_1C);
	if (es->var_68) unload_resource(es->var_68);
	es->var_1C = NULL;
	es->var_68 = NULL;
	es->var_5A = NULL;
}

/* ------------------------------------------------------------------ */
/* seg000 13D83 / 1421B - the 0x1E-tick frame script.                  */
/* ------------------------------------------------------------------ */
int endscreen_advance(struct ENDSCREEN* es, int16_t ticks)
{
	if (es->var_16 == 0 || es->var_14 != 0 || es->var_18 == 2) return 0;
	if (!es->var_5A) return 0;
	es->var_42 = (int16_t)(es->var_42 + ticks);
	while (es->var_42 >= 0x1E) {
		es->var_42 -= 0x1E;
		es->var_8E++;
		if (es->var_5A[es->var_8E] == 0) es->var_8E = 0;
	}
	if (es->var_8E == es->var_6C) return 0;
	es->var_6C = es->var_8E;
	return 1;
}

/* ------------------------------------------------------------------ */
/* seg000 13FDA..14130 - the buttons.                                  */
/* ------------------------------------------------------------------ */
static void endscreen_buttons(struct ENDSCREEN* es)
{
	void far* misc = es->var_4E;
	const char* lbl;

	if (es->nbuttons == 4) {
		lbl = res_of(misc, es->var_14 != 0 ? "bev" : "bhi");
		draw_button(lbl, (int16_t)(es->var_9C + word_3BCEC[0] + 1), 0xAF,
		            0x46, 0x15, word_407F4, word_407F6, word_407F8, 0);
	}
	draw_button(res_of(misc, "brp"),
	            (int16_t)(es->var_9C + word_3BCEC[1] + 1), 0xAF,
	            0x46, 0x15, word_407F4, word_407F6, word_407F8, 0);
	draw_button(res_of(misc, es->var_16 != 0 ? "bra" : "bdr"),
	            (int16_t)(es->var_9C + word_3BCEC[2] + 1), 0xAF,
	            0x46, 0x15, word_407F4, word_407F6, word_407F8, 0);
	draw_button(res_of(misc, "bmm"),
	            (int16_t)(es->var_9C + word_3BCEC[3] + 1), 0xAF,
	            0x46, 0x15, word_407F4, word_407F6, word_407F8, 0);
}

/* ------------------------------------------------------------------ */
void endscreen_draw(struct ENDSCREEN* es, int blink)
{
	/* the two panels, loc_13207 / loc_13232 */
	draw_button(NULL, 0, 0, 0x140, 0x64,
	            word_407F4, word_407F6, word_407F8, 0);
	draw_button(NULL, 0, 0x65, 0x140, 0x63,
	            word_407F4, word_407F6, word_407F8, 0);

	endscreen_stats(es, 1);

	if (es->var_14 == 0) {
		endscreen_eval(es);
	} else if (es->var_6E == 0xFF) {            /* loc_13F84 */
		snprintf(rs_line, sizeof rs_line, "%s", res_of(es->var_4E, "hna"));
		font_set_fontdef2(fontdefptr);
		hiscore_draw_text(rs_line, font_op2_alt(rs_line), 0x32,
		                  dialog_fnt_colour, 0);
	} else {
		highscore_text_unk();
	}

	endscreen_buttons(es);

	/* seg008 mouse_timer_sprite_unk -> sprite_1_unk4: a one-pixel outline
	 * round the selected button, alternating between word_407CE and
	 * word_407D0 every 30 of its 60 ticks. */
	{
		int16_t i = es->var_selectedmenu;
		if (i >= 0 && i < 4)
			sprite_1_unk4(es->x1[i], es->y1, es->x2[i], es->y2,
			              blink ? word_407CE : word_407D0);
	}
}

/* ------------------------------------------------------------------ */
/* seg008 mouse_multi_hittest, against the same rectangles.  In the     */
/* three-button form the original passes &var_62 / &var_98, i.e. the    */
/* arrays offset by one entry, and adds 1 to the result (loc_14343).    */
/* ------------------------------------------------------------------ */
int16_t endscreen_hittest(const struct ENDSCREEN* es, int16_t x, int16_t y)
{
	int16_t i, first = (int16_t)(es->nbuttons == 4 ? 0 : 1);
	for (i = first; i < 4; i++)
		if (es->x1[i] <= x && es->x2[i] >= x && es->y1 <= y && es->y2 >= y)
			return i;
	return -1;
}

/* seg000 1447A */
void endscreen_left(struct ENDSCREEN* es)
{
	if (es->nbuttons == 4) {
		if (es->var_selectedmenu == 0) es->var_selectedmenu = 3;
		else                           es->var_selectedmenu--;
	} else {
		if (es->var_selectedmenu > 1) es->var_selectedmenu--;
		else                          es->var_selectedmenu = 3;
	}
}

/* seg000 144A4 */
void endscreen_right(struct ENDSCREEN* es)
{
	if (es->var_selectedmenu < 3) es->var_selectedmenu++;
	else es->var_selectedmenu = (int16_t)(es->nbuttons == 4 ? 0 : 1);
}

/* seg000 143C6 */
int16_t endscreen_activate(struct ENDSCREEN* es)
{
	if (es->var_selectedmenu == 0) {
		/* loc_143C6: clear the top panel and swap the page. */
		es->var_14 = (int16_t)(es->var_14 != 0 ? 0 : 1);
		es->var_selectedmenu = 1;
		return -1;
	}
	return (int16_t)(es->var_selectedmenu - 1);
}
