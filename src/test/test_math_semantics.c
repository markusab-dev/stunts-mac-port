#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "../sim/stunts_math.h"

static int s_tests_passed = 0;
static int s_tests_failed = 0;

#define TEST_ASSERT(expr, msg) do { \
    if (expr) { \
        s_tests_passed++; \
    } else { \
        s_tests_failed++; \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
    } \
} while(0)

void test_integer_wrapping(void) {
    printf("Testing 16-bit Integer Arithmetic Wrapping Semantics...\n");

    /* 1. Signed 16-bit overflow: 0x7FFF (32767) + 1 => -32768 */
    int32_t a = 0x7FFF;
    int16_t wrapped_signed = stunts_wrap_i16(a + 1);
    TEST_ASSERT(wrapped_signed == -32768, "0x7FFF + 1 signed wrap must equal -32768");

    /* 2. Unsigned 16-bit overflow: 0xFFFF (65535) + 1 => 0 */
    uint32_t u = 0xFFFF;
    uint16_t wrapped_unsigned = stunts_wrap_u16(u + 1);
    TEST_ASSERT(wrapped_unsigned == 0, "0xFFFF + 1 unsigned wrap must equal 0");

    /* 3. Narrowing 32-bit to 16-bit with sign extension */
    int32_t big_val = 0x12347FFF;
    int16_t narrowed = stunts_narrow_i32_to_i16(big_val);
    TEST_ASSERT(narrowed == 0x7FFF, "32-bit narrowing must preserve low 16 bits");

    /* 4. Arithmetic Right Shift (ASR) of negative 16-bit value */
    int16_t neg = -32768;
    int16_t shifted = stunts_sar16(neg, 1);
    TEST_ASSERT(shifted == -16384, "-32768 >> 1 must equal -16384 (sign preserved)");

    int16_t neg_odd = -7;
    int16_t shifted_odd = stunts_sar16(neg_odd, 1);
    TEST_ASSERT(shifted_odd == -4, "-7 >> 1 must equal -4 (floored towards -inf)");

    /* 5. Signed division and modulo semantics */
    int32_t div_neg = -7 / 2;
    int32_t mod_neg = -7 % 2;
    TEST_ASSERT(div_neg == -3, "-7 / 2 in C99/C11 must equal -3");
    TEST_ASSERT(mod_neg == -1, "-7 % 2 in C99/C11 must equal -1");
}

void test_trigonometry(void) {
    printf("Testing 1024-degree Trigonometry Tables...\n");

    /* Cardinal Angles: 0, 90 deg (256), 180 deg (512), 270 deg (768), 360 deg (1024) */
    TEST_ASSERT(stunts_sin(0) == 0, "sin(0) == 0");
    TEST_ASSERT(stunts_sin(256) == 16384, "sin(90 deg / 256) == 16384 (1.0)");
    TEST_ASSERT(stunts_sin(512) == 0, "sin(180 deg / 512) == 0");
    TEST_ASSERT(stunts_sin(768) == -16384, "sin(270 deg / 768) == -16384 (-1.0)");
    TEST_ASSERT(stunts_sin(1024) == 0, "sin(360 deg / 1024) == 0 (periodic wrap)");

    TEST_ASSERT(stunts_cos(0) == 16384, "cos(0) == 16384 (1.0)");
    TEST_ASSERT(stunts_cos(256) == 0, "cos(90 deg / 256) == 0");
    TEST_ASSERT(stunts_cos(512) == -16384, "cos(180 deg / 512) == -16384 (-1.0)");
    TEST_ASSERT(stunts_cos(768) == 0, "cos(270 deg / 768) == 0");
    TEST_ASSERT(stunts_cos(1024) == 16384, "cos(360 deg / 1024) == 16384 (periodic wrap)");

    /* 45 degree angle (128 units): sin(45 deg) = 0.7071 * 16384 = 11585 */
    TEST_ASSERT(stunts_sin(128) == 11585, "sin(45 deg / 128) == 11585");
    TEST_ASSERT(stunts_cos(128) == 11585, "cos(45 deg / 128) == 11585");

    /* polarAngle tests */
    TEST_ASSERT(stunts_polar_angle(0, 0) == 0, "polarAngle(0, 0) == 0");
    TEST_ASSERT(stunts_polar_angle(0, 100) == 0, "polarAngle(0, 100) == 0 (North / 0 deg)");
    TEST_ASSERT(stunts_polar_angle(100, 0) == 256, "polarAngle(100, 0) == 256 (East / 90 deg)");
    TEST_ASSERT(stunts_polar_angle(0, -100) == 512, "polarAngle(0, -100) == 512 (South / 180 deg)");
    TEST_ASSERT(stunts_polar_angle(-100, 0) == -256, "polarAngle(-100, 0) == -256 (West / 270 deg)");
    TEST_ASSERT(stunts_polar_angle(100, 100) == 128, "polarAngle(100, 100) == 128 (North-East / 45 deg)");
}

