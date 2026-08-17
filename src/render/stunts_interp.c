#include "stunts_interp.h"
#include <string.h>

static int16_t interp_angle(int16_t a0, int16_t a1, float alpha) {
    int32_t diff = (a1 - a0) & 0x3FF;
    if (diff > 512) {
        diff -= 1024;
    }
    int32_t res = (int32_t)a0 + (int32_t)(diff * alpha);
    return (int16_t)(res & 0x3FF);
}

void stunts_interp_snapshot(const stunts_render_snapshot_t* prev,
                            const stunts_render_snapshot_t* curr,
                            float alpha,
                            stunts_render_snapshot_t* out_interp) {
    if (!prev || !curr || !out_interp) return;
    if (alpha <= 0.0f) {
        *out_interp = *prev;
        return;
    }
    if (alpha >= 1.0f) {
        *out_interp = *curr;
        return;
    }

    *out_interp = *curr;

    out_interp->car_pos.lx = prev->car_pos.lx + (int32_t)((curr->car_pos.lx - prev->car_pos.lx) * alpha);
    out_interp->car_pos.ly = prev->car_pos.ly + (int32_t)((curr->car_pos.ly - prev->car_pos.ly) * alpha);
    out_interp->car_pos.lz = prev->car_pos.lz + (int32_t)((curr->car_pos.lz - prev->car_pos.lz) * alpha);

    out_interp->car_rot.x = interp_angle(prev->car_rot.x, curr->car_rot.x, alpha);
    out_interp->car_rot.y = interp_angle(prev->car_rot.y, curr->car_rot.y, alpha);
    out_interp->car_rot.z = interp_angle(prev->car_rot.z, curr->car_rot.z, alpha);

    out_interp->steering_angle = prev->steering_angle + (int16_t)((curr->steering_angle - prev->steering_angle) * alpha);
    out_interp->speed_actual = prev->speed_actual + (uint16_t)((curr->speed_actual - prev->speed_actual) * alpha);
    out_interp->engine_rpm = prev->engine_rpm + (uint16_t)((curr->engine_rpm - prev->engine_rpm) * alpha);
}
