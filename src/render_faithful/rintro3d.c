/*
 * rintro3d.c - the 3D intro animation between the two title stills and the
 * credits.
 *
 * Ported from reference/restunts/src/restunts/asm/seg003.asm:
 *
 *   setup_intro   6268..6830   (562 lines)
 *   intro_op      6831..7182   (351 lines)
 *
 * plus the one routine setup_intro needs that had no port yet:
 *
 *   init_plantrak       seg001.asm 8529..8682   (154 lines)
 *   do_opponent_op      seg001.asm 8683..8690   (a far thunk to opponent_op)
 *   setup_aero_trackdata  restunts.c 715..737
 *
 * ------------------------------------------------------------------------
 * WHAT THE ANIMATION IS
 *
 * Three objects out of TITLE.P3S - "logo", "log2" and "brav" - a hundred
 * randomly placed single-pixel stars, and a Lamborghini Countach that is
 * genuinely SIMULATED, not scripted: init_plantrak builds a five-entry
 * racing line out of nothing (track elements 7, 6, 8, 9, 7 at columns
 * 1,0,0,1,1 and rows 28,28,29,29,28) and hands it to the ordinary opponent
 * AI, which then drives round it for 23 seconds while the camera watches.
 *
 * There are three camera phases, cut on the frame counter var_5D4:
 *
 *   frames 1 .. 6*fps    (0-6 s)   the camera IS the car: position = the
 *                                  car's own, 0x14 higher, yaw = the car's
 *                                  heading, pitch 0.  arg_A = 0, so the car
 *                                  itself is not drawn - you are in it.
 *                                  "log2" is the object in view.
 *   6*fps .. 11*fps      (6-11 s)  the camera jumps to the fixed point
 *                                  (0x400, 0x5A, 0x400) and LOOKS AT the
 *                                  car, which is now drawn (arg_A = 1).
 *                                  The look-at is done the long way:
 *                                  polarAngle for the yaw, polarRadius2D
 *                                  then polarAngle for the pitch.
 *   11*fps .. 23*fps    (11-23 s)  the camera climbs (var_3C += 0x14 per
 *                                  frame), pulls back (var_3A -= 5), eases
 *                                  var_3E back to 0x400 ten units at a
 *                                  time, and the look-at target walks one
 *                                  unit per frame towards (0x400, *, 0x400).
 *                                  arg_C flips to 1, so "logo" replaces
 *                                  "log2".
 *
 * The stars are laid out once, from get_kevinrandom:
 *     x = (r << 7) - 0x4000        y = -((r << 7) - 0x1388)      z = as x
 * and drawn one pixel each in a colour that cycles 1..15 (word_407CC = 16
 * is the wrap point), so the field shimmers.  A star is only drawn when its
 * camera-space z exceeds 0xC8 - the same near plane the projection uses.
 *
 * The projection is set_projection(0x28, 0x28, 0x140, 0xC8): 40 degrees
 * each way over the whole screen, which is much wider than the 0x23 the
 * in-car view uses.
 *
 * ------------------------------------------------------------------------
 * THE SLOW-VIDEO BRANCH IS NOT PORTED, AND IT IS NOT REACHABLE
 *
 * Almost half of both routines is under `slow_video_mgmt_copy != 0`: two
 * ping-pong 100-entry POINT2D buffers, three global RECTANGLEs, rect_union
 * / rect_intersect and a dirty-rectangle blit, so that on a slow machine
 * only the changed strip is copied out of the offscreen window.  That flag
 * is dseg's `slow_video_mgmt`, which the graphics-detail menu sets and
 * which is 0 in every configuration this port has; and the whole mechanism
 * exists to avoid copying a full offscreen window, which this port does not
 * have (see [DEVIATION] 1 in rintro.c).  The taken branch is ported
 * instruction by instruction; the other is described here and skipped.
 * Same for `video_flag5_is0`, which selects setup_mcgawnd1/2 - the port has
 * no MCGA page flipping and game_init keeps that flag 0.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] - stated plainly.
 *
 *  1. The set-up the intro needs is a cut-down game_init.  The original
 *     reaches run_intro_looped with init_trackdata() already done and
 *     DEFAULT.TRK already read into td14_elem_map_main (seg000:1041D reads
 *     the whole 1802-byte file there, which fills td15 as well because the
 *     two are contiguous), but with NO track_setup: the intro's five-tile
 *     racing line is written by init_plantrak by hand.  intro3d_init below
 *     reproduces exactly that, plus the four video flags and the material
 *     tables that main_native.c's game_init sets and that nothing has set
 *     yet at intro time.  It is idempotent and game_init redoes all of it.
 *
 *  2. td04_aerotable_pl / td05_aerotable_op are not carved out of the
 *     trakdata block in this port (sfdata.c leaves those two slots
 *     unassigned), so setup_aero_trackdata writes into a static array here,
 *     exactly as main_native.c's game_init already does for a race.
 *
 *  3. putpixel_single_maybe writes rfb_pixels directly and does not scale,
 *     the deviation Phase 5 already recorded.  At RFB_SCALE > 1 the stars
 *     are therefore single pixels on a larger screen, which is what they
 *     are meant to be.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "rfbsize.h"
#include "math.h"
#include "shape3d.h"
#include "shape2d.h"
#include "rintro.h"
#include "../asset/stunts_asset_loader.h"

extern uint8_t rfb_pixels[];
extern struct MATRIX mat_temp;
extern void  putpixel_single_maybe(uint16_t x, uint16_t y, uint16_t colour);
extern void  init_polyinfo(void);
extern void  rblit_init(void);
extern void  rfileio_set_data_dir(const char* dir);
extern void  copy_material_list_pointers(int16_t*, int16_t*, int16_t*,
                                         int16_t*, int16_t);
extern int16_t *material_clrlist_ptr, *material_clrlist2_ptr;
extern int16_t *material_patlist_ptr, *material_patlist2_ptr;
extern void  sfdata_init_track_tables(void);
extern void  sfdata_init_trackdata(void);
extern void  locate_many_resources(char far* data, char* names, char far** result);
extern void* file_load_3dres(const char* filename);
extern void* file_load_resfile(const char* filename);
extern void* locate_shape_alt(void* resptr, const char* shapename);
extern void  opponent_op(void);
extern int16_t sub_18D60(int16_t, struct VECTOR*, int16_t, int16_t*);
extern struct SIMD simd_opponent;
extern int16_t video_flag1_is1, video_flag2_is1, video_flag3_isFFFF,
               video_flag5_is0;
extern char oppnentSped[16];
extern char startcol2, startrow2;
extern int16_t terrainpos[], terraincenterpos[];
extern uint16_t slow_video_mgmt;
extern uint16_t slow_video_mgmt_copy;
extern void  unload_resource(void far* resptr);
extern struct PLANE far *planptr, *current_planptr;
extern int16_t planindex;
extern uint16_t polyinfonumpolys;
/* dseg.asm:38492 - setup_intro's own tick accumulator. */
static int16_t word_44DCC;

