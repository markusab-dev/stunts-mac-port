/*
 * rreplaybar.c - Phase 9, the in-race recording bar.
 *
 * `loop_game` (seg005 3391..5365) is not the main loop, despite the name: it
 * is the VCR strip under the race - pause, scrub, save and load a replay, and
 * the camera pan and zoom. This file is all of it:
 *
 *   check_input           seg008 3109..3147
 *   mouse_multi_hittest   seg008 3049..3108
 *   replaybar_view_height seg005  498..520   (the piece of ported_run_game_
 *                                             that decides the view height)
 *   loop_game             seg005 3391..5365
 *
 * The strip only exists when four things hold at once, and seg005:498 spells
 * them out:
 *
 *     if (dashb_toggle && !followOpponentFlag
 *         && game_replay_mode == 2 && replaybar_enabled)
 *              height_above_replaybar = 0x97;   151
 *     else     height_above_replaybar = 0xC8;   200
 *
 * so the bar is a *playback* control: never there while driving, gone in the
 * opponent's camera. 200 - 151 = 49 rows, which is exactly the height of
 * every one of its background shapes.
 *
 * ------------------------------------------------------------------------
 * THE LAYOUT IS IN SDGAME.PVS, NOT HERE
 *
 * loop_game loads 23 resources by name in one call:
 *
 *   rply rpic rpac rpmc rptc  bof6 bof5 bof4 bof3 bof2 bof1 bof0
 *   zoom pann  bon6 bon5 bon4 bon3 bon2 bon1 bof0  zoom pann
 *
 * and every one of them carries its own position in its SHAPE2D header, so
 * nothing here places anything.  Read out of the file with
 * tools/dump_shape2d.c, and it agrees with the hit rectangles in dseg to
 * within the couple of pixels of slop the artwork has:
 *
 *   rply 216x49 (104,151)   the strip proper, right of the camera panel
 *   rpic 104x49 (  0,151)   the camera panel, one per cameramode
 *   rpac 104x49 (  0,151)   (rpic rpac rpmc rptc = camera modes 0..3)
 *   rpmc 104x49 (  0,151)
 *   rptc 104x49 (  0,151)
 *   bof6  48x18 (104,156)   button 6 unlit  |  bon6 lit
 *   bof5  48x18 (104,176)   button 1 unlit  |  bon5 lit
 *   bof4  48x18 (272,156)   button 2 unlit  |  bon4 lit
 *   bof3  48x18 (232,156)   button 3 unlit  |  bon3 lit
 *   bof2  56x18 (184,156)   button 4 unlit  |  bon2 lit
 *   bof1  48x18 (144,156)   button 5 unlit  |  bon1 lit
 *   bof0  48x18 (104,156)   button 6 unlit  |  bof0 again - see [ODDITY]
 *   zoom  32x38 ( 64,156)   the zoom rocker  (button 7)
 *   pann  40x38 (  8,156)   the pan pad      (button 8)
 *
 * The nine hit rectangles come from dseg 14180..14215, which IDA split
 * across labels because the last one or two entries of each array picked up
 * their own names (word_3EA18 is x1[8], word_3EA3A/3EA3C are y1[7]/y1[8],
 * word_3EA4C/3EA4E are y2[7]/y2[8]).  Reassembled they are the table below,
 * and it lines up with the shape positions button for button.
 *
 * What the nine controls do, from the jump table at seg005 off_24D20 - and
 * the glyph each one is drawn with, read off the rendered strip, which is
 * what settles what byte_449E6 means:
 *
 *   0  >>  fast forward, held; accelerates  x 272..314, y 176..193
 *   1  <<  rewind, held; accelerates        x 109..151, y 176..193
 *   2  >>  play fast     byte_449E6 = 3     x 274..314, y 156..173
 *   3  >   play          byte_449E6 = 0     x 232..274, y 156..173
 *   4  []  stop/pause                       x 190..232, y 156..173
 *   5  |<  back to the start                x 151..190, y 156..173
 *   6 menu the pause menu (same as Esc)     x 108..151, y 156..173
 *   7  +-  zoom, a two-way rocker           x  66..91,  y 156..193
 *   8  <^> pan, a four-way pad              x  10..47,  y 156..193
 *
 * byte_449E6 is therefore the playback speed: 0 for the Play button, 3 for
 * the double-arrow beside it.  Buttons 0 and 2 carry the same >> glyph
 * because they do the same thing at different grain - 2 latches fast
 * playback, 0 scrubs for as long as it is held.
 *
 * ------------------------------------------------------------------------
 * [ODDITY] seg005:3446, the resource-name string.  The "lit" shape for
 * button 6 is spelled `bof0` - the unlit one - where the pattern wants
 * `bon0`, and the string then repeats `zoom` and `pann` for slots 21 and 22.
 * Neither is a typo: SDGAME.PVS contains bon1..bon6 and no bon0 at all
 * (checked with tools/dump_shape2d.c, 22 resources), so button 6 - the menu
 * button, which opens a dialog rather than latching - simply has no lit
 * state, and the repeated zoom/pann let the "restore the shape under the
 * cursor" path at loc_23E41 index slots 7 and 8 without a special case.
 * Reproduced exactly.
 *
 * [ODDITY] seg005 loc_23F25, the "light the latched buttons" loop, writes
 * byte_40E7A[i*2 + page] = 1 both before and after the blit, with nothing in
 * between that could change it.  Kept.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] One framebuffer, one page.  byte_4432A selects which of two
 * video pages the bar's caches belong to, and it only ever changes under
 * `if (video_flag5_is0)` (seg005 loc_23FDA).  This port sets
 * video_flag5_is0 = 0 (main_native.c:1719) and composites into a single
 * buffer, so byte_4432A stays 0 and the [2]-wide caches below use slot 0 -
 * which is what the original does too on a machine without the second page.
 * sprite_copy_2_to_1, mouse_draw_opaque_check and mouse_draw_transparent_check
 * are the page-flip and software-cursor bracket around each redraw; with one
 * page and the host's own cursor they are no-ops here, kept as named stubs
 * so the call sites still read like the original.
 *
 * [DEVIATION] input_checking / input_do_checking / timer_get_delta_alt /
 * kb_get_key_state are DOS input and DOS timing.  They are re-implemented on
 * SDL at the bottom of this file.  The timer keeps the original's unit: the
 * PIT callback ran at 100 Hz (word_4499C = 100 / framespersec, see
 * audio_native.c), so a tick here is 10 ms, and the scrub acceleration in
 * loc_2485C - `di = acc/50 + 3`, capped at 100, `acc += delta * di`,
 * `frames = acc/20` - keeps the feel it was tuned for.
 *
 * [DEVIATION] show_dialog, five call sites.  See rreplaybar.h.
 *
 * ------------------------------------------------------------------------
 * WHAT IS NOT HERE.  loc_2450A and loc_24630, the load- and save-replay
 * entries of the pause menu, need do_fileselect_dialog, do_savefile_dialog,
 * file_build_path, file_find, ported_file_load_replay_ and
 * ported_file_write_replay_.  None of the six is in this tree, so the two
 * menu entries call replaybar_hook_load_replay / _save_replay and the
 * original's control flow around them - the "file exists" and "save failed"
 * dialogs, the track/car comparison that decides whether the loaded replay
 * needs the cars rebuilding - is left for whoever ports the replay file I/O.
 * The rest of loop_game is complete.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "externs.h"
#include "math.h"
#include "memmgr.h"
#include "rfbsize.h"
#include "rreplaybar.h"
#include "rwidgets.h"
#include "shape2d.h"

extern int16_t rintro_input(int16_t ticks);
extern void far* unflip_shape(void far* shape);
extern void shape2d_op_unk(void far* shape);
extern void format_frame_as_string(char* s, int16_t time, int16_t c);
extern void font_draw_text(const char* s, int16_t x, int16_t y);
extern void font_set_colour(uint16_t fg, uint16_t bg);
extern void font_set_fontdef2(void far* data);
extern uint16_t font_op2(const char* s);
extern struct RECTANGLE* intro_draw_text(char* str, int16_t x, int16_t y,
                                         int16_t colour_text,
                                         int16_t colour_shadow);
extern void far* file_load_resource(int16_t restype, const char* filename);
extern void restore_gamestate(int16_t frame);
extern void init_game_state(int16_t arg_0);
extern void update_crash_state(int16_t, int16_t);
extern void far* fontledresptr;
extern int16_t dialog_fnt_colour;
extern int16_t word_44D20;
extern int16_t custom_camera_distance;
extern int16_t custom_camera_azimuth_angle;
extern int16_t custom_camera_elevation_angle;
extern uint8_t rfb_pixels[];
extern uint32_t* rs_rgba;
extern const uint32_t* rs_pal;

static void rb_draw_waiting(void);

/*
 * seg008 check_input (3109..3147).
 *
 * A debounce, not a read: it spins until nothing is held any more, so a
 * keypress that opened a screen cannot immediately act on it as well. The
 * original polls three sources - the keyboard/joystick flag word, the
 * timer-driven input_do_checking, and the mouse buttons.
 *
 *     loc_28EEA:
 *         if (get_kb_or_joy_flags() & 0x30)                 -> still held
 *         if (input_do_checking(timer_get_delta_alt()))      -> still held
 *         if (kbormouse != 0 && (mouse_butstate & 3))        -> still held
 *         if (still held) goto loc_28EEA
 *
 * [DEVIATION] rintro_input is this port's stand-in for input_do_checking -
 * an SDL event poll returning non-zero on a key or button - and it already
 * services the window, so the loop stays responsive rather than spinning on
 * a DOS timer.
 */
