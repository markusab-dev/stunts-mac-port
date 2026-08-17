#include "stunts_rasterizer.h"
#include "../sim/stunts_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef STUNTS_MIN
#define STUNTS_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef STUNTS_MAX
#define STUNTS_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define NEAR_PLANE_Z 16.0f

typedef struct {
    float x, y, z;
} vec3f_t;

typedef struct {
    float x, y;
} vec2f_t;

typedef struct {
    float z_avg;
    uint8_t color_idx;
    uint8_t num_pts;
    vec2f_t pts[STUNTS_MAX_POLY_VERTS + 4];
} modern_draw_poly_t;

#define MAX_MODERN_POLYS 4096
static modern_draw_poly_t s_modern_poly_queue[MAX_MODERN_POLYS];
static uint32_t s_modern_poly_count = 0;

static const char* get_track_shape_tag(uint8_t elem_id, int16_t* out_yaw) {
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
        case 0x1B: *out_yaw = 0;   return "brid";
        case 0x1C: *out_yaw = 256; return "brid";
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

static int compare_modern_polys(const void* a, const void* b) {
    const modern_draw_poly_t* pa = (const modern_draw_poly_t*)a;
    const modern_draw_poly_t* pb = (const modern_draw_poly_t*)b;
    if (pb->z_avg > pa->z_avg) return 1;
    if (pb->z_avg < pa->z_avg) return -1;
    return 0;
}

bool stunts_rasterizer_init(stunts_rasterizer_t* r, int32_t width, int32_t height, const char* data_dir, const char* car_id) {
    if (!r || width <= 0 || height <= 0) return false;
    memset(r, 0, sizeof(stunts_rasterizer_t));

    r->width = width;
    r->height = height;
    r->pitch = width * 4;
    r->pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!r->pixels) return false;

    r->viewport.x = 0;
    r->viewport.y = 0;
    r->viewport.width = width;
    r->viewport.height = height;
    r->viewport.aspect_ratio = (float)width / (float)height;

    char sdmain_path[512];
    snprintf(sdmain_path, sizeof(sdmain_path), "%s/SDMAIN.PVS", data_dir);
    if (!stunts_palette_load(sdmain_path, &r->palette)) {
        stunts_palette_init_default(&r->palette);
    }

    stunts_shape_db_load(data_dir, car_id, &r->shape_db);
    return true;
}

void stunts_rasterizer_cleanup(stunts_rasterizer_t* r) {
    if (!r) return;
    free(r->pixels);
    r->pixels = NULL;
}

bool stunts_rasterizer_resize(stunts_rasterizer_t* r, int32_t new_width, int32_t new_height) {
    if (!r || new_width <= 0 || new_height <= 0) return false;
    if (r->width == new_width && r->height == new_height && r->pixels != NULL) return true;

    uint32_t* new_pix = (uint32_t*)realloc(r->pixels, new_width * new_height * sizeof(uint32_t));
    if (!new_pix) return false;

    r->pixels = new_pix;
    r->width = new_width;
    r->height = new_height;
    r->pitch = new_width * 4;

    r->viewport.x = 0;
    r->viewport.y = 0;
    r->viewport.width = new_width;
    r->viewport.height = new_height;
    r->viewport.aspect_ratio = (float)new_width / (float)new_height;
    return true;
}

