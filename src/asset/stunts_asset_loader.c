#include "stunts_asset_loader.h"
#include "stunts_dsi_unpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

stunts_res_archive_t* stunts_asset_load_archive(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return NULL;
    }

    uint8_t* raw_file = (uint8_t*)malloc(file_size);
    if (!raw_file) {
        fclose(f);
        return NULL;
    }

    if (fread(raw_file, 1, file_size, f) != (size_t)file_size) {
        free(raw_file);
        fclose(f);
        return NULL;
    }
    fclose(f);

    uint32_t decomp_size = stunts_dsi_get_decompressed_size(raw_file, file_size);
    uint8_t* unpacked_data = NULL;
    uint32_t unpacked_len = 0;

    if (decomp_size > 0 && decomp_size < 20 * 1024 * 1024) {
        unpacked_data = (uint8_t*)malloc(decomp_size + 4096);
        if (unpacked_data) {
            unpacked_len = stunts_dsi_decompress(raw_file, file_size, unpacked_data, decomp_size + 4096);
        }
    }

    if (!unpacked_data || unpacked_len == 0) {
        /* Raw uncompressed archive */
        free(unpacked_data);
        unpacked_data = raw_file;
        unpacked_len = (uint32_t)file_size;
    } else {
        free(raw_file);
    }

    return stunts_asset_adopt_archive(unpacked_data, unpacked_len, true);
}

stunts_res_archive_t* stunts_asset_adopt_archive(uint8_t* unpacked_data,
                                                 uint32_t unpacked_len,
                                                 bool take_ownership) {
    if (!unpacked_data) return NULL;

    stunts_res_archive_t* arc = (stunts_res_archive_t*)calloc(1, sizeof(stunts_res_archive_t));
    if (!arc) {
        if (take_ownership) free(unpacked_data);
        return NULL;
    }

    arc->raw_unpacked_data = unpacked_data;
    arc->total_size = unpacked_len;
    arc->owns_data = take_ownership;

    if (unpacked_len >= 6) {
        uint16_t num_res = (uint16_t)unpacked_data[4] | ((uint16_t)unpacked_data[5] << 8);
        if (num_res > 0 && (6 + num_res * 8) <= unpacked_len) {
            arc->num_resources = num_res;
            arc->resources = (stunts_sub_resource_t*)calloc(num_res, sizeof(stunts_sub_resource_t));

            const uint8_t* tag_ptr = unpacked_data + 6;
            const uint8_t* off_ptr = unpacked_data + 6 + (num_res * 4);
            const uint8_t* data_base = unpacked_data + 6 + (num_res * 8);

            for (uint16_t i = 0; i < num_res; i++) {
                memcpy(arc->resources[i].tag, tag_ptr + (i * 4), 4);
                arc->resources[i].tag[4] = '\0';

                uint32_t offset = (uint32_t)off_ptr[i * 4] |
                                  ((uint32_t)off_ptr[i * 4 + 1] << 8) |
                                  ((uint32_t)off_ptr[i * 4 + 2] << 16) |
                                  ((uint32_t)off_ptr[i * 4 + 3] << 24);

                arc->resources[i].offset = offset;
                if ((size_t)(data_base + offset - unpacked_data) < unpacked_len) {
                    arc->resources[i].data = data_base + offset;
                }
            }

            /* Calculate sub-resource sizes */
            for (uint16_t i = 0; i < num_res; i++) {
                if (i + 1 < num_res) {
                    arc->resources[i].size = arc->resources[i + 1].offset - arc->resources[i].offset;
                } else {
                    arc->resources[i].size = unpacked_len - (uint32_t)(data_base + arc->resources[i].offset - unpacked_data);
                }
            }
        }
    }

    return arc;
}

void stunts_asset_free_archive(stunts_res_archive_t* archive) {
    if (!archive) return;
    free(archive->resources);
    if (archive->owns_data) free(archive->raw_unpacked_data);
    free(archive);
}

const stunts_sub_resource_t* stunts_asset_find_resource(const stunts_res_archive_t* archive, const char* tag) {
    if (!archive || !tag) return NULL;
    for (uint16_t i = 0; i < archive->num_resources; i++) {
        if (strncasecmp(archive->resources[i].tag, tag, 4) == 0) {
            return &archive->resources[i];
        }
    }
    return NULL;
}

