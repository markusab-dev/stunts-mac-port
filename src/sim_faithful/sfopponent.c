/*
 * sfopponent.c - the opponent.
 *
 * Instruction-exact translations of
 *
 *   load_opponent_data   reference/restunts2/src/asm/seg004.asm 5772..6023
 *                        (`load_opponent_data_asm_ proc far` .. `endp`)
 *   opponent_op          reference/restunts2/src/asm/seg001.asm   11..725
 *                        (`opponent_op_asm_ proc far` .. `endp`)
 *
 * restunts1 carries neither body: seg004.inc / seg001.inc only declare
 * `extrn load_opponent_data:proc` and there is no C version in
 * reference/restunts/src/restunts/c/.  restunts2's Ghidra export is the only
 * disassembly of either, so every line number below refers to that tree.
 *
 * Names used by restunts2 that this port already has under restunts1 names:
 *
 *     int_atan2(z, y)        -> polarAngle(z, y)          seg012
 *     int_hypot(z, y)        -> polarRadius2D(z, y)       seg011
 *     int_hypot_3d(VECTOR*)  -> polarRadius3D(VECTOR*)    seg011
 *     vec_transform(i,m,o)   -> mat_mul_vector2(i,m,o)    seg012
 *     int_sin / int_cos      -> sin_fast / cos_fast
 *     gettlistpoint          -> sub_18D60                 seg001
 *
 * The last one is not a guess: both are the same near proc in seg001, both
 * take (trackdata3 element, VECTOR* out, field_CE, int16_t* spedOut), and
 * the port's sub_18D60 already writes oppnentSped[] through its fourth
 * argument (sfasm_port.c:1047) - which is exactly what opponent_op reads
 * back out of state.field_3F9 to pick the opponent's target speed.
 */
#include <string.h>
#include "externs.h"
#include "math.h"
#include "fileio.h"
#include "memmgr.h"

extern char oppnentSped[16];
extern char far* trackdata3;
extern char far* td17_trk_elem_ordered;
extern int16_t far* td01_track_file_cpy;
extern int16_t far* td02_penalty_related;

extern void update_car_speed(char, int16_t, struct CARSTATE*, struct SIMD*);
extern void update_grip(struct CARSTATE*, struct SIMD*, int16_t);
extern void update_player_state(struct CARSTATE*, struct SIMD*,
                                struct CARSTATE*, struct SIMD*, int16_t);

/* dseg: the opponent's two-letter initials, copied out of the "nam" text
 * resource. Six bytes is what the resource carries ("BR\0", "OP\0", ...). */
char g_opponent_initials[16];

/* Diagnostics, so a test can see the search actually ran and how long a
 * racing line it produced. Not part of the original. */
int16_t sfopp_path_len;
int32_t sfopp_path_cost;

/* ==================================================================== */
/* load_opponent_data                                                   */
/* seg004.asm 5772..6023                                                */
/* ==================================================================== */
/*
 * WHAT THE DATA TURNED OUT TO BE (measured, see the doc section):
 *
 *   "sped"  is exactly 16 bytes in all six OPP<n>.PRE - a 4x4 matrix read
 *           as oppnentSped[si_oppSpedCode + ss_surfaceType].
 *   "path"  is 186 bytes, is loaded here into var_414/var_412, and is then
 *           NEVER READ.  [ODDITY] - see the doc.
 *
 * The routing cost per tile is `sped[td17_trk_elem_ordered[si]] + 1`, i.e.
 * the SAME resource pointer indexed by the track element id, which runs
 * past the 16 bytes into whatever follows in the archive ("winn", "lose").
 * That is what the instructions say; it is reproduced, and it stays inside
 * the loaded archive for every one of the six files (the smallest tail is
 * 255 bytes after "sped", the largest element id is < 215).
 */