void check_input(void)
{
	int16_t var_2;

loc_28EEA:
	var_2 = 0;
	if ((get_kb_or_joy_flags() & 0x30) != 0) {
		var_2 = 1;                                   /* loc_28EF3 */
	} else if (rintro_input(0) != 0) {
		var_2 = 1;
	} else if (kbormouse != 0 && (mouse_butstate & 3) != 0) {
		var_2 = 1;
	}
	if (var_2 != 0) goto loc_28EEA;
}

/*
 * Both of these are declared in externs.h and were defined nowhere until
 * check_input needed them - the same gap Phase 6 found in video_flag1_is1 and
 * Phase 10 in waitflag. They are the port's stand-ins, backed by SDL rather
 * than by the DOS keyboard and mouse handlers.
 *
 * get_kb_or_joy_flags: restunts2 seg012 3761..3820 (restunts1 carries it only
 * as an `extrn`). It folds ten scancodes out of kbscancodes into one flag word
 * and then - only if none of them was down - falls through to get_joy_flags
 * and returns that instead:
 *
 *     0x39 space -> 0x10   0x1C enter -> 0x20
 *     0x47 kp7   -> 0x09   0x48 kp8   -> 0x01   0x49 kp9 -> 0x05
 *     0x4B kp4   -> 0x08                        0x4D kp6 -> 0x04
 *     0x4F kp1   -> 0x0A   0x50 kp2   -> 0x02   0x51 kp3 -> 0x06
 *
 * i.e. bit0 up, bit1 down, bit2 right, bit3 left, bit4/bit5 the two buttons -
 * exactly the bitfield get_joy_flags returns and exactly the one seg005's
 * driving loop records. BOTH halves are answered now: the joystick half comes
 * from src/render_faithful/rjoystick.c, through the joy_flags_hook pointer
 * below.
 *
 * [DEVIATION] the "any other key also sets 0x10" fallback is this port's, not
 * the original's, and is kept on purpose: check_input above spins on
 * `flags & 0x30` to wait a keypress out, and the original backs that up with a
 * kbinput[] array this port does not have. Drop the fallback and holding a key
 * that is not one of the ten stops debouncing.
 *
 * mouse_butstate: bits 0 and 1 are the two buttons, which is what check_input
 * masks with 3. Refreshed on each read rather than kept by an interrupt.
 */
int16_t mouse_butstate;

/* kbormouse selects which pointer the game listens to. do_mou_restext sets
 * byte_3B8F2 to 1 and do_key_restext to 0; this port drives the menus with
 * both, so the mouse half is always live. Declared in externs.h, defined
 * here for the same reason as the two above. */
char kbormouse = 1;

/* seg008 input_checking's mouse_get_state call fills these three.  Declared
 * in externs.h (311, 312), defined nowhere until now; they are in the
 * original's 320x200 screen coordinates, so the window scale comes off here
 * and every consumer downstream stays in the original's units. */
int16_t mouse_xpos;
int16_t mouse_ypos;

static void rb_read_mouse(void)
{
	int mx = 0, my = 0, ww = 0, wh = 0;
	uint32_t b;
	SDL_Window* w;

	SDL_PumpEvents();
	b = SDL_GetMouseState(&mx, &my);
	mouse_butstate = (int16_t)(((b & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0)
	                         | ((b & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 2 : 0));

	w = SDL_GetMouseFocus();
	if (w) SDL_GetWindowSize(w, &ww, &wh);
	if (ww > 0 && wh > 0) {
		mouse_xpos = (int16_t)((int32_t)mx * 320 / ww);
		mouse_ypos = (int16_t)((int32_t)my * 200 / wh);
	} else {
		mouse_xpos = (int16_t)(mx / RFB_SCALE);
		mouse_ypos = (int16_t)(my / RFB_SCALE);
	}
}

/* rjoystick.c's get_joy_flags, when that file is linked in.
 *
 * This is a WEAK definition, and rjoystick.c carries the strong one that
 * points at get_joy_flags. Link rjoystick.c and the strong definition wins,
 * so the joystick half answers; leave it out - as tools/build_dumper.sh and
 * tools/build_intro.sh do - and this NULL stands, so the call below is
 * skipped and this file still links on its own. A weak or weak_import
 * *declaration* was tried first and does not survive ld64: a reference to a
 * symbol no object defines is an error either way. */
__attribute__((weak)) int16_t (*joy_flags_hook)(void);

/* One scancode's contribution, `mov bl,kbscancodes[n] ; cmp kbinput[bx],0 ;
 * jz .. ; or al,<bits>`. SDL scancode values ARE the PC set-1 make codes for
 * these ten, but they are spelled by name; the arrow keys are accepted
 * alongside the keypad because a modern keyboard's keypad is often absent. */
static int16_t kb_bit(const uint8_t* k, int n, int sc1, int sc2, int16_t bits)
{
	if (sc1 >= 0 && sc1 < n && k[sc1]) return bits;
	if (sc2 >= 0 && sc2 < n && k[sc2]) return bits;
	return 0;
}

int16_t get_kb_or_joy_flags(void)
{
	const uint8_t* k;
	int n = 0, i;
	int16_t ax = 0;

	rb_read_mouse();
	k = SDL_GetKeyboardState(&n);
	/* restunts2 seg012 3762..3819, in the listing's order. */
	ax |= kb_bit(k, n, SDL_SCANCODE_SPACE,  -1,                    0x10);
	ax |= kb_bit(k, n, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, 0x20);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_7,   SDL_SCANCODE_HOME,     0x09);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_8,   SDL_SCANCODE_UP,       0x01);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_9,   SDL_SCANCODE_PAGEUP,   0x05);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_6,   SDL_SCANCODE_RIGHT,    0x04);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_3,   SDL_SCANCODE_PAGEDOWN, 0x06);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_2,   SDL_SCANCODE_DOWN,     0x02);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_1,   SDL_SCANCODE_END,      0x0A);
	ax |= kb_bit(k, n, SDL_SCANCODE_KP_4,   SDL_SCANCODE_LEFT,     0x08);

	/* [DEVIATION] the debounce fallback - see the note above. */
	if ((ax & 0x30) == 0)
		for (i = 0; i < n; i++)
			if (k[i]) { ax |= 0x10; break; }

	/* LAB_2ea2_1b9e: `or ax,ax ; jnz .. ; call get_joy_flags`. */
	if (ax != 0) return ax;
	if (joy_flags_hook) return joy_flags_hook();
	return 0;
}

/*
 * seg005:498..520 - how tall the 3D view is.  replaybar_enabled and
 * height_above_replaybar are two more externs.h declarations that nothing
 * defined; default on, because the original ships with the bar available in
 * playback, and 200 = the full-height view.
 */
char replaybar_enabled = 1;
int16_t height_above_replaybar = 0xC8;

int16_t replaybar_view_height(void)
{
	if (dashb_toggle != 0 && followOpponentFlag == 0
	    && game_replay_mode == 2 && replaybar_enabled != 0)
		height_above_replaybar = 0x97;
	else
		height_above_replaybar = 0xC8;
	return height_above_replaybar;
}

/* ------------------------------------------------------------------ */
/* dseg                                                                */
/* ------------------------------------------------------------------ */

/* dseg 14176: how far byte_3E9DB may travel in each camera mode.  Mode 2
 * reaches all nine controls, mode 3 stops before the pan pad, modes 0 and 1
 * stop before the zoom rocker. */
