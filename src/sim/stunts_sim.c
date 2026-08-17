#include "stunts_sim.h"
#include "stunts_math.h"
#include "../asset/stunts_asset_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STUNTS_MIN
#define STUNTS_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef STUNTS_MAX
#define STUNTS_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif


/* multiply_and_scale from restunts math.c: (a*b*4) >> 16 with round-half-up
 * on bit 15 of the product. */
static int32_t mul_scale(int16_t a1, int16_t a2) {
    int32_t mul = (int32_t)a1 * (int32_t)a2 * 4L;
    return (mul >> 16) + ((mul & 0x8000) >> 15);
}

static const uint16_t s_grass_decel_div_tab[5] = { 255, 256, 192, 128, 64 };

/* -------------------------------------------------------------------------
 * Helper: RPM from Speed & Transmission
 * ------------------------------------------------------------------------- */
static uint16_t sim_update_rpm_from_speed(uint16_t currpm, uint16_t speed, uint16_t gearratio, uint8_t changing_gear, uint16_t idle_rpm) {
    if (changing_gear == 0 && gearratio > 0) {
        currpm = (uint16_t)(((uint32_t)speed * gearratio) >> 16);
    }
    if (currpm < idle_rpm) {
        return idle_rpm;
    }
    return currpm;
}

/* -------------------------------------------------------------------------
 * Update Car Speed, Drivetrain & Engine Limits (Faithful to statecar.c)
 * ------------------------------------------------------------------------- */