void load_opponent_data(void)
{
	/* The original's stack frame is 0xF30 bytes and holds four arrays.
	 * Their bases and extents are read off the indexed writes:
	 *   [bx+0xf4e0] bx=i*2  -> -2848 (local_b22), 902 words
	 *   [bx+0xfbf2] bx=i*2  -> -1038                256 words
	 *   [bx+0xfdf6] bx=i*2  ->  -522                256 words
	 *   [bx+0xf0d2] bx=i*4  -> -3886                256 dwords
	 * [DEVIATION] they are static here rather than automatic: ~3.8 KB of
	 * uninitialised stack is a poor trade for zero behavioural difference,
	 * since no path through the routine reads a slot it has not written. */
	static int16_t local_b22[902];      /* the tile stack                */
	static int16_t branch_len[256];     /* var_B28 saved per fork        */
	static int16_t branch_si[256];      /* the fork's other successor    */
	static int32_t branch_cost[256];    /* the 32-bit cost saved per fork*/

	char far* var_B2E_res;              /* var_B2E:var_B2C, the archive  */
	const uint8_t far* var_414_path;    /* var_414:var_412 - see below   */
	const uint8_t far* var_A_sped;      /* var_A, the "sped" resource    */
	int32_t var_B26;                    /* var_B26:var_B24 - best cost   */
	int16_t var_B28;                    /* tile-stack depth              */
	int32_t var_20C;                    /* var_20C:var_20E - running cost*/
	int16_t var_B2A;                    /* branch-stack depth            */
	int16_t var_410;                    /* "this tile terminates a walk" */
	int16_t var_4;                      /* td01[si], the next tile       */
	int16_t var_2;                      /* 1 = reached the finish        */
	int16_t var_6;                      /* td02[si], the fork            */
	int16_t si, di;
	int16_t ax;

	char aOpp1[8];

	/* mov al,[gameconfig.game_opponenttype] ; add al,30h ; mov [aOpp1_3],al */
	memcpy(aOpp1, "opp1", 5);
	aOpp1[3] = (char)(gameconfig.game_opponenttype + 0x30);

	var_B2E_res = (char far*)file_load_resfile(aOpp1);

	/* locate_text_res(res,"nam") -> copy_string(g_opponent_initials, ...) */
	{
		char far* nam = locate_text_res(var_B2E_res, "nam");
		if (nam) {
			size_t n = strlen((const char*)nam);
			if (n >= sizeof(g_opponent_initials))
				n = sizeof(g_opponent_initials) - 1;
			memcpy(g_opponent_initials, nam, n);
			g_opponent_initials[n] = 0;
		}
	}

	var_414_path = (const uint8_t far*)locate_shape_alt(var_B2E_res, "path");
	(void)var_414_path;   /* [ODDITY] loaded and never read - see the doc */

	var_A_sped = (const uint8_t far*)locate_shape_alt(var_B2E_res, "sped");

	/* LAB_1e1a_36a6: 16 bytes straight into the dseg table */
	for (si = 0; si < 0x10; si++)
		oppnentSped[si] = (char)var_A_sped[si];

	var_B26 = 0x000F423FL;      /* var_B26=0x423f, var_B24=0x0f */
	var_B28 = 0;
	var_20C = 0;
	var_B2A = 0;
	si = 0;

loc_36d8:
	var_410 = 0;
	var_4 = td01_track_file_cpy[si];
	if (var_4 != 0) goto loc_3702;
	var_2 = 1;                          /* the finish */
loc_36f9:
	var_410 = 1;
	goto loc_373a;

loc_3702:
	if (var_4 != -1) goto loc_3710;
	var_2 = 0;                          /* dead end */
	goto loc_36f9;

loc_3710:
	if (var_B28 == 0) goto loc_373a;
	di = 0;
	goto loc_3734;
loc_371c:
	if (local_b22[di] == si) {          /* already walked - a loop */
		var_2 = 0;
		var_410 = 1;
	}
	di++;
loc_3734:
	if (var_B28 > di) goto loc_371c;

loc_373a:
	local_b22[var_B28++] = si;
	/* mov bl, td17[si] ; sub bh,bh ; add bx,var_A ; mov al,es:[bx]
	 * sub ah,ah ; inc ax ; sub dx,dx ; add/adc into var_20E:var_20C */
	ax = (int16_t)(uint8_t)var_A_sped[(uint8_t)td17_trk_elem_ordered[si]];
	ax = (int16_t)(ax + 1);
	var_20C += (int32_t)(uint16_t)ax;
	/* `cmp var_410, dx` with dx just zeroed - a comparison against 0 */
	if (var_410 == 0) goto loc_385e;
	if (var_2 == 0) goto loc_3805;
	/* unsigned 32-bit compare of the running cost against the best */
	if ((uint32_t)var_20C >= (uint32_t)var_B26) goto loc_3805;

/* loc_3790: a new best route */
	local_b22[var_B28++] = 0;
	var_B26 = var_20C;
	di = 0;
	goto loc_37d8;
loc_37b8:
	((int16_t far*)trackdata3)[di] = local_b22[di];
	di++;
loc_37d8:
	if (var_B28 > di) goto loc_37b8;
	((int16_t far*)trackdata3)[var_B28]     = 0;
	((int16_t far*)trackdata3)[var_B28 + 1] = 1;
	sfopp_path_len = var_B28;
	sfopp_path_cost = var_B26;

loc_3805:
	if (var_B2A != 0) goto loc_3822;
	unload_resource(var_B2E_res);
	return;

loc_3822:
	var_B2A--;
	si      = branch_si[var_B2A];
	var_B28 = branch_len[var_B2A];
	var_20C = branch_cost[var_B2A];
	goto loc_36d8;

loc_385e:
	var_6 = td02_penalty_related[si];
	if (var_6 == -1) goto loc_38b4;
	branch_si[var_B2A]  = var_6;
	branch_len[var_B2A] = var_B28;
	branch_cost[var_B2A] = var_20C;
	var_B2A++;
loc_38b4:
	si = var_4;
	goto loc_36d8;
}

