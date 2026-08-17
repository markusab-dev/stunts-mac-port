#include "stunts_canonical_state.h"
#include <inttypes.h>
#include <string.h>

void stunts_canonical_state_write_jsonl(FILE* f, const stunts_canonical_state_t* s) {
    if (!f || !s) return;
    fprintf(f, "{\"schema\":\"%s\",\"frame\":%" PRIu32 ",\"game_frame\":%" PRIu16 ",\"time_ms\":%" PRId32 ","
               "\"pos\":[%" PRId32 ",%" PRId32 ",%" PRId32 "],\"rot\":[%" PRId16 ",%" PRId16 ",%" PRId16 "],"
               "\"speed_coupled\":%" PRIu16 ",\"speed_actual\":%" PRIu16 ",\"speed_last\":%" PRIu16 ",\"speed_diff\":%" PRId16 ","
               "\"rpm\":%" PRId16 ",\"gear\":%" PRIu8 ",\"gear_ratio\":%" PRIu16 ",\"brake\":%" PRIu8 ",\"accel\":%" PRIu8 ",\"steering\":%" PRId16 ","
               "\"whl_surf\":[%" PRIu8 ",%" PRIu8 ",%" PRIu8 ",%" PRIu8 "],\"wheels_on_ground\":%" PRIu8 ","
               "\"whl_force\":[%" PRId16 ",%" PRId16 ",%" PRId16 ",%" PRId16 "],"
               "\"pseudo_gravity\":%" PRId16 ",\"sliding\":%" PRIu8 ",\"demanded_grip\":%" PRId16 ",\"surface_grip_sum\":%" PRId16 ","
               "\"crashed\":%" PRIu8 ",\"distance\":%" PRId32 ",\"penalty\":%" PRId16 ",\"impact_speed\":%" PRIu16 "}\n",
            STUNTS_CANONICAL_SCHEMA,
            s->frame_index, s->game_frame, s->time_ms,
            s->pos_x, s->pos_y, s->pos_z,
            s->rot_pitch_x, s->rot_yaw_y, s->rot_roll_z,
            s->speed_coupled, s->speed_actual, s->speed_last, s->speed_diff,
            s->engine_rpm, s->current_gear, s->gear_ratio, s->is_braking, s->is_accelerating, s->steering_angle,
            s->whl_surf[0], s->whl_surf[1], s->whl_surf[2], s->whl_surf[3], s->wheels_on_ground,
            s->whl_force[0], s->whl_force[1], s->whl_force[2], s->whl_force[3],
            s->pseudo_gravity, s->sliding_flag, s->demanded_grip, s->surface_grip_sum,
            s->crash_flag, s->distance_traveled, s->penalty_counter, s->impact_speed);
}

void stunts_canonical_state_write_binary(FILE* f, const stunts_canonical_state_t* s) {
    if (!f || !s) return;
    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));

    /* Header & Timing (0..11) */
    memcpy(buf + 0, &s->frame_index, 4);
    memcpy(buf + 4, &s->game_frame, 2);
    memcpy(buf + 6, &s->time_ms, 4);

    /* Transform (12..29) */
    memcpy(buf + 12, &s->pos_x, 4);
    memcpy(buf + 16, &s->pos_y, 4);
    memcpy(buf + 20, &s->pos_z, 4);
    memcpy(buf + 24, &s->rot_pitch_x, 2);
    memcpy(buf + 26, &s->rot_yaw_y, 2);
    memcpy(buf + 28, &s->rot_roll_z, 2);

    /* Drivetrain & Speeds (30..47) */
    memcpy(buf + 30, &s->speed_coupled, 2);
    memcpy(buf + 32, &s->speed_actual, 2);
    memcpy(buf + 34, &s->speed_last, 2);
    memcpy(buf + 36, &s->speed_diff, 2);
    memcpy(buf + 38, &s->engine_rpm, 2);
    buf[40] = s->current_gear;
    memcpy(buf + 41, &s->gear_ratio, 2);
    buf[43] = s->is_braking;
    buf[44] = s->is_accelerating;
    memcpy(buf + 45, &s->steering_angle, 2);

    /* Wheels & Suspension (48..63) */
    memcpy(buf + 48, s->whl_surf, 4);
    buf[52] = s->wheels_on_ground;
    buf[53] = s->wheels_front_contact;
    buf[54] = s->wheels_rear_contact;
    memcpy(buf + 56, s->whl_force, 8);

    /* Physical Dynamics (64..79) */
    memcpy(buf + 64, &s->pseudo_gravity, 2);
    buf[66] = s->sliding_flag;
    memcpy(buf + 67, &s->demanded_grip, 2);
    memcpy(buf + 69, &s->surface_grip_sum, 2);
    buf[71] = s->crash_flag;
    buf[72] = s->engine_limiter_timer;

    /* Metrics & PRNG (80..99) */
    memcpy(buf + 80, &s->distance_traveled, 4);
    memcpy(buf + 84, &s->penalty_counter, 2);
    memcpy(buf + 86, &s->impact_speed, 2);
    memcpy(buf + 88, &s->top_speed, 2);
    memcpy(buf + 90, &s->jump_count, 2);
    memcpy(buf + 92, s->kevin_seed, 6);

    fwrite(buf, 1, 128, f);
}

bool stunts_canonical_state_read_binary(FILE* f, stunts_canonical_state_t* s) {
    if (!f || !s) return false;
    uint8_t buf[128];
    if (fread(buf, 1, 128, f) != 128) return false;

    memset(s, 0, sizeof(stunts_canonical_state_t));

    memcpy(&s->frame_index, buf + 0, 4);
    memcpy(&s->game_frame, buf + 4, 2);
    memcpy(&s->time_ms, buf + 6, 4);

    memcpy(&s->pos_x, buf + 12, 4);
    memcpy(&s->pos_y, buf + 16, 4);
    memcpy(&s->pos_z, buf + 20, 4);
    memcpy(&s->rot_pitch_x, buf + 24, 2);
    memcpy(&s->rot_yaw_y, buf + 26, 2);
    memcpy(&s->rot_roll_z, buf + 28, 2);

    memcpy(&s->speed_coupled, buf + 30, 2);
    memcpy(&s->speed_actual, buf + 32, 2);
    memcpy(&s->speed_last, buf + 34, 2);
    memcpy(&s->speed_diff, buf + 36, 2);
    memcpy(&s->engine_rpm, buf + 38, 2);
    s->current_gear = buf[40];
    memcpy(&s->gear_ratio, buf + 41, 2);
    s->is_braking = buf[43];
    s->is_accelerating = buf[44];
    memcpy(&s->steering_angle, buf + 45, 2);

    memcpy(s->whl_surf, buf + 48, 4);
    s->wheels_on_ground = buf[52];
    s->wheels_front_contact = buf[53];
    s->wheels_rear_contact = buf[54];
    memcpy(s->whl_force, buf + 56, 8);

    memcpy(&s->pseudo_gravity, buf + 64, 2);
    s->sliding_flag = buf[66];
    memcpy(&s->demanded_grip, buf + 67, 2);
    memcpy(&s->surface_grip_sum, buf + 69, 2);
    s->crash_flag = buf[71];
    s->engine_limiter_timer = buf[72];

    memcpy(&s->distance_traveled, buf + 80, 4);
    memcpy(&s->penalty_counter, buf + 84, 2);
    memcpy(&s->impact_speed, buf + 86, 2);
    memcpy(&s->top_speed, buf + 88, 2);
    memcpy(&s->jump_count, buf + 90, 2);
    memcpy(s->kevin_seed, buf + 92, 6);

    return true;
}
