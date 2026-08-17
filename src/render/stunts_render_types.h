#ifndef STUNTS_RENDER_TYPES_H
#define STUNTS_RENDER_TYPES_H

#include "../common/stunts_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Color & Palette Types
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} stunts_color_rgba_t;

typedef struct {
    stunts_color_rgba_t colors[256];
} stunts_palette_t;

/* -------------------------------------------------------------------------
 * 3D Shape Data Structures
 * ------------------------------------------------------------------------- */
#define STUNTS_MAX_SHAPE_VERTS 256
#define STUNTS_MAX_SHAPE_PRIMS 256
#define STUNTS_MAX_POLY_VERTS  16

typedef enum {
    STUNTS_PRIM_POLYGON = 0,
    STUNTS_PRIM_LINE    = 1,
    STUNTS_PRIM_WHEEL   = 2,
    STUNTS_PRIM_SPHERE  = 3
} stunts_prim_type_t;

typedef struct {
    uint8_t type;
    uint8_t color_index;
    uint8_t material_id;
    uint8_t num_verts;
    uint8_t vert_indices[STUNTS_MAX_POLY_VERTS];
    int16_t normal_x;
    int16_t normal_y;
    int16_t normal_z;
    bool backface_cull;
} stunts_primitive_t;

typedef struct {
    char name[8];
    uint16_t num_verts;
    uint16_t num_prims;
    uint16_t num_paints;
    stunts_vector_t verts[STUNTS_MAX_SHAPE_VERTS];
    stunts_primitive_t prims[STUNTS_MAX_SHAPE_PRIMS];
} stunts_shape3d_t;

/* -------------------------------------------------------------------------
 * Render Viewport & Camera
 * ------------------------------------------------------------------------- */
typedef enum {
    STUNTS_CAM_CHASE   = 0,
    STUNTS_CAM_COCKPIT = 1,
    STUNTS_CAM_OVERVIEW = 2,
    STUNTS_CAM_FREE    = 3
} stunts_cam_mode_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float aspect_ratio;
    bool is_4_3_pillarboxed;
} stunts_viewport_t;

typedef struct {
    stunts_vector_long_t pos;
    stunts_vector_t rot;       /* pitch, yaw, roll (0..1023) */
    stunts_matrix_t view_matrix;
    int32_t focal_length;
    stunts_cam_mode_t mode;
} stunts_camera_t;

/* -------------------------------------------------------------------------
 * Render Snapshot (Immutable Snapshot from 20 Hz Simulation)
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t frame_index;
    stunts_vector_long_t car_pos;
    stunts_vector_t car_rot;
    int16_t steering_angle;
    uint16_t speed_actual;
    uint16_t engine_rpm;
    uint8_t current_gear;
    uint8_t is_braking;
    uint8_t is_accelerating;
    uint8_t sliding_flag;
    uint8_t crash_flag;
    uint8_t wheels_on_ground;
    char car_id[5];
    char track_name[9];
} stunts_render_snapshot_t;

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_RENDER_TYPES_H */
