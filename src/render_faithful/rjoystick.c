/*
 * rjoystick.c - the joystick.  Two pieces: the input path, and seg008's
 * nine-square calibration screen.  The input path comes first, because a
 * calibration screen on its own would carefully measure a device that
 * nothing then steers with.
 *
 * Ported from reference/restunts/src/restunts/asm (IDA) and, for the four
 * routines restunts1 only carries as `extrn` in seg012.inc, from
 * reference/restunts2/src/asm (Ghidra):
 *
 *   get_joy_flags        restunts2 seg012 3850..4041   the gameport read
 *   sub_307B4            restunts2 seg012 4043..4050   start calibrating
 *   sub_307D2            restunts2 seg012 4052..4066   nibble -> 3x3 cell
 *   sub_307E3            restunts2 seg012 4068..4078   the analog wheel
 *   get_kb_or_joy_flags  restunts2 seg012 3761..3820   (used by rreplaybar.c)
 *   show_dialog          restunts1 seg008  334..1206   the layout half only
 *   do_joy_restext       restunts1 seg008 4708..4993   the screen
 *   replay_unk           restunts1 seg005 3301..3390   wheel -> steer bits
 *   loc_2279A..loc_227C8 restunts1 seg005 1319..1352   how driving reads it
 *
 * ------------------------------------------------------------------------
 * WHAT THE FLAG BYTE MEANS
 *
 * There is one bitfield and the whole game speaks it.  get_joy_flags builds
 * it from the gameport, get_kb_or_joy_flags builds the same thing from ten
 * keyboard scancodes and falls through to get_joy_flags when no key is
 * down, and seg005's driving loop reads whichever it gets:
 *
 *     0x01 up      0x02 down     0x04 right
 *     0x08 left    0x10 button1  0x20 button2
 *
 * The recorded driving byte in td16_rpl_buffer is the same bitfield, and
 * that is not a coincidence - seg005:1352 is literally `si = flags & 0x33`.
 * statecar.c reads it back as:
 *
 *     arg_carInputByte & 3         1 accelerate, 2 brake   (loc_17CEA)
 *     (arg_carInputByte >> 2) & 3  1 right, 2 left         (player_op)
 *     0x10                         car_current_gear++      (loc_17B0F)
 *     0x20                         car_current_gear--      (loc_17B2E)
 *
 * [ODDITY] src/main_native.c:2646 documents 0x10/0x20 as "bit4 down, bit5
 * up".  They are gear up (0x10) and gear down (0x20) - loc_17B0F does
 * `inc car_current_gear`, loc_17B2E does `dec`.  main_native.c binds
 * LSHIFT->0x10 and SPACE->0x20, which is upshift/downshift, so only the
 * comment is wrong.  Left alone here; main_native.c is not ours to edit.
 *
 * ------------------------------------------------------------------------
 * THE STEERING IS ANALOG AND THEN IT IS NOT
 *
 * Worth spelling out, because it is the one genuinely surprising thing in
 * this corner of the game.  With a joystick selected, seg005:1352 masks the
 * flag byte with 0x33 - it throws the digital left/right bits AWAY.  The
 * stick's x axis goes instead through sub_307E3 (raw count -> -31..+31),
 * then through the byte_3E85C curve (dead zone plus a soft ramp) into a
 * -127..+127 *wheel target*, which is parked in byte_44292[frame & 0x3F].
 *
 * replay_unk (seg005 3301..3390) then runs a servo: it compares the car's
 * current car_steeringAngle against that target, and ORs a single 4 (right)
 * or 8 (left) into td16_rpl_buffer for this frame - the step size coming
 * from steerWhlRespTable_ptr, quadrupled while the wheel is crossing centre.
 * So the analog stick is converted back into the same two digital bits the
 * keyboard produces, one frame at a time, which is exactly why a joystick
 * replay is byte-compatible with a keyboard one.
 *
 * Both halves are ported here.  [DEVIATION] the original stores the wheel
 * target in byte_44292[elapsed_time2 & 0x3F] and consumes it a frame later
 * out of byte_44292[state.game_frame & 0x3F]; this port does both in the
 * one rjoy_or_input() call, which is the same values as long as the host
 * samples input for the frame it is about to simulate.  main_native.c does
 * (elapsed_time2 is assigned state.game_frame right after update_gamestate),
 * so the 64-entry ring buys nothing here and is not carried over.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] WHERE THE NUMBERS COME FROM
 *
 * The original's get_joy_flags is a capacitor timer: it strobes port 0x201
 * and counts loop iterations until each one-shot decays, so joyflag1 and
 * joyflag2 are *durations*, on an unknown scale that differs per machine
 * and per stick.  That is the whole reason a calibration screen exists.
 *
 * joy_read_axes() below is the only place SDL appears in the flag path.  It
 * fills the same four variables - joyflag1, joyflag2, joybutton - from
 * SDL's axes, mapped to 0..1023 so the original's auto-calibration
 * arithmetic (0x4000 / range, thresholds at quarter and three-quarter) has
 * the same shape of input it was written for.  Everything downstream of
 * that line is the original's, oddities included.
 *
 * ------------------------------------------------------------------------
 * WHAT IS NOT HERE
 *
 *  - input_checking's joystick-to-keystroke half (seg008 2267..2325):
 *    joyflags/newjoyflags/joyinputcode turn a stick waggle into 4800h/5000h
 *    etc. so the stick can drive the MENUS.  That belongs with a ported
 *    input_checking, which this tree does not have (rintro_input stands in
 *    for it).  get_kb_or_joy_flags does answer the joystick now, so
 *    check_input's debounce already sees the buttons.
 *  - do_key_restext / do_mou_restext and the five other four-line routines
 *    at seg008 4994..5246.  Already covered elsewhere in this port.
 *  - sub_275C6 (the dialog's saved-screen restore) and audio_unk/sub_372F4
 *    (the audio driver's pause/resume pair).  This port has one framebuffer
 *    and its own audio; both are no-ops here and are marked at the call.
 *
 * ------------------------------------------------------------------------
 * OVERLAP TO COLLAPSE AT INTEGRATION
 *
 * show_dialog_layout() below is a subset of seg008 show_dialog - the two
 * measuring passes and the '@' recording, which is all do_joy_restext needs.
 * A sibling deliverable, src/render_faithful/rdialog.c, ports the whole of
 * show_dialog and is not in tools/build_native.sh either, so the two were
 * written in parallel and neither could depend on the other.  They agree
 * exactly where they overlap: rdialog's `fields[]` is this file's `g.at[]`,
 * both filled as x, y, x, y in '@' order, and both return var_9E / 2 for
 * mode 3.
 *
 * If rdialog.c lands, delete show_dialog_layout and struct dlg3 and call
 *
 *     struct RDIALOG d;
 *     rdialog_open(&d, 3, 1, j->text, 0xFFFF, 0xFFFF, dialogarg2, NULL, 0);
 *     rdialog_draw(&d);
 *
 * then read the seven pairs out of d.fields[] instead of g.at[] and take the
 * count from rdialog_immediate_result(&d).  Nothing else in this file has to
 * change; joycal_geometry already takes its ten numbers by index.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "externs.h"
#include "rfbsize.h"
#include "rwidgets.h"
#include "rjoystick.h"

extern uint8_t rfb_pixels[];
extern uint32_t* rs_rgba;          /* rshape2d.c: truecolour target, NULL = indexed */
extern int16_t dialog_fnt_colour;
extern int16_t fontdef_unk_0E;
extern uint16_t font_op2(const char* str);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern void font_set_colour(uint16_t colour, uint16_t unused);
extern void font_set_fontdef2(void far* data);
extern char far* locate_text_res(char far* res, char* key);

