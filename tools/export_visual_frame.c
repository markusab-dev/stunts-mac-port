#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sim/stunts_sim.h"
#include "asset/stunts_asset_loader.h"
#include "render/stunts_rasterizer.h"
#include "render/stunts_camera.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPHeader;

typedef struct {
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
} BMPInfoHeader;
#pragma pack(pop)

static bool save_bmp(const char* filepath, int width, int height, const uint32_t* pixels) {
    FILE* f = fopen(filepath, "wb");
    if (!f) return false;

    BMPHeader hdr;
    BMPInfoHeader info;
    memset(&hdr, 0, sizeof(hdr));
    memset(&info, 0, sizeof(info));

    hdr.bfType = 0x4D42; // "BM"
    hdr.bfOffBits = sizeof(BMPHeader) + sizeof(BMPInfoHeader);
    hdr.bfSize = hdr.bfOffBits + (width * height * 4);

    info.biSize = sizeof(BMPInfoHeader);
    info.biWidth = width;
    info.biHeight = -height; // Top-down
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = 0; // BI_RGB
    info.biSizeImage = width * height * 4;

    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(&info, sizeof(info), 1, f);

    // Stunts rasterizer outputs (A<<24)|(R<<16)|(G<<8)|B. Windows BMP 32-bit is BGRA.
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t p = pixels[y * width + x];
            uint8_t b = p & 0xFF;
            uint8_t g = (p >> 8) & 0xFF;
            uint8_t r = (p >> 16) & 0xFF;
            uint8_t a = (p >> 24) & 0xFF;
            uint8_t bgra[4] = { b, g, r, a };
            fwrite(bgra, 4, 1, f);
        }
    }

    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    const char* replay_path = "tests/replays/01_coun_accel_topspeed.rpl";
    const char* data_dir = "extracted/stunts/stunts";
    const char* out_bmp = "tests/visual_captures/frame_000.bmp";
    int target_frame = 50;
    int render_w = 1440;
    int render_h = 1080;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) replay_path = argv[++i];
        else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) data_dir = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) out_bmp = argv[++i];
        else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) target_frame = atoi(argv[++i]);
        else if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &render_w, &render_h);
        }
    }

    stunts_game_info_t game_info;
    uint8_t* inputs = NULL;
    uint16_t input_count = 0;

    if (!stunts_load_replay(replay_path, &game_info, &inputs, &input_count)) {
        fprintf(stderr, "Failed to load replay %s\n", replay_path);
        return 1;
    }

    stunts_sim_context_t ctx;
    if (!stunts_sim_init(&ctx, data_dir, &game_info)) {
        fprintf(stderr, "Failed to init sim\n");
        return 1;
    }

    /* Advance simulation to target frame */
    for (int f = 0; f < target_frame && f < input_count; f++) {
        stunts_sim_step(&ctx, inputs[f]);
    }

    stunts_rasterizer_t r;
    if (!stunts_rasterizer_init(&r, render_w, render_h, data_dir, game_info.player_car_id)) {
        fprintf(stderr, "Failed to init rasterizer\n");
        return 1;
    }

    stunts_camera_t cam;
    stunts_camera_init(&cam, STUNTS_CAM_CHASE);
    stunts_camera_update(&cam, &ctx.player_state.pos_world, &ctx.player_state.rotate, ctx.player_state.speed_actual);

    stunts_rasterizer_render_scene(&r, &ctx, &cam);

    if (save_bmp(out_bmp, render_w, render_h, r.pixels)) {
        printf("Saved visual capture to '%s' (%dx%d, frame=%d, car=%s, track=%s)\n",
               out_bmp, render_w, render_h, target_frame, game_info.player_car_id, game_info.track_name);
    }

    stunts_rasterizer_cleanup(&r);
    stunts_sim_cleanup(&ctx);
    free(inputs);

    return 0;
}
