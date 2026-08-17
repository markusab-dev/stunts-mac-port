/*
 * rreplaybar.c - Phase 9, the in-race recording bar.
 *
 * `loop_game` (seg005 3391..5365) is not the main loop, despite the name: it
 * is the VCR strip under the race - pause, scrub, save and load a replay, and
 * the camera pan and zoom. This file is where it goes. What is here so far:
 *
 *   check_input   seg008 3109..3147   ported below
 *
 * and the two controls that matter most are already usable without the drawn
 * strip: `P` pauses and Backspace rewinds one snapshot interval, wired into
 * the race loop in main_native.c. Both ride on the mechanism ported in
 * sfasm_port.c - twenty 1120-byte snapshots at cvxptr, written by
 * update_gamestate every `word_45A00` frames and put back by
 * restore_gamestate, RNG seed included so the simulation carries on
 * identically. That is measured, not assumed: rewinding to frames 0, 600 and
 * 1200 restores all 1120 bytes byte for byte.
 *
 * Still to come here, with the scouting written up in
 * docs/HANDOVER_PHASE9_11.md: the drawn strip itself, save/load replay, and
 * the camera controls. The one other routine it needs that this tree lacks is
 * a full `init_game_state` (seg001 3885..4021); everything else it calls is
 * already ported.
 *
 * `height_above_replaybar` (externs.h) is the field the strip shrinks the 3D
 * view into. It is declared and never set anywhere in this port, which is
 * consistent - nothing shrinks the view until the strip exists.
 */
#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "externs.h"

extern int16_t rintro_input(int16_t ticks);

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
 * get_kb_or_joy_flags: the original returns a flag word whose 0x30 pair means
 * "a key or a joystick button is down". This port reads no joystick (see
 * docs/HANDOVER_PHASE9_11.md), so only the keyboard half can be answered, and
 * it is answered honestly: 0x10 when any key is held, 0 otherwise.
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

int16_t get_kb_or_joy_flags(void)
{
	const uint8_t* k;
	int n = 0, i;
	uint32_t b;

	SDL_PumpEvents();
	b = SDL_GetMouseState(NULL, NULL);
	mouse_butstate = (int16_t)(((b & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0)
	                         | ((b & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 2 : 0));

	k = SDL_GetKeyboardState(&n);
	for (i = 0; i < n; i++)
		if (k[i]) return 0x10;
	return 0;
}

/*
 * seg005:498..520 - how tall the 3D view is.
 *
 * The strip only appears under four conditions together, and the original
 * spells them out:
 *
 *     if (dashb_toggle && !followOpponentFlag
 *         && game_replay_mode == 2 && replaybar_enabled)
 *              height_above_replaybar = 0x97;   151
 *     else     height_above_replaybar = 0xC8;   200
 *
 * so the bar is a *playback* control: it is never there while you are
 * driving, and it goes away in the opponent's camera. 200 - 151 = 49 rows.
 *
 * replaybar_enabled is declared in externs.h and, like the four before it
 * this session, was defined nowhere. Default on: the original ships with the
 * bar available in playback.
 */
char replaybar_enabled = 1;

/* Sixth of the same kind: externs.h:277 declares it, nothing defined it.
 * 200 = the full-height view, which is what every screen before the strip
 * exists needs. */
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

/*
 * The strip itself. loop_game draws its buttons from MISC.PRE and hit-tests
 * them with mouse_multi_hittest; that part is still to come (see
 * docs/HANDOVER_PHASE9_11.md). What is here is the frame readout, which is
 * the one piece of it that is information rather than chrome:
 * format_frame_as_string turns a frame count into the mm:ss.ss the game
 * shows everywhere else.
 */
void replaybar_draw(int16_t frame, int16_t total)
{
	extern void format_frame_as_string(char* s, int16_t time, int16_t c);
	extern void font_draw_text(const char* s, int16_t x, int16_t y);
	extern void font_set_colour(uint16_t fg, uint16_t bg);
	extern int16_t dialog_fnt_colour;
	char now[16], all[16], line[40];

	if (height_above_replaybar >= 0xC8) return;   /* no strip, nothing to draw */

	format_frame_as_string(now, frame, 0);
	format_frame_as_string(all, total, 0);
	snprintf(line, sizeof line, "%s / %s", now, all);
	font_set_colour((uint16_t)dialog_fnt_colour, 0);
	font_draw_text(line, 8, (int16_t)(height_above_replaybar + 6));
}
