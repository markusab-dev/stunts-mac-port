#ifndef STUNTS_RENDER_320_H
#define STUNTS_RENDER_320_H

#include <stdint.h>
#include <stdbool.h>
#include "../sim/stunts_sim.h"
#include "stunts_render_types.h"
#include "stunts_palette.h"
#include "stunts_shape3d.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STUNTS_RENDER_W 320
#define STUNTS_RENDER_H 200

typedef struct {
    uint32_t pixels[STUNTS_RENDER_W * STUNTS_RENDER_H]; /* 32-bit ARGB8888 */
    uint8_t  indexed[STUNTS_RENDER_W * STUNTS_RENDER_H]; /* 8-bit VGA DAC indexed */
    int32_t  pitch;
    stunts_palette_t palette;
    stunts_shape_db_t shape_db;
    int camera_mode; /* 0: Cockpit, 1: Overview, 2: Chase */
} stunts_render_320_t;

/**
 * Initializes the faithful 320x200 software renderer.
 */
bool stunts_render_320_init(stunts_render_320_t* r, const char* data_dir, const char* car_id);

/**
 * Frees any allocated resources.
 */
void stunts_render_320_cleanup(stunts_render_320_t* r);

/**
 * Renders one complete 320x200 Stunts frame faithfully using the Restunts pipeline.
 */
void stunts_render_320_frame(stunts_render_320_t* r, const stunts_sim_context_t* sim_ctx);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_RENDER_320_H */
