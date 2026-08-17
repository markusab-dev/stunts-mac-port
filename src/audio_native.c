/*
 * audio_native.c - Phase 4. Sound.
 *
 * ====================================================================
 * PART 1 - the decision layer, ported instruction by instruction
 * ====================================================================
 *
 * Line numbers are reference/restunts2/src/asm/ unless marked (r1), which
 * means reference/restunts/src/restunts/asm/. restunts1 has no body for
 * audio_carstate at all - it is `extrn` there - so the main routine comes
 * from restunts2's Ghidra export; the small seg007 helpers only exist in
 * restunts1.
 *
 *   audio_carstate        seg001.asm 7192..7662   (471 lines)
 *   audio_unk3            seg001.asm 7664..7687
 *   sub_18D06             seg001.asm 7689..7725
 *   audio_op_unk          seg007.asm 455..512   (r1)   engine loop on
 *   audio_function2       seg007.asm 513..540   (r1)   engine loop off
 *   audio_function2_wrap  seg007.asm            (r1)   crash + engine off
 *   audio_op_unk2         seg007.asm            (r1)   volume/doppler/pitch
 *   audio_op_unk3/4       seg007.asm            (r1)   two one-shots
 *   audio_op_unk5/6/7     seg007.asm 1046..1156 (r1)   skid A / skid B / off
 *   frame_callback        seg005.asm 1099..1136 (r1)   the ring consumer
 *
 * How the original's sound actually works, which took reading all of the
 * above to see:
 *
 *   The simulation raises bits in CARSTATE.field_CF every frame (state.c
 *   sets bit 0 = engine running; sfasm_port.c:530/533 set bit 1 or bit 2 =
 *   sliding, on surface type 1 or not; stateply.c:902 sets 0x10 = scraping
 *   along a wall; stateply.c:1838 sets 0x20 = a wheel bottomed out).
 *
 *   audio_carstate turns the *level* bits 0/1/2 into start/stop *edges*
 *   through a latch byte per car (byte_42D26 / byte_42D2A), and pushes one
 *   0x22-byte record per frame into a 40-entry ring at unk_44F4C. The record
 *   holds the listener-relative position of each car this frame and last
 *   frame, plus each car's rpm.
 *
 *   A 100 Hz timer callback drains one record every word_4499C = 100/fps = 5
 *   ticks and hands it to audio_op_unk2, which is where the real decisions
 *   are: distance cutoff, linear volume falloff, radial velocity, Doppler.
 *
 *   audio_unk3 handles the one-shot bits 0x10 and 0x20 directly, and
 *   update_crash_state calls audio_function2_wrap on impact.
 *
 * [DEVIATION] There is no oracle for any of this. The DOS dumps compare
 * GAMESTATE, and none of the audio globals live in GAMESTATE, so nothing
 * below can be checked against the original the way the physics can. The
 * decision half is transcribed instruction by instruction and is asserted
 * on that basis; the playback half is new code and is only behaviour-exact
 * in the sense that it is driven by the ported numbers.
 *
 * ====================================================================
 * PART 2 - the playback layer, new
 * ====================================================================
 *
 * A small SDL2 synth: a two-saw engine tone through a one-pole lowpass,
 * band-passed noise for the two skid variants, and three decaying noise
 * bursts for crash / bump / scrape. Mono, because every driver the original
 * shipped (PC speaker, AdLib/OPL2, Tandy, MT-32) is mono.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL.h>

#include "render_faithful/externs.h"
#include "render_faithful/math.h"
#include "audio_native.h"
#include "music_native.h"

/* ------------------------------------------------------------------ */
/* dseg globals this routine owns                                     */
/* ------------------------------------------------------------------ */

/* Defined in sim_faithful/sfdata.c; declared there as arrays, used as
 * scalars by statecrs.c the same way. word_43964 and word_4408C are the
 * audiotimers[] slot each car speaks through - the original's audio init
 * hands them out; here they are simply 0 and 1. */
extern int16_t word_43964;      /* player channel   */
extern int16_t word_4408C;      /* opponent channel */
extern char    byte_459D8;      /* "a record has been queued" */
extern char    is_in_replay;

extern int16_t word_44D20;      /* rdata.c; camera-mode-3 height bias */

static char byte_42D26;         /* dseg 0x42D26: player latch  */
static char byte_42D2A;         /* dseg 0x42D2A: opponent latch */
static char byte_3BE02_a;       /* dseg 0x3BE02: last is_in_replay seen */