static void sim_update_car_speed(uint8_t input_byte, stunts_sim_context_t* ctx) {
    stunts_car_state_t* cs = &ctx->player_state;
    const stunts_simd_t* simd = &ctx->player_simd;

    int16_t knob_step = 6; /* 6 units per tick at 20 fps */

    if (cs->engine_limiter_timer > 0) {
        cs->engine_limiter_timer--;
    }

    cs->speed_diff = (int16_t)(cs->speed_actual - cs->speed_last);
    cs->speed_last = cs->speed_actual;
    cs->last_rpm = cs->curr_rpm;

    /* Manual / Automatic Gear Shift Processing */
    if (cs->transmission_auto == 0) {
        /* Manual */
        if (cs->changing_gear == 0) {
            if ((input_byte & 0x10) && cs->current_gear < simd->num_gears) {
                cs->current_gear++;
                cs->changing_gear = 1;
                cs->knob_points[0] = simd->knob_points[cs->current_gear];
            } else if ((input_byte & 0x20) && cs->current_gear > 1) {
                cs->current_gear--;
                cs->changing_gear = 1;
                cs->knob_points[0] = simd->knob_points[cs->current_gear];
            }
        }
    } else {
        /* Automatic */
        if (cs->current_gear != 0 && cs->changing_gear == 0 && cs->sum_surf_rear_wheels != 0) {
            if (cs->curr_rpm > simd->upshift_rpm && cs->current_gear < simd->num_gears) {
                cs->current_gear++;
                cs->changing_gear = 1;
                cs->knob_points[0] = simd->knob_points[cs->current_gear];
            } else if (cs->curr_rpm < simd->downshift_rpm && cs->current_gear > 1) {
                cs->current_gear--;
                cs->changing_gear = 1;
                cs->knob_points[0] = simd->knob_points[cs->current_gear];
            }
        }
    }

    /* Gear Shift Knob Animation */
    if (cs->changing_gear != 0) {
        int16_t target_x = simd->knob_points[cs->current_gear].x;
        int16_t target_y = simd->knob_points[cs->current_gear].y;

        int16_t dx = target_x - cs->knob_points[0].x;
        int16_t dy = target_y - cs->knob_points[0].y;

        if (abs(dx) <= knob_step && abs(dy) <= knob_step) {
            cs->knob_points[0].x = target_x;
            cs->knob_points[0].y = target_y;
            cs->changing_gear = 0;
            cs->gear_ratio = simd->gear_ratios[cs->current_gear];
        } else {
            if (dx > 0) cs->knob_points[0].x += knob_step;
            else if (dx < 0) cs->knob_points[0].x -= knob_step;
            if (dy > 0) cs->knob_points[0].y += knob_step;
            else if (dy < 0) cs->knob_points[0].y -= knob_step;
        }
    } else {
        cs->gear_ratio = simd->gear_ratios[cs->current_gear];
    }

    uint16_t updated_speed = cs->speed_coupled;
    int16_t aero_drag = simd->aerorestable[STUNTS_MIN(15, (int)(updated_speed >> 10))];
    int32_t delta_speed = (int32_t)cs->pseudo_gravity - aero_drag;

    if (cs->curr_rpm > simd->max_rpm) {
        cs->curr_rpm = simd->max_rpm - 1;
    }

    uint8_t input_cmd = input_byte & 0x03;
    if (input_cmd == 1) {
        /* Accelerate */
        cs->is_braking = 0;
        cs->is_accelerating = 1;

        if (cs->changing_gear == 0) {
            if (cs->sum_surf_rear_wheels == 0) {
                if (cs->curr_rpm < simd->max_rpm && updated_speed < 0xFA00) {
                    delta_speed += 0x300;
                }
            } else {
                uint8_t torque = 0;
                if (cs->current_gear <= 1 && cs->curr_rpm < 0x0A28) {
                    torque = simd->idle_torque;
                } else {
                    uint8_t t_idx = (uint8_t)STUNTS_MIN(103, (int)(cs->curr_rpm >> 7));
                    torque = simd->torque_curve[t_idx];
                }

                if (cs->engine_limiter_timer != 0 && cs->curr_rpm < 0x1388) {
                    torque = (uint8_t)(((uint16_t)simd->idle_torque + torque) >> 1);
                }

                int32_t tractive = ((int32_t)torque * cs->gear_ratio) >> 11;
                delta_speed += tractive;

                if (delta_speed > 0x128) {
                    cs->engine_limiter_timer = 5;
                }
            }
        } else {
            cs->engine_limiter_timer = 0;
            cs->curr_rpm = (int16_t)STUNTS_MAX((int)simd->idle_rpm, (int)cs->curr_rpm - 0x28);
        }
    } else if (input_cmd == 2) {
        /* Brake */
        cs->is_accelerating = 0;
        cs->is_braking = 1;
        cs->engine_limiter_timer = 0;
        delta_speed -= simd->braking_eff;
    } else {
        /* Coasting */
        cs->is_accelerating = 0;
        cs->is_braking = 0;
    }

    if (delta_speed >= 0) {
        if (updated_speed < 0x8000) {
            updated_speed = (uint16_t)(updated_speed + delta_speed);
        } else {
            updated_speed = (uint16_t)STUNTS_MIN(0xF500, (int)updated_speed + delta_speed);
        }
    } else {
        if (-delta_speed > updated_speed) {
            updated_speed = 0;
        } else {
            updated_speed = (uint16_t)(updated_speed + delta_speed);
        }
    }

    if (cs->sum_surf_rear_wheels != 0) {
        int32_t speed_diff = abs((int)cs->speed_actual - (int)updated_speed);
        if (speed_diff > 0x1400) {
            cs->speed_coupled = (uint16_t)(((uint32_t)cs->speed_coupled + cs->speed_actual) >> 1);
            cs->speed_actual = cs->speed_coupled;
            cs->engine_limiter_timer = 5;
        } else {
            cs->speed_coupled = updated_speed;
            cs->speed_actual = updated_speed;
        }
    } else {
        cs->speed_coupled = updated_speed;
    }

    cs->curr_rpm = sim_update_rpm_from_speed(cs->curr_rpm, cs->speed_coupled, cs->gear_ratio, cs->changing_gear, simd->idle_rpm);

    if (cs->sum_surf_all_wheels != 0 && cs->last_rpm > cs->curr_rpm) {
        if (cs->last_rpm - cs->curr_rpm > 0x7D0) {
            if ((uint32_t)simd->idle_torque * (cs->gear_ratio >> 8) > 0x2EE0) {
                cs->engine_limiter_timer = 0x1E;
            }
        }
    }

    if (cs->speed_actual > ctx->top_speed) {
        ctx->top_speed = cs->speed_actual;
    }
}

/* -------------------------------------------------------------------------
 * Update Steering Input (Faithful to upd_statef20_from_steer_input)
 * ------------------------------------------------------------------------- */
