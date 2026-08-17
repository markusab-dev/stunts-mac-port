#include "ui/stunts_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char* prog_name) {
    printf("Stunts (4D Sports Driving) — Native Apple Silicon Port (Phase 3)\n\n");
    printf("Usage:\n");
    printf("  %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  --track <name>       Track name (default: DEFAULT)\n");
    printf("  --car <id>           Car ID (COUN, LANC, VETT, P962, LM02, AUDI, FGTO, JAGU, PC04, ANSX, PMIN)\n");
    printf("  --original           Start in original 320x200 4:3 presentation mode\n");
    printf("  --res <WxH>          Target rendering resolution (e.g. 1024x768, 1440x1080, 1920x1080)\n");
    printf("  --smooth             Enable 60/120Hz visual snapshot interpolation\n");
    printf("  --widescreen         Enable widescreen presentation\n");
    printf("  --data <dir>         Stunts game asset directory (default: extracted/stunts/stunts)\n");
    printf("  --help               Display this help message\n\n");
    printf("Live Controls:\n");
    printf("  Arrow Keys / WASD    Accelerate, Brake, Steer Left/Right\n");
    printf("  Space / Left Shift   Shift Gear Up / Down (Manual mode)\n");
    printf("  F1 / F2 / F3 / F4    Switch Render Mode (320x200, 1024x768, 1440x1080, 1920x1080)\n");
    printf("  C                    Cycle Camera (Chase / Cockpit / Overview)\n");
    printf("  I                    Toggle Smooth Visual Interpolation\n");
    printf("  A                    Toggle 4:3 Pillarbox vs Widescreen\n");
    printf("  R                    Restart Track\n");
    printf("  ESC                  Quit\n");
}

int main(int argc, char** argv) {
    stunts_app_config_t config;
    memset(&config, 0, sizeof(config));

    config.win_width = 1440;
    config.win_height = 1080;
    config.render_width = 1440;
    config.render_height = 1080;
    config.is_original_mode = false;
    config.enable_interpolation = true;
    config.is_widescreen = false;
    config.data_dir = "extracted/stunts/stunts";
    config.track_name = "DEFAULT";
    config.car_id = "COUN";

    config.freeze_frame = -1;
    config.debug_cube = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
            config.track_name = argv[++i];
        } else if (strcmp(argv[i], "--car") == 0 && i + 1 < argc) {
            config.car_id = argv[++i];
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if (strcmp(argv[i], "--modern") == 0) {
            config.is_original_mode = false;
        } else if (strcmp(argv[i], "--original") == 0 || strcmp(argv[i], "--faithful") == 0) {
            config.is_original_mode = true;
            config.render_width = 320;
            config.render_height = 200;
            config.enable_interpolation = false;
        } else if (strcmp(argv[i], "--smooth") == 0) {
            config.enable_interpolation = true;
        } else if (strcmp(argv[i], "--widescreen") == 0) {
            config.is_widescreen = true;
        } else if (strcmp(argv[i], "--debug-cube") == 0) {
            config.debug_cube = true;
        } else if (strcmp(argv[i], "--freeze-frame") == 0 && i + 1 < argc) {
            config.freeze_frame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                config.render_width = w;
                config.render_height = h;
            }
        }
    }

    printf("================================================================================\n");
    printf("         Stunts (4D Sports Driving) — Native Apple Silicon Modern Port          \n");
    printf("================================================================================\n");
    printf("Track:         %s\n", config.track_name);
    printf("Car:           %s\n", config.car_id);
    printf("Resolution:    %dx%d (%s)\n", config.render_width, config.render_height,
           config.is_original_mode ? "Original 320x200" : "Enhanced HD Direct Rasterization");
    printf("Interpolation: %s\n", config.enable_interpolation ? "Enabled (Smooth 60/120Hz)" : "Disabled (20Hz)");
    printf("Aspect Policy: %s\n", config.is_widescreen ? "Widescreen" : "Original 4:3 Pillarbox");
    printf("================================================================================\n\n");

    stunts_app_t app;
    if (!stunts_app_init(&app, &config)) {
        fprintf(stderr, "Initialization failed.\n");
        return 1;
    }

    stunts_app_run(&app);
    stunts_app_cleanup(&app);

    return 0;
}
