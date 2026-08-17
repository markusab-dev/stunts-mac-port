#ifndef STUNTS_ASSET_LOADER_H
#define STUNTS_ASSET_LOADER_H

#include "../common/stunts_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char tag[5];
    uint32_t offset;
    uint32_t size;
    const uint8_t* data;
} stunts_sub_resource_t;

typedef struct {
    uint8_t* raw_unpacked_data;
    uint32_t total_size;
    uint16_t num_resources;
    stunts_sub_resource_t* resources;
    bool owns_data;   /* false for a view adopted over someone else's buffer */
} stunts_res_archive_t;

/**
 * Loads and decompresses a .PRE, .RES, .P3S, or .PVS file into a structured archive.
 */
stunts_res_archive_t* stunts_asset_load_archive(const char* filepath);

/**
 * Builds an archive view over an already-unpacked buffer, without reading a
 * file. The game itself duplicates a loaded archive with a plain byte copy
 * (shape3d_load_car_shapes does this when the opponent drives the same car as
 * the player), and the copy has to be searchable just like the original.
 * With take_ownership = false the caller keeps the buffer and must outlive
 * the view. Returns NULL if the bytes carry no usable resource table.
 */
stunts_res_archive_t* stunts_asset_adopt_archive(uint8_t* data, uint32_t len,
                                                 bool take_ownership);
void stunts_asset_free_archive(stunts_res_archive_t* archive);
const stunts_sub_resource_t* stunts_asset_find_resource(const stunts_res_archive_t* archive, const char* tag);

/**
 * Loads a 1,802-byte track file (.TRK) into 30x30 element and height grids.
 */
bool stunts_load_track(const char* filepath, uint8_t elements[900], uint8_t heights[900]);

/**
 * Loads a replay file (.RPL).
 */
bool stunts_load_replay(const char* filepath, stunts_game_info_t* out_info, uint8_t** out_inputs, uint16_t* out_input_count);

/**
 * Loads and parses a car's SIMD physics parameter block from ST<ID>.P3S.
 */
bool stunts_load_car_simd(const char* car_p3s_path, stunts_simd_t* out_simd);

/**
 * Loads the collision planes (plan) and barriers (wall) from GAME.PRE.
 */
bool stunts_load_collision_data(const char* game_pre_path, stunts_plane_t** out_planes, uint16_t* out_plane_count);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_ASSET_LOADER_H */