void stunts_rasterizer_clear_sky_ground(stunts_rasterizer_t* r, const stunts_camera_t* cam) {
    if (!r || !r->pixels) return;

    uint8_t sky_idx = 0x96; /* Classic Sky Blue */
    uint8_t gnd_idx = 0x6C; /* Classic Forest Grass Green */

    stunts_color_rgba_t sky_c = r->palette.colors[sky_idx];
    stunts_color_rgba_t gnd_c = r->palette.colors[gnd_idx];

    uint32_t sky_val = (sky_c.a << 24) | (sky_c.r << 16) | (sky_c.g << 8) | sky_c.b;
    uint32_t gnd_val = (gnd_c.a << 24) | (gnd_c.r << 16) | (gnd_c.g << 8) | gnd_c.b;

    int32_t horizon_y = (r->height / 2);
    if (cam) {
        /* 10-bit signed pitch angle (-512 .. +511) */
        int16_t pitch = (int16_t)((cam->rot.x + 512) & 0x3FF) - 512;
        horizon_y += (pitch * r->height) / 512;
    }
    horizon_y = STUNTS_MAX(0, STUNTS_MIN(r->height, horizon_y));

    for (int32_t y = 0; y < horizon_y; y++) {
        uint32_t* row = r->pixels + (y * r->width);
        for (int32_t x = 0; x < r->width; x++) {
            row[x] = sky_val;
        }
    }

    for (int32_t y = horizon_y; y < r->height; y++) {
        uint32_t* row = r->pixels + (y * r->width);
        for (int32_t x = 0; x < r->width; x++) {
            row[x] = gnd_val;
        }
    }
}

/* High-Precision Scanline Polygon Rasterizer */
void stunts_rasterizer_fill_poly2d(stunts_rasterizer_t* r,
                                  int num_pts,
                                  const int32_t* xs,
                                  const int32_t* ys,
                                  uint8_t color_idx) {
    if (!r || !r->pixels || num_pts < 3) return;

    int32_t min_y = ys[0];
    int32_t max_y = ys[0];
    for (int i = 1; i < num_pts; i++) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    min_y = STUNTS_MAX(0, min_y);
    max_y = STUNTS_MIN(r->height - 1, max_y);
    if (min_y > max_y) return;

    stunts_color_rgba_t c = r->palette.colors[color_idx];
    uint32_t pixel_val = (c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;

    for (int32_t y = min_y; y <= max_y; y++) {
        int32_t min_x = r->width;
        int32_t max_x = -1;

        for (int i = 0; i < num_pts; i++) {
            int j = (i + 1) % num_pts;
            int32_t y0 = ys[i];
            int32_t y1 = ys[j];
            int32_t x0 = xs[i];
            int32_t x1 = xs[j];

            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                int64_t num = (int64_t)(y - y0) * (x1 - x0);
                int64_t den = (y1 - y0);
                int32_t x_inter = x0 + (int32_t)(num / den);
                if (x_inter < min_x) min_x = x_inter;
                if (x_inter > max_x) max_x = x_inter;
            }
        }

        min_x = STUNTS_MAX(0, min_x);
        max_x = STUNTS_MIN(r->width - 1, max_x);

        if (min_x <= max_x) {
            uint32_t* scanline = r->pixels + (y * r->width);
            for (int32_t x = min_x; x <= max_x; x++) {
                scanline[x] = pixel_val;
            }
        }
    }
}

/* 3D Near-Plane Polygon Clipper (Sutherland-Hodgman) */
static int clip_polygon_near_plane(int in_count, const vec3f_t* in_v, vec3f_t* out_v) {
    int out_count = 0;
    for (int i = 0; i < in_count; i++) {
        int j = (i + 1) % in_count;
        const vec3f_t* v1 = &in_v[i];
        const vec3f_t* v2 = &in_v[j];

        bool v1_inside = (v1->z >= NEAR_PLANE_Z);
        bool v2_inside = (v2->z >= NEAR_PLANE_Z);

        if (v1_inside && v2_inside) {
            out_v[out_count++] = *v2;
        } else if (v1_inside && !v2_inside) {
            float t = (NEAR_PLANE_Z - v1->z) / (v2->z - v1->z);
            out_v[out_count].x = v1->x + t * (v2->x - v1->x);
            out_v[out_count].y = v1->y + t * (v2->y - v1->y);
            out_v[out_count].z = NEAR_PLANE_Z;
            out_count++;
        } else if (!v1_inside && v2_inside) {
            float t = (NEAR_PLANE_Z - v1->z) / (v2->z - v1->z);
            out_v[out_count].x = v1->x + t * (v2->x - v1->x);
            out_v[out_count].y = v1->y + t * (v2->y - v1->y);
            out_v[out_count].z = NEAR_PLANE_Z;
            out_count++;
            out_v[out_count++] = *v2;
        }
    }
    return out_count;
}

