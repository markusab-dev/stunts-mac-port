/*
 * replaybar_shot - render the in-race recording bar once and write a BMP.
 *
 * The strip is Phase 9's deliverable and it has to be *looked at*, not
 * merely reported as "a file appeared".  This is the smallest harness that
 * can draw it: it does what main_native.c does before a race - data
 * directory, palette, the two fonts - and then hands over to
 * replaybar_shot() in src/render_faithful/rreplaybar.c, which is the same
 * entry point the host will call for STUNTS_REPLAYBAR_SHOT.
 *
 *   bash tools/build_replaybar_shot.sh
 *   STUNTS_REPLAYBAR_SHOT=/tmp/bar.bmp ./bin/replaybar_shot --data <dir>
 *
 * Nothing here belongs to the port proper; it is a viewer.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "../src/render/stunts_palette.h"
#include "../src/render_faithful/rfbsize.h"
#include "../src/render_faithful/rreplaybar.h"

extern void rfileio_set_data_dir(const char* dir);
extern uint8_t rfb_pixels[];
extern const uint32_t* rs_pal;
extern uint32_t* rs_rgba;
extern void far* fontledresptr;
extern void far* fontdefptr;

/* The pieces --smoke drives.  loop_game itself comes from externs.h. */
extern void loop_game(int16_t, int16_t, int16_t);
extern char cameramode, dashb_toggle, followOpponentFlag, is_in_replay;
extern uint8_t game_replay_mode;
extern char replaybar_enabled;
extern int16_t custom_camera_distance;
extern int16_t custom_camera_azimuth_angle;
extern int16_t custom_camera_elevation_angle;
extern int16_t word_44D20;
extern void sprite_set_1_size(uint16_t, uint16_t, uint16_t, uint16_t);

static uint32_t s_pal_rgba[256];
static int s_fails;

static void* load_font_raw(const char* dir, const char* name)
{
	char p[640];
	FILE* f;
	long n;
	uint8_t* b;
	snprintf(p, sizeof p, "%s/%s", dir, name);
	f = fopen(p, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = (uint8_t*)malloc((size_t)n);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
	fclose(f);
	return b;
}

/* ------------------------------------------------------------------ */
/* --smoke: drive the strip and measure what moved.                    */
/*                                                                     */
/* Not a screenshot check.  Each case reads a number out of the port    */
/* after loop_game has acted on a keypress, or counts pixels on the     */
/* rectangle the focus ring is supposed to be drawn on.                 */
/* ------------------------------------------------------------------ */
static void chk(const char* what, long got, long want)
{
	if (got == want) printf("  ok    %-46s %ld\n", what, got);
	else { printf("  FEL   %-46s %ld (vantat %ld)\n", what, got, want);
	       s_fails++; }
}

/* input_checking reads SDL's event queue, so a pushed key event is exactly
 * what a real one looks like to it. */
static void press(SDL_Keycode k)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.type = SDL_KEYDOWN;
	e.key.state = SDL_PRESSED;
	e.key.keysym.sym = k;
	e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
	SDL_PushEvent(&e);
}

static void step(SDL_Keycode k)
{
	press(k);
	loop_game(3, 0, 0);
}

/* sprite_1_unk4(x1,y1,x2,y2,c) draws the top and bottom rows x1..x2 and the
 * left and right columns y1..y2-1 - the asymmetry is the original's, see
 * rwidgets.c.  Count how much of that perimeter carries colour 4. */
static int ring_perimeter_hits(int x1, int y1, int x2, int y2, int* total)
{
	int n = 0, t = 0, x, y;
	for (x = x1; x <= x2; x++) {
		t += 2;
		if (rfb_pixels[y1 * RFB_VIEW_W + x] == 4) n++;
		if (rfb_pixels[y2 * RFB_VIEW_W + x] == 4) n++;
	}
	for (y = y1; y < y2; y++) {
		t += 2;
		if (rfb_pixels[y * RFB_VIEW_W + x1] == 4) n++;
		if (rfb_pixels[y * RFB_VIEW_W + x2] == 4) n++;
	}
	*total = t;
	return n;
}

extern int16_t input_checking(int16_t);