/* ------------------------------------------------------------------ */
/* dseg globals.  Every one of these is declared nowhere or declared    */
/* and defined nowhere in this tree - checked with grep before writing  */
/* a line of it, per the house rule.  Addresses and initial values from */
/* restunts2/src/asm/dseg.asm 4354..4377 and 4451, 4993..4997.         */
/* ------------------------------------------------------------------ */

/* dseg 0x3FE00.  bit 0 = "the joystick is the driving input device".
 * do_key_restext (seg008:5024) and do_mou_restext clear it; do_joy_restext
 * sets it and then clears it again if a square was missed. */
char byte_3FE00;

/* dseg 0x3FB0E..0x3FB17 - the raw reading and the button latch. */
static int16_t joyflag1;           /* x */
static int16_t joyflag2;           /* y */
static char    joybutton;
static char    joyinput;

/* dseg 0x3FB18..0x3FB25 - the x axis' running calibration.
 *   word_3FB18 min      word_3FB1A candidate min   word_3FB1C max
 *   word_3FB1E candidate max       word_3FB20 the 20-sample debounce
 *   word_3FB22 low threshold       word_3FB24 high threshold */
static int16_t word_3FB18 = 0x50, word_3FB1A, word_3FB1C, word_3FB1E;
static int16_t word_3FB20 = 0x14, word_3FB22, word_3FB24;
/* dseg 0x3FB26..0x3FB33 - the same seven for the y axis. */
static int16_t word_3FB26 = 0x50, word_3FB28, word_3FB2A, word_3FB2C;
static int16_t word_3FB2E = 0x14, word_3FB30, word_3FB32;
/* dseg 0x3FB34/0x3FB36 - 0x4000 / range, the scale sub_307E3 multiplies by. */
static int16_t word_3FB34, word_3FB36;

/* dseg 0x3FB38 - sixteen entries, indexed by the four direction bits, giving
 * the cell of the 3x3 grid the calibration screen lights up.  Decoding it
 * against the x/y tables in do_joy_restext gives the layout:
 *     0 centre  1 N  2 NE  3 E  4 SE  5 S  6 SW  7 W  8 NW
 * so up|right (5) -> 2 = NE, and the impossible ones (up|down, left|right,
 * all four) fall back to 0 = centre. */
static const uint8_t byte_3FB38[16] = {
	0x00, 0x01, 0x05, 0x00, 0x03, 0x02, 0x04, 0x03,
	0x07, 0x08, 0x06, 0x07, 0x00, 0x01, 0x05, 0x00
};

/* dseg 0x3E85C - the steering curve, 34 entries.  Indexed by the absolute
 * wheel deflection 0..33 that sub_307E3 produces; the first six are 0, so
 * the middle fifth of the stick's travel is a dead zone, and the top of the
 * ramp flattens at 127. */
static const uint8_t byte_3E85C[34] = {
	  0,   0,   0,   0,   0,   0,   4,   8,  12,  16,  20,  24,
	 28,  32,  36,  40,  44,  48,  52,  56,  60,  64,  68,  72,
	 76,  84,  90,  98, 106, 114, 121, 127, 127, 127
};

/* dseg 0x407FC / 0x3EB90.  externs.h:358 declares dialogarg2 and nothing
 * defined it (rhighscore.c:104 keeps a private copy of the same 4);
 * word_3EB90 is show_dialog's background colour, set to 0 on entry. */
uint16_t dialogarg2 = 4;
int16_t  word_3EB90;

/* dseg 0x3B8F2 and 0x3F88E.  externs.h:263 and :267 declare both and
 * nothing defined either - the ninth and tenth time this tree has hit that.
 * byte_3B8F2 is "the mouse is the driving input device" (do_mou_restext
 * sets it, do_key_restext and do_joy_restext clear it); it is NOT kbormouse
 * at dseg 0x3B8F9, which rreplaybar.c defines and which selects which
 * pointer the menus listen to.  word_3F88E is the "a modal screen is up"
 * flag every one of the seven restext routines raises and drops.
 * Both live here because rjoystick.c is their only reference; if a later
 * file needs them, move them somewhere both bin/stunts_native and
 * bin/dump_native_states link. */
char    byte_3B8F2;
int16_t word_3F88E;

/* ------------------------------------------------------------------ */
/* [DEVIATION] The SDL side of the boundary: opening a device, and      */
/* joy_read_axes.  From get_joy_flags onwards it is the original's      */
/* arithmetic, with no SDL in it.                                       */
/* ------------------------------------------------------------------ */

static SDL_GameController* s_pad;
static SDL_Joystick*       s_stick;
static int                 s_subsystem;   /* we opened it, we close it */
static int                 s_tried;

/* [DEVIATION - test hook] STUNTS_JOYFAKE="<x>,<y>,<buttons>" stands in for a
 * device so that every number below this line can be checked without one
 * plugged in: x and y are on the 0..1023 scale joy_read_axes produces, and
 * buttons is a mask of 0x10 (button 1) and 0x20 (button 2).  Re-read on each
 * poll, so a test can move the stick between calls.  In the same spirit as
 * main_native.c's STUNTS_GEAR / STUNTS_CRASH / STUNTS_DETAIL hooks. */