/* ==================================================================== */
/* opponent_op                                                          */
/* seg001.asm 11..725                                                   */
/* ==================================================================== */
/*
 * The opponent is a servo, not an AI. Every frame it
 *   - walks a waypoint cursor (car_trackdata3_index) along the racing line
 *     load_opponent_data() computed, advancing whenever it is within 200
 *     units of the current waypoint, or is aimed more than 0x100 away from
 *     it;
 *   - steers towards that waypoint, rate-limited to var_14 per frame;
 *   - accelerates or brakes towards state.field_3F9 << 8, which sub_18D60
 *     filled from oppnentSped[] for the tile it is standing on;
 *   - then runs the same update_car_speed / update_grip / update_player_state
 *     the player runs, with the two SIMD/CARSTATE pairs swapped.
 *
 * The three-way "who is where" test in the middle (var_18/var_1C/var_1A)
 * is the "Opponent Near" logic: it writes state.field_45E = 1 or 2, which
 * is the side the opponent is about to be squeezed on.
 */
void opponent_op(void)
{
	int16_t var_40, var_3E;
	struct VECTOR var_3C;          /* var_3C/var_3A/var_38 - one VECTOR  */
	int16_t var_36, var_34;
	struct VECTOR var_32;          /* var_32/var_30/var_2E               */
	int16_t var_2C;
	int16_t var_2A, var_28;
	struct VECTOR var_26;          /* var_26/var_24/var_22               */
	char    var_20;
	struct VECTOR var_1C;          /* var_1C/var_1A/var_18               */
	struct MATRIX* var_16;
	int16_t var_14, var_12, var_10;
	char    var_E;
	struct VECTOR var_C;           /* var_C/var_A/var_8                  */
	struct VECTOR var_6;           /* var_6 (+var_4/var_2, unnamed)      */
	int16_t si;
	int16_t ax;

	if (framespersec != 0x14) goto loc_001e;
	var_14 = 8;
	var_10 = 1;
	goto loc_0028;
loc_001e:
	var_14 = 0x10;
	var_10 = 2;
loc_0028:
	if (state.opponentstate.car_36MwhlAngle != 0) goto loc_0036;
	if (state.game_inputmode != 2) goto loc_003c;
loc_0036:
	var_20 = 1;
	goto loc_0040;
loc_003c:
	var_20 = 0;
loc_0040:
	/* six `sar dx,1 / rcr ax,1` chains: an arithmetic >> 6 of a long,
	 * keeping only the low word */
	var_28 = (int16_t)(state.opponentstate.car_posWorld1.lx >> 6);
	var_2A = (int16_t)(state.opponentstate.car_posWorld1.ly >> 6);
	var_36 = (int16_t)(state.opponentstate.car_posWorld1.lz >> 6);
	var_34 = (int16_t)(state.playerstate.car_posWorld1.lx >> 6);
	var_3E = (int16_t)(state.playerstate.car_posWorld1.ly >> 6);
	var_40 = (int16_t)(state.playerstate.car_posWorld1.lz >> 6);

	state.opponentstate.field_CF = 0;
	state.field_45E = 0;
	/* pushes are (1, rotate.vx, rotate.vy, rotate.vz), so the LAST push -
	 * i.e. the first cdecl argument - is rotate.z. Same call shape as
	 * state.c:132 for the player. */
	var_16 = mat_rot_zxy(state.opponentstate.car_rotate.z,
	                     state.opponentstate.car_rotate.y,
	                     state.opponentstate.car_rotate.x, 1);
	state.opponentstate.field_CF = 1;
	if (state.opponentstate.car_crashBmpFlag == 0) goto loc_00fc;
	if (state.opponentstate.car_speed2 == 0) goto loc_00f3;
	goto loc_0462;
loc_00f3:
	state.opponentstate.field_CF = 0;
	goto loc_0462;

loc_00fc:
	var_3C = state.opponentstate.car_vec_unk3;   /* three movsw */
	if (var_3C.y == -1) goto loc_0138;
	var_32.x = (int16_t)(var_3C.x - var_28);
	var_32.y = (int16_t)(var_3C.y - var_2A);
	var_32.z = (int16_t)(var_3C.z - var_36);
	si = polarRadius3D(&var_32);
	goto loc_014e;
loc_0138:
	si = polarRadius2D((int16_t)(var_3C.x - var_28),
	                   (int16_t)(var_3C.z - var_36));
loc_014e:
	if (si >= 0xC8) goto loc_01a3;

loc_0156:
	/* advance the waypoint cursor */
	{
		char ce = state.opponentstate.field_CE;
		state.opponentstate.field_CE++;
		if (sub_18D60(((int16_t far*)trackdata3)[state.opponentstate.car_trackdata3_index],
		              &state.opponentstate.car_vec_unk3,
		              (int16_t)(uint8_t)ce,
		              (int16_t*)&state.field_3F9) == 0)
			goto loc_01a3;
	}
	state.opponentstate.car_trackdata3_index++;
	if (((int16_t far*)trackdata3)[state.opponentstate.car_trackdata3_index] != 0)
		goto loc_019e;
	state.opponentstate.field_CD++;          /* a lap */
	state.opponentstate.car_trackdata3_index = 0;
loc_019e:
	state.opponentstate.field_CE = 0;

loc_01a3:
	if (state.game_inputmode != 2) goto loc_01dc;
loc_01aa:
	var_C = state.opponentstate.car_vec_unk3;
loc_01b7:
	var_26 = var_C;
	var_26.x = (int16_t)(var_26.x - var_28);
	if (var_C.y == -1) goto loc_01d3;
	goto loc_035e;
loc_01d3:
	var_26.y = 0;
	goto loc_0364;

loc_01dc:
	var_26.x = (int16_t)(var_34 - var_28);
	var_26.y = (int16_t)(var_3E - var_2A);
	var_26.z = (int16_t)(var_40 - var_36);
	mat_mul_vector2(&var_26, (struct MATRIX far*)var_16, &var_1C);
	if (var_1C.y > 0x5A) goto loc_01aa;
	if (var_1C.x >= 0) goto loc_021e;
	ax = (int16_t)(-var_1C.x);
	goto loc_0221;
loc_021e:
	ax = var_1C.x;
loc_0221:
	if (ax > 0xB4) goto loc_01aa;
	if (var_1C.z > 0x258) goto loc_01aa;
	if (var_1C.z < (int16_t)0xFF4C) goto loc_01aa;

	var_26.x = (int16_t)(var_34 - state.opponentstate.car_vec_unk3.x);
	if (state.opponentstate.car_vec_unk3.y != -1) goto loc_0252;
	var_26.y = 0;
	goto loc_025c;
loc_0252:
	var_26.y = (int16_t)(var_3E - state.opponentstate.car_vec_unk3.y);
loc_025c:
	var_26.z = (int16_t)(var_40 - state.opponentstate.car_vec_unk3.z);
	mat_mul_vector2(&var_26, (struct MATRIX far*)var_16, &var_6);
	if (var_6.x >= 0) goto loc_02ee;

	/* the player is to one side: aim at the midpoint of the waypoint and
	 * car_vec_unk5 (a `cwd`-widened add then `sar/rcr` - an average) */
	var_C.x = (int16_t)(((int32_t)state.opponentstate.car_vec_unk3.x +
	                     (int32_t)state.opponentstate.car_vec_unk5.x) >> 1);
	if (state.opponentstate.car_vec_unk3.y != -1) goto loc_02a4;
	var_C.y = -1;
	goto loc_02bb;
loc_02a4:
	var_C.y = (int16_t)(((int32_t)state.opponentstate.car_vec_unk3.y +
	                     (int32_t)state.opponentstate.car_vec_unk5.y) >> 1);
loc_02bb:
	var_C.z = (int16_t)(((int32_t)state.opponentstate.car_vec_unk3.z +
	                     (int32_t)state.opponentstate.car_vec_unk5.z) >> 1);
	if (var_1C.z <= (int16_t)-0x4E) goto loc_01b7;
	if (state.playerstate.car_crashBmpFlag != 0) goto loc_01b7;
	state.field_45E = 2;
	goto loc_01b7;

loc_02ee:
	var_C.x = (int16_t)(((int32_t)state.opponentstate.car_vec_unk3.x +
	                     (int32_t)state.opponentstate.car_vec_unk4.x) >> 1);
	if (state.opponentstate.car_vec_unk3.y != -1) goto loc_0314;
	var_C.y = -1;
	goto loc_032b;
loc_0314:
	var_C.y = (int16_t)(((int32_t)state.opponentstate.car_vec_unk3.y +
	                     (int32_t)state.opponentstate.car_vec_unk4.y) >> 1);
loc_032b:
	var_C.z = (int16_t)(((int32_t)state.opponentstate.car_vec_unk3.z +
	                     (int32_t)state.opponentstate.car_vec_unk4.z) >> 1);
	if (var_1C.z <= (int16_t)-0x4E) goto loc_01b7;
	if (state.playerstate.car_crashBmpFlag != 0) goto loc_01b7;
	state.field_45E = 1;
	goto loc_01b7;

loc_035e:
	var_26.y = (int16_t)(var_26.y - var_2A);
loc_0364:
	var_26.z = (int16_t)(var_26.z - var_36);
	mat_mul_vector2(&var_26, (struct MATRIX far*)var_16, &var_3C);
	var_2C = polarAngle(var_3C.x, var_3C.z);
	if (state.opponentstate.car_slidingFlag != 0) goto loc_03f3;
	ax = var_2C;
	if (ax >= 0) goto loc_039e;
	ax = (int16_t)(-ax);
	goto loc_03a1;
loc_039e:
	ax = var_2C;
loc_03a1:
	if (ax <= 0x100) goto loc_03f3;
	{
		char ce = state.opponentstate.field_CE;
		state.opponentstate.field_CE++;
		if (sub_18D60(((int16_t far*)trackdata3)[state.opponentstate.car_trackdata3_index],
		              &state.opponentstate.car_vec_unk3,
		              (int16_t)(uint8_t)ce,
		              (int16_t*)&state.field_3F9) == 0)
			goto loc_03f3;
	}
	state.opponentstate.car_trackdata3_index++;
	if (((int16_t far*)trackdata3)[state.opponentstate.car_trackdata3_index] != 0)
		goto loc_03ee;
	state.opponentstate.field_CD++;
	state.opponentstate.car_trackdata3_index = 0;
loc_03ee:
	state.opponentstate.field_CE = 0;

loc_03f3:
	if (var_2C <= 0x41) goto loc_040e;
	if (var_20 != 0) goto loc_0406;
loc_03ff:
	var_20 = 1;
	goto loc_0156;
loc_0406:
	var_2C = 0x41;
	goto loc_041f;
loc_040e:
	if (var_2C >= -0x41) goto loc_041f;
	if (var_20 == 0) goto loc_03ff;
	var_2C = (int16_t)0xFFBF;
loc_041f:
	if (state.opponentstate.car_sumSurfFrontWheels != 0) goto loc_042b;
	var_2C = 0;
loc_042b:
	si = (int16_t)(var_2C - state.opponentstate.car_steeringAngle);
	if (si >= 0) goto loc_043a;
	ax = (int16_t)(-si);
	goto loc_043c;
loc_043a:
	ax = si;
loc_043c:
	if (ax <= var_14) goto loc_045c;
	if (var_2C >= state.opponentstate.car_steeringAngle) goto loc_0452;
	state.opponentstate.car_steeringAngle -= var_14;
	goto loc_0462;
loc_0452:
	state.opponentstate.car_steeringAngle += var_14;
	goto loc_0462;
loc_045c:
	state.opponentstate.car_steeringAngle = var_2C;

loc_0462:
	var_E = 0;
	if (state.opponentstate.car_sumSurfRearWheels == 0) goto loc_04ea;
	if (state.opponentstate.car_crashBmpFlag != 0) goto loc_04e6;
	if (state.opponentstate.car_36MwhlAngle == 0) goto loc_04a6;
	/* spinning: bleed speed off by var_10 << 9 per frame */
	if ((uint16_t)(var_10 << 9) <= state.opponentstate.car_speed2) goto loc_0498;
	state.opponentstate.car_speed2 = 0;
	state.opponentstate.car_36MwhlAngle = 0;
	goto loc_04ea;
loc_0498:
	state.opponentstate.car_speed2 -= (uint16_t)(var_10 << 9);
	goto loc_04ea;
loc_04a6:
	if (state.opponentstate.car_demandedGrip > state.opponentstate.car_surfacegrip_sum)
		goto loc_04e6;
	if (state.game_inputmode != 2) goto loc_04be;
	var_12 = 0x4000;
	goto loc_04c7;
loc_04be:
	/* mov ah,[state.field_3F9] ; sub al,al  -> the sped byte << 8 */
	var_12 = (int16_t)((uint16_t)(uint8_t)state.field_3F9 << 8);
loc_04c7:
	/* cmp ax, car_speed ; jbe loc_04da   - UNSIGNED */
	if ((uint16_t)(var_12 - 0x100) > state.opponentstate.car_speed) {
		var_E = 1;                       /* below target: accelerate */
		goto loc_04ea;
	}
/* loc_04da: `add ah, 3` is an 8-BIT add into the high byte, so it wraps
 * inside that byte instead of carrying out of the word. */
	ax = (int16_t)(((uint16_t)var_12 & 0x00FF) |
	               (uint16_t)(((((uint16_t)var_12 >> 8) + 3) & 0xFF) << 8));
	/* cmp ax, car_speed ; jnc loc_04ea   - UNSIGNED >= */
	if ((uint16_t)ax >= state.opponentstate.car_speed) goto loc_04ea;
loc_04e6:
	var_E = 2;                               /* over target: brake */

loc_04ea:
	update_car_speed(var_E, 1, &state.opponentstate, &simd_opponent);
	update_grip(&state.opponentstate, &simd_opponent, 0);
	update_player_state(&state.opponentstate, &simd_opponent,
	                    &state.playerstate, &simd_player, 1);
	if (state.opponentstate.car_crashBmpFlag != 0) goto loc_05c7;

	var_26 = state.opponentstate.car_vec_unk3;
	var_26.x = (int16_t)(var_26.x - (int16_t)(state.opponentstate.car_posWorld1.lx >> 6));
	var_26.y = (int16_t)(var_26.y - (int16_t)(state.opponentstate.car_posWorld1.ly >> 6));
	var_26.z = (int16_t)(var_26.z - (int16_t)(state.opponentstate.car_posWorld1.lz >> 6));
	/* pushes are (1, rotate.vx, rotate.vy, rotate.vz), so the LAST push -
	 * i.e. the first cdecl argument - is rotate.z. Same call shape as
	 * state.c:132 for the player. */
	var_16 = mat_rot_zxy(state.opponentstate.car_rotate.z,
	                     state.opponentstate.car_rotate.y,
	                     state.opponentstate.car_rotate.x, 1);
	mat_mul_vector2(&var_26, (struct MATRIX far*)var_16, &var_3C);
	/* `and ah,3` after the atan2 - the result is kept in 0..0x3FF */
	state.opponentstate.field_48 =
		(int16_t)(polarAngle((int16_t)(-var_3C.x), var_3C.z) & 0x03FF);

loc_05c7:
	if (state.opponentstate.field_CD == 0) goto loc_0656;
	/* crossed the line: only a finish if the car is past the start plane */
	si = multiply_and_scale(
	         cos_fast((uint16_t)track_angle),
	         (int16_t)(trackcenterpos[(int)startrow2] -
	                   (int16_t)(state.opponentstate.car_posWorld1.lz >> 6)));
	si = (int16_t)(si + multiply_and_scale(
	         sin_fast((uint16_t)track_angle),
	         (int16_t)(trackcenterpos2[(int)startcol2] -
	                   (int16_t)(state.opponentstate.car_posWorld1.lx >> 6))));
	if (si >= 0) goto loc_0656;
	update_crash_state(3, 1);
loc_0656:
	;
}