/* unk_44F4C: 40 records of 0x22 bytes. Read and written as words. */
#define AUD_RING 0x28
#define AUD_REC  0x11           /* 0x22 bytes = 17 words */
static int16_t unk_44F4C[AUD_RING][AUD_REC];
static int16_t word_449E4;      /* write cursor */
static int16_t word_44D1E;      /* read cursor  */
static int16_t word_443F4;      /* ticks since the last drain */
/* word_4499C moved to sim_faithful/sfasm_port.c: init_game_state needs it
 * and the oracle dumper does not link this file. */

/* Word indices into a record. seg001.asm 4372..4430 writes exactly these
 * and nothing else - bytes 0..5 of every record are never written and never
 * read. [ODDITY] six dead bytes per record, faithful, harmless. */
#define R_P2X 3   /* +0x06 */
#define R_P2Y 4
#define R_P2Z 5
#define R_P1X 6   /* +0x0C */
#define R_P1Y 7
#define R_P1Z 8
#define R_O2X 9   /* +0x12 */
#define R_O2Y 10
#define R_O2Z 11
#define R_O1X 12  /* +0x18 */
#define R_O1Y 13
#define R_O1Z 14
#define R_PRPM 15 /* +0x1E */
#define R_ORPM 16 /* +0x20 */

/* ------------------------------------------------------------------ */
/* audiotimers[] - only the fields the ported code touches             */
/* ------------------------------------------------------------------ */
typedef struct {
	int16_t  freq;          /* +0x0C, set by audio_op_unk / audio_op_unk2 */
	int16_t  vol;           /* +0x0A, 0..127                              */
	uint8_t  engine;        /* +0x01, the engine loop is sounding         */
	uint8_t  skid;          /* which of the two skid loops is sounding    */
	uint32_t hit_crash;     /* one-shot retrigger counters, read by the   */
	uint32_t hit_bump;      /* synth thread                               */
	uint32_t hit_scrape;
} AUDTIMER;
static AUDTIMER audiotimers[2];

/* The engine's rpm -> frequency scale. audio_op_unk (seg007.asm 481..489)
 * and audio_op_unk2 both compute
 *      freq = rpm / vce[0x0E] + (vce[0x0F] << 4)
 * where vce is the ENGI entry of the driver's <XX>ENG1.VCE. Measured from
 * the shipped files: PC 6/0, AdLib 11/0, MT-32 60/0. The PC divisor is the
 * one whose result is plausibly in Hz (idle ~1000 rpm -> 167 Hz, redline
 * ~7000 -> 1167 Hz), so PCENG1.VCE is the file read here and the result is
 * treated as Hz. [DEVIATION] that last step - "the PC driver's frequency
 * word is Hz" - is a calibration choice, not something the disassembly
 * states; the divisor itself is read out of the shipped file. */
static int16_t vce_engi_div = 6;
static int16_t vce_engi_off = 0;

/* ------------------------------------------------------------------ */
/* runtime switches                                                    */
/* ------------------------------------------------------------------ */
static int  snd_mode;           /* 0 off, 1 device, 2 wav */
static int  snd_enabled = 1;    /* the option menu's toggle */
static int  snd_trace;
static SDL_AudioDeviceID snd_dev;
static FILE* snd_wav;
static uint32_t snd_wav_bytes;

#define SND_RATE 44100

static void audio_hook_unk3_impl(int16_t flags, int16_t chan);
static void audio_hook_fn2wrap_impl(int16_t chan);
extern void (*audio_hook_unk3)(int16_t, int16_t);
extern void (*audio_hook_fn2wrap)(int16_t);

/* ==================================================================== */
/* seg007 helpers                                                       */
/* ==================================================================== */

/* audio_op_unk (r1 seg007.asm 455..512): start the engine loop. Only if the
 * slot is allocated ([si]==1) and not already running ([si+1]==0). The
 * frequency it lays down is the same rpm formula audio_op_unk2 uses. */
static void audio_op_unk(int16_t chan)
{
	AUDTIMER* t = &audiotimers[chan];
	if (t->engine) return;
	t->engine = 1;
	/* [si+0x1C] is the slot's rpm scratch; the port feeds the live value
	 * from the ring instead, so the initial frequency is the idle one. */
	if (vce_engi_div)
		t->freq = (int16_t)((uint16_t)0 / (uint16_t)vce_engi_div
		                    + (uint16_t)(vce_engi_off << 4));
}

/* audio_function2 (r1 seg007.asm 513..540): stop the engine loop. */
static void audio_function2(int16_t chan)
{
	AUDTIMER* t = &audiotimers[chan];
	if (!t->engine) return;
	t->engine = 0;
	t->freq = 0;
}

/* audio_op_unk5 / audio_op_unk6 (r1 seg007.asm 1046..1135): start one of
 * two skid loops. They are byte-identical apart from which resource pointer
 * they take (sub+0x20 vs sub+0x24), which is why they are one function
 * here. audio_op_unk7 (1136..1156) stops whichever is running. */