static int s_fake;
static int s_fake_step;      /* STUNTS_JOYFAKE=sweep's position in its script */

static void joy_recompute_x(void);   /* get_joy_flags' LAB_2ea2_1c60 */
static void joy_recompute_y(void);   /* get_joy_flags' LAB_2ea2_1cfd */

/* Commit a known range, so a controller works the moment it is plugged in.
 *
 * [DEVIATION] the original has no such seed: dseg 0x3FB18..0x3FB24 start at
 * (min 0x50, max 0, thresholds 0, 0), which reads as "hard right" until the
 * 20-sample outlier filter has widened the extents.  That is fine for a
 * gameport whose counts really are unknown; SDL's axes are already
 * normalised, so seeding min 0 / max 1023 and running the original's own
 * recompute gives the thresholds it would have converged on.  The
 * calibration screen still calls sub_307B4 and re-measures from scratch,
 * exactly as before. */
static void joy_seed_calibration(void)
{
	word_3FB18 = 0;    word_3FB1C = 1023;
	word_3FB26 = 0;    word_3FB2A = 1023;
	word_3FB20 = 0x14; word_3FB1E = 0x4E20; word_3FB1A = 0;
	word_3FB2E = 0x14; word_3FB2C = 0x4E20; word_3FB28 = 0;
	joy_recompute_x();
	joy_recompute_y();
}

/*
 * rreplaybar.c's get_kb_or_joy_flags falls through to get_joy_flags when no
 * key is down (restunts2 seg012 LAB_2ea2_1b9e).  It reaches it through this
 * pointer rather than by name, and this is the STRONG definition of a symbol
 * rreplaybar.c also defines weakly as NULL.
 *
 * That is what lets the two files be linked independently: rreplaybar.c is in
 * every build and this file is not (tools/build_dumper.sh and build_intro.sh
 * link neither joysticks nor SDL game controllers), so link this file and the
 * strong definition below wins and the joystick half is answered; leave it
 * out and rreplaybar.c's NULL stands and it answers the keyboard only.
 * Nothing has to be called first - get_joy_flags is safe before rjoy_init,
 * it returns 0 while byte_3FE00 is clear or no device is open.
 */
int16_t (*joy_flags_hook)(void) = get_joy_flags;

int rjoy_init(void)
{
	int i, n;

	if (s_pad || s_stick || s_fake) return 1;
	if (s_tried) return 0;
	s_tried = 1;

	if (getenv("STUNTS_JOYFAKE")) {        /* the test hook, see above */
		s_fake = 1;
		joy_seed_calibration();
		return 1;
	}

	/* main_native.c's SDL_Init does not ask for the joystick subsystem and
	 * is not ours to edit, so bring it up here.  SDL_INIT_GAMECONTROLLER
	 * implies SDL_INIT_JOYSTICK. */
	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
		if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
			if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0 &&
			    SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
				return 0;              /* no joystick support at all */
			s_subsystem = SDL_INIT_JOYSTICK;
		} else {
			s_subsystem = SDL_INIT_GAMECONTROLLER;
		}
	}

	n = SDL_NumJoysticks();
	for (i = 0; i < n; i++) {
		if (SDL_IsGameController(i)) {
			s_pad = SDL_GameControllerOpen(i);
			if (s_pad) break;
		} else {
			s_stick = SDL_JoystickOpen(i);
			if (s_stick) break;
		}
	}
	if (!s_pad && !s_stick) return 0;

	joy_seed_calibration();
	return 1;
}

int rjoy_present(void)
{
	return (s_pad || s_stick || s_fake) ? 1 : 0;
}

void rjoy_shutdown(void)
{
	s_fake = 0;
	if (s_pad)   { SDL_GameControllerClose(s_pad); s_pad = NULL; }
	if (s_stick) { SDL_JoystickClose(s_stick);     s_stick = NULL; }
	if (s_subsystem) { SDL_QuitSubSystem(s_subsystem); s_subsystem = 0; }
	s_tried = 0;
}

void rjoy_set_enabled(int on)
{
	byte_3FE00 = (char)(on ? 1 : 0);
}

/* SDL axis (-32768..32767) -> the 0..1023 scale the calibration works on. */
static int16_t joy_axis(int raw)
{
	return (int16_t)(((int32_t)raw + 32768) >> 6);
}

/* The extras a 1990 gameport did not have.  Kept in one place, and OR'd in
 * as flag bits so nothing below this line has to know about them:
 * the triggers are gas and brake, the shoulders are the gearbox, and the
 * d-pad doubles the stick.  [DEVIATION] - not in the original, which has
 * two axes and two buttons and nothing else. */
static int16_t joy_controller_extras(void)
{
	int16_t f = 0;
	if (!s_pad) return 0;
	if (SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8192)
		f |= 0x01;                                        /* accelerate */
	if (SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8192)
		f |= 0x02;                                        /* brake      */
	if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    f |= 0x01;
	if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  f |= 0x02;
	if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) f |= 0x04;
	if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  f |= 0x08;
	if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
		f |= 0x10;                                        /* shift up   */
	if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
		f |= 0x20;                                        /* shift down */
	return f;
}

/*
 * The replacement for the port-0x201 strobe and its two timing loops
 * (restunts2 seg012 3852..3878).  Fills joyflag1, joyflag2 and joybutton -
 * the same three variables, in the same units-of-nothing the original's
 * counts were, only stable.
 *
 * joybutton keeps the original's meaning: bit 4 button 1, bit 5 button 2,
 * ACTIVE LOW, because get_joy_flags' tail does `and al,0x30 ; xor al,0x30`.
 */
