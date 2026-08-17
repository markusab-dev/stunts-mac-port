#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/stunts_sim.h"
#include "render/stunts_rasterizer.h"
#include "render/stunts_camera.h"

int main() {
    printf("================================================================================\n");
    printf("            STUNTS FROZEN SIMULATION 300-FRAME STABILITY TEST                   \n");
    printf("================================================================================\n\n");

    const char* data_dir = "extracted/stunts/stunts";
    stunts_game_info_t game_info;
    memset(&game_info, 0, sizeof(game_info));
    strncpy(game_info.track_name, "DEFAULT", 8);
    strncpy(game_info.player_car_id, "COUN", 4);
    game_info.player_transmission = 1;

    stunts_sim_context_t ctx;
    if (!stunts_sim_init(&ctx, data_dir, &game_info)) {
        fprintf(stderr, "Failed to init sim\n");
        return 1;
    }

    /* Target 1440x1080 */
    int width = 1440;
    int height = 1080;

    stunts_rasterizer_t r;
    if (!stunts_rasterizer_init(&r, width, height, data_dir, "COUN")) {
        fprintf(stderr, "Failed to init rasterizer\n");
        return 1;
    }

    stunts_camera_t cam;
    stunts_camera_init(&cam, STUNTS_CAM_CHASE);

    uint32_t* baseline_frame = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    int pixel_mismatches = 0;

    printf("Rendering 300 continuous presentation frames with simulation FROZEN at Frame 0...\n");

    for (int frame = 0; frame < 300; frame++) {
        /* Update camera from frozen car state */
        stunts_camera_update(&cam, &ctx.player_state.pos_world, &ctx.player_state.rotate, ctx.player_state.speed_actual);

        /* Render scene directly to framebuffer */
        stunts_rasterizer_render_scene(&r, &ctx, &cam);

        if (frame == 0) {
            memcpy(baseline_frame, r.pixels, width * height * sizeof(uint32_t));
        } else {
            if (memcmp(baseline_frame, r.pixels, width * height * sizeof(uint32_t)) != 0) {
                printf("  [ERROR] Frame %d differed from baseline frame!\n", frame);
                pixel_mismatches++;
                break;
            }
        }
    }

    if (pixel_mismatches == 0) {
        printf("  [PASS] 300 of 300 rendered frames are 100.0%% BIT-EXACT and IDENTICAL.\n");
        printf("  [PASS] Zero visible flicker, zero movement, perfectly stable geometry.\n\n");
    }

    free(baseline_frame);
    stunts_rasterizer_cleanup(&r);
    stunts_sim_cleanup(&ctx);

    return (pixel_mismatches == 0) ? 0 : 1;
}
