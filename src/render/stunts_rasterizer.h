#ifndef STUNTS_RASTERIZER_H
#define STUNTS_RASTERIZER_H

#include "stunts_render_types.h"
#include "stunts_shape3d.h"
#include "stunts_camera.h"
#include "stunts_palette.h"
#include "../sim/stunts_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t objects_submitted;
    uint32_t vertices_transformed;
    uint32_t polygons_submitted;
    uint32_t behind_camera;
    uint32_t near_clipped;
    uint32_t backface_culled;
    uint32_t screen_clipped;
    uint32_t polygons_rasterized;
    uint32_t track_tiles_parsed;
    uint32_t track_instances;
    uint32_t terrain_instances;
    uint32_t scenery_instances;
    uint32_t car_instances;
} stunts_render_stats_t;

typedef struct {
    uint32_t* pixels;
    int32_t width;
    int32_t height;
    int32_t pitch; /* in bytes */
    bool render_debug_cube;
    stunts_render_stats_t stats;
    stunts_viewport_t viewport;
    stunts_palette_t palette;
    stunts_shape_db_t shape_db;
} stunts_rasterizer_t;

/**
 * Initializes rasterizer framebuffer at specified target resolution.
 */
bool stunts_rasterizer_init(stunts_rasterizer_t* r, int32_t width, int32_t height, const char* data_dir, const char* car_id);

/**
 * Frees rasterizer memory.
 */
void stunts_rasterizer_cleanup(stunts_rasterizer_t* r);

/**
 * Resizes the rasterizer framebuffer for direct high-resolution rendering.
 */
bool stunts_rasterizer_resize(stunts_rasterizer_t* r, int32_t new_width, int32_t new_height);

/**
 * Clears background with sky and ground colors based on horizon line.
 */
void stunts_rasterizer_clear_sky_ground(stunts_rasterizer_t* r, const stunts_camera_t* cam);

/**
 * Draws a filled 2D convex polygon using scanline rasterization.
 */
void stunts_rasterizer_fill_poly2d(stunts_rasterizer_t* r,
                                  int num_pts,
                                  const int32_t* xs,
                                  const int32_t* ys,
                                  uint8_t color_idx);

/**
 * Renders a full 3D Stunts scene: track, scenery, terrain, and vehicle.
 */
void stunts_rasterizer_render_scene(stunts_rasterizer_t* r,
                                   const stunts_sim_context_t* sim_ctx,
                                   const stunts_camera_t* cam);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_RASTERIZER_H */