static void joy_read_axes(void)
{
	int b0 = 0, b1 = 0;

	if (s_fake) {
		const char* e = getenv("STUNTS_JOYFAKE");
		int fx = 512, fy = 512, fb = 0;
		if (e && !strncmp(e, "sweep", 5)) {
			/* A scripted stick that waves round all nine positions and
			 * then presses button 1, so the calibration screen's own
			 * loop can be driven to its "all nine reached" exit with no
			 * hardware.
			 *
			 * Each position is held for 25 polls and the lap is walked
			 * three times, and both of those numbers matter.  The
			 * outlier filter above commits the LEAST extreme of 20
			 * consecutive out-of-range readings, so a position has to
			 * be held for more than those 20 polls or the window spans
			 * two sides of the stick and the extent collapses towards
			 * the middle; and the first lap is spent learning the
			 * travel, so only the later ones light every square.  That
			 * is not an artefact of the test - it is exactly what the
			 * screen asks a person to do, and why it says "to ALL the
			 * squares" rather than "once round". */
			static const int seq[9][2] = {
				{ 512,  512 }, { 512,    0 }, {1023,    0 },
				{1023,  512 }, {1023, 1023 }, { 512, 1023 },
				{   0, 1023 }, {   0,  512 }, {   0,    0 }
			};
			/* "sweepfail" is the same wave done far too quickly - one
			 * lap, three polls a position - which is how a real person
			 * fails this screen and gets the "jox" refusal. */
			int fail = !strcmp(e, "sweepfail");
			int hold = fail ? 3 : 25, laps = fail ? 1 : 3;
			int k = s_fake_step / hold;
			if (k >= 9 * laps) { fx = fy = 512; fb = 0x10; }
			else { fx = seq[k % 9][0]; fy = seq[k % 9][1]; }
			s_fake_step++;
		} else if (e) {
			sscanf(e, "%d,%d,%d", &fx, &fy, &fb);
		}
		joyflag1 = (int16_t)fx;
		joyflag2 = (int16_t)fy;
		b0 = (fb & 0x10) != 0;
		b1 = (fb & 0x20) != 0;
	} else if (s_pad) {
		SDL_GameControllerUpdate();
		joyflag1 = joy_axis(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX));
		joyflag2 = joy_axis(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY));
		b0 = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_A);
		b1 = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_B);
	} else if (s_stick) {
		SDL_JoystickUpdate();
		joyflag1 = joy_axis(SDL_JoystickGetAxis(s_stick, 0));
		joyflag2 = SDL_JoystickNumAxes(s_stick) > 1
		         ? joy_axis(SDL_JoystickGetAxis(s_stick, 1))
		         : (int16_t)512;
		if (SDL_JoystickNumButtons(s_stick) > 0) b0 = SDL_JoystickGetButton(s_stick, 0);
		if (SDL_JoystickNumButtons(s_stick) > 1) b1 = SDL_JoystickGetButton(s_stick, 1);
	} else {
		joyflag1 = joyflag2 = 512;
	}
	joybutton = (char)(0x30 ^ ((b0 ? 0x10 : 0) | (b1 ? 0x20 : 0)));
}

/* ------------------------------------------------------------------ */
/* restunts2 seg012 3850..4041 - get_joy_flags.                        */
/* ------------------------------------------------------------------ */

/* LAB_2ea2_1c60 - recompute the x thresholds after an extent moved.
 *     bx = max - min
 *     if (bx > 0) scale = 0x4000 / bx
 *     half = bx >> 1 ; quarter = half >> 1
 *     hi = min + half + quarter ; lo = min + half - quarter
 * i.e. the live quarter of the travel either side of centre is "neutral". */
static void joy_recompute_x(void)
{
	int16_t bx = (int16_t)(word_3FB1C - word_3FB18);
	int16_t ax, q;
	if (bx > 0) word_3FB34 = (int16_t)(0x4000 / (uint16_t)bx);
	ax = (int16_t)(bx >> 1);
	q  = (int16_t)(ax >> 1);
	ax = (int16_t)(ax + word_3FB18);
	word_3FB24 = (int16_t)(ax + q);
	word_3FB22 = (int16_t)(ax - q);
}

/* LAB_2ea2_1cfd - the same for y. */
static void joy_recompute_y(void)
{
	int16_t bx = (int16_t)(word_3FB2A - word_3FB26);
	int16_t ax, q;
	if (bx > 0) word_3FB36 = (int16_t)(0x4000 / (uint16_t)bx);
	ax = (int16_t)(bx >> 1);
	q  = (int16_t)(ax >> 1);
	ax = (int16_t)(ax + word_3FB26);
	word_3FB32 = (int16_t)(ax + q);
	word_3FB30 = (int16_t)(ax - q);
}

/*
 * byte get_joy_flags(void)   restunts2 seg012 3851..4041
 *
 * The auto-calibration is the interesting half and it is not obvious from
 * the listing, so: while a reading sits inside [min, max] the three
 * candidate/counter variables are reset every call.  The moment readings go
 * outside, a 20-call countdown starts and the candidate tracks the LEAST
 * extreme of those readings - so a single spike cannot move an extent.  When
 * the countdown expires the extent commits to the candidate and the
 * thresholds are recomputed.  That is what lets the game learn a stick's
 * travel while you play, and what do_joy_restext accelerates by resetting
 * the extents first.
 */
int16_t get_joy_flags(void)
{
	int16_t ax;

	if ((byte_3FE00 & 1) == 0) return 0;              /* LAB_2ea2_1be6 */
	if (!rjoy_present()) return 0;    /* [DEVIATION] no gameport to read */

	joyinput = 0;
	joy_read_axes();

	/* ---- x ---- */
	ax = joyflag1;
	if (ax < word_3FB18) {                            /* LAB_2ea2_1c38 */
		if (--word_3FB20 > 0) {
			if (ax >= word_3FB1A) word_3FB1A = ax;
			goto loc_1cc5;
		}
		word_3FB18 = word_3FB1A;                      /* LAB_2ea2_1c54 */
		joy_recompute_x();
		ax = joyflag1;
	} else if (ax > word_3FB1C) {                     /* LAB_2ea2_1c91 */
		if (--word_3FB20 > 0) {
			if (ax < word_3FB1E) word_3FB1E = ax;
			goto loc_1cc5;
		}
		word_3FB1C = word_3FB1E;                      /* LAB_2ea2_1ca9 */
		joy_recompute_x();
		ax = joyflag1;
	}
	word_3FB20 = 0x14;                                /* LAB_2ea2_1cb3 */
	word_3FB1E = 0x4E20;
	word_3FB1A = 0;
loc_1cc5:
	if (ax < word_3FB22)       joyinput |= 0x08;      /* LAB_2ea2_1d2e  left  */
	else if (ax >= word_3FB24) joyinput |= 0x04;      /*                right */

	/* ---- y ----
	 * [ODDITY - faithful] the y axis is compared UNSIGNED (jnc/jc) where
	 * the x axis is signed (jge/jl), while the two candidate tests inside
	 * it stay signed (jl/jge).  Reproduced; with 0..1023 readings the two
	 * agree, so it changes nothing here, but it is the original's. */
	ax = joyflag2;
	if ((uint16_t)ax < (uint16_t)word_3FB26) {        /* LAB_2ea2_1cd6 */
		if (--word_3FB2E > 0) {
			if (ax >= word_3FB28) word_3FB28 = ax;
			goto loc_1d69;
		}
		word_3FB26 = word_3FB28;                      /* LAB_2ea2_1cf1 */
		joy_recompute_y();
		ax = joyflag2;
	} else if (ax > word_3FB2A) {                     /* LAB_2ea2_1d35 */
		/* [ODDITY - faithful] `dec word_3FB2E ; jz` here, against `jle`
		 * in the other three places.  If the counter ever reaches this
		 * branch already at or below zero it steps straight past the
		 * commit and the maximum never moves.  Left exactly so. */
		if (--word_3FB2E != 0) {
			if (ax < word_3FB2C) word_3FB2C = ax;
			goto loc_1d69;
		}
		word_3FB2A = word_3FB2C;                      /* LAB_2ea2_1d4d */
		joy_recompute_y();
		ax = joyflag2;
	}
	word_3FB2E = 0x14;                                /* LAB_2ea2_1d57 */
	word_3FB2C = 0x4E20;
	word_3FB28 = 0;
loc_1d69:
	if ((uint16_t)ax < (uint16_t)word_3FB30)      joyinput |= 0x01;  /* up   */
	else if ((uint16_t)ax >= (uint16_t)word_3FB32) joyinput |= 0x02; /* down */

	/* LAB_2ea2_1d7a: `in al,dx ; and al,[joybutton] ; and al,0x30 ;
	 * xor al,0x30 ; or [joyinput],al` - a second port read ANDed with the
	 * latch taken before the timing loops, so a button only counts if it
	 * was held across the whole read, and then the gameport's active-low
	 * is folded out.  joy_read_axes latches once, so the AND of the two
	 * reads is just the latch. */
	joyinput = (char)(joyinput | ((joybutton & 0x30) ^ 0x30));

	return (int16_t)(uint8_t)joyinput;
}

