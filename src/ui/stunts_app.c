#include "stunts_app.h"
#include "../render/stunts_interp.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window* s_window = NULL;
static SDL_Renderer* s_renderer = NULL;
static SDL_Texture* s_texture = NULL;

static void update_input_state(stunts_app_t* app) {
    const Uint8* state = SDL_GetKeyboardState(NULL);

    uint8_t input = 0;
    /* Accelerate: Up / W */
    if (state[SDL_SCANCODE_UP] || state[SDL_SCANCODE_W]) {
        input |= 0x01;
    }
    /* Brake: Down / S */
    if (state[SDL_SCANCODE_DOWN] || state[SDL_SCANCODE_S]) {
        input |= 0x02;
    }
    /* Steer Right: Right / D */
    if (state[SDL_SCANCODE_RIGHT] || state[SDL_SCANCODE_D]) {
        input |= 0x04;
    }
    /* Steer Left: Left / A */
    if (state[SDL_SCANCODE_LEFT] || state[SDL_SCANCODE_A]) {
        input |= 0x08;
    }
    /* Shift Up: Space / E */
    if (state[SDL_SCANCODE_SPACE] || state[SDL_SCANCODE_E]) {
        input |= 0x10;
    }
    /* Shift Down: LShift / Q */
    if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_Q]) {
        input |= 0x20;
    }

    app->live_input_byte = input;
}

static void recreate_texture(stunts_app_t* app) {
    if (s_texture) {
        SDL_DestroyTexture(s_texture);
        s_texture = NULL;
    }
    s_texture = SDL_CreateTexture(s_renderer,
                                  SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  app->rasterizer.width,
                                  app->rasterizer.height);
    if (app->config.is_original_mode) {
        SDL_SetTextureScaleMode(s_texture, SDL_ScaleModeNearest);
    } else {
        SDL_SetTextureScaleMode(s_texture, SDL_ScaleModeLinear);
    }
}

static void switch_resolution(stunts_app_t* app, int width, int height, bool is_orig) {
    app->config.render_width = width;
    app->config.render_height = height;
    app->config.is_original_mode = is_orig;
    stunts_rasterizer_resize(&app->rasterizer, width, height);
    recreate_texture(app);
    printf("Switched mode: %s (%dx%d direct rasterization)\n",
           is_orig ? "Original 320x200" : "Enhanced HD", width, height);
}

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
            uint8_t rgb[3] = {b, g, r};
            fwrite(rgb, 1, 3, f);
        }
        if (pad > 0) fwrite(pad_bytes, 1, pad, f);
    }
    fclose(f);
}

bool stunts_app_init(stunts_app_t* app, const stunts_app_config_t* config) {
    if (!app || !config) return false;
    memset(app, 0, sizeof(stunts_app_t));
    app->config = *config;

    printf("LIVE RENDER BUILD: PHASE3-DEBUG-002 | Built: %s %s\n", __DATE__, __TIME__);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return false;
    }

    s_window = SDL_CreateWindow("Stunts (4D Sports Driving) — Native Apple Silicon Port",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                config->win_width,
                                config->win_height,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, 0);
    }

    /* Initialize Simulation */
    stunts_game_info_t game_info;
    memset(&game_info, 0, sizeof(game_info));
    strncpy(game_info.player_car_id, config->car_id, 4);
    strncpy(game_info.track_name, config->track_name, 8);
    game_info.player_transmission = 1; // Automatic default for ease of driving

    if (!stunts_sim_init(&app->sim_ctx, config->data_dir, &game_info)) {
        fprintf(stderr, "Failed to initialize Stunts simulation from %s\n", config->data_dir);
        return false;
    }

    /* Initialize Rasterizer */
    if (!stunts_rasterizer_init(&app->rasterizer,
                                config->render_width,
                                config->render_height,
                                config->data_dir,
                                config->car_id)) {
        fprintf(stderr, "Failed to initialize rasterizer\n");
        return false;
    }
    app->rasterizer.render_debug_cube = config->debug_cube;

    /* Initialize Faithful 320x200 Restunts Renderer */
    stunts_render_320_init(&app->render_320, config->data_dir, config->car_id);

    /* Initialize Camera */
    stunts_camera_init(&app->camera, STUNTS_CAM_CHASE);
    stunts_camera_update(&app->camera,
                         &app->sim_ctx.player_state.pos_world,
                         &app->sim_ctx.player_state.rotate,
                         app->sim_ctx.player_state.speed_actual);

    recreate_texture(app);

    app->running = true;
    app->last_tick_ms = SDL_GetTicks64();
    app->sim_accumulator_ms = 0.0;

    return true;
}

