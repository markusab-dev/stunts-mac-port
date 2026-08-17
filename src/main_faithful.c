/*
 * main_faithful.c - Playable SDL2 shell around the ported original renderer.
 *
 * Renders the authentic 320x200 frame produced by the vendored restunts
 * pipeline (update_frame -> get_a_poly_info -> span blitters) into an SDL
 * texture, presented with integer 4:3 scaling.
 *
 * The simulation steps at the original 20 Hz; presentation runs at 60 FPS.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "sim/stunts_sim.h"
#include "render/stunts_palette.h"

extern uint8_t rfb_pixels[320 * 200];
void rfaithful_init(const char* data_dir, const stunts_sim_context_t* ctx);
void rfaithful_render(const stunts_sim_context_t* ctx);
void rfaithful_set_camera_mode(int16_t mode);

#define VIEW_W 320
#define VIEW_H 200

int main(int argc, char** argv)
{
	const char* data_dir = "extracted/stunts/stunts";
	const char* track_name = "DEFAULT";
	const char* car_id = "COUN";
	int scale = 4;
	/* Self-test: run headless for N sim frames with throttle held, dumping
	 * BMPs, then exit. Verifies the loop drives the renderer over time. */
	int selftest_frames = 0;
	const char* selftest_dir = ".";

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data_dir = argv[++i];
		else if (!strcmp(argv[i], "--track") && i + 1 < argc) track_name = argv[++i];
		else if (!strcmp(argv[i], "--car") && i + 1 < argc) car_id = argv[++i];
		else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--selftest") && i + 1 < argc) selftest_frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--selftest-dir") && i + 1 < argc) selftest_dir = argv[++i];
	}

	stunts_game_info_t info;
	memset(&info, 0, sizeof(info));
	snprintf(info.player_car_id, sizeof(info.player_car_id), "%s", car_id);
	snprintf(info.track_name, sizeof(info.track_name), "%s", track_name);
	info.player_transmission = 1; /* automatic */
	info.frames_per_sec = 20;

	static stunts_sim_context_t ctx;
	if (!stunts_sim_init(&ctx, data_dir, &info)) {
		fprintf(stderr, "sim init failed (data dir '%s', track '%s')\n",
		        data_dir, track_name);
		return 1;
	}

	rfaithful_init(data_dir, &ctx);

	stunts_palette_t pal;
	{
		char sdmain[600];
		snprintf(sdmain, sizeof(sdmain), "%s/SDMAIN.PVS", data_dir);
		if (!stunts_palette_load(sdmain, &pal))
			stunts_palette_init_default(&pal);
	}

	if (selftest_frames > 0) {
		/* Headless: step the sim with throttle held and dump a few frames. */
		char path[700];
		for (int f = 1; f <= selftest_frames; f++) {
			stunts_sim_step(&ctx, 0x01); /* accelerate */
			rfaithful_render(&ctx);
			if (f % (selftest_frames / 4 ? selftest_frames / 4 : 1) == 0) {
				unsigned long ink = 0;
				for (int i = 0; i < VIEW_W * VIEW_H; i++)
					if (rfb_pixels[i] != rfb_pixels[0]) ink++;
				snprintf(path, sizeof(path), "%s/selftest_f%03d.bmp", selftest_dir, f);
				FILE* fp = fopen(path, "wb");
				if (fp) {
					uint32_t rowb = VIEW_W * 3, pad = (4 - (rowb % 4)) % 4;
					uint32_t img = (rowb + pad) * VIEW_H, fsz = 54 + img;
					uint8_t h[54] = {0}; int w = VIEW_W, hh = VIEW_H;
					h[0]='B'; h[1]='M'; memcpy(h+2,&fsz,4); h[10]=54; h[14]=40;
					memcpy(h+18,&w,4); memcpy(h+22,&hh,4); h[26]=1; h[28]=24;
					memcpy(h+34,&img,4); fwrite(h,1,54,fp);
					for (int y = VIEW_H - 1; y >= 0; y--) {
						for (int x = 0; x < VIEW_W; x++) {
							stunts_color_rgba_t c = pal.colors[rfb_pixels[y*VIEW_W+x]];
							uint8_t bgr[3] = { c.b, c.g, c.r };
							fwrite(bgr,1,3,fp);
						}
						for (uint32_t p = 0; p < pad; p++) fputc(0, fp);
					}
					fclose(fp);
				}
				printf("frame %3d: pos=(%d,%d,%d) speed=%u  non-bg pixels=%lu -> %s\n",
				       f, ctx.player_state.pos_world.lx, ctx.player_state.pos_world.ly,
				       ctx.player_state.pos_world.lz, ctx.player_state.speed_actual,
				       ink, path);
			}
		}
		return 0;
	}

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	/* 4:3 presentation: 320x200 stretched to 320x240 aspect (1.2x vertical),
	 * then integer-scaled. */
	int win_w = VIEW_W * scale;
	int win_h = VIEW_H * scale * 6 / 5;
	SDL_Window* win = SDL_CreateWindow(
		"Stunts — Native Port (Faithful Renderer)",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		win_w, win_h, SDL_WINDOW_ALLOW_HIGHDPI);
	SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);

	static uint32_t rgba[VIEW_W * VIEW_H];

	bool quit = false;
	uint32_t last_tick_ms = SDL_GetTicks();
	uint32_t sim_accum_ms = 0;
	int16_t camera_mode = 2;

	while (!quit) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) quit = true;
			else if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
					case SDLK_ESCAPE: quit = true; break;
					case SDLK_F1: camera_mode = 0; rfaithful_set_camera_mode(0); break;
					case SDLK_F3: camera_mode = 2; rfaithful_set_camera_mode(2); break;
					default: break;
				}
			}
		}

		const Uint8* keys = SDL_GetKeyboardState(NULL);
		uint8_t input = 0;
		if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) input |= 0x01;
		if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) input |= 0x02;
		if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) input |= 0x04;
		if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) input |= 0x08;
		if (keys[SDL_SCANCODE_SPACE]) input |= 0x20;  /* bit5 = shift up */
		if (keys[SDL_SCANCODE_LSHIFT]) input |= 0x10; /* bit4 = shift down */

		uint32_t now = SDL_GetTicks();
		sim_accum_ms += now - last_tick_ms;
		last_tick_ms = now;
		/* 20 Hz fixed step; cap catch-up to avoid spiral of death */
		if (sim_accum_ms > 250) sim_accum_ms = 250;
		while (sim_accum_ms >= 50) {
			stunts_sim_step(&ctx, input);
			sim_accum_ms -= 50;
		}

		rfaithful_render(&ctx);

		for (int i = 0; i < VIEW_W * VIEW_H; i++) {
			stunts_color_rgba_t c = pal.colors[rfb_pixels[i]];
			rgba[i] = 0xFF000000u | ((uint32_t)c.r << 16) |
			          ((uint32_t)c.g << 8) | c.b;
		}
		SDL_UpdateTexture(tex, NULL, rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(1);
	}

	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	(void)camera_mode;
	return 0;
}
