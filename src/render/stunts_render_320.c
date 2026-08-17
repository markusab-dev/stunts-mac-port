#include "stunts_render_320.h"
#include "../sim/stunts_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STUNTS_MIN
#define STUNTS_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef STUNTS_MAX
#define STUNTS_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/* -------------------------------------------------------------------------
 * Restunts 23-Tile Lookahead Schemas (from dseg.asm / frame.c)
 * 8 rotation tables, each 23 tiles ahead of camera in drawing order
 * (farthest tiles first). Format: {east_offset, south_offset, detail_threshold}
 * ------------------------------------------------------------------------- */
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

/* Track Element to Shape Tag Mapping */
static const char* get_track_shape_tag_320(uint8_t elem_id, int16_t* out_yaw) {
    *out_yaw = 0;
    if (elem_id == 0) return NULL;

    switch (elem_id) {
        case 0x01: *out_yaw = 0;   return "road";
        case 0x02: *out_yaw = 256; return "road";
        case 0x03: *out_yaw = 0;   return "turn";
        case 0x04: *out_yaw = 256; return "turn";
        case 0x05: *out_yaw = 512; return "turn";
        case 0x06: *out_yaw = 768; return "turn";
        case 0x07: *out_yaw = 0;   return "inte";
        case 0x11: *out_yaw = 0;   return "ramp";
        case 0x12: *out_yaw = 256; return "ramp";
        case 0x13: *out_yaw = 512; return "ramp";
        case 0x14: *out_yaw = 768; return "ramp";
        case 0x27: *out_yaw = 0;   return "fini";
        case 0x28: *out_yaw = 256; return "fini";
        case 0x29: *out_yaw = 512; return "fini";
        case 0x2A: *out_yaw = 768; return "fini";
        case 0x33: *out_yaw = 0;   return "loop";
        case 0x34: *out_yaw = 256; return "loop";
        case 0x35: *out_yaw = 512; return "loop";
        case 0x36: *out_yaw = 768; return "loop";
        case 0x4D: *out_yaw = 0;   return "barn";
        case 0x51: *out_yaw = 0;   return "gass";
        case 0x53: *out_yaw = 0;   return "tree";
        case 0x54: *out_yaw = 0;   return "palm";
        case 0x55: *out_yaw = 0;   return "cact";
        case 0x56: *out_yaw = 0;   return "wind";
        default:
            if (elem_id >= 0x3B && elem_id <= 0x42) return "bank";
            return "road";
    }
}

typedef struct {
    int32_t z_avg;
    uint8_t color_idx;
    uint8_t num_pts;
    int32_t px[STUNTS_MAX_POLY_VERTS];
    int32_t py[STUNTS_MAX_POLY_VERTS];
} draw_poly_320_t;

#define MAX_DRAW_POLYS_320 2048
static draw_poly_320_t s_poly_queue_320[MAX_DRAW_POLYS_320];
static uint32_t s_poly_queue_count_320 = 0;

static int compare_draw_polys_320(const void* a, const void* b) {
    const draw_poly_320_t* pa = (const draw_poly_320_t*)a;
    const draw_poly_320_t* pb = (const draw_poly_320_t*)b;
    if (pb->z_avg > pa->z_avg) return 1;
    if (pb->z_avg < pa->z_avg) return -1;
    return 0;
}

bool stunts_render_320_init(stunts_render_320_t* r, const char* data_dir, const char* car_id) {
    if (!r || !data_dir) return false;
    memset(r, 0, sizeof(stunts_render_320_t));
    r->pitch = STUNTS_RENDER_W * 4;
    r->camera_mode = 2; /* Chase camera default */

    char sdmain_path[512];
    snprintf(sdmain_path, sizeof(sdmain_path), "%s/SDMAIN.PVS", data_dir);
    if (!stunts_palette_load(sdmain_path, &r->palette)) {
        stunts_palette_init_default(&r->palette);
    }

    stunts_shape_db_load(data_dir, car_id, &r->shape_db);
    return true;
}

void stunts_render_320_cleanup(stunts_render_320_t* r) {
    if (!r) return;
}

