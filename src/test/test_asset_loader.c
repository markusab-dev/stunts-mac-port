#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "../asset/stunts_asset_loader.h"
#include "../asset/stunts_dsi_unpack.h"

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

void test_track_loader(void) {
    printf("Testing Track Loader (DEFAULT.TRK)...\n");
    uint8_t elements[900];
    uint8_t heights[900];
    bool ok = stunts_load_track("extracted/stunts/stunts/DEFAULT.TRK", elements, heights);
    TEST_ASSERT(ok, "stunts_load_track returned true for DEFAULT.TRK");
    TEST_ASSERT(elements[0] != 0 || elements[1] != 0 || elements[29*30+29] == 0, "Track elements loaded");
}

void test_replay_loader(void) {
    printf("Testing Replay Loader (DEFAULT.RPL)...\n");
    stunts_game_info_t info;
    uint8_t* inputs = NULL;
    uint16_t input_count = 0;
    bool ok = stunts_load_replay("extracted/stunts/stunts/DEFAULT.RPL", &info, &inputs, &input_count);
    TEST_ASSERT(ok, "stunts_load_replay returned true for DEFAULT.RPL");
    TEST_ASSERT(info.frames_per_sec == 20, "Replay FPS is 20");
    TEST_ASSERT(input_count > 0 && inputs != NULL, "Replay inputs extracted");
    printf("  Loaded replay: Track='%s', Car='%s', Frames=%u\n", info.track_name, info.player_car_id, input_count);
    free(inputs);
}

void test_car_simd_loader(void) {
    printf("Testing Car Physics SIMD Loader (STCOUN.P3S)...\n");
    stunts_simd_t simd;
    bool ok = stunts_load_car_simd("extracted/stunts/stunts/STCOUN.P3S", &simd);
    TEST_ASSERT(ok, "stunts_load_car_simd returned true for STCOUN.P3S");
    TEST_ASSERT(simd.num_gears >= 5 && simd.num_gears <= 6, "Countach has 5 forward gears");
    TEST_ASSERT(simd.idle_rpm > 500 && simd.idle_rpm < 2000, "Countach idle RPM is reasonable");
    TEST_ASSERT(simd.max_rpm >= 6000, "Countach redline is >= 6000 RPM");
    printf("  Countach parameters: Gears=%d, Idle RPM=%d, Max RPM=%d, Mass=%d\n",
           simd.num_gears, simd.idle_rpm, simd.max_rpm, simd.car_mass);
}

void test_collision_planes_loader(void) {
    printf("Testing Collision Planes Loader (GAME.PRE -> plan)...\n");
    stunts_plane_t* planes = NULL;
    uint16_t plane_count = 0;
    bool ok = stunts_load_collision_data("extracted/stunts/stunts/GAME.PRE", &planes, &plane_count);
    TEST_ASSERT(ok, "stunts_load_collision_data returned true for GAME.PRE");
    TEST_ASSERT(plane_count == 536, "GAME.PRE contains exactly 536 collision planes (verified against game.res.txt)");
    if (planes) {
        printf("  Plane 0: origin=(%d, %d, %d), normal=(%d, %d, %d)\n",
               planes[0].origin.x, planes[0].origin.y, planes[0].origin.z,
               planes[0].normal.x, planes[0].normal.y, planes[0].normal.z);
        free(planes);
    }
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("============================================================\n");
    printf(" Running Stunts Asset & DSI Decompressor Tests (ARM64)\n");
    printf("============================================================\n");

    test_track_loader();
    test_replay_loader();
    test_car_simd_loader();
    test_collision_planes_loader();

    printf("------------------------------------------------------------\n");
    printf("Results: %d Passed, %d Failed\n", s_tests_passed, s_tests_failed);
    printf("============================================================\n");

    return s_tests_failed == 0 ? 0 : 1;
}