/* dseg.asm:2630 - intro_cliprect {0, 0x140, 0, 0xC8}, scaled with the
 * framebuffer like every other pixel rectangle in this port. */
static struct RECTANGLE intro_cliprect = { 0, RFB_VIEW_W, 0, RFB_VIEW_H };
/* dseg.asm:2638 */
static int16_t intro_colorvalue = 1;
/* dseg.asm:20572 */
static const int16_t word_407CC = 16;

/* dseg.asm:35776 / 35836 / 38272 - the three SHAPE3D slots the intro owns. */
static struct SHAPE3D logoshape, logo2shape, bravshape;

/* [DEVIATION] 2 */
static int16_t s_aero_op[0x40];

/* ------------------------------------------------------------------ */
/* restunts.c:715 setup_aero_trackdata(carresptr, 1)                    */
/* ------------------------------------------------------------------ */
static void setup_aero_trackdata_opponent(void far* carresptr)
{
	const void far* simd = locate_shape_alt((void*)carresptr, "simd");
	int i;
	if (simd) memcpy(&simd_opponent, simd, sizeof(struct SIMD));
	simd_opponent.aerorestable = s_aero_op;
	for (i = 0; i < 0x40; i++)
		s_aero_op[i] = (int16_t)(((int32_t)simd_opponent.aero_resistance
		                          * (int32_t)i * (int32_t)i) >> 9);
	/* copy_string(gsna_string, locate_shape_alt(carresptr, "gsna")) - the
	 * opponent's initials, which nothing in the intro draws. */
}

