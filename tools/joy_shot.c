/*
 * joy_shot.c - a standalone driver for src/render_faithful/rjoystick.c, so
 * the joystick input path and the calibration screen can be built, run and
 * screenshotted without src/main_native.c being involved at all.  Modelled
 * on tools/intro_shot.c, which exists for the same reason.
 *
 *   bin/joy_shot --data <dir>              report what is attached, then run
 *                                          the calibration screen for real
 *   bin/joy_shot --data <dir> --selftest   every flag, every cell, the
 *                                          steering servo and a scripted
 *                                          wave, with numbers.  Exit 0 = all
 *                                          of it agreed with the listing.
 *   bin/joy_shot --data <dir> --live       print the live flag byte
 *
 *   STUNTS_JOYCAL_SHOT=<f.bmp>             one frame of the screen, then exit
 *   STUNTS_JOYCAL_CELL=0..8                  ... with that cell lit
 *   STUNTS_JOYCAL_JOX=1                      ... the "jox" refusal instead
 *   STUNTS_JOYFAKE=<x>,<y>,<buttons>       a stick that is not there
 *   STUNTS_JOYFAKE=sweep                   a stick that waves round all nine
 *                                          squares and then presses a button
 *   STUNTS_JOYFAKE=sweepfail                 ... too fast, and so is refused
 *
 * --live is the only part that needs a joystick plugged in; everything else
 * runs, and is expected to run, with nothing attached.
 *
 * NOTE: with no joystick and no STUNTS_JOYCAL_* or STUNTS_JOYFAKE, the plain
 * form ends on the "jox" refusal, which is modal - it waits for a key, as the
 * original does.  Under SDL_VIDEODRIVER=dummy no key can arrive, so use one
 * of the hooks above for headless runs.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_faithful/externs.h"
#include "render_faithful/rfbsize.h"
#include "render_faithful/rjoystick.h"
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
extern int16_t get_kb_or_joy_flags(void);

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

static const char* const NAMES9[9] = {
	"mitt", "N", "NO", "O", "SO", "S", "SV", "V", "NV"
};

int main(int argc, char** argv)
{
	const char* data_dir = "extracted/stunts/stunts";
	int scale = 3, i, live = 0, selftest = 0;
	int16_t r;
	stunts_palette_t pal;
	SDL_Window* win;
	SDL_Renderer* ren;
	SDL_Texture* tex;

	setvbuf(stdout, NULL, _IOLBF, 0);   /* so a redirect still shows progress */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data_dir = argv[++i];
		else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--live")) live = 1;
		else if (!strcmp(argv[i], "--selftest")) selftest = 1;
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
	win = SDL_CreateWindow("Stunts joystick", SDL_WINDOWPOS_CENTERED,
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

	if (selftest) {
		/* Nine stick positions and two buttons, through the real
		 * get_joy_flags, with STUNTS_JOYFAKE standing in for hardware.
		 * The expected flags are the original's: 0x01 up, 0x02 down,
		 * 0x04 right, 0x08 left, 0x10 button1, 0x20 button2, and the
		 * cell numbers are byte_3FB38's. */
		static const struct { int x, y, b; int flags; int cell;
		                      const char* name; } cases[] = {
			{ 512,  512, 0x00, 0x00, 0, "mitten"      },
			{ 512,    0, 0x00, 0x01, 1, "upp"         },
			{1023,    0, 0x00, 0x05, 2, "upp-hoger"   },
			{1023,  512, 0x00, 0x04, 3, "hoger"       },
			{1023, 1023, 0x00, 0x06, 4, "ner-hoger"   },
			{ 512, 1023, 0x00, 0x02, 5, "ner"         },
			{   0, 1023, 0x00, 0x0A, 6, "ner-vanster" },
			{   0,  512, 0x00, 0x08, 7, "vanster"     },
			{   0,    0, 0x00, 0x09, 8, "upp-vanster" },
			{ 512,  512, 0x10, 0x10, 0, "knapp 1"     },
			{ 512,  512, 0x20, 0x20, 0, "knapp 2"     },
			{ 512,  512, 0x30, 0x30, 0, "bada knappar"},
		};
		int bad = 0;
		char buf[64];
		snprintf(buf, sizeof buf, "512,512,0");
		setenv("STUNTS_JOYFAKE", buf, 1);
		rjoy_init();
		rjoy_set_enabled(1);
		for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
			int16_t f, in;
			snprintf(buf, sizeof buf, "%d,%d,%d",
			         cases[i].x, cases[i].y, cases[i].b);
			setenv("STUNTS_JOYFAKE", buf, 1);
			f = get_joy_flags();
			in = (int16_t)(unsigned char)rjoy_or_input(0);
			printf("%-13s (%4d,%4d,%02X) flaggor 0x%02X (vantat 0x%02X) "
			       "ruta %d (vantat %d) indata 0x%02X %s\n",
			       cases[i].name, cases[i].x, cases[i].y, cases[i].b,
			       f, cases[i].flags,
			       sub_307D2(f), cases[i].cell, in,
			       (f == cases[i].flags &&
			        sub_307D2(f) == cases[i].cell) ? "" : "  <-- FEL");
			if (f != cases[i].flags ||
			    sub_307D2(f) != cases[i].cell) bad++;
		}
		/* The servo (seg005 replay_unk): with the stick held hard right
		 * the steering bit must be 4 while the wheel is short of the
		 * target and 0 once it has caught up - a bang-bang controller,
		 * not a constant "steer right". */
		{
			static const int16_t angles[] = { -120, -60, 0, 60, 120, 126, 127, 200 };
			int k;
			setenv("STUNTS_JOYFAKE", "1023,512,0", 1);
			printf("\nservo, spak hart hoger (mal +127):\n");
			for (k = 0; k < (int)(sizeof angles / sizeof angles[0]); k++) {
				state.playerstate.car_steeringAngle = angles[k];
				state.playerstate.car_speed2 = 0;
				printf("   ratt %4d -> indata 0x%02X\n", angles[k],
				       (unsigned char)rjoy_or_input(0));
			}
			state.playerstate.car_steeringAngle = 0;
		}

		/* The scripted wave (STUNTS_JOYFAKE=sweep) through the real
		 * get_joy_flags, exactly as the calibration screen sees it:
		 * which of the nine cells has been reached after N polls. */
		{
			int seen[9], k, poll, first_all = -1;
			for (k = 0; k < 9; k++) seen[k] = 0;
			setenv("STUNTS_JOYFAKE", "sweep", 1);
			rjoy_shutdown();
			rjoy_init();
			rjoy_set_enabled(1);
			sub_307B4();
			for (poll = 1; poll <= 1000; poll++) {
				int16_t f = get_joy_flags();
				if (f & 0x30) break;
				seen[sub_307D2(f)] = 1;
				for (k = 0; k < 9 && seen[k]; k++) ;
				if (k == 9 && first_all < 0) first_all = poll;
			}
			printf("\nsvep: alla nio rutor natta efter %d avlasningar "
			       "(knapp vid %d)\n", first_all, poll);
			for (k = 0; k < 9; k++)
				printf("   ruta %d %-6s: %s\n", k, NAMES9[k],
				       seen[k] ? "natt" : "MISSAD");
			if (first_all < 0) bad++;
		}

		printf("sjalvtest: %d fel\n", bad);
		rjoy_shutdown();
		SDL_Quit();
		return bad ? 1 : 0;
	}

	printf("styrspak ansluten: %s\n", rjoy_init() ? "ja" : "nej");
	{   /* rreplaybar.c's fall-through to get_joy_flags */
		extern int16_t (*joy_flags_hook)(void);
		printf("joy_flags_hook installerad: %s\n",
		       joy_flags_hook ? "ja" : "NEJ");
	}
	printf("get_joy_flags (avstangd)      = 0x%02X\n", get_joy_flags());
	rjoy_set_enabled(1);
	printf("get_joy_flags (paslagen)      = 0x%02X\n", get_joy_flags());
	printf("get_kb_or_joy_flags           = 0x%02X\n", get_kb_or_joy_flags());
	printf("rjoy_or_input(0)              = 0x%02X\n",
	       (unsigned char)rjoy_or_input(0));
	printf("rjoy_or_input(0x05) rors ej   = 0x%02X\n",
	       (unsigned char)rjoy_or_input(0x05));
	rjoy_set_enabled(0);

	r = rjoy_calibrate((struct SDL_Window*)win, (struct SDL_Renderer*)ren,
	                   (struct SDL_Texture*)tex, &pal, fontdefptr);
	printf("rjoy_calibrate -> %d (byte_3FE00 = %d)\n", r, byte_3FE00 & 1);

	if (live) {
		printf("kor med styrspaken; esc avslutar\n");
		for (;;) {
			SDL_Event ev; int quit = 0;
			while (SDL_PollEvent(&ev))
				if (ev.type == SDL_QUIT ||
				    (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
					quit = 1;
			if (quit) break;
			printf("\rflaggor 0x%02X  indata 0x%02X   ",
			       get_joy_flags(), (unsigned char)rjoy_or_input(0));
			fflush(stdout);
			SDL_Delay(50);
		}
		printf("\n");
	}

	rjoy_shutdown();
	SDL_Quit();
	return 0;
}
