/*
 * rbridge.c - Bridge between this project's simulation context and the
 * vendored restunts renderer globals.
 *
 * The renderer (frame.c update_frame) reads the original game's globals:
 * `state` (struct GAMESTATE), `gameconfig`, the track element/terrain maps
 * td14_elem_map_main/td15_terr_map_main, the row/position lookup tables,
 * and camera-mode variables. This file fills them from a
 * stunts_sim_context_t each frame.
 *
 * NOTE: the current src/sim implementation is a hand-written approximation
 * (see Task #8) — the bridge isolates the renderer from it, so the sim can
 * be replaced by the restunts original without touching the renderer.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "externs.h"
#include "shape3d.h"

#include "../sim/stunts_sim.h"
#include "../asset/stunts_asset_loader.h"

/* renderer entry points */
extern void init_polyinfo(void);
extern int16_t shape3d_load_all(void);
extern void shape3d_load_car_shapes(char arg_playercarid[], char arg_opponentcarid[]);
extern void set_projection(int16_t i1, int16_t i2, int16_t i3, int16_t i4);
extern void update_frame(char arg_0, struct RECTANGLE* arg_cliprectptr);
extern void rblit_init(void);
extern void rfileio_set_data_dir(const char* dir);
extern void copy_material_list_pointers(int16_t* m1, int16_t* m2, int16_t* m3,
                                        int16_t* m4, int16_t unk);
extern int16_t* material_clrlist_ptr;
extern int16_t* material_clrlist2_ptr;
extern int16_t* material_patlist_ptr;
extern int16_t* material_patlist2_ptr;
extern int16_t skybox_sky_color;
extern int16_t skybox_grd_color;
extern struct SIMD simd_player;
extern struct SIMD simd_opponent;

/* renderer globals filled here */
extern struct GAMESTATE state;
extern struct GAMEINFO gameconfig;
extern uint8_t far* td14_elem_map_main;
extern uint8_t far* td15_terr_map_main;
extern int16_t trackrows[];
extern int16_t terrainrows[];
extern int16_t trackpos[];
extern int16_t trackpos2[];
extern int16_t trackcenterpos[];
extern int16_t trackcenterpos2[];
extern char cameramode;
extern uint8_t detail_level;
extern uint8_t game_replay_mode;
extern char followOpponentFlag;
extern int16_t run_game_random;
extern int16_t custom_camera_distance;
extern int16_t custom_camera_elevation_angle;
extern int16_t custom_camera_azimuth_angle;
extern int16_t planindex;
extern struct PLANE far* planptr;
extern struct PLANE far* current_planptr;
extern int16_t far* td08_direction_related;
extern int16_t far* trackdata9;
extern int16_t far* td10_track_check_rel;
extern uint8_t far* trackdata19;
extern uint8_t far* trackdata23;
extern int16_t far* td08_direction_related;
extern int16_t far* trackdata9;
extern int16_t far* td10_track_check_rel;
extern uint8_t far* trackdata19;
extern uint8_t far* trackdata23;

/* Track maps: 0x385 bytes each in the original trakdata chunk. */
static uint8_t s_elem_map[0x385];
static uint8_t s_terr_map[0x385];

