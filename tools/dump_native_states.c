/*
 * dump_native_states.c - Run the vendored simulation over a replay and dump
 * the same 1120-byte GAMESTATE per frame that the oracle produces, so the two
 * can be diffed record for record.
 *
 * The replay file carries everything: a 26-byte GAMEINFO header, then the
 * 0x385-byte element map, then the 0x385-byte terrain map, then one input
 * byte per frame. That is exactly how init_trackdata() carves the trakdata
 * chunk (td13 -> td14 -> td15 -> td16), which is why file_load_replay() can
 * simply read the whole file into td13_rpl_header.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/render_faithful/externs.h"
#include "../src/asset/stunts_asset_loader.h"

extern void player_op(char input);
extern struct GAMESTATE far* cvxptr;
extern int16_t word_45A00;
extern char far* td16_rpl_buffer;
extern uint8_t g_kevinrandom_seed[];
extern void sfdata_init_track_tables(void);
extern int16_t sfstub_hits[8];
extern int dbg_rc_probe;
extern int16_t dbg_model, dbg_terrain, dbg_plan, dbg_col, dbg_row;
extern int16_t dbg_bto[8][8];
extern int dbg_bto_n;
extern int16_t terrainHeight;
extern int16_t wallindex;
extern int16_t far* wallptr;
extern char current_surf_type;
extern void rfileio_set_data_dir(const char* dir);

extern struct GAMESTATE state;
extern struct GAMEINFO gameconfig;
extern struct SIMD simd_player;
extern uint8_t far *td14_elem_map_main, *td15_terr_map_main;
extern int16_t trackrows[], terrainrows[], trackpos[], trackpos2[];
extern int16_t trackcenterpos[], trackcenterpos2[];
extern struct PLANE far *planptr, *current_planptr;
extern int16_t planindex;
extern uint16_t framespersec;
extern uint8_t far *trackdata19, *trackdata23;
extern int16_t far *td10_track_check_rel, *td08_direction_related, *trackdata9;
extern int16_t far *td01_track_file_cpy, *td02_penalty_related;
extern char far *td17_trk_elem_ordered, *trackdata18;   /* externs.h: char* */
extern char far *td21_col_from_path, *td22_row_from_path;
extern char far *trackdata3;
extern char startcol2, startrow2, hillFlag;
extern int16_t track_angle;
extern int16_t hillHeightConsts[];
extern int16_t sin_fast(uint16_t), cos_fast(uint16_t);
extern int16_t multiply_and_scale(int16_t, int16_t);

#define RPL_HDR   0x1A
#define MAP_SIZE  0x385

static uint8_t s_elem[MAP_SIZE], s_terr[MAP_SIZE];
static uint8_t g_trakblob[0x6BF3];
static int16_t s_aero[0x40];

/* init_carstate_from_simd (restunts.c), transcribed. */
static void init_carstate(struct CARSTATE* p, struct SIMD* simd, char transmission,
                          int32_t posX, int32_t posY, int32_t posZ, int16_t track_angle)
{
	struct VECTOR w;
	p->car_posWorld1.lx = posX; p->car_posWorld2.lx = posX;
	p->car_posWorld1.ly = posY + 512; p->car_posWorld2.ly = posY;
	p->car_posWorld1.lz = posZ; p->car_posWorld2.lz = posZ;
	p->car_rotate.x = track_angle; p->car_rotate.y = 0; p->car_rotate.z = 0;
	p->car_36MwhlAngle = 0; p->car_pseudoGravity = 0; p->car_steeringAngle = 0;
	p->car_is_braking = 0; p->car_is_accelerating = 0;
	p->car_currpm = simd->idle_rpm; p->car_lastrpm = p->car_currpm;
	p->car_idlerpm2 = p->car_currpm; p->car_current_gear = 1;
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
	w.x = (int16_t)(posX / 64); w.y = (int16_t)(posY / 64); w.z = (int16_t)(posZ / 64);
	for (int i = 0; i < 4; ++i) {
		p->car_surfaceWhl[i] = 1;
		p->car_rc1[i] = p->car_rc2[i] = p->car_rc3[i] = 0;
		p->car_rc4[i] = p->car_rc5[i] = 0;
		p->car_whlWorldCrds1[i] = w; p->car_whlWorldCrds2[i] = w;
	}
	p->car_engineLimiterTimer = 0; p->car_slidingFlag = 0; p->field_C8 = 0;
	p->car_crashBmpFlag = 0; p->car_changing_gear = 0; p->car_fpsmul2 = 0;
	p->car_transmission = transmission;
	p->field_CD = 0; p->field_CE = 0; p->field_CF = 1;
}

