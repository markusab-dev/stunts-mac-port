/*
 * test_faithful_sim.c - Exercise the vendored restunts simulation.
 *
 * Sets up the original globals the way restunts.c does (track maps, row
 * tables, SIMD block, start position), then drives player_op() for N ticks
 * with a fixed input and reports the trajectory.
 *
 * Also reports how often the temporary sfstubs.c stand-ins were called, so
 * results can be judged for how much they depend on unported code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/render_faithful/externs.h"
#include "../src/asset/stunts_asset_loader.h"

extern void player_op(char input);
extern int16_t sfstub_hits[8];
extern void sfdata_init_track_tables(void);
extern void rfileio_set_data_dir(const char* dir);

extern struct GAMESTATE state;
extern struct GAMEINFO gameconfig;
extern struct SIMD simd_player;
extern uint8_t far* td14_elem_map_main;
extern uint8_t far* td15_terr_map_main;
extern int16_t trackrows[], terrainrows[], trackpos[], trackpos2[];
extern int16_t trackcenterpos[], trackcenterpos2[];
extern struct PLANE far* planptr;
extern struct PLANE far* current_planptr;
extern int16_t planindex;
extern uint16_t framespersec;
extern uint8_t far* trackdata19;
extern uint8_t far* trackdata23;
extern int16_t far* td10_track_check_rel;
extern int16_t far* td08_direction_related;
extern int16_t far* trackdata9;

static uint8_t s_elem[0x385], s_terr[0x385];

static const char* stub_names[8] = {
	"detect_penalty", "carState_rc_op", "car_car_speed_adjust",
	"state_op_unk", "audio_unk3", "sub_18D60",
	"car_car_coll_detect", "bto_auxiliary1"
};

int main(int argc, char** argv)
{
	const char* data_dir = argc > 1 ? argv[1] : "extracted/stunts/stunts";
	const char* track    = argc > 2 ? argv[2] : "DEFAULT";
	int frames           = argc > 3 ? atoi(argv[3]) : 40;
	char input           = argc > 4 ? (char)atoi(argv[4]) : 1; /* accelerate */

	rfileio_set_data_dir(data_dir);

	for (int i = 0; i < 30; i++) {
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
		trackpos[i] = (int16_t)((29 - i) << 10);
		trackpos2[i] = (int16_t)(i << 10);
		trackcenterpos[i] = (int16_t)(((29 - i) << 10) + 0x200);
		trackcenterpos2[i] = (int16_t)((i << 10) + 0x200);
	}
	sfdata_init_track_tables();

	/* trakdata slices track_setup() would carve out. 0xFF = "no checkpoint
	 * marker on this tile", so the lap/marker logic stays inert. */
	{
		static uint8_t td19[0x385], td23[0x385];
		static int16_t td08[0x60/2], td09[0x180/2], td10[0x120/2];
		memset(td19, 0xFF, sizeof(td19));
		trackdata19 = td19; trackdata23 = td23;
		td08_direction_related = td08; trackdata9 = td09;
		td10_track_check_rel = td10;
	}

	/* Track maps */
	{
		static uint8_t el[900], he[900];
		char p[600];
		snprintf(p, sizeof(p), "%s/%s.TRK", data_dir, track);
		if (!stunts_load_track(p, el, he)) { printf("kan ej ladda %s\n", p); return 1; }
		memset(s_elem, 0, sizeof(s_elem)); memset(s_terr, 0, sizeof(s_terr));
		memcpy(s_elem, el, 900); memcpy(s_terr, he, 900);
		td14_elem_map_main = s_elem;
		td15_terr_map_main = s_terr;

		/* Start tile: element code 0x01..0x03 is the "fini" start line. */
		int sr = 15, sc = 15;
		for (int r = 0; r < 30; r++)
			for (int c = 0; c < 30; c++)
				if (el[r * 30 + c] >= 1 && el[r * 30 + c] <= 3) { sr = r; sc = c; r = 30; break; }
		printf("startruta: kolumn %d, rad %d (elementkod 0x%02X)\n", sc, sr, el[sr * 30 + sc]);

		state.playerstate.car_posWorld1.lx = ((int32_t)sc * 1024 + 512) * 64;
		state.playerstate.car_posWorld1.lz = ((int32_t)sr * 1024 + 512) * 64;
		state.playerstate.car_posWorld1.ly = 0;
		state.playerstate.car_posWorld2 = state.playerstate.car_posWorld1;
	}

	/* Collision planes */
	{
		char p[600];
		stunts_plane_t* planes = NULL; uint16_t n = 0;
		snprintf(p, sizeof(p), "%s/GAME.PRE", data_dir);
		if (stunts_load_collision_data(p, &planes, &n)) {
			planptr = (struct PLANE*)planes;
			current_planptr = planptr;
			planindex = 0;
			printf("kollisionsplan: %u st\n", n);
		} else printf("VARNING: inga kollisionsplan\n");
	}

	/* SIMD block, copied raw as restunts.c does */
	{
		char p[600];
		snprintf(p, sizeof(p), "%s/CARCOUN.RES", data_dir);
		stunts_res_archive_t* a = stunts_asset_load_archive(p);
		if (a) {
			const stunts_sub_resource_t* sd = stunts_asset_find_resource(a, "simd");
			if (sd) {
				size_t k = sd->size < sizeof(struct SIMD) ? sd->size : sizeof(struct SIMD);
				memcpy(&simd_player, sd->data, k);
				/* setup_aero_trackdata (restunts.c:716): aerorestable is a
				 * POINTER into the trakdata aero slice, and the table is
				 * computed from the car's aero_resistance. */
				static int16_t aero[0x40];
				for (int i = 0; i < 0x40; i++)
					aero[i] = (int16_t)(((int32_t)simd_player.aero_resistance
					                     * (int32_t)i * (int32_t)i) >> 9);
				simd_player.aerorestable = aero;
				printf("bil: %d växlar, tomgång %d varv, max %d varv, höjd %d\n",
				       simd_player.num_gears, simd_player.idle_rpm,
				       simd_player.max_rpm, simd_player.car_height);
				printf("utväxlingar: ");
				for (int g = 0; g < 7; g++) printf("%u ", simd_player.gear_ratios[g]);
				printf("\nvridmoment (första 8): ");
				for (int t = 0; t < 8; t++) printf("%d ", (int)simd_player.torque_curve[t]);
				printf("\ntomgångsmoment: %d, massa: %d, luftmotstånd: %d\n",
				       (int)simd_player.idle_torque, simd_player.car_mass, simd_player.aero_resistance);
			}
		}
	}

	memcpy(gameconfig.game_playercarid, "COUN", 4);
	gameconfig.game_playertransmission = 1;
	gameconfig.game_opponenttype = 0;
	framespersec = 20;

	/* init_carstate_from_simd (restunts.c) — transcribed, not improvised.
	 * Note car_gearratioshr8: the torque term is
	 * (car_gearratioshr8 * torque) >> 4, so leaving it 0 means zero drive. */
	{
		struct CARSTATE* p = &state.playerstate;
		int32_t posX = p->car_posWorld1.lx, posY = p->car_posWorld1.ly, posZ = p->car_posWorld1.lz;
		struct VECTOR whlPos;
		p->car_posWorld1.ly = posY + 512;
		p->car_posWorld2.ly = posY;
		p->car_rotate.x = 0; p->car_rotate.y = 0; p->car_rotate.z = 0;
		p->car_36MwhlAngle = 0; p->car_pseudoGravity = 0; p->car_steeringAngle = 0;
		p->car_is_braking = 0; p->car_is_accelerating = 0;
		p->car_currpm = simd_player.idle_rpm;
		p->car_lastrpm = p->car_currpm;
		p->car_idlerpm2 = p->car_currpm;
		p->car_current_gear = 1;
		p->car_speeddiff = 0; p->car_speed = 0; p->car_speed2 = 0; p->car_lastspeed = 0;
		p->car_gearratio = simd_player.gear_ratios[1];
		p->car_gearratioshr8 = p->car_gearratio >> 8;
		p->car_knob_x = simd_player.knob_points[1].px;
		p->car_knob_x2 = p->car_knob_x;
		p->car_knob_y = simd_player.knob_points[1].py;
		p->car_knob_y2 = p->car_knob_y;
		p->car_angle_z = 0; p->car_40MfrontWhlAngle = 0;
		p->field_42 = 0; p->field_48 = 0; p->car_trackdata3_index = 0;
		p->car_sumSurfFrontWheels = 2; p->car_sumSurfRearWheels = 2;
		p->car_sumSurfAllWheels = 4;
		p->car_demandedGrip = 0; p->car_surfacegrip_sum = 1000;
		whlPos.x = (int16_t)(posX / 64);
		whlPos.y = (int16_t)(posY / 64);
		whlPos.z = (int16_t)(posZ / 64);
		for (int i = 0; i < 4; ++i) {
			p->car_surfaceWhl[i] = 1;
			p->car_rc1[i] = 0; p->car_rc2[i] = 0; p->car_rc3[i] = 0;
			p->car_rc4[i] = 0; p->car_rc5[i] = 0;
			p->car_whlWorldCrds1[i] = whlPos;
			p->car_whlWorldCrds2[i] = whlPos;
		}
		p->car_engineLimiterTimer = 0; p->car_slidingFlag = 0; p->field_C8 = 0;
		p->car_crashBmpFlag = 0; p->car_changing_gear = 0; p->car_fpsmul2 = 0;
		p->car_transmission = 1;
		p->field_CD = 0; p->field_CE = 0; p->field_CF = 1;
	}
	state.game_inputmode = 1;

	printf("\n=== kör %d bildrutor (indata=0x%02X) ===\n", frames, (unsigned char)input);
	printf("ruta |      X |      Z |   fart | varv | växel\n");
	for (int f = 1; f <= frames; f++) {
		player_op(input);
		state.game_frame++;
		if (f % (frames / 8 ? frames / 8 : 1) == 0 || f == 1)
			printf("%4d | %6ld | %6ld | %6u | %4d | %d | hjul i marken: %d | gas:%d\n", f,
			       (long)(state.playerstate.car_posWorld1.lx >> 6),
			       (long)(state.playerstate.car_posWorld1.lz >> 6),
			       state.playerstate.car_speed2,
			       state.playerstate.car_currpm,
			       state.playerstate.car_current_gear,
			       state.playerstate.car_sumSurfAllWheels,
			       state.playerstate.car_is_accelerating);
		if (f == 1) printf("       (utväxling nu: %u, varvkopplad fart: %u)\n", state.playerstate.car_gearratio, state.playerstate.car_speed);
	}

	printf("\n=== attrapper anropade ===\n");
	for (int i = 0; i < 8; i++)
		if (sfstub_hits[i]) printf("  %-24s %d gånger\n", stub_names[i], sfstub_hits[i]);
	return 0;
}