/* ==================================================================== */
/* car_car_coll_detect_maybe                                            */
/* seg001.asm 8102..8499                                                */
/* ==================================================================== */
/*
 * Despite the name this is a general oriented-box against oriented-box
 * test, and stateply.c calls it four times: once car-against-car and three
 * times against the fixed boxes unk_3BD5A / unk_3BD62 / unk_3BD6A. That is
 * why the stub was being hit 208 times in a single-car replay.
 *
 * `points` is a SIMD collide_points[2] pair used as half-extents:
 *     [0].px  x half-extent      [0].py  height
 *     [1].px  z half-extent      [1].py  broad-phase radius
 * `vec` is a VECTOR[2]: [0] world position (>>6 units), [1] rotation.
 *
 * The two sign tables pick the four corners of the box in the xz plane.
 * dseg.asm 559/566:
 *     word_3BE04  dw 1, 0, 0, 1     - negate the x half-extent
 *     word_3BE0C  dw 0, 0, 1, 1     - negate the z half-extent
 * The `[bx+0x69c]` in the first loop is word_3BE0C addressed by its raw
 * dseg offset; word_3BE04 sits eight bytes below it, which is exactly the
 * pair relationship the other three loops spell out by name.
 */
static const int16_t word_3BE04[4] = { 1, 0, 0, 1 };
static const int16_t word_3BE0C[4] = { 0, 0, 1, 1 };