/* 320x200 Scanline Polygon Rasterizer */
static void fill_poly_320(stunts_render_320_t* r, int num_pts, const int32_t* xs, const int32_t* ys, uint8_t color_idx) {
    if (num_pts < 3) return;

    int32_t min_y = ys[0];
    int32_t max_y = ys[0];
    for (int i = 1; i < num_pts; i++) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    min_y = STUNTS_MAX(0, min_y);
    max_y = STUNTS_MIN(STUNTS_RENDER_H - 1, max_y);
    if (min_y > max_y) return;

    stunts_color_rgba_t c = r->palette.colors[color_idx];
    uint32_t pixel_val = (c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;

    for (int32_t y = min_y; y <= max_y; y++) {
        int32_t min_x = STUNTS_RENDER_W;
        int32_t max_x = -1;

        for (int i = 0; i < num_pts; i++) {
            int j = (i + 1) % num_pts;
            int32_t y0 = ys[i];
            int32_t y1 = ys[j];
            int32_t x0 = xs[i];
            int32_t x1 = xs[j];

            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                int32_t x_inter = x0 + (((y - y0) * (x1 - x0)) / (y1 - y0));
                if (x_inter < min_x) min_x = x_inter;
                if (x_inter > max_x) max_x = x_inter;
            }
        }

        min_x = STUNTS_MAX(0, min_x);
        max_x = STUNTS_MIN(STUNTS_RENDER_W - 1, max_x);

        if (min_x <= max_x) {
            uint32_t* scanline = r->pixels + (y * STUNTS_RENDER_W);
            uint8_t* scanline_idx = r->indexed + (y * STUNTS_RENDER_W);
            for (int32_t x = min_x; x <= max_x; x++) {
                scanline[x] = pixel_val;
                scanline_idx[x] = color_idx;
            }
        }
    }
}

static inline uint8_t rect_compare_point_320(int32_t px, int32_t py) {
    uint8_t flag = 0;
    if (py < 0) flag |= 1;
    else if (py >= STUNTS_RENDER_H) flag |= 2;
    if (px < 0) flag |= 4;
    else if (px >= STUNTS_RENDER_W) flag |= 8;
    return flag;
}

