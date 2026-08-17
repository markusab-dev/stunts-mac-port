#ifndef STUNTS_APP_H
#define STUNTS_APP_H

#include "../sim/stunts_sim.h"
#include "../render/stunts_rasterizer.h"
#include "../render/stunts_camera.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t win_width;
    int32_t win_height;
    int32_t render_width;
    int32_t render_height;
    bool is_original_mode;
    bool enable_interpolation;
    bool is_widescreen;
    int freeze_frame; /* -1 = live simulation, >= 0 = frozen at frame */
    bool debug_cube;
    const char* data_dir;
    const char* track_name;
    const char* car_id;
} stunts_app_config_t;

#include "../render/stunts_render_320.h"

typedef struct {
    stunts_app_config_t config;
    stunts_sim_context_t sim_ctx;
    stunts_rasterizer_t rasterizer;
    stunts_render_320_t render_320;
    stunts_camera_t camera;
    bool running;
    uint8_t live_input_byte;
    uint64_t last_tick_ms;
    double sim_accumulator_ms;
} stunts_app_t;

/**
 * Initializes the Stunts native application.
 */
bool stunts_app_init(stunts_app_t* app, const stunts_app_config_t* config);

/**
 * Runs the main interactive game loop.
 */
void stunts_app_run(stunts_app_t* app);

/**
 * Cleans up SDL and app resources.
 */
void stunts_app_cleanup(stunts_app_t* app);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_APP_H */
