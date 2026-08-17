#include "stunts_shape3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t s_prim_idx_counts[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0
};

/* Restunts material_color_list mapping from Material ID -> VGA Palette Index */
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

bool stunts_shape_parse(const char* name, const uint8_t* data, uint32_t size, stunts_shape3d_t* out_shape) {
    if (!name || !data || size < 4 || !out_shape) return false;
    memset(out_shape, 0, sizeof(stunts_shape3d_t));

    strncpy(out_shape->name, name, 7);
    out_shape->name[7] = '\0';

    uint8_t num_verts = data[0];
    uint8_t num_prims = data[1];
    uint8_t num_paints = data[2];

    out_shape->num_verts = num_verts;
    out_shape->num_prims = num_prims;
    out_shape->num_paints = num_paints;

    uint32_t offset = 4;

    /* 1. Parse Vertices */
    for (uint32_t i = 0; i < num_verts && i < STUNTS_MAX_SHAPE_VERTS; i++) {
        if (offset + 6 > size) break;
        int16_t vx = (int16_t)(data[offset] | (data[offset + 1] << 8));
        int16_t vy = (int16_t)(data[offset + 2] | (data[offset + 3] << 8));
        int16_t vz = (int16_t)(data[offset + 4] | (data[offset + 5] << 8));
        offset += 6;

        out_shape->verts[i].x = vx;
        out_shape->verts[i].y = vy;
        out_shape->verts[i].z = vz;
    }

    /* Skip normal/cull tables (cull1: 4 bytes/prim, cull2: 4 bytes/prim = 8 bytes/prim) */
    uint32_t cull_bytes = (uint32_t)num_prims * 8;
    offset += cull_bytes;

    /* 2. Parse Primitives */
    for (uint32_t p = 0; p < num_prims && p < STUNTS_MAX_SHAPE_PRIMS; p++) {
        if (offset + 2 > size) break;
        uint8_t prim_type = data[offset];
        // uint8_t flags = data[offset + 1];
        offset += 2;

        /* Paint job color table */
        uint8_t raw_mat = 0;
        if (num_paints > 0 && offset < size) {
            raw_mat = data[offset];
            offset += num_paints;
        }

        uint8_t vcount = 0;
        if (prim_type < 16) {
            vcount = s_prim_idx_counts[prim_type];
        }

        uint8_t pal_idx = (raw_mat < 128) ? s_material_color_list[raw_mat] : raw_mat;

        out_shape->prims[p].type = (prim_type <= 2) ? prim_type : STUNTS_PRIM_POLYGON;
        out_shape->prims[p].color_index = pal_idx;
        out_shape->prims[p].num_verts = vcount;
        out_shape->prims[p].backface_cull = false; /* Disable aggressive backface culling */

        for (uint8_t v = 0; v < vcount && v < STUNTS_MAX_POLY_VERTS; v++) {
            if (offset < size) {
                out_shape->prims[p].vert_indices[v] = data[offset++];
            }
        }
    }

    return true;
}

bool stunts_shape_db_load(const char* data_dir, const char* car_id, stunts_shape_db_t* out_db) {
    if (!data_dir || !out_db) return false;
    memset(out_db, 0, sizeof(stunts_shape_db_t));

    char path[512];
    const char* archives[3] = { "GAME1.P3S", "GAME2.P3S", NULL };

    char car_p3s[64];
    if (car_id && car_id[0] != '\0') {
        snprintf(car_p3s, sizeof(car_p3s), "ST%s.P3S", car_id);
        archives[2] = car_p3s;
    }

    for (int a = 0; a < 3; a++) {
        if (!archives[a]) continue;
        snprintf(path, sizeof(path), "%s/%s", data_dir, archives[a]);
        stunts_res_archive_t* arc = stunts_asset_load_archive(path);
        if (!arc) continue;

        for (uint16_t r = 0; r < arc->num_resources; r++) {
            if (out_db->count >= 160) break;
            const stunts_sub_resource_t* sub = &arc->resources[r];
            stunts_shape3d_t* shape = &out_db->shapes[out_db->count];
            if (stunts_shape_parse(sub->tag, sub->data, sub->size, shape)) {
                out_db->count++;
            }
        }
        stunts_asset_free_archive(arc);
    }

    return (out_db->count > 0);
}

const stunts_shape3d_t* stunts_shape_db_find(const stunts_shape_db_t* db, const char* name) {
    if (!db || !name) return NULL;
    for (uint16_t i = 0; i < db->count; i++) {
        if (strncasecmp(db->shapes[i].name, name, 4) == 0) {
            return &db->shapes[i];
        }
    }
    return NULL;
}