/* void sub_307B4(void) - restunts2 seg012 4043..4050.  Arm the joystick and
 * throw both extents away so the next few hundred reads re-learn them; the
 * inverted start (min 0x50, max 0) is what makes the first reading widen
 * both at once. */
void sub_307B4(void)
{
	s_fake_step = 0;      /* the scripted stick restarts with the screen */
	byte_3FE00 = 1;
	word_3FB18 = 0x50;
	word_3FB1C = 0;
	word_3FB26 = 0x50;
	word_3FB2A = 0;
}

/* byte sub_307D2(uint) - restunts2 seg012 4052..4066. */
int16_t sub_307D2(int16_t param_1)
{
	return (int16_t)byte_3FB38[param_1 & 0x0F];
}

/*
 * int sub_307E3(void) - restunts2 seg012 4068..4078, then seg005
 * loc_2279A..loc_227C5 which is the only caller.
 *
 *     ax = joyflag1 - min ; if (ax < 0) ax = 0
 *     dx:ax = ax * scale ; ax = (dx:ax >> 8) & 0xFFFF   (`al=ah ; ah=dl`)
 *     ax -= 0x1F
 * With scale = 0x4000/range that is (x-min)*64/range - 31, i.e. -31..+33
 * across the travel.  seg005 then keeps only AL, signed, and bends it
 * through byte_3E85C with the sign put back.
 *
 * [DEVIATION] the index is clamped to the table's 34 entries.  The original
 * does not clamp: at +33 it is already reading the last entry, and anything
 * past that walks into the "rpl" string that follows byte_3E85C in dseg.
 * Nothing can produce that here, and a clamp is cheaper than the surprise.
 */
static int16_t rjoy_wheel(void)
{
	int32_t a = (int32_t)joyflag1 - (int32_t)word_3FB18;
	uint32_t prod;
	int16_t r;
	int8_t v;

	if (a < 0) a = 0;
	prod = (uint32_t)(uint16_t)a * (uint32_t)(uint16_t)word_3FB34;
	r = (int16_t)((prod >> 8) & 0xFFFFu);
	r = (int16_t)(r - 0x1F);

	v = (int8_t)r;                       /* mov byte_40D6A, al */
	if (v > 0) {                                       /* loc_2279A */
		int i = v; if (i > 33) i = 33;
		return (int16_t)(int8_t)byte_3E85C[i];
	}
	if (v < 0) {                                       /* loc_227B0 */
		int i = -(int)v; if (i > 33) i = 33;
		return (int16_t)(-(int8_t)byte_3E85C[i]);
	}
	return 0;
}

/*
 * seg005 replay_unk (3301..3390) - the servo that turns the wheel target
 * back into one steering bit.  `di` is the target; var_A is this frame's
 * step, taken from steerWhlRespTable_ptr indexed by the car's speed, and
 * quadrupled while the wheel is on the far side of centre from the target.
 * Returns 4 (right), 8 (left) or 0, which the caller ORs into the byte -
 * exactly as loc_23B28 ORs it into td16_rpl_buffer.
 */
static char replay_unk_bits(int16_t di)
{
	const int8_t* table = (const int8_t*)steerWhlRespTable_ptr;
	int16_t angle = state.playerstate.car_steeringAngle;
	int8_t var_A, var_8;
	char var_6;

	if (!table) return 0;

	var_8 = (int8_t)((state.playerstate.car_speed2 >> 10) & 0xFC);
	var_A = table[(int16_t)var_8 + 1];

	if (angle < di) {                                  /* loc_23AB1 */
		if (angle >= -1) goto loc_23AF2;
		goto loc_23AED;
	}
	if (angle <= di) goto loc_23AF2;                   /* loc_23AE0 */
	if (angle <= 1)  goto loc_23AF2;
loc_23AED:
	var_A = (int8_t)(var_A << 2);
loc_23AF2:
	if (angle > di) {
		int16_t cx = (int16_t)(angle - (int16_t)var_A);
		if (cx >= di) { var_6 = 8; goto loc_23B28; }
	}
	if (angle < di) {                                  /* loc_23B0C */
		int16_t a = (int16_t)((int16_t)var_A + angle);
		if (a <= di) { var_6 = 4; goto loc_23B28; }
	}
	var_6 = 0;                                         /* loc_23B24 */
loc_23B28:
	return var_6;
}

/*
 * seg005 loc_2279A..loc_227EF, as much of it as a byte-wide input path can
 * carry.  The mask really is 0x33: with a joystick selected the digital
 * left/right bits are discarded and the steering comes from the servo.
 */
