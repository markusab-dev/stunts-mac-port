#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sim/stunts_math.h"
#include "sim/stunts_sim.h"
#include "render/stunts_rasterizer.h"
#include "render/stunts_camera.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} bmp_header_t;
#pragma pack(pop)

static void save_framebuffer_bmp(const char* filepath, const uint32_t* pixels, int width, int height) {
    if (!filepath || !pixels || width <= 0 || height <= 0) return;
    FILE* f = fopen(filepath, "wb");
    if (!f) return;

    int row_bytes = width * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    uint32_t image_size = (row_bytes + pad) * height;

    bmp_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.bfType = 0x4D42; /* "BM" */
    hdr.bfOffBits = sizeof(bmp_header_t);
    hdr.bfSize = hdr.bfOffBits + image_size;
    hdr.biSize = 40;
    hdr.biWidth = width;
    hdr.biHeight = height;
    hdr.biPlanes = 1;
    hdr.biBitCount = 24;
    hdr.biCompression = 0;
    hdr.biSizeImage = image_size;

    fwrite(&hdr, 1, sizeof(hdr), f);

    uint8_t pad_bytes[4] = {0, 0, 0, 0};
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            uint32_t argb = pixels[y * width + x];
            uint8_t b = (uint8_t)(argb & 0xFF);
            uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
            uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
            fputc(b, f);
            fputc(g, f);
            fputc(r, f);
        }
        if (pad > 0) fwrite(pad_bytes, 1, pad, f);
    }
    fclose(f);
}

static void test_modern_render(const char* data_dir, const char* track_name, const char* car_id,
                               int width, int height, const char* out_bmp) {
    stunts_game_info_t game_info;
    memset(&game_info, 0, sizeof(game_info));
    strncpy(game_info.track_name, track_name, 8);
    strncpy(game_info.player_car_id, car_id, 4);
    game_info.player_transmission = 1;

    stunts_sim_context_t ctx;
    if (!stunts_sim_init(&ctx, data_dir, &game_info)) {
        fprintf(stderr, "Failed to init sim for track '%s'\n", track_name);
        return;
    }

    stunts_rasterizer_t r;
    if (!stunts_rasterizer_init(&r, width, height, data_dir, car_id)) {
        fprintf(stderr, "Failed to init rasterizer for car '%s'\n", car_id);
        stunts_sim_cleanup(&ctx);
        return;
    }

    stunts_camera_t cam;
    stunts_camera_init(&cam, STUNTS_CAM_CHASE);
    stunts_camera_update(&cam, &ctx.player_state.pos_world, &ctx.player_state.rotate, 0);

    stunts_rasterizer_render_scene(&r, &ctx, &cam);

    printf("================================================================================\n");
    printf(" MODERN FLAT-SHADED RENDER: Track '%s' | Car '%s' | %dx%d\n", track_name, car_id, width, height);
    printf("================================================================================\n");
    printf("  Objects submitted:    %u\n", r.stats.objects_submitted);
    printf("  Vertices transformed: %u\n", r.stats.vertices_transformed);
    printf("  Polygons submitted:   %u\n", r.stats.polygons_submitted);
    printf("  Backface culled:      %u\n", r.stats.backface_culled);
    printf("  Near clipped:         %u\n", r.stats.near_clipped);
    printf("  Screen clipped:       %u\n", r.stats.screen_clipped);
    printf("  Polygons rasterized:  %u\n", r.stats.polygons_rasterized);
    printf("  Camera Pos:           (%ld, %ld, %ld)\n", (long)cam.pos.lx, (long)cam.pos.ly, (long)cam.pos.lz);
    printf("  Camera Rot:           (pitch=%d, yaw=%d, roll=%d)\n", cam.rot.x, cam.rot.y, cam.rot.z);

    save_framebuffer_bmp(out_bmp, r.pixels, width, height);
    printf("  Saved capture to:     %s\n\n", out_bmp);

    stunts_rasterizer_cleanup(&r);
    stunts_sim_cleanup(&ctx);
}

int main(int argc, char** argv) {
    const char* data_dir = (argc > 1) ? argv[1] : "extracted/stunts/stunts";

    test_modern_render(data_dir, "DEFAULT", "COUN", 1024, 768, "tests/visual_captures/modern_default_coun_1024x768.bmp");
    test_modern_render(data_dir, "FAST2", "P962", 1024, 768, "tests/visual_captures/modern_fast2_p962_1024x768.bmp");
    test_modern_render(data_dir, "ALLJUMPS", "PMIN", 1024, 768, "tests/visual_captures/modern_alljumps_pmin_1024x768.bmp");
    test_modern_render(data_dir, "FUNHILLS", "LM02", 1024, 768, "tests/visual_captures/modern_funhills_lm02_1024x768.bmp");

    return 0;
}