static const signed char game_camera_buttons_count[4] = { 6, 6, 8, 7 };

/* dseg 14180..14215, reassembled - see the note at the top of the file. */
static const int16_t game_camera_buttons_x1[9] =
	{ 272, 109, 274, 232, 190, 151, 108, 66, 10 };
static const int16_t game_camera_buttons_x2[9] =
	{ 314, 151, 314, 274, 232, 190, 151, 91, 47 };
static const int16_t game_camera_buttons_y1[9] =
	{ 176, 176, 156, 156, 156, 156, 156, 156, 156 };
static const int16_t game_camera_buttons_y2[9] =
	{ 193, 193, 173, 173, 173, 173, 173, 193, 193 };

/* dseg 14216: the camera panel on the left of the strip, hit-tested as one
 * rectangle.  A click anywhere in it is the 'c' shortcut (loc_24107). */
static const int16_t gameunk_button_x1[1] = {   0 };
static const int16_t gameunk_button_x2[1] = { 104 };
static const int16_t gameunk_button_y1[1] = { 151 };
static const int16_t gameunk_button_y2[1] = { 200 };

/* dseg 14135..14169: where each arrow key moves the focus.  Checked against
 * the geometry above, entry by entry - left from 0 (x272, lower row) lands
 * on 1 (x109, lower row), up from 1 lands on 6 (x108, upper row), and so on
 * for all 36. */
/* signed char, not char: `char` is unsigned on arm64 and these three hold
 * 0xFF as "none".  The original reads them with `cbw`, which sign-extends,
 * and compares them with jge/jle, which are signed - so the port has to pin
 * the sign rather than inherit the compiler's. */
static signed char byte_3E9DB = 6;                      /* focused control */
static const char byte_3E9DC[9] = { 1, 7, 3, 4, 5, 6, 7, 8, 8 };  /* left  */
static const char byte_3E9E6[9] = { 0, 0, 2, 2, 3, 4, 5, 1, 7 };  /* right */
static const char byte_3E9F0[9] = { 2, 6, 2, 3, 4, 5, 6, 7, 8 };  /* up    */
static const char byte_3E9FA[9] = { 0, 1, 0, 0, 1, 1, 1, 7, 8 };  /* down  */

/* dseg 21102: the two colours the scrub control is drawn in. */
static const int16_t word_407FC = 1;    /* the trough                      */
static const int16_t word_407FE = 4;    /* the cursor, and the focus ring  */

/* dseg 22130..22200.  Every one of these is a "what is on the screen right
 * now" cache, two deep because the original has two video pages; see the
 * [DEVIATION] at the top.  They exist so a redraw only touches what moved. */
void far* rplyshapes[23];               /* dseg 0x40E0E */
static signed char byte_40E6A[9];       /* latched, one-hot                */
static signed char byte_40E74[2];       /* cameramode last drawn           */
static signed char byte_40E08[2];       /* focused control last outlined   */
static signed char byte_40E7A[9 * 2];   /* per control: drawn lit?          */
static int16_t word_40E0A[2];           /* time value last printed          */
static int16_t word_40E76[2];           /* marker position last drawn       */
static int16_t word_40E04[2];           /* cursor position last drawn       */

/* dseg 35389/33766/33791: "has the background been laid down on this page",
 * and the page pair itself.  All three are externs.h declarations that
 * nothing defined. */
char byte_449D8[2];
char byte_4432A;                        /* the page being drawn            */
char byte_44346;                        /* the page being shown            */

/*
 * [DEVIATION] byte_449D8 is a per-page "the strip has already been laid down
 * here" flag, and in the original it is sound: the 3D view is clipped to rows
 * 0..0x96 and nothing else ever writes rows 0x97..0xC7, so the strip survives
 * from frame to frame on each of the two pages and mode 1 only has to touch
 * the two clocks and the two markers.
 *
 * This port has one framebuffer that the host redraws whole every frame, so
 * the strip does NOT survive: it is drawn once, wiped by the next
 * update_frame, and mode 1 then skips redrawing it for ever - which leaves a
 * black band with a focus ring and two clocks floating in it.  The host calls
 * this immediately before each loop_game(3, ...) to say so.  Clearing the
 * flag is not a redraw: mode 1's loc_23BB9 arm also resets word_40E0A /
 * word_40E76 / byte_40E7A, so everything on the strip is put back.
 * replaybar_shot() does the same two stores inline for the same reason.
 */
void replaybar_invalidate(void)
{
	byte_449D8[0] = 0;
	byte_449D8[1] = 0;
}

/* dseg 35398/35401/33xxx: playback speed, the replay-bar toggles.  Declared
 * in externs.h, defined nowhere. */
char byte_449E6;
char replaybar_toggle;
char is_in_replay_copy;
int16_t word_449EA;

/* The archive the 23 shapes come from.  The original loads it in
 * ported_setup_player_cars_ (seg005:3117) and keeps it in sdgameresptr;
 * [DEVIATION] mode 0 loads it here if the host has not, so the bar can be
 * brought up on its own. */
void far* sdgameresptr;

/* ------------------------------------------------------------------ */
/* Host hooks - see rreplaybar.h                                       */
/* ------------------------------------------------------------------ */
int16_t (*replaybar_hook_dialog)(const struct replaybar_dialog*);
int16_t (*replaybar_hook_kb_shortcut)(int16_t code);
int16_t (*replaybar_hook_load_replay)(void);
int16_t (*replaybar_hook_save_replay)(void);
void    (*replaybar_hook_graphics_menu)(void);
void    (*replaybar_hook_present)(void);

/* Returns -1 when no dialog can be shown at all, which every call site
 * below then treats the way the original treats a cancelled one. */
static int16_t rb_dialog(const char* resid, int16_t kind, int16_t nitems,
                         const int16_t* disabled)
{
	struct replaybar_dialog d;
	if (!replaybar_hook_dialog) return -1;
	d.resid = resid;
	d.kind = kind;
	d.nitems = nitems;
	d.disabled = disabled;
	return replaybar_hook_dialog(&d);
}

/* [DEVIATION] the page-flip and software-cursor bracket; see the header. */
static void rb_sprite_copy_2_to_1(void)          { }
static void rb_mouse_draw_opaque_check(void)     { }
static void rb_mouse_draw_transparent_check(void){ }
/* seg005 loc_244B0 calls mouse_minmax_position(byte_3B8F2) to re-clamp the
 * DOS mouse driver's range; SDL owns the pointer here. */
static void rb_mouse_minmax_position(int16_t a)  { (void)a; }

/* ------------------------------------------------------------------ */
/* seg008 3049..3108 mouse_multi_hittest                               */
/*                                                                     */
/* Which of `count` rectangles the pointer is in, or -1.  Instruction   */
/* for instruction, including the early -1 when the game is listening   */
/* to the keyboard rather than the mouse.  Note the bounds are          */
/* inclusive on all four sides, so neighbouring rectangles that share   */
/* an edge - buttons 5 and 6 both claim x = 151 - overlap by a column,  */
/* and the first one in the list wins.  The original's own behaviour.   */
/* ------------------------------------------------------------------ */
int16_t mouse_multi_hittest(int16_t arg_count, const int16_t* arg_x1_array,
                            const int16_t* arg_x2_array,
                            const int16_t* arg_y1_array,
                            const int16_t* arg_y2_array)
{
	int16_t si;

	if (kbormouse == 0) goto loc_28EDA;
	for (si = 0; arg_count > si; si++) {                     /* loc_28EA5 */
		if (arg_x1_array[si] > mouse_xpos) continue;     /* loc_28EA4 */
		if (arg_x2_array[si] < mouse_xpos) continue;
		if (arg_y1_array[si] > mouse_ypos) continue;
		if (arg_y2_array[si] < mouse_ypos) continue;
		return si;
	}
loc_28EDA:
	return -1;
}

/* ------------------------------------------------------------------ */
/* DOS input and DOS timing, on SDL.  [DEVIATION] - see the header.     */
/* ------------------------------------------------------------------ */

/* seg008 input_checking sets this; loop_game's held-button loops test
 * `kbjoyflags & 0x30`.  Bit 0x20 is the left mouse button, 0x10 the right
 * one or any key (loc_2894A.. and _try_ret_mousebut01). */
int16_t kbjoyflags;

/* The original's PIT callback ran at 100 Hz, so one tick is 10 ms. */
static uint32_t s_last_ticks;
static int s_have_ticks;