char rjoy_or_input(char input)
{
	int16_t f, extras;

	if (!rjoy_present()) return input;
	if ((byte_3FE00 & 1) == 0) return input;  /* not the chosen device */

	f = get_joy_flags();
	input = (char)(input | (f & 0x33));                /* loc_227C8 */
	input = (char)(input | replay_unk_bits(rjoy_wheel()));

	/* [DEVIATION] the extras, and the d-pad's own left/right, which the
	 * 0x33 mask would otherwise throw away along with the stick's. */
	extras = joy_controller_extras();
	input = (char)(input | (extras & 0x3F));
	return input;
}

/* ------------------------------------------------------------------ */
/* The calibration screen.                                             */
/* ------------------------------------------------------------------ */

/* The host, the same shape rintro.c uses. */
static SDL_Window*   s_win;
static SDL_Renderer* s_ren;
static SDL_Texture*  s_tex;
static const stunts_palette_t* s_pal;
static uint32_t s_rgba[RFB_VIEW_W * RFB_VIEW_H];

static void joycal_present(void)
{
	int i;
	if (!s_ren || !s_tex || !s_pal) return;
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

/* seg012 kb_read_char, as much of it as this port has: any key, or the
 * window being closed, counts.  A close is re-posted so the caller's own
 * loop still sees it. */
static int16_t joycal_kb_read_char(void)
{
	SDL_Event ev;
	int16_t r = 0;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) {
			SDL_Event q = ev;
			SDL_PushEvent(&q);
			return 27;
		}
		if (ev.type == SDL_KEYDOWN) r = 1;
		else if (ev.type == SDL_MOUSEBUTTONDOWN) r = 1;
	}
	return r;
}

/*
 * seg008 show_dialog (334..1206), the parts do_joy_restext needs.
 *
 * arg_0 selects what the routine does when the layout is finished
 * (loc_27B56): 0 return 0 at once, 1 show and wait for a key, 2 the full
 * button loop, 3 return the number of '@' markers WITHOUT waiting, 4 the
 * timed form.  do_joy_restext uses 3 for the calibration frame and 1 for
 * the "jox" refusal, so those two are what is here.
 *
 * The template language, straight out of the two passes:
 *     ']'  end of line - draw it, advance by fontdef_unk_0E + 2
 *     '}'  end of the title - draw it, advance by 4
 *     '['  first button; everything from here is the button pass
 *     '@'  when arg_0 == 3, record (left + width-so-far, top + line) into
 *          the caller's array and put a space in its place
 *
 * MAIN.RES "ejoy" is, with the ']' shown as line breaks:
 *
 *     Calibrate your joystick
 *     by using it to move the
 *     indicator below, to all
 *     the squares.
 *
 *          @   @   @   @
 *
 *     @
 *
 *     @
 *
 *     @Press joystick button
 *         when complete
 *
 * - seven '@', no '[' at all.  Four on one line give the three column x's
 * plus the right edge; the next three give the three row y's; the last line
 * gives the bottom edge.  Which is the whole 3x3 grid, in the data, with no
 * layout invented anywhere.  (House rule 2, and it held again.)
 *
 * [DEVIATION] four things.  sub_274B0/sub_275C6 save and restore the screen
 * under the dialog - this port has one framebuffer and repaints, so the
 * caller clears first.  sprite_clear_1_color works in framebuffer pixels
 * rather than the original's 320x200, so the interior is cleared with
 * sprite_1_unk, which scales.  sub_345BC (the string blitter that reads
 * fontdefptr2) becomes font_draw_text.  And the button pass from loc_279A0
 * on is absent, because neither template this file shows has a '['.
 */
struct dlg3 {
	int16_t left, top;      /* arg_8 / arg_A, after the +8 at loc_277A5 */
	int16_t w, h;           /* var_194 / var_1C6                        */
	int16_t at[16];         /* the arg_E array: x, y, x, y, ...         */
	int16_t nat;            /* var_9E / 2 - how many '@' were recorded  */
};

static int16_t show_dialog_layout(const char* tpl, int16_t arg_0,
                                  int16_t arg_C, struct dlg3* g)
{
	char var_80[96];
	int16_t var_82 = 0;                 /* index into var_80             */
	int16_t var_1C6 = 0;                /* accumulated height            */
	int16_t var_194 = 0x20;             /* widest line, minimum 32       */
	int16_t var_1D6 = (int16_t)(fontdef_unk_0E + 2);
	int16_t arg_8, arg_A, var_30, var_2E, var_2C, var_2A;
	int16_t var_9E = 0;
	const char* p;

	if (!tpl) return 0;
	memset(g, 0, sizeof *g);

	/* ---- pass 1, loc_276C0..loc_2776C: measure ---- */
	for (p = tpl; *p; p++) {
		if (*p == ']' || *p == '}') {
			int16_t var_1C2;
			var_80[var_82] = 0;
			var_1C2 = (int16_t)font_op2(var_80);
			if (var_1C2 > var_194) var_194 = var_1C2;
			var_82 = 0;
			var_1C6 = (int16_t)(var_1C6 + (*p == ']' ? var_1D6 : 4));
		} else if (var_82 < (int16_t)sizeof var_80 - 1) {
			var_80[var_82++] = *p;
		}
	}

	var_194 = (int16_t)((var_194 + 0x18) & 0xFFF8);     /* and al, 0F8h */
	arg_8 = (int16_t)(((0x140 - var_194) / 2) & 0xFFF8);
	arg_A = (int16_t)((0xC8 - var_1C6) / 2);

	var_30 = arg_8;                                     /* loc_277A5 */
	var_2E = (int16_t)(arg_8 + var_194);
	var_2C = (int16_t)(arg_A - 8);
	var_2A = (int16_t)(arg_A + var_1C6 + 8);
	arg_8  = (int16_t)(arg_8 + 8);
	var_194 = (int16_t)(var_194 - 0x10);

	/* loc_277F6: clear the box, then one pixel of border round it. */
	sprite_1_unk(var_30, var_2C, (int16_t)(var_2E - var_30),
	             (int16_t)(var_2A - var_2C), 0);
	sprite_1_unk4((int16_t)(arg_8 - 4), (int16_t)(arg_A - 4),
	              (int16_t)(arg_8 + var_194 + 4),
	              (int16_t)(arg_A + var_1C6 + 4), arg_C);
	word_3EB90 = 0;
	font_set_colour((uint16_t)dialog_fnt_colour, 0);

	/* ---- pass 2, loc_27890..loc_2798E: draw, and record the '@' ---- */
	var_82 = 0;
	var_1C6 = 1;
	for (p = tpl; *p; p++) {
		if (*p == '[') break;                           /* loc_27890 */
		if (*p == ']' || *p == '}') {                   /* loc_2789A/278E2 */
			var_80[var_82] = 0;
			font_draw_text(var_80, arg_8, (int16_t)(arg_A + var_1C6));
			var_82 = 0;
			var_1C6 = (int16_t)(var_1C6 + (*p == ']' ? var_1D6 : 4));
			continue;
		}
		if (*p == '@') {                                /* loc_27918 */
			if (arg_0 == 3 && var_9E + 1 < (int16_t)(sizeof g->at / sizeof g->at[0])) {
				var_80[var_82] = 0;
				g->at[var_9E]     = (int16_t)(font_op2(var_80) + arg_8);
				g->at[var_9E + 1] = (int16_t)(arg_A + var_1C6);
				var_9E = (int16_t)(var_9E + 2);
			}
			if (var_82 < (int16_t)sizeof var_80 - 1)    /* loc_2796A */
				var_80[var_82++] = ' ';
			continue;
		}
		if (var_82 < (int16_t)sizeof var_80 - 1)        /* loc_27978 */
			var_80[var_82++] = *p;
	}

	g->left = arg_8; g->top = arg_A;
	g->w = var_194;  g->h = var_1C6;
	g->nat = (int16_t)(var_9E / 2);
	return g->nat;                       /* loc_27BC4: al = var_9E / 2 */
}

