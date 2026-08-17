#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sim/stunts_math.h"
#include "asset/stunts_asset_loader.h"

/* Restunts Tables */
static const uint8_t s_primidxcounttab[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0
};
static const uint8_t s_primtypetab[16] = {
    0, 5, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 4, 0, 0
};
static const uint8_t s_material_color_list[128] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x6C, 0x74, 0x0F, 0x1C, 0x1D, 0x0E, 0x1C, 0x1F,
    0x0E, 0xC8, 0xC6, 0xC4, 0x70, 0x72, 0x74, 0xC2,
    0xC5, 0xC8, 0x92, 0x25, 0x23, 0xB5, 0x1D, 0x1F,
    0x13, 0x03, 0x0B, 0x1B, 0x00, 0x04, 0x04, 0x0C,
    0x9C, 0x9A, 0x98, 0x96, 0x2A, 0x28, 0x26, 0x25,
    0x1B, 0x1A, 0x19, 0x18, 0x48, 0x46, 0x44, 0x42,
    0x7B, 0x79, 0x78, 0x75, 0x5C, 0x5A, 0x58, 0x57,
    0xAD, 0xAB, 0xA9, 0xA7, 0x14, 0x13, 0x12, 0x11,
    0x4D, 0x4C, 0x4A, 0x49, 0x2D, 0x2C, 0x2A, 0x29,
    0x9F, 0xAF, 0xAE, 0xAC, 0x1D, 0x1C, 0x12, 0x5A,
    0x0F, 0x07, 0xC8, 0xDB, 0x88, 0x63, 0x65, 0x67,
    0x68, 0x6A, 0x11, 0x14, 0x3C, 0x4D, 0x2E, 0x3D,
    0x2D, 0xCA, 0xBE, 0xBA, 0xB7, 0xB4, 0x00, 0x1C,
    0x1E, 0x10, 0x14, 0x44, 0x36, 0x27, 0x2B, 0x0C
};

typedef struct {
    int16_t x, y, z;
} vec3_t;

typedef struct {
    int px, py;
} pt2_t;