/* ------------------------------------------------------------------ */
/* seg001 init_plantrak (8529..8682): the five-tile racing line the      */
/* intro's Countach drives round, written by hand.                      */
/* ------------------------------------------------------------------ */
/* restunts.c init_carstate_from_simd, the same transcription main_native.c
 * carries as its static init_carstate (main_native.c:222..260).  Kept local
 * so nothing else has to change. */
static void intro_init_carstate(struct CARSTATE* p, struct SIMD* simd,
                                char transmission, int32_t posX, int32_t posY,
                                int32_t posZ, int16_t track_angle)
{
	struct VECTOR whlPos;
	int i;
	p->car_posWorld1.lx = posX; p->car_posWorld2.lx = posX;
	p->car_posWorld1.ly = posY + 512; p->car_posWorld2.ly = posY;
	p->car_posWorld1.lz = posZ; p->car_posWorld2.lz = posZ;
	p->car_rotate.x = track_angle; p->car_rotate.y = 0; p->car_rotate.z = 0;
	p->car_36MwhlAngle = 0; p->car_pseudoGravity = 0; p->car_steeringAngle = 0;
	p->car_is_braking = 0; p->car_is_accelerating = 0;
	p->car_currpm = simd->idle_rpm;
	p->car_lastrpm = p->car_currpm;
	p->car_idlerpm2 = p->car_currpm;
	p->car_current_gear = 1;
	p->car_speeddiff = 0; p->car_speed = 0; p->car_speed2 = 0; p->car_lastspeed = 0;
	p->car_gearratio = simd->gear_ratios[1];
	p->car_gearratioshr8 = p->car_gearratio >> 8;
	p->car_knob_x = simd->knob_points[1].px; p->car_knob_x2 = p->car_knob_x;
	p->car_knob_y = simd->knob_points[1].py; p->car_knob_y2 = p->car_knob_y;
	p->car_angle_z = 0; p->car_40MfrontWhlAngle = 0;
	p->field_42 = 0; p->field_48 = 0; p->car_trackdata3_index = 0;
	p->car_sumSurfFrontWheels = 2; p->car_sumSurfRearWheels = 2;
	p->car_sumSurfAllWheels = 4;
	p->car_demandedGrip = 0; p->car_surfacegrip_sum = 1000;
	whlPos.x = (int16_t)(posX / 64);
	whlPos.y = (int16_t)(posY / 64);
	whlPos.z = (int16_t)(posZ / 64);
	for (i = 0; i < 4; ++i) {
		p->car_surfaceWhl[i] = 1;
		p->car_rc1[i] = p->car_rc2[i] = p->car_rc3[i] = 0;
		p->car_rc4[i] = p->car_rc5[i] = 0;
		p->car_whlWorldCrds1[i] = whlPos;
		p->car_whlWorldCrds2[i] = whlPos;
	}
	p->car_engineLimiterTimer = 0; p->car_slidingFlag = 0; p->field_C8 = 0;
	p->car_crashBmpFlag = 0; p->car_changing_gear = 0; p->car_fpsmul2 = 0;
	p->car_transmission = transmission;
	p->field_CD = 0; p->field_CE = 0; p->field_CF = 1;
}

