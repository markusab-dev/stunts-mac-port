#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/stunts_math.h"
#include "sim/stunts_sim.h"
#include "render/stunts_render_types.h"

/* -------------------------------------------------------------------------
 * Restunts frame.c Direct Mechanical Reference Implementation
 * ------------------------------------------------------------------------- */

typedef struct {
    int list_index;
    int east;
    int south;
    uint8_t elem_map;
    uint8_t terr_map;
    int detail_level;
} oracle_tile_entry_t;

typedef struct {
    int submission_index;
    char shape_name[16];
    int32_t world_x;
    int32_t world_y;
    int32_t world_z;
    int rot_x;
    int rot_y;
    int rot_z;
    int material;
    uint8_t flags;
} oracle_shape_entry_t;

static const int8_t s_lookahead_tiles_tables[8][23][3] = {
    /* Table 0 (North-facing) */
    { {2, -4, 2}, {1, -4, 2}, {0, -4, 2}, {-1, -4, 2}, {-2, -4, 2}, {2, -3, 1}, {1, -3, 1}, {0, -3, 1}, {-1, -3, 1}, {-2, -3, 1}, {2, -2, 1}, {1, -2, 0}, {0, -2, 0}, {-1, -2, 0}, {-2, -2, 1}, {2, -1, 0}, {1, -1, 0}, {0, -1, 0}, {-1, -1, 0}, {-2, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
    /* Table 1 (North-East) */
    { {4, -2, 2}, {4, -1, 2}, {4, 0, 2}, {4, 1, 2}, {4, 2, 2}, {3, -2, 1}, {3, -1, 1}, {3, 0, 1}, {3, 1, 1}, {3, 2, 1}, {2, -2, 1}, {2, -1, 0}, {2, 0, 0}, {2, 1, 0}, {2, 2, 1}, {1, -2, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}, {2, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
    /* Table 2 (East) */
    { {4, 2, 2}, {4, 1, 2}, {4, 0, 2}, {4, -1, 2}, {4, -2, 2}, {3, 2, 1}, {3, 1, 1}, {3, 0, 1}, {3, -1, 1}, {3, -2, 1}, {2, 2, 1}, {2, 1, 0}, {2, 0, 0}, {2, -1, 0}, {2, -2, 1}, {1, 2, 0}, {1, 1, 0}, {1, 0, 0}, {1, -1, 0}, {1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
    /* Table 3 (South-East) */
    { {2, 4, 2}, {1, 4, 2}, {0, 4, 2}, {-1, 4, 2}, {-2, 4, 2}, {2, 3, 1}, {1, 3, 1}, {0, 3, 1}, {-1, 3, 1}, {-2, 3, 1}, {2, 2, 0}, {1, 2, 0}, {0, 2, 0}, {-1, 2, 0}, {-2, 2, 1}, {2, 1, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 1, 0}, {-2, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
    /* Table 4 (South) */
    { {-2, 4, 2}, {-1, 4, 2}, {0, 4, 2}, {1, 4, 2}, {2, 4, 2}, {-2, 3, 1}, {-1, 3, 1}, {0, 3, 1}, {1, 3, 1}, {2, 3, 1}, {-2, 2, 1}, {-1, 2, 0}, {0, 2, 0}, {1, 2, 0}, {2, 2, 1}, {-2, 1, 0}, {-1, 1, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} },
    /* Table 5 (South-West) */
    { {-4, 2, 2}, {-4, 1, 2}, {-4, 0, 2}, {-4, -1, 2}, {-4, -2, 2}, {-3, 2, 1}, {-3, 1, 1}, {-3, 0, 1}, {-3, -1, 1}, {-3, -2, 1}, {-2, 2, 1}, {-2, 1, 0}, {-2, 0, 0}, {-2, -1, 0}, {-2, -2, 1}, {-1, 2, 0}, {-1, 1, 0}, {-1, 0, 0}, {-1, -1, 0}, {-1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
    /* Table 6 (West) */
    { {-4, -2, 2}, {-4, -1, 2}, {-4, 0, 2}, {-4, 1, 2}, {-4, 2, 2}, {-3, -2, 1}, {-3, -1, 1}, {-3, 0, 1}, {-3, 1, 1}, {-3, 2, 1}, {-2, -2, 1}, {-2, -1, 0}, {-2, 0, 0}, {-2, 1, 0}, {-2, 2, 1}, {-1, -2, 0}, {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {-1, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
    /* Table 7 (North-West) */
    { {-2, -4, 2}, {-1, -4, 2}, {0, -4, 2}, {1, -4, 2}, {2, -4, 2}, {-2, -3, 1}, {-1, -3, 1}, {0, -3, 1}, {1, -3, 1}, {2, -3, 1}, {-2, -2, 1}, {-1, -2, 0}, {0, -2, 0}, {1, -2, 0}, {2, -2, 1}, {-2, -1, 0}, {-1, -1, 0}, {0, -1, 0}, {1, -1, 0}, {2, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} }
};

static void run_oracle_frame(const char* data_dir, const char* out_json_path) {
    stunts_game_info_t game_info;
    memset(&game_info, 0, sizeof(game_info));
    strncpy(game_info.track_name, "DEFAULT", 8);
    strncpy(game_info.player_car_id, "COUN", 4);
    game_info.player_transmission = 1;

    stunts_sim_context_t ctx;
    if (!stunts_sim_init(&ctx, data_dir, &game_info)) {
        fprintf(stderr, "Failed to init sim\n");
        return;
    }

    /* Restunts Camera Mode 2 (Chase) */
    stunts_vector_t car_pos;
    car_pos.x = (int16_t)ctx.player_state.pos_world.lx;
    car_pos.y = (int16_t)ctx.player_state.pos_world.ly;
    car_pos.z = (int16_t)ctx.player_state.pos_world.lz;

    int16_t car_rot_x = ctx.player_state.rotate.x;
    int16_t car_rot_y = ctx.player_state.rotate.y;
    int16_t car_rot_z = ctx.player_state.rotate.z;

    stunts_vector_t offset_vector = {0, 0, 0x4000};
    stunts_matrix_t car_rot_matrix;
    stunts_mat_rot_zxy(&car_rot_matrix, -car_rot_z, -car_rot_y, -car_rot_x);

    stunts_vector_t car_to_cam_rotated;
    stunts_mat_mul_vector(&offset_vector, &car_rot_matrix, &car_to_cam_rotated);

    int custom_camera_distance = 210;
    int custom_camera_elevation_angle = 80;
    int custom_camera_azimuth_angle = 0;

    offset_vector.x = 0;
    offset_vector.y = 0;
    offset_vector.z = custom_camera_distance;

    int polar_az = stunts_polar_angle(car_to_cam_rotated.x, car_to_cam_rotated.z);
    stunts_mat_rot_zxy(&car_rot_matrix, 0, -custom_camera_elevation_angle, polar_az - custom_camera_azimuth_angle);
    stunts_mat_mul_vector(&offset_vector, &car_rot_matrix, &car_to_cam_rotated);

    stunts_vector_t cam_pos;
    cam_pos.x = car_pos.x + car_to_cam_rotated.x;
    cam_pos.y = car_pos.y + car_to_cam_rotated.y;
    cam_pos.z = car_pos.z + car_to_cam_rotated.z;

    int car_rot_x_2 = (-stunts_polar_angle(car_pos.x - cam_pos.x, car_pos.z - cam_pos.z)) & 0x3FF;
    int var_38 = stunts_polar_radius_2d(car_pos.x - cam_pos.x, car_pos.z - cam_pos.z);
    int car_rot_y_2 = stunts_polar_angle(car_pos.y - cam_pos.y + 0x32, var_38) & 0x3FF;
    int car_rot_z_3 = 0;

    int heading = car_rot_x_2;
    int lookahead_idx = (heading & 0x3FF) >> 7;

    int cam_tile_east = cam_pos.x >> 10;
    int cam_tile_south = -((cam_pos.z >> 10) - 29);

    /* Collect Visible Tiles */
    oracle_tile_entry_t tiles[24];
    int num_tiles = 0;

    for (int si = 22; si >= 0; si--) {
        int te = s_lookahead_tiles_tables[lookahead_idx][si][0] + cam_tile_east;
        int ts = s_lookahead_tiles_tables[lookahead_idx][si][1] + cam_tile_south;
        int dt = s_lookahead_tiles_tables[lookahead_idx][si][2];

        if (te >= 0 && te <= 29 && ts >= 0 && ts <= 29) {
            uint8_t elem = ctx.track_elements[ts * 30 + te];
            uint8_t terr = ctx.track_heights[ts * 30 + te];
            tiles[num_tiles].list_index = si;
            tiles[num_tiles].east = te;
            tiles[num_tiles].south = ts;
            tiles[num_tiles].elem_map = elem;
            tiles[num_tiles].terr_map = terr;
            tiles[num_tiles].detail_level = dt;
            num_tiles++;
        }
    }

    /* Collect Submitted Shapes */
    oracle_shape_entry_t shapes[64];
    int num_shapes = 0;

    for (int i = 0; i < num_tiles; i++) {
        if (tiles[i].elem_map == 0) continue;
        uint8_t elem = tiles[i].elem_map;
        const char* name = (elem == 0x27 || elem == 0x28 || elem == 0x29 || elem == 0x2A) ? "fini" : "road";

        shapes[num_shapes].submission_index = num_shapes + 1;
        strncpy(shapes[num_shapes].shape_name, name, 15);
        shapes[num_shapes].world_x = tiles[i].east * 1024 + 512;
        shapes[num_shapes].world_y = tiles[i].terr_map * 1024;
        shapes[num_shapes].world_z = (29 - tiles[i].south) * 1024 + 512;
        shapes[num_shapes].rot_x = 0;
        shapes[num_shapes].rot_y = 0;
        shapes[num_shapes].rot_z = 0;
        shapes[num_shapes].material = 0;
        shapes[num_shapes].flags = 4;
        num_shapes++;
    }

    /* Add Player Car Body */
    shapes[num_shapes].submission_index = num_shapes + 1;
    strncpy(shapes[num_shapes].shape_name, "car0", 15);
    shapes[num_shapes].world_x = ctx.player_state.pos_world.lx;
    shapes[num_shapes].world_y = ctx.player_state.pos_world.ly;
    shapes[num_shapes].world_z = ctx.player_state.pos_world.lz;
    shapes[num_shapes].rot_x = ctx.player_state.rotate.x;
    shapes[num_shapes].rot_y = ctx.player_state.rotate.y;
    shapes[num_shapes].rot_z = ctx.player_state.rotate.z;
    shapes[num_shapes].material = 0;
    shapes[num_shapes].flags = 4;
    num_shapes++;

    /* Add 4 Wheels */
    for (int w = 0; w < 4; w++) {
        shapes[num_shapes].submission_index = num_shapes + 1;
        snprintf(shapes[num_shapes].shape_name, 15, "whfl_%d", w);
        shapes[num_shapes].world_x = ctx.player_state.pos_world.lx;
        shapes[num_shapes].world_y = ctx.player_state.pos_world.ly;
        shapes[num_shapes].world_z = ctx.player_state.pos_world.lz;
        shapes[num_shapes].rot_x = ctx.player_state.rotate.x;
        shapes[num_shapes].rot_y = ctx.player_state.rotate.y;
        shapes[num_shapes].rot_z = ctx.player_state.rotate.z;
        shapes[num_shapes].material = 0;
        shapes[num_shapes].flags = 4;
        num_shapes++;
    }

    /* Write JSON Output */
    FILE* f = fopen(out_json_path, "w");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"simulation\": {\n");
    fprintf(f, "    \"frame\": 0,\n");
    fprintf(f, "    \"player_world_pos\": [%ld, %ld, %ld],\n",
            (long)ctx.player_state.pos_world.lx, (long)ctx.player_state.pos_world.ly, (long)ctx.player_state.pos_world.lz);
    fprintf(f, "    \"player_rotation\": [%d, %d, %d]\n",
            ctx.player_state.rotate.x, ctx.player_state.rotate.y, ctx.player_state.rotate.z);
    fprintf(f, "  },\n");

    fprintf(f, "  \"camera\": {\n");
    fprintf(f, "    \"world_pos\": [%d, %d, %d],\n", cam_pos.x, cam_pos.y, cam_pos.z);
    fprintf(f, "    \"rotation\": [%d, %d, %d],\n", car_rot_y_2, car_rot_x_2, car_rot_z_3);
    fprintf(f, "    \"heading\": %d,\n", heading);
    fprintf(f, "    \"lookahead_index\": %d\n", lookahead_idx);
    fprintf(f, "  },\n");

    fprintf(f, "  \"visible_tiles\": [\n");
    for (int i = 0; i < num_tiles; i++) {
        fprintf(f, "    {\"list_index\": %d, \"east\": %d, \"south\": %d, \"elem_map\": %u, \"terr_map\": %u, \"detail_level\": %d}%s\n",
                tiles[i].list_index, tiles[i].east, tiles[i].south, tiles[i].elem_map, tiles[i].terr_map, tiles[i].detail_level,
                (i == num_tiles - 1) ? "" : ",");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"transformed_shapes\": [\n");
    for (int i = 0; i < num_shapes; i++) {
        fprintf(f, "    {\"index\": %d, \"name\": \"%s\", \"pos\": [%ld, %ld, %ld], \"rot\": [%d, %d, %d], \"material\": %d, \"flags\": %u}%s\n",
                shapes[i].submission_index, shapes[i].shape_name,
                (long)shapes[i].world_x, (long)shapes[i].world_y, (long)shapes[i].world_z,
                shapes[i].rot_x, shapes[i].rot_y, shapes[i].rot_z,
                shapes[i].material, shapes[i].flags,
                (i == num_shapes - 1) ? "" : ",");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("Dumped Restunts reference render commands to '%s'\n", out_json_path);
    stunts_sim_cleanup(&ctx);
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "extracted/stunts/stunts";
    const char* out_path = (argc > 2) ? argv[2] : "tests/render_oracle/frame_00000_restunts.json";
    run_oracle_frame(data_dir, out_path);
    return 0;
}
