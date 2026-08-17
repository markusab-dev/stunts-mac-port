/*
 * intro_shot.c - a standalone driver for the Phase 10 intro, so the title
 * stills, the 3D animation and the credits can be built, run and
 * screenshotted without touching src/main_native.c.
 *
 * It reproduces only the part of main_native.c's start-up that
 * run_intro_looped depends on: the data directory, SDL, the palette, the
 * two fonts and the two language archives.
 *
 *   bin/intro_shot --data <dir>
 *   STUNTS_CREDITS_SHOT=/tmp/cred.bmp bin/intro_shot --data <dir>
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_faithful/externs.h"
#include "render_faithful/rfbsize.h"
#include "render_faithful/rintro.h"
#include "render/stunts_palette.h"

extern void rfileio_set_data_dir(const char* dir);
extern void rblit_init(void);
extern void* file_load_resfile(const char* filename);
extern void far* fontdefptr;
extern void far* fontnptr;
extern void far* mainresptr;
extern void far* miscptr;
extern uint32_t* rs_rgba;
extern const uint32_t* rs_pal;

static void far* load_font_raw(const char* dir, const char* name)
{
	char p[600];
	FILE* f;
	long n;
	uint8_t* b;
	snprintf(p, sizeof p, "%s/%s", dir, name);
	f = fopen(p, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = (uint8_t*)malloc((size_t)n);
	if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
	fclose(f);
	return b;
}

static uint32_t pal_rgba[256];

int main(int argc, char** argv)
{
	const char* data_dir = "extracted/stunts/stunts";
	int scale = 4, i, r, loops = 1, pass;
	stunts_palette_t pal;
	SDL_Window* win;
	SDL_Renderer* ren;
	SDL_Texture* tex;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data_dir = argv[++i];
		else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--loops") && i + 1 < argc) loops = atoi(argv[++i]);
	}

	rfileio_set_data_dir(data_dir);
	rblit_init();

	{
		char p[600];
		snprintf(p, sizeof p, "%s/SDMAIN.PVS", data_dir);
		if (!stunts_palette_load(p, &pal)) stunts_palette_init_default(&pal);
	}
	for (i = 0; i < 256; i++)
		pal_rgba[i] = 0xFF000000u | ((uint32_t)pal.colors[i].r << 16)
		            | ((uint32_t)pal.colors[i].g << 8) | pal.colors[i].b;
	rs_pal = pal_rgba;
	rs_rgba = NULL;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	win = SDL_CreateWindow("Stunts intro", SDL_WINDOWPOS_CENTERED,
	                       SDL_WINDOWPOS_CENTERED,
	                       (RFB_VIEW_W / RFB_SCALE) * scale,
	                       (RFB_VIEW_H / RFB_SCALE) * scale * 6 / 5,
	                       SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
	ren = SDL_CreateRenderer(win, -1,
	                         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
	                        SDL_TEXTUREACCESS_STREAMING, RFB_VIEW_W, RFB_VIEW_H);

	fontdefptr = load_font_raw(data_dir, "FONTDEF.FNT");
	if (!fontdefptr) fontdefptr = load_font_raw(data_dir, "FONTN.FNT");
	fontnptr = load_font_raw(data_dir, "FONTN.FNT");
	if (!fontnptr) fontnptr = fontdefptr;
	mainresptr = file_load_resfile("main");
	miscptr    = file_load_resfile("misc");

	for (pass = 0; pass < loops; pass++) {
		r = rintro_run_looped((struct SDL_Window*)win, (struct SDL_Renderer*)ren,
		                      (struct SDL_Texture*)tex, &pal, data_dir);
		printf("varv %d: rintro_run_looped -> %d\n", pass + 1, r);
	}
	SDL_Quit();
	return 0;
}
