#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sim/stunts_sim.h"
#include "sim/stunts_canonical_state.h"
#include "asset/stunts_asset_loader.h"

static void print_usage(const char* prog_name) {
    printf("Stunts Native Simulation & Fidelity Verification Target (ARM64)\n");
    printf("Usage: %s --replay <path.rpl> --data <data_dir> [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  --replay <path>       Path to .RPL replay file (required)\n");
    printf("  --data <path>         Path to Stunts data directory (default: extracted/stunts/stunts)\n");
    printf("  --output <path>       Output canonical state JSON Lines file (.jsonl)\n");
    printf("  --binary <path>       Output compact binary canonical state (.cs1)\n");
    printf("  --max-frames <N>      Limit simulation to N frames\n");
    printf("  --verbose             Print frame-by-frame progress\n");
    printf("  --help                Show this help message\n");
}

int main(int argc, char* argv[]) {
    const char* replay_path = NULL;
    const char* data_dir = "extracted/stunts/stunts";
    const char* output_jsonl_path = NULL;
    const char* output_binary_path = NULL;
    int max_frames = -1;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_jsonl_path = argv[++i];
        } else if (strcmp(argv[i], "--binary") == 0 && i + 1 < argc) {
            output_binary_path = argv[++i];
        } else if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!replay_path) {
        fprintf(stderr, "Error: --replay argument is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    /* 1. Load Replay */
    stunts_game_info_t replay_info;
    uint8_t* inputs = NULL;
    uint16_t input_count = 0;
    if (!stunts_load_replay(replay_path, &replay_info, &inputs, &input_count)) {
        fprintf(stderr, "Error: Failed to load replay '%s'\n", replay_path);
        return 1;
    }

    printf("Loaded Replay: Track='%s', Player='%s', Frames=%u\n",
           replay_info.track_name, replay_info.player_car_id, input_count);

    /* 2. Initialize Simulation */
    stunts_sim_context_t ctx;
    if (!stunts_sim_init(&ctx, data_dir, &replay_info)) {
        fprintf(stderr, "Error: Failed to initialize simulation from data dir '%s'\n", data_dir);
        free(inputs);
        return 1;
    }

    FILE* f_jsonl = NULL;
    if (output_jsonl_path) {
        f_jsonl = fopen(output_jsonl_path, "w");
        if (!f_jsonl) {
            fprintf(stderr, "Warning: Failed to open '%s' for writing\n", output_jsonl_path);
        }
    }

    FILE* f_binary = NULL;
    if (output_binary_path) {
        f_binary = fopen(output_binary_path, "wb");
        if (!f_binary) {
            fprintf(stderr, "Warning: Failed to open '%s' for writing\n", output_binary_path);
        }
    }

    uint32_t total_frames = input_count;
    if (max_frames >= 0 && (uint32_t)max_frames < total_frames) {
        total_frames = (uint32_t)max_frames;
    }

    printf("Simulating %u frames at 20 Hz...\n", total_frames);

    /* 3. Run Discrete 20 Hz Simulation Loop */
    for (uint32_t f = 0; f < total_frames; f++) {
        uint8_t input_byte = inputs[f];
        stunts_sim_step(&ctx, input_byte);

        stunts_canonical_state_t state;
        stunts_sim_get_canonical_state(&ctx, &state);

        if (f_jsonl) {
            stunts_canonical_state_write_jsonl(f_jsonl, &state);
        }
        if (f_binary) {
            stunts_canonical_state_write_binary(f_binary, &state);
        }

        if (verbose && (f % 100 == 0 || f == total_frames - 1)) {
            printf("  Frame %5u | Pos: (%6d, %5d, %6d) | Speed: %3u mph | RPM: %5d | Gear: %d\n",
                   f, state.pos_x, state.pos_y, state.pos_z,
                   state.speed_actual >> 8, state.engine_rpm, state.current_gear);
        }
    }

    if (f_jsonl) fclose(f_jsonl);
    if (f_binary) fclose(f_binary);
    stunts_sim_cleanup(&ctx);
    free(inputs);

    printf("Simulation completed successfully. Total frames: %u\n", total_frames);
    return 0;
}