static void audio_op_unk5(int16_t chan) { audiotimers[chan].skid = 2; }
static void audio_op_unk6(int16_t chan) { audiotimers[chan].skid = 4; }
static void audio_op_unk7(int16_t chan) { audiotimers[chan].skid = 0; }

/* audio_op_unk3 (r1 seg007.asm, resource at +0x44) - field_CF bit 0x20,
 * which stateply.c:1838 raises when a wheel's car_rc1 goes over 0xFA: a
 * suspension bottom-out. audio_op_unk4 (+0x48) - bit 0x10, raised at
 * stateply.c:902 in the wall-contact path, so it re-fires every frame the
 * car is scraping along something. */
static void audio_op_unk3(int16_t chan) { audiotimers[chan].hit_bump++; }
static void audio_op_unk4(int16_t chan) { audiotimers[chan].hit_scrape++; }

/* audio_function2_wrap (r1 seg007.asm): the impact sound, then the engine
 * off. Called from update_crash_state (statecrs.c:167/181/196/207). */
static void audio_hook_fn2wrap_impl(int16_t chan)
{
	if (!snd_mode || !snd_enabled) return;
	if (chan < 0 || chan > 1) return;
	audiotimers[chan].hit_crash++;
	audio_function2(chan);
}

/* audio_unk3 (seg001.asm 7664..7687). */
static void audio_hook_unk3_impl(int16_t flags, int16_t chan)
{
	if (!snd_mode || !snd_enabled) return;
	if (chan < 0 || chan > 1) return;
	if (byte_459D8 == 0) return;               /* LAB_1471_45f4 */
	if (flags & 0x10) audio_op_unk4(chan);
	if (flags & 0x20) audio_op_unk3(chan);
}

/* ==================================================================== */
/* audio_op_unk2 (r1 seg007.asm) - the actual decisions                 */
/* ==================================================================== */
/*
 * Arguments in the order sub_18D06 pushes them: the channel, the rpm, the
 * previous-frame listener-relative vector (record +0x06..+0x0A) and the
 * current-frame one (+0x0C..+0x10), then the tick count since the last
 * drain.
 *
 * The 3D distance is built with two calls to polarRadius2D sharing a stack
 * slot: hypot(hypot(dx, dz), dy).
 */
static void audio_op_unk2(int16_t chan, int16_t rpm,
                          int16_t p2x, int16_t p2y, int16_t p2z,
                          int16_t p1x, int16_t p1y, int16_t p1z,
                          int16_t elapsed)
{
	AUDTIMER* t = &audiotimers[chan];
	int16_t d_now, d_prev, vrad, vol, base, denom;

	/* push arg_C(p1y); push arg_E(p1z); push arg_A(p1x); call polarRadius2D
	 * -> polarRadius2D(p1x, p1z); add sp,4 leaves p1y on the stack;
	 * push ax; call polarRadius2D -> polarRadius2D(hypot, p1y). */
	d_now = polarRadius2D(polarRadius2D(p1x, p1z), p1y);

	if (d_now > 0x1770) {                  /* 6000 units - out of earshot */
		t->vol = 0;
		return;
	}
	d_prev = polarRadius2D(polarRadius2D(p2x, p2z), p2y);

	/* mov ax,100; xor dx,dx; div elapsed; imul (d_prev - d_now)
	 * NOTE the divide happens FIRST, on the constant, so the scale is the
	 * truncated 100/elapsed. elapsed is word_4499C = 5 at 20 fps, so this
	 * is exactly 20 * delta = units per second. [ODDITY] at 10 fps
	 * word_4499C is 10 and the scale is 10, still exact; at any other rate
	 * it would truncate. */
	if (elapsed == 0) elapsed = 1;
	vrad = (int16_t)((int16_t)(100 / elapsed) * (int16_t)(d_prev - d_now));

	/* vol = 127 - 127*d/6000 : linear falloff to nothing at the cutoff. */
	vol = (int16_t)(0x7F - (int16_t)(((uint16_t)0x7F * (uint16_t)d_now) / 0x1770));

	/* and 1/16 quieter while closing. Not a mistake in the transcription -
	 * `cmp var_16,0 / jle / shr 4 / sub` is what the instructions say.
	 * [ODDITY] unexplained; it makes an approaching car slightly quieter
	 * than a receding one at the same distance. */
	if (vrad > 0) vol -= (int16_t)((uint16_t)vol >> 4);

	/* base pitch straight from the rev counter */
	base = (int16_t)((uint16_t)rpm / (uint16_t)(vce_engi_div ? vce_engi_div : 1));
	base = (int16_t)(base + (uint16_t)(vce_engi_off << 4));

	/* Doppler: f' = f * 6000 / (6000 - vrad). 6000 is therefore the speed
	 * of sound in the same units, and it is the same 6000 as the audible
	 * cutoff. Approaching (vrad > 0) shrinks the denominator and raises
	 * the pitch, which is the right sign. */
	denom = (int16_t)(0x1770 - vrad);
	if (denom != 0)
		t->freq = (int16_t)(((uint32_t)0x1770 * (uint16_t)base) / (uint16_t)denom);

	t->vol = (int16_t)(uint8_t)vol;        /* mov al, byte ptr var_8 */
}