static void analyze_shape(const char* name, const uint8_t* data, uint32_t size,
                          vec3_t cam_pos, vec3_t cam_rot, vec3_t obj_pos, vec3_t obj_rot) {
    printf("================================================================================\n");
    printf(" SHAPE FORENSIC ORACLE: '%s' (Size: %u bytes)\n", name, size);
    printf("================================================================================\n");

    uint8_t num_verts = data[0];
    uint8_t num_prims = data[1];
    uint8_t num_paints = data[2];
    printf("Header: num_verts=%u, num_prims=%u, num_paints=%u\n\n", num_verts, num_prims, num_paints);

    /* 1. Model Vertices */
    printf("--- [1. MODEL-SPACE VERTICES (%u)] ---\n", num_verts);
    vec3_t model_verts[256];
    uint32_t offset = 4;
    for (uint32_t i = 0; i < num_verts; i++) {
        model_verts[i].x = (int16_t)(data[offset] | (data[offset + 1] << 8));
        model_verts[i].y = (int16_t)(data[offset + 2] | (data[offset + 3] << 8));
        model_verts[i].z = (int16_t)(data[offset + 4] | (data[offset + 5] << 8));
        offset += 6;
        printf("  v[%2u]: (%6d, %6d, %6d)\n", i, model_verts[i].x, model_verts[i].y, model_verts[i].z);
    }

    /* Cull tables */
    const uint32_t* cull1_tbl = (const uint32_t*)(data + offset);
    offset += num_prims * 4;
    const uint32_t* cull2_tbl = (const uint32_t*)(data + offset);
    offset += num_prims * 4;

    /* 2. Camera & Transformation Setup */
    stunts_matrix_t view_mat;
    stunts_mat_rot_zxy(&view_mat, -cam_rot.z, -cam_rot.y, -cam_rot.x);

    stunts_vector_long_t pos_rel = {
        obj_pos.x - cam_pos.x,
        obj_pos.y - cam_pos.y,
        obj_pos.z - cam_pos.z
    };
    stunts_vector_long_t obj_cam_center;
    stunts_mat_mul_vector_long(&pos_rel, &view_mat, &obj_cam_center);

    stunts_matrix_t obj_rot_mat;
    stunts_mat_rot_zxy(&obj_rot_mat, obj_rot.z, obj_rot.x, obj_rot.y);
    stunts_matrix_t compound_mat;
    stunts_mat_multiply(&obj_rot_mat, &view_mat, &compound_mat);

    printf("\n--- [2. CAMERA-SPACE TRANSFORMED VERTICES] ---\n");
    vec3_t cam_verts[256];
    pt2_t proj_pts[256];
    bool vert_valid[256];

    for (uint32_t i = 0; i < num_verts; i++) {
        stunts_vector_t sv = {model_verts[i].x, model_verts[i].y, model_verts[i].z};
        stunts_vector_t rot_v;
        stunts_mat_mul_vector(&sv, &compound_mat, &rot_v);

        cam_verts[i].x = obj_cam_center.lx + rot_v.x;
        cam_verts[i].y = obj_cam_center.ly + rot_v.y;
        cam_verts[i].z = obj_cam_center.lz + rot_v.z;

        if (cam_verts[i].z >= 12) {
            vert_valid[i] = true;
            /* Restunts vector_to_point */
            proj_pts[i].px = 160 + (int32_t)(((int64_t)cam_verts[i].x * 256) / cam_verts[i].z);
            proj_pts[i].py = 100 - (int32_t)(((int64_t)cam_verts[i].y * 213) / cam_verts[i].z);
            printf("  v[%2u]: Camera(%6d, %6d, %6d) -> Screen(%4d, %4d)\n",
                   i, cam_verts[i].x, cam_verts[i].y, cam_verts[i].z, proj_pts[i].px, proj_pts[i].py);
        } else {
            vert_valid[i] = false;
            printf("  v[%2u]: Camera(%6d, %6d, %6d) -> CLIPPED (z < 12)\n",
                   i, cam_verts[i].x, cam_verts[i].y, cam_verts[i].z);
        }
    }

    /* 3. Primitives */
    printf("\n--- [3. DECODED PRIMITIVES & PROJECTED 2D POLYGONS (%u)] ---\n", num_prims);
    for (uint32_t p = 0; p < num_prims; p++) {
        uint8_t raw_type = data[offset];
        uint8_t flags = data[offset + 1];
        offset += 2;

        uint8_t raw_mat = (num_paints > 0) ? data[offset] : 0;
        offset += num_paints;

        uint8_t vcount = (raw_type < 16) ? s_primidxcounttab[raw_type] : 0;
        uint8_t resolved_type = (raw_type < 16) ? s_primtypetab[raw_type] : 0;
        uint8_t pal_idx = (raw_mat < 128) ? s_material_color_list[raw_mat] : raw_mat;

        uint8_t v_indices[16];
        for (uint8_t v = 0; v < vcount; v++) {
            v_indices[v] = data[offset++];
        }

        uint32_t c1 = cull1_tbl[p];
        uint32_t c2 = cull2_tbl[p];

        const char* type_name = "POLYGON";
        if (resolved_type == 1) type_name = "LINE";
        else if (resolved_type == 2) type_name = "SPHERE/CYL";
        else if (resolved_type == 3) type_name = "WHEEL";
        else if (resolved_type == 4) type_name = "SOLID_SPHERE";
        else if (resolved_type == 5) type_name = "PIXEL";

        printf("  prim[%2u]: raw_type=%2u (%-12s) flags=0x%02X mat_id=%2u -> pal=0x%02X cull1=0x%08X cull2=0x%08X\n",
               p, raw_type, type_name, flags, raw_mat, pal_idx, c1, c2);

        printf("    verts[%u]: [", vcount);
        for (uint8_t v = 0; v < vcount; v++) {
            printf("%u ", v_indices[v]);
        }
        printf("] -> 2D Projected: ");

        bool all_valid = true;
        for (uint8_t v = 0; v < vcount; v++) {
            uint8_t vi = v_indices[v];
            if (!vert_valid[vi]) {
                all_valid = false;
                break;
            }
        }

        if (!all_valid) {
            printf("CLIPPED\n");
            continue;
        }

        /* Print Projected Vertices */
        for (uint8_t v = 0; v < vcount; v++) {
            uint8_t vi = v_indices[v];
            printf("(%d, %d) ", proj_pts[vi].px, proj_pts[vi].py);
        }

        /* Check Restunts is_facing_camera */
        if (resolved_type == 0 && vcount >= 3) {
            uint8_t i0 = v_indices[0];
            uint8_t i1 = v_indices[1];
            uint8_t i2 = v_indices[2];
            int32_t dx0 = proj_pts[i0].px - proj_pts[i1].px;
            int32_t dx1 = proj_pts[i2].px - proj_pts[i1].px;
            int32_t dy0 = proj_pts[i0].py - proj_pts[i1].py;
            int32_t dy1 = proj_pts[i2].py - proj_pts[i1].py;
            int64_t cross = ((int64_t)dx1 * dy0) - ((int64_t)dx0 * dy1);
            printf("| cross=%lld (%s)", (long long)cross, (cross > 0) ? "FRONT-FACING" : "BACKFACE-CULLED");
        } else if (resolved_type == 3 && vcount == 6) {
            /* Restunts Wheel Primitive Facing */
            pt2_t wheel_front[4] = {proj_pts[v_indices[0]], proj_pts[v_indices[1]], proj_pts[v_indices[2]], proj_pts[v_indices[3]]};
            int32_t dx0 = wheel_front[0].px - wheel_front[1].px;
            int32_t dx1 = wheel_front[2].px - wheel_front[1].px;
            int32_t dy0 = wheel_front[0].py - wheel_front[1].py;
            int32_t dy1 = wheel_front[2].py - wheel_front[1].py;
            int64_t cross = ((int64_t)dx1 * dy0) - ((int64_t)dx0 * dy1);
            printf("| WHEEL %s FACE DRAWN", (cross > 0) ? "OUTER (0..3)" : "INNER (3,4,5,0)");
        }
        printf("\n");
    }
    printf("\n");
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "extracted/stunts/stunts";

    char path[512];
    snprintf(path, sizeof(path), "%s/GAME1.P3S", data_dir);
    stunts_res_archive_t* arc_game1 = stunts_asset_load_archive(path);
    if (!arc_game1) return 1;

    /* Frame 0 Test Conditions */
    vec3_t cam_pos = {26112, 98, 27321};
    vec3_t cam_rot = {983, 512, 0};

    /* 1. Test Road Tile at (25, 4): World Pos (26112, 0, 26112) */
    vec3_t road_pos = {26112, 0, 26112};
    vec3_t road_rot = {0, 0, 0};

    const stunts_sub_resource_t* res_road = stunts_asset_find_resource(arc_game1, "road");
    if (res_road) {
        analyze_shape("road", res_road->data, res_road->size, cam_pos, cam_rot, road_pos, road_rot);
    }

    /* 2. Test Car0 from STCOUN.P3S */
    snprintf(path, sizeof(path), "%s/STCOUN.P3S", data_dir);
    stunts_res_archive_t* arc_coun = stunts_asset_load_archive(path);
    if (arc_coun) {
        const stunts_sub_resource_t* res_car0 = stunts_asset_find_resource(arc_coun, "car0");
        if (res_car0) {
            vec3_t car_pos = {26112, 0, 27136};
            vec3_t car_rot = {0, 768, 0};
            analyze_shape("car0", res_car0->data, res_car0->size, cam_pos, cam_rot, car_pos, car_rot);
        }
        stunts_asset_free_archive(arc_coun);
    }

    stunts_asset_free_archive(arc_game1);
    return 0;
}