int main(int argc, char** argv)
{
	if (argc < 4) {
		printf("usage: %s <data_dir> <replay.rpl> <out.bin> [seed.bin seed_frame]\n", argv[0]);
		return 1;
	}
	/* Optional: seed the whole GAMESTATE from an oracle record, so the
	 * per-frame physics can be compared without the (unported) track_setup /
	 * init_game_state path affecting the starting conditions. */
	const char* seed_path = (argc > 4 && argv[4][0]) ? argv[4] : NULL;
	int seed_frame = argc > 5 ? atoi(argv[5]) : 1;
	/* Optional: the real trakdata chunk, dumped by the oracle straight after
	 * track_setup() ran. Lets the simulation use the genuine track tables
	 * without track_setup being ported (task #9), so the remaining divergence
	 * can be attributed. */
	const char* trak_path = argc > 6 ? argv[6] : NULL;
	setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: keep probe output on a crash */
	const char* data_dir = argv[1];
	rfileio_set_data_dir(data_dir);

	uint8_t* rpl; long rpl_len;
	{
		FILE* f = fopen(argv[2], "rb");
		if (!f) { perror(argv[2]); return 1; }
		fseek(f, 0, SEEK_END); rpl_len = ftell(f); fseek(f, 0, SEEK_SET);
		rpl = malloc(rpl_len);
		if (fread(rpl, 1, rpl_len, f) != (size_t)rpl_len) { printf("kort läsning\n"); return 1; }
		fclose(f);
	}

	memcpy(&gameconfig, rpl, sizeof(struct GAMEINFO));
	uint16_t frames = gameconfig.game_recordedframes;
	char car[5] = {0}; memcpy(car, gameconfig.game_playercarid, 4);
	char trk[10] = {0}; memcpy(trk, gameconfig.game_trackname, 9);
	long in_off = RPL_HDR + 2L * MAP_SIZE;
	printf("inspelning: bil %s, bana %s, %u rutor, indata från offset %ld\n",
	       car, trk, frames, in_off);
	if (in_off + frames != rpl_len) {
		printf("VARNING: filstorlek %ld != %ld\n", rpl_len, in_off + frames);
	}

	for (int i = 0; i < 30; i++) {
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
		trackpos[i] = (int16_t)((29 - i) << 10);
		trackpos2[i] = (int16_t)(i << 10);
		trackcenterpos[i] = (int16_t)(((29 - i) << 10) + 0x200);
		trackcenterpos2[i] = (int16_t)((i << 10) + 0x200);
		/* The two tables the original's init_row_tables also fills
		 * (restunts.c:244-245). Missing them left elem_zCenter = 0 in
		 * build_track_object, which broke every plane with index >= 4 -
		 * i.e. all slopes, ramps and banked corners. Flat ground never
		 * noticed: planes 0-3 short-circuit before touching the centre. */
		terrainpos[i] = (int16_t)(i << 10);
		terraincenterpos[i] = (int16_t)((i << 10) + 0x200);
	}
	sfdata_init_track_tables();
	sfdata_init_trackdata();

	/* Track maps come from the replay itself, exactly as file_load_replay does. */
	memcpy(td14_elem_map_main, rpl + RPL_HDR, MAP_SIZE);
	memcpy(td15_terr_map_main, rpl + RPL_HDR + MAP_SIZE, MAP_SIZE);

	/* Our own track_setup builds every path table and derives the start tile;
	 * the old "search for element 01..03" guess is gone. A trakdata file can
	 * still be passed to override the result for A/B comparison. */
	{
		int16_t err = track_setup();
		printf("track_setup: felkod %d, startruta (%d,%d), vinkel %d, "
		       "kulle %d, %d banrutor\n",
		       err, startcol2, startrow2, track_angle, hillFlag,
		       track_pieces_counter);
		/* Warn but continue: the original game also runs a track whose path
		 * walk failed (its tables are simply left short), and comparisons on
		 * deliberately malformed test tracks are useful. */
		if (err != 0) printf("VARNING: banan är ogiltig, tabellerna är ofullständiga\n");
	}

	{
		char p[600]; stunts_plane_t* planes = NULL; uint16_t n = 0;
		snprintf(p, sizeof(p), "%s/GAME.PRE", data_dir);
		if (stunts_load_collision_data(p, &planes, &n)) {
			planptr = (struct PLANE*)planes; current_planptr = planptr; planindex = 0;
		} else printf("VARNING: kollisionsplan saknas\n");
		{
			/* The wall table, loaded exactly where the original does
			 * (restunts.c:799 locate_shape_alt(gameresptr, "wall")).
			 * Left NULL it segfaults the first time build_track_object
			 * meets a walled tile (hill edges, banked roads). */
			stunts_res_archive_t* wa = stunts_asset_load_archive(p);
			const stunts_sub_resource_t* w =
				wa ? stunts_asset_find_resource(wa, "wall") : NULL;
			if (w && w->data) {
				int16_t* copy = malloc(w->size);
				memcpy(copy, w->data, w->size);
				wallptr = copy;
			} else printf("VARNING: väggdata saknas\n");
		}
	}
	{
		char p[600];
		snprintf(p, sizeof(p), "%s/CAR%.4s.RES", data_dir, car);
		stunts_res_archive_t* a = stunts_asset_load_archive(p);
		if (!a) { printf("kan ej ladda %s\n", p); return 1; }
		const stunts_sub_resource_t* sd = stunts_asset_find_resource(a, "simd");
		size_t k = sd->size < sizeof(struct SIMD) ? sd->size : sizeof(struct SIMD);
		memcpy(&simd_player, sd->data, k);
		for (int i = 0; i < 0x40; i++)
			s_aero[i] = (int16_t)(((int32_t)simd_player.aero_resistance * i * i) >> 9);
		simd_player.aerorestable = s_aero;
	}
	framespersec = 20;

	/* --- real trakdata from the oracle -------------------------------------
	 * Layout is exactly init_trackdata()'s carve-up of the 0x6BF3 chunk.
	 * The file starts with an 8-byte snapshot of the dseg bytes at startcol2:
	 *   [0]=startcol2 [1]=hillFlag [2..3]=word_4499C [4]=startrow2 ...        */
	int have_trak = 0;
	if (trak_path) {
		FILE* tf = fopen(trak_path, "rb");
		if (!tf) { perror(trak_path); return 1; }
		static uint8_t blob[10 + 0x6BF3];
		size_t got = fread(blob, 1, sizeof(blob), tf);
		fclose(tf);
		if (got != sizeof(blob)) { printf("TRAKDATA fel storlek: %zu\n", got); return 1; }
		startcol2 = (char)blob[0];
		hillFlag  = (char)blob[1];
		startrow2 = (char)blob[4];
		track_angle = (int16_t)(blob[8] | (blob[9] << 8));
		uint8_t* b = blob + 10;
		td01_track_file_cpy    = (int16_t*)(b + 0x0000);
		td02_penalty_related   = (int16_t*)(b + 0x070A);
		trackdata3             = (char*)(b + 0x0E14);
		td08_direction_related = (int16_t*)(b + 0x171E);
		trackdata9             = (int16_t*)(b + 0x177E);
		td10_track_check_rel   = (int16_t*)(b + 0x18FE);
		td14_elem_map_main     = b + 0x1C94;
		td15_terr_map_main     = b + 0x2019;
		td17_trk_elem_ordered  = (char*)(b + 0x527E);
		trackdata18            = (char*)(b + 0x5603);
		trackdata19            = b + 0x5988;
		td21_col_from_path     = (char*)(b + 0x64B9);
		td22_row_from_path     = (char*)(b + 0x683E);
		trackdata23            = b + 0x6BC3;
		memcpy(g_trakblob, b, sizeof(g_trakblob));
		have_trak = 1;
		printf("trakdata inläst: startcol2=%d startrow2=%d hillFlag=%d track_angle=%d\n",
		       startcol2, startrow2, hillFlag, track_angle);
	}

	/* --- track_setup verification -------------------------------------
	 * Run our own track_setup() over a private trakdata block and compare
	 * every table it fills against the oracle's TRAKDATA.BIN, which the real
	 * game produced from the same maps. Enabled with STUNTS_TS_VERIFY=1. */
	if (getenv("STUNTS_TS_VERIFY") && have_trak) {
		extern int16_t track_setup(void);
		static uint8_t ours[0x6BF3];
		uint8_t* o = ours;
		/* Keep the oracle's copies to compare against. */
		static uint8_t want[0x6BF3];
		memcpy(want, g_trakblob, sizeof(want));
		/* Poison everything, then restore ONLY the inputs track_setup reads:
		 * the element and terrain maps. Anything that still matches the oracle
		 * afterwards was genuinely written by our track_setup. */
		/* STUNTS_TS_VERIFY=1 zero-fills (the state a fresh DOS allocation is
		 * in, so a clean run must match the oracle byte for byte);
		 * =2 poisons instead, which additionally proves our writes stop at
		 * exactly the same index the original's did. */
		int poison = atoi(getenv("STUNTS_TS_VERIFY")) == 2;
		memset(ours, poison ? 0xAA : 0x00, sizeof(ours));
		memcpy(ours + 0x1C94, g_trakblob + 0x1C94, 0x385);   /* td14 elements */
		memcpy(ours + 0x2019, g_trakblob + 0x2019, 0x385);   /* td15 terrain  */
		td01_track_file_cpy    = (int16_t*)(o + 0x0000);
		td02_penalty_related   = (int16_t*)(o + 0x070A);
		trackdata3             = (char*)(o + 0x0E14);
		trackdata6             = (char*)(o + 0x161E);
		trackdata7             = (char*)(o + 0x169E);
		td08_direction_related = (int16_t*)(o + 0x171E);
		trackdata9             = (int16_t*)(o + 0x177E);
		td10_track_check_rel   = (int16_t*)(o + 0x18FE);
		td14_elem_map_main     = o + 0x1C94;
		td15_terr_map_main     = o + 0x2019;
		td17_trk_elem_ordered  = (char*)(o + 0x527E);
		trackdata18            = (char*)(o + 0x5603);
		trackdata19            = o + 0x5988;
		td21_col_from_path     = (char*)(o + 0x64B9);
		td22_row_from_path     = (char*)(o + 0x683E);
		trackdata23            = o + 0x6BC3;
		char sc = startcol2, sr = startrow2, hf = hillFlag;
		int16_t ta = track_angle;
		startcol2 = startrow2 = hillFlag = 0; track_angle = 0;
		int16_t err = track_setup();
		printf("\ntrack_setup returnerade %d (0 = OK)\n", err);
		printf("  startcol2 %d/%d  startrow2 %d/%d  hillFlag %d/%d  angle %d/%d\n",
		       startcol2, sc, startrow2, sr, hillFlag, hf, track_angle, ta);
		struct { const char* n; long off, len; } T[] = {
			{"td01_track_file_cpy",  0x0000, 0x70A}, {"td02_penalty_related", 0x070A, 0x70A},
			{"trackdata6",           0x161E, 0x80},  {"trackdata7",           0x169E, 0x80},
			{"td08_direction",       0x171E, 0x60},  {"trackdata9",           0x177E, 0x180},
			{"td10_track_check",     0x18FE, 0x120}, {"td17_elem_ordered",    0x527E, 0x385},
			{"trackdata18",          0x5603, 0x385}, {"trackdata19",          0x5988, 0x385},
			{"td21_col_from_path",   0x64B9, 0x385}, {"td22_row_from_path",   0x683E, 0x385},
			{"trackdata23",          0x6BC3, 0x30},
		};
		int totbad = 0;
		for (unsigned i = 0; i < sizeof(T)/sizeof(T[0]); i++) {
			int bad = 0, first = -1;
			for (long k = 0; k < T[i].len; k++)
				if (ours[T[i].off+k] != want[T[i].off+k]) {
					if (!bad) first = (int)k;
					bad++;
				}
			totbad += bad;
			printf("  %-22s %s", T[i].n, bad ? "" : "identisk\n");
			if (bad) printf("%d/%ld byte skiljer, första index %d (vår %d, facit %d)\n",
			                bad, T[i].len, first,
			                ours[T[i].off+first], want[T[i].off+first]);
		}
		printf("  track_pieces_counter = %d\n", track_pieces_counter);
		printf("  SUMMA: %d avvikande byte\n\n", totbad);
		return totbad == 0 ? 0 : 1;
	}

	/* init_game_state's own preamble, in the original's order: the state
	 * flags and the four helicopter vectors come BEFORE the car placement
	 * (seg001.asm 3885..4021), and the rewind ring plus word_45A00 are set
	 * up before that (seg000.asm 436..442, seg001.asm 3873..3875). */
	/* The DOS oracle's RNG has already been advanced by the menus before a
	 * race starts, and there is no way to derive by how much from the dump
	 * alone. Every oracle recording shows the same six bytes in
	 * state.kevinseed at frame 1, so STUNTS_KEVINSEED lets a comparison run
	 * adopt them; without it we start from the dseg zeros the game boots
	 * with. Only state_op_unk's crash debris depends on this. */
	if (getenv("STUNTS_KEVINSEED")) {
		const char* s = getenv("STUNTS_KEVINSEED");
		unsigned v[6] = {0,0,0,0,0,0};
		if (sscanf(s, "%u,%u,%u,%u,%u,%u",
		           &v[0],&v[1],&v[2],&v[3],&v[4],&v[5]) == 6)
			for (int i = 0; i < 6; i++)
				g_kevinrandom_seed[i] = (uint8_t)v[i];
		printf("kevinseed: %u,%u,%u,%u,%u,%u\n",
		       g_kevinrandom_seed[0], g_kevinrandom_seed[1],
		       g_kevinrandom_seed[2], g_kevinrandom_seed[3],
		       g_kevinrandom_seed[4], g_kevinrandom_seed[5]);
	}
	word_45A00 = (int16_t)(30 * framespersec);
	if (!cvxptr) cvxptr = calloc(20, sizeof(struct GAMESTATE));
	init_game_state_vars();

	{
		/* restunts.c init_game_state: tile centre + (210 back, 36 lateral),
		 * rotation = -track_angle, height from hillHeightConsts[hillFlag]. */
		int16_t a = track_angle;
		int32_t tmpcol = multiply_and_scale(sin_fast((uint16_t)(a + 0x200)), 210)
		               + multiply_and_scale(sin_fast((uint16_t)(a + 0x100)), 36);
		int32_t tmprow = multiply_and_scale(cos_fast((uint16_t)(a + 0x200)), 210)
		               + multiply_and_scale(cos_fast((uint16_t)(a + 0x100)), 36);
		/* restunts.c init_game_state:537-540 mirrors the dseg start tile into
		 * GAMESTATE. Without this the four fields stay 0 and differ from the
		 * oracle from frame 1 onwards. */
		state.game_startcol  = startcol2;
		state.game_startcol2 = startcol2;
		state.game_startrow  = startrow2;
		state.game_startrow2 = startrow2;

		int32_t cx = ((int32_t)startcol2 * 1024 + 512) + tmpcol;
		int32_t cz = ((int32_t)(29 - startrow2) * 1024 + 512) + tmprow;
		init_carstate(&state.playerstate, &simd_player,
		              gameconfig.game_playertransmission,
		              cx * 64, (int32_t)hillHeightConsts[(int)hillFlag] * 64,
		              cz * 64, (int16_t)(-a));
		printf("startposition: (%ld, %ld) rot=%d\n", (long)(cx * 64), (long)(cz * 64), -a);
	}
	state.game_inputmode = 1;
	state.game_frame = 0;
	/* update_gamestate reads the input byte out of td16_rpl_buffer at the
	 * current game_frame; file_load_replay is what normally puts it there. */
	memcpy(td16_rpl_buffer, rpl + in_off, frames);
	gameconfig.game_recordedframes = frames;

	uint16_t start_frame = 0;
	if (seed_path) {
		FILE* sf = fopen(seed_path, "rb");
		if (!sf) { perror(seed_path); return 1; }
		if (fseek(sf, 2L + (long)(seed_frame - 1) * 1120L, SEEK_SET) != 0
		    || fread(&state, 1120, 1, sf) != 1) {
			printf("kunde inte läsa ruta %d ur %s\n", seed_frame, seed_path);
			return 1;
		}
		fclose(sf);
		start_frame = (uint16_t)seed_frame;
		printf("startar från facits ruta %d (game_frame=%d, pos %ld,%ld,%ld)\n",
		       seed_frame, state.game_frame,
		       (long)state.playerstate.car_posWorld1.lx,
		       (long)state.playerstate.car_posWorld1.ly,
		       (long)state.playerstate.car_posWorld1.lz);
	}

	/* Mirror of the oracle's GLOBALS.BIN: what build_track_object decided,
	 * per frame. That routine is a 2700-line machine translation and was
	 * never ported by restunts either, so its outputs are worth checking. */
	FILE* globals_out = NULL;
	{
		char gp[700];
		snprintf(gp, sizeof(gp), "%s.globals", argv[3]);
		globals_out = fopen(gp, "wb");
	}
	FILE* out = fopen(argv[3], "wb");
	if (!out) { perror(argv[3]); return 1; }
	{ uint16_t n_out = (uint16_t)(frames - start_frame); fwrite(&n_out, 2, 1, out); }

	/* update_gamestate order: read input at the current frame, increment the
	 * frame counter, then run the player. */
	for (uint16_t f = start_frame; f < frames; f++) {
		dbg_rc_probe = 0;
		dbg_bto_n = 0;
		/* Was: read the input byte here, bump game_frame, call player_op.
		 * That is update_gamestate's job, and doing it by hand skipped
		 * move_helicopters entirely - see sfasm_port.c PART 2. */
		update_gamestate();
		if (dbg_rc_probe) {
			int16_t* r2 = state.playerstate.car_rc2;
			printf("ruta %u: rc2=[%d,%d,%d,%d] Y=%ld\n", f + 1,
			       r2[0], r2[1], r2[2], r2[3],
			       (long)state.playerstate.car_posWorld1.ly);
		}
		fwrite(&state, 1120, 1, out);
		if (globals_out) {
			int16_t g[4] = { terrainHeight, planindex, wallindex,
			                 (int16_t)(unsigned char)current_surf_type };
			fwrite(g, sizeof(g), 1, globals_out);
		}
		if (getenv("STUNTS_BTO")) {
			int lo = atoi(getenv("STUNTS_BTO"));
			if ((int)f + 1 >= lo && (int)f + 1 <= lo + 5) {
				printf("ruta %u  Y=%ld\n", f + 1,
				       (long)state.playerstate.car_posWorld1.ly);
				for (int i = 0; i < dbg_bto_n; i++)
					printf("   anrop %d: ruta(%d,%d) terräng=%2d "
					       "elem %3d->%3d modell=0x%02X terrainHeight=%d plan=%d\n",
					       i, dbg_bto[i][0], dbg_bto[i][1], dbg_bto[i][2],
					       dbg_bto[i][3], dbg_bto[i][4],
					       dbg_bto[i][5] & 0xFF, dbg_bto[i][6], dbg_bto[i][7]);
			}
		}
		if (getenv("STUNTS_PROBE") && f >= 58 && f <= 70)
			printf("  ruta %3u: ruta(%d,%d) modell=0x%02X terrainHeight=%d planindex=%d  Y=%ld rot=(%d,%d,%d)\n",
			       f + 1, dbg_col, dbg_row, dbg_model & 0xFF, terrainHeight, planindex,
			       (long)state.playerstate.car_posWorld1.ly,
			       state.playerstate.car_rotate.x, state.playerstate.car_rotate.y,
			       state.playerstate.car_rotate.z);
	}
	fclose(out);
	if (globals_out) fclose(globals_out);
	printf("skrev %s (%u poster)\n", argv[3], frames);
	printf("attrapper: ");
	for (int i = 0; i < 8; i++) if (sfstub_hits[i]) printf("[%d]=%d ", i, sfstub_hits[i]);
	printf("\n");
	/* Non-zero would mean sub_18D60 read the zeroed stand-in for dseg:0000 and
	 * the substitution documented in sfshapeinfo.c is actually observable. */
	printf("shapeinfo_null-träffar: %lu\n", shapeinfo_null_hits);
	return 0;
}