static void capture_render_snapshot(const stunts_sim_context_t* ctx, stunts_render_snapshot_t* snap) {
    if (!ctx || !snap) return;
    snap->frame_index = ctx->current_frame;
    snap->car_pos = ctx->player_state.pos_world;
    snap->car_rot = ctx->player_state.rotate;
    snap->steering_angle = ctx->player_state.steering_angle;
    snap->speed_actual = ctx->player_state.speed_actual;
    snap->engine_rpm = ctx->player_state.curr_rpm;
    snap->current_gear = ctx->player_state.current_gear;
    snap->is_braking = ctx->player_state.is_braking;
    snap->is_accelerating = ctx->player_state.is_accelerating;
    snap->sliding_flag = ctx->player_state.sliding_flag;
    snap->crash_flag = ctx->player_state.crash_flag;
    snap->wheels_on_ground = ctx->player_state.sum_surf_all_wheels;
}

void stunts_app_run(stunts_app_t* app) {
    if (!app || !app->running) return;

    SDL_Event ev;
    stunts_render_snapshot_t prev_snap;
    stunts_render_snapshot_t curr_snap;
    memset(&prev_snap, 0, sizeof(prev_snap));
    memset(&curr_snap, 0, sizeof(curr_snap));

    capture_render_snapshot(&app->sim_ctx, &curr_snap);
    prev_snap = curr_snap;

    uint64_t last_stat_time = 0;
    bool saved_pre_present = false;

    while (app->running) {
        /* 1. Poll Window & Keyboard Events */
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                app->running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        app->running = false;
                        break;
                    case SDLK_F1:
                        switch_resolution(app, 320, 200, true);
                        break;
                    case SDLK_F2:
                        switch_resolution(app, 1024, 768, false);
                        break;
                    case SDLK_F3:
                        switch_resolution(app, 1440, 1080, false);
                        break;
                    case SDLK_F4:
                        switch_resolution(app, 1920, 1080, false);
                        break;
                    case SDLK_c:
                        app->camera.mode = (app->camera.mode + 1) % 3;
                        printf("Camera mode: %s\n",
                               app->camera.mode == STUNTS_CAM_CHASE ? "Chase" :
                               app->camera.mode == STUNTS_CAM_COCKPIT ? "Cockpit" : "Overview");
                        break;
                    case SDLK_i:
                        app->config.enable_interpolation = !app->config.enable_interpolation;
                        printf("Visual Interpolation: %s\n",
                               app->config.enable_interpolation ? "ON (Smooth 60/120Hz)" : "OFF (Exact 20Hz)");
                        break;
                    case SDLK_a:
                        app->config.is_widescreen = !app->config.is_widescreen;
                        printf("Aspect Policy: %s\n",
                               app->config.is_widescreen ? "Widescreen" : "Original 4:3 Pillarbox");
                        break;
                    case SDLK_r: {
                        stunts_game_info_t gi = app->sim_ctx.game_info;
                        stunts_sim_cleanup(&app->sim_ctx);
                        stunts_sim_init(&app->sim_ctx, app->config.data_dir, &gi);
                        capture_render_snapshot(&app->sim_ctx, &curr_snap);
                        prev_snap = curr_snap;
                        printf("Restarted track '%s'\n", gi.track_name);
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        update_input_state(app);

        /* 2. Advance 20 Hz Simulation Fixed Time Step (Unless frozen) */
        uint64_t now = SDL_GetTicks64();
        double elapsed = (double)(now - app->last_tick_ms);
        app->last_tick_ms = now;

        if (app->config.freeze_frame < 0) {
            app->sim_accumulator_ms += elapsed;
            if (app->sim_accumulator_ms > 200.0) {
                app->sim_accumulator_ms = 200.0;
            }
            while (app->sim_accumulator_ms >= 50.0) {
                prev_snap = curr_snap;
                stunts_sim_step(&app->sim_ctx, app->live_input_byte);
                capture_render_snapshot(&app->sim_ctx, &curr_snap);
                app->sim_accumulator_ms -= 50.0;
            }
        } else {
            /* Simulation Frozen at specified frame */
            app->sim_accumulator_ms = 0.0;
            prev_snap = curr_snap;
        }

        /* 3. Visual Interpolation */
        float alpha = (float)(app->sim_accumulator_ms / 50.0);
        if (!app->config.enable_interpolation || app->config.freeze_frame >= 0) {
            alpha = 1.0f; /* Pure 20 Hz / Frozen presentation */
        }

        stunts_render_snapshot_t interp_snap;
        stunts_interp_snapshot(&prev_snap, &curr_snap, alpha, &interp_snap);

        /* 4. Update Camera */
        stunts_camera_update(&app->camera,
                             &interp_snap.car_pos,
                             &interp_snap.car_rot,
                             interp_snap.speed_actual);

        /* 5. Render Scene */
        const uint32_t* fb_pixels = app->rasterizer.pixels;
        int fb_pitch = app->rasterizer.pitch;
        int fb_w = app->rasterizer.width;
        int fb_h = app->rasterizer.height;

        if (app->config.is_original_mode) {
            /* Faithful Restunts 320x200 Software Render Path */
            stunts_render_320_frame(&app->render_320, &app->sim_ctx);
            fb_pixels = app->render_320.pixels;
            fb_pitch = app->render_320.pitch;
            fb_w = STUNTS_RENDER_W;
            fb_h = STUNTS_RENDER_H;
        } else {
            /* Direct High-Resolution 3D Rasterization */
            stunts_rasterizer_render_scene(&app->rasterizer, &app->sim_ctx, &app->camera);
        }

        /* Print Live Production Counters Once Per Second */
        if (now - last_stat_time >= 1000) {
            last_stat_time = now;
            int win_w = 0, win_h = 0;
            SDL_GetRendererOutputSize(s_renderer, &win_w, &win_h);

            printf("\n[LIVE PRODUCTION STATS @ %lu ms]\n", (unsigned long)now);
            printf("  track_tiles_parsed:    %u\n", app->rasterizer.stats.track_tiles_parsed);
            printf("  track_instances:       %u\n", app->rasterizer.stats.track_instances);
            printf("  terrain_instances:     %u\n", app->rasterizer.stats.terrain_instances);
            printf("  scenery_instances:     %u\n", app->rasterizer.stats.scenery_instances);
            printf("  car_instances:         %u\n", app->rasterizer.stats.car_instances);
            printf("  objects_submitted:     %u\n", app->rasterizer.stats.objects_submitted);
            printf("  vertices_transformed:  %u\n", app->rasterizer.stats.vertices_transformed);
            printf("  polygons_submitted:    %u\n", app->rasterizer.stats.polygons_submitted);
            printf("  behind_camera:         %u\n", app->rasterizer.stats.behind_camera);
            printf("  near_clipped:          %u\n", app->rasterizer.stats.near_clipped);
            printf("  backface_culled:       %u\n", app->rasterizer.stats.backface_culled);
            printf("  screen_clipped:        %u\n", app->rasterizer.stats.screen_clipped);
            printf("  polygons_rasterized:   %u\n", app->rasterizer.stats.polygons_rasterized);
            printf("  framebuffer:           %dx%d (pitch=%d)\n", fb_w, fb_h, fb_pitch);
            printf("  SDL window pixels:     %dx%d\n", win_w, win_h);
            printf("  camera pos:            (%d, %d, %d)\n", app->camera.pos.lx, app->camera.pos.ly, app->camera.pos.lz);
            printf("  camera rot:            (pitch=%d, yaw=%d, roll=%d)\n", app->camera.rot.x, app->camera.rot.y, app->camera.rot.z);
            fflush(stdout);
        }

        /* 6. Dump Pre-Present Framebuffer on First Rendered Frame */
        if (!saved_pre_present) {
            saved_pre_present = true;
            save_framebuffer_bmp("build/live_pre_present.bmp", fb_pixels, fb_w, fb_h);
            system("sips -s format png build/live_pre_present.bmp --out build/live_pre_present.png > /dev/null 2>&1");
            printf("Dumped initial framebuffer to 'build/live_pre_present.png' (%dx%d)\n", fb_w, fb_h);
            fflush(stdout);
        }

        /* 7. Present to Window */
        SDL_UpdateTexture(s_texture, NULL, fb_pixels, fb_pitch);

        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
        SDL_RenderClear(s_renderer);

        int win_w, win_h;
        SDL_GetRendererOutputSize(s_renderer, &win_w, &win_h);

        SDL_Rect dst;
        if (!app->config.is_widescreen) {
            /* 4:3 Pillarbox presentation */
            int target_w = (win_h * 4) / 3;
            if (target_w > win_w) {
                target_w = win_w;
                int target_h = (win_w * 3) / 4;
                dst.x = 0;
                dst.y = (win_h - target_h) / 2;
                dst.w = target_w;
                dst.h = target_h;
            } else {
                dst.x = (win_w - target_w) / 2;
                dst.y = 0;
                dst.w = target_w;
                dst.h = win_h;
            }
        } else {
            /* Widescreen full window */
            dst.x = 0;
            dst.y = 0;
            dst.w = win_w;
            dst.h = win_h;
        }

        SDL_RenderCopy(s_renderer, s_texture, NULL, &dst);
        SDL_RenderPresent(s_renderer);

        /* Frame Pacing: Cap rendering to 60 FPS (16.6 ms) to drop CPU usage to < 2% */
        uint64_t frame_end = SDL_GetTicks64();
        uint64_t frame_time = frame_end - now;
        if (frame_time < 16) {
            SDL_Delay((Uint32)(16 - frame_time));
        } else {
            SDL_Delay(1);
        }
    }
}

void stunts_app_cleanup(stunts_app_t* app) {
    if (!app) return;
    if (s_texture) {
        SDL_DestroyTexture(s_texture);
        s_texture = NULL;
    }
    if (s_renderer) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    stunts_rasterizer_cleanup(&app->rasterizer);
    stunts_sim_cleanup(&app->sim_ctx);
    SDL_Quit();
}