static void copy_carstate(struct CARSTATE* dst, const stunts_car_state_t* src)
{
	int i;
	/* Scale: the original stores car_posWorld1 in 1/64 tile-units — frame.c
	 * reads it as `car_posWorld1.lx >> 6` and restunts.c initialises it as
	 * `trackcenterpos2[col] * 64`. The sim keeps positions already at
	 * tile scale (1024 per tile), so shift up by 6 here. */
	dst->car_posWorld1.lx = src->pos_world.lx << 6;
	dst->car_posWorld1.ly = src->pos_world.ly << 6;
	dst->car_posWorld1.lz = src->pos_world.lz << 6;
	dst->car_posWorld2.lx = src->pos_world_sub.lx << 6;
	dst->car_posWorld2.ly = src->pos_world_sub.ly << 6;
	dst->car_posWorld2.lz = src->pos_world_sub.lz << 6;
	/* Axis convention: restunts CARSTATE.car_rotate.x is the HEADING
	 * (rotation about the vertical axis — see frame.c camera math), .y is
	 * pitch, .z is roll. The sim's canonical rotate keeps heading in .y. */
	dst->car_rotate.x = src->rotate.y;
	dst->car_rotate.y = src->rotate.x;
	dst->car_rotate.z = src->rotate.z;
	dst->car_pseudoGravity = src->pseudo_gravity;
	dst->car_steeringAngle = src->steering_angle;
	dst->car_currpm = src->curr_rpm;
	dst->car_lastrpm = src->last_rpm;
	dst->car_speeddiff = src->speed_diff;
	dst->car_speed = src->speed_coupled;
	dst->car_speed2 = src->speed_actual;
	dst->car_lastspeed = src->speed_last;
	dst->car_gearratio = src->gear_ratio;
	dst->car_demandedGrip = src->demanded_grip;
	dst->car_surfacegrip_sum = src->surface_grip_sum;
	for (i = 0; i < 4; i++) {
		dst->car_rc1[i] = src->wheel_forces_rc1[i];
		dst->car_rc2[i] = src->wheel_forces_rc2[i];
		dst->car_rc3[i] = src->wheel_forces_rc3[i];
		dst->car_rc4[i] = src->wheel_forces_rc4[i];
		dst->car_rc5[i] = src->wheel_forces_rc5[i];
		dst->car_whlWorldCrds1[i].x = src->wheel_world_pos[i].x;
		dst->car_whlWorldCrds1[i].y = src->wheel_world_pos[i].y;
		dst->car_whlWorldCrds1[i].z = src->wheel_world_pos[i].z;
		dst->car_surfaceWhl[i] = (char)src->surface_whl[i];
	}
	dst->car_is_braking = (char)src->is_braking;
	dst->car_is_accelerating = (char)src->is_accelerating;
	dst->car_current_gear = (char)src->current_gear;
	dst->car_sumSurfFrontWheels = (char)src->sum_surf_front_wheels;
	dst->car_sumSurfRearWheels = (char)src->sum_surf_rear_wheels;
	dst->car_sumSurfAllWheels = (char)src->sum_surf_all_wheels;
	dst->car_crashBmpFlag = (char)src->crash_flag;
	dst->car_changing_gear = (char)src->changing_gear;
	dst->car_transmission = (char)src->transmission_auto;
}