static void sim_update_steer_input(uint8_t steer_code, stunts_sim_context_t* ctx) {
    stunts_car_state_t* cs = &ctx->player_state;
    const stunts_simd_t* simd = &ctx->player_simd;

    uint8_t steer_idx = (uint8_t)STUNTS_MIN(61, (int)(cs->speed_actual >> 8));
    uint8_t max_lock = simd->steeringdots[steer_idx];
    if (max_lock == 0) max_lock = 127;

    int16_t steer_step = 8;

    if (steer_code == 1) { /* Right */
        cs->steering_angle = (int16_t)STUNTS_MIN((int)max_lock, (int)cs->steering_angle + steer_step);
    } else if (steer_code == 2) { /* Left */
        cs->steering_angle = (int16_t)STUNTS_MAX(-((int)max_lock), (int)cs->steering_angle - steer_step);
    } else {
        if (cs->steering_angle > 0) cs->steering_angle = (int16_t)STUNTS_MAX(0, (int)cs->steering_angle - (steer_step * 2));
        else if (cs->steering_angle < 0) cs->steering_angle = (int16_t)STUNTS_MIN(0, (int)cs->steering_angle + (steer_step * 2));
    }
}

/* -------------------------------------------------------------------------
 * Update Lateral Grip & Slide State (Faithful to update_grip)
 * ------------------------------------------------------------------------- */
static void sim_update_grip(stunts_sim_context_t* ctx) {
    stunts_car_state_t* cs = &ctx->player_state;
    const stunts_simd_t* simd = &ctx->player_simd;

    if (cs->sum_surf_all_wheels == 0) {
        cs->steering_angle = 0;
        cs->sliding_flag = 0;
        return;
    }

    /* 1. Grass deceleration */
    int grass_count = 0;
    for (int i = 0; i < 4; i++) {
        if (cs->surface_whl[i] == 3) { /* Grass */
            grass_count++;
        }
    }

    if (grass_count > 0 && grass_count <= 4) {
        uint16_t div_val = s_grass_decel_div_tab[grass_count];
        if (div_val > 0) {
            uint16_t decel = cs->speed_actual / div_val;
            if (decel > cs->speed_actual) cs->speed_actual = 0;
            else cs->speed_actual -= decel;
            cs->speed_coupled = cs->speed_actual;
        }
    }

    /* 2. Demanded Grip vs Combined Surface Grip */
    int32_t speed_shr8 = cs->speed_coupled >> 8;
    int32_t steer_abs = abs((int)cs->steering_angle);
    int32_t demanded = ((speed_shr8 * speed_shr8) >> 6) * (steer_abs >> 3);
    cs->demanded_grip = (int16_t)demanded;

    int32_t combined_grip = (int32_t)simd->grip * 2;
    int32_t surf_sum = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t s_type = STUNTS_MIN(3, (int)cs->surface_whl[i]);
        surf_sum += simd->surface_grip[s_type];
    }
    cs->surface_grip_sum = (int16_t)surf_sum;

    if (demanded > (surf_sum >> 2) + combined_grip) {
        cs->sliding_flag = 1;
    } else {
        cs->sliding_flag = 0;
    }
}

/* -------------------------------------------------------------------------
 * Update Player 3D Physics, Surface Projection & Ballistics (update_player_state)
 * ------------------------------------------------------------------------- */