uint32_t timer_get_delta_alt(void)
{
	uint32_t now = SDL_GetTicks();
	uint32_t d;
	if (!s_have_ticks) { s_have_ticks = 1; s_last_ticks = now; return 0; }
	d = now - s_last_ticks;
	s_last_ticks = now;
	return d / 10;
}

/* seg008 kb_get_key_state(scancode).  loop_game asks for 0x1D, Ctrl. */
static int16_t rb_kb_get_key_state(int16_t scancode)
{
	const uint8_t* k = SDL_GetKeyboardState(NULL);
	if (!k) return 0;
	if (scancode == 0x1D)
		return (int16_t)(k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]);
	return 0;
}

/* seg008 input_checking (2243..2347): one input code, plus the side effects
 * loop_game depends on - mouse_xpos/ypos/butstate and kbjoyflags.  The
 * joystick half is dropped (this port opens no joystick at all), the
 * keyboard half becomes SDL's event queue, and the auto-repeat that
 * input_framecount2/3 implement is SDL's own key repeat. */
static int16_t s_pending_code;

int16_t input_checking(int16_t arg_0)
{
	SDL_Event ev;
	int16_t si = 0;
	static int16_t s_oldbut;                  /* dseg mouse_oldbut */
	static int16_t s_oldx = -1, s_oldy = -1;  /* dseg mouse_oldx / mouse_oldy */

	(void)arg_0;
	/* Drain the queue to the end every call and keep the first key found,
	 * rather than stopping at it.  SDL 2.0.18 puts a POLL SENTINEL at the
	 * tail of each pump and SDL_PollEvent returns 0 when it reaches one, so
	 * a loop that breaks early leaves that sentinel behind and the *next*
	 * call reports "no input" no matter what is queued.  Measured: with the
	 * early break, three keys pushed one per call came back as +, nothing,
	 * -, i.e. one call late from the second key on. */
	while (SDL_PollEvent(&ev)) {
		int16_t code = 0;
		if (ev.type == SDL_QUIT) {
			SDL_Event q = ev;
			SDL_PushEvent(&q);       /* the host's loop must see it too */
			si = 0x1B;
			break;
		}
		if (ev.type != SDL_KEYDOWN) continue;
		kbormouse = 0;                       /* loc_287FD */
		switch (ev.key.keysym.sym) {
		case SDLK_ESCAPE:     code = 0x1B;   break;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:   code = 0x0D;   break;
		case SDLK_SPACE:      code = 0x20;   break;
		case SDLK_UP:         code = 0x4800; break;
		case SDLK_DOWN:       code = 0x5000; break;
		case SDLK_LEFT:       code = 0x4B00; break;
		case SDLK_RIGHT:      code = 0x4D00; break;
		case SDLK_PLUS:
		case SDLK_EQUALS:
		case SDLK_KP_PLUS:    code = '+';    break;
		case SDLK_MINUS:
		case SDLK_KP_MINUS:   code = '-';    break;
		default:
			if (ev.key.keysym.sym > 0 && ev.key.keysym.sym < 0x80)
				code = (int16_t)ev.key.keysym.sym;
			break;
		}
		/* The DOS keyboard ring hands out one code per call and keeps the
		 * rest; s_pending_code is where the rest go. */
		if (si == 0)            si = code;
		else if (code != 0 && s_pending_code == 0) s_pending_code = code;
	}

	rb_read_mouse();
	kbjoyflags = get_kb_or_joy_flags();       /* loc_287FD..loc_28815 */

	/* loc_288A9..loc_28908.  The order matters and it is the original's:
	 * a keypress has just set kbormouse = 0, and if the pointer has moved
	 * or a button has changed since the last call this sets it straight
	 * back to 1.  Without it mouse_multi_hittest returns -1 for ever after
	 * the first keypress, because its first line is `if (!kbormouse)
	 * return -1` - which is how the strip lost its focus ring the moment
	 * anything was typed.  (The one piece not reproduced is loc_28908's
	 * 0x1F4-tick idle timeout that hands control back to the keyboard;
	 * that is a DOS-timer behaviour with no reader here.) */
	if (mouse_xpos != s_oldx || mouse_ypos != s_oldy
	    || mouse_butstate != s_oldbut) {
		s_oldx = mouse_xpos;                             /* loc_288D8 */
		s_oldy = mouse_ypos;
		kbormouse = 1;
	}
	if (mouse_butstate != s_oldbut) {         /* loc_28934 */
		if (mouse_butstate & 1) s_pending_code = 0x20;   /* loc_2894A */
		else if (mouse_butstate & 2) s_pending_code = 0x0D;
		s_oldbut = mouse_butstate;
	}
	if (mouse_butstate & 1) kbjoyflags |= 0x20;   /* _try_ret_mousebut01 */
	if (mouse_butstate & 2) kbjoyflags |= 0x10;

	if (si == 0 && s_pending_code != 0) {         /* _try_ret_mousebutinput */
		si = s_pending_code;
		s_pending_code = 0;
	}
	return si;
}

/* seg008 input_do_checking is a one-line thunk onto input_checking. */
int16_t input_do_checking(int16_t arg_0)
{
	return input_checking(arg_0);
}

/* seg005 loc_24CE7 calls timer_get_counter_unk(50) - a busy wait of 50
 * ticks, half a second, after rewinding to the start.  Kept, but it pumps
 * the window instead of spinning on the PIT. */
static void rb_timer_get_counter_unk(uint32_t ticks)
{
	uint32_t end = SDL_GetTicks() + ticks * 10;
	while ((int32_t)(SDL_GetTicks() - end) < 0) {
		input_do_checking(1);
		if (replaybar_hook_present) replaybar_hook_present();
		SDL_Delay(5);
	}
	timer_get_delta_alt();          /* swallow the wait */
}