int16_t car_car_coll_detect_maybe(struct POINT2D* points1, struct VECTOR* vec1,
                                  struct POINT2D* points2, struct VECTOR* vec2)
{
	struct VECTOR var_2A;          /* var_2A/var_28/var_26 - the output   */
	struct VECTOR var_24[4];       /* byte ptr -36: the four corners      */
	struct VECTOR var_C;           /* var_C/var_A/var_8 - the input       */
	struct MATRIX* var_4;
	char var_6;
	int16_t si, di;
	int16_t ax;
	struct VECTOR* bx;

	si = (int16_t)(points1[1].py + points2[1].py);

	/* three axis-aligned rejects, each written as "max minus min" */
	ax = vec2[0].x;
	bx = vec1;
	if (vec1[0].x >= ax) { ax = vec1[0].x; bx = vec2; }
	if ((int16_t)(ax - bx[0].x) > si) goto loc_49c2;

	ax = vec2[0].z;
	bx = vec1;
	if (vec1[0].z >= ax) { ax = vec1[0].z; bx = vec2; }
	if ((int16_t)(ax - bx[0].z) > si) goto loc_49c2;

	ax = vec2[0].y;
	bx = vec1;
	if (vec1[0].y >= ax) { ax = vec1[0].y; bx = vec2; }
	if ((int16_t)(ax - bx[0].y) <= si) goto loc_49ca;
loc_49c2:
	return 0;

loc_49ca:
	var_2A.x = (int16_t)(vec1[0].x - vec2[0].x);
	var_2A.y = (int16_t)(vec1[0].y - vec2[0].y);
	var_2A.z = (int16_t)(vec1[0].z - vec2[0].z);
	/* `cmp ax, si ; ja` - UNSIGNED, so a radius above 0x7FFF also rejects */
	if ((uint16_t)polarRadius3D(&var_2A) > (uint16_t)si) goto loc_49c2;

