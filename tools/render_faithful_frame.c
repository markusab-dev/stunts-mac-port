/*
 * render_faithful_frame.c - Static-frame test harness for the ported
 * restunts renderer.
 *
 * Runs the simulation N frames into a replay, renders one frame through the
 * ported original pipeline (update_frame), and writes the 320x200 result as
 * a BMP using the original VGA palette from SDMAIN.PVS.
 *
 * Usage:
 *   render_faithful_frame --data <dir> --replay <file.rpl> --frame <N>
 *                         --out <file.bmp> [--camera 0|1|2|3]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/sim/stunts_sim.h"
#include "../src/asset/stunts_asset_loader.h"
#include "../src/render/stunts_palette.h"

extern uint8_t rfb_pixels[320 * 200];
extern unsigned short polyinfonumpolys;
extern unsigned char transformedshape_counter;
void rfaithful_init(const char* data_dir, const stunts_sim_context_t* ctx);
void rfaithful_render(const stunts_sim_context_t* ctx);
void rfaithful_set_camera_mode(int16_t mode);

static void write_bmp(const char* path, const uint8_t* idx_pixels,
                      const stunts_palette_t* pal, int w, int h)
{
	FILE* f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }

	uint32_t row_bytes = (uint32_t)w * 3;
	uint32_t pad = (4 - (row_bytes % 4)) % 4;
	uint32_t img_size = (row_bytes + pad) * (uint32_t)h;
	uint32_t file_size = 54 + img_size;

	uint8_t hdr[54] = {0};
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &file_size, 4);
	hdr[10] = 54;
	hdr[14] = 40;
	memcpy(hdr + 18, &w, 4);
	memcpy(hdr + 22, &h, 4);
	hdr[26] = 1;
	hdr[28] = 24;
	memcpy(hdr + 34, &img_size, 4);
	fwrite(hdr, 1, 54, f);

	for (int y = h - 1; y >= 0; y--) {
		for (int x = 0; x < w; x++) {
			stunts_color_rgba_t c = pal->colors[idx_pixels[y * w + x]];
			uint8_t bgr[3] = { c.b, c.g, c.r };
			fwrite(bgr, 1, 3, f);
		}
		for (uint32_t p = 0; p < pad; p++) fputc(0, f);
	}
	fclose(f);
}

int main(int argc, char** argv)
{
	const char* data_dir = NULL;
	const char* replay_path = NULL;
	const char* out_path = "faithful_frame.bmp";
	int target_frame = 0;
	int camera = 2;

	for (int i = 1; i < argc - 1; i++) {
		if (!strcmp(argv[i], "--data")) data_dir = argv[++i];
		else if (!strcmp(argv[i], "--replay")) replay_path = argv[++i];
		else if (!strcmp(argv[i], "--frame")) target_frame = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--out")) out_path = argv[++i];
		else if (!strcmp(argv[i], "--camera")) camera = atoi(argv[++i]);
	}
	if (!data_dir || !replay_path) {
		fprintf(stderr, "usage: %s --data <dir> --replay <rpl> --frame <N> --out <bmp> [--camera N]\n", argv[0]);
		return 1;
	}

	stunts_game_info_t info;
	uint8_t* inputs = NULL;
	uint16_t input_count = 0;
	if (!stunts_load_replay(replay_path, &info, &inputs, &input_count)) {
		fprintf(stderr, "failed to load replay %s\n", replay_path);
		return 1;
	}

	static stunts_sim_context_t ctx;
	if (!stunts_sim_init(&ctx, data_dir, &info)) {
		fprintf(stderr, "failed to init sim\n");
		return 1;
	}

	for (int f = 0; f < target_frame && f < input_count; f++)
		stunts_sim_step(&ctx, inputs[f]);

	printf("sim at frame %u: car pos (%d, %d, %d) rot (%d, %d, %d)\n",
	       ctx.current_frame,
	       ctx.player_state.pos_world.lx, ctx.player_state.pos_world.ly,
	       ctx.player_state.pos_world.lz, ctx.player_state.rotate.x,
	       ctx.player_state.rotate.y, ctx.player_state.rotate.z);

	rfaithful_init(data_dir, &ctx);
	rfaithful_set_camera_mode((int16_t)camera);
	rfaithful_render(&ctx);

	stunts_palette_t pal;
	{
		char sdmain[600];
		snprintf(sdmain, sizeof(sdmain), "%s/SDMAIN.PVS", data_dir);
		if (!stunts_palette_load(sdmain, &pal))
			stunts_palette_init_default(&pal);
	}

	printf("polys queued=%u  transformed shapes=%u\n", polyinfonumpolys, transformedshape_counter);
	write_bmp(out_path, rfb_pixels, &pal, 320, 200);
	printf("wrote %s\n", out_path);
	return 0;
}