/* The nine-cell grid, redrawn from scratch each presented frame.
 *
 * [DEVIATION] the original draws this ONCE (loc_29B84..loc_29C96) and then
 * only repaints two rectangles whenever the cell changes, because the DOS
 * screen it drew on stayed put.  This port has no saved screen under the
 * dialog, so the whole picture is laid down every frame.  The rectangles,
 * their sizes and their colours are the original's, instruction for
 * instruction; only the frequency differs. */
struct joycal {
	struct dlg3 g;
	int16_t var_40[9];      /* [bp+var_C*2-0x40] - the nine cell x's */
	int16_t var_54[9];      /* [bp+var_C*2-0x54] - the nine cell y's */
	int16_t var_12;         /* cell width  */
	int16_t var_42;         /* cell height */
	const char* text;
};

static void joycal_geometry(struct joycal* j)
{
	/* The seven '@' in reading order: (x,y) pairs 0..6.  Only these ten
	 * of the fourteen words are used, which is the original's own choice
	 * of locals - var_20, var_1E, var_1A and var_16 are written by
	 * show_dialog and never read. */
	int16_t var_2E = j->g.at[0],  var_2C = j->g.at[1];   /* col 1, top row */
	int16_t var_2A = j->g.at[2],  var_28 = j->g.at[3];   /* col 2          */
	int16_t var_26 = j->g.at[4],  var_24 = j->g.at[5];   /* col 3          */
	int16_t var_22 = j->g.at[6];                         /* right edge     */
	int16_t var_1C = j->g.at[9];                         /* middle row     */
	int16_t var_18 = j->g.at[11];                        /* bottom row     */
	int16_t var_14 = j->g.at[13];                        /* bottom edge    */

	/* loc_29B84..: the two verticals and the two horizontals. */
	sprite_1_unk((int16_t)(var_2A - 4), var_28, 1,
	             (int16_t)(var_14 - var_28 - 8), (int16_t)dialogarg2);
	sprite_1_unk((int16_t)(var_26 - 4), var_24, 1,
	             (int16_t)(var_14 - var_28 - 8), (int16_t)dialogarg2);
	sprite_1_unk(var_2E, (int16_t)(var_1C - 4),
	             (int16_t)(var_22 - var_2E), 1, (int16_t)dialogarg2);
	sprite_1_unk(var_2E, (int16_t)(var_18 - 4),
	             (int16_t)(var_22 - var_2E), 1, (int16_t)dialogarg2);

	/* The nine cells, in the order byte_3FB38 indexes them:
	 * 0 centre, 1 N, 2 NE, 3 E, 4 SE, 5 S, 6 SW, 7 W, 8 NW. */
	j->var_40[0] = var_2A; j->var_40[1] = var_2A; j->var_40[5] = var_2A;
	j->var_40[2] = var_26; j->var_40[3] = var_26; j->var_40[4] = var_26;
	j->var_40[6] = var_2E; j->var_40[7] = var_2E; j->var_40[8] = var_2E;
	j->var_54[0] = var_1C; j->var_54[3] = var_1C; j->var_54[7] = var_1C;
	j->var_54[1] = var_28; j->var_54[2] = var_28; j->var_54[8] = var_28;
	j->var_54[4] = var_18; j->var_54[5] = var_18; j->var_54[6] = var_18;

	j->var_12 = (int16_t)(var_2A - var_2E - 8);
	j->var_42 = (int16_t)(var_1C - var_2C - 8);
}

/* One complete picture, from the cell `si` is standing on. */
static void joycal_frame(struct joycal* j, int16_t si)
{
	int16_t var_C;

	/* [DEVIATION] the original leaves the option menu's screen underneath;
	 * word_407FA = 9 is the colour run_option_menu clears to. */
	sprite_1_unk(0, 0, 320, 200, 9);
	show_dialog_layout(j->text, 3, (int16_t)dialogarg2, &j->g);
	joycal_geometry(j);

	for (var_C = 0; var_C < 9; var_C++)                 /* loc_29CC9 */
		sprite_1_unk(j->var_40[var_C], j->var_54[var_C],
		             j->var_12, j->var_42, word_3EB90);
	if (si >= 0 && si < 9)                              /* loc_29CFA */
		sprite_1_unk(j->var_40[si], j->var_54[si],
		             j->var_12, j->var_42, dialog_fnt_colour);
	joycal_present();
}

/* loc_29D3A's refusal: show_dialog(1, 1, "jox", ...) - "Joystick Disabled.
 * Indicator wasn't moved to all squares."  arg_0 == 1 is loc_27B98, which
 * shows it and waits for a key.  Drawn here; the wait is the caller's. */
static void joycal_jox(void)
{
	const char* jox = (const char*)(mainresptr
	        ? locate_text_res((char far*)mainresptr, (char*)"jox") : NULL);
	struct dlg3 g2;
	if (!jox) return;
	sprite_1_unk(0, 0, 320, 200, 9);
	show_dialog_layout(jox, 1, (int16_t)dialogarg2, &g2);
	joycal_present();
}