/* sub_18D06 (seg001.asm 7689..7725) */
static void sub_18D06(const int16_t* rec, int16_t elapsed)
{
	audio_op_unk2(word_43964, rec[R_PRPM],
	              rec[R_P2X], rec[R_P2Y], rec[R_P2Z],
	              rec[R_P1X], rec[R_P1Y], rec[R_P1Z], elapsed);
	if (gameconfig.game_opponenttype != 0)
		audio_op_unk2(word_4408C, rec[R_ORPM],
		              rec[R_O2X], rec[R_O2Y], rec[R_O2Z],
		              rec[R_O1X], rec[R_O1Y], rec[R_O1Z], elapsed);
}

/* ==================================================================== */
/* audio_carstate (seg001.asm 7192..7662)                               */
/* ==================================================================== */

static void carstate_shr6(const struct VECTORLONG* v,
                          int16_t* x, int16_t* y, int16_t* z)
{
	/* six `sar dx,1 / rcr ax,1` pairs, i.e. an arithmetic >>6 of the
	 * 32-bit world position, then the low word is kept. */
	*x = (int16_t)(v->lx >> 6);
	*y = (int16_t)(v->ly >> 6);
	*z = (int16_t)(v->lz >> 6);
}

void audio_carstate(void)
{
	int16_t p2[3], p1[3], o2[3], o1[3];   /* var_1C/var_8/var_24/var_E   */
	int16_t l1[3], l2[3];                 /* var_14 (near), var_2A (far) */
	int16_t* rec;
	int16_t ncars, i;

	if (is_in_replay != 0) {
		/* LAB_1471_41a6 - the teardown path. Reached in the original only
		 * from frame_callback (r1 seg005.asm 1129), which sets
		 * is_in_replay just before calling. It flushes the queue and
		 * silences whatever the latches say is still running. */
		if (byte_459D8 != 0) {
			word_44D1E = word_449E4;
			if (byte_42D26 & 6) audio_op_unk7(word_43964);
			if (byte_42D26 & 1) audio_function2(word_43964);
			if (gameconfig.game_opponenttype != 0) {
				if (byte_42D2A & 6) audio_op_unk7(word_4408C);
				if (byte_42D2A & 1) audio_function2(word_4408C);
			}
			byte_459D8 = 0;
			byte_42D26 = 0;
			byte_42D2A = 0;
		}
		if (byte_3BE02_a != is_in_replay) {
			/* call sub_38178 - the driver's own reset. Nothing to do. */
		}
		byte_3BE02_a = is_in_replay;
		return;
	}

	/* LAB_1471_422a */
	carstate_shr6(&state.playerstate.car_posWorld2, &p2[0], &p2[1], &p2[2]);
	carstate_shr6(&state.playerstate.car_posWorld1, &p1[0], &p1[1], &p1[2]);
	if (gameconfig.game_opponenttype != 0) {
		carstate_shr6(&state.opponentstate.car_posWorld2, &o2[0], &o2[1], &o2[2]);
		carstate_shr6(&state.opponentstate.car_posWorld1, &o1[0], &o1[1], &o1[2]);
	} else {
		o2[0] = o2[1] = o2[2] = 0;
		o1[0] = o1[1] = o1[2] = 0;
	}

	/* LAB_1471_4321 - the listener, chosen by camera mode. The same four
	 * cases frame.c:178..216 uses for the eye point, except that mode 0 and
	 * mode 2 both put the listener in the car rather than at the camera.
	 * [ODDITY] a cameramode outside 0..3 falls through to LAB_1471_4372
	 * with l1/l2 never assigned - uninitialised stack in the original.
	 * main_native.c masks the key with & 3, so it cannot happen; zeroed
	 * here to match the build's -ftrivial-auto-var-init=zero. */
	l1[0] = l1[1] = l1[2] = 0;
	l2[0] = l2[1] = l2[2] = 0;

	if (cameramode == 0 || cameramode == 2) {
		if (followOpponentFlag != 0) {
			memcpy(l1, o1, sizeof l1);
			memcpy(l2, o2, sizeof l2);
		} else {
			memcpy(l1, p1, sizeof l1);
			memcpy(l2, p2, sizeof l2);
		}
	} else if (cameramode == 1) {
		/* si = followOpponentFlag*6; [si + game_vec1] and [si + game_vec3].
		 * game_vec1 is a two-element array, but game_vec3 is not - the
		 * opponent's second vector is game_vec4, which sits immediately
		 * after it. Faithful: the original indexes by raw offset. */
		const struct VECTOR* v1 = &state.game_vec1[(int)followOpponentFlag];
		const struct VECTOR* v3 = &(&state.game_vec3)[(int)followOpponentFlag];
		l1[0] = v1->x; l1[1] = v1->y; l1[2] = v1->z;
		l2[0] = v3->x; l2[1] = v3->y; l2[2] = v3->z;
	} else if (cameramode == 3) {
		int idx = state.field_3F7[(int)followOpponentFlag] * 3;
		l1[0] = trackdata9[idx + 0];
		l1[1] = (int16_t)(trackdata9[idx + 1] + word_44D20 + 0x5A);
		l1[2] = trackdata9[idx + 2];
		memcpy(l2, l1, sizeof l2);       /* LAB_1471_435a copies l1 to l2 */
	}

	/* LAB_1471_4372 - fill one ring record with listener-relative vectors */
	rec = unk_44F4C[word_449E4];
	rec[R_P2X] = (int16_t)(l2[0] - p2[0]);
	rec[R_P2Y] = (int16_t)(l2[1] - p2[1]);
	rec[R_P2Z] = (int16_t)(l2[2] - p2[2]);
	rec[R_P1X] = (int16_t)(l1[0] - p1[0]);
	rec[R_P1Y] = (int16_t)(l1[1] - p1[1]);
	rec[R_P1Z] = (int16_t)(l1[2] - p1[2]);
	rec[R_PRPM] = state.playerstate.car_currpm;

	if (gameconfig.game_opponenttype != 0) {
		rec[R_O2X] = (int16_t)(l2[0] - o2[0]);
		rec[R_O2Y] = (int16_t)(l2[1] - o2[1]);
		rec[R_O2Z] = (int16_t)(l2[2] - o2[2]);
		rec[R_O1X] = (int16_t)(l1[0] - o1[0]);
		rec[R_O1Y] = (int16_t)(l1[1] - o1[1]);
		rec[R_O1Z] = (int16_t)(l1[2] - o1[2]);
		rec[R_ORPM] = state.opponentstate.car_currpm;
		ncars = 2;
	} else {
		ncars = 1;                         /* LAB_1471_44a6 */
	}

	/* LAB_1471_44b4 - turn the level bits into start/stop edges */
	for (i = 0; i < ncars; i++) {
		const struct CARSTATE* cs;
		int16_t chan;
		char f;

		if (i == 0) {
			cs = &state.playerstate; chan = word_43964; f = byte_42D26;
		} else {
			cs = &state.opponentstate; chan = word_4408C; f = byte_42D2A;
		}

		/* bit 0 - the engine loop */
		if (cs->field_CF & 1) {
			if (!(f & 1)) { f |= 1; audio_op_unk(chan); }
		} else {
			if (f & 1) { f--; audio_function2(chan); }
		}

		/* bits 1 and 2 - the two skid loops, one at a time */
		if (cs->field_CF & 6) {
			if ((cs->field_CF & 6) != (f & 6)) {
				if (f & 6) goto skid_off;   /* LAB_1471_454c */
				if (cs->field_CF & 2) { audio_op_unk5(chan); f += 2; }
				else                  { audio_op_unk6(chan); f += 4; }
			}
		} else if (f & 6) {
skid_off:
			/* [ODDITY, faithful] switching directly from skid A to skid B
			 * takes two frames: this frame stops A, the next starts B. */
			if (f & 2) f -= 2;
			if (f & 4) f -= 4;
			audio_op_unk7(chan);
		}

		if (i != 0) byte_42D2A = f; else byte_42D26 = f;
	}

	/* LAB_1471_45a6 */
	byte_459D8 = 1;
	word_449E4++;
	if (word_449E4 == AUD_RING) word_449E4 = 0;
	byte_3BE02_a = is_in_replay;
}