static void init_plantrak(void)
{
	int16_t si = 0;
	int16_t i;
	int16_t* t3;

	/* init_game_state(-3): only the three lines outside its `arg != -3`
	 * block (restunts.c:456..464). */
	if (framespersec == 0) framespersec = 20;
	word_45A00 = (int16_t)(framespersec * 30);
	word_4499C = (int16_t)(100 / framespersec);

	state.game_inputmode = 2;
	/* planptr = plan_memres: the collision planes, loaded in intro3d_init. */
	startcol2 = 1;
	startrow2 = 0x1C;

	{
		static const uint8_t elem[5] = { 7, 6, 8, 9, 7 };
		static const uint8_t col[5]  = { 1, 0, 0, 1, 1 };
		static const uint8_t rowadd[5] = { 0, 0, 1, 1, 0 };
		for (i = 0; i < 5; i++) {
			((uint8_t far*)td17_trk_elem_ordered)[i] = elem[i];
			((uint8_t far*)td21_col_from_path)[i] = col[i];
			((uint8_t far*)td22_row_from_path)[i] =
				(uint8_t)(startrow2 + rowadd[i]);
			((uint8_t far*)trackdata18)[i] = 0;
		}
	}

	t3 = (int16_t far*)trackdata3;
	{
		static const int16_t path[18] = {
			0, 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 0, 1, 2, 3, 0
		};
		for (i = 0; i < 18; i++) t3[i] = path[i];
	}

	oppnentSped[0] = (char)0xC8;

	intro_init_carstate(&state.opponentstate, &simd_opponent, 1,
	                    0x00017700L, 0L,
	                    (int32_t)(trackpos[0x1C] + 0x12E) << 6,
	                    0);

	sub_18D60(t3[state.opponentstate.car_trackdata3_index],
	          &state.opponentstate.car_vec_unk3,
	          (int16_t)(uint8_t)state.opponentstate.field_CE,
	          (int16_t*)&state.field_3F9);
	state.opponentstate.field_CE++;
	(void)si;
}

/* ------------------------------------------------------------------ */
/* [DEVIATION] 1 - the cut-down game_init the intro needs.              */
/* ------------------------------------------------------------------ */
/* init_polyinfo allocates; everything else here is idempotent and is redone
 * on every pass, exactly as restunts.c:1584 re-reads DEFAULT.TRK before
 * every run_intro_looped.  That matters because a race played between two
 * intros leaves its own track in td14 and its own tables everywhere. */
static int s_polyinfo_done;

static void intro3d_init(const char* data_dir)
{
	int i;

	rfileio_set_data_dir(data_dir);
	rblit_init();

	/* restunts.c:1256-1264, the "// Video" block, and seg031.asm:236.
	 * video_flag1_is1 is the one the CREDITS need too (seg000:875 multiplies
	 * the arrow's width by it), so it is set here rather than in game_init. */
	video_flag1_is1 = 1;
	video_flag2_is1 = 1;
	video_flag3_isFFFF = -1;
	video_flag5_is0 = 0;
	slow_video_mgmt = 0;

	if (framespersec == 0) framespersec = 20;

	for (i = 0; i < 30; i++) {                 /* init_row_tables */
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
		trackpos[i] = (int16_t)((29 - i) << 10);
		trackpos2[i] = (int16_t)(i << 10);
		trackcenterpos[i] = (int16_t)(((29 - i) << 10) + 0x200);
		trackcenterpos2[i] = (int16_t)((i << 10) + 0x200);
		terrainpos[i] = (int16_t)(i << 10);
		terraincenterpos[i] = (int16_t)((i << 10) + 0x200);
	}
	sfdata_init_track_tables();
	sfdata_init_trackdata();

	/* seg000:1041D - the whole 1802-byte .TRK straight into
	 * td14_elem_map_main, which is contiguous with td15_terr_map_main. */
	{
		char p[600];
		FILE* f;
		snprintf(p, sizeof p, "%s/DEFAULT.TRK", data_dir);
		f = fopen(p, "rb");
		if (f) {
			if (fread(td14_elem_map_main, 1, 1802, f) != 1802)
				fprintf(stderr, "intro: DEFAULT.TRK ar kortare an 1802 byte\n");
			fclose(f);
		} else {
			fprintf(stderr, "intro: kan inte oppna %s\n", p);
		}
	}

	/* The collision planes init_plantrak's `planptr = plan_memres` points
	 * at.  Same load main_native.c's game_init does. */
	{
		char p[600];
		stunts_plane_t* planes = NULL;
		uint16_t n = 0;
		snprintf(p, sizeof p, "%s/GAME.PRE", data_dir);
		if (stunts_load_collision_data(p, &planes, &n)) {
			planptr = (struct PLANE far*)planes;
			current_planptr = planptr;
			planindex = 0;
		}
	}

	copy_material_list_pointers(material_clrlist_ptr, material_clrlist2_ptr,
	                            material_patlist_ptr, material_patlist2_ptr, 0);
	if (!s_polyinfo_done) { s_polyinfo_done = 1; init_polyinfo(); }
}