/* ------------------------------------------------------------------ */
/* seg005 3391..5365  loop_game                                        */
/* ------------------------------------------------------------------ */
void loop_game(int16_t arg_0, int16_t arg_2, int16_t arg_4)
{
	int16_t var_44, var_42;
	int16_t var_40;
	int16_t var_counter;
	int16_t var_1E;
	int16_t var_18, var_inputcode;
	int16_t var_dlg[8];                 /* [bp+var_14] .. [bp+var_6]      */
	int16_t var_4;
	int16_t var_2;
	int32_t var_acc;                    /* var_22:var_24, one long        */
	int16_t si, di;
	int16_t page;

	if (arg_0 == 0) goto loc_23B70;
	if (arg_0 == 1) goto loc_23BA6;
	if (arg_0 == 2) goto loc_23B8D;
	if (arg_0 == 3) goto loc_23FB8;
	goto loc_24D5E;                                          /* loc_23B6D */

/* -------- mode 0: load the shapes ---------------------------------- */
loc_23B70:
	if (!sdgameresptr)
		sdgameresptr = file_load_resource(3, "sdgame.pvs");
	if (!sdgameresptr) goto loc_24D5E;
	{   /* dseg 13996, aRplyrpicrpacrpmcrptcbof6bof5b - 23 four-character
	     * names with no separators, exactly as the original spells them,
	     * repeated bof0/zoom/pann and all.  See the [ODDITY] above. */
		static char s_names[] =
			"rplyrpicrpacrpmcrptcbof6bof5bof4bof3bof2bof1bof0"
			"zoompannbon6bon5bon4bon3bon2bon1bof0zoompann";
		locate_many_resources((char far*)sdgameresptr, s_names,
		                      (char far**)rplyshapes);
	}
	/* House rule: every 2D shape goes through unflip_shape.  A transposed
	 * shape renders as diagonal streaks and no size check can spot it.
	 * These twenty-three all report flip = 0, so this is a no-op today -
	 * and it stays here so it keeps being one. */
	for (si = 0; si < 23; si++)
		if (rplyshapes[si]) rplyshapes[si] = unflip_shape(rplyshapes[si]);
	arg_2 = 4;
	/* falls through into mode 2, as the original does */

/* -------- mode 2: latch one button --------------------------------- */
loc_23B8D:
	for (si = 0; si < 9; si++)                               /* loc_23B8F */
		byte_40E6A[si] = 0;
	/* [DEVIATION] the original indexes byte_40E6A[arg_2] unchecked; every
	 * call site passes 0..5, and an out-of-range one would corrupt dseg. */
	if (arg_2 >= 0 && arg_2 < 9) byte_40E6A[arg_2] = 1;
	goto loc_24D5E;

/* -------- mode 1: redraw what changed ------------------------------ */
loc_23BA6:
	if (!rplyshapes[0]) goto loc_24D5E;
	page = byte_4432A;
	var_42 = page;
	if (byte_449D8[page] != 0) goto loc_23C66;

	byte_449D8[page] = 1;                                    /* loc_23BB9 */
	byte_40E74[page] = (signed char)0xFF;
	byte_40E08[page] = (signed char)0xFF;
	for (si = 0; si < 9; si++)                               /* loc_23BD0 */
		byte_40E7A[si * 2 + page] = 0;
	rb_mouse_draw_opaque_check();
	shape2d_op_unk(rplyshapes[0]);                    /* "rply", the strip */
	word_40E0A[page] = (int16_t)0xFFFF;
	word_40E76[page] = (int16_t)0xFFFF;               /* loc_23C10 */
	format_frame_as_string(resID_byte1,
	                       (int16_t)(gameconfig.game_recordedframes
	                                 + elapsed_time1), 1);
	font_set_colour((uint16_t)dialog_fnt_colour, 0);  /* font_set_unk */
	font_set_fontdef2(fontledresptr);
	font_draw_text(resID_byte1, 0xD8, 0xBB);          /* sub_345BC */
	font_set_fontdef();

loc_23C66:
	var_42 = (int16_t)(arg_4 + elapsed_time1);
	if (word_40E0A[page] != var_42) {
		word_40E0A[page] = var_42;
		format_frame_as_string(resID_byte1, var_42, 1);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		rb_mouse_draw_opaque_check();
		font_set_fontdef2(fontledresptr);
		font_draw_text(resID_byte1, 0x98, 0xBB);
		font_set_fontdef();
	}

loc_23CD7:
	if (byte_40E74[page] != cameramode) {
		byte_40E74[page] = cameramode;
		word_40E76[page] = (int16_t)0xFFFF;
		rb_mouse_draw_opaque_check();
		if (cameramode >= 0 && cameramode < 4)
			shape2d_op_unk(rplyshapes[1 + cameramode]);
		var_42 = game_camera_buttons_count[(int)cameramode & 3];
		if (var_42 < byte_3E9DB)                         /* loc_23D32 */
			byte_3E9DB = (signed char)var_42;
		if (byte_40E08[page] > 6) byte_40E08[page] = (signed char)0xFF;
	}

loc_23D46:
	if (gameconfig.game_recordedframes == 0) {
		si = 0;
		di = 0;
	} else {                                                 /* loc_23D54 */
		si = (int16_t)(((int32_t)arg_2 * 0x6E)
		               / (int32_t)gameconfig.game_recordedframes);
		di = (int16_t)(((int32_t)arg_4 * 0x6E)
		               / (int32_t)gameconfig.game_recordedframes);
	}

loc_23D94:
	if (word_40E76[page] == si && word_40E04[page] == di) goto loc_23E1A;
	rb_mouse_draw_opaque_check();                            /* loc_23DAB */
	word_40E76[page] = si;
	word_40E04[page] = di;
	/* The scrub control: a 116x6 trough at (154,177), the solid marker at
	 * the frame being shown, the hollow cursor at the frame being sought.
	 * Every number is the original's. */
	sprite_1_unk(0x9A, 0xB1, 0x74, 6, word_407FC);
	sprite_1_unk((int16_t)(si + 0x9A), 0xB1, 6, 6, dialog_fnt_colour);
	sprite_1_unk4((int16_t)(di + 0x9A), 0xB1,
	              (int16_t)(di + 0x9F), 0xB6, word_407FE);

loc_23E1A:
	if (byte_40E08[page] == byte_3E9DB) {
		for (var_counter = 0; var_counter < 7; var_counter++) {  /* loc_23E68 */
			if (byte_40E7A[var_counter * 2 + page]
			    != byte_40E6A[var_counter]) goto loc_23E29;
		}
		goto loc_23FB0;
	}

loc_23E29:
	rb_mouse_draw_opaque_check();
	if (byte_40E08[page] == (signed char)0xFF) goto loc_23EC6;
	var_42 = (int16_t)byte_40E08[page];                      /* loc_23E41 */
	if (byte_40E7A[var_42 * 2 + page] != 0)
		shape2d_op_unk(rplyshapes[14 + var_42]);
	else
		shape2d_op_unk(rplyshapes[5 + var_42]);          /* loc_23E9A */
	byte_40E08[page] = (signed char)0xFF;                    /* loc_23EB3 */

loc_23EC6:
	for (var_counter = 0; var_counter < 7; var_counter++) {   /* loc_23ECA */
		if (byte_40E6A[var_counter] != 0) continue;
		if (byte_40E7A[var_counter * 2 + page] == byte_40E6A[var_counter])
			continue;
		shape2d_op_unk(rplyshapes[5 + var_counter]);
		byte_40E7A[var_counter * 2 + page] = 0;
	}
	for (var_counter = 0; var_counter < 7; var_counter++) {   /* loc_23F25 */
		if (byte_40E6A[var_counter] == 0) continue;
		byte_40E7A[var_counter * 2 + page] = 1;   /* [ODDITY] set twice */
		shape2d_op_unk(rplyshapes[14 + var_counter]);
		byte_40E7A[var_counter * 2 + page] = 1;
	}

	byte_40E08[page] = byte_3E9DB;                           /* loc_23F6C */
	if (byte_3E9DB != (signed char)0xFF) {
		var_44 = (int16_t)byte_3E9DB;
		sprite_1_unk4(game_camera_buttons_x1[var_44],
		              game_camera_buttons_y1[var_44],
		              game_camera_buttons_x2[var_44],
		              game_camera_buttons_y2[var_44], word_407FE);
	}

loc_23FB0:
	rb_mouse_draw_transparent_check();
	goto loc_24D5E;

/* -------- mode 3: one pass of the interactive loop ------------------ */
loc_23FB8:
	var_44 = game_camera_buttons_count[(int)cameramode & 3];
	if (var_44 < byte_3E9DB && cameramode != 2)
		byte_3E9DB = (signed char)var_44;

loc_23FDA:
	rb_sprite_copy_2_to_1();
	if (video_flag5_is0 != 0) byte_4432A = (char)(byte_44346 ^ 1);

loc_23FEE:
	/* [DEVIATION] not in the original: it draws into the page the CRT is
	 * scanning, so there is nothing to push.  This loop can spin for as
	 * long as the strip is paused, so without this the window would be a
	 * frozen rectangle. */
	if (replaybar_hook_present) replaybar_hook_present();
	var_inputcode = input_checking((int16_t)timer_get_delta_alt());
	var_counter = mouse_multi_hittest(
		(int16_t)(game_camera_buttons_count[(int)cameramode & 3] + 1),
		game_camera_buttons_x1, game_camera_buttons_x2,
		game_camera_buttons_y1, game_camera_buttons_y2);
	if (var_counter == -1) goto loc_240D8;

	if (var_counter != byte_3E9DB && var_inputcode == 0)     /* loc_2402E */
		var_inputcode = 1;
	byte_3E9DB = (signed char)var_counter;                   /* loc_24041 */
	if (var_inputcode != 0x20 && var_inputcode != 0x0D) goto loc_2410C;
	if (byte_3E9DB < 7) goto loc_2410C;                      /* loc_24056 */
	if (byte_3E9DB == 7) {                                   /* loc_24060 */
		/* The zoom rocker: which half of it was clicked. */
		if ((int16_t)((game_camera_buttons_y1[7]
		               + game_camera_buttons_y2[7]) >> 1) >= mouse_ypos)
			var_inputcode = 0x4800;                  /* loc_2407A */
		else
			var_inputcode = 0x5000;                  /* loc_24071 */
		goto loc_2410C;
	}
	{   /* loc_24082 - the pan pad: the quadrant the click falls in.
	     * polarAngle returns 1024 units to the circle and 0 is straight up,
	     * so +0x80 rotates by 45 degrees and the top two bits name the
	     * quadrant. */
		int16_t cx = (int16_t)((game_camera_buttons_x1[8]
		                        + game_camera_buttons_x2[8]) >> 1);
		int16_t cy = (int16_t)((game_camera_buttons_y1[8]
		                        + game_camera_buttons_y2[8]) >> 1);
		int16_t q = (int16_t)(((polarAngle((int16_t)(mouse_xpos - cx),
		                                   (int16_t)(cy - mouse_ypos))
		                        + 0x80) & 0x3FF) >> 8);
		if (q == 0)      var_inputcode = 0x4800;         /* loc_2407A */
		else if (q == 1) var_inputcode = 0x4D00;         /* loc_240C8 */
		else if (q == 2) var_inputcode = 0x5000;         /* loc_24071 */
		else if (q == 3) var_inputcode = 0x4B00;         /* loc_240D0 */
		goto loc_2410C;
	}

loc_240D8:
	var_counter = mouse_multi_hittest(1, gameunk_button_x1,
	                                  gameunk_button_x2, gameunk_button_y1,
	                                  gameunk_button_y2);
	if (var_counter == 0
	    && (var_inputcode == 0x20 || var_inputcode == 0x0D))
		var_inputcode = 0x63;                     /* 'c' - loc_24107 */

loc_2410C:
	if (var_inputcode != 0 && var_inputcode != 0x1B) {
		if (replaybar_hook_kb_shortcut
		    && replaybar_hook_kb_shortcut(var_inputcode))
			goto loc_24D5E;
	}

loc_24129:
	if (is_in_replay == 0 && var_inputcode == 0) {
		if (replaybar_enabled == 0) goto loc_24D5E;
		goto loc_24140;
	}

loc_2415A:
	if (replaybar_enabled == 0) {
		is_in_replay_copy = (char)0xFF;
		word_449EA = (int16_t)0xFFFF;
	}
loc_2416C:
	/* byte_40E6C and byte_40E6D are dseg 0x40E6C/0x40E6D, which fall two
	 * and three bytes into byte_40E6A at 0x40E6A - so this reads "is
	 * either Play button still latched?", and if so it latches Pause
	 * instead.  IDA named them separately; they are the same array. */
	if (is_in_replay != 0 && (byte_40E6A[3] != 0 || byte_40E6A[2] != 0))
		loop_game(2, 4, 0);                              /* loc_24181 */

loc_24193:
	loop_game(1, state.game_frame, state.game_frame);
	var_40 = 0;
	if (rb_kb_get_key_state(0x1D) != 0) var_40 = 1;          /* Ctrl */
	else if (byte_3E9DB == 8 && (kbjoyflags & 0x30) != 0) var_40 = 1;

loc_241CC:
	if (var_40 != 0) {
		switch (var_inputcode) {
		case '+':    goto loc_2429C;
		case '-':    goto loc_2426E;
		case 0x4800: goto loc_24242;
		case 0x4B00: goto loc_24236;
		case 0x4D00: goto loc_2422A;
		case 0x5000: goto loc_24258;
		default:     var_inputcode = 0;  break;          /* loc_241F9 */
		}
	}

loc_241FE:
	if (var_inputcode == '+')  goto loc_2429C;
	/* `jbe` - unsigned, so the extended codes 0x4800..0x5000 and '-' all
	 * land above '+' and go to loc_24D32. */
	if ((uint16_t)var_inputcode > (uint16_t)'+') goto loc_24D32; /* loc_24209 */
	if (var_inputcode == 0x0D) goto loc_24334;
	if (var_inputcode == 0x1B) goto loc_24346;
	if (var_inputcode == 0x20) goto loc_24334;
	goto loc_242E7;                                          /* loc_24226 */

/* -------- the camera, panned and zoomed ---------------------------- */
loc_2422A:
	custom_camera_azimuth_angle = (int16_t)(custom_camera_azimuth_angle + 0x10);
	goto loc_24D5E;
loc_24236:
	custom_camera_azimuth_angle = (int16_t)(custom_camera_azimuth_angle - 0x10);
	goto loc_24D5E;
loc_24242:
	if ((int16_t)(custom_camera_elevation_angle + 0x10) >= 0x100) {
		var_inputcode = 0;                               /* loc_241F9 */
		goto loc_241FE;
	}
	custom_camera_elevation_angle =
		(int16_t)(custom_camera_elevation_angle + 0x10);
	goto loc_24D5E;
loc_24258:
	if ((int16_t)(custom_camera_elevation_angle - 0x10) <= (int16_t)0xFF00) {
		var_inputcode = 0;
		goto loc_241FE;
	}
	custom_camera_elevation_angle =
		(int16_t)(custom_camera_elevation_angle - 0x10);
	goto loc_24D5E;
loc_2426E:                                       /* '-' : zoom out       */
	if (cameramode == 3) {
		if (word_44D20 <= 0) { var_inputcode = 0; goto loc_241FE; }
		word_44D20 = (int16_t)(word_44D20 - 0x1E);       /* loc_2427F */
		goto loc_24D5E;
	}
	if (custom_camera_distance >= 0x5DC) {                   /* loc_24288 */
		var_inputcode = 0; goto loc_241FE;
	}
	custom_camera_distance = (int16_t)(custom_camera_distance + 0x1E);
	goto loc_24D5E;
loc_2429C:                                       /* '+' : zoom in        */
	if (cameramode == 3) {
		if (word_44D20 >= 0x384) { var_inputcode = 0; goto loc_241FE; }
		word_44D20 = (int16_t)(word_44D20 + 0x1E);       /* loc_242AE */
		goto loc_24D5E;
	}
	if (custom_camera_distance <= 0x78) {                    /* loc_242B6 */
		var_inputcode = 0; goto loc_241FE;
	}
	custom_camera_distance = (int16_t)(custom_camera_distance - 0x1E);
	goto loc_24D5E;

/* -------- moving the focus -----------------------------------------
 *
 * Only Left is clamped against game_camera_buttons_count; Right, Up and
 * Down jump straight to loc_242E4 without the compare.  That is not a
 * missing check - Left is the only direction whose table ever *raises* the
 * index (0->1, 1->7, 6->7, 7->8), so it is the only one that can walk off
 * the end of the controls this camera mode has. */
loc_242C8:                                       /* left                 */
	var_44 = (int16_t)byte_3E9DC[(int)byte_3E9DB];
	if (game_camera_buttons_count[(int)cameramode & 3] < var_44)
		goto loc_242E7;
	goto loc_242E4;
loc_242FE:                                       /* right                */
	var_44 = (int16_t)byte_3E9E6[(int)byte_3E9DB];
	goto loc_242E4;
loc_2430A:                                       /* up                   */
	if (byte_3E9DB == 7) goto loc_2429C;
	var_44 = (int16_t)byte_3E9F0[(int)byte_3E9DB];
	goto loc_242E4;
loc_2431E:                                       /* down                 */
	if (byte_3E9DB == 7) goto loc_2426E;
	var_44 = (int16_t)byte_3E9FA[(int)byte_3E9DB];        /* loc_24328 */
loc_242E4:
	byte_3E9DB = (signed char)var_44;

loc_242E7:
	loop_game(1, state.game_frame, state.game_frame);
	goto loc_23FEE;

/* -------- pressing the focused control ----------------------------- */
loc_24334:
	/* cbw then `cmp ax, 6 / ja` - sign-extend, then compare UNSIGNED, so
	 * 0xFF ("nothing focused") lands above 6 and falls out.  Both halves
	 * matter; either one alone gets this wrong. */
	if ((uint16_t)(int16_t)byte_3E9DB > 6) goto loc_242E7;
	switch ((int)byte_3E9DB) {                       /* off_24D20 */
	case 0: goto loc_24830;                          /* fast forward */
	case 1: goto loc_24A28;                          /* rewind       */
	case 2: goto loc_24D04;                          /* play fast    */
	case 3: goto loc_24C5A;                          /* play         */
	case 4: goto loc_24C74;                          /* stop         */
	case 5: goto loc_24CA6;                          /* to the start */
	default: goto loc_24346;                         /* the menu     */
	}

/* -------- the pause menu ------------------------------------------- */
loc_24346:
	is_in_replay = 1;
	audio_carstate();
	loop_game(2, 4, 0);
	loop_game(1, state.game_frame, state.game_frame);
	for (si = 0; si < 8; si++) var_dlg[si] = 0;              /* loc_24377 */
	if (state.playerstate.car_crashBmpFlag != 0) var_dlg[3] = 1;
	/* loc_24394, exactly as written: no recording at all disables it, and
	 * so does having one with elapsed_time1 already counted - but a
	 * recording with elapsed_time1 == 0 does not.  [ODDITY] kept. */
	if (gameconfig.game_recordedframes == 0 || elapsed_time1 != 0)
		var_dlg[5] = 1;
	if (passed_security == 0) {                              /* loc_243A7 */
		var_dlg[2] = 1;
		var_dlg[3] = 1;
	}
	if ((byte_43966 & 4) == 0) var_dlg[1] = 1;               /* loc_243B8 */
	/* loc_243C4 copies video_flag6_is1 into byte_454A4 for show_dialog's
	 * benefit; video_flag6_is1 is one more externs.h declaration nothing
	 * defines, and with show_dialog replaced the copy has no reader. */
	var_2 = rb_dialog("men", 2, 8, var_dlg);
	var_2 = (int16_t)(signed char)var_2;
	if ((uint16_t)(var_2 - 1) > 6) goto loc_24828;
	switch (var_2 - 1) {                                     /* off_2481A */
	case 0: goto loc_24748;
	case 1: goto loc_24416;
	case 2: goto loc_2444C;
	case 3: goto loc_2450A;
	case 4: goto loc_24630;
	case 5: goto loc_24776;
	default: goto loc_24760;
	}

loc_24416:                             /* restart the race               */
	check_input();
	framespersec = framespersec2;
	/* mov byte ptr gameconfig.game_framespersec, al - the low byte only */
	gameconfig.game_framespersec =
		(uint16_t)((gameconfig.game_framespersec & 0xFF00)
		           | (framespersec2 & 0x00FF));
	init_game_state(-1);
	elapsed_time2 = 0;
	gameconfig.game_recordedframes = 0;
	/* mov byte ptr word_45D3E, 0 - likewise the low byte only */
	word_45D3E = (int16_t)(word_45D3E & (int16_t)0xFF00);
	byte_43966 = 1;
	goto loc_244B0;

loc_2444C:                             /* carry on driving from here     */
	if ((byte_43966 & 2) != 0) goto loc_24453;
	if (gameconfig.game_recordedframes == elapsed_time2) {   /* loc_2445A */
		byte_43966 = 1;                                  /* loc_244A2 */
		goto loc_244A7;
	}
	/* "this will overwrite the rest of the recording" */
	si = rb_dialog("con", 2, 0, NULL);
	if (si < 1) goto loc_24828;                              /* loc_2449F */
loc_24453:
	byte_43966 = 3;
loc_244A7:
	elapsed_time2 = state.game_frame;
	gameconfig.game_recordedframes = state.game_frame;

loc_244B0:
	dashb_toggle = 1;
	show_penalty_counter = 0;
	followOpponentFlag = 0;
	game_replay_mode = 0;
	cameramode = 0;
	state.game_3F6autoLoadEvalFlag = 0;
	state.game_frame_in_sec = 0;
	byte_449E6 = 0;
	loop_game(2, 3, 0);
	is_in_replay = 0;
	/* mouse_minmax_position(byte_3B8F2) - byte_3B8F2 is another externs.h
	 * declaration with no definition, and the callee is a no-op here. */
	rb_mouse_minmax_position(0);
	check_input();
	kbormouse = 0;
	goto loc_24828;

loc_2450A:                             /* load a replay                  */
	byte_43966 = 0;
	audio_carstate();
	/* [DEVIATION] the original's do_fileselect_dialog +
	 * ported_file_load_replay_ + the track/car comparison that decides
	 * whether the cars need rebuilding; see "WHAT IS NOT HERE". */
	if (replaybar_hook_load_replay && replaybar_hook_load_replay()) {
		framespersec = (uint16_t)(uint8_t)gameconfig.game_framespersec;
		init_game_state(-1);
	}
	goto loc_24828;

loc_24630:                             /* save the replay                */
	audio_carstate();
loc_24635:
	var_1E = 0;
	/* [DEVIATION] do_savefile_dialog, file_build_path, file_find, the
	 * "fex" overwrite dialog, ported_file_write_replay_ and the "ser"
	 * failure box.  The hook stands in for all six. */
	if (replaybar_hook_save_replay) var_1E = replaybar_hook_save_replay();
	(void)var_1E;
	goto loc_24828;

loc_24748:                             /* carry on watching              */
	update_crash_state(4, 0);
loc_24757:
	byte_449DA = 2;
	goto loc_24828;

loc_24760:                             /* give up                        */
	update_crash_state(4, 0);
	byte_43966 = 0;
	goto loc_24757;

loc_24776:                             /* the display sub-menu           */
	for (si = 0; si < 5; si++) var_dlg[si] = 0;              /* loc_24778 */
	if (gameconfig.game_opponenttype == 0) var_dlg[4] = 1;
	var_2 = rb_dialog("mdo", 2, 5, var_dlg);
	/* [DEVIATION] 0 is a real selection here (it toggles the dashboard),
	 * so "nothing was chosen" cannot be spelled 0 the way it can in the
	 * "men" menu above.  rb_dialog answers -1 and this drops out. */
	if (var_2 < 0) goto loc_24828;
	var_2 = (int16_t)(signed char)var_2;
	switch (var_2) {
	case 0: dashb_toggle ^= 1;                     break;    /* loc_247E8 */
	case 1: replaybar_toggle ^= 1;                 break;    /* loc_247F0 */
	case 2:                                                  /* loc_247F8 */
		cameramode++;
		if (cameramode == 4) cameramode = 0;
		break;
	case 3:                                                  /* loc_2480A */
		if (replaybar_hook_graphics_menu) replaybar_hook_graphics_menu();
		break;
	case 4: followOpponentFlag ^= 1;               break;    /* loc_24812 */
	default: break;
	}

loc_24828:
	check_input();
	goto loc_24D5E;

/* -------- button 0: fast forward ----------------------------------- */
loc_24830:
	is_in_replay = 1;
	audio_carstate();
	loop_game(2, 0, 0);
	timer_get_delta_alt();
	var_acc = 0x14;
	goto loc_248F4;
loc_2485C:
	di = (int16_t)(var_acc / 50);
	di = (int16_t)(di + 3);
	if (di > 100) di = 100;                                  /* loc_2487A */
	var_18 = (int16_t)timer_get_delta_alt();
	si = (int16_t)(var_18 * di);
	var_acc += si;
	var_44 = (int16_t)(gameconfig.game_recordedframes - elapsed_time2);
	if ((int16_t)(var_acc / 20) > var_44)                    /* loc_248C4 */
		var_acc = (int32_t)var_44 * 20;
	loop_game(1, state.game_frame,
	          (int16_t)((int16_t)(var_acc / 20) + elapsed_time2));
	input_do_checking(var_18);
	if (replaybar_hook_present) replaybar_hook_present();
loc_248F4:
	if ((kbjoyflags & 0x30) != 0) goto loc_2485C;
loc_248FE:
	var_44 = (int16_t)(gameconfig.game_recordedframes - elapsed_time2);
	if ((int16_t)(var_acc / 20) > var_44)
		var_acc = (int32_t)var_44 * 20;
	si = (int16_t)((int16_t)(var_acc / 20) + elapsed_time2); /* loc_24935 */
	if ((int16_t)gameconfig.game_recordedframes < si)
		si = (int16_t)gameconfig.game_recordedframes;
	restore_gamestate(si);                                   /* loc_24956 */
	elapsed_time2 = (uint16_t)si;
	loop_game(2, 4, 0);
	rb_draw_waiting();
	goto loc_24A10;
loc_249F8:
	update_gamestate();
	loop_game(1, state.game_frame, (int16_t)elapsed_time2);
loc_24A10:
	if (state.game_frame != (int16_t)elapsed_time2) goto loc_249F8;
loc_24A19:
	input_do_checking(0x3E8);
	goto loc_24D5E;

/* -------- button 1: rewind ----------------------------------------- */
loc_24A28:
	is_in_replay = 1;
	audio_carstate();
	loop_game(2, 1, 0);
	timer_get_delta_alt();
	var_acc = 0x14;
	goto loc_24AEA;
loc_24A58:
	di = (int16_t)(var_acc / 50);
	di = (int16_t)(di + 3);
	if (di > 100) di = 100;                                  /* loc_24A76 */
	var_18 = (int16_t)timer_get_delta_alt();
	si = (int16_t)(var_18 * di);
	var_acc += si;
	if ((int16_t)(var_acc / 20) > (int16_t)elapsed_time2)    /* loc_24AB8 */
		var_acc = (int32_t)elapsed_time2 * 20;
	loop_game(1, state.game_frame,
	          (int16_t)(elapsed_time2 - (int16_t)(var_acc / 20)));
	input_do_checking(var_18);
	if (replaybar_hook_present) replaybar_hook_present();
loc_24AEA:
	if ((kbjoyflags & 0x30) != 0) goto loc_24A58;
loc_24AF4:
	if ((int16_t)(var_acc / 20) > (int16_t)elapsed_time2)
		var_acc = (int32_t)elapsed_time2 * 20;
	di = (int16_t)(var_acc / 20);                            /* loc_24B23 */
	loop_game(2, 4, 0);
	if (di == 0) goto loc_24C43;
	rb_draw_waiting();                                       /* loc_24B4F */
loc_24BD4:
	si = (int16_t)(elapsed_time2 - di);
	restore_gamestate(si);
	elapsed_time2 = (uint16_t)si;
	var_4 = (int16_t)(si - state.game_frame);
	if (var_4 == 0) goto loc_24C43;
	si = var_4;
	goto loc_24C3A;
loc_24BF8:
	update_gamestate();
	si--;
	loop_game(1, (int16_t)(elapsed_time2
	                       + (int16_t)(((int32_t)si * di) / var_4)),
	          (int16_t)elapsed_time2);
	input_do_checking(1);
	if (replaybar_hook_present) replaybar_hook_present();
loc_24C3A:
	if (state.game_frame != (int16_t)elapsed_time2) goto loc_24BF8;
loc_24C43:
	loop_game(1, state.game_frame, state.game_frame);
	goto loc_24A19;

/* -------- buttons 2..5 --------------------------------------------- */
loc_24C5A:                             /* play, normal speed             */
	byte_449E6 = 0;
	loop_game(2, 3, 0);
	goto loc_24D18;

loc_24C74:                             /* stop                           */
	is_in_replay = 1;
	audio_carstate();
	loop_game(2, 4, 0);
	loop_game(1, state.game_frame, state.game_frame);
	goto loc_242E7;

loc_24CA6:                             /* back to the start              */
	is_in_replay = 1;
	audio_carstate();
	loop_game(2, 5, 0);
	loop_game(1, state.game_frame, state.game_frame);
	restore_gamestate(0);
	rb_timer_get_counter_unk(0x32);
	loop_game(2, 4, 0);
	goto loc_24140;

loc_24D04:                             /* play fast                      */
	loop_game(2, 2, 0);
	byte_449E6 = 3;
loc_24D18:
	is_in_replay = 0;
	goto loc_242E7;

loc_24140:
	loop_game(1, state.game_frame, state.game_frame);
	goto loc_24D5E;

loc_24D32:
	if (var_inputcode == '-')    goto loc_2426E;
	if (var_inputcode == 0x4800) goto loc_2430A;
	if (var_inputcode == 0x4B00) goto loc_242C8;
	if (var_inputcode == 0x4D00) goto loc_242FE;
	if (var_inputcode == 0x5000) goto loc_2431E;
	goto loc_242E7;                                          /* loc_24D5A */

loc_24D5E:
	return;
}

