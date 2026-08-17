#include "stunts_palette.h"
#include "../asset/stunts_asset_loader.h"
#include <string.h>
#include <stdio.h>

/* Material color remap table from Restunts */
static const uint8_t s_material_color_list[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x6C, 0x74, 0x0F, 0x1C, 0x1D, 0x0E, 0x1C, 0x1F,
    0x0E, 0xC8, 0xC6, 0xC4, 0x70, 0x72, 0x74, 0xC2,
    0xC5, 0xC8, 0x92, 0x25, 0x23, 0xB5, 0x1D, 0x1F,
    0x13, 0x03, 0x0B, 0x1B, 0x00, 0x04, 0x04, 0x0C,
    0x9C, 0x9A, 0x98, 0x96, 0x2A, 0x28, 0x26, 0x25,
    0x1B, 0x1A, 0x19, 0x18, 0x48, 0x46, 0x44, 0x42,
    0x7B, 0x79, 0x78, 0x75, 0x5C, 0x5A, 0x58, 0x57,
    0xAD, 0xAB, 0xA9, 0xA7, 0x14, 0x13, 0x12, 0x11,
    0x4D, 0x4C, 0x4A, 0x49, 0x2D, 0x2C, 0x2A, 0x29,
    0x9F, 0xAF, 0xAE, 0xAC, 0x1D, 0x1C, 0x12, 0x5A,
    0x0F, 0x07, 0xC8, 0xDB, 0x88, 0x63, 0x65, 0x67,
    0x68, 0x6A, 0x11, 0x14, 0x3C, 0x4D, 0x2E, 0x3D,
    0x2D, 0xCA, 0xBE, 0xBA, 0xB7, 0xB4, 0x00, 0x1C,
    0x1E, 0x10, 0x14, 0x44, 0x36, 0x27, 0x2B, 0x0C,
    0x11
};

bool stunts_palette_load(const char* sdmain_path, stunts_palette_t* out_palette) {
    if (!sdmain_path || !out_palette) return false;

    stunts_res_archive_t* arc = stunts_asset_load_archive(sdmain_path);
    if (!arc) {
        stunts_palette_init_default(out_palette);
        return false;
    }

    const stunts_sub_resource_t* res = stunts_asset_find_resource(arc, "!pal");
    if (!res || res->size < 16 + 768) {
        stunts_asset_free_archive(arc);
        stunts_palette_init_default(out_palette);
        return false;
    }

    const uint8_t* dac_bytes = res->data + 16;
    for (int i = 0; i < 256; i++) {
        uint8_t r6 = dac_bytes[i * 3 + 0];
        uint8_t g6 = dac_bytes[i * 3 + 1];
        uint8_t b6 = dac_bytes[i * 3 + 2];

        out_palette->colors[i].r = (uint8_t)((r6 << 2) | (r6 >> 4));
        out_palette->colors[i].g = (uint8_t)((g6 << 2) | (g6 >> 4));
        out_palette->colors[i].b = (uint8_t)((b6 << 2) | (b6 >> 4));
        out_palette->colors[i].a = 255;
    }

    stunts_asset_free_archive(arc);
    return true;
}

void stunts_palette_init_default(stunts_palette_t* out_palette) {
    if (!out_palette) return;

    /* Standard 16-color EGA/VGA palette */
    static const uint8_t s_std_vga[16][3] = {
        {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
        {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
        {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
        {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}
    };

    for (int i = 0; i < 16; i++) {
        out_palette->colors[i].r = s_std_vga[i][0];
        out_palette->colors[i].g = s_std_vga[i][1];
        out_palette->colors[i].b = s_std_vga[i][2];
        out_palette->colors[i].a = 255;
    }

    /* Grayscale & custom ramps for 16..255 */
    for (int i = 16; i < 256; i++) {
        uint8_t v = (uint8_t)(i);
        out_palette->colors[i].r = v;
        out_palette->colors[i].g = v;
        out_palette->colors[i].b = v;
        out_palette->colors[i].a = 255;
    }
}

uint8_t stunts_material_to_color(uint8_t material_id, uint8_t paint_job) {
    (void)paint_job;
    if (material_id < sizeof(s_material_color_list)) {
        return s_material_color_list[material_id];
    }
    return material_id;
}
