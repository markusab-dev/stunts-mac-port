#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/stunts_sim.h"
#include "sim/stunts_math.h"
#include "asset/stunts_asset_loader.h"
#include "render/stunts_render_types.h"
#include "render/stunts_palette.h"
#include "render/stunts_shape3d.h"
#include "render/stunts_camera.h"
#include "render/stunts_rasterizer.h"

int main() {
    const char* data_dir = "extracted/stunts/stunts";
    stunts_game_info_t game_info;
    memset(&game_info, 0, sizeof(game_info));
    strncpy(game_info.track_name, "DEFAULT", 8);
    strncpy(game_info.player_car_id, "COUN", 4);
    game_info.player_transmission = 1;

    stunts_sim_context_t ctx;
    stunts_sim_init(&ctx, data_dir, &game_info);

    stunts_shape_db_t db;
    stunts_shape_db_load(data_dir, "COUN", &db);

    const stunts_shape3d_t* car_shape = stunts_shape_db_find(&db, "car0");
    const stunts_shape3d_t* road_shape = stunts_shape_db_find(&db, "road");

    stunts_viewport_t vp;
    vp.x = 0; vp.y = 0; vp.width = 1440; vp.height = 1080;
    vp.aspect_ratio = 1440.0f / 1080.0f;

    stunts_camera_t cam;
    stunts_camera_init(&cam, STUNTS_CAM_CHASE);
    stunts_camera_update(&cam, &ctx.player_state.pos_world, &ctx.player_state.rotate, ctx.player_state.speed_actual);

    printf("Camera World Position: (%d, %d, %d)\n", cam.pos.lx, cam.pos.ly, cam.pos.lz);
    printf("Camera Orientation:    (pitch=%d, yaw=%d, roll=%d)\n", cam.rot.x, cam.rot.y, cam.rot.z);
    printf("Car World Position:    (%d, %d, %d)\n", ctx.player_state.pos_world.lx, ctx.player_state.pos_world.ly, ctx.player_state.pos_world.lz);

    /* 1. Project Countach Vertices */
    printf("\nCountach First 6 Vertices Projection:\n");
    for (uint16_t v = 0; v < car_shape->num_verts && v < 6; v++) {
        stunts_matrix_t obj_mat;
        stunts_mat_rot_zxy(&obj_mat, ctx.player_state.rotate.z, ctx.player_state.rotate.x, ctx.player_state.rotate.y);
        stunts_vector_t rot_v;
        stunts_mat_mul_vector(&car_shape->verts[v], &obj_mat, &rot_v);

        stunts_vector_long_t v_world;
        v_world.lx = ctx.player_state.pos_world.lx + rot_v.x;
        v_world.ly = ctx.player_state.pos_world.ly + rot_v.y;
        v_world.lz = ctx.player_state.pos_world.lz + rot_v.z;

        int32_t px = 0, py = 0, cz = 0;
        bool valid = stunts_camera_project_point(&cam, &vp, &v_world, &px, &py, &cz);
        printf("  Car V%u: Local=(%d,%d,%d) -> World=(%d,%d,%d) -> Cam-Z=%d -> Screen=(%d,%d) [%s]\n",
               v, car_shape->verts[v].x, car_shape->verts[v].y, car_shape->verts[v].z,
               v_world.lx, v_world.ly, v_world.lz, cz, px, py, valid ? "VISIBLE" : "CLIPPED");
    }

    /* 2. Project Road Tile in Front of Car (Col 24, Row 3) */
    printf("\nRoad Tile Ahead (Col 24, Row 3) Projection:\n");
    stunts_vector_long_t road_tile_pos;
    road_tile_pos.lx = 24 * 1024 + 512;
    road_tile_pos.lz = (29 - 3) * 1024 + 512;
    road_tile_pos.ly = 0;

    int16_t road_yaw = 256; /* East-West road */
    stunts_matrix_t road_mat;
    stunts_mat_rot_zxy(&road_mat, 0, 0, road_yaw);

    for (uint16_t v = 0; v < road_shape->num_verts && v < 6; v++) {
        stunts_vector_t rot_v;
        stunts_mat_mul_vector(&road_shape->verts[v], &road_mat, &rot_v);

        stunts_vector_long_t v_world;
        v_world.lx = road_tile_pos.lx + rot_v.x;
        v_world.ly = road_tile_pos.ly + rot_v.y;
        v_world.lz = road_tile_pos.lz + rot_v.z;

        int32_t px = 0, py = 0, cz = 0;
        bool valid = stunts_camera_project_point(&cam, &vp, &v_world, &px, &py, &cz);
        printf("  Road V%u: Local=(%d,%d,%d) -> World=(%d,%d,%d) -> Cam-Z=%d -> Screen=(%d,%d) [%s]\n",
               v, road_shape->verts[v].x, road_shape->verts[v].y, road_shape->verts[v].z,
               v_world.lx, v_world.ly, v_world.lz, cz, px, py, valid ? "VISIBLE" : "CLIPPED");
    }

    stunts_sim_cleanup(&ctx);
    return 0;
}
