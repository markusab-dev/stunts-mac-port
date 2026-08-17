#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sim/stunts_math.h"
#include "sim/stunts_sim.h"
#include "render/stunts_render_320.h"

int main(int argc, char** argv) {
    const char* data_dir = "extracted/stunts/stunts";
    int max_cmd = (argc > 1) ? atoi(argv[1]) : 999;
    int max_prim = (argc > 2) ? atoi(argv[2]) : 999;

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

    stunts_render_320_t r;
    if (!stunts_render_320_init(&r, data_dir, "COUN")) {
        fprintf(stderr, "Failed to init 320 renderer\n");
        return 1;
    }

    printf("================================================================================\n");
    printf(" STUNTS PREFIX-RENDER BISECTION & PRIMITIVE AUDIT (MaxCmd: %d, MaxPrim: %d)\n", max_cmd, max_prim);
    printf("================================================================================\n");

    /* Chase Camera matching Restunts frame.c */
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

    offset_vector.x = 0;
    offset_vector.y = 0;
    offset_vector.z = 210;

    int polar_az = stunts_polar_angle(car_to_cam_rotated.x, car_to_cam_rotated.z);
    stunts_mat_rot_zxy(&car_rot_matrix, 0, -80, polar_az);
    stunts_mat_mul_vector(&offset_vector, &car_rot_matrix, &car_to_cam_rotated);

    stunts_vector_t cam_pos;
    cam_pos.x = car_pos.x + car_to_cam_rotated.x;
    cam_pos.y = car_pos.y + car_to_cam_rotated.y;
    cam_pos.z = car_pos.z + car_to_cam_rotated.z;

    int car_rot_x_2 = (-stunts_polar_angle(car_pos.x - cam_pos.x, car_pos.z - cam_pos.z)) & 0x3FF;
    int var_38 = stunts_polar_radius_2d(car_pos.x - cam_pos.x, car_pos.z - cam_pos.z);
    int car_rot_y_2 = stunts_polar_angle(car_pos.y - cam_pos.y + 0x32, var_38) & 0x3FF;
    int car_rot_z_3 = 0;

    stunts_matrix_t view_mat;
    stunts_mat_rot_zxy(&view_mat, -car_rot_z_3, -car_rot_y_2, -car_rot_x_2);

    int cam_tile_east = cam_pos.x >> 10;
    int cam_tile_south = -((cam_pos.z >> 10) - 29);
    int heading_idx = ((car_rot_x_2 & 0x3FF) >> 7);

    static const int8_t s_lookahead_tbl[8][23][3] = {
        { {2, -4, 2}, {1, -4, 2}, {0, -4, 2}, {-1, -4, 2}, {-2, -4, 2}, {2, -3, 1}, {1, -3, 1}, {0, -3, 1}, {-1, -3, 1}, {-2, -3, 1}, {2, -2, 1}, {1, -2, 0}, {0, -2, 0}, {-1, -2, 0}, {-2, -2, 1}, {2, -1, 0}, {1, -1, 0}, {0, -1, 0}, {-1, -1, 0}, {-2, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
        { {4, -2, 2}, {4, -1, 2}, {4, 0, 2}, {4, 1, 2}, {4, 2, 2}, {3, -2, 1}, {3, -1, 1}, {3, 0, 1}, {3, 1, 1}, {3, 2, 1}, {2, -2, 1}, {2, -1, 0}, {2, 0, 0}, {2, 1, 0}, {2, 2, 1}, {1, -2, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}, {2, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
        { {4, 2, 2}, {4, 1, 2}, {4, 0, 2}, {4, -1, 2}, {4, -2, 2}, {3, 2, 1}, {3, 1, 1}, {3, 0, 1}, {3, -1, 1}, {3, -2, 1}, {2, 2, 1}, {2, 1, 0}, {2, 0, 0}, {2, -1, 0}, {2, -2, 1}, {1, 2, 0}, {1, 1, 0}, {1, 0, 0}, {1, -1, 0}, {1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
        { {2, 4, 2}, {1, 4, 2}, {0, 4, 2}, {-1, 4, 2}, {-2, 4, 2}, {2, 3, 1}, {1, 3, 1}, {0, 3, 1}, {-1, 3, 1}, {-2, 3, 1}, {2, 2, 0}, {1, 2, 0}, {0, 2, 0}, {-1, 2, 0}, {-2, 2, 1}, {2, 1, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 1, 0}, {-2, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
        { {-2, 4, 2}, {-1, 4, 2}, {0, 4, 2}, {1, 4, 2}, {2, 4, 2}, {-2, 3, 1}, {-1, 3, 1}, {0, 3, 1}, {1, 3, 1}, {2, 3, 1}, {-2, 2, 1}, {-1, 2, 0}, {0, 2, 0}, {1, 2, 0}, {2, 2, 1}, {-2, 1, 0}, {-1, 1, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} },
        { {-4, 2, 2}, {-4, 1, 2}, {-4, 0, 2}, {-4, -1, 2}, {-4, -2, 2}, {-3, 2, 1}, {-3, 1, 1}, {-3, 0, 1}, {-3, -1, 1}, {-3, -2, 1}, {-2, 2, 1}, {-2, 1, 0}, {-2, 0, 0}, {-2, -1, 0}, {-2, -2, 1}, {-1, 2, 0}, {-1, 1, 0}, {-1, 0, 0}, {-1, -1, 0}, {-1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
        { {-4, -2, 2}, {-4, -1, 2}, {-4, 0, 2}, {-4, 1, 2}, {-4, 2, 2}, {-3, -2, 1}, {-3, -1, 1}, {-3, 0, 1}, {-3, 1, 1}, {-3, 2, 1}, {-2, -2, 1}, {-2, -1, 0}, {-2, 0, 0}, {-2, 1, 0}, {-2, 2, 1}, {-1, -2, 0}, {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {-1, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
        { {-2, -4, 2}, {-1, -4, 2}, {0, -4, 2}, {1, -4, 2}, {2, -4, 2}, {-2, -3, 1}, {-1, -3, 1}, {0, -3, 1}, {1, -3, 1}, {2, -3, 1}, {-2, -2, 1}, {-1, -2, 0}, {0, -2, 0}, {1, -2, 0}, {2, -2, 1}, {-2, -1, 0}, {-1, -1, 0}, {0, -1, 0}, {1, -1, 0}, {2, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} }
    };

    int cmd_count = 0;

    /* 1. Track Shapes */
    for (int si = 22; si >= 0; si--) {
        int te = s_lookahead_tbl[heading_idx][si][0] + cam_tile_east;
        int ts = s_lookahead_tbl[heading_idx][si][1] + cam_tile_south;
        if (te < 0 || te > 29 || ts < 0 || ts > 29) continue;

        uint8_t elem = ctx.track_elements[ts * 30 + te];
        if (elem == 0) continue;

        cmd_count++;
        const char* tag = (elem == 0x27 || elem == 0x28 || elem == 0x29 || elem == 0x2A) ? "fini" : "road";
        const stunts_shape3d_t* sh = stunts_shape_db_find(&r.shape_db, tag);
        if (!sh) continue;

        printf("\n[COMMAND %2d] Shape: '%s' @ Tile(%d, %d) World(%d, %d, %d) Prims: %d\n",
               cmd_count, tag, te, ts, te * 1024 + 512, ctx.track_heights[ts * 30 + te] * 1024, (29 - ts) * 1024 + 512, sh->num_prims);

        if (cmd_count > max_cmd) {
            printf("  -> Skipping beyond MaxCmd %d\n", max_cmd);
            continue;
        }

        /* Check each primitive */
        stunts_vector_long_t pos_rel = {
            te * 1024 + 512 - cam_pos.x,
            ctx.track_heights[ts * 30 + te] * 1024 - cam_pos.y,
            (29 - ts) * 1024 + 512 - cam_pos.z
        };
        stunts_vector_long_t obj_cam_center;
        stunts_mat_mul_vector_long(&pos_rel, &view_mat, &obj_cam_center);

        stunts_matrix_t obj_rot_mat;
        stunts_mat_rot_zxy(&obj_rot_mat, 0, 0, 0);
        stunts_matrix_t compound_mat;
        stunts_mat_multiply(&obj_rot_mat, &view_mat, &compound_mat);

        int32_t screen_x[256], screen_y[256], cam_z[256];
        bool vert_valid[256];

        for (uint16_t v = 0; v < sh->num_verts; v++) {
            stunts_vector_t rot_v;
            stunts_mat_mul_vector(&sh->verts[v], &compound_mat, &rot_v);
            int32_t vx = obj_cam_center.lx + rot_v.x;
            int32_t vy = obj_cam_center.ly + rot_v.y;
            int32_t vz = obj_cam_center.lz + rot_v.z;
            cam_z[v] = vz;
            if (vz >= 12) {
                vert_valid[v] = true;
                screen_x[v] = 160 + (int32_t)(((int64_t)vx * 256) / vz);
                screen_y[v] = 100 - (int32_t)(((int64_t)vy * 213) / vz);
            } else {
                vert_valid[v] = false;
            }
        }

        for (uint16_t p = 0; p < sh->num_prims && (int)p < max_prim; p++) {
            const stunts_primitive_t* prim = &sh->prims[p];
            if (prim->num_verts < 2) continue;

            bool all_valid = true;
            for (uint8_t v = 0; v < prim->num_verts; v++) {
                uint8_t vi = prim->vert_indices[v];
                if (!vert_valid[vi]) { all_valid = false; break; }
            }
            if (!all_valid) {
                printf("  prim[%2d]: CLIPPED (vert z < 12)\n", p);
                continue;
            }

            int min_x = screen_x[prim->vert_indices[0]], max_x = min_x;
            int min_y = screen_y[prim->vert_indices[0]], max_y = min_y;
            for (uint8_t v = 1; v < prim->num_verts; v++) {
                uint8_t vi = prim->vert_indices[v];
                if (screen_x[vi] < min_x) min_x = screen_x[vi];
                if (screen_x[vi] > max_x) max_x = screen_x[vi];
                if (screen_y[vi] < min_y) min_y = screen_y[vi];
                if (screen_y[vi] > max_y) max_y = screen_y[vi];
            }

            int bbox_w = max_x - min_x;
            int bbox_h = max_y - min_y;

            bool suspicious = (bbox_w > 320 || bbox_h > 200 || min_x < -200 || max_x > 520 || min_y < -200 || max_y > 400);

            printf("  prim[%2d]: type=%d num_verts=%d bbox=[%4d,%4d to %4d,%4d] (%dx%d) %s\n",
                   p, prim->type, prim->num_verts, min_x, min_y, max_x, max_y, bbox_w, bbox_h,
                   suspicious ? ">>> SUSPICIOUS SCREEN-SPANNING GEOMETRY <<<" : "OK");
        }
    }

    /* 2. Car0 */
    cmd_count++;
    const stunts_shape3d_t* car_sh = stunts_shape_db_find(&r.shape_db, "car0");
    if (car_sh) {
        printf("\n[COMMAND %2d] Shape: 'car0' (Countach Body) World(%ld, %ld, %ld) Prims: %d\n",
               cmd_count, (long)ctx.player_state.pos_world.lx, (long)ctx.player_state.pos_world.ly, (long)ctx.player_state.pos_world.lz, car_sh->num_prims);

        if (cmd_count <= max_cmd) {
            stunts_vector_long_t pos_rel = {
                (int16_t)(ctx.player_state.pos_world.lx - cam_pos.x),
                (int16_t)(ctx.player_state.pos_world.ly - cam_pos.y),
                (int16_t)(ctx.player_state.pos_world.lz - cam_pos.z)
            };
            stunts_vector_long_t obj_cam_center;
            stunts_mat_mul_vector_long(&pos_rel, &view_mat, &obj_cam_center);

            stunts_matrix_t obj_rot_mat;
            stunts_mat_rot_zxy(&obj_rot_mat, ctx.player_state.rotate.z, ctx.player_state.rotate.x, ctx.player_state.rotate.y);
            stunts_matrix_t compound_mat;
            stunts_mat_multiply(&obj_rot_mat, &view_mat, &compound_mat);

            int32_t screen_x[256], screen_y[256], cam_z[256];
            bool vert_valid[256];

            for (uint16_t v = 0; v < car_sh->num_verts; v++) {
                stunts_vector_t rot_v;
                stunts_mat_mul_vector(&car_sh->verts[v], &compound_mat, &rot_v);
                int32_t vx = obj_cam_center.lx + rot_v.x;
                int32_t vy = obj_cam_center.ly + rot_v.y;
                int32_t vz = obj_cam_center.lz + rot_v.z;
                cam_z[v] = vz;
                if (vz >= 12) {
                    vert_valid[v] = true;
                    screen_x[v] = 160 + (int32_t)(((int64_t)vx * 256) / vz);
                    screen_y[v] = 100 - (int32_t)(((int64_t)vy * 213) / vz);
                } else {
                    vert_valid[v] = false;
                }
            }

            for (uint16_t p = 0; p < car_sh->num_prims && (int)p < max_prim; p++) {
                const stunts_primitive_t* prim = &car_sh->prims[p];
                if (prim->num_verts < 2) continue;

                bool all_valid = true;
                for (uint8_t v = 0; v < prim->num_verts; v++) {
                    uint8_t vi = prim->vert_indices[v];
                    if (!vert_valid[vi]) { all_valid = false; break; }
                }
                if (!all_valid) {
                    printf("  prim[%2d]: CLIPPED (vert z < 12)\n", p);
                    continue;
                }

                /* Backface Culling */
                if (prim->num_verts >= 3) {
                    uint8_t i0 = prim->vert_indices[0];
                    uint8_t i1 = prim->vert_indices[1];
                    uint8_t i2 = prim->vert_indices[2];
                    int32_t dx0 = screen_x[i0] - screen_x[i1];
                    int32_t dx1 = screen_x[i2] - screen_x[i1];
                    int32_t dy0 = screen_y[i0] - screen_y[i1];
                    int32_t dy1 = screen_y[i2] - screen_y[i1];
                    int64_t cross = ((int64_t)dx1 * dy0) - ((int64_t)dx0 * dy1);
                    if (cross <= 0) {
                        printf("  prim[%2d]: CULLED (cross = %lld)\n", p, (long long)cross);
                        continue;
                    }
                }

                int min_x = screen_x[prim->vert_indices[0]], max_x = min_x;
                int min_y = screen_y[prim->vert_indices[0]], max_y = min_y;
                for (uint8_t v = 1; v < prim->num_verts; v++) {
                    uint8_t vi = prim->vert_indices[v];
                    if (screen_x[vi] < min_x) min_x = screen_x[vi];
                    if (screen_x[vi] > max_x) max_x = screen_x[vi];
                    if (screen_y[vi] < min_y) min_y = screen_y[vi];
                    if (screen_y[vi] > max_y) max_y = screen_y[vi];
                }

                int bbox_w = max_x - min_x;
                int bbox_h = max_y - min_y;

                bool suspicious = (bbox_w > 320 || bbox_h > 200 || min_x < -100 || max_x > 420 || min_y < -100 || max_y > 300);

                printf("  prim[%2d]: type=%d num_verts=%d bbox=[%4d,%4d to %4d,%4d] (%dx%d) %s\n",
                       p, prim->type, prim->num_verts, min_x, min_y, max_x, max_y, bbox_w, bbox_h,
                       suspicious ? ">>> SUSPICIOUS SCREEN-SPANNING GEOMETRY <<<" : "OK");
            }
        }
    }

    stunts_render_320_cleanup(&r);
    stunts_sim_cleanup(&ctx);
    return 0;
}
