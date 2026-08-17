#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/stunts_sim.h"
#include "render/stunts_render_320.h"

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

static void save_bmp(const char* filepath, const uint32_t* pixels, int width, int height) {
    FILE* f = fopen(filepath, "wb");
    if (!f) return;

    int row_bytes = width * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    uint32_t image_size = (row_bytes + pad) * height;

    bmp_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.bfType = 0x4D42;
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
            uint8_t rgb[3] = {b, g, r};
            fwrite(rgb, 1, 3, f);
        }
        if (pad > 0) fwrite(pad_bytes, 1, pad, f);
    }
    fclose(f);
}

int main() {
    printf("================================================================================\n");
    printf("       STUNTS FAITHFUL 320x200 RESTUNTS REBASE RENDER HARNESS                   \n");
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

    stunts_render_320_t r;
    if (!stunts_render_320_init(&r, data_dir, "COUN")) {
        fprintf(stderr, "Failed to init 320x200 renderer\n");
        return 1;
    }

    printf("Rendering faithful 320x200 frame with Restunts 23-tile lookahead cone...\n");
    stunts_render_320_frame(&r, &ctx);

    const char* bmp_path = "tests/visual_captures/rebase_320x200.bmp";
    const char* png_path = "tests/visual_captures/rebase_320x200.png";
    save_bmp(bmp_path, r.pixels, STUNTS_RENDER_W, STUNTS_RENDER_H);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sips -s format png %s --out %s > /dev/null 2>&1", bmp_path, png_path);
    system(cmd);

    printf("Saved rebase render capture to '%s'\n", png_path);

    stunts_render_320_cleanup(&r);
    stunts_sim_cleanup(&ctx);
    return 0;
}
