#ifndef STUNTS_SIM_H
#define STUNTS_SIM_H

#include "../common/stunts_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Configuration & Assets */
    stunts_game_info_t game_info;
    stunts_simd_t player_simd;
    stunts_simd_t opponent_simd;
    uint8_t track_elements[900];
    uint8_t track_heights[900];
    stunts_plane_t* collision_planes;
    uint16_t plane_count;

    /* Runtime State */
    stunts_car_state_t player_state;
    stunts_car_state_t opponent_state;
    uint32_t current_frame;
    uint16_t sim_game_frame;
    int32_t total_distance;
    int16_t penalty_time;
    uint16_t impact_speed;
    uint16_t top_speed;
    int16_t jump_count;
    uint8_t kevin_prng_seed[6];

    /* Dynamic Tables */
    int16_t player_aero_table[64];
    int16_t opponent_aero_table[64];
} stunts_sim_context_t;

/**
 * Initializes simulation context with track, car, and collision data.
 */
bool stunts_sim_init(stunts_sim_context_t* ctx,
                    const char* data_dir,
                    const stunts_game_info_t* game_info);

/**
 * Frees any dynamic allocations inside simulation context.
 */
void stunts_sim_cleanup(stunts_sim_context_t* ctx);

/**
 * Advances the simulation by exactly one 20 Hz discrete tick (50 ms).
 * @param input_byte Discrete 8-bit input bitfield from replay or controller.
 */
void stunts_sim_step(stunts_sim_context_t* ctx, uint8_t input_byte);

/**
 * Extracts current frame into architecture-independent STUNTS_CANONICAL_STATE_V1.
 */
void stunts_sim_get_canonical_state(const stunts_sim_context_t* ctx, stunts_canonical_state_t* out_state);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_SIM_H */
