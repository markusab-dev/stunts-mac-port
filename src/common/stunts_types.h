#ifndef STUNTS_TYPES_H
#define STUNTS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 2D & 3D Integer Coordinate Vectors (Exact 1990 DSI Precision)
 * ------------------------------------------------------------------------- */

typedef struct {
    int16_t x;
    int16_t y;
} stunts_point2d_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} stunts_vector_t;

typedef struct {
    int32_t lx;
    int32_t ly;
    int32_t lz;
} stunts_vector_long_t;

typedef struct {
    int16_t m[3][3]; /* 3x3 fixed-point rotation matrix, scale 1.0 = 16384 (2^14) */
} stunts_matrix_t;

typedef struct {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
} stunts_rectangle_t;

/* -------------------------------------------------------------------------
 * Collision Geometry Structures (plan & wall)
 * ------------------------------------------------------------------------- */

typedef struct {
    int16_t angle_yz;               /* Pitch inclination (0..1023) */
    int16_t angle_xy;               /* Bank/roll inclination (0..1023) */
    stunts_vector_t origin;         /* Plane reference point in tile coordinates */
    stunts_vector_t normal;         /* Normal vector perpendicular to surface */
    stunts_vector_t rotation_matrix[3]; /* 3x3 orientation matrix */
} stunts_plane_t; /* 34 bytes in original GAME.PRE */

typedef struct {
    int16_t x1, z1;
    int16_t x2, z2;
    int16_t y_min, y_max;
    uint16_t flags;
} stunts_wall_t;

/* -------------------------------------------------------------------------
 * Vehicle Physics Parameter Block (struct SIMD - 320 Bytes)
 * ------------------------------------------------------------------------- */

typedef struct {
    uint8_t num_gears;                  /* Number of forward gears (typically 5 or 6) */
    uint8_t simd_unk;
    int16_t car_mass;                   /* Curb weight (units: ~lbs/10) */
    int16_t braking_eff;                /* Braking deceleration force factor */
    int16_t idle_rpm;                   /* Base engine idle RPM (e.g. 1000) */
    int16_t downshift_rpm;              /* Automatic downshift RPM threshold */
    int16_t upshift_rpm;                /* Automatic upshift RPM threshold */
    int16_t max_rpm;                    /* Engine redline / rev-limiter RPM */
    uint16_t gear_ratios[7];            /* Gear transmission ratios (fixed point) */
    stunts_point2d_t knob_points[7];    /* Dashboard gear shifter 2D coordinates */
    int16_t aero_resistance;            /* Aerodynamic drag coefficient */
    uint8_t idle_torque;                /* Engine torque at idle */
    uint8_t torque_curve[104];          /* Torque curve lookup table */
    uint8_t field_a3;
    int16_t grip;                       /* Base tire lateral grip coefficient */
    int16_t field_a6[7];
    int16_t sliding;                    /* Lateral slip angle slide threshold */
    int16_t surface_grip[4];            /* Multipliers: 0=Tarmac, 1=Dirt, 2=Ice, 3=Grass */
    uint8_t simd_unk3[10];
    stunts_point2d_t collide_points[2]; /* Chassis collision bounding box */
    int16_t car_height;                 /* Roof height / center of gravity */
    stunts_vector_t wheel_coords[4];    /* Local (x,y,z) wheel positions: FL, FR, RL, RR */
    uint8_t steeringdots[62];           /* Speed-dependent steering angle lock limits */
    stunts_point2d_t spdcenter;         /* Speedometer gauge needle pivot */
    int16_t spdnumpoints;               /* Speedometer calibration point count */
    uint8_t spdpoints[208];             /* Speedometer deflection angles */
    stunts_point2d_t revcenter;         /* Tachometer gauge needle pivot */
    int16_t revnumpoints;               /* Tachometer calibration point count */
    uint8_t revpoints[256];             /* Tachometer deflection angles */
    int16_t aerorestable[16];           /* High-speed air resistance lookup table */
} stunts_simd_t;

/* -------------------------------------------------------------------------
 * Vehicle Runtime Dynamic State
 * ------------------------------------------------------------------------- */