static void smoke(void)
{
	int t, n, y, x, ink = 0;

	/* STUNTS_REPLAYBAR_EVTEST=1 prints the raw code input_checking returns
	 * for four presses in a row.  It is here because it found a real bug:
	 * with an early `break` out of the SDL poll loop the second press came
	 * back as 0 and the third as the second's code, one call late for ever
	 * after - SDL's poll sentinel.  Expected output is 0x2B 0x2D 0x4B00
	 * 0x2D, i.e. no lag at all. */
	if (getenv("STUNTS_REPLAYBAR_EVTEST")) {
		press(SDLK_PLUS);  printf("evtest '+' -> 0x%X\n", input_checking(0));
		press(SDLK_MINUS); printf("evtest '-' -> 0x%X\n", input_checking(0));
		press(SDLK_LEFT);  printf("evtest LEFT -> 0x%X\n", input_checking(0));
		press(SDLK_MINUS); printf("evtest '-' -> 0x%X\n", input_checking(0));
	}

	dashb_toggle = 1; followOpponentFlag = 0;
	game_replay_mode = 2; replaybar_enabled = 1;
	is_in_replay = 0;            /* so loop_game(3) returns after one act */
	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	loop_game(0, 0, 0);
	loop_game(2, 4, 0);

	printf("\nzoom, kameralage 0..2 (custom_camera_distance)\n");
	cameramode = 0;
	custom_camera_distance = 210;
	step(SDLK_PLUS);   chk("'+' drar in 0x1E", custom_camera_distance, 180);
	step(SDLK_MINUS);  chk("'-' drar ut 0x1E", custom_camera_distance, 210);
	/* The stop is `if (distance <= 0x78) do nothing`, so the last step that
	 * is allowed lands on 0x78 + 1 - 0x1E = 0x5B and the next one is
	 * refused - the floor the player can reach is 0x5B, not 0x78. */
	custom_camera_distance = 0x78 + 1;
	step(SDLK_PLUS);   chk("'+' tar sista steget", custom_camera_distance, 0x5B);
	step(SDLK_PLUS);   chk("'+' vagrar under 0x78", custom_camera_distance, 0x5B);
	custom_camera_distance = 0x5DC;
	step(SDLK_MINUS);  chk("'-' klamps vid 0x5DC", custom_camera_distance, 0x5DC);

	printf("\nzoom, kameralage 3 (word_44D20)\n");
	cameramode = 3;
	word_44D20 = 300;
	step(SDLK_PLUS);   chk("'+' okar 0x1E", word_44D20, 330);
	step(SDLK_MINUS);  chk("'-' minskar 0x1E", word_44D20, 300);
	word_44D20 = 0x384;
	step(SDLK_PLUS);   chk("'+' klamps vid 0x384", word_44D20, 0x384);
	word_44D20 = 0;
	step(SDLK_MINUS);  chk("'-' klamps vid 0", word_44D20, 0);

	/* The focus ring, read off the framebuffer.  Rectangles are dseg
	 * 14180..14215; the walk is the table at dseg 14135..14169. */
	printf("\nfokusringen gar dit tabellerna sager\n");
	cameramode = 2;                       /* all nine controls reachable */
	loop_game(1, 400, 400);
	step(SDLK_LEFT);   /* 6 -> 7, the zoom rocker */
	loop_game(1, 400, 400);
	n = ring_perimeter_hits(66, 156, 91, 193, &t);
	chk("vanster: 6 -> 7 (zoom, 66..91 x 156..193)", n, t);
	step(SDLK_LEFT);   /* 7 -> 8, the pan pad */
	loop_game(1, 400, 400);
	n = ring_perimeter_hits(10, 156, 47, 193, &t);
	chk("vanster: 7 -> 8 (pann, 10..47 x 156..193)", n, t);
	step(SDLK_RIGHT);  /* 8 -> 7 */
	step(SDLK_RIGHT);  /* 7 -> 1 */
	loop_game(1, 400, 400);
	n = ring_perimeter_hits(109, 176, 151, 193, &t);
	chk("hoger x2: 8 -> 7 -> 1 (109..151 x 176..193)", n, t);
	step(SDLK_UP);     /* 1 -> 6 */
	loop_game(1, 400, 400);
	n = ring_perimeter_hits(108, 156, 151, 173, &t);
	chk("upp: 1 -> 6 (108..151 x 156..173)", n, t);
	step(SDLK_DOWN);   /* 6 -> 1 */
	loop_game(1, 400, 400);
	n = ring_perimeter_hits(109, 176, 151, 193, &t);
	chk("ned: 6 -> 1 (109..151 x 176..193)", n, t);

	/* The two pointer-driven controls, seg005 loc_24056..loc_240D8.  Both
	 * need the pointer inside the control *and* Space or Return, so this
	 * needs a window for SDL_WarpMouseInWindow to aim at.
	 *
	 * The zoom rocker is the one that can be measured from outside: which
	 * half was clicked becomes Up or Down (midpoint (156+193)>>1 = 174),
	 * and Up/Down on control 7 are the two special cases that turn into
	 * '+' and '-' - so the click lands in custom_camera_distance, 0x1E at
	 * a time, and nothing else on the strip can put it there.
	 *
	 * The pan pad's four quadrants cannot be measured the same way: with
	 * no button held the arrow it produces only walks the focus, and the
	 * pointer is still over the pad on the next pass, which pulls the
	 * focus straight back.  That is the original's behaviour (the pointer
	 * owns the focus, loc_2402E), so all this can show is that the pad is
	 * hit-tested and the quadrant path runs without wandering. */
	{
		SDL_Window* w = SDL_CreateWindow("rb", 0, 0, 320, 200,
		                                 SDL_WINDOW_HIDDEN);
		if (!w) {
			printf("\n  --    pekdon: inget fonster, hoppar over\n");
		} else {
			printf("\nzoomvippan och panelen under pekaren\n");
			cameramode = 2;
			custom_camera_distance = 300;
			SDL_WarpMouseInWindow(w, 78, 165);      /* upper half */
			step(SDLK_SPACE);
			chk("klick pa vippans ovre halva = '+'",
			    custom_camera_distance, 270);
			SDL_WarpMouseInWindow(w, 78, 185);      /* lower half */
			step(SDLK_SPACE);
			chk("klick pa vippans nedre halva = '-'",
			    custom_camera_distance, 300);
			SDL_WarpMouseInWindow(w, 28, 160);      /* the pan pad */
			step(SDLK_SPACE);
			loop_game(1, 400, 400);
			n = ring_perimeter_hits(10, 156, 47, 193, &t);
			chk("panelen tar fokus (10..47 x 156..193)", n, t);
			chk("panelen rorde inte zoomen", custom_camera_distance, 300);
			SDL_DestroyWindow(w);
		}
	}

	/* And that the strip is actually painted, not a valid black rectangle. */
	for (y = 151; y < 200; y++)
		for (x = 0; x < 320; x++)
			if (rfb_pixels[y * RFB_VIEW_W + x] != 0) ink++;
	printf("\n  %d malade pixlar av %d i remsan\n", ink, 49 * 320);
	if (ink < 49 * 320 / 2) { printf("  FEL   remsan ar mest svart\n"); s_fails++; }
}