bool stunts_load_track(const char* filepath, uint8_t elements[900], uint8_t heights[900]) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return false;

    uint8_t buffer[1802];
    if (fread(buffer, 1, 1802, f) != 1802) {
        fclose(f);
        return false;
    }
    fclose(f);

    /* A .TRK is two 0x385-byte (901) maps, not two 900-byte ones: that is the
     * layout init_trackdata() carves out (td14 -> +0x385 -> td15), and it is
     * why file_load_replay() can read a replay straight into td13 and have the
     * maps land in place. 900 of each 901 are the 30x30 grid; the last byte is
     * padding. Reading the heights from +900 shifted every hill one tile,
     * leaving road pieces and terrain misaligned. */
    memcpy(elements, buffer, 900);
    memcpy(heights, buffer + 901, 900);
    return true;
}

bool stunts_load_replay(const char* filepath, stunts_game_info_t* out_info, uint8_t** out_inputs, uint16_t* out_input_count) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return false;

    uint8_t header[26];
    if (fread(header, 1, 26, f) != 26) {
        fclose(f);
        return false;
    }

    memcpy(out_info->player_car_id, header, 4);
    out_info->player_car_id[4] = '\0';
    out_info->player_material = header[4];
    out_info->player_transmission = header[5];
    out_info->opponent_type = header[6];
    memcpy(out_info->opponent_car_id, header + 7, 4);
    out_info->opponent_car_id[4] = '\0';
    out_info->opponent_material = header[11];
    out_info->opponent_transmission = header[12];
    memcpy(out_info->track_name, header + 13, 9);
    out_info->track_name[9] = '\0';
    out_info->frames_per_sec = (uint16_t)header[22] | ((uint16_t)header[23] << 8);
    out_info->recorded_frames = (uint16_t)header[24] | ((uint16_t)header[25] << 8);

    uint16_t frame_count = out_info->recorded_frames;
    uint8_t* inputs = (uint8_t*)malloc(frame_count + 1);
    if (!inputs) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(inputs, 1, frame_count, f);
    if (read_bytes < frame_count) {
        out_info->recorded_frames = (uint16_t)read_bytes;
    }
    fclose(f);

    *out_inputs = inputs;
    *out_input_count = out_info->recorded_frames;
    return true;
}

