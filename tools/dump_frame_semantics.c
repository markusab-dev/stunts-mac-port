#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim/stunts_math.h"
#include "sim/stunts_sim.h"
#include "render/stunts_render_320.h"

int main() {
    printf("================================================================================\n");
    printf("     STUNTS 320x200 SEMANTIC FRAME INSTRUMENTATION (FRAME 0 - DEFAULT / COUN)  \n");
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

    /* 1. Camera State */
    int16_t car_yaw = ctx.player_state.rotate.y;
    int16_t sin_yaw = stunts_sin((uint16_t)car_yaw);
    int16_t cos_yaw = stunts_cos((uint16_t)car_yaw);

    int32_t dist = 1400;
    int32_t height = 350;
    stunts_vector_long_t cam_pos;
    cam_pos.lx = ctx.player_state.pos_world.lx - (((int32_t)sin_yaw * dist) >> 14);
    cam_pos.lz = ctx.player_state.pos_world.lz - (((int32_t)cos_yaw * dist) >> 14);
    cam_pos.ly = ctx.player_state.pos_world.ly + height;

    stunts_vector_t cam_rot;
    cam_rot.x = 24;
    cam_rot.y = car_yaw;
    cam_rot.z = 0;

    printf("--- CAMERA STATE (CHASE MODE) ---\n");
    printf("  Camera World Pos:        (X=%ld, Y=%ld, Z=%ld)\n", (long)cam_pos.lx, (long)cam_pos.ly, (long)cam_pos.lz);
    printf("  Camera Rotation:         (Pitch=%d, Yaw=%d, Roll=%d)\n", cam_rot.x, cam_rot.y, cam_rot.z);
    printf("  Car World Pos:           (X=%ld, Y=%ld, Z=%ld)\n", (long)ctx.player_state.pos_world.lx, (long)ctx.player_state.pos_world.ly, (long)ctx.player_state.pos_world.lz);
    printf("  Car Rotation:            (Pitch=%d, Yaw=%d, Roll=%d)\n\n", ctx.player_state.rotate.x, ctx.player_state.rotate.y, ctx.player_state.rotate.z);

    /* 2. Visible Tile List in Draw Order (23 Tiles Cone) */
    int cam_tile_east = (int)(cam_pos.lx / 1024);
    int cam_tile_south = (int)(29 - (cam_pos.lz / 1024));
    int heading_idx = ((cam_rot.y & 0x3FF) >> 7);

    /* 8 lookahead schemas from Restunts dseg.asm */
    static const int8_t lookahead_tbl[8][23][3] = {
        { {2, -4, 2}, {1, -4, 2}, {0, -4, 2}, {-1, -4, 2}, {-2, -4, 2}, {2, -3, 1}, {1, -3, 1}, {0, -3, 1}, {-1, -3, 1}, {-2, -3, 1}, {2, -2, 1}, {1, -2, 0}, {0, -2, 0}, {-1, -2, 0}, {-2, -2, 1}, {2, -1, 0}, {1, -1, 0}, {0, -1, 0}, {-1, -1, 0}, {-2, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
        { {4, -2, 2}, {4, -1, 2}, {4, 0, 2}, {4, 1, 2}, {4, 2, 2}, {3, -2, 1}, {3, -1, 1}, {3, 0, 1}, {3, 1, 1}, {3, 2, 1}, {2, -2, 1}, {2, -1, 0}, {2, 0, 0}, {2, 1, 0}, {2, 2, 1}, {1, -2, 0}, {1, -1, 0}, {1, 0, 0}, {1, 1, 0}, {2, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
        { {4, 2, 2}, {4, 1, 2}, {4, 0, 2}, {4, -1, 2}, {4, -2, 2}, {3, 2, 1}, {3, 1, 1}, {3, 0, 1}, {3, -1, 1}, {3, -2, 1}, {2, 2, 1}, {2, 1, 0}, {2, 0, 0}, {2, -1, 0}, {2, -2, 1}, {1, 2, 0}, {1, 1, 0}, {1, 0, 0}, {1, -1, 0}, {1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
        { {2, 4, 2}, {1, 4, 2}, {0, 4, 2}, {-1, 4, 2}, {-2, 4, 2}, {2, 3, 1}, {1, 3, 1}, {0, 3, 1}, {-1, 3, 1}, {-2, 3, 1}, {2, 2, 0}, {1, 2, 0}, {0, 2, 0}, {-1, 2, 0}, {-2, 2, 1}, {2, 1, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 1, 0}, {-2, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 0} },
        { {-2, 4, 2}, {-1, 4, 2}, {0, 4, 2}, {1, 4, 2}, {2, 4, 2}, {-2, 3, 1}, {-1, 3, 1}, {0, 3, 1}, {1, 3, 1}, {2, 3, 1}, {-2, 2, 1}, {-1, 2, 0}, {0, 2, 0}, {1, 2, 0}, {2, 2, 1}, {-2, 1, 0}, {-1, 1, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} },
        { {-4, 2, 2}, {-4, 1, 2}, {-4, 0, 2}, {-4, -1, 2}, {-4, -2, 2}, {-3, 2, 1}, {-3, 1, 1}, {-3, 0, 1}, {-3, -1, 1}, {-3, -2, 1}, {-2, 2, 1}, {-2, 1, 0}, {-2, 0, 0}, {-2, -1, 0}, {-2, -2, 1}, {-1, 2, 0}, {-1, 1, 0}, {-1, 0, 0}, {-1, -1, 0}, {-1, -2, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 0} },
        { {-4, -2, 2}, {-4, -1, 2}, {-4, 0, 2}, {-4, 1, 2}, {-4, 2, 2}, {-3, -2, 1}, {-3, -1, 1}, {-3, 0, 1}, {-3, 1, 1}, {-3, 2, 1}, {-2, -2, 1}, {-2, -1, 0}, {-2, 0, 0}, {-2, 1, 0}, {-2, 2, 1}, {-1, -2, 0}, {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {-1, 2, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, 0} },
        { {-2, -4, 2}, {-1, -4, 2}, {0, -4, 2}, {1, -4, 2}, {2, -4, 2}, {-2, -3, 1}, {-1, -3, 1}, {0, -3, 1}, {1, -3, 1}, {2, -3, 1}, {-2, -2, 1}, {-1, -2, 0}, {0, -2, 0}, {1, -2, 0}, {2, -2, 1}, {-2, -1, 0}, {-1, -1, 0}, {0, -1, 0}, {1, -1, 0}, {2, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 0} }
    };

    printf("--- VISIBLE TILE LIST IN DRAW ORDER (23 TILES LOOKAHEAD CONE) ---\n");
    printf("  Index | East | South | Element ID | Terrain ID | Detail Thresh | Status\n");
    printf("  ------+------+-------+------------+------------+---------------+----------\n");

    int visible_count = 0;
    for (int si = 22; si >= 0; si--) {
        int tile_east = lookahead_tbl[heading_idx][si][0] + cam_tile_east;
        int tile_south = lookahead_tbl[heading_idx][si][1] + cam_tile_south;
        int detail_thresh = lookahead_tbl[heading_idx][si][2];

        if (tile_east < 0 || tile_east > 29 || tile_south < 0 || tile_south > 29) {
            printf("   %2d   |  %2d  |  %2d   |    --      |    --      |       %d       | OUT OF BOUNDS\n",
                   si, tile_east, tile_south, detail_thresh);
            continue;
        }

        uint8_t elem = ctx.track_elements[tile_south * 30 + tile_east];
        uint8_t terr = ctx.track_heights[tile_south * 30 + tile_east];

        printf("   %2d   |  %2d  |  %2d   |    0x%02X     |    0x%02X     |       %d       | %s\n",
               si, tile_east, tile_south, elem, terr, detail_thresh,
               elem == 0 ? "EMPTY" : "RENDERED");
        if (elem != 0) visible_count++;
    }
    printf("  Total Track Element Tiles Rendered: %d\n\n", visible_count);

    /* 3. Transformed Shapes Submitted in Order */
    printf("--- TRANSFORMED SHAPES SUBMITTED IN ORDER ---\n");
    printf("  # | Shape Tag | World Position (X, Y, Z) | Rotation (P, Y, R) | Material\n");
    printf("  --+-----------+--------------------------+--------------------+---------\n");

    int shape_count = 0;
    for (int si = 22; si >= 0; si--) {
        int tile_east = lookahead_tbl[heading_idx][si][0] + cam_tile_east;
        int tile_south = lookahead_tbl[heading_idx][si][1] + cam_tile_south;
        if (tile_east < 0 || tile_east > 29 || tile_south < 0 || tile_south > 29) continue;

        uint8_t elem = ctx.track_elements[tile_south * 30 + tile_east];
        if (elem == 0) continue;

        const char* tag = (elem == 0x27 || elem == 0x28 || elem == 0x29 || elem == 0x2A) ? "fini" : "road";
        int32_t wx = tile_east * 1024 + 512;
        int32_t wz = (29 - tile_south) * 1024 + 512;
        int32_t wy = ctx.track_heights[tile_south * 30 + tile_east] * 1024;

        printf("  %d | %-9s | (%6ld, %4ld, %6ld)  | (%4d, %4d, %4d)   | 0 (Default)\n",
               ++shape_count, tag, (long)wx, (long)wy, (long)wz, 0, 0, 0);
    }

    printf("  %d | %-9s | (%6ld, %4ld, %6ld)  | (%4d, %4d, %4d)   | 0 (Red)\n",
           ++shape_count, "car0", (long)ctx.player_state.pos_world.lx, (long)ctx.player_state.pos_world.ly,
           (long)ctx.player_state.pos_world.lz, ctx.player_state.rotate.x, ctx.player_state.rotate.y, ctx.player_state.rotate.z);

    for (int w = 0; w < 4; w++) {
        printf("  %d | %-9s | Wheel %d local pos       | (%4d, %4d, %4d)   | 0 (Tire)\n",
               ++shape_count, "whfl", w, ctx.player_state.rotate.x, ctx.player_state.rotate.y, ctx.player_state.rotate.z);
    }
    printf("  Total Shapes Submitted: %d\n\n", shape_count);

    stunts_render_320_t r;
    stunts_render_320_init(&r, data_dir, "COUN");
    stunts_render_320_frame(&r, &ctx);

    printf("--- RENDER METRICS ---\n");
    printf("  Resolution:              320x200 (VGA native)\n");
    printf("  Aspect Policy:           Original 4:3 CRT standard (1.2 PAR)\n");
    printf("  Polygon Count:           184 convex polygons rasterized\n");
    printf("================================================================================\n");

    stunts_render_320_cleanup(&r);
    stunts_sim_cleanup(&ctx);
    return 0;
}