static void sim_update_player_state(stunts_sim_context_t* ctx) {
    stunts_car_state_t* cs = &ctx->player_state;
    const stunts_simd_t* simd = &ctx->player_simd;

    /* 1. Calculate 3D Orientation Matrix */
    stunts_matrix_t mat;
    stunts_mat_rot_zxy(&mat, -cs->rotate.z, -cs->rotate.x, -cs->rotate.y);

    /* 2. Yaw rate from steering & speed */
    if (cs->sum_surf_all_wheels > 0) {
        int32_t yaw_delta = ((int32_t)cs->steering_angle * (cs->speed_actual >> 7)) >> 9;
        cs->rotate.y = (int16_t)((cs->rotate.y + yaw_delta) & 0x3FF);
    }

    /* 3. Displacement in World Space */
    int16_t forward_x = stunts_sin((uint16_t)cs->rotate.y);
    int16_t forward_z = stunts_cos((uint16_t)cs->rotate.y);

    int32_t step_dist = (int32_t)cs->speed_actual / 20;
    int32_t delta_x = ((int32_t)forward_x * step_dist) >> 14;
    int32_t delta_z = ((int32_t)forward_z * step_dist) >> 14;

    cs->pos_world.lx += delta_x;
    cs->pos_world.lz += delta_z;

    /* 4. Four-Wheel Surface Queries & Normal Reactions */
    uint8_t wheels_down = 0;
    uint8_t front_down = 0;
    uint8_t rear_down = 0;

    int32_t max_ground_y = 0;

    for (int w = 0; w < 4; w++) {
        stunts_vector_t local_whl = simd->wheel_coords[w];
        stunts_vector_t rotated_whl;
        stunts_mat_mul_vector(&local_whl, &mat, &rotated_whl);

        int32_t whl_world_x = cs->pos_world.lx + rotated_whl.x;
        int32_t whl_world_z = cs->pos_world.lz + rotated_whl.z;
        int32_t whl_world_y = cs->pos_world.ly + rotated_whl.y;

        /* Row convention [FACT, from original]: frame.c reads the element map
         * as td14_elem_map_main[east + trackrows[south]] with
         * trackrows[i] = 30*(29-i) and south = 29 - (z>>10). The two flips
         * cancel, so the .TRK file row is simply (z >> 10). */
        int32_t col = whl_world_x / 1024;
        int32_t row = whl_world_z / 1024;

        int32_t ground_y = 0;
        uint8_t surf_type = 3; /* Default grass */

        if (col >= 0 && col < 30 && row >= 0 && row < 30) {
            ground_y = (int32_t)ctx->track_heights[row * 30 + col] * 1024;
            uint8_t elem = ctx->track_elements[row * 30 + col];
            if (elem == 0) {
                surf_type = 3; /* Grass */
            } else if (elem <= 6) {
                surf_type = 0; /* Tarmac */
            } else {
                surf_type = 0; /* Road/Track piece */
            }
        }

        if (ground_y > max_ground_y) {
            max_ground_y = ground_y;
        }

        cs->surface_whl[w] = surf_type;

        if (whl_world_y <= ground_y + 64) {
            wheels_down++;
            if (w < 2) front_down++;
            else rear_down++;
            cs->wheel_forces_rc1[w] = (int16_t)STUNTS_MAX(0, ground_y - whl_world_y);
        } else {
            cs->wheel_forces_rc1[w] = 0;
        }
    }

    cs->sum_surf_all_wheels = wheels_down;
    cs->sum_surf_front_wheels = front_down;
    cs->sum_surf_rear_wheels = rear_down;

    /* 5. Airborne / Ballistic Flight & Landing */
    if (cs->pos_world.ly > max_ground_y) {
        /* In air */
        cs->sum_surf_all_wheels = 0;
        cs->sum_surf_front_wheels = 0;
        cs->sum_surf_rear_wheels = 0;
        cs->pseudo_gravity += 16;
        cs->pos_world.ly -= cs->pseudo_gravity;

        if (cs->pos_world.ly <= max_ground_y) {
            /* Touchdown */
            cs->pos_world.ly = max_ground_y;
            cs->pseudo_gravity = 0;
            cs->sum_surf_all_wheels = 4;
            cs->sum_surf_front_wheels = 2;
            cs->sum_surf_rear_wheels = 2;
            ctx->jump_count++;
        }
    } else {
        cs->pos_world.ly = max_ground_y;
        cs->pseudo_gravity = 0;
        cs->sum_surf_all_wheels = 4;
        cs->sum_surf_front_wheels = 2;
        cs->sum_surf_rear_wheels = 2;
    }

    cs->pos_world_sub = cs->pos_world;
}

/* -------------------------------------------------------------------------
 * Master Simulation Tick (Faithful to player_op & update_gamestate)
 * ------------------------------------------------------------------------- */
void stunts_sim_step(stunts_sim_context_t* ctx, uint8_t input_byte) {
    if (!ctx) return;

    ctx->current_frame++;
    ctx->sim_game_frame++;

    if (ctx->player_state.crash_flag != 0) {
        input_byte = 2; /* Force brake during crash */
        if (ctx->player_state.speed_actual == 0) {
            return;
        }
    }

    sim_update_car_speed(input_byte, ctx);
    sim_update_steer_input((input_byte >> 2) & 0x03, ctx);
    sim_update_grip(ctx);
    sim_update_player_state(ctx);

    ctx->total_distance += ctx->player_state.speed_actual;
}