	/* --- car 1's four corners, in world space --- */
	var_4 = mat_rot_zxy((int16_t)(-vec1[1].x), (int16_t)(-vec1[1].y),
	                    (int16_t)(-vec1[1].z), 0);
	for (var_6 = 0; var_6 < 4; var_6++) {
		var_C.x = word_3BE04[(int)var_6] != 0 ? (int16_t)(-points1[0].px)
		                                      : points1[0].px;
		var_C.y = 0;
		var_C.z = word_3BE0C[(int)var_6] == 0 ? points1[1].px
		                                      : (int16_t)(-points1[1].px);
		mat_mul_vector2(&var_C, (struct MATRIX far*)var_4, &var_2A);
		var_2A.x = (int16_t)(var_2A.x + vec1[0].x);
		var_2A.y = (int16_t)(var_2A.y + vec1[0].y);
		var_2A.z = (int16_t)(var_2A.z + vec1[0].z);
		var_24[(int)var_6] = var_2A;
	}

	/* --- are any of them inside car 2's box? --- */
	var_4 = mat_rot_zxy(vec2[1].x, vec2[1].y, vec2[1].z, 1);
	for (var_6 = 0; var_6 < 4; var_6++) {
		var_C.x = (int16_t)(vec2[0].x - var_24[(int)var_6].x);
		var_C.y = (int16_t)(vec2[0].y - var_24[(int)var_6].y);
		var_C.z = (int16_t)(vec2[0].z - var_24[(int)var_6].z);
		mat_mul_vector2(&var_C, (struct MATRIX far*)var_4, &var_2A);
		if (var_2A.y < 0) continue;
		if (points2[0].py < var_2A.y) continue;
		di = points2[0].px;
		if (var_2A.x < (int16_t)(-di)) continue;
		if (var_2A.x > di) continue;
		if ((int16_t)(-points2[1].px) > var_2A.z) continue;
		if (points2[1].px < var_2A.z) continue;
		return 1;
	}