/*
 * seg005 loc_2495D / loc_24B4F - the "please wait" line the two scrub
 * buttons print while the simulation catches up.  copy_string pulls the
 * text out of the game resource under the key "wai"; font_op2_alt centres
 * it; intro_draw_text draws it at y = 0x64 with a shadow.  The
 * slow_video_mgmt_copy branch only adds a dirty-rect union, which this port
 * has no use for (it redraws whole frames), so the two branches collapse
 * into one.
 */
static void rb_draw_waiting(void)
{
	static char s_wai[] = "wai";
	char far* p = gameresptr ? locate_text_res((char far*)gameresptr, s_wai)
	                         : NULL;
	if (p) {
		snprintf(resID_byte1, sizeof resID_byte1, "%s", (const char*)p);
	} else {
		resID_byte1[0] = 0;
		return;
	}
	intro_draw_text(resID_byte1,
	                (int16_t)((320 - (int16_t)font_op2(resID_byte1)) / 2),
	                0x64, dialog_fnt_colour, 0);
}

/*
 * The frame readout on its own, kept from the first cut of this file. It is
 * what loop_game mode 1 prints at (0x98,0xBB) and (0xD8,0xBB) with the LED
 * font; this is the fallback for a host that wants the number without the
 * strip.
 */
void replaybar_draw(int16_t frame, int16_t total)
{
	char now[16], all[16], line[40];

	if (height_above_replaybar >= 0xC8) return;   /* no strip, nothing to draw */

	format_frame_as_string(now, frame, 0);
	format_frame_as_string(all, total, 0);
	snprintf(line, sizeof line, "%s / %s", now, all);
	font_set_colour((uint16_t)dialog_fnt_colour, 0);
	font_draw_text(line, 8, (int16_t)(height_above_replaybar + 6));
}