/* frame_callback's drain (r1 seg005.asm 1099..1136). One 100 Hz tick. */
static void audio_timer_tick(void)
{
	word_443F4++;
	if (word_443F4 < word_4499C) return;
	if (word_44D1E == word_449E4) return;
	sub_18D06(unk_44F4C[word_44D1E], word_443F4);
	word_443F4 = 0;
	word_44D1E++;
	if (word_44D1E == AUD_RING) word_44D1E = 0;
}

/* ==================================================================== */
/* PART 2 - the synth                                                   */
/* ==================================================================== */

typedef struct {
	double   ph1, ph2, lp;
	double   bp_lo, bp_band;
	double   env_crash, env_bump, env_scrape;
	double   lp_crash, lp_bump, lp_scrape;
	uint32_t seen_crash, seen_bump, seen_scrape;
	/* the parameters the sim thread hands over */
	double   freq, vol;
	int      engine, skid;
} VOICE;

static VOICE voices[2];
static uint32_t snd_rng = 0x1234567u;

static double snd_noise(void)
{
	snd_rng = snd_rng * 1664525u + 1013904223u;
	return (double)(int32_t)snd_rng / 2147483648.0;
}

/* Copy the ported decision state into the synth. Called under the audio
 * lock so the callback never sees a half-updated voice. */