void test_matrix_transforms(void) {
    printf("Testing 3x3 Matrix Transformations & Vectors...\n");

    stunts_matrix_t id, rot_y, rot_inv, mul_res;
    stunts_mat_identity(&id);
    TEST_ASSERT(id.m[0][0] == 16384 && id.m[1][1] == 16384 && id.m[2][2] == 16384, "Identity matrix diagonal is 16384");
    TEST_ASSERT(id.m[0][1] == 0 && id.m[1][0] == 0, "Identity off-diagonals are 0");

    /* 90 degree yaw rotation (256 units) */
    stunts_mat_rot_y(&rot_y, 256);
    stunts_vector_t forward = { 0, 0, 1024 }; /* Vector pointing North (+Z) */
    stunts_vector_t turned;
    stunts_mat_mul_vector(&forward, &rot_y, &turned);

    /* Yaw by +90 deg turns +Z vector to +X vector (East) */
    TEST_ASSERT(abs(turned.x - 1024) <= 1, "Yaw 90 deg rotated forward vector X to ~1024");
    TEST_ASSERT(turned.y == 0, "Yaw rotation Y remains 0");
    TEST_ASSERT(abs(turned.z) <= 1, "Yaw 90 deg rotated forward vector Z to ~0");

    /* Inversion (Transpose) check */
    stunts_mat_invert(&rot_y, &rot_inv);
    stunts_mat_multiply(&rot_y, &rot_inv, &mul_res);
    TEST_ASSERT(abs(mul_res.m[0][0] - 16384) <= 2, "M * M^-1 [0][0] == 16384");
    TEST_ASSERT(abs(mul_res.m[1][1] - 16384) <= 2, "M * M^-1 [1][1] == 16384");
    TEST_ASSERT(abs(mul_res.m[2][2] - 16384) <= 2, "M * M^-1 [2][2] == 16384");
    TEST_ASSERT(abs(mul_res.m[0][1]) <= 2, "M * M^-1 [0][1] == 0");
}

void test_plane_math(void) {
    printf("Testing Track Surface Collision Plane Height Solver...\n");

    /* Horizontal plane at Y=500 */
    stunts_plane_t flat_plane = {
        .angle_yz = 0,
        .angle_xy = 0,
        .origin = { 512, 500, 512 },
        .normal = { 0, 16384, 0 }
    };
    int32_t h1 = stunts_plane_height_at(&flat_plane, 100, 200);
    TEST_ASSERT(h1 == 500, "Flat horizontal plane height is constant 500");

    /* 45-degree ramp climbing in +Z: normal = (0, 11585, -11585) */
    stunts_plane_t ramp_plane = {
        .angle_yz = 128,
        .angle_xy = 0,
        .origin = { 512, 0, 0 },
        .normal = { 0, 11585, -11585 }
    };
    int32_t h_z0 = stunts_plane_height_at(&ramp_plane, 512, 0);
    int32_t h_z512 = stunts_plane_height_at(&ramp_plane, 512, 512);
    int32_t h_z1024 = stunts_plane_height_at(&ramp_plane, 512, 1024);

    TEST_ASSERT(h_z0 == 0, "Ramp start Z=0 elevation is 0");
    TEST_ASSERT(abs(h_z512 - 512) <= 1, "Ramp mid Z=512 elevation is 512");
    TEST_ASSERT(abs(h_z1024 - 1024) <= 1, "Ramp end Z=1024 elevation is 1024");
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("============================================================\n");
    printf(" Running Stunts Compiler-Semantic & Math Unit Tests (ARM64)\n");
    printf("============================================================\n");

    test_integer_wrapping();
    test_trigonometry();
    test_matrix_transforms();
    test_plane_math();

    printf("------------------------------------------------------------\n");
    printf("Results: %d Passed, %d Failed\n", s_tests_passed, s_tests_failed);
    printf("============================================================\n");

    return s_tests_failed == 0 ? 0 : 1;
}