/* ------------------------------------------------------------------ */
/* seg003 intro_op (6831..7182)                                         */
/* ------------------------------------------------------------------ */
static void intro_op(int16_t arg_0, int16_t arg_2, int16_t arg_4,
                     int16_t arg_6, int16_t arg_8, int16_t arg_A,
                     int16_t arg_C, const struct VECTOR* arg_E)
{
	struct TRANSFORMEDSHAPE3D ts;
	struct VECTOR var_vec, var_vec2;
	struct POINT2D var_point;
	int16_t si;

	memset(&ts, 0, sizeof ts);

	select_cliprect_rotate(0, arg_8, arg_6, &intro_cliprect, 0);

	ts.shapeptr = arg_C ? &logoshape : &logo2shape;   /* loc_1DEC4 */
	ts.pos.x = (int16_t)(0x400 - arg_0);
	ts.pos.y = (int16_t)(-arg_2);
	ts.pos.z = (int16_t)(0x400 - arg_4);
	ts.ts_flags = 4;                                  /* loc_1DF16 */
	ts.rotvec.x = 0; ts.rotvec.y = 0; ts.rotvec.z = 0;
	ts.unk = 0x400;
	ts.material = 0;
	transformed_shape_op(&ts);

	if (arg_A) {                                      /* loc_1DF47 */
		ts.pos.x = (int16_t)((state.opponentstate.car_posWorld1.lx >> 6) - arg_0);
		ts.pos.y = (int16_t)((state.opponentstate.car_posWorld1.ly >> 6) - arg_2);
		ts.pos.z = (int16_t)((state.opponentstate.car_posWorld1.lz >> 6) - arg_4);
		ts.shapeptr = &bravshape;
		ts.ts_flags = 4;
		ts.rotvec.x = 0;
		ts.rotvec.y = 0;
		ts.rotvec.z = (int16_t)(-state.opponentstate.car_rotate.x);
		ts.unk = 0x400;
		ts.material = 0;
		transformed_shape_op(&ts);
	}

	/* loc_1E06C - the slow_video_mgmt_copy == 0 arm, see the note above. */
	sprite_set_1_size((uint16_t)intro_cliprect.left,
	                  (uint16_t)intro_cliprect.right,
	                  (uint16_t)intro_cliprect.top,
	                  (uint16_t)intro_cliprect.bottom);
	sprite_clear_1_color(0);
	sprite_set_1_size((uint16_t)intro_cliprect.left,   /* loc_1E08F */
	                  (uint16_t)intro_cliprect.right,
	                  (uint16_t)intro_cliprect.top,
	                  (uint16_t)intro_cliprect.bottom);

	for (si = 0; si < 0x64; si++) {                   /* loc_1E0AB */
		var_vec2.x = (int16_t)(arg_E[si].x - arg_0);
		var_vec2.y = (int16_t)(arg_E[si].y - arg_2);
		var_vec2.z = (int16_t)(arg_E[si].z - arg_4);
		mat_mul_vector(&var_vec2, &mat_temp, &var_vec);
		if (var_vec.z > 0xC8) {
			vector_to_point(&var_vec, &var_point);
			putpixel_single_maybe((uint16_t)var_point.px,
			                      (uint16_t)var_point.py,
			                      (uint16_t)intro_colorvalue);
			intro_colorvalue++;                       /* loc_1E144 */
			if (intro_colorvalue == word_407CC) intro_colorvalue = 1;
		}
	}

	get_a_poly_info();                                /* loc_1E16C */
}