bool stunts_load_car_simd(const char* car_path, stunts_simd_t* out_simd) {
    char resolved_path[256];
    strcpy(resolved_path, car_path);

    /* If given ST<ID>.P3S or just CAR ID, map to CAR<ID>.RES */
    char* ext = strrchr(resolved_path, '.');
    if (ext && strcasecmp(ext, ".p3s") == 0) {
        char* fname = strrchr(resolved_path, '/');
        if (!fname) fname = resolved_path;
        else fname++;
        if (strncasecmp(fname, "ST", 2) == 0) {
            char car_id[5];
            strncpy(car_id, fname + 2, 4);
            car_id[4] = '\0';
            *fname = '\0';
            strcat(resolved_path, "CAR");
            strcat(resolved_path, car_id);
            strcat(resolved_path, ".RES");
        }
    }

    stunts_res_archive_t* arc = stunts_asset_load_archive(resolved_path);
    if (!arc) return false;

    const stunts_sub_resource_t* simd_res = stunts_asset_find_resource(arc, "simd");
    if (!simd_res || !simd_res->data) {
        stunts_asset_free_archive(arc);
        return false;
    }

    const uint8_t* p = simd_res->data;
    memset(out_simd, 0, sizeof(stunts_simd_t));

    out_simd->num_gears = p[0];
    out_simd->simd_unk = p[1];
    out_simd->car_mass = (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
    out_simd->braking_eff = (int16_t)((uint16_t)p[4] | ((uint16_t)p[5] << 8));
    out_simd->idle_rpm = (int16_t)((uint16_t)p[6] | ((uint16_t)p[7] << 8));
    out_simd->downshift_rpm = (int16_t)((uint16_t)p[8] | ((uint16_t)p[9] << 8));
    out_simd->upshift_rpm = (int16_t)((uint16_t)p[10] | ((uint16_t)p[11] << 8));
    out_simd->max_rpm = (int16_t)((uint16_t)p[12] | ((uint16_t)p[13] << 8));

    for (int i = 0; i < 7; i++) {
        out_simd->gear_ratios[i] = (uint16_t)p[14 + i * 2] | ((uint16_t)p[15 + i * 2] << 8);
    }
    for (int i = 0; i < 7; i++) {
        out_simd->knob_points[i].x = (int16_t)((uint16_t)p[28 + i * 4] | ((uint16_t)p[29 + i * 4] << 8));
        out_simd->knob_points[i].y = (int16_t)((uint16_t)p[30 + i * 4] | ((uint16_t)p[31 + i * 4] << 8));
    }

    out_simd->aero_resistance = (int16_t)((uint16_t)p[56] | ((uint16_t)p[57] << 8));
    out_simd->idle_torque = p[58];
    memcpy(out_simd->torque_curve, p + 59, 104);

    out_simd->grip = (int16_t)((uint16_t)p[164] | ((uint16_t)p[165] << 8));
    out_simd->sliding = (int16_t)((uint16_t)p[180] | ((uint16_t)p[181] << 8));

    for (int i = 0; i < 4; i++) {
        out_simd->surface_grip[i] = (int16_t)((uint16_t)p[182 + i * 2] | ((uint16_t)p[183 + i * 2] << 8));
    }

    out_simd->car_height = (int16_t)((uint16_t)p[206] | ((uint16_t)p[207] << 8));

    for (int i = 0; i < 4; i++) {
        out_simd->wheel_coords[i].x = (int16_t)((uint16_t)p[208 + i * 6] | ((uint16_t)p[209 + i * 6] << 8));
        out_simd->wheel_coords[i].y = (int16_t)((uint16_t)p[210 + i * 6] | ((uint16_t)p[211 + i * 6] << 8));
        out_simd->wheel_coords[i].z = (int16_t)((uint16_t)p[212 + i * 6] | ((uint16_t)p[213 + i * 6] << 8));
    }

    memcpy(out_simd->steeringdots, p + 232, 62);

    /* Precompute 64-entry aerodynamic resistance table (drag = aero_res * v^2 / 512) */
    for (int i = 0; i < 16; i++) {
        out_simd->aerorestable[i] = (int16_t)(((int32_t)out_simd->aero_resistance * i * i) >> 9);
    }

    stunts_asset_free_archive(arc);
    return true;
}

bool stunts_load_collision_data(const char* game_pre_path, stunts_plane_t** out_planes, uint16_t* out_plane_count) {
    stunts_res_archive_t* arc = stunts_asset_load_archive(game_pre_path);
    if (!arc) return false;

    const stunts_sub_resource_t* plan_res = stunts_asset_find_resource(arc, "plan");
    if (!plan_res || !plan_res->data) {
        stunts_asset_free_archive(arc);
        return false;
    }

    uint16_t count = (uint16_t)(plan_res->size / 34);
    stunts_plane_t* planes = (stunts_plane_t*)calloc(count, sizeof(stunts_plane_t));
    if (!planes) {
        stunts_asset_free_archive(arc);
        return false;
    }

    const uint8_t* p = plan_res->data;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t* entry = p + (i * 34);
        planes[i].angle_yz = (int16_t)((uint16_t)entry[0] | ((uint16_t)entry[1] << 8));
        planes[i].angle_xy = (int16_t)((uint16_t)entry[2] | ((uint16_t)entry[3] << 8));

        planes[i].origin.x = (int16_t)((uint16_t)entry[4] | ((uint16_t)entry[5] << 8));
        planes[i].origin.y = (int16_t)((uint16_t)entry[6] | ((uint16_t)entry[7] << 8));
        planes[i].origin.z = (int16_t)((uint16_t)entry[8] | ((uint16_t)entry[9] << 8));

        planes[i].normal.x = (int16_t)((uint16_t)entry[10] | ((uint16_t)entry[11] << 8));
        planes[i].normal.y = (int16_t)((uint16_t)entry[12] | ((uint16_t)entry[13] << 8));
        planes[i].normal.z = (int16_t)((uint16_t)entry[14] | ((uint16_t)entry[15] << 8));

        for (int r = 0; r < 3; r++) {
            planes[i].rotation_matrix[r].x = (int16_t)((uint16_t)entry[16 + r * 6] | ((uint16_t)entry[17 + r * 6] << 8));
            planes[i].rotation_matrix[r].y = (int16_t)((uint16_t)entry[18 + r * 6] | ((uint16_t)entry[19 + r * 6] << 8));
            planes[i].rotation_matrix[r].z = (int16_t)((uint16_t)entry[20 + r * 6] | ((uint16_t)entry[21 + r * 6] << 8));
        }
    }

    *out_planes = planes;
    *out_plane_count = count;

    stunts_asset_free_archive(arc);
    return true;
}