/*
 * seg008 do_joy_restext (4708..4993).
 *
 * The shape of it: arm the joystick, throw the calibration away, and then
 * sit in a loop that lights whichever of nine squares the stick is pointing
 * at, ticking each one off as it is reached.  A key or a button ends it.  If
 * all nine were reached byte_3FE00 survives the AND at loc_29D25 and the
 * joystick stays on; if any was missed it is cleared and the "jox" refusal
 * goes up.  The measuring is a side effect: every get_joy_flags call in the
 * loop widens the extents, so swinging the stick to all nine corners IS the
 * calibration.
 */
int16_t rjoy_calibrate(SDL_Window* win, SDL_Renderer* ren, SDL_Texture* tex,
                       const stunts_palette_t* pal, void far* dlgfont)
{
	struct joycal j;
	char var_A[9];                                 /* nine "was reached" */
	int16_t var_C;
	int16_t si, di;
	uint32_t* saved_rgba = rs_rgba;
	const char* shot = getenv("STUNTS_JOYCAL_SHOT");

	s_win = win; s_ren = ren; s_tex = tex; s_pal = pal;
	memset(&j, 0, sizeof j);
	(void)s_win;

	/* rs_rgba selects rfont/rwidgets' truecolour target; this screen draws
	 * indexed, like rintro.c, and hands the buffer back on the way out. */
	rs_rgba = NULL;
	if (dlgfont) font_set_fontdef2(dlgfont);

	/* input_push_status (seg008 4676..4692) then word_3F88E = 1 and
	 * audio_unk.  [DEVIATION] the first saves the DOS mouse cursor state
	 * and the last is the audio driver's pause; neither exists here. */
	word_3F88E = 1;

	rjoy_init();
	j.text = (const char*)(mainresptr
	         ? locate_text_res((char far*)mainresptr, (char*)"joy") : NULL);

	if (show_dialog_layout(j.text, 3, (int16_t)dialogarg2, &j.g) <= 0
	    || j.g.nat < 7) {
		byte_3FE00 = 0;                            /* loc_29D76 */
		goto loc_29D7B;
	}

	for (var_C = 0; var_C < 9; var_C++) var_A[var_C] = 0;   /* loc_29B89 */
	byte_3FE00 = 1;
	joycal_geometry(&j);

	si = -1;                                       /* mov si, 0FFFFh */
	sub_307B4();

	/* The test hook comes before the device check on purpose: the picture
	 * is what it is asked for, and it does not depend on a stick.
	 * STUNTS_JOYCAL_CELL forces which of the nine is lit, so all nine
	 * positions can be screenshotted and checked without a joystick. */
	if (shot) {
		const char* cell = getenv("STUNTS_JOYCAL_CELL");
		if (getenv("STUNTS_JOYCAL_JOX")) {   /* the refusal, loc_29D3A */
			byte_3FE00 = 0;
			joycal_jox();
		} else {
			joycal_frame(&j, cell ? (int16_t)atoi(cell) : 0);
		}
		rjoy_write_bmp(shot);
		byte_3FE00 = 0;
		word_3F88E = 0;
		rs_rgba = saved_rgba;
		return 0;
	}
	if (!rjoy_present()) {
		/* [DEVIATION] no device: the loop below could never light a
		 * square, so take the same exit a missed square takes rather
		 * than hanging on a stick that is not there. */
		byte_3FE00 = 0;
		goto loc_29D3A;
	}

loc_29C96:
	joycal_frame(&j, si);
	if (joycal_kb_read_char() != 0) goto loc_29C9F;
	di = get_joy_flags();                          /* loc_29CA8 */
	if ((di & 0x30) != 0) goto loc_29C9F;
	di = sub_307D2(di);
	if (di == si) goto loc_29C96;
	si = di;                                       /* loc_29D0E tail */
	var_A[di] = 1;
	goto loc_29C96;

loc_29C9F:
	/* loc_29D25: byte_3FE00 survives only if every square was reached. */
	for (var_C = 0; var_C < 9; var_C++)
		byte_3FE00 = (char)(byte_3FE00 & var_A[var_C]);

loc_29D3A:
	/* sub_275C6 - restore the screen the dialog covered.  [DEVIATION] no
	 * saved screen here; the caller repaints. */
	if (byte_3FE00 == 0) {
		joycal_jox();
		/* arg_0 == 1: loc_27B98 spins on input_checking until a key
		 * arrives, then check_input debounces it.  Modal, as it is in
		 * the original.  Under the STUNTS_JOYFAKE test hook there is
		 * nobody to press that key, so the wait is skipped there and
		 * only there. */
		if (!getenv("STUNTS_JOYFAKE"))
			while (joycal_kb_read_char() == 0) SDL_Delay(16);
	}

loc_29D7B:
	byte_3B8F2 = 0;                                /* not the mouse     */
	word_3F88E = 0;
	rs_rgba = saved_rgba;
	return (int16_t)(byte_3FE00 & 1);
}

/* One 24-bit BMP of the framebuffer, for STUNTS_JOYCAL_SHOT and
 * tools/verify.sh.  Same layout rintro_write_bmp uses. */
void rjoy_write_bmp(const char* path)
{
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

	if (!path || !s_pal) return;
	buf = (uint8_t*)calloc(1, img ? img : 1);
	if (!buf) return;
	memset(hdr, 0, sizeof hdr);
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4);
	hdr[10] = 54;
	hdr[14] = 40;
	memcpy(hdr + 18, &w, 4);
	memcpy(hdr + 22, &h, 4);
	hdr[26] = 1; hdr[28] = 24;
	memcpy(hdr + 34, &img, 4);

	for (y = 0; y < RFB_VIEW_H; y++) {
		uint8_t* row = buf + (uint32_t)(RFB_VIEW_H - 1 - y) * stride;
		for (x = 0; x < RFB_VIEW_W; x++) {
			stunts_color_rgba_t c =
				s_pal->colors[rfb_pixels[(uint32_t)y * RFB_VIEW_W + x]];
			row[x * 3 + 0] = c.b;
			row[x * 3 + 1] = c.g;
			row[x * 3 + 2] = c.r;
		}
	}
	f = fopen(path, "wb");
	if (f) { fwrite(hdr, 1, 54, f); fwrite(buf, 1, img, f); fclose(f); }
	free(buf);
}