/* ------------------------------------------------------------------ */
/* seg003 setup_intro (6268..6830)                                      */
/* ------------------------------------------------------------------ */
int16_t rintro_setup_intro(const char* data_dir)
{
	char  var_38 = 0;
	void far* title3dres;
	char far* res[4];
	struct VECTOR stars[100];
	int16_t var_3E, var_3C, var_3A;     /* the camera position            */
	int16_t var_6 = 0, var_4 = 0, var_2 = 0;   /* the look-at target      */
	int16_t var_E = 0;                  /* which logo shape               */
	int16_t var_5D4 = 0;                /* the frame counter              */
	int16_t var_2A2 = 1;                /* "something moved, redraw"      */
	int16_t var_2C, var_2A4, var_2A6, var_C;
	int16_t var_2A0, var_29E, var_29C;
	int16_t var_40 = 0, var_42, i;
	const char* shotdir = getenv("STUNTS_INTRO3D_SHOTS");
	int shotstep = 0, shotn = 0;

	if (shotdir) {
		const char* s = getenv("STUNTS_INTRO3D_STEP");
		shotstep = s ? atoi(s) : 40;
		if (shotstep <= 0) shotstep = 40;
	}

	intro3d_init(data_dir);

	title3dres = file_load_3dres("title");            /* seg003:6320 */
	if (!title3dres) return 0;
	locate_many_resources((char far*)title3dres, "logolog2brav", res);
	if (!res[0] || !res[1] || !res[2]) {
		fprintf(stderr, "intro: TITLE.P3S saknar logo/log2/brav\n");
		unload_resource(title3dres);
		return 0;
	}
	shape3d_init_shape(res[0], &logoshape);
	shape3d_init_shape(res[1], &logo2shape);
	shape3d_init_shape(res[2], &bravshape);

	/* sprite_make_wnd(0x140, 0xC8, 0x0F) when video_flag5_is0 == 0 -
	 * no offscreen windows in this port. */

	for (i = 0; i < 0x64; i++) {                      /* loc_1D9CF */
		stars[i].x = (int16_t)((get_kevinrandom() << 7) - 0x4000);
		stars[i].y = (int16_t)(-(int16_t)((get_kevinrandom() << 7) - 0x1388));
		stars[i].z = (int16_t)((get_kevinrandom() << 7) - 0x4000);
	}

	set_projection(0x28, 0x28, RFB_VIEW_W, RFB_VIEW_H);
	var_3E = 0x400;
	var_3A = 0x400;
	var_3C = 0x12C;

	{                                                 /* seg003:6460 */
		void far* carres = file_load_resfile("carcoun");
		if (carres) {
			setup_aero_trackdata_opponent(carres);
			unload_resource(carres);
		}
	}
	init_plantrak();

	rintro_delta();                                   /* timer_get_delta */
	slow_video_mgmt_copy = slow_video_mgmt;
	word_44DCC = 0;

	for (;;) {                                        /* loc_1DADE */
		var_40 = rintro_delta();
		word_44DCC = (int16_t)(word_44DCC + var_40);

		while (word_44DCC > word_4499C) {             /* loc_1DAEE */
			word_44DCC = (int16_t)(word_44DCC - word_4499C);
			opponent_op();                            /* do_opponent_op */
			var_2A2 = 1;
			var_5D4++;
			if ((int16_t)(framespersec * 11) >= var_5D4) continue;
			var_E = 1;
			var_3C = (int16_t)(var_3C + 0x14);
			var_3A = (int16_t)(var_3A - 5);
			var_42 = (int16_t)(var_3E - 0x400);
			if ((var_42 < 0 ? -var_42 : var_42) < 0x0A) var_3E = 0x400;
			else if (var_42 > 0) var_3E = (int16_t)(var_3E - 0x0A);
			else if (var_42 < 0) var_3E = (int16_t)(var_3E + 0x0A);
			if (var_6 > 0x400) var_6--; else if (var_6 < 0x400) var_6++;
			if (var_2 > 0x400) var_2--; else if (var_2 < 0x400) var_2++;
		}

		if (var_2A2) {                                /* loc_1DB96 */
			var_2A2 = 0;
			/* sprite_copy_wnd_to_1 - no window */
			var_2C = (int16_t)0xFFFF;                 /* loc_1DBB9 */
			var_2A6 = 1;
			var_2A0 = (int16_t)(state.opponentstate.car_posWorld1.lx >> 6);
			var_29E = (int16_t)(state.opponentstate.car_posWorld1.ly >> 6);
			var_29C = (int16_t)(state.opponentstate.car_posWorld1.lz >> 6);

			if ((int16_t)(framespersec * 6) > var_5D4) {
				var_2A6 = 0;
				var_2C = (int16_t)(state.opponentstate.car_rotate.x & 0x3FF);
				var_2A4 = 0;
				var_3E = var_2A0;
				var_3C = (int16_t)(var_29E + 0x14);
				var_3A = var_29C;
			} else if ((int16_t)(framespersec * 11) > var_5D4) {
				var_3E = 0x400;                       /* loc_1DC44 */
				var_3A = 0x400;
				var_3C = 0x5A;
				var_6 = var_2A0;
				var_4 = var_29E;
				var_2 = var_29C;
			}

			if (var_2C == (int16_t)0xFFFF) {          /* loc_1DC7D */
				var_2C = (int16_t)((-polarAngle((int16_t)(var_6 - var_3E),
				                                (int16_t)(var_2 - var_3A)))
				                   & 0x3FF);
				var_C = polarRadius2D((int16_t)(var_6 - var_3E),
				                      (int16_t)(var_2 - var_3A));
				/* seg003:6739 pushes the radius FIRST and the height
				 * difference SECOND, so the height difference is
				 * polarAngle's first argument - the same order
				 * frame.c:246 uses for the chase camera. */
				var_2A4 = (int16_t)(polarAngle((int16_t)(var_4 - var_3C),
				                               var_C)
				                    & 0x3FF);
			}

			intro_op(var_3E, var_3C, var_3A, var_2C, var_2A4, var_2A6,
			         var_E, stars);
			if (getenv("STUNTS_INTRO3D_TRACE") && (var_5D4 % 20) == 0)
				printf("f%4d cam(%6d,%6d,%6d) yaw %4d pitch %4d car(%6ld,%6ld,%6ld) rotx %4d drawcar %d logo %d polys %d\n",
				       var_5D4, var_3E, var_3C, var_3A, var_2C, var_2A4,
				       (long)(state.opponentstate.car_posWorld1.lx >> 6),
				       (long)(state.opponentstate.car_posWorld1.ly >> 6),
				       (long)(state.opponentstate.car_posWorld1.lz >> 6),
				       state.opponentstate.car_rotate.x, var_2A6, var_E,
				       (int)polyinfonumpolys);
			/* loc_1DDFC: sprite_copy_2_to_1_2 + sprite_putimage(wnd) */
			rintro_present();

			if (shotdir && (var_5D4 % shotstep) == 0) {
				char p[600];
				snprintf(p, sizeof p, "%s/intro3d_%03d.bmp", shotdir, shotn++);
				rintro_write_bmp(p);
			}
		}

		if (rintro_input(var_40)) {                   /* loc_1DE19 */
			var_38 = 1;
			break;
		}
		if ((int16_t)(0x17 * framespersec) <= var_5D4) break;
	}

	if (shotdir) exit(0);

	/* sprite_free_wnd(wndsprite) */
	unload_resource(title3dres);                      /* seg003:6812 */
	return (int16_t)var_38;
}
