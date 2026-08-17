#ifndef STUNTS_MATH_H
#define STUNTS_MATH_H

#include "../common/stunts_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Arithmetic & Wrapping Helpers (Explicit 16-bit x86 Semantics on ARM64)
 * ------------------------------------------------------------------------- */

static inline int16_t stunts_wrap_i16(int32_t v) {
    return (int16_t)(uint16_t)(v);
}

static inline uint16_t stunts_wrap_u16(uint32_t v) {
    return (uint16_t)(v & 0xFFFFu);
}

static inline int16_t stunts_sar16(int16_t val, int shift) {
    /* Implementation-defined in C, but GCC/Clang on ARM64 perform ASR */
    return (int16_t)(val >> shift);
}

static inline int32_t stunts_sar32(int32_t val, int shift) {
    return (int32_t)(val >> shift);
}

static inline int16_t stunts_narrow_i32_to_i16(int32_t v) {
    return (int16_t)(uint16_t)(v & 0xFFFF);
}

/* -------------------------------------------------------------------------
 * Trigonometry (1024 Units = 360 Degrees, Scale 1.0 = 16384)
 * ------------------------------------------------------------------------- */

int16_t stunts_sin(uint16_t angle);
int16_t stunts_cos(uint16_t angle);
int16_t stunts_polar_angle(int16_t z, int16_t y);
int32_t stunts_polar_radius_2d(int16_t z, int16_t y);
int32_t stunts_polar_radius_3d(const stunts_vector_t* vec);

/* -------------------------------------------------------------------------
 * Matrix & Vector Transformations
 * ------------------------------------------------------------------------- */

void stunts_mat_identity(stunts_matrix_t* mat);
void stunts_mat_rot_x(stunts_matrix_t* out, int16_t angle);
void stunts_mat_rot_y(stunts_matrix_t* out, int16_t angle);
void stunts_mat_rot_z(stunts_matrix_t* out, int16_t angle);
void stunts_mat_rot_zxy(stunts_matrix_t* out, int16_t z, int16_t x, int16_t y);
void stunts_mat_multiply(const stunts_matrix_t* a, const stunts_matrix_t* b, stunts_matrix_t* out);
void stunts_mat_invert(const stunts_matrix_t* in, stunts_matrix_t* out);
void stunts_mat_mul_vector(const stunts_vector_t* in_vec, const stunts_matrix_t* mat, stunts_vector_t* out_vec);
void stunts_mat_mul_vector_long(const stunts_vector_long_t* in_vec, const stunts_matrix_t* mat, stunts_vector_long_t* out_vec);

/* -------------------------------------------------------------------------
 * Collision Planes & Geometry Operations
 * ------------------------------------------------------------------------- */

int32_t stunts_vec_normal_inner_product(int16_t x, int16_t y, int16_t z, const stunts_vector_t* normal);
int32_t stunts_plane_height_at(const stunts_plane_t* plane, int16_t tile_x, int16_t tile_z);
bool stunts_rect_intersect(const stunts_rectangle_t* r1, const stunts_rectangle_t* r2);
bool stunts_rect_is_inside(const stunts_rectangle_t* inner, const stunts_rectangle_t* outer);

#ifdef __cplusplus
}
#endif

#endif /* STUNTS_MATH_H */