	/* --- and the mirror image: car 2's corners against car 1's box --- */
	var_4 = mat_rot_zxy((int16_t)(-vec2[1].x), (int16_t)(-vec2[1].y),
	                    (int16_t)(-vec2[1].z), 0);
	for (var_6 = 0; var_6 < 4; var_6++) {
		var_C.x = word_3BE04[(int)var_6] != 0 ? (int16_t)(-points2[0].px)
		                                      : points2[0].px;
		var_C.y = 0;
		var_C.z = word_3BE0C[(int)var_6] == 0 ? points2[1].px
		                                      : (int16_t)(-points2[1].px);
		mat_mul_vector2(&var_C, (struct MATRIX far*)var_4, &var_2A);
		var_2A.x = (int16_t)(var_2A.x + vec2[0].x);
		var_2A.y = (int16_t)(var_2A.y + vec2[0].y);
		var_2A.z = (int16_t)(var_2A.z + vec2[0].z);
		var_24[(int)var_6] = var_2A;
	}

	var_4 = mat_rot_zxy(vec1[1].x, vec1[1].y, vec1[1].z, 1);
	for (var_6 = 0; var_6 < 4; var_6++) {
		var_C.x = (int16_t)(vec1[0].x - var_24[(int)var_6].x);
		var_C.y = (int16_t)(vec1[0].y - var_24[(int)var_6].y);
		var_C.z = (int16_t)(vec1[0].z - var_24[(int)var_6].z);
		mat_mul_vector2(&var_C, (struct MATRIX far*)var_4, &var_2A);
		if (var_2A.y < 0) continue;
		if (points1[0].py < var_2A.y) continue;
		di = points1[0].px;
		if (var_2A.x < (int16_t)(-di)) continue;
		if (var_2A.x > di) continue;
		if ((int16_t)(-points1[1].px) > var_2A.z) continue;
		if (points1[1].px < var_2A.z) continue;
		return 1;
	}
	goto loc_49c2;
}