/* ------------------------------------------------------------------ */
/* STUNTS_REPLAYBAR_SHOT                                               */
/* ------------------------------------------------------------------ */
static int rb_env_int(const char* name, int deflt)
{
	const char* s = getenv(name);
	return s && *s ? atoi(s) : deflt;
}

static int rb_write_bmp(const char* path)
{
	const int w = RFB_VIEW_W, h = RFB_VIEW_H;
	uint32_t rowb = (uint32_t)w * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)h;
	uint8_t hdr[54];
	int32_t bw = w, bh = h;
	uint32_t fsz = 54 + img;
	uint8_t* buf;
	FILE* f;
	int x, y;

	buf = (uint8_t*)calloc(1, img);
	if (!buf) return 0;
	for (y = 0; y < h; y++) {
		uint8_t* row = buf + (uint32_t)(h - 1 - y) * stride;
		for (x = 0; x < w; x++) {
			int32_t o = (int32_t)y * w + x;
			uint32_t p = rs_rgba ? rs_rgba[o]
			           : (rs_pal ? rs_pal[rfb_pixels[o]]
			                     : (uint32_t)(rfb_pixels[o] * 0x010101u));
			row[x * 3 + 0] = (uint8_t)p;
			row[x * 3 + 1] = (uint8_t)(p >> 8);
			row[x * 3 + 2] = (uint8_t)(p >> 16);
		}
	}
	memset(hdr, 0, sizeof hdr);
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4); hdr[10] = 54; hdr[14] = 40;
	memcpy(hdr + 18, &bw, 4); memcpy(hdr + 22, &bh, 4);
	hdr[26] = 1; hdr[28] = 24; memcpy(hdr + 34, &img, 4);
	f = fopen(path, "wb");
	if (!f) { free(buf); return 0; }
	fwrite(hdr, 1, sizeof hdr, f);
	fwrite(buf, 1, img, f);
	fclose(f);
	free(buf);
	return 1;
}