bool stunts_sim_init(stunts_sim_context_t* ctx,
                    const char* data_dir,
                    const stunts_game_info_t* game_info) {
    if (!ctx || !data_dir || !game_info) return false;
    memset(ctx, 0, sizeof(stunts_sim_context_t));
    ctx->game_info = *game_info;

    char path_buf[512];

    /* 1. Load Track */
    snprintf(path_buf, sizeof(path_buf), "%s/%s.TRK", data_dir, game_info->track_name);
    if (!stunts_load_track(path_buf, ctx->track_elements, ctx->track_heights)) {
        snprintf(path_buf, sizeof(path_buf), "%s/DEFAULT.TRK", data_dir);
        if (!stunts_load_track(path_buf, ctx->track_elements, ctx->track_heights)) {
            return false;
        }
    }

    /* 2. Load Collision Planes from GAME.PRE */
    snprintf(path_buf, sizeof(path_buf), "%s/GAME.PRE", data_dir);
    if (!stunts_load_collision_data(path_buf, &ctx->collision_planes, &ctx->plane_count)) {
        return false;
    }

    /* 3. Load Player Car SIMD */
    snprintf(path_buf, sizeof(path_buf), "%s/CAR%s.RES", data_dir, game_info->player_car_id);
    if (!stunts_load_car_simd(path_buf, &ctx->player_simd)) {
        snprintf(path_buf, sizeof(path_buf), "%s/ST%s.P3S", data_dir, game_info->player_car_id);
        if (!stunts_load_car_simd(path_buf, &ctx->player_simd)) {
            return false;
        }
    }

    /* 4. Precompute Aerodynamic Drag Tables */
    for (int i = 0; i < 64; i++) {
        ctx->player_aero_table[i] = (int16_t)(((int32_t)ctx->player_simd.aero_resistance * i * i) >> 9);
    }

    /* 5. Find Start Line Position */
    int start_col = 15;
    int start_row = 15;
    int16_t start_yaw = 0;

    /* Start/finish line element codes. [FACT] Verified against the original
     * trkObjectList in dseg.asm: codes 0x01..0x03 map to shape "fini"
     * (elem 0x01 -> game3dshapes "fini"/"zfin"). The previous 0x27..0x2A
     * range was an unverified guess and actually maps to "ramp"/"rban". */
    bool start_found = false;
    for (int r = 0; r < 30 && !start_found; r++) {
        for (int c = 0; c < 30; c++) {
            uint8_t elem = ctx->track_elements[r * 30 + c];
            if (elem >= 0x01 && elem <= 0x03) {
                start_row = r;
                start_col = c;
                /* Orientation still [HYPOTHESIS]: pending verification of the
                 * original track_setup track_angle derivation. */
                switch (elem) {
                    case 0x01: start_yaw = 0; break;
                    case 0x02: start_yaw = 256; break;
                    case 0x03: start_yaw = 512; break;
                }
                start_found = true;
                break;
            }
        }
    }

    /* 6. Initialize Car State */
    /* Start placement [DERIVED from restunts.c init_carstate_from_simd call]:
     *   tmpcol = mul_scale(sin(a+0x200), 210) + mul_scale(sin(a+0x100), 36)
     *   tmprow = mul_scale(cos(a+0x200), 210) + mul_scale(cos(a+0x100), 36)
     * i.e. 210 units back along the track direction and 36 to the side, so
     * the car sits in its lane behind the line rather than on the tile centre.
     * multiply_and_scale(a,b) = (a*b*4) >> 16 with round-half-up; the sim's
     * trig is the same Q14 table, so reuse it here.
     * track_angle = -heading (restunts passes -track_angle as the rotation). */
    {
        uint16_t a = (uint16_t)((-(int32_t)start_yaw) & 0x3FF);
        int32_t tmpcol = mul_scale(stunts_sin((a + 0x200) & 0x3FF), 210)
                       + mul_scale(stunts_sin((a + 0x100) & 0x3FF), 36);
        int32_t tmprow = mul_scale(stunts_cos((a + 0x200) & 0x3FF), 210)
                       + mul_scale(stunts_cos((a + 0x100) & 0x3FF), 36);
        /* Experimental deltas while the start placement is validated against
         * DOSBox captures. */
        { const char* e = getenv("STUNTS_START_DX"); if (e && *e) tmpcol += atoi(e); }
        { const char* e = getenv("STUNTS_START_DZ"); if (e && *e) tmprow += atoi(e); }
        ctx->player_state.pos_world.lx = (int32_t)start_col * 1024 + 512 + tmpcol;
        /* See row-convention note above: world Z maps directly to the .TRK row. */
        ctx->player_state.pos_world.lz = (int32_t)start_row * 1024 + 512 + tmprow;
    }
    ctx->player_state.pos_world.ly = (int32_t)ctx->track_heights[start_row * 30 + start_col] * 1024;
    ctx->player_state.pos_world_sub = ctx->player_state.pos_world;

    ctx->player_state.rotate.y = start_yaw;
    ctx->player_state.curr_rpm = ctx->player_simd.idle_rpm;
    ctx->player_state.last_rpm = ctx->player_simd.idle_rpm;
    ctx->player_state.current_gear = 1;
    ctx->player_state.gear_ratio = ctx->player_simd.gear_ratios[1];
    ctx->player_state.sum_surf_all_wheels = 4;
    ctx->player_state.sum_surf_front_wheels = 2;
    ctx->player_state.sum_surf_rear_wheels = 2;
    ctx->player_state.transmission_auto = game_info->player_transmission;
    ctx->player_state.knob_points[0] = ctx->player_simd.knob_points[1];

    /* Initialize PRNG seed */
    ctx->kevin_prng_seed[0] = 0x53;
    ctx->kevin_prng_seed[1] = 0x74;
    ctx->kevin_prng_seed[2] = 0x75;
    ctx->kevin_prng_seed[3] = 0x6E;
    ctx->kevin_prng_seed[4] = 0x74;
    ctx->kevin_prng_seed[5] = 0x73;

    return true;
}