static void snd_publish(void)
{
	int c;
	for (c = 0; c < 2; c++) {
		AUDTIMER* t = &audiotimers[c];
		VOICE* v = &voices[c];
		v->engine = t->engine;
		v->skid   = t->skid;
		v->freq   = (double)(uint16_t)t->freq;
		v->vol    = (double)t->vol / 127.0;
		if (v->vol < 0) v->vol = 0;
		if (v->vol > 1) v->vol = 1;
		if (t->hit_crash  != v->seen_crash)  { v->seen_crash  = t->hit_crash;  v->env_crash  = 1.0; }
		if (t->hit_bump   != v->seen_bump)   { v->seen_bump   = t->hit_bump;   v->env_bump   = 1.0; }
		if (t->hit_scrape != v->seen_scrape) { v->seen_scrape = t->hit_scrape; v->env_scrape = 1.0; }
	}
}

static void snd_render(int16_t* out, int nsamples)
{
	int n, c;
	for (n = 0; n < nsamples; n++) {
		double mix = 0.0;
		for (c = 0; c < 2; c++) {
			VOICE* v = &voices[c];
			double amp = v->vol;

			if (v->engine && v->freq > 1.0) {
				double f = v->freq;
				double cut, raw;
				if (f > 4000.0) f = 4000.0;
				v->ph1 += f / SND_RATE;          if (v->ph1 >= 1.0) v->ph1 -= 1.0;
				v->ph2 += f * 2.01 / SND_RATE;   if (v->ph2 >= 1.0) v->ph2 -= 1.0;
				raw = 0.70 * (2.0 * v->ph1 - 1.0)
				    + 0.30 * (2.0 * v->ph2 - 1.0);
				/* one-pole lowpass that opens with revs, so the tone gets
				 * brighter as well as higher */
				cut = 3.0 * f / SND_RATE;
				if (cut > 0.45) cut = 0.45;
				v->lp += cut * (raw - v->lp);
				mix += 0.33 * amp * v->lp;
			}

			if (v->skid) {
				/* state-variable bandpass on white noise */
				/* 2*sin(pi*fc/rate) for fc = 2600 Hz and 1450 Hz at
				 * 44100. Constants rather than sin(): the build's -I
				 * puts render_faithful/math.h in front of <math.h>. */
				double f  = (v->skid == 2) ? 0.36832 : 0.20622;
				double q  = 0.22;
				double in = snd_noise();
				v->bp_lo   += f * v->bp_band;
				v->bp_band += f * (in - v->bp_lo - q * v->bp_band);
				mix += 0.30 * amp * v->bp_band;
			}

			if (v->env_crash > 0.0005) {
				double in = snd_noise();
				v->lp_crash += 0.10 * (in - v->lp_crash);
				mix += 0.9 * v->env_crash * (0.6 * v->lp_crash + 0.4 * in);
				v->env_crash *= 0.99993;   /* ~0.5 s */
			} else v->env_crash = 0.0;

			if (v->env_bump > 0.0005) {
				double in = snd_noise();
				v->lp_bump += 0.03 * (in - v->lp_bump);
				mix += 2.2 * v->env_bump * v->lp_bump;
				v->env_bump *= 0.9996;     /* ~0.05 s */
			} else v->env_bump = 0.0;

			if (v->env_scrape > 0.0005) {
				double in = snd_noise();
				v->lp_scrape += 0.25 * (in - v->lp_scrape);
				mix += 0.8 * v->env_scrape * v->lp_scrape;
				v->env_scrape *= 0.9993;   /* ~0.03 s, retriggered */
			} else v->env_scrape = 0.0;
		}
		if (mix >  1.0) mix =  1.0;
		if (mix < -1.0) mix = -1.0;
		out[n] = (int16_t)(mix * 30000.0);
	}
}

static void SDLCALL snd_callback(void* ud, Uint8* stream, int len)
{
	(void)ud;
	snd_render((int16_t*)stream, len / 2);
	/* Phase 8: the music mixes on top of the effects. music_native_mix adds
	 * into the buffer and is a no-op when no song is playing or when the
	 * option menu has music switched off. */
	music_native_mix((int16_t*)stream, len / 2, 1);
}