/* Mechanical TransformedShapeOp Port from Restunts shape3d.c */
static void render_shape_transformed_320(const stunts_shape3d_t* shape,
                                        const stunts_vector_t* pos_rel,
                                        const stunts_vector_t* rot_obj,
                                        const stunts_matrix_t* mat_view) {
    if (!shape || shape->num_verts == 0) return;

    /* 1. Transform Object Center into Camera Space */
    stunts_vector_long_t pos_long = {pos_rel->x, pos_rel->y, pos_rel->z};
    stunts_vector_long_t obj_cam_center;
    stunts_mat_mul_vector_long(&pos_long, mat_view, &obj_cam_center);

    /* 2. Compound Matrix: obj_rot * mat_view */
    stunts_matrix_t obj_rot_mat;
    stunts_mat_rot_zxy(&obj_rot_mat, rot_obj->z, rot_obj->x, rot_obj->y);

    stunts_matrix_t compound_mat;
    stunts_mat_multiply(&obj_rot_mat, mat_view, &compound_mat);

    int32_t screen_x[STUNTS_MAX_SHAPE_VERTS];
    int32_t screen_y[STUNTS_MAX_SHAPE_VERTS];
    int32_t cam_z[STUNTS_MAX_SHAPE_VERTS];
    bool vert_valid[STUNTS_MAX_SHAPE_VERTS];

    /* 3. Transform & Project Vertices using Restunts vector_to_point */
    for (uint16_t i = 0; i < shape->num_verts; i++) {
        stunts_vector_t rot_v;
        stunts_mat_mul_vector(&shape->verts[i], &compound_mat, &rot_v);

        int32_t vx = obj_cam_center.lx + rot_v.x;
        int32_t vy = obj_cam_center.ly + rot_v.y;
        int32_t vz = obj_cam_center.lz + rot_v.z;

        cam_z[i] = vz;

        if (vz <= 16) {
            vert_valid[i] = false;
            continue;
        }

        /* Restunts vector_to_point Perspective Projection */
        screen_x[i] = 160 + (int32_t)(((int64_t)vx * 256) / vz);
        screen_y[i] = 100 - (int32_t)(((int64_t)vy * 213) / vz);
        vert_valid[i] = true;
    }

    /* 4. Process Primitives */
    for (uint16_t p = 0; p < shape->num_prims; p++) {
        if (s_poly_queue_count_320 >= MAX_DRAW_POLYS_320) break;
        const stunts_primitive_t* prim = &shape->prims[p];
        if (prim->num_verts < 2) continue;

        bool all_valid = true;
        int32_t z_sum = 0;
        uint8_t pt_rect_flag = 0x0F;

        for (uint8_t v = 0; v < prim->num_verts; v++) {
            uint8_t vi = prim->vert_indices[v];
            if (vi >= shape->num_verts || !vert_valid[vi]) {
                all_valid = false;
                break;
            }
            z_sum += cam_z[vi];
            pt_rect_flag &= rect_compare_point_320(screen_x[vi], screen_y[vi]);
        }
        if (!all_valid) continue;

        /* Restunts Outcode Culling: Reject if all vertices lie outside the same screen edge */
        if (pt_rect_flag != 0) continue;

        /* Wheel Primitive: 6 verts -> select outer (0..3) or inner (3,4,5,0) face */
        if (prim->type == STUNTS_PRIM_WHEEL && prim->num_verts == 6) {
            uint8_t i0 = prim->vert_indices[0];
            uint8_t i1 = prim->vert_indices[1];
            uint8_t i2 = prim->vert_indices[2];
            int32_t dx0 = screen_x[i0] - screen_x[i1];
            int32_t dx1 = screen_x[i2] - screen_x[i1];
            int32_t dy0 = screen_y[i0] - screen_y[i1];
            int32_t dy1 = screen_y[i2] - screen_y[i1];
            int64_t cross = ((int64_t)dx1 * dy0) - ((int64_t)dx0 * dy1);

            draw_poly_320_t* dpoly = &s_poly_queue_320[s_poly_queue_count_320++];
            dpoly->z_avg = z_sum / 6;
            dpoly->color_idx = prim->color_index;
            dpoly->num_pts = 4;

            if (cross > 0) {
                /* Outer Face (0, 1, 2, 3) */
                for (int v = 0; v < 4; v++) {
                    uint8_t vi = prim->vert_indices[v];
                    dpoly->px[v] = screen_x[vi];
                    dpoly->py[v] = screen_y[vi];
                }
            } else {
                /* Inner Face (3, 4, 5, 0) */
                uint8_t idxs[4] = {prim->vert_indices[3], prim->vert_indices[4], prim->vert_indices[5], prim->vert_indices[0]};
                for (int v = 0; v < 4; v++) {
                    dpoly->px[v] = screen_x[idxs[v]];
                    dpoly->py[v] = screen_y[idxs[v]];
                }
            }
            continue;
        }

        /* Polygon Backface Culling (Exact Restunts is_facing_camera) */
        if (prim->num_verts >= 3) {
            uint8_t i0 = prim->vert_indices[0];
            uint8_t i1 = prim->vert_indices[1];
            uint8_t i2 = prim->vert_indices[2];
            int32_t dx0 = screen_x[i0] - screen_x[i1];
            int32_t dx1 = screen_x[i2] - screen_x[i1];
            int32_t dy0 = screen_y[i0] - screen_y[i1];
            int32_t dy1 = screen_y[i2] - screen_y[i1];
            int64_t cross = ((int64_t)dx1 * dy0) - ((int64_t)dx0 * dy1);
            if (cross <= 0) continue;
        }

        draw_poly_320_t* dpoly = &s_poly_queue_320[s_poly_queue_count_320++];
        dpoly->z_avg = z_sum / prim->num_verts;
        dpoly->color_idx = prim->color_index;
        dpoly->num_pts = prim->num_verts;

        for (uint8_t v = 0; v < prim->num_verts; v++) {
            uint8_t v_idx = prim->vert_indices[v];
            dpoly->px[v] = screen_x[v_idx];
            dpoly->py[v] = screen_y[v_idx];
        }
    }
}