int replaybar_shot(const char* path)
{
	int16_t latch, focus, frame, target, total, cam;

	if (!path) path = getenv("STUNTS_REPLAYBAR_SHOT");
	if (!path || !*path) return 0;

	cam    = (int16_t)rb_env_int("STUNTS_REPLAYBAR_CAMERA", 0);
	latch  = (int16_t)rb_env_int("STUNTS_REPLAYBAR_LATCH",  4);
	focus  = (int16_t)rb_env_int("STUNTS_REPLAYBAR_FOCUS",  2);
	total  = (int16_t)rb_env_int("STUNTS_REPLAYBAR_TOTAL",  1200);
	frame  = (int16_t)rb_env_int("STUNTS_REPLAYBAR_FRAME",  400);
	target = (int16_t)rb_env_int("STUNTS_REPLAYBAR_TARGET", 700);

	/* The four conditions seg005:498 asks for, so the strip exists. */
	dashb_toggle = 1;
	followOpponentFlag = 0;
	game_replay_mode = 2;
	replaybar_enabled = 1;
	cameramode = (char)(cam & 3);
	replaybar_view_height();

	gameconfig.game_recordedframes = (uint16_t)total;
	elapsed_time1 = 0;
	elapsed_time2 = (uint16_t)frame;
	state.game_frame = frame;

	/* The original's host does this before every mode-1 call
	 * (seg005 loc_21FC2). */
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);

	loop_game(0, 0, 0);
	if (!rplyshapes[0]) {
		fprintf(stderr, "replaybar_shot: SDGAME.PVS gav inga former\n");
		return 0;
	}
	loop_game(2, latch, 0);
	byte_3E9DB = (signed char)focus;
	byte_449D8[0] = 0;                 /* force the background down again */
	byte_449D8[1] = 0;
	loop_game(1, frame, target);

	if (!rb_write_bmp(path)) {
		fprintf(stderr, "replaybar_shot: kan inte skriva %s\n", path);
		return 0;
	}
	printf("replaybar: %s (kamera %d, tand knapp %d, fokus %d, "
	       "ruta %d av %d, mal %d)\n",
	       path, cam, latch, focus, frame, total, target);
	return 1;
}