typedef struct {
    stunts_vector_long_t pos_world;     /* 32-bit world position (X, Y, Z) */
    stunts_vector_long_t pos_world_sub; /* Visual sub-frame position */
    stunts_vector_t rotate;             /* Orientation angles (pitch_x, yaw_y, roll_z) */
    int16_t pseudo_gravity;             /* Ballistic vertical acceleration accumulator */
    int16_t steering_angle;             /* Current front wheel steering angle */
    int16_t curr_rpm;                   /* Current engine RPM */
    int16_t last_rpm;                   /* Previous frame engine RPM */
    int16_t idle_rpm2;
    int16_t speed_diff;                 /* Speed acceleration/deceleration delta */
    uint16_t speed_coupled;             /* Rev-coupled wheel speed (256 * mph) */
    uint16_t speed_actual;              /* True vehicle inertial speed (256 * mph) */
    uint16_t speed_last;                /* Previous frame speed */
    uint16_t gear_ratio;                /* Active gear transmission ratio */
    int16_t demanded_grip;              /* Lateral cornering demand */
    int16_t surface_grip_sum;           /* Sum of active tire friction coefficients */
    int16_t wheel_forces_rc1[4];        /* Per-wheel contact / normal forces */
    int16_t wheel_forces_rc2[4];
    int16_t wheel_forces_rc3[4];
    int16_t wheel_forces_rc4[4];
    int16_t wheel_forces_rc5[4];
    stunts_vector_t wheel_world_pos[4]; /* World coordinates of 4 wheels */
    uint8_t is_braking;                 /* 1 if brake active */
    uint8_t is_accelerating;            /* 1 if throttle active */
    uint8_t current_gear;               /* 0 = Reverse, 1..6 = Forward gears */
    uint8_t sum_surf_front_wheels;      /* Front wheels on ground count (0..2) */
    uint8_t sum_surf_rear_wheels;       /* Rear wheels on ground count (0..2) */
    uint8_t sum_surf_all_wheels;        /* Total wheels on ground (0 = airborne/jump) */
    uint8_t surface_whl[4];             /* Surface material under each wheel */
    uint8_t engine_limiter_timer;       /* Rev-limiter cooldown ticks */
    uint8_t sliding_flag;               /* Lateral drift state machine flag */
    uint8_t crash_flag;                 /* 1 if vehicle has crashed */
    uint8_t changing_gear;              /* 1 during gear shift delay */
    uint8_t transmission_auto;          /* 0 = Manual, 1 = Automatic */
    stunts_point2d_t knob_points[2];    /* Current shifter 2D coordinates */
} stunts_car_state_t;

/* -------------------------------------------------------------------------
 * Replay Header Info (struct GAMEINFO)
 * ------------------------------------------------------------------------- */

typedef struct {
    char player_car_id[5];              /* e.g. "AUDI", "COUN", null-terminated */
    uint8_t player_material;            /* Paint scheme index */
    uint8_t player_transmission;        /* 0 = Manual, 1 = Automatic */
    uint8_t opponent_type;              /* 0 = None, 1..6 = Opponents */
    char opponent_car_id[5];            /* Opponent car ID */
    uint8_t opponent_material;
    uint8_t opponent_transmission;
    char track_name[10];                /* Track name (up to 8 chars + null) */
    uint16_t frames_per_sec;            /* Always 20 */
    uint16_t recorded_frames;           /* Total replay frame count */
} stunts_game_info_t;

/* -------------------------------------------------------------------------
 * Canonical Simulation State (STUNTS_CANONICAL_STATE_V1)
 * ------------------------------------------------------------------------- */

#define STUNTS_CANONICAL_SCHEMA "STUNTS_CANONICAL_STATE_V1"

typedef struct {
    /* Timing */
    uint32_t frame_index;
    uint16_t game_frame;
    int32_t time_ms;

    /* Player Transform */
    int32_t pos_x;
    int32_t pos_y;
    int32_t pos_z;
    int16_t rot_pitch_x;
    int16_t rot_yaw_y;
    int16_t rot_roll_z;

    /* Player Drivetrain */
    uint16_t speed_coupled;
    uint16_t speed_actual;
    uint16_t speed_last;
    int16_t speed_diff;
    int16_t engine_rpm;
    uint8_t current_gear;
    uint16_t gear_ratio;
    uint8_t is_braking;
    uint8_t is_accelerating;
    int16_t steering_angle;

    /* Player Wheels & Contact */
    uint8_t whl_surf[4];
    uint8_t wheels_on_ground;
    uint8_t wheels_front_contact;
    uint8_t wheels_rear_contact;
    int16_t whl_force[4];

    /* Player Dynamics */
    int16_t pseudo_gravity;
    uint8_t sliding_flag;
    int16_t demanded_grip;
    int16_t surface_grip_sum;
    uint8_t crash_flag;
    uint8_t engine_limiter_timer;

    /* Race Metrics & PRNG */
    int32_t distance_traveled;
    int16_t penalty_counter;
    uint16_t impact_speed;
    uint16_t top_speed;
    int16_t jump_count;
    uint8_t kevin_seed[6];

    /* Opponent Transform (if active) */
    bool opponent_active;
    int32_t opp_pos_x;
    int32_t opp_pos_y;
    int32_t opp_pos_z;
    int16_t opp_rot_pitch_x;
    int16_t opp_rot_yaw_y;
    int16_t opp_rot_roll_z;
    uint16_t opp_speed;
    int16_t opp_rpm;
    uint8_t opp_gear;
} stunts_canonical_state_t;

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_TYPES_H */
