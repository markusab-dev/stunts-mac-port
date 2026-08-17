#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/stunts_math.h"
#include "sim/stunts_sim.h"
#include "render/stunts_render_320.h"

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "extracted/stunts/stunts";
    const char* out_path = (argc > 2) ? argv[2] : "tests/render_oracle/frame_00000_native.json";

    stunts_game_info_t game_info;
    memset(&game_info, 0, sizeof(game_info));
    strncpy(game_info.track_name, "DEFAULT", 8);
    strncpy(game_info.player_car_id, "COUN", 4);
    game_info.player_transmission = 1;

    stunts_sim_context_t ctx;
    if (!stunts_sim_init(&ctx, data_dir, &game_info)) {
        fprintf(stderr, "Failed to init sim\n");
        return 1;
    }

    /* Native Camera Calculation matching Restunts frame.c */
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

    static const int8_t lookahead_tbl[8][23][3] = {
        { {2, -4, 2}, {1, -4, 2}, {0, -4, 2}, {-1, -4, 2}, {-2, -4, 2}, {2, -3, 1}, {1, -3, 1}, {0, -3, 1}, {-1, -3, 1}, {-2, -3, 1}, {2, -2, 1}, {1, -2, 0}, {0, -2, 0}, {-1, -2, 0}, {-2, -2, 1}, {2, -1, 0}, {1, -1, 0}, {0, -1, 0}, {-1, -1, 0}, {-2, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
        { {4, -2, 2}, {4, -1, 2}, {4, 0, 2}, {4, 1, 2}, {4, 2, 2}, {3, -2, 1}, {3, -1, 1}, {3, 0, 1}, {3, 1, 1}, {3, 2, 1}, {2, -2, 1}, {2, -1, 0}, {2, 0, 0}, {2, 1, 0}, {2, 2, 1}, {1, -2, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}, {2, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
        { {4, 2, 2}, {4, 1, 2}, {4, 0, 2}, {4, -1, 2}, {4, -2, 2}, {3, 2, 1}, {3, 1, 1}, {3, 0, 1}, {3, -1, 1}, {3, -2, 1}, {2, 2, 1}, {2, 1, 0}, {2, 0, 0}, {2, -1, 0}, {2, -2, 1}, {1, 2, 0}, {1, 1, 0}, {1, 0, 0}, {1, -1, 0}, {1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
        { {2, 4, 2}, {1, 4, 2}, {0, 4, 2}, {-1, 4, 2}, {-2, 4, 2}, {2, 3, 1}, {1, 3, 1}, {0, 3, 1}, {-1, 3, 1}, {-2, 3, 1}, {2, 2, 0}, {1, 2, 0}, {0, 2, 0}, {-1, 2, 0}, {-2, 2, 1}, {2, 1, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 1, 0}, {-2, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
        { {-2, 4, 2}, {-1, 4, 2}, {0, 4, 2}, {1, 4, 2}, {2, 4, 2}, {-2, 3, 1}, {-1, 3, 1}, {0, 3, 1}, {1, 3, 1}, {2, 3, 1}, {-2, 2, 1}, {-1, 2, 0}, {0, 2, 0}, {1, 2, 0}, {2, 2, 1}, {-2, 1, 0}, {-1, 1, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} },
        { {-4, 2, 2}, {-4, 1, 2}, {-4, 0, 2}, {-4, -1, 2}, {-4, -2, 2}, {-3, 2, 1}, {-3, 1, 1}, {-3, 0, 1}, {-3, -1, 1}, {-3, -2, 1}, {-2, 2, 1}, {-2, 1, 0}, {-2, 0, 0}, {-2, -1, 0}, {-2, -2, 1}, {-1, 2, 0}, {-1, 1, 0}, {-1, 0, 0}, {-1, -1, 0}, {-1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
        { {-4, -2, 2}, {-4, -1, 2}, {-4, 0, 2}, {-4, 1, 2}, {-4, 2, 2}, {-3, -2, 1}, {-3, -1, 1}, {-3, 0, 1}, {-3, 1, 1}, {-3, 2, 1}, {-2, -2, 1}, {-2, -1, 0}, {-2, 0, 0}, {-2, 1, 0}, {-2, 2, 1}, {-1, -2, 0}, {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {-1, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
        { {-2, -4, 2}, {-1, -4, 2}, {0, -4, 2}, {1, -4, 2}, {2, -4, 2}, {-2, -3, 1}, {-1, -3, 1}, {0, -3, 1}, {1, -3, 1}, {2, -3, 1}, {-2, -2, 1}, {-1, -2, 0}, {0, -2, 0}, {1, -2, 0}, {2, -2, 1}, {-2, -1, 0}, {-1, -1, 0}, {0, -1, 0}, {1, -1, 0}, {2, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} }
    };

    FILE* f = fopen(out_path, "w");
    if (!f) return 1;

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
    int num_tiles = 0;
    for (int si = 22; si >= 0; si--) {
        int te = lookahead_tbl[lookahead_idx][si][0] + cam_tile_east;
        int ts = lookahead_tbl[lookahead_idx][si][1] + cam_tile_south;
        int dt = lookahead_tbl[lookahead_idx][si][2];
        if (te >= 0 && te <= 29 && ts >= 0 && ts <= 29) {
            uint8_t elem = ctx.track_elements[ts * 30 + te];
            uint8_t terr = ctx.track_heights[ts * 30 + te];
            if (num_tiles > 0) fprintf(f, ",\n");
            fprintf(f, "    {\"list_index\": %d, \"east\": %d, \"south\": %d, \"elem_map\": %u, \"terr_map\": %u, \"detail_level\": %d}",
                    si, te, ts, elem, terr, dt);
            num_tiles++;
        }
    }
    fprintf(f, "\n  ],\n");

    fprintf(f, "  \"transformed_shapes\": [\n");
    int shape_count = 0;
    for (int si = 22; si >= 0; si--) {
        int te = lookahead_tbl[lookahead_idx][si][0] + cam_tile_east;
        int ts = lookahead_tbl[lookahead_idx][si][1] + cam_tile_south;
        if (te < 0 || te > 29 || ts < 0 || ts > 29) continue;
        uint8_t elem = ctx.track_elements[ts * 30 + te];
        if (elem == 0) continue;

        const char* name = (elem == 0x27 || elem == 0x28 || elem == 0x29 || elem == 0x2A) ? "fini" : "road";
        if (shape_count > 0) fprintf(f, ",\n");
        fprintf(f, "    {\"index\": %d, \"name\": \"%s\", \"pos\": [%d, %d, %d], \"rot\": [0, 0, 0], \"material\": 0, \"flags\": 4}",
                ++shape_count, name, te * 1024 + 512, ctx.track_heights[ts * 30 + te] * 1024, (29 - ts) * 1024 + 512);
    }

    /* Car Body */
    if (shape_count > 0) fprintf(f, ",\n");
    fprintf(f, "    {\"index\": %d, \"name\": \"car0\", \"pos\": [%ld, %ld, %ld], \"rot\": [%d, %d, %d], \"material\": 0, \"flags\": 4},\n",
            ++shape_count, (long)ctx.player_state.pos_world.lx, (long)ctx.player_state.pos_world.ly, (long)ctx.player_state.pos_world.lz,
            ctx.player_state.rotate.x, ctx.player_state.rotate.y, ctx.player_state.rotate.z);

    /* 4 Wheels */
    for (int w = 0; w < 4; w++) {
        fprintf(f, "    {\"index\": %d, \"name\": \"whfl_%d\", \"pos\": [%ld, %ld, %ld], \"rot\": [%d, %d, %d], \"material\": 0, \"flags\": 4}%s\n",
                ++shape_count, w, (long)ctx.player_state.pos_world.lx, (long)ctx.player_state.pos_world.ly, (long)ctx.player_state.pos_world.lz,
                ctx.player_state.rotate.x, ctx.player_state.rotate.y, ctx.player_state.rotate.z,
                (w == 3) ? "" : ",");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("Dumped native render commands to '%s'\n", out_path);
    stunts_sim_cleanup(&ctx);
    return 0;
}