void stunts_render_320_frame(stunts_render_320_t* r, const stunts_sim_context_t* sim_ctx) {
    if (!r || !sim_ctx) return;

    /* 1. Setup Camera matching Restunts frame.c lines 158-253 */
    stunts_vector_t car_pos;
    car_pos.x = (int16_t)sim_ctx->player_state.pos_world.lx;
    car_pos.y = (int16_t)sim_ctx->player_state.pos_world.ly;
    car_pos.z = (int16_t)sim_ctx->player_state.pos_world.lz;

    int16_t car_rot_x = sim_ctx->player_state.rotate.x;
    int16_t car_rot_y = sim_ctx->player_state.rotate.y;
    int16_t car_rot_z = sim_ctx->player_state.rotate.z;

    stunts_vector_t cam_pos;
    int car_rot_x_2 = 0;
    int car_rot_y_2 = 0;
    int car_rot_z_3 = 0;

    if (r->camera_mode == 0) {
        /* Cockpit Camera at eye level */
        stunts_matrix_t car_rot_matrix;
        stunts_mat_rot_zxy(&car_rot_matrix, -car_rot_z, -car_rot_y, -car_rot_x);
        stunts_vector_t offset_vector = {0, (int16_t)(sim_ctx->player_simd.car_height - 6), 0};
        stunts_vector_t car_to_cam_rotated;
        stunts_mat_mul_vector(&offset_vector, &car_rot_matrix, &car_to_cam_rotated);
        cam_pos.x = car_pos.x + car_to_cam_rotated.x;
        cam_pos.y = car_pos.y + car_to_cam_rotated.y;
        cam_pos.z = car_pos.z + car_to_cam_rotated.z;
        car_rot_x_2 = car_rot_x & 0x3FF;
        car_rot_y_2 = car_rot_y & 0x3FF;
        car_rot_z_3 = car_rot_z & 0x3FF;
    } else {
        /* Chase Camera: Mode 2 from Restunts frame.c lines 197-211 */
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

        cam_pos.x = car_pos.x + car_to_cam_rotated.x;
        cam_pos.y = car_pos.y + car_to_cam_rotated.y;
        cam_pos.z = car_pos.z + car_to_cam_rotated.z;

        car_rot_x_2 = (-stunts_polar_angle(car_pos.x - cam_pos.x, car_pos.z - cam_pos.z)) & 0x3FF;
        int var_38 = stunts_polar_radius_2d(car_pos.x - cam_pos.x, car_pos.z - cam_pos.z);
        car_rot_y_2 = stunts_polar_angle(car_pos.y - cam_pos.y + 0x32, var_38) & 0x3FF;
        car_rot_z_3 = 0;
    }

    /* 2. Camera View Matrix via Restunts select_cliprect_rotate */
    stunts_matrix_t view_mat;
    stunts_mat_rot_zxy(&view_mat, -car_rot_z_3, -car_rot_y_2, -car_rot_x_2);

    /* 3. Clear Sky & Ground Horizon */
    uint8_t sky_idx = 0x96; /* Blue Sky */
    uint8_t gnd_idx = 0x6C; /* Green Grass Ground */
    stunts_color_rgba_t sky_c = r->palette.colors[sky_idx];
    stunts_color_rgba_t gnd_c = r->palette.colors[gnd_idx];
    uint32_t sky_val = (sky_c.a << 24) | (sky_c.r << 16) | (sky_c.g << 8) | sky_c.b;
    uint32_t gnd_val = (gnd_c.a << 24) | (gnd_c.r << 16) | (gnd_c.g << 8) | gnd_c.b;

    int32_t horizon_y = 100 + (car_rot_y_2 * 200) / 512;
    horizon_y = STUNTS_MAX(0, STUNTS_MIN(STUNTS_RENDER_H, horizon_y));

    for (int32_t y = 0; y < horizon_y; y++) {
        uint32_t* row = r->pixels + (y * STUNTS_RENDER_W);
        uint8_t* row_idx = r->indexed + (y * STUNTS_RENDER_W);
        for (int32_t x = 0; x < STUNTS_RENDER_W; x++) {
            row[x] = sky_val;
            row_idx[x] = sky_idx;
        }
    }
    for (int32_t y = horizon_y; y < STUNTS_RENDER_H; y++) {
        uint32_t* row = r->pixels + (y * STUNTS_RENDER_W);
        uint8_t* row_idx = r->indexed + (y * STUNTS_RENDER_W);
        for (int32_t x = 0; x < STUNTS_RENDER_W; x++) {
            row[x] = gnd_val;
            row_idx[x] = gnd_idx;
        }
    }

    s_poly_queue_count_320 = 0;

    /* 4. 23-Tile Cone Traversal from Restunts frame.c */
    int cam_tile_east = cam_pos.x >> 10;
    int cam_tile_south = -((cam_pos.z >> 10) - 29);
    int heading_idx = ((car_rot_x_2 & 0x3FF) >> 7);

    for (int si = 22; si >= 0; si--) {
        int tile_east = s_lookahead_tiles_tables[heading_idx][si][0] + cam_tile_east;
        int tile_south = s_lookahead_tiles_tables[heading_idx][si][1] + cam_tile_south;

        if (tile_east < 0 || tile_east > 29 || tile_south < 0 || tile_south > 29) continue;

        uint8_t elem = sim_ctx->track_elements[tile_south * 30 + tile_east];
        if (elem == 0) continue;

        /* Multi-tile filler redirection */
        if (elem == 0xFD) { tile_east--; tile_south--; elem = sim_ctx->track_elements[tile_south * 30 + tile_east]; }
        else if (elem == 0xFE) { tile_south--; elem = sim_ctx->track_elements[tile_south * 30 + tile_east]; }
        else if (elem == 0xFF) { tile_east--; elem = sim_ctx->track_elements[tile_south * 30 + tile_east]; }

        int16_t shape_yaw = 0;
        const char* tag = get_track_shape_tag_320(elem, &shape_yaw);
        if (!tag) continue;

        const stunts_shape3d_t* shape = stunts_shape_db_find(&r->shape_db, tag);
        if (!shape) continue;

        stunts_vector_t tile_pos_rel;
        tile_pos_rel.x = (int16_t)(tile_east * 1024 + 512 - cam_pos.x);
        tile_pos_rel.z = (int16_t)((29 - tile_south) * 1024 + 512 - cam_pos.z);
        tile_pos_rel.y = (int16_t)(sim_ctx->track_heights[tile_south * 30 + tile_east] * 1024 - cam_pos.y);

        stunts_vector_t tile_rot = {0, shape_yaw, 0};
        render_shape_transformed_320(shape, &tile_pos_rel, &tile_rot, &view_mat);
    }

    /* 5. Render Car Body */
    const stunts_shape3d_t* car_shape = stunts_shape_db_find(&r->shape_db, "car0");
    if (car_shape) {
        stunts_vector_t car_rel;
        car_rel.x = (int16_t)(sim_ctx->player_state.pos_world.lx - cam_pos.x);
        car_rel.y = (int16_t)(sim_ctx->player_state.pos_world.ly - cam_pos.y);
        car_rel.z = (int16_t)(sim_ctx->player_state.pos_world.lz - cam_pos.z);
        render_shape_transformed_320(car_shape, &car_rel, &sim_ctx->player_state.rotate, &view_mat);
    }

    /* 6. Render Wheels */
    const stunts_shape3d_t* front_whl = stunts_shape_db_find(&r->shape_db, "car1");
    const stunts_shape3d_t* rear_whl = stunts_shape_db_find(&r->shape_db, "car2");

    if (front_whl || rear_whl) {
        stunts_matrix_t car_mat;
        stunts_mat_rot_zxy(&car_mat, sim_ctx->player_state.rotate.z,
                           sim_ctx->player_state.rotate.x,
                           sim_ctx->player_state.rotate.y);

        for (int w = 0; w < 4; w++) {
            const stunts_shape3d_t* wheel_shape = (w < 2) ? front_whl : rear_whl;
            if (!wheel_shape) wheel_shape = front_whl ? front_whl : rear_whl;
            if (!wheel_shape) continue;

            stunts_vector_t local_whl = sim_ctx->player_simd.wheel_coords[w];
            stunts_vector_t rot_whl;
            stunts_mat_mul_vector(&local_whl, &car_mat, &rot_whl);

            stunts_vector_t whl_rel;
            whl_rel.x = (int16_t)(sim_ctx->player_state.pos_world.lx + rot_whl.x - cam_pos.x);
            whl_rel.y = (int16_t)(sim_ctx->player_state.pos_world.ly + rot_whl.y - cam_pos.y);
            whl_rel.z = (int16_t)(sim_ctx->player_state.pos_world.lz + rot_whl.z - cam_pos.z);

            stunts_vector_t whl_rot = sim_ctx->player_state.rotate;
            if (w < 2) {
                whl_rot.y = (int16_t)((whl_rot.y + sim_ctx->player_state.steering_angle) & 0x3FF);
            }
            render_shape_transformed_320(wheel_shape, &whl_rel, &whl_rot, &view_mat);
        }
    }

    /* 7. Sort Depth Queue Back-to-Front */
    if (s_poly_queue_count_320 > 1) {
        qsort(s_poly_queue_320, s_poly_queue_count_320, sizeof(draw_poly_320_t), compare_draw_polys_320);
    }

    /* 8. Rasterize All Queued Polygons into 320x200 Framebuffer */
    for (uint32_t p = 0; p < s_poly_queue_count_320; p++) {
        const draw_poly_320_t* dp = &s_poly_queue_320[p];
        fill_poly_320(r, dp->num_pts, dp->px, dp->py, dp->color_idx);
    }
}