static void render_shape_transformed_modern(stunts_rasterizer_t* r,
                                           const stunts_shape3d_t* shape,
                                           const stunts_vector_long_t* pos_rel,
                                           const stunts_vector_t* rot_obj,
                                           const stunts_matrix_t* mat_view) {
    if (!shape || shape->num_verts == 0) return;

    r->stats.objects_submitted++;
    r->stats.vertices_transformed += shape->num_verts;
    r->stats.polygons_submitted += shape->num_prims;

    /* 1. Transform Object Center into Camera Space */
    stunts_vector_long_t obj_cam_center;
    stunts_mat_mul_vector_long(pos_rel, mat_view, &obj_cam_center);

    /* 2. Compound Matrix: obj_rot * mat_view */
    stunts_matrix_t obj_rot_mat;
    stunts_mat_rot_zxy(&obj_rot_mat, rot_obj->z, rot_obj->x, rot_obj->y);
    stunts_matrix_t compound_mat;
    stunts_mat_multiply(&obj_rot_mat, mat_view, &compound_mat);

    /* 3. Transform All Model Vertices into Camera Space */
    vec3f_t cam_verts[STUNTS_MAX_SHAPE_VERTS];
    for (uint16_t i = 0; i < shape->num_verts; i++) {
        stunts_vector_t rot_v;
        stunts_mat_mul_vector(&shape->verts[i], &compound_mat, &rot_v);
        cam_verts[i].x = (float)(obj_cam_center.lx + rot_v.x);
        cam_verts[i].y = (float)(obj_cam_center.ly + rot_v.y);
        cam_verts[i].z = (float)(obj_cam_center.lz + rot_v.z);
    }

    float fov_x = (float)r->width * (256.0f / 320.0f);
    float fov_y = (float)r->height * (213.0f / 200.0f);
    float cx = (float)r->width * 0.5f;
    float cy = (float)r->height * 0.5f;

    /* 4. Process Each Primitive */
    for (uint16_t p = 0; p < shape->num_prims; p++) {
        if (s_modern_poly_count >= MAX_MODERN_POLYS) break;
        const stunts_primitive_t* prim = &shape->prims[p];
        if (prim->num_verts < 2) continue;

        vec3f_t poly_3d[STUNTS_MAX_POLY_VERTS];
        int num_v = 0;

        /* Wheel Primitive: 6 verts -> select outer or inner face */
        if (prim->type == STUNTS_PRIM_WHEEL && prim->num_verts == 6) {
            uint8_t i0 = prim->vert_indices[0];
            uint8_t i1 = prim->vert_indices[1];
            uint8_t i2 = prim->vert_indices[2];
            float dx0 = cam_verts[i0].x - cam_verts[i1].x;
            float dx1 = cam_verts[i2].x - cam_verts[i1].x;
            float dy0 = cam_verts[i0].y - cam_verts[i1].y;
            float dy1 = cam_verts[i2].y - cam_verts[i1].y;
            float cross = (dx1 * dy0) - (dx0 * dy1);

            uint8_t idxs[4];
            if (cross > 0) {
                idxs[0] = prim->vert_indices[0]; idxs[1] = prim->vert_indices[1];
                idxs[2] = prim->vert_indices[2]; idxs[3] = prim->vert_indices[3];
            } else {
                idxs[0] = prim->vert_indices[3]; idxs[1] = prim->vert_indices[4];
                idxs[2] = prim->vert_indices[5]; idxs[3] = prim->vert_indices[0];
            }
            for (int v = 0; v < 4; v++) poly_3d[v] = cam_verts[idxs[v]];
            num_v = 4;
        } else {
            for (uint8_t v = 0; v < prim->num_verts; v++) {
                uint8_t vi = prim->vert_indices[v];
                if (vi < shape->num_verts) poly_3d[num_v++] = cam_verts[vi];
            }
        }

        if (num_v < 3) continue;

        /* 5. 3D Near-Plane Polygon Clipping */
        vec3f_t clipped_3d[STUNTS_MAX_POLY_VERTS + 4];
        int clipped_count = clip_polygon_near_plane(num_v, poly_3d, clipped_3d);
        if (clipped_count < 3) {
            r->stats.near_clipped++;
            continue;
        }

        /* 6. Project to Screen Space */
        vec2f_t proj_2d[STUNTS_MAX_POLY_VERTS + 4];
        float z_sum = 0.0f;
        float min_px = (float)r->width, max_px = 0.0f;
        float min_py = (float)r->height, max_py = 0.0f;

        for (int v = 0; v < clipped_count; v++) {
            float inv_z = 1.0f / clipped_3d[v].z;
            proj_2d[v].x = cx + (clipped_3d[v].x * fov_x * inv_z);
            proj_2d[v].y = cy - (clipped_3d[v].y * fov_y * inv_z);
            z_sum += clipped_3d[v].z;

            if (proj_2d[v].x < min_px) min_px = proj_2d[v].x;
            if (proj_2d[v].x > max_px) max_px = proj_2d[v].x;
            if (proj_2d[v].y < min_py) min_py = proj_2d[v].y;
            if (proj_2d[v].y > max_py) max_py = proj_2d[v].y;
        }

        /* 7. Screen Viewport Outcode Rejection */
        if (max_px < 0 || min_px >= (float)r->width || max_py < 0 || min_py >= (float)r->height) {
            r->stats.screen_clipped++;
            continue;
        }

        /* 8. Backface Culling */
        float dx0 = proj_2d[0].x - proj_2d[1].x;
        float dx1 = proj_2d[2].x - proj_2d[1].x;
        float dy0 = proj_2d[0].y - proj_2d[1].y;
        float dy1 = proj_2d[2].y - proj_2d[1].y;
        float cross = (dx1 * dy0) - (dx0 * dy1);
        if (cross <= 0.0f) {
            r->stats.backface_culled++;
            continue;
        }

        /* 9. Queue for Depth Sorting */
        modern_draw_poly_t* dpoly = &s_modern_poly_queue[s_modern_poly_count++];
        dpoly->z_avg = z_sum / (float)clipped_count;
        dpoly->color_idx = prim->color_index;
        dpoly->num_pts = (uint8_t)clipped_count;
        for (int v = 0; v < clipped_count; v++) {
            dpoly->pts[v] = proj_2d[v];
        }
    }
}

