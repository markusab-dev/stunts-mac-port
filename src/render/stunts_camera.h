#ifndef STUNTS_CAMERA_H
#define STUNTS_CAMERA_H

#include "stunts_render_types.h"
#include "../sim/stunts_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes camera state.
 */
void stunts_camera_init(stunts_camera_t* cam, stunts_cam_mode_t mode);

/**
 * Updates camera position and orientation to track vehicle.
 */
void stunts_camera_update(stunts_camera_t* cam,
                         const stunts_vector_long_t* car_pos,
                         const stunts_vector_t* car_rot,
                         uint16_t speed);

/**
 * Projects a 3D world coordinate into 2D viewport screen coordinates.
 * Returns true if vertex is in front of camera (Z > 0), false if clipped.
 */
bool stunts_camera_project_point(const stunts_camera_t* cam,
                                const stunts_viewport_t* vp,
                                const stunts_vector_long_t* world_pos,
                                int32_t* out_screen_x,
                                int32_t* out_screen_y,
                                int32_t* out_cam_z);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_CAMERA_H */
