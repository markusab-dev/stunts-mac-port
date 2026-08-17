#include "stunts_camera.h"
#include <string.h>

void stunts_camera_init(stunts_camera_t* cam, stunts_cam_mode_t mode) {
    if (!cam) return;
    memset(cam, 0, sizeof(stunts_camera_t));
    cam->mode = mode;
    cam->focal_length = 256; /* Default focal distance */
}

void stunts_camera_update(stunts_camera_t* cam,
                         const stunts_vector_long_t* car_pos,
                         const stunts_vector_t* car_rot,
                         uint16_t speed) {
    if (!cam || !car_pos || !car_rot) return;
    (void)speed;

    if (cam->mode == STUNTS_CAM_CHASE) {
        /* Stable Chase Camera behind vehicle */
        int16_t yaw = car_rot->y;
        int16_t sin_yaw = stunts_sin((uint16_t)yaw);
        int16_t cos_yaw = stunts_cos((uint16_t)yaw);

        int32_t dist = 650;   /* Distance behind vehicle */
        int32_t height = 180; /* Height above vehicle */

        cam->pos.lx = car_pos->lx - (((int32_t)sin_yaw * dist) >> 14);
        cam->pos.lz = car_pos->lz - (((int32_t)cos_yaw * dist) >> 14);
        cam->pos.ly = car_pos->ly + height;

        cam->rot.x = -35;     /* Downward tilt angle (~12 degrees) */
        cam->rot.y = yaw;     /* Align with vehicle heading */
        cam->rot.z = 0;
    } else if (cam->mode == STUNTS_CAM_COCKPIT) {
        cam->pos.lx = car_pos->lx;
        cam->pos.ly = car_pos->ly + 90;
        cam->pos.lz = car_pos->lz;
        cam->rot = *car_rot;
    } else if (cam->mode == STUNTS_CAM_OVERVIEW) {
        cam->pos.lx = 15 * 1024;
        cam->pos.ly = 6000;
        cam->pos.lz = 15 * 1024;
        cam->rot.x = -200;
        cam->rot.y = 0;
        cam->rot.z = 0;
    }

    stunts_mat_rot_zxy(&cam->view_matrix, -cam->rot.z, -cam->rot.x, -cam->rot.y);
}

bool stunts_camera_project_point(const stunts_camera_t* cam,
                                const stunts_viewport_t* vp,
                                const stunts_vector_long_t* world_pos,
                                int32_t* out_screen_x,
                                int32_t* out_screen_y,
                                int32_t* out_cam_z) {
    if (!cam || !vp || !world_pos || !out_screen_x || !out_screen_y || !out_cam_z) {
        return false;
    }

    stunts_vector_long_t rel;
    rel.lx = world_pos->lx - cam->pos.lx;
    rel.ly = world_pos->ly - cam->pos.ly;
    rel.lz = world_pos->lz - cam->pos.lz;

    stunts_vector_long_t cam_p;
    stunts_mat_mul_vector_long(&rel, &cam->view_matrix, &cam_p);

    *out_cam_z = (int32_t)cam_p.lz;

    if (cam_p.lz <= 16) {
        return false;
    }

    float fov_x = (float)vp->width * (256.0f / 320.0f);
    float fov_y = (float)vp->height * (213.0f / 200.0f);
    float cx = (float)vp->x + ((float)vp->width * 0.5f);
    float cy = (float)vp->y + ((float)vp->height * 0.5f);

    float inv_z = 1.0f / (float)cam_p.lz;
    *out_screen_x = (int32_t)(cx + ((float)cam_p.lx * fov_x * inv_z));
    *out_screen_y = (int32_t)(cy - ((float)cam_p.ly * fov_y * inv_z));

    return true;
}