void stunts_rasterizer_render_scene(stunts_rasterizer_t* r,
                                   const stunts_sim_context_t* sim_ctx,
                                   const stunts_camera_t* cam) {
    if (!r || !sim_ctx || !cam) return;

    memset(&r->stats, 0, sizeof(r->stats));
    stunts_rasterizer_clear_sky_ground(r, cam);
    s_modern_poly_count = 0;

    /* 1. Camera View Matrix */
    stunts_matrix_t view_mat;
    stunts_mat_rot_zxy(&view_mat, -cam->rot.z, -cam->rot.x, -cam->rot.y);

    int cam_tile_east = (int)(cam->pos.lx / 1024);
    int cam_tile_south = (int)(29 - (cam->pos.lz / 1024));

    /* 2. Traverse all Track & Terrain Tiles within visibility radius (24x24 tiles) */
    int min_ts = STUNTS_MAX(0, cam_tile_south - 12);
    int max_ts = STUNTS_MIN(29, cam_tile_south + 12);
    int min_te = STUNTS_MAX(0, cam_tile_east - 12);
    int max_te = STUNTS_MIN(29, cam_tile_east + 12);

    for (int ts = min_ts; ts <= max_ts; ts++) {
        for (int te = min_te; te <= max_te; te++) {
            uint8_t elem = sim_ctx->track_elements[ts * 30 + te];
            if (elem == 0) continue;

            /* Multi-tile filler redirection */
            if (elem == 0xFD) { te--; ts--; elem = sim_ctx->track_elements[ts * 30 + te]; }
            else if (elem == 0xFE) { ts--; elem = sim_ctx->track_elements[ts * 30 + te]; }
            else if (elem == 0xFF) { te--; elem = sim_ctx->track_elements[ts * 30 + te]; }

            int16_t shape_yaw = 0;
            const char* tag = get_track_shape_tag(elem, &shape_yaw);
            if (!tag) continue;

            const stunts_shape3d_t* shape = stunts_shape_db_find(&r->shape_db, tag);
            if (!shape) continue;

            stunts_vector_long_t pos_rel;
            pos_rel.lx = (int32_t)te * 1024 + 512 - cam->pos.lx;
            pos_rel.lz = (int32_t)(29 - ts) * 1024 + 512 - cam->pos.lz;
            pos_rel.ly = (int32_t)sim_ctx->track_heights[ts * 30 + te] * 1024 - cam->pos.ly;

            stunts_vector_t tile_rot = {0, shape_yaw, 0};
            render_shape_transformed_modern(r, shape, &pos_rel, &tile_rot, &view_mat);
        }
    }

    /* 3. Render Player Vehicle Chassis (car0) */
    const stunts_shape3d_t* car_shape = stunts_shape_db_find(&r->shape_db, "car0");
    if (car_shape) {
        stunts_vector_long_t car_rel;
        car_rel.lx = sim_ctx->player_state.pos_world.lx - cam->pos.lx;
        car_rel.ly = sim_ctx->player_state.pos_world.ly - cam->pos.ly;
        car_rel.lz = sim_ctx->player_state.pos_world.lz - cam->pos.lz;
        render_shape_transformed_modern(r, car_shape, &car_rel, &sim_ctx->player_state.rotate, &view_mat);
    }

    /* 4. Render 4 Wheels (car1 front, car2 rear) */
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

            stunts_vector_long_t whl_rel;
            whl_rel.lx = sim_ctx->player_state.pos_world.lx + rot_whl.x - cam->pos.lx;
            whl_rel.ly = sim_ctx->player_state.pos_world.ly + rot_whl.y - cam->pos.ly;
            whl_rel.lz = sim_ctx->player_state.pos_world.lz + rot_whl.z - cam->pos.lz;

            stunts_vector_t whl_rot = sim_ctx->player_state.rotate;
            if (w < 2) {
                whl_rot.y = (int16_t)((whl_rot.y + sim_ctx->player_state.steering_angle) & 0x3FF);
            }
            render_shape_transformed_modern(r, wheel_shape, &whl_rel, &whl_rot, &view_mat);
        }
    }

    /* 5. Sort All Drawn Polygons Back-to-Front */
    if (s_modern_poly_count > 1) {
        qsort(s_modern_poly_queue, s_modern_poly_count, sizeof(modern_draw_poly_t), compare_modern_polys);
    }

    /* 6. Rasterize Polygons into High-Resolution Framebuffer */
    for (uint32_t p = 0; p < s_modern_poly_count; p++) {
        const modern_draw_poly_t* dp = &s_modern_poly_queue[p];
        int32_t xs[STUNTS_MAX_POLY_VERTS + 4];
        int32_t ys[STUNTS_MAX_POLY_VERTS + 4];
        for (uint8_t v = 0; v < dp->num_pts; v++) {
            xs[v] = (int32_t)roundf(dp->pts[v].x);
            ys[v] = (int32_t)roundf(dp->pts[v].y);
        }
        stunts_rasterizer_fill_poly2d(r, dp->num_pts, xs, ys, dp->color_idx);
        r->stats.polygons_rasterized++;
    }
}