/* ==================================================================== */
/* car_car_speed_adjust_maybe                                           */
/* seg001.asm 6690..6855                                                */
/* ==================================================================== */
/*
 * Runs after car_car_coll_detect_maybe says the two cars overlap. It
 * projects both cars' speeds onto their own headings, takes the length of
 * the difference as the impact speed, sheds 0x300*impact/4 from the
 * player's speed2, spins both cars by their heading difference, and
 * reports "hard enough to count" when the impact exceeds 30.
 *
 * CARSTATE offsets used, derived from struct CARSTATE in externs.h:
 *   0x18 car_rotate.x   0x2a car_speed   0x2c car_speed2
 *   0x36 car_36MwhlAngle (0x37 is its HIGH BYTE)   0xc8 field_C8
 */
int16_t car_car_speed_adjust_maybe(struct CARSTATE* pState, struct CARSTATE* oState)
{
	int16_t var_18, var_16, var_14, var_12, var_10, var_E, var_C, var_A;
	int16_t var_8, var_6, var_4, var_2;

	pState->field_C8 = 1;
	oState->field_C8 = 1;
	var_6 = (int16_t)pState->car_speed2;
	var_C = (int16_t)oState->car_speed2;
	var_2 = pState->car_rotate.x;
	var_4 = oState->car_rotate.x;

	/* `shr ax, cl` with cl=8 - a LOGICAL shift, car_speed2 is unsigned */
	var_10 = multiply_and_scale((int16_t)((uint16_t)var_6 >> 8),
	                            sin_fast((uint16_t)var_2));
	var_14 = multiply_and_scale((int16_t)((uint16_t)var_C >> 8),
	                            sin_fast((uint16_t)var_4));
	var_12 = multiply_and_scale((int16_t)((uint16_t)var_6 >> 8),
	                            cos_fast((uint16_t)var_2));
	var_16 = multiply_and_scale((int16_t)((uint16_t)var_C >> 8),
	                            cos_fast((uint16_t)var_4));
	var_A = polarRadius2D((int16_t)(var_14 - var_10),
	                      (int16_t)(var_16 - var_12));
	if (var_A < 10) var_A = 10;

	/* Two dead stores the original still performs; kept so the translation
	 * stays line-for-line. Neither var_8 nor var_E is read again. */
	var_8 = (int16_t)(((uint16_t)(var_2 - var_4)) & 0x03FF);
	var_E = (int16_t)((uint16_t)((uint16_t)var_A & 0xFF) << 8);
	(void)var_8; (void)var_E;

	/* mov ax,300h ; imul var_A ; sar ax,1 ; sar ax,1 - only AX survives */
	var_18 = (int16_t)((int16_t)(uint16_t)(0x300 * (uint16_t)var_A) >> 2);
	if (pState->car_speed2 < (uint16_t)var_18)
		pState->car_speed2 = 0;
	else
		pState->car_speed2 -= (uint16_t)var_18;

	pState->car_36MwhlAngle = (int16_t)(var_4 - var_2);
	if (pState->car_36MwhlAngle >= 0x200)
		pState->car_36MwhlAngle -= 0x400;
	/* `add byte ptr [bx+37h], 4` - an 8-bit add into the HIGH byte, i.e.
	 * +0x400 that cannot carry out of the word */
	if (pState->car_36MwhlAngle <= (int16_t)0xFE00)
		pState->car_36MwhlAngle = (int16_t)
			(((uint16_t)pState->car_36MwhlAngle & 0x00FF) |
			 ((uint16_t)(((((uint16_t)pState->car_36MwhlAngle >> 8) + 4) & 0xFF) << 8)));

	oState->car_36MwhlAngle = (int16_t)(var_2 - var_4);
	if (oState->car_36MwhlAngle >= 0x200)
		oState->car_36MwhlAngle -= 0x400;
	if (oState->car_36MwhlAngle <= (int16_t)0xFE00)
		oState->car_36MwhlAngle = (int16_t)
			(((uint16_t)oState->car_36MwhlAngle & 0x00FF) |
			 ((uint16_t)(((((uint16_t)oState->car_36MwhlAngle >> 8) + 4) & 0xFF) << 8)));

	pState->car_speed = pState->car_speed2;
	oState->car_speed = oState->car_speed2;

	if (var_A > 0x1E) return 1;
	return 0;
}