/* ------------------------------------------------------------------ */
/* wav writing (deterministic verification path)                       */
/* ------------------------------------------------------------------ */
static void wav_open(const char* path)
{
	uint8_t h[44];
	snd_wav = fopen(path, "wb");
	if (!snd_wav) return;
	memset(h, 0, sizeof h);
	memcpy(h, "RIFF", 4); memcpy(h + 8, "WAVEfmt ", 8);
	h[16] = 16; h[20] = 1; h[22] = 1;
	h[24] = (uint8_t)(SND_RATE & 0xFF); h[25] = (uint8_t)((SND_RATE >> 8) & 0xFF);
	h[28] = (uint8_t)((SND_RATE * 2) & 0xFF); h[29] = (uint8_t)(((SND_RATE * 2) >> 8) & 0xFF);
	h[32] = 2; h[34] = 16;
	memcpy(h + 36, "data", 4);
	fwrite(h, 1, sizeof h, snd_wav);
}

static void wav_close(void)
{
	uint8_t v[4];
	uint32_t riff;
	if (!snd_wav) return;
	riff = snd_wav_bytes + 36;
	fseek(snd_wav, 4, SEEK_SET);
	v[0] = riff & 0xFF; v[1] = (riff >> 8) & 0xFF; v[2] = (riff >> 16) & 0xFF; v[3] = (riff >> 24) & 0xFF;
	fwrite(v, 1, 4, snd_wav);
	fseek(snd_wav, 40, SEEK_SET);
	v[0] = snd_wav_bytes & 0xFF; v[1] = (snd_wav_bytes >> 8) & 0xFF;
	v[2] = (snd_wav_bytes >> 16) & 0xFF; v[3] = (snd_wav_bytes >> 24) & 0xFF;
	fwrite(v, 1, 4, snd_wav);
	fclose(snd_wav);
	snd_wav = NULL;
}

/* ------------------------------------------------------------------ */
/* PCENG1.VCE - read the two engine-pitch bytes out of the real file   */
/* ------------------------------------------------------------------ */
/* A .VCE is: dword total size, word count, count * 4-char names, count
 * dwords of entry offsets, then the entries. Verified against all four
 * shipped ENG1 files. The ENGI entry's bytes +0x0E and +0x0F are the two
 * audio_op_unk2 reads through `les bx,[di+8]`. */
static void vce_read_engine(const char* data_dir)
{
	char path[1024];
	FILE* f;
	uint8_t hdr[6];
	uint8_t* names = NULL;
	uint8_t* offs = NULL;
	uint8_t ent[16];
	long data0;
	int count, i, idx = -1;
	uint32_t off;

	snprintf(path, sizeof path, "%s/PCENG1.VCE", data_dir);
	f = fopen(path, "rb");
	if (!f) return;
	if (fread(hdr, 1, 6, f) != 6) { fclose(f); return; }
	count = hdr[4] | (hdr[5] << 8);
	if (count <= 0 || count > 64) { fclose(f); return; }
	names = (uint8_t*)malloc((size_t)count * 4);
	offs  = (uint8_t*)malloc((size_t)count * 4);
	if (!names || !offs) { free(names); free(offs); fclose(f); return; }
	if (fread(names, 1, (size_t)count * 4, f) != (size_t)count * 4 ||
	    fread(offs,  1, (size_t)count * 4, f) != (size_t)count * 4) {
		free(names); free(offs); fclose(f); return;
	}
	data0 = 6 + (long)count * 8;
	for (i = 0; i < count; i++)
		if (memcmp(names + i * 4, "ENGI", 4) == 0) { idx = i; break; }
	if (idx < 0) { free(names); free(offs); fclose(f); return; }
	off = (uint32_t)offs[idx * 4] | ((uint32_t)offs[idx * 4 + 1] << 8)
	    | ((uint32_t)offs[idx * 4 + 2] << 16) | ((uint32_t)offs[idx * 4 + 3] << 24);
	if (fseek(f, data0 + (long)off, SEEK_SET) == 0 && fread(ent, 1, 16, f) == 16) {
		vce_engi_div = ent[0x0E];
		vce_engi_off = ent[0x0F];
		if (vce_engi_div == 0) vce_engi_div = 6;
	}
	free(names); free(offs); fclose(f);
}

/* ==================================================================== */
/* public API                                                           */
/* ==================================================================== */