int main(int argc, char** argv)
{
	const char* data = getenv("STUNTS_DATA");
	const char* out  = getenv("STUNTS_REPLAYBAR_SHOT");
	stunts_palette_t pal;
	char pp[640];
	int i, do_smoke = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
		else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
		else if (!strcmp(argv[i], "--smoke")) do_smoke = 1;
	}
	if (!data) data = "extracted/stunts/stunts";
	if (!out && !do_smoke) {
		fprintf(stderr, "satt STUNTS_REPLAYBAR_SHOT eller --out\n");
		return 2;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	rfileio_set_data_dir(data);

	snprintf(pp, sizeof pp, "%s/SDMAIN.PVS", data);
	if (!stunts_palette_load(pp, &pal)) stunts_palette_init_default(&pal);
	for (i = 0; i < 256; i++)
		s_pal_rgba[i] = 0xFF000000u | ((uint32_t)pal.colors[i].r << 16)
		              | ((uint32_t)pal.colors[i].g << 8) | pal.colors[i].b;
	rs_pal = s_pal_rgba;
	rs_rgba = NULL;              /* draw into the indexed buffer */

	fontledresptr = load_font_raw(data, "FONTLED.FNT");
	fontdefptr    = load_font_raw(data, "FONTDEF.FNT");
	if (!fontdefptr) fontdefptr = fontledresptr;
	if (!fontledresptr) fprintf(stderr, "varning: FONTLED.FNT saknas\n");

	/* Rows 0..150 are the 3D view the strip shrinks away; paint them so the
	 * split at height_above_replaybar = 151 is visible in the picture and
	 * an all-black frame cannot pass for a drawn one. */
	memset(rfb_pixels, 0, (size_t)RFB_VIEW_W * RFB_VIEW_H);
	{
		int y, x;
		for (y = 0; y < 151 * RFB_SCALE; y++)
			for (x = 0; x < RFB_VIEW_W; x++)
				rfb_pixels[y * RFB_VIEW_W + x] = (uint8_t)(16 + (y / RFB_SCALE) / 20);
	}

	if (do_smoke) {
		smoke();
		if (s_fails == 0) printf("\nallt gick igenom\n");
		else              printf("\n%d fel\n", s_fails);
		SDL_Quit();
		return s_fails ? 1 : 0;
	}

	if (!replaybar_shot(out)) { SDL_Quit(); return 1; }
	SDL_Quit();
	return 0;
}