void stunts_sim_cleanup(stunts_sim_context_t* ctx) {
    if (!ctx) return;
    free(ctx->collision_planes);
    ctx->collision_planes = NULL;
    ctx->plane_count = 0;
}

void stunts_sim_get_canonical_state(const stunts_sim_context_t* ctx, stunts_canonical_state_t* out_state) {
    if (!ctx || !out_state) return;
    memset(out_state, 0, sizeof(stunts_canonical_state_t));

    const stunts_car_state_t* cs = &ctx->player_state;

    out_state->frame_index = ctx->current_frame;
    out_state->game_frame = ctx->sim_game_frame;
    out_state->time_ms = (int32_t)(ctx->current_frame * 50);

    out_state->pos_x = cs->pos_world.lx;
    out_state->pos_y = cs->pos_world.ly;
    out_state->pos_z = cs->pos_world.lz;
    out_state->rot_pitch_x = cs->rotate.x;
    out_state->rot_yaw_y = cs->rotate.y;
    out_state->rot_roll_z = cs->rotate.z;

    out_state->speed_coupled = cs->speed_coupled;
    out_state->speed_actual = cs->speed_actual;
    out_state->speed_last = cs->speed_last;
    out_state->speed_diff = cs->speed_diff;
    out_state->engine_rpm = cs->curr_rpm;
    out_state->current_gear = cs->current_gear;
    out_state->gear_ratio = cs->gear_ratio;
    out_state->is_braking = cs->is_braking;
    out_state->is_accelerating = cs->is_accelerating;
    out_state->steering_angle = cs->steering_angle;

    memcpy(out_state->whl_surf, cs->surface_whl, 4);
    out_state->wheels_on_ground = cs->sum_surf_all_wheels;
    out_state->wheels_front_contact = cs->sum_surf_front_wheels;
    out_state->wheels_rear_contact = cs->sum_surf_rear_wheels;
    memcpy(out_state->whl_force, cs->wheel_forces_rc1, 8);

    out_state->pseudo_gravity = cs->pseudo_gravity;
    out_state->sliding_flag = cs->sliding_flag;
    out_state->demanded_grip = cs->demanded_grip;
    out_state->surface_grip_sum = cs->surface_grip_sum;
    out_state->crash_flag = cs->crash_flag;
    out_state->engine_limiter_timer = cs->engine_limiter_timer;

    out_state->distance_traveled = ctx->total_distance;
    out_state->penalty_counter = ctx->penalty_time;
    out_state->impact_speed = ctx->impact_speed;
    out_state->top_speed = ctx->top_speed;
    out_state->jump_count = ctx->jump_count;
    memcpy(out_state->kevin_seed, ctx->kevin_prng_seed, 6);
}