int audio_native_init(const char* data_dir, int mode, const char* wav_path)
{
	snd_mode = mode;
	if (mode == 0) return 0;

	vce_read_engine(data_dir);

	word_43964 = 0;
	word_4408C = 1;
	byte_459D8 = 0;
	byte_42D26 = byte_42D2A = 0;
	memset(audiotimers, 0, sizeof audiotimers);
	memset(voices, 0, sizeof voices);
	memset(unk_44F4C, 0, sizeof unk_44F4C);
	word_449E4 = word_44D1E = word_443F4 = 0;
	word_4499C = (int16_t)(100 / (framespersec ? framespersec : 20));

	audio_hook_unk3    = audio_hook_unk3_impl;
	audio_hook_fn2wrap = audio_hook_fn2wrap_impl;

	if (mode == 2) {
		wav_open(wav_path);
		if (!snd_wav) { snd_mode = 0; return -1; }
		return 0;
	}

	{
		SDL_AudioSpec want, have;
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
			fprintf(stderr, "ljud: SDL_INIT_AUDIO: %s\n", SDL_GetError());
			snd_mode = 0;
			return -1;
		}
		SDL_zero(want);
		want.freq = SND_RATE;
		want.format = AUDIO_S16SYS;
		want.channels = 1;
		want.samples = 512;
		want.callback = snd_callback;
		snd_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
		if (!snd_dev) {
			fprintf(stderr, "ljud: SDL_OpenAudioDevice: %s\n", SDL_GetError());
			snd_mode = 0;
			return -1;
		}
		SDL_PauseAudioDevice(snd_dev, 0);
	}
	return 0;
}

void audio_native_set_enabled(int on)
{
	snd_enabled = on ? 1 : 0;
	if (!snd_enabled && snd_mode) {
		/* the teardown path, without pretending is_in_replay changed */
		if (snd_dev) SDL_LockAudioDevice(snd_dev);
		memset(audiotimers, 0, sizeof audiotimers);
		snd_publish();
		if (snd_dev) SDL_UnlockAudioDevice(snd_dev);
		byte_42D26 = byte_42D2A = 0;
	}
}

int audio_native_enabled(void) { return snd_enabled; }

void audio_native_set_trace(int on) { snd_trace = on; }

void audio_native_frame(void)
{
	int i;
	if (!snd_mode || !snd_enabled) return;

	audio_carstate();
	/* word_4499C ticks of the original's 100 Hz callback per sim frame,
	 * which drains exactly one ring record with elapsed == word_4499C. */
	for (i = 0; i < word_4499C; i++) audio_timer_tick();

	if (snd_dev) SDL_LockAudioDevice(snd_dev);
	snd_publish();
	if (snd_dev) SDL_UnlockAudioDevice(snd_dev);

	if (snd_trace) {
		printf("aud f=%5d rpm=%5d hz=%5d vol=%3d eng=%d skid=%d "
		       "cf=%02X crash=%u bump=%u scrape=%u\n",
		       state.game_frame,
		       (int)state.playerstate.car_currpm,
		       (int)(uint16_t)audiotimers[0].freq,
		       (int)audiotimers[0].vol,
		       audiotimers[0].engine, audiotimers[0].skid,
		       (unsigned char)state.playerstate.field_CF,
		       audiotimers[0].hit_crash, audiotimers[0].hit_bump,
		       audiotimers[0].hit_scrape);
		if (gameconfig.game_opponenttype != 0) {
			const int16_t* r = unk_44F4C[(word_44D1E + AUD_RING - 1) % AUD_RING];
			int16_t d = polarRadius2D(polarRadius2D(r[R_O1X], r[R_O1Z]), r[R_O1Y]);
			printf("opp f=%5d rpm=%5d hz=%5d vol=%3d eng=%d skid=%d "
			       "dist=%5d cf=%02X\n",
			       state.game_frame,
			       (int)state.opponentstate.car_currpm,
			       (int)(uint16_t)audiotimers[1].freq,
			       (int)audiotimers[1].vol,
			       audiotimers[1].engine, audiotimers[1].skid,
			       (int)d,
			       (unsigned char)state.opponentstate.field_CF);
		}
	}

	if (snd_mode == 2 && snd_wav) {
		static int16_t buf[SND_RATE / 5];
		int n = SND_RATE / (framespersec ? framespersec : 20);
		if (n > (int)(sizeof buf / sizeof buf[0])) n = (int)(sizeof buf / sizeof buf[0]);
		snd_render(buf, n);
		fwrite(buf, 2, (size_t)n, snd_wav);
		snd_wav_bytes += (uint32_t)n * 2;
	}
}

void audio_native_shutdown(void)
{
	if (snd_dev) { SDL_CloseAudioDevice(snd_dev); snd_dev = 0; }
	if (snd_wav) wav_close();
	audio_hook_unk3 = NULL;
	audio_hook_fn2wrap = NULL;
	snd_mode = 0;
}