void rfaithful_init(const char* data_dir, const stunts_sim_context_t* ctx)
{
	int16_t i;

	rfileio_set_data_dir(data_dir);
	rblit_init();

	/* init_row_tables (restunts.c:236) */
	for (i = 0; i < 30; i++) {
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
		trackpos[i] = (int16_t)((29 - i) << 10);
		trackpos2[i] = (int16_t)(i << 10);
		trackcenterpos[i] = (int16_t)(((29 - i) << 10) + 0x200);
		trackcenterpos2[i] = (int16_t)((i << 10) + 0x200);
	}

	/* Track maps: raw .TRK layout, element grid then terrain grid. */
	memset(s_elem_map, 0, sizeof(s_elem_map));
	memset(s_terr_map, 0, sizeof(s_terr_map));
	memcpy(s_elem_map, ctx->track_elements, 900);
	memcpy(s_terr_map, ctx->track_heights, 900);
	td14_elem_map_main = s_elem_map;
	td15_terr_map_main = s_terr_map;

	/* Collision planes: the sim's stunts_plane_t is byte-identical to the
	 * renderer's struct PLANE (34 bytes; verified below). build_track_object
	 * and the camera use them for terrain/plane queries. */
	_Static_assert(sizeof(struct PLANE) == 34, "PLANE layout");
	_Static_assert(sizeof(stunts_plane_t) == 34, "stunts_plane_t layout");
	planptr = (struct PLANE*)ctx->collision_planes;
	current_planptr = planptr;
	planindex = 0;

	/* trakdata slices normally built by track_setup (not yet ported).
	 * trackdata19 holds per-tile checkpoint/cone marker indices with 0xFF =
	 * "no marker": fill with 0xFF so no phantom markers are drawn. The
	 * related tables are only read when trackdata19[tile] != 0xFF, but give
	 * them zeroed storage so any stray read is defined. */
	{
		static uint8_t s_td19[0x385];
		static uint8_t s_td23[0x385];
		static int16_t s_td08[0x60 / 2];
		static int16_t s_td09[0x180 / 2];
		static int16_t s_td10[0x120 / 2];
		memset(s_td19, 0xFF, sizeof(s_td19));
		trackdata19 = s_td19;
		trackdata23 = s_td23;
		td08_direction_related = s_td08;
		trackdata9 = s_td09;
		td10_track_check_rel = s_td10;
	}

	/* trakdata slices normally built by track_setup (not yet ported).
	 * trackdata19 holds per-tile checkpoint/cone marker indices with 0xFF =
	 * "no marker": fill with 0xFF so no phantom markers are drawn. The
	 * related tables are only read when trackdata19[tile] != 0xFF, but give
	 * them zeroed storage so any stray read is defined. */
	{
		static uint8_t s_td19[0x385];
		static uint8_t s_td23[0x385];
		static int16_t s_td08[0x60 / 2];
		static int16_t s_td09[0x180 / 2];
		static int16_t s_td10[0x120 / 2];
		memset(s_td19, 0xFF, sizeof(s_td19));
		trackdata19 = s_td19;
		trackdata23 = s_td23;
		td08_direction_related = s_td08;
		trackdata9 = s_td09;
		td10_track_check_rel = s_td10;
	}

	/* SIMD block: the original copies the raw 'simd' resource straight into
	 * the global (restunts.c: fmemcpy(&simd_player, locate_shape_alt(
	 * carresptr, "simd"), sizeof(struct SIMD))). Do the same rather than
	 * relying on the sim's own field-by-field parse — update_frame reads
	 * car_height (cockpit eye level) and wheel_coords from here. */
	{
		char p[640];
		snprintf(p, sizeof(p), "%s/CAR%.4s.RES", data_dir,
		         ctx->game_info.player_car_id);
		stunts_res_archive_t* car = stunts_asset_load_archive(p);
		if (car) {
			const stunts_sub_resource_t* sd =
				stunts_asset_find_resource(car, "simd");
			if (sd) {
				size_t n = sd->size < sizeof(struct SIMD)
				         ? sd->size : sizeof(struct SIMD);
				memcpy(&simd_player, sd->data, n);
				memcpy(&simd_opponent, sd->data, n);
			}
			stunts_asset_free_archive(car);
		}
		/* Experimental override while the cockpit eye height is being
		 * validated against DOSBox captures. */
		{
			const char* h = getenv("STUNTS_CAR_HEIGHT");
			if (h && *h) simd_player.car_height = (int16_t)atoi(h);
		}
	}

	/* gameconfig from replay header */
	memcpy(gameconfig.game_playercarid, ctx->game_info.player_car_id, 4);
	gameconfig.game_playermaterial = (char)ctx->game_info.player_material;
	gameconfig.game_playertransmission = (char)ctx->game_info.player_transmission;
	gameconfig.game_opponenttype = (char)ctx->game_info.opponent_type;
	memcpy(gameconfig.game_opponentcarid, ctx->game_info.opponent_car_id, 4);
	gameconfig.game_opponentmaterial = (char)ctx->game_info.opponent_material;
	gameconfig.game_opponenttransmission = (char)ctx->game_info.opponent_transmission;
	gameconfig.game_framespersec = ctx->game_info.frames_per_sec;
	gameconfig.game_recordedframes = ctx->game_info.recorded_frames;

	/* init_video() equivalent: publish the material color/pattern lists to
	 * their _cpy aliases used by the draw dispatcher. */
	copy_material_list_pointers(material_clrlist_ptr, material_clrlist2_ptr,
	                            material_patlist_ptr, material_patlist2_ptr, 0);

	/* load_skybox color tail (seg003 loc_1D88E): sky/ground colors come from
	 * the active material color list. Bitmap panorama part is stubbed. */
	skybox_sky_color = material_clrlist_ptr[0x22 / 2];
	skybox_grd_color = material_clrlist_ptr[0x20 / 2];

	init_polyinfo();
	if (shape3d_load_all() != 0)
		fatal_error("shape3d_load_all failed");

	{
		/* Original sentinel for "no opponent": first id byte = 0xFF
		 * (set_default_car in restunts.c). */
		char carid[5] = {0};
		char oppid[5] = {(char)0xFF, 0, 0, 0, 0};
		memcpy(carid, ctx->game_info.player_car_id, 4);
		if (ctx->game_info.opponent_type != 0)
			memcpy(oppid, ctx->game_info.opponent_car_id, 4);
		shape3d_load_car_shapes(carid, oppid);
	}

	/* Full-screen 3D view (chase/replay layout, no dashboard):
	 * restunts.c:1031 uses set_projection(0x23, dash_y/6, 0x140, dash_y)
	 * with dash_y = 0xC8 (200) in the full-screen path. */
	set_projection(0x23, 0xC8 / 6, 0x140, 0xC8);

	cameramode = 2;              /* chase camera */
	followOpponentFlag = 0;
	game_replay_mode = 0;
	detail_level = 0;            /* full detail */
	run_game_random = 0;         /* deterministic scenery variation */
}

void rfaithful_set_camera_mode(int16_t mode)
{
	cameramode = (char)mode;
}

void rfaithful_render(const stunts_sim_context_t* ctx)
{
	struct RECTANGLE windshield;

	state.game_frame = (int16_t)ctx->sim_game_frame;
	copy_carstate(&state.playerstate, &ctx->player_state);
	copy_carstate(&state.opponentstate, &ctx->opponent_state);

	windshield.left = 0;
	windshield.right = 0x140;
	windshield.top = 0;
	windshield.bottom = 0xC8;

	update_frame(1, &windshield);
}
