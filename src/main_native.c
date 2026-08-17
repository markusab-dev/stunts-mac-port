/*
 * main_native.c - Playable shell driving the vendored original simulation
 * and the vendored original renderer.
 *
 * Both halves are the real Stunts code operating on the original globals, so
 * there is no translation layer between them: player_op() writes
 * `struct GAMESTATE state`, update_frame() reads it. (The rbridge.c state
 * copy that the old hand-written sim needed is not linked here.)
 *
 *   20 Hz fixed simulation tick, 60 fps presentation, 320x200 framebuffer
 *   scaled with the original 1.2 pixel aspect.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <mach-o/dyld.h>   /* _NSGetExecutablePath, for the .app bundle path */

#include "render_faithful/externs.h"
#include "asset/stunts_asset_loader.h"
#include "render/stunts_palette.h"
#include "music_native.h"
#include "render_faithful/rfbsize.h"
#include "render_faithful/rintro.h"
/* Phase 11. Four screens that arrived as separate ports and are driven from
 * here: the in-race recording strip, the track editor and its icon palette,
 * and the joystick. rdialog.c (seg008 show_dialog) drives the replay bar's
 * five dialogs, below; the editor's are raised through reditor_activate(),
 * and that loop is not built. */
#include "render_faithful/rreplaybar.h"
#include "render_faithful/rdialog.h"
#include "render_faithful/reditor.h"
#include "render_faithful/reditoricons.h"
#include "render_faithful/rjoystick.h"
#include "audio_native.h"

/* --- simulation --- */
extern void player_op(char input);
/* src/sim_faithful/sfopponent.c - Phase 2 */
extern void load_opponent_data(void);
extern void opponent_op(void);
extern char oppnentSped[16];
extern char g_opponent_initials[16];
extern int16_t sfopp_path_len;
extern int32_t sfopp_path_cost;
extern int16_t sub_18D60(int16_t, struct VECTOR*, int16_t, int16_t*);
extern char far* trackdata3;
/* --opponent <1..6>, --opponent-car <id>. 0 = no opponent, which is what
 * every physics baseline in tests/ was recorded with. */
static int opt_opponent = 0;
static char opt_opponent_car[8] = "COUN";
/* The attract sequence - two stills, the 3D logo animation, the credits -
 * runs 38 seconds and is skippable with any key. The original showed it on
 * every launch and returned to it whenever the menu sat idle; here it is
 * off unless asked for, because 38 seconds before the menu on every single
 * launch is not what anyone wants from a port they are playing.
 *   --intro     show it
 *   --nointro   never show it (kept so scripts that pass it still work) */
static int opt_nointro = 0;
static int opt_intro   = 0;
/* seg000 loc_12E6A: gameconfig.game_opponentcarid == 0xFF means "not chosen
 * yet", and Done then copies the player's car into it. */
static int opt_opponent_car_chosen = 0;
static char g_player_car[8] = "COUN";
extern void sfdata_init_track_tables(void);
extern int16_t sfstub_hits[8];
extern int16_t dbg_model, dbg_col, dbg_row;   /* diagnostics from build_track_object */
extern int16_t terrainHeight, planindex;
extern uint16_t elapsed_time1, elapsed_time2;
extern int16_t hillHeightConsts[];
extern int16_t multiply_and_scale(int16_t, int16_t);
extern char startcol2, startrow2, hillFlag;
extern int16_t track_angle;

/* --- renderer --- */
extern void init_polyinfo(void);
extern int16_t shape3d_load_all(void);
extern void shape3d_load_car_shapes(char* playercarid, char* opponentcarid);
extern void set_projection(int16_t, int16_t, int16_t, int16_t);
extern void setup_car_shapes(int16_t mode);
extern void shape2d_op_unk3(void far* shape);
extern void far* unflip_shape(void far* shape);
extern void* file_load_resource(int16_t restype, const char* filename);
extern char far* locate_shape_nofatal(char far* data, char* name);
extern void* file_load_resfile(const char* filename);
extern void locate_many_resources(char far* data, char* names, char far** result);
extern void unload_resource(void far* resptr);
extern struct RECTANGLE* intro_draw_text(char* str, int16_t x, int16_t y,
                                         int16_t colour_text, int16_t colour_shadow);
extern void font_set_fontdef2(void far* data);
extern void font_set_colour(uint16_t colour, uint16_t unused);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern void sprite_set_1_size(uint16_t left, uint16_t right, uint16_t top, uint16_t height);
extern int16_t roofbmpheight;   /* height of the cockpit roof strip, 0 if none */
extern int16_t dashbmp_y;       /* first screen row covered by the dash panel  */
extern char cameramode, followOpponentFlag;

extern void update_frame(char arg_0, struct RECTANGLE* cliprect);
extern void rblit_init(void);
extern void rfileio_set_data_dir(const char* dir);
extern const char* rfileio_get_data_dir(void);
extern uint8_t rfb_pixels[];
extern void copy_material_list_pointers(int16_t*, int16_t*, int16_t*, int16_t*, int16_t);
extern int16_t *material_clrlist_ptr, *material_clrlist2_ptr;
extern int16_t *material_patlist_ptr, *material_patlist2_ptr;
extern int16_t skybox_sky_color, skybox_grd_color;
extern void load_skybox(char sel);
extern void load_sdgame2_shapes(void);
extern int16_t video_flag1_is1, video_flag2_is1, video_flag3_isFFFF, video_flag5_is0;
extern int16_t meter_needle_color, dialog_fnt_colour;
extern int rshape2d_smooth;   /* Scale2x on the cockpit art */
extern uint32_t* rs_rgba;     /* truecolour cockpit target, NULL = indexed */
extern const uint32_t* rs_pal;
extern const char *rshape2d_export_dir, *rshape2d_import_dir;

/* --- shared globals --- */
extern struct GAMESTATE state;
extern struct GAMEINFO gameconfig;
extern struct SIMD simd_player;
extern uint8_t far *td14_elem_map_main, *td15_terr_map_main;
extern int16_t trackrows[], terrainrows[], trackpos[], trackpos2[];
extern int16_t trackcenterpos[], trackcenterpos2[];
extern struct PLANE far *planptr, *current_planptr;
extern int16_t planindex;
extern uint16_t framespersec;
extern uint8_t far *trackdata19, *trackdata23;
extern int16_t far *td10_track_check_rel, *td08_direction_related, *trackdata9;
extern uint8_t detail_level, game_replay_mode;
extern int16_t run_game_random;

#define VIEW_W RFB_VIEW_W
#define VIEW_H RFB_VIEW_H

/* dashb_toggle is the original's own global. The cockpit only makes sense
 * from inside the car, so like the original it is suppressed in the external
 * camera modes. */
static int dash_visible(void)
{
	return dashb_toggle && dashbmp_y && cameramode == 0 && !followOpponentFlag;
}
static int16_t view_top(void)    { return dash_visible() ? roofbmpheight : 0; }
/* seg005:498..520 is the original's own sizing rule, and replaybar_view_height()
 * is that routine: while the recording strip is up it sets
 * height_above_replaybar to 0x97 and the strip owns rows 0x97..0xC7, so the 3D
 * view must stop there whatever the cockpit would otherwise ask for. Off the
 * replay screen it answers 0xC8 and this is exactly what it was before. */
static int16_t view_bottom(void)
{
	int16_t bottom = dash_visible() ? dashbmp_y : 200;
	int16_t bar    = replaybar_view_height();
	/* 0xC8 is that routine's "no strip" answer, and only a raised strip
	 * may shorten the view - testing it explicitly rather than relying on
	 * 0xC8 being an upper bound keeps this a provable no-op off the replay
	 * screen, including rshape2d.c:828's dashbmp_y = RFB_VIEW_H fallback
	 * when a car has no cockpit artwork at all. */
	if (bar < 0xC8 && bar < bottom) bottom = bar;
	return bottom;
}

/* restunts.c:1029 only redoes this when one of the inputs actually changed;
 * set_projection rebuilds tables the whole 3D pipeline reads. */
static void apply_view(struct RECTANGLE* windshield)
{
	static int16_t last_top = -1, last_bottom = -1;
	int16_t top = view_top(), bottom = view_bottom();
	if (top == last_top && bottom == last_bottom) return;
	last_top = top; last_bottom = bottom;
	/* restunts.c:1031-1033 */
	set_projection(0x23, bottom / 6, VIEW_W, bottom * RFB_SCALE);
	windshield->top    = top * RFB_SCALE;
	windshield->bottom = bottom * RFB_SCALE;
}

/* Draw the cockpit over the finished 3D view: roof strip at the top, dash,
 * gearbox and steering wheel at the bottom. restunts.c widens the sprite1
 * window to the whole screen for this; the pieces carry their own screen
 * positions and between them cover every row the windshield does not. */
/* The presented frame. The 3D view is rendered palette-indexed exactly as the
 * original does and converted here; the cockpit is then drawn over it in
 * truecolour, which is what lets its enlarged artwork use colours the game's
 * 256-entry palette does not contain. */
static uint32_t frame_rgba[RFB_VIEW_W * RFB_VIEW_H];
static uint32_t pal_rgba[256];

static void draw_cockpit(void)
{
	if (!dash_visible()) return;
	sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
	setup_car_shapes(1);   /* roof, dash, gearbox */
	setup_car_shapes(2);   /* wheel, needles, gear gate */
}

static void present(const stunts_palette_t* pal)
{
	int i;
	for (i = 0; i < VIEW_W * VIEW_H; i++) {
		stunts_color_rgba_t c = pal->colors[rfb_pixels[i]];
		frame_rgba[i] = 0xFF000000u | ((uint32_t)c.r << 16)
		              | ((uint32_t)c.g << 8) | c.b;
	}
	rs_rgba = frame_rgba;      /* the cockpit composites in truecolour */
	draw_cockpit();
	rs_rgba = NULL;
}

/* Phase 11. loop_game(3, ...) spins inside itself for as long as the strip is
 * paused or a scrub button is held. The original draws straight into the page
 * the CRT is scanning and so has nothing to push; this port has to put the
 * frame on the screen from in there, or the window is a frozen rectangle for
 * the whole of a pause. rreplaybar.h calls this replaybar_hook_present.
 *
 * It only uploads. It must NOT re-run present(): the strip is composited into
 * frame_rgba over the cockpit, and present() rebuilds frame_rgba from the
 * indexed framebuffer, which would wipe the strip the caller is in the middle
 * of drawing. While the strip spins the world is stopped, so the 3D half of
 * frame_rgba is already the right picture. */
static SDL_Renderer* rb_present_ren;
static SDL_Texture*  rb_present_tex;

static void rb_key_pump(void);

static void replaybar_present(void)
{
	if (!rb_present_ren || !rb_present_tex) return;
	/* The scripted-key pump gets its turn here as well as in the race loop.
	 * loop_game(3, ...) spins inside itself for as long as the strip is
	 * paused - which is what the pause menu leaves it - and the race loop
	 * never comes round again, so a script that has to press Play to carry
	 * on would otherwise wait for a key that nothing can push. Does nothing
	 * unless STUNTS_REPLAYBAR_KEYS is set. */
	rb_key_pump();
	SDL_UpdateTexture(rb_present_tex, NULL, frame_rgba, VIEW_W * 4);
	SDL_RenderClear(rb_present_ren);
	SDL_RenderCopy(rb_present_ren, rb_present_tex, NULL, NULL);
	SDL_RenderPresent(rb_present_ren);
}
/* One 24-bit BMP of the current presented frame, bottom-up as the format
 * wants. Built in memory and written in a single call - the earlier
 * byte-at-a-time version produced short files. */
static void write_bmp(const char* path, const stunts_palette_t* pal)
{
	uint32_t rowb = (uint32_t)VIEW_W * 3;
	uint32_t pad = (4 - (rowb % 4)) % 4;
	uint32_t stride = rowb + pad;
	uint32_t img = stride * (uint32_t)VIEW_H;
	uint8_t hdr[54] = {0};
	int32_t w = VIEW_W, h = VIEW_H;
	uint32_t fsz = 54 + img;
	uint8_t* buf;
	FILE* f;
	int y, x;

	(void)pal;
	buf = (uint8_t*)calloc(1, img);
	if (!buf) return;
	for (y = 0; y < VIEW_H; y++) {
		uint8_t* row = buf + (uint32_t)(VIEW_H - 1 - y) * stride;
		for (x = 0; x < VIEW_W; x++) {
			uint32_t p = frame_rgba[(int32_t)y * VIEW_W + x];
			row[x * 3 + 0] = (uint8_t)p;
			row[x * 3 + 1] = (uint8_t)(p >> 8);
			row[x * 3 + 2] = (uint8_t)(p >> 16);
		}
	}
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(hdr + 2, &fsz, 4); hdr[10] = 54; hdr[14] = 40;
	memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
	hdr[26] = 1; hdr[28] = 24; memcpy(hdr + 34, &img, 4);
	f = fopen(path, "wb");
	if (f) {
		fwrite(hdr, 1, 54, f);
		fwrite(buf, 1, img, f);
		fclose(f);
	}
	free(buf);
}

/* An index the palette maps to a colour nothing in a scene uses. */
#define PAINT_MARKER 0xFE
static long paint_bad;

static uint8_t s_elem[0x385], s_terr[0x385];
static int16_t s_aero[0x40];
static int16_t s_aero_opp[0x40];

/* init_carstate_from_simd (restunts.c), transcribed. */
static void init_carstate(struct CARSTATE* p, struct SIMD* simd,
                          char transmission, int32_t posX, int32_t posY,
                          int32_t posZ, int16_t track_angle)
{
	struct VECTOR whlPos;
	p->car_posWorld1.lx = posX; p->car_posWorld2.lx = posX;
	p->car_posWorld1.ly = posY + 512; p->car_posWorld2.ly = posY;
	p->car_posWorld1.lz = posZ; p->car_posWorld2.lz = posZ;
	p->car_rotate.x = track_angle; p->car_rotate.y = 0; p->car_rotate.z = 0;
	p->car_36MwhlAngle = 0; p->car_pseudoGravity = 0; p->car_steeringAngle = 0;
	p->car_is_braking = 0; p->car_is_accelerating = 0;
	p->car_currpm = simd->idle_rpm;
	p->car_lastrpm = p->car_currpm;
	p->car_idlerpm2 = p->car_currpm;
	p->car_current_gear = 1;
	p->car_speeddiff = 0; p->car_speed = 0; p->car_speed2 = 0; p->car_lastspeed = 0;
	p->car_gearratio = simd->gear_ratios[1];
	p->car_gearratioshr8 = p->car_gearratio >> 8;
	p->car_knob_x = simd->knob_points[1].px; p->car_knob_x2 = p->car_knob_x;
	p->car_knob_y = simd->knob_points[1].py; p->car_knob_y2 = p->car_knob_y;
	p->car_angle_z = 0; p->car_40MfrontWhlAngle = 0;
	p->field_42 = 0; p->field_48 = 0; p->car_trackdata3_index = 0;
	p->car_sumSurfFrontWheels = 2; p->car_sumSurfRearWheels = 2;
	p->car_sumSurfAllWheels = 4;
	p->car_demandedGrip = 0; p->car_surfacegrip_sum = 1000;
	whlPos.x = (int16_t)(posX / 64);
	whlPos.y = (int16_t)(posY / 64);
	whlPos.z = (int16_t)(posZ / 64);
	for (int i = 0; i < 4; ++i) {
		p->car_surfaceWhl[i] = 1;
		p->car_rc1[i] = p->car_rc2[i] = p->car_rc3[i] = 0;
		p->car_rc4[i] = p->car_rc5[i] = 0;
		p->car_whlWorldCrds1[i] = whlPos;
		p->car_whlWorldCrds2[i] = whlPos;
	}
	p->car_engineLimiterTimer = 0; p->car_slidingFlag = 0; p->field_C8 = 0;
	p->car_crashBmpFlag = 0; p->car_changing_gear = 0; p->car_fpsmul2 = 0;
	p->car_transmission = transmission;
	p->field_CD = 0; p->field_CE = 0; p->field_CF = 1;
}




/* ------------------------------------------------------------------ */
/* The car picker (seg000 run_car_menu)                                 */
/*                                                                      */
/* Data again. Every car ships its own CAR<id>.RES holding, besides the  */
/* SIMD physics block the simulation already reads:                      */
/*                                                                      */
/*   gnam  "Lamborghini Countach"          the full name                 */
/*   gsna  "Coun|Lamborghini Countach"     short name, then full         */
/*   edes  "25th Aniversary]Lamborghini Countach]]Mid Engine, RW Drive]" */
/*                                                                      */
/* and SDCSEL.PVS holds "stop", a 320x103 showroom - red curtains and a  */
/* turntable - which is the backdrop the original spins the car on.      */
/*                                                                      */
/* [DEVIATION] run_car_menu is 1300 lines of seg000 and renders the car's */
/* 3D model rotating on that turntable, with paint and transmission       */
/* toggles. Not ported. The backdrop, the names and the descriptions are  */
/* the game's own; the list and the interaction are not.                  */
/* ------------------------------------------------------------------ */
#define CAR_MAX 24

static void menu_point(SDL_Window* win, int mx, int my, int16_t* ox, int16_t* oy);
static void menu_fill(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t idx);

static bool run_car_dialog(SDL_Window* win, SDL_Renderer* ren,
                           SDL_Texture* tex, const stunts_palette_t* pal,
                           const char* data_dir, void far* dlgfont,
                           char* out_car, size_t out_sz)
{
	char ids[CAR_MAX][8];
	char names[CAR_MAX][40];
	int count = 0, sel = 0;
	bool done = false, ok = false;
	void far* csel;
	void far* stage;
	DIR* d;
	struct dirent* e;

	d = opendir(data_dir);
	while (d && (e = readdir(d)) && count < CAR_MAX) {
		size_t l = strlen(e->d_name);
		if (l == 11 && !strncasecmp(e->d_name, "CAR", 3) &&
		    !strcasecmp(e->d_name + 7, ".RES")) {
			char base[16];
			void far* res;
			char* nm;
			int k;
			for (k = 0; k < 4; k++)
				ids[count][k] = (char)tolower((unsigned char)e->d_name[3 + k]);
			ids[count][4] = 0;
			snprintf(base, sizeof(base), "car%s", ids[count]);
			res = file_load_resfile(base);
			nm = res ? (char*)locate_shape_nofatal((char far*)res, "gnam") : NULL;
			snprintf(names[count], sizeof(names[count]), "%s",
			         nm && *nm ? nm : ids[count]);
			count++;
		}
	}
	if (d) closedir(d);
	if (count == 0) return false;

	csel = file_load_resource(3, "sdcsel.pvs");
	stage = csel ? locate_shape_nofatal((char far*)csel, "stop") : NULL;
	unflip_shape(stage);

	rs_rgba = frame_rgba;
	font_set_fontdef2(dlgfont);

	while (!done) {
		SDL_Event ev;
		int i;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) done = true;
			else if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
				case SDLK_ESCAPE: done = true; break;
				case SDLK_LEFT: case SDLK_UP:
					if (--sel < 0) sel = count - 1; break;
				case SDLK_RIGHT: case SDLK_DOWN:
					if (++sel >= count) sel = 0; break;
				case SDLK_RETURN: ok = true; done = true; break;
				default: break;
				}
			} else if (ev.type == SDL_MOUSEWHEEL) {
				sel -= ev.wheel.y;
				while (sel < 0) sel += count;
				sel %= count;
			} else if (ev.type == SDL_MOUSEBUTTONDOWN &&
			           ev.button.button == SDL_BUTTON_LEFT) {
				int16_t mx, my;
				menu_point(win, ev.button.x, ev.button.y, &mx, &my);
				if (my >= 118 && my < 118 + 8 * 9) {
					int row = (my - 118) / 9;
					if (row < count) {
						if (sel == row) { ok = true; done = true; }
						sel = row;
					}
				} else if (my < 110) {          /* click the stage to accept */
					ok = true; done = true;
				}
			}
		}

		menu_fill(0, 0, 320, 200, 0);
		if (stage) { sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
		             shape2d_op_unk3(stage); }

		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text(names[sel], 8, 106);

		for (i = 0; i < count && i < 8; i++) {
			int16_t y = (int16_t)(118 + i * 9);
			int idx = i;
			if (count > 8) idx = (sel / 8) * 8 + i;   /* page of eight */
			if (idx >= count) break;
			if (idx == sel) {
				menu_fill(4, y, 150, (int16_t)(y + 8), 8);
				font_set_colour(0, 0);
			} else {
				font_set_colour((uint16_t)dialog_fnt_colour, 0);
			}
			font_draw_text(names[idx], 6, (int16_t)(y + 1));
		}

		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		{
			char foot[48];
			snprintf(foot, sizeof(foot), "%d/%d  Enter=valj Esc=ater",
			         sel + 1, count);
			font_draw_text(foot, 6, 190);
		}

		{
			const char* shot = getenv("STUNTS_CAR_SHOT");
			if (shot) { write_bmp(shot, pal); exit(0); }
		}

		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}

	if (ok) snprintf(out_car, out_sz, "%s", ids[sel]);
	return ok;
}

/* ------------------------------------------------------------------ */
/* The track picker (seg000 run_tracks_menu -> seg008 do_fileselect_dialog) */
/*                                                                      */
/* The layout is a string. MAIN.RES is the language file, and the dialog */
/* templates live in it under a prefix that `textresprefix` selects -    */
/* 'e' for English, so "loa" becomes "eloa":                             */
/*                                                                      */
/*   eloa: "Load @]Path:]@                 ]@]@]@]@]@]@]@]@]"            */
/*   elsu: "(Scroll Up)"      elsd: "(Scroll Down)"                      */
/*                                                                      */
/* ']' ends a line and '@' marks a field. So the dialog is a title, a    */
/* path line, and *eight* file rows - which is where the eight rows and  */
/* the scroll buttons below come from rather than from a guess.          */
/*                                                                      */
/* [DEVIATION] show_dialog (872 lines of seg008) parses that template and */
/* renders it through the DOS mouse driver and its own widget system;    */
/* do_fileselect_dialog adds another 777 for the file handling. Those are */
/* not ported. What is reproduced exactly is the data: the template's own */
/* strings, its eight-row shape, the game's font, and the file list read  */
/* from the same directory the original reads. The box is a filled        */
/* rectangle because that is what show_dialog draws it with               */
/* (sprite_clear_1_color); the interaction is SDL.                        */
/* ------------------------------------------------------------------ */
#define TRK_ROWS 8

static int trk_cmp(const void* a, const void* b)
{
	return strcmp(*(const char* const*)a, *(const char* const*)b);
}

/* Split a dialog template on ']' into at most n segments. */
static int dlg_split(const char* tpl, char seg[][40], int n)
{
	int k = 0, i = 0;
	while (*tpl && k < n) {
		if (*tpl == ']') { seg[k][i] = 0; k++; i = 0; }
		else if (i < 39)  { seg[k][i++] = *tpl; }
		tpl++;
	}
	if (i && k < n) { seg[k][i] = 0; k++; }
	return k;
}

/* The same dialog serves .TRK and .RPL: `eloa` is the generic Load template
 * and do_fileselect_dialog takes the extension as an argument
 * (seg000:130E2 passes ".rpl", run_tracks_menu ".trk"). */
static bool run_file_dialog(SDL_Window* win, SDL_Renderer* ren,
                            SDL_Texture* tex, const stunts_palette_t* pal,
                            const char* data_dir, void far* dlgfont,
                            const char* ext,
                            char* out_track, size_t out_sz)
{
	char (*names)[16] = NULL;
	char** ptrs = NULL;
	int count = 0, top = 0, sel = 0;
	bool done = false, ok = false;
	char seg[12][40];
	int nseg = 0;
	void far* res;
	char* tpl;
	DIR* d;
	struct dirent* e;

	/* the file list, from the same directory the original scans */
	names = (char (*)[16])malloc(256 * 16);
	ptrs = (char**)malloc(256 * sizeof(char*));
	if (!names || !ptrs) { free(names); free(ptrs); return false; }
	d = opendir(data_dir);
	while (d && (e = readdir(d)) && count < 256) {
		size_t l = strlen(e->d_name);
		if (l > 4 && l < 16 &&
		    (!strcasecmp(e->d_name + l - 4, ext))) {
			memcpy(names[count], e->d_name, l - 4);
			names[count][l - 4] = 0;
			ptrs[count] = names[count];
			count++;
		}
	}
	if (d) closedir(d);
	if (count == 0) { free(names); free(ptrs); return false; }
	qsort(ptrs, (size_t)count, sizeof(char*), trk_cmp);

    /* the template, so the wording is the game's own */
	res = file_load_resfile("main");
	tpl = res ? (char*)locate_shape_nofatal((char far*)res, "eloa") : NULL;
	if (tpl) nseg = dlg_split(tpl, seg, 12);
	{   /* '@' marks where a field goes; it is not part of the label */
		int k, j;
		for (k = 0; k < nseg; k++) {
			for (j = 0; seg[k][j]; j++)
				if (seg[k][j] == '@') { seg[k][j] = 0; break; }
			while (j > 0 && seg[k][j - 1] == ' ') seg[k][--j] = 0;
		}
	}
	if (nseg < 2) { strcpy(seg[0], "Load"); strcpy(seg[1], "Path:"); }

	rs_rgba = frame_rgba;
	font_set_fontdef2(dlgfont);

	while (!done) {
		SDL_Event ev;
		int i;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) { done = true; }
			else if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
				case SDLK_ESCAPE: done = true; break;
				case SDLK_UP:     if (sel > 0) sel--; break;
				case SDLK_DOWN:   if (sel < count - 1) sel++; break;
				case SDLK_PAGEUP:   sel -= TRK_ROWS; if (sel < 0) sel = 0; break;
				case SDLK_PAGEDOWN: sel += TRK_ROWS;
				                    if (sel > count - 1) sel = count - 1; break;
				case SDLK_RETURN: ok = true; done = true; break;
				default: break;
				}
			} else if (ev.type == SDL_MOUSEWHEEL) {
				sel -= ev.wheel.y;
				if (sel < 0) sel = 0;
				if (sel > count - 1) sel = count - 1;
			} else if (ev.type == SDL_MOUSEBUTTONDOWN &&
			           ev.button.button == SDL_BUTTON_LEFT) {
				int16_t mx, my;
				menu_point(win, ev.button.x, ev.button.y, &mx, &my);
				if (mx >= 62 && mx <= 258 && my >= 70 && my < 70 + TRK_ROWS * 11) {
					int row = (my - 70) / 11;
					if (top + row < count) {
						if (sel == top + row) { ok = true; done = true; }
						sel = top + row;
					}
				}
			}
		}
		if (sel < top) top = sel;
		if (sel >= top + TRK_ROWS) top = sel - TRK_ROWS + 1;
		if (top > count - TRK_ROWS) top = count - TRK_ROWS;
		if (top < 0) top = 0;

		menu_fill(58, 38, 262, 172, 0);        /* the box */
		menu_fill(59, 39, 261, 171, 8);        /* a one-pixel frame */
		menu_fill(60, 40, 260, 170, 0);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text(seg[0], 64, 43);                    /* "Load" */
		if (nseg > 1) font_draw_text(seg[1], 64, 55);      /* "Path:" */
		{   /* just the leaf directory - the full path runs off the box */
			const char* leaf = strrchr(data_dir, '/');
			font_draw_text((char*)(leaf ? leaf + 1 : data_dir), 100, 55);
		}

		for (i = 0; i < TRK_ROWS && top + i < count; i++) {
			int16_t y = (int16_t)(70 + i * 11);
			if (top + i == sel) {
				menu_fill(62, y, 258, (int16_t)(y + 10), 8);
				font_set_colour(0, 0);
			} else {
				font_set_colour((uint16_t)dialog_fnt_colour, 0);
			}
			font_draw_text(ptrs[top + i], 66, (int16_t)(y + 1));
		}
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		{
			char foot[48];
			snprintf(foot, sizeof(foot), "%d/%d  Enter=kor Esc=ater",
			         sel + 1, count);
			font_draw_text(foot, 64, 160);
		}

		{   /* one-shot render, for checking without a mouse */
			const char* shot = getenv("STUNTS_TRACK_SHOT");
			if (shot) { write_bmp(shot, pal); exit(0); }
		}

		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}

	if (ok) snprintf(out_track, out_sz, "%s", ptrs[sel]);
	free(names); free(ptrs);
	return ok;
}

static bool run_tracks_dialog(SDL_Window* win, SDL_Renderer* ren,
                              SDL_Texture* tex, const stunts_palette_t* pal,
                              const char* data_dir, void far* dlgfont,
                              char* out_track, size_t out_sz)
{
	return run_file_dialog(win, ren, tex, pal, data_dir, dlgfont, ".TRK",
	                       out_track, out_sz);
}

/* ------------------------------------------------------------------ */
/* The main menu (seg000 run_menu).                                     */
/*                                                                      */
/* Everything about it is data. The screen is one 320x200 shape called  */
/* "scrn" in SDMSEL.PVS - the road with the signposts - and the five    */
/* clickable areas are four parallel word tables in dseg:               */
/*                                                                      */
/*     menu_buttons_x1/x2/y1/y2, five entries each                      */
/*                                                                      */
/* which land exactly on the signs in the picture:                      */
/*                                                                      */
/*     0  105..208 119..197   the car, "Let's Drive"  -> start          */
/*     1   66..107  77..120   the round blue "Car" sign                 */
/*     2    5.. 67 114..170   the red "Opponent" stop sign              */
/*     3  190..253  76..122   the orange "Track" sign                   */
/*     4  255..312 116..166   the green "Option" sign                   */
/*                                                                      */
/* run_menu returns that index, or -1 to leave. The original's own hit  */
/* test is mouse_multi_hittest walking the same four tables.            */
/*                                                                      */
/* [DEVIATION] The original drives this through its DOS mouse driver and
 * its own idle timer (it drops into an attract mode after 0x1770 ticks).
 * Here it is an SDL event loop. The tables, the artwork and the meaning
 * of each index are the original's.                                    */
/* ------------------------------------------------------------------ */
static const int16_t menu_btn_x1[5] = { 105,  66,   5, 190, 255 };
static const int16_t menu_btn_x2[5] = { 208, 107,  67, 253, 312 };
static const int16_t menu_btn_y1[5] = { 119,  77, 114,  76, 116 };
static const int16_t menu_btn_y2[5] = { 197, 120, 170, 122, 166 };

/* seg000 mouse_multi_hittest: first rectangle containing the point. */
static int menu_hittest(int16_t x, int16_t y)
{
	int i;
	for (i = 0; i < 5; i++)
		if (x >= menu_btn_x1[i] && x <= menu_btn_x2[i] &&
		    y >= menu_btn_y1[i] && y <= menu_btn_y2[i])
			return i;
	return -1;
}

/* Window pixels -> the original's 320x200 space. */
static void menu_point(SDL_Window* win, int mx, int my, int16_t* ox, int16_t* oy)
{
	int ww = 1, wh = 1;
	SDL_GetWindowSize(win, &ww, &wh);
	if (ww < 1) ww = 1;
	if (wh < 1) wh = 1;
	*ox = (int16_t)((int32_t)mx * (VIEW_W / RFB_SCALE) / ww);
	*oy = (int16_t)((int32_t)my * (VIEW_H / RFB_SCALE) / wh);
}

/* A .FNT is a plain file, not one of the game's resource archives, so it is
 * read raw - file_load_resource() would look for an archive directory that is
 * not there. */
static void far* load_font_raw(const char* data_dir, const char* name)
{
	char fp[600];
	FILE* ff;
	long fl;
	uint8_t* fb;
	snprintf(fp, sizeof(fp), "%s/%s", data_dir, name);
	ff = fopen(fp, "rb");
	if (!ff) return NULL;
	fseek(ff, 0, SEEK_END); fl = ftell(ff); fseek(ff, 0, SEEK_SET);
	fb = (uint8_t*)malloc((size_t)fl);
	if (fb && fread(fb, 1, (size_t)fl, ff) != (size_t)fl) { free(fb); fb = NULL; }
	fclose(ff);
	return fb;
}

/* Solid fill straight into the presented frame, in the original's 320x200
 * coordinates. seg008 show_dialog clears its box with sprite_clear_1_color;
 * that writes to the palette-indexed buffer, and the dialog is composited
 * after the conversion, so it needs its own fill. */
static void menu_fill(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t idx)
{
	int16_t x, y;
	uint32_t c = pal_rgba[idx];
	for (y = (int16_t)(y0 * RFB_SCALE); y < (int16_t)(y1 * RFB_SCALE); y++) {
		if (y < 0 || y >= VIEW_H) continue;
		for (x = (int16_t)(x0 * RFB_SCALE); x < (int16_t)(x1 * RFB_SCALE); x++)
			if (x >= 0 && x < VIEW_W) frame_rgba[(int32_t)y * VIEW_W + x] = c;
	}
}

/* The menu screen goes straight into the presented frame. present() is for
 * the race, where it rebuilds that frame from the palette-indexed 3D view;
 * calling it here would overwrite the shape with a black screen, since the
 * indexed buffer holds nothing. */
static void menu_draw(void far* scrn)
{
	rs_rgba = frame_rgba;
	memset(frame_rgba, 0, sizeof frame_rgba);
	sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
	shape2d_op_unk3(scrn);
}

static int run_main_menu(SDL_Window* win, SDL_Renderer* ren, SDL_Texture* tex,
                         const stunts_palette_t* pal)
{
	void far* res = file_load_resource(3, "sdmsel.pvs");
	void far* scrn = res ? locate_shape_nofatal((char far*)res, "scrn") : NULL;
	unflip_shape(scrn);   /* no-op unless the header says otherwise */
	int chosen = -1;

	if (!scrn) {
		fprintf(stderr, "kan inte ladda menyskarmen (SDMSEL.PVS/scrn)\n");
		return 0;   /* fall through to a race rather than dying */
	}

	{   /* one-shot render, for checking the screen without a mouse */
		const char* shot = getenv("STUNTS_MENU_SHOT");
		if (shot) {
			menu_draw(scrn);
			write_bmp(shot, pal);
			return -1;
		}
	}

	while (chosen < 0) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) return -1;
			if (ev.type == SDL_KEYDOWN) {
				if (ev.key.keysym.sym == SDLK_ESCAPE) return -1;
				if (ev.key.keysym.sym == SDLK_RETURN) chosen = 0;
			}
			if (ev.type == SDL_MOUSEBUTTONDOWN &&
			    ev.button.button == SDL_BUTTON_LEFT) {
				int16_t x, y;
				int hit;
				menu_point(win, ev.button.x, ev.button.y, &x, &y);
				hit = menu_hittest(x, y);
				if (hit >= 0) chosen = hit;
			}
		}

		menu_draw(scrn);
		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}
	return chosen;
}

/* ================================================================== */
/* Phase 3: the opponent menu, the option menu, the fastest-times      */
/* screen and replay save/load.                                        */
/* ================================================================== */

extern void far* mainresptr;     /* MAIN.RES, the language file  */
extern void far* miscptr;        /* MISC.PRE, the button labels  */
extern void far* fontnptr;       /* FONTN.FNT, the narrow face   */
extern char far* locate_text_res(char far* res, char* key);
extern char far* td13_rpl_header;
extern char far* td16_rpl_buffer;
extern int16_t fontdef_unk_0E;
extern uint16_t font_op2(const char* str);
extern void format_frame_as_string(char* buf, int16_t frames, int16_t centiseconds);
#include "render_faithful/rhighscore.h"
#include "render_faithful/rwidgets.h"
#include "render_faithful/rendscreen.h"

/* dseg 21098: word_407F4 dw 15, word_407F6 dw 8, word_407F8 dw 7,
 * word_407FA dw 9. draw_button is called with (15, 8, 7, 0) throughout
 * seg000, and the option screen clears its background to 9. */
#define BTN_LIGHT 15
#define BTN_DARK   8
#define BTN_FACE   7
#define BTN_TEXT   0
#define OPT_BG     9

static const char* res_text(void far* res, const char* key)
{
	char far* p = res ? locate_text_res((char far*)res, (char*)key) : NULL;
	return p ? (const char*)p : "";
}

/* seg008 draw_button, behaviour-exact. The original draws a three-pixel
 * bevel with preRender_line - top and left in `light`, bottom and right in
 * `dark`, over a face filled by sprite_1_unk - and then centres the label,
 * splitting it on ']' into 8-pixel lines:
 *      lines  = 1 + count(']')
 *      y0     = (h - lines*8) / 2 + 1
 *      each line at x + (w - font_op2(line)) / 2
 * [DEVIATION] the bevel is drawn with menu_fill rather than five
 * preRender_line calls per edge; the pixels are the same. */
static void menu_button(const char* label, int16_t x, int16_t y,
                        int16_t w, int16_t h)
{
	int lines = 1, li = 0, i;
	const char* p;
	menu_fill(x, y, (int16_t)(x + w), (int16_t)(y + h), BTN_FACE);
	menu_fill(x, y, (int16_t)(x + w), (int16_t)(y + 3), BTN_LIGHT);
	menu_fill(x, y, (int16_t)(x + 3), (int16_t)(y + h), BTN_LIGHT);
	menu_fill(x, (int16_t)(y + h - 3), (int16_t)(x + w), (int16_t)(y + h), BTN_DARK);
	menu_fill((int16_t)(x + w - 3), y, (int16_t)(x + w), (int16_t)(y + h), BTN_DARK);
	if (!label || !*label) return;
	for (p = label; *p; p++) if (*p == ']') lines++;
	{
		int16_t y0 = (int16_t)((h - lines * 8) / 2 + 1);
		char seg[64];
		int n = 0;
		font_set_colour(BTN_TEXT, 0);
		for (i = 0; ; i++) {
			char c = label[i];
			if (c == ']' || c == 0) {
				seg[n] = 0;
				font_draw_text(seg,
					(int16_t)(x + (w - (int16_t)font_op2(seg)) / 2),
					(int16_t)(y + y0 + li * 8));
				li++; n = 0;
				if (c == 0) break;
			} else if (n < 63) {
				seg[n++] = c;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* The opponent menu (seg000 run_opponent_menu, 4276..4885).           */
/*                                                                     */
/* Data, once more. SDOSEL.PVS holds `scrn` (the backdrop), `clip`     */
/* (the frame drawn over the portrait) and seven portraits under the   */
/* packed name string "opp0opp1opp2opp3opp4opp5opp6" - index 0 being   */
/* "race against the clock". MISC.PRE holds the five button labels     */
/* under the language prefix:                                          */
/*                                                                     */
/*   ebla "Last"  ebnx "Next"  ebcl "Clock"  ebca "Car"  ebdo "Done"   */
/*   erac "Race against]the Clock.]"                                   */
/*                                                                     */
/* and each OPP<n>.PRE its own `edes`, e.g.                            */
/*   "Squealin']Bernie Rubber]]Age: 23 Height: 5'4]..."                */
/*                                                                     */
/* The five hit rectangles are the dseg tables opponentmenu_buttons_*  */
/* (dseg.asm 2529..2548): x1 20/76/132/188/244, x2 76/132/188/244/300, */
/* y 177..197 for all five - and draw_button is called at x+1, y+1 with */
/* w=0x36 h=0x12, which lands exactly inside them.                     */
/*                                                                     */
/* [DEVIATION] the event loop, the sprite window and the mouse driver  */
/* are SDL; run_car_menu (1300 lines) behind the "Car" button is this  */
/* port's own run_car_dialog. Everything drawn is the game's own data.  */
/* ------------------------------------------------------------------ */
static const int16_t opp_btn_x1[5] = {  20,  76, 132, 188, 244 };
static const int16_t opp_btn_x2[5] = {  76, 132, 188, 244, 300 };
static const int16_t opp_btn_y1 = 177, opp_btn_y2 = 197;

static int opp_hittest(int16_t x, int16_t y)
{
	int i;
	if (y < opp_btn_y1 || y > opp_btn_y2) return -1;
	for (i = 0; i < 5; i++)
		if (x >= opp_btn_x1[i] && x <= opp_btn_x2[i]) return i;
	return -1;
}

static void run_opponent_dialog(SDL_Window* win, SDL_Renderer* ren,
                                SDL_Texture* tex, const stunts_palette_t* pal,
                                const char* data_dir, void far* dlgfont)
{
	void far* oppres = file_load_resource(8, "sdosel.pvs");
	void far* scrn = oppres ? locate_shape_nofatal((char far*)oppres, "scrn") : NULL;
	void far* clip = oppres ? locate_shape_nofatal((char far*)oppres, "clip") : NULL;
	void far* portrait[7] = {0};
	int i;
	bool done = false;
	void far* desres = NULL;
	int desres_for = -1;

	if (oppres)
		locate_many_resources((char far*)oppres,
		                      "opp0opp1opp2opp3opp4opp5opp6",
		                      (char far**)portrait);
	unflip_shape(scrn);
	unflip_shape(clip);
	for (i = 0; i < 7; i++) unflip_shape(portrait[i]);

	rs_rgba = frame_rgba;

	while (!done) {
		SDL_Event ev;
		int hit;

		while (SDL_PollEvent(&ev)) {
			int action = -1;
			if (ev.type == SDL_QUIT) { done = true; break; }
			if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
				case SDLK_ESCAPE: case SDLK_RETURN: action = 4; break;
				case SDLK_LEFT:   action = 0; break;
				case SDLK_RIGHT:  action = 1; break;
				case SDLK_0:      action = 2; break;
				default: break;
				}
			} else if (ev.type == SDL_MOUSEBUTTONDOWN &&
			           ev.button.button == SDL_BUTTON_LEFT) {
				int16_t mx, my;
				menu_point(win, ev.button.x, ev.button.y, &mx, &my);
				hit = opp_hittest(mx, my);
				/* seg000 loc_12D8D: index 3 ("Car") is dead while
				 * racing the clock. */
				if (hit == 3 && opt_opponent == 0) hit = -1;
				action = hit;
			}
			switch (action) {
			case 0:   /* "Last" - loc_12DE0 */
				if (--opt_opponent < 1) opt_opponent = 6;
				break;
			case 1:   /* "Next" - loc_12DF6 */
				if (++opt_opponent == 7) opt_opponent = 1;
				break;
			case 2:   /* "Clock" - loc_12E0C */
				opt_opponent = 0;
				break;
			case 3:   /* "Car" - loc_12E14 -> run_car_menu */
				if (opt_opponent != 0) {
					char chosen[8];
					if (run_car_dialog(win, ren, tex, pal, data_dir,
					                   dlgfont, chosen, sizeof chosen)) {
						snprintf(opt_opponent_car,
						         sizeof opt_opponent_car, "%s", chosen);
						opt_opponent_car_chosen = 1;
					}
				}
				break;
			case 4:   /* "Done" - loc_12E6A */
				done = true;
				break;
			default: break;
			}
		}
		if (done) break;

		/* --- draw, in the original's order (loc_12A4D onwards) --- */
		menu_fill(0, 0, 320, 200, 0);      /* sprite_clear_1_color(0) */
		sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
		if (scrn) shape2d_op_unk3(scrn);

		font_set_fontdef2(dlgfont);
		menu_button(res_text(miscptr, "bla"), 0x15, (int16_t)(opp_btn_y1 + 1), 0x36, 0x12);
		menu_button(res_text(miscptr, "bnx"), 0x4D, (int16_t)(opp_btn_y1 + 1), 0x36, 0x12);
		menu_button(res_text(miscptr, "bcl"), 0x85, (int16_t)(opp_btn_y1 + 1), 0x36, 0x12);
		menu_button(opt_opponent ? res_text(miscptr, "bca") : "",
		            0xBD, (int16_t)(opp_btn_y1 + 1), 0x36, 0x12);
		menu_button(res_text(miscptr, "bdo"), 0xF5, (int16_t)(opp_btn_y1 + 1), 0x36, 0x12);

		sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
		if (portrait[opt_opponent]) shape2d_op_unk3(portrait[opt_opponent]);
		if (clip) shape2d_op_unk3(clip);

		/* loc_12C46: the profile, from the opponent's own file, or
		 * "Race against the Clock." from misc when there is none. */
		if (opt_opponent != desres_for) {
			char nm[16];
			if (desres) { unload_resource(desres); desres = NULL; }
			if (opt_opponent != 0) {
				snprintf(nm, sizeof nm, "opp%d", opt_opponent);
				desres = file_load_resfile(nm);
			}
			desres_for = opt_opponent;
		}
		{
			const char* txt = opt_opponent != 0
			                ? res_text(desres, "des")
			                : res_text(miscptr, "rac");
			char seg[64];
			int n = 0, y = 0;
			font_set_fontdef2(fontnptr ? fontnptr : dlgfont);
			font_set_colour((uint16_t)dialog_fnt_colour, 0);
			for (i = 0; ; i++) {
				char c = txt[i];
				if (c == ']' || c == 0) {
					seg[n] = 0;
					/* loc_12CD2: blank lines still advance y */
					if (n) font_draw_text(seg, 0x0C, (int16_t)(y + 0x21));
					y += fontdef_unk_0E; n = 0;
					if (c == 0) break;
				} else if (n < 63) {
					seg[n++] = c;
				}
			}
		}

		{
			const char* shot = getenv("STUNTS_OPP_SHOT");
			if (shot) { write_bmp(shot, pal); exit(0); }
		}

		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}

	/* loc_12E6A: an opponent with no car of its own gets the player's. */
	if (opt_opponent != 0 && !opt_opponent_car_chosen)
		snprintf(opt_opponent_car, sizeof opt_opponent_car, "%s", g_player_car);
	if (desres) unload_resource(desres);
}

/* ------------------------------------------------------------------ */
/* Replay save and load.                                               */
/*                                                                     */
/* A .RPL is the 26-byte GAMEINFO struct followed by one input byte per */
/* recorded frame - which is exactly what file_write_replay does        */
/* (fileio.c:1071): `*(GAMEINFO*)td13_rpl_header = gameconfig;` then    */
/* write sizeof(GAMEINFO) + gameconfig.game_recordedframes.  fileio.c   */
/* is not in the link (its DOS file layer is replaced by rfileio.c), so */
/* the two routines are transcribed here against the same globals.     */
/* ------------------------------------------------------------------ */
#define RPL_MAX 12000            /* td16_rpl_buffer is 0x2EE0 bytes */
/* 26 bytes of GAMEINFO + the 1802-byte track: where a .RPL's input bytes
 * start.  See THE FILE FORMAT over the replay bar's hooks below. */
#define RPL_DATA_OFS 0x724

static int16_t file_write_replay(const char* filename)
{
	FILE* f;
	size_t n;
	memcpy(td13_rpl_header, &gameconfig, sizeof(struct GAMEINFO));
	f = fopen(filename, "wb");
	if (!f) return 0;
	n = fwrite(td13_rpl_header, 1, sizeof(struct GAMEINFO), f);
	n += fwrite(td16_rpl_buffer, 1, gameconfig.game_recordedframes, f);
	fclose(f);
	return n == sizeof(struct GAMEINFO) + gameconfig.game_recordedframes;
}

/* Reading one back. stunts_load_replay() takes the inputs to start right
 * after the 26-byte header, which is what this port's own recordings look
 * like - but a .RPL the game wrote has the whole track in between, so its
 * first 1802 input bytes would be the track map read as steering. (Play the
 * shipped DEFAULT.RPL and the car sets off on a recording of HELL5.TRK.)
 * The two layouts tell themselves apart by length, and there is no overlap:
 * one is 26 + frames long and the other 1828 + frames. See THE FILE FORMAT
 * over the replay bar's hooks for where 1828 comes from. */
static bool load_replay_file(const char* path, stunts_game_info_t* info,
                             uint8_t** inputs, uint16_t* count)
{
	FILE* f;
	long len = 0;
	if (!stunts_load_replay(path, info, inputs, count)) return false;
	f = fopen(path, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		if (len == (long)RPL_DATA_OFS + info->recorded_frames) {
			size_t n;
			fseek(f, RPL_DATA_OFS, SEEK_SET);
			n = fread(*inputs, 1, info->recorded_frames, f);
			if (n < info->recorded_frames) {
				info->recorded_frames = (uint16_t)n;
				*count = (uint16_t)n;
			}
			printf("inspelningen har banan inbakad (%ld byte), "
			       "styrningen borjar vid %d\n", len, RPL_DATA_OFS);
		}
		fclose(f);
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* The option menu (seg000 run_option_menu, 4886..5125).               */
/*                                                                     */
/* The screen is "Stunts" (misc gstu) centred at y=6 and "Version 1.1  */
/* (Feb 12 1991)" (misc gver) at y=16, both through intro_draw_text,    */
/* over a background cleared to word_407FA = 9.  The menu itself is a  */
/* show_dialog template, misc emop:                                     */
/*                                                                     */
/*   "Options]}[Driving input device][Music on/off][Sound effects      */
/*    on/off][Load replay][Set graphic levels][Exit to Dos]"           */
/*                                                                     */
/* '}' separates the title from the buttons and '[' starts each one.   */
/* show_dialog returns the button index, and the jump table at         */
/* off_1314A (seg000:5136) maps it: -1 and 6 leave, 3 is the replay    */
/* loader, and the rest are the input/audio/graphics sub-screens.      */
/*                                                                     */
/* Returns 1 if a replay was loaded and should be played, else 0 -     */
/* which is what run_option_menu's own var_4 means.                    */
/*                                                                     */
/* [DEVIATION] show_dialog (872 lines) is not ported: the template is  */
/* parsed here and the buttons drawn with the same draw_button bevel.  */
/* Input and Music are stubs that print their own text resource;       */
/* Sound effects toggles src/audio_native.c (Phase 4) and Graphics     */
/* toggles detail_level, the only graphics knob this port has.         */
/* Music stays unimplemented: the .KMS songs need the same OPL2/MT-32  */
/* emulation the .VCE voices do, which is out of scope by choice.      */
/* ------------------------------------------------------------------ */
static int dlg_buttons(const char* tpl, char title[64], char btn[8][40])
{
	int n = 0, i = 0;
	const char* p = tpl;
	title[0] = 0;
	while (*p && *p != '}' && i < 63) title[i++] = *p++;
	title[i] = 0;
	while (i > 0 && (title[i-1] == ']' || title[i-1] == ' ')) title[--i] = 0;
	while (*p && n < 8) {
		if (*p == '[') {
			int k = 0;
			p++;
			while (*p && *p != '[' && *p != ']' && k < 39) btn[n][k++] = *p++;
			btn[n][k] = 0;
			n++;
			if (*p == ']') p++;
		} else p++;
	}
	return n;
}

/*
 * Phase 7: the graphics sub-screen, seg008 show_graphic_levels_menu
 * (5247..5422).
 *
 * The original builds its dialog from the "mrl" template in MAIN.RES: nine
 * `[` markers, and it writes a `*` after the one that is currently chosen,
 * so the whole screen is a checkbox list. What the nine choices *do* is the
 * part worth being exact about (loc_2A098 and the four labels below it):
 *
 *     0..4  detail_level = choice
 *     5     slow_video_mgmt = 0
 *     6     slow_video_mgmt = 1
 *     7     framespersec2 = 0x0A
 *     8     framespersec2 = 0x14
 *     9     leave
 *
 * and on the way out, if framespersec2 changed, it puts up "mrs".
 *
 * detail_level is real here: frame.c reads it in four places, including
 * detail_threshold_by_level[], so these five settings genuinely change what
 * gets drawn. slow_video_mgmt selects an offscreen-window path this port
 * does not have (see rintro3d.c), so choices 5 and 6 are shown and stored -
 * the setting round-trips - but nothing reads it. That is stated on screen
 * rather than hidden.
 *
 * [DEVIATION] Drawn in this port's own dialog chrome, like every other
 * screen here, instead of through show_dialog. The nine choices and their
 * effects are the original's.
 */
static void run_graphics_dialog(SDL_Window* win, SDL_Renderer* ren,
                                SDL_Texture* tex, const stunts_palette_t* pal,
                                void far* dlgfont)
{
	static const char* const rows[9] = {
		"Detaljniva 0  (snabbast)",
		"Detaljniva 1",
		"Detaljniva 2",
		"Detaljniva 3",
		"Detaljniva 4  (finast)",
		"Skarmhantering 0",
		"Skarmhantering 1  (oanvand)",
		"10 bilder/s",
		"20 bilder/s",
	};
	int sel = 0;
	bool done = false;
	uint16_t fps_on_entry = framespersec2;   /* var_212 */

	rs_rgba = frame_rgba;
	font_set_fontdef2(dlgfont);

	while (!done) {
		SDL_Event ev;
		int i;
		int16_t bx = 40, by = 38, bw = 240, bh = 13;

		while (SDL_PollEvent(&ev)) {
			int act = -1;
			if (ev.type == SDL_QUIT) { done = true; break; }
			else if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
				case SDLK_ESCAPE: done = true; break;
				case SDLK_UP:     if (--sel < 0) sel = 8; break;
				case SDLK_DOWN:   if (++sel > 8) sel = 0; break;
				case SDLK_RETURN:
				case SDLK_KP_ENTER: act = sel; break;
				default: break;
				}
			} else if (ev.type == SDL_MOUSEBUTTONDOWN) {
				int mx = ev.button.x * 320 / (VIEW_W * 320 / VIEW_W);
				int my = ev.button.y;
				int row;
				SDL_GetMouseState(&mx, &my);
				mx = mx * 320 / (int)(VIEW_W / RFB_SCALE ? VIEW_W : 320);
				row = (my - by) / (bh + 2);
				if (row >= 0 && row < 9) { sel = row; act = row; }
			}
			if (act < 0) continue;
			/* loc_2A098 and the four labels under it */
			if (act <= 4)      detail_level = (uint8_t)act;
			else if (act == 5) slow_video_mgmt = 0;
			else if (act == 6) slow_video_mgmt = 1;
			else if (act == 7) framespersec2 = 0x0A;
			else               framespersec2 = 0x14;
		}
		if (done) break;

		menu_fill(0, 0, 320, 200, OPT_BG);
		font_set_fontdef2(dlgfont);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text("Grafik", bx, 24);
		for (i = 0; i < 9; i++) {
			int16_t y = (int16_t)(by + i * (bh + 2));
			char line[64];
			/* the `*` the original writes after the chosen `[` */
			int on = i <= 4 ? (detail_level == (uint8_t)i)
			       : i <= 6 ? (slow_video_mgmt == (uint16_t)(i - 5))
			       : i == 7 ? (framespersec2 == 0x0A)
			                : (framespersec2 != 0x0A);
			snprintf(line, sizeof line, "%s %s", on ? "*" : " ", rows[i]);
			menu_button(line, bx, y, bw, bh);
			if (i == sel) {
				menu_fill(bx, y, (int16_t)(bx + bw), (int16_t)(y + 1), BTN_TEXT);
				menu_fill(bx, (int16_t)(y + bh - 1), (int16_t)(bx + bw),
				          (int16_t)(y + bh), BTN_TEXT);
			}
		}
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text("Enter=valj  Esc=klar", bx, 182);

		{
			const char* shot = getenv("STUNTS_GRAPHICS_SHOT");
			if (shot) { write_bmp(shot, pal); exit(0); }
		}
		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}

	/* loc_2A0CC: the "mrs" note, only if the frame rate actually changed. */
	if (framespersec2 != fps_on_entry)
		printf("bildfrekvens andrad till %u/s\n", framespersec2);
	rs_rgba = NULL;
	(void)win;
}

static bool run_option_dialog(SDL_Window* win, SDL_Renderer* ren,
                              SDL_Texture* tex, const stunts_palette_t* pal,
                              const char* data_dir, void far* dlgfont,
                              char* out_replay, size_t out_sz)
{
	char title[64], btn[8][40];
	int nbtn;
	int sel = 0;
	bool done = false, loaded = false;
	const char* note = "";
	int confirm_exit = 0;

	nbtn = dlg_buttons(res_text(miscptr, "mop"), title, btn);
	if (nbtn == 0) return false;

	/* test hook: go straight into the graphics sub-screen, which is two
	 * keystrokes deep otherwise. */
	if (getenv("STUNTS_GRAPHICS_SHOT"))
		run_graphics_dialog(win, ren, tex, pal, dlgfont);
	/* the same for the joystick calibration screen, which is behind the
	 * first button. rjoy_calibrate writes the BMP itself and returns
	 * without waiting for a stick when STUNTS_JOYCAL_SHOT is set. */
	if (getenv("STUNTS_JOYCAL_SHOT")) {
		rjoy_calibrate(win, ren, tex, pal, dlgfont);
		SDL_Quit();
		exit(0);
	}

	rs_rgba = frame_rgba;
	font_set_fontdef2(dlgfont);

	while (!done) {
		SDL_Event ev;
		int i;
		int16_t bx = 60, by = 60, bw = 200, bh = 18;

		while (SDL_PollEvent(&ev)) {
			int act = -1;
			if (ev.type == SDL_QUIT) { done = true; break; }
			else if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
				case SDLK_ESCAPE: done = true; break;
				case SDLK_UP:   if (--sel < 0) sel = nbtn - 1; break;
				case SDLK_DOWN: if (++sel >= nbtn) sel = 0; break;
				case SDLK_RETURN: act = sel; break;
				default: break;
				}
			} else if (ev.type == SDL_MOUSEBUTTONDOWN &&
			           ev.button.button == SDL_BUTTON_LEFT) {
				int16_t mx, my;
				menu_point(win, ev.button.x, ev.button.y, &mx, &my);
				if (mx >= bx && mx < bx + bw) {
					int row = (my - by) / (bh + 2);
					if (my >= by && row >= 0 && row < nbtn) { sel = row; act = row; }
				}
			}
			if (act < 0) continue;
			/* off_1314A, seg000:5136 */
			switch (act) {
			/* off_1314A[0] is do_joy_restext, seg008 4708..4993 -
			 * the nine-square calibration screen. It returns
			 * byte_3FE00 bit 0 as it leaves it: the stick is the
			 * driving input device, or it is not. */
			case 0:
				note = rjoy_calibrate(win, ren, tex, pal, dlgfont)
				     ? res_text(mainresptr, "joy")
				     : res_text(mainresptr, "key");
				break;
			case 1:  note = res_text(mainresptr, "mof"); break;  /* music */
			case 2:  /* sound effects on/off - Phase 4 wires this up */
				audio_native_set_enabled(!audio_native_enabled());
				note = res_text(mainresptr, "son");
				break;
			case 3: {                                            /* load replay */
				char chosen[64];
				if (run_file_dialog(win, ren, tex, pal, data_dir, dlgfont,
				                    ".RPL", chosen, sizeof chosen)) {
					snprintf(out_replay, out_sz, "%s/%s.RPL", data_dir, chosen);
					loaded = true; done = true;
				}
				break;
			}
			case 4:  /* seg008 show_graphic_levels_menu, five detail levels and
			          * the frame rate - not the two-way toggle this used to be */
				run_graphics_dialog(win, ren, tex, pal, dlgfont);
				break;
			case 5:  /* "Exit to Dos". [DEVIATION] do_dos_restext puts up
			          * edos ("Exit to Dos]}[ No [ Yes ]") first; here the
			          * button has to be pressed twice. */
				if (confirm_exit) { SDL_Quit(); exit(0); }
				confirm_exit = 1;
				note = res_text(mainresptr, "dos");
				break;
			default: done = true; break;                          /* "Done" */
			}
		}
		if (done) break;

		menu_fill(0, 0, 320, 200, OPT_BG);
		font_set_fontdef2(dlgfont);
		{   /* gstu/gver are looked up with locate_shape_alt, NOT
		     * locate_text_res: they are the only two strings in MISC.PRE
		     * that carry no language prefix (seg000:12F9x). */
			char* s = miscptr ? (char*)locate_shape_nofatal((char far*)miscptr,
			                                                "gstu") : NULL;
			if (s) intro_draw_text(s,
				(int16_t)((0x140 - (int16_t)font_op2(s)) / 2), 6,
				dialog_fnt_colour, 0);
			s = miscptr ? (char*)locate_shape_nofatal((char far*)miscptr,
			                                          "gver") : NULL;
			if (s) intro_draw_text(s,
				(int16_t)((0x140 - (int16_t)font_op2(s)) / 2), 0x10,
				dialog_fnt_colour, 0);
		}
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text(title, bx, 44);
		for (i = 0; i < nbtn; i++) {
			int16_t y = (int16_t)(by + i * (bh + 2));
			menu_button(btn[i], bx, y, bw, bh);
			if (i == sel) {
				menu_fill(bx, y, (int16_t)(bx + bw), (int16_t)(y + 1), BTN_TEXT);
				menu_fill(bx, (int16_t)(y + bh - 1), (int16_t)(bx + bw),
				          (int16_t)(y + bh), BTN_TEXT);
			}
		}
		font_set_fontdef2(dlgfont);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		if (*note) {
			char one[48]; int k;
			for (k = 0; k < 47 && note[k] && note[k] != ']'; k++) one[k] = note[k];
			one[k] = 0;
			font_draw_text(one, bx, 186);
		}

		{
			const char* shot = getenv("STUNTS_OPTION_SHOT");
			if (shot) { write_bmp(shot, pal); exit(0); }
		}

		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}
	return loaded;
}

/* ------------------------------------------------------------------ */
/* The fastest-times screen (seg000 highscore_text_unk, drawn here).   */
/* The table itself is rhighscore.c; this is only the frame around it. */
/* ------------------------------------------------------------------ */
static void run_highscore_screen(SDL_Window* win, SDL_Renderer* ren,
                                 SDL_Texture* tex, const stunts_palette_t* pal,
                                 void far* dlgfont)
{
	bool done = false;
	(void)win;
	rs_rgba = frame_rgba;
	while (!done) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT || ev.type == SDL_KEYDOWN ||
			    ev.type == SDL_MOUSEBUTTONDOWN) done = true;
		}
		/* seg000:13ECD - the table sits on one full-width bevelled
		 * panel, draw_button(NULL, 0, 0, 0x140, 0x64, 15, 8, 7, 0).
		 * That grey face is why the rows are drawn in colour 0. */
		menu_fill(0, 0, 320, 200, 0);
		menu_button("", 0, 0, 0x140, 0x64);
		highscore_text_unk();
		font_set_fontdef2(dlgfont);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text("tryck for att fortsatta", 0x10, 180);
		{
			const char* shot = getenv("STUNTS_HISCORE_SHOT");
			if (shot) { write_bmp(shot, pal); exit(0); }
		}
		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(16);
	}
}

/* ------------------------------------------------------------------ */
/* The results screen - seg000 end_hiscore, 5126..7089, ported in full  */
/* into src/render_faithful/rendscreen.c.  What is left here is the     */
/* host half the original gets from the DOS mouse driver and its        */
/* offscreen sprite window: the event loop, the one-shot test hook, and */
/* the name entry that stands in for show_dialog + call_read_line.      */
/*                                                                     */
/* end_hiscore's return value is the original's: 0 = view the replay,   */
/* 1 = race/drive again, 2 = back to the main menu (seg000:104C0 reads  */
/* it exactly that way).                                               */
/* ------------------------------------------------------------------ */
extern int16_t gState_topSpeed, gState_jumpCount;
extern int16_t gState_total_finish_time, gState_penalty;
extern int16_t gState_144, gState_impactSpeed;
extern int16_t gState_pEndFrame, gState_oEndFrame;
extern int32_t gState_travDist;
extern uint16_t gState_frame;
extern void far* fontdefptr;

/* seg008 mouse_timer_sprite_unk: word_45D1C wraps at 60 ticks and the
 * selection outline is word_407CE for the first 30 and word_407D0 for the
 * rest.  At the original's tick rate that is a half-second blink. */
static int rs_blink(void) { return ((SDL_GetTicks() / 500) & 1) != 0; }

static void rs_present(SDL_Renderer* ren, SDL_Texture* tex)
{
	SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
	SDL_RenderClear(ren);
	SDL_RenderCopy(ren, tex, NULL, NULL);
	SDL_RenderPresent(ren);
	SDL_Delay(16);
}


/* A scripted run of the results screen, so its three phases can be driven
 * without a mouse: STUNTS_ENDSCREEN_KEYS="right right enter" pushes one of
 * those keys as a real SDL event per frame, and every loop handles them
 * exactly as it handles a human.  "q" pushes SDL_QUIT. */
static void rs_script_pump(void)
{
	static char script[256];
	static char* save;
	static int armed;
	char* tok;
	SDL_Event e;
	if (!armed) {
		const char* env = getenv("STUNTS_ENDSCREEN_KEYS");
		if (!env) { armed = -1; return; }
		snprintf(script, sizeof script, "%s", env);
		save = script;
		armed = 1;
	}
	if (armed < 0) return;
	tok = strtok_r(NULL, " ,", &save);
	if (save == script) tok = strtok_r(script, " ,", &save);
	if (!tok) return;
	memset(&e, 0, sizeof e);
	if (!strcmp(tok, "q")) { e.type = SDL_QUIT; SDL_PushEvent(&e); return; }
	e.type = SDL_KEYDOWN;
	e.key.state = SDL_PRESSED;
	e.key.keysym.sym = !strcmp(tok, "left")  ? SDLK_LEFT
	                 : !strcmp(tok, "right") ? SDLK_RIGHT
	                 : !strcmp(tok, "space") ? SDLK_SPACE
	                 : !strcmp(tok, "esc")   ? SDLK_ESCAPE
	                 : SDLK_RETURN;
	e.key.keysym.scancode = SDL_GetScancodeFromKey(e.key.keysym.sym);
	SDL_PushEvent(&e);
}

static int16_t run_result_screen(SDL_Window* win, SDL_Renderer* ren,
                                 SDL_Texture* tex, const stunts_palette_t* pal,
                                 const char* data_dir, void far* dlgfont,
                                 const char* track, const char* car)
{
	struct ENDSCREEN es;
	char trackpath[700];
	char saved[128] = {0};
	int16_t choice = 2;                 /* what quitting means */
	bool done = false, quit_now = false;
	uint32_t last_tick;
	const char* shot = getenv("STUNTS_ENDSCREEN_SHOT");
	if (!shot) shot = getenv("STUNTS_RESULT_SHOT");   /* the Phase 3 name */

	if (!fontdefptr) fontdefptr = dlgfont;
	rs_rgba = frame_rgba;

	snprintf(trackpath, sizeof trackpath, "%s/%s.TRK", data_dir, track);
	endscreen_open(&es, trackpath);

	/* seg000:134DC - file_load_audiores("skidvict"/"skidover", "skidms",
	 * "VICT"/"OVER").  Phase 8 owns the sequencer. */
	music_native_play(es.song == 3 ? MUSIC_SONG_VICTORY : MUSIC_SONG_GAMEOVER);
	printf("resultatskarm: %s, sang %s\n",
	       es.var_18 == 0 ? "du vann" : es.var_18 == 1 ? "motstandaren vann"
	                                                   : "ingen vinnare",
	       es.song == 3 ? "SKIDVICT" : "SKIDOVER");

	/* ---- the one-shot render hook -------------------------------- */
	if (shot) {
		const char* page = getenv("STUNTS_ENDSCREEN_PAGE");
		const char* fr   = getenv("STUNTS_ENDSCREEN_FRAME");
		/* 0 = the evaluation, 1 = the table, 2 = the "Continue" wait. */
		int pg = page ? atoi(page) : (es.var_14 != 0);
		if (getenv("STUNTS_ENDSCREEN_HNA")) {
			/* the loc_13F84 branch: the .TRK no longer matches the track
			 * in memory, or the .HIG cannot be read or created */
			es.var_6E = 0xFF;
			es.nbuttons = 3;
			es.var_9C = -36;
			for (int i = 0; i < 4; i++) {
				es.x1[i] = (int16_t)(es.x1[i] + es.var_9C);
				es.x2[i] = (int16_t)(es.x2[i] + es.var_9C);
			}
			pg = 1;
		}
		if (fr) { es.var_8E = (int16_t)atoi(fr); es.var_6C = -1; }
		if (pg == 2) endscreen_draw_continue(&es);
		else { es.var_14 = (int16_t)(pg != 0); endscreen_draw(&es, 1); }
		write_bmp(shot, pal);
		endscreen_close(&es);
		exit(0);
	}

	/* ---- seg000:13D13, the "Continue" wait before the name entry -- */
	if (es.var_6E == 1 && es.var_16 != 0) {
		uint32_t last = SDL_GetTicks();
		bool waiting = true;
		while (waiting) {
			SDL_Event ev;
			uint32_t now;
			rs_script_pump();
			while (SDL_PollEvent(&ev)) {
				if (ev.type == SDL_QUIT) { waiting = false; quit_now = true; }
				else if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN)
					waiting = false;
			}
			now = SDL_GetTicks();
			endscreen_advance_continue(&es, (int16_t)((now - last) / 50));
			if ((now - last) >= 50) last = now - (now - last) % 50;
			endscreen_draw_continue(&es);
			rs_present(ren, tex);
		}
	}

	/* ---- seg000 enter_hiscore, the half that needs a text widget --- */
	if (es.var_6E == 1 && !quit_now) {
		char name[17] = {0};
		char carname[24] = {0};
		char opp[10] = " ";
		int namelen = 0;
		bool typing = true;

		/* enter_hiscore stores gnam_string (the car's own long name) and,
		 * with an opponent, "<initials>/<gsna>" - the opponent's initials,
		 * a slash and its car's short name (seg000:11ABC). */
		{
			char base[16];
			void far* res;
			char* nm;
			snprintf(base, sizeof base, "car%.4s", car);
			res = file_load_resfile(base);
			nm = res ? (char*)locate_shape_nofatal((char far*)res, "gnam") : NULL;
			snprintf(carname, sizeof carname, "%s", nm && *nm ? nm : car);
		}
		if (opt_opponent != 0) {
			char base[16];
			void far* res;
			char* sn;
			snprintf(base, sizeof base, "car%.4s", opt_opponent_car);
			res = file_load_resfile(base);
			sn = res ? (char*)locate_shape_nofatal((char far*)res, "gsna") : NULL;
			snprintf(opp, sizeof opp, "%s/%s", g_opponent_initials,
			         sn && *sn ? sn : opt_opponent_car);
		}

		es.hiscore_row = highscore_would_enter(es.var_88);
		if (es.hiscore_row >= 0) {
			/* [bp+arg_6] goes straight into the record's byte 41, and
			 * that byte is what print_highscore_entry parenthesises on -
			 * so a time set while the opponent won is bracketed. */
			highscore_insert(es.hiscore_row, carname, es.var_18, opp);
			SDL_StartTextInput();
			while (typing) {
				SDL_Event ev;
				rs_script_pump();
				while (SDL_PollEvent(&ev)) {
					if (ev.type == SDL_QUIT) { typing = false; quit_now = true; }
					else if (ev.type == SDL_TEXTINPUT && namelen < 16) {
						name[namelen++] = ev.text.text[0];
						name[namelen] = 0;
					} else if (ev.type == SDL_KEYDOWN) {
						if (ev.key.keysym.sym == SDLK_BACKSPACE && namelen)
							name[--namelen] = 0;
						else if (ev.key.keysym.sym == SDLK_RETURN ||
						         ev.key.keysym.sym == SDLK_ESCAPE)
							typing = false;
					}
				}
				es.var_14 = 1;
				endscreen_draw(&es, rs_blink());
				/* the einh template, "You've got a high score!]Please
				 * input your name.]@]" - '@' is where the field goes. */
				{
					const char* inh = res_text(miscptr, "inh");
					char seg[64];
					int n = 0, i;
					int16_t y = 0x69;
					font_set_fontdef2(fontdefptr);
					font_set_colour((uint16_t)dialog_fnt_colour, 0);
					for (i = 0; ; i++) {
						char c = inh[i];
						if (c == ']' || c == 0) {
							seg[n] = 0;
							if (n && seg[0] != '@') {
								font_draw_text(seg,
									(int16_t)((0x140 - (int16_t)font_op2(seg)) / 2),
									y);
								y += 10;
							}
							n = 0;
							if (c == 0) break;
						} else if (n < 63) seg[n++] = c;
					}
					snprintf(seg, sizeof seg, "%s_", name);
					font_draw_text(seg,
						(int16_t)((0x140 - (int16_t)font_op2(seg)) / 2), y);
				}
				rs_present(ren, tex);
			}
			SDL_StopTextInput();
			highscore_set_name(name);
			highscore_write_b();
		}
		es.var_6E = 0;
		es.var_14 = 1;
	}

	/* ---- seg000:14188, the main loop ----------------------------- */
	last_tick = SDL_GetTicks();
	if (quit_now) { endscreen_close(&es); return 2; }
	while (!done) {
		SDL_Event ev;
		uint32_t now;

		rs_script_pump();
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) { choice = 2; done = true; break; }
			if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONDOWN) {
				int16_t mx, my, hit;
				menu_point(win, ev.type == SDL_MOUSEMOTION ? ev.motion.x
				                                           : ev.button.x,
				           ev.type == SDL_MOUSEMOTION ? ev.motion.y
				                                      : ev.button.y, &mx, &my);
				hit = endscreen_hittest(&es, mx, my);
				if (hit >= 0) es.var_selectedmenu = hit;
				if (ev.type == SDL_MOUSEBUTTONDOWN && hit >= 0) {
					int16_t r = endscreen_activate(&es);
					if (r >= 0) { choice = r; done = true; break; }
				}
				continue;
			}
			if (ev.type != SDL_KEYDOWN) continue;
			if (getenv("STUNTS_ENDSCREEN_KEYS"))
				printf("tangent %d: val=%d knappar=%d sida=%d\n",
				       (int)ev.key.keysym.sym, (int)es.var_selectedmenu,
				       (int)es.nbuttons, (int)es.var_14);
			switch (ev.key.keysym.sym) {
			case SDLK_LEFT:  endscreen_left(&es);  break;
			case SDLK_RIGHT: endscreen_right(&es); break;
			case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: {
				int16_t r = endscreen_activate(&es);
				if (r >= 0) { choice = r; done = true; }
				break;
			}
			case SDLK_ESCAPE: choice = 2; done = true; break;   /* 0x1B */
			case SDLK_s: {
				/* Port-only, and it was here before Phase 6: writing the
				 * recording out.  The original's replay save lives in
				 * loop_game's bar (Phase 9), not on this screen. */
				char p[700];
				snprintf(p, sizeof p, "%s/%s.RPL", data_dir, track);
				if (file_write_replay(p))
					snprintf(saved, sizeof saved, "%s.RPL (%u rutor)",
					         track, gameconfig.game_recordedframes);
				else
					snprintf(saved, sizeof saved, "%s",
					         res_text(mainresptr, "ser"));
				break;
			}
			default: break;
			}
			if (done) break;      /* the original leaves the loop at once */
		}
		if (done) break;

		now = SDL_GetTicks();
		if (now - last_tick >= 50) {
			endscreen_advance(&es, (int16_t)((now - last_tick) / 50));
			last_tick = now - (now - last_tick) % 50;
		}

		endscreen_draw(&es, rs_blink());
		if (*saved) {
			font_set_fontdef2(fontdefptr);
			font_set_colour((uint16_t)dialog_fnt_colour, 0);
			font_draw_text(saved, 8, 0xA4);
		}
		rs_present(ren, tex);
	}

	endscreen_close(&es);
	return choice;
}

/* ------------------------------------------------------------------ */
/* The three blocks of game_init that a replay load has to run again.  */
/*                                                                     */
/* seg005 loc_2450A reloads a race from a .RPL header, and the original */
/* does it with ported_free_player_cars_ + ported_setup_player_cars_    */
/* followed by init_game_state, whose second half places the cars. This */
/* port has neither routine: game_init did all of it inline, once. So   */
/* the three pieces are lifted out here, byte for byte, and called both */
/* from game_init and from the replay bar's hooks. Every id they need   */
/* is read from gameconfig, which is where the .RPL header has just     */
/* landed - and which game_init fills in from the command line first.   */
/* ------------------------------------------------------------------ */

/* One car's SIMD block and its aero table (setup_aero_trackdata). */
static bool load_one_simd(const char* what, const char* carid,
                          struct SIMD* out, int16_t* aero)
{
	char p[600];
	stunts_res_archive_t* a;
	const stunts_sub_resource_t* sd;
	size_t k;
	int i;
	snprintf(p, sizeof(p), "%s/CAR%.4s.RES", rfileio_get_data_dir(), carid);
	a = stunts_asset_load_archive(p);
	if (!a) { fprintf(stderr, "kan inte ladda %s %s\n", what, p); return false; }
	sd = stunts_asset_find_resource(a, "simd");
	if (!sd) { fprintf(stderr, "%s saknar simd-block\n", what); return false; }
	k = sd->size < sizeof(struct SIMD) ? sd->size : sizeof(struct SIMD);
	memcpy(out, sd->data, k);
	/* aerorestable is a pointer into trakdata in the original */
	for (i = 0; i < 0x40; i++)
		aero[i] = (int16_t)(((int32_t)out->aero_resistance * i * i) >> 9);
	out->aerorestable = aero;
	return true;
}

/* restunts.c setup_player_cars(): the opponent gets the same treatment as
 * the player - its own SIMD, its own aero table - and only then is
 * load_opponent_data() allowed to run, because that routine needs
 * track_setup()'s td01/td02/td17 tables, which must already be built. */
static bool setup_player_cars(void)
{
	if (!load_one_simd("bilen", gameconfig.game_playercarid,
	                   &simd_player, s_aero)) return false;
	if (gameconfig.game_opponenttype != 0) {
		if (!load_one_simd("motstandarbilen", gameconfig.game_opponentcarid,
		                   &simd_opponent, s_aero_opp)) return false;
		load_opponent_data();
		printf("motstandare %d (%s), bil %.4s, korlinje %d rutor, kostnad %ld\n",
		       (int)gameconfig.game_opponenttype, g_opponent_initials,
		       gameconfig.game_opponentcarid,
		       sfopp_path_len, (long)sfopp_path_cost);
	}
	return true;
}

/* The 3D car shapes and the cockpit bitmaps. setup_car_shapes(0) is what
 * fills in roofbmpheight and dashbmp_y, which decide the windshield height. */
static void load_car_shapes(void)
{
	char carid[5] = {0}, oppid[5] = { (char)0xFF, 0, 0, 0, 0 };
	memcpy(carid, gameconfig.game_playercarid, 4);
	if (gameconfig.game_opponenttype != 0)
		memcpy(oppid, gameconfig.game_opponentcarid, 4);
	shape3d_load_car_shapes(carid, oppid);
	setup_car_shapes(0);
}

/* Start placement: tile centre + (210 back, 36 lateral), per restunts.c
 * init_game_state (seg001 4021..). Identical to the code verified against
 * the oracle in tools/dump_native_states.c; track_angle and hillFlag come
 * from track_setup() rather than being assumed zero. */
static void place_cars_at_start(void)
{
	int16_t a = track_angle;
	int32_t tmpcol = multiply_and_scale(sin_fast((uint16_t)(a + 0x200)), 210)
	               + multiply_and_scale(sin_fast((uint16_t)(a + 0x100)), 36);
	int32_t tmprow = multiply_and_scale(cos_fast((uint16_t)(a + 0x200)), 210)
	               + multiply_and_scale(cos_fast((uint16_t)(a + 0x100)), 36);
	int32_t cx = ((int32_t)startcol2 * 1024 + 512) + tmpcol;
	int32_t cz = ((int32_t)(29 - startrow2) * 1024 + 512) + tmprow;
	init_carstate(&state.playerstate, &simd_player, 1,
	              cx * 64,
	              (int32_t)hillHeightConsts[(int)hillFlag] * 64,
	              cz * 64,
	              (int16_t)(-a));
	/* init_game_state also mirrors the start tile into GAMESTATE. */
	state.game_startcol  = startcol2;
	state.game_startcol2 = startcol2;
	state.game_startrow  = startrow2;
	state.game_startrow2 = startrow2;

	/* The opponent is placed by the same code with 0x300 in place of the
	 * player's 0x100 - i.e. 36 units to the OTHER side of the tile centre,
	 * which is what puts the two cars side by side on the grid
	 * (restunts.c init_game_state, "Init opponent car"). */
	if (gameconfig.game_opponenttype != 0) {
		int32_t ocol = multiply_and_scale(sin_fast((uint16_t)(a + 0x200)), 210)
		             + multiply_and_scale(sin_fast((uint16_t)(a + 0x300)), 36);
		int32_t orow = multiply_and_scale(cos_fast((uint16_t)(a + 0x200)), 210)
		             + multiply_and_scale(cos_fast((uint16_t)(a + 0x300)), 36);
		init_carstate(&state.opponentstate, &simd_opponent, 1,
		              (((int32_t)startcol2 * 1024 + 512) + ocol) * 64,
		              (int32_t)hillHeightConsts[(int)hillFlag] * 64,
		              (((int32_t)(29 - startrow2) * 1024 + 512) + orow) * 64,
		              (int16_t)(-a));
		sub_18D60(((int16_t*)trackdata3)[state.opponentstate.car_trackdata3_index],
		          &state.opponentstate.car_vec_unk3,
		          (int16_t)(uint8_t)state.opponentstate.field_CE,
		          (int16_t*)&state.field_3F9);
		state.opponentstate.field_CE++;
	}
}

/* ------------------------------------------------------------------ */
/* The replay bar's Load Replay and Save Replay (seg005 loc_2450A and   */
/* loc_24630).                                                          */
/*                                                                      */
/* The control flow is in rreplaybar.c, ported from the listing; this is */
/* the host half it asks for through the hooks rreplaybar.h documents -  */
/* the two file dialogs, the file layer and the two routines that build  */
/* a race again for a recording driven in another car.                   */
/*                                                                      */
/* THE FILE FORMAT. A .RPL is 26 bytes of GAMEINFO, then 1802 bytes of   */
/* track - the two 901-byte maps, i.e. a whole .TRK - then one input     */
/* byte per recorded frame. The original never spells that out: it reads */
/* the file in one go into td13_rpl_header and writes                     */
/*                                                                       */
/*     mov ax, gameconfig.game_recordedframes                            */
/*     add ax, 724h        ; offset of .rpl kb. event block               */
/*                                                                       */
/* bytes back out (seg005:1992), and trakdata lays td13 (0x1A) ->        */
/* td14_elem_map_main (0x385) -> td15_terr_map_main (0x385) ->            */
/* td16_rpl_buffer out with no gaps (sfdata.c:210). 0x1A + 0x385 + 0x385 */
/* is exactly 0x724, so the single read lands the header in gameconfig,   */
/* the recording's own track in the two maps and its inputs in the replay */
/* buffer. Checked against the shipped DEFAULT.RPL: its bytes 26..1827    */
/* are HELL5.TRK, byte for byte, and 26 + 1802 + 10646 recorded frames is */
/* its 12474-byte length.                                                 */
/*                                                                       */
/* [DEVIATION] restunts' own fileio.c writes sizeof(GAMEINFO) +           */
/* game_recordedframes, i.e. no track at all; that is a decompilation     */
/* slip, and the port had copied it. Recordings this port wrote before    */
/* today are in that shorter form and are still read, by length.          */
/* ------------------------------------------------------------------ */
static SDL_Window* rb_win;
static const stunts_palette_t* rb_pal;
static void far*   rb_font;
/* set when a recording has just been read in through the bar, so the host
 * loop can pick up its length; see the [DEVIATION] where it is read. */
static int rb_reloaded;

/* One token at a time out of a comma-separated environment variable, so a
 * run through the bar can be scripted without a mouse - the same idea as
 * STUNTS_ENDSCREEN_KEYS. Returns NULL once the list is used up. */
static const char* rb_script_pop(const char* var)
{
	static const char* names[4];
	static char bufs[4][256];
	static char* pos[4];
	static int used;
	int i;
	char *tok, *p;
	for (i = 0; i < used; i++) if (!strcmp(names[i], var)) break;
	if (i == used) {
		const char* e = getenv(var);
		if (!e || used == 4) return NULL;
		names[used] = var;
		snprintf(bufs[used], sizeof bufs[used], "%s", e);
		pos[used] = bufs[used];
		used++;
	}
	p = pos[i];
	if (!p || !*p) return NULL;
	tok = p;
	while (*p && *p != ',') p++;
	if (*p) { *p = 0; p++; }
	pos[i] = p;
	return tok;
}

/* STUNTS_REPLAYBAR_KEYS="esc,-,-" pushes one key per frame as a real SDL
 * event, so the strip and its pause menu can be driven without a mouse -
 * the same trick STUNTS_ENDSCREEN_KEYS plays on the results screen. "-" is
 * one frame of nothing and "q" is SDL_QUIT. */
static void rb_key_pump(void)
{
	static int trace = -1;
	const char* tok;
	SDL_Event e;
	/* One key at a time: the strip's own loop reads at most one code per
	 * pass, and this is called from two places at two different rates, so
	 * without the guard a queued key can be overtaken by the next one. */
	if (SDL_HasEvent(SDL_KEYDOWN)) return;
	tok = rb_script_pop("STUNTS_REPLAYBAR_KEYS");
	if (trace < 0) trace = getenv("STUNTS_REPLAYBAR_TRACE") ? 1 : 0;
	if (trace)
		fprintf(stderr, "list: knapp %s\n", tok ? tok : "(slut)");
	if (!tok || !strcmp(tok, "-")) return;
	memset(&e, 0, sizeof e);
	if (!strcmp(tok, "q")) { e.type = SDL_QUIT; SDL_PushEvent(&e); return; }
	e.type = SDL_KEYDOWN;
	e.key.state = SDL_PRESSED;
	e.key.keysym.sym = !strcmp(tok, "esc")   ? SDLK_ESCAPE
	                 : !strcmp(tok, "enter") ? SDLK_RETURN
	                 : !strcmp(tok, "space") ? SDLK_SPACE
	                 : !strcmp(tok, "up")    ? SDLK_UP
	                 : !strcmp(tok, "down")  ? SDLK_DOWN
	                 : !strcmp(tok, "left")  ? SDLK_LEFT
	                 : !strcmp(tok, "right") ? SDLK_RIGHT
	                 : (SDL_Keycode)(unsigned char)tok[0];
	e.key.keysym.scancode = SDL_GetScancodeFromKey(e.key.keysym.sym);
	SDL_PushEvent(&e);
}

/* seg008 show_dialog's modal loop, the half a DOS program gets from its
 * mouse driver. rdialog.c is the other half: the template parser, the
 * layout, the drawing and the key handling, all ported. */
static int16_t rb_run_dialog(const char* text, int16_t mode,
                             const int16_t* enable)
{
	struct RDIALOG d;
	int open;
	if (!rb_present_ren || !rb_present_tex) return -1;
	rs_rgba = frame_rgba;
	font_set_fontdef2(rb_font ? rb_font : fontdefptr);
	if (!rdialog_open(&d, mode, 1, text, -1, -1, (int16_t)dialog_fnt_colour,
	                  enable, 0))
		return -1;
	{   /* one-shot render, so the menu can be looked at without a mouse -
	     * the same hook every other screen in this file has. Answers the
	     * way Escape does, so the bar carries on. */
		const char* shot = getenv("STUNTS_REPLAYMENU_SHOT");
		if (shot) {
			rdialog_draw(&d);
			write_bmp(shot, rb_pal);
			printf("ritade dialogen \"%s\": %d knappar, ruta (%d,%d)-(%d,%d)\n",
			       text, (int)d.var_140, (int)d.var_30, (int)d.var_2C,
			       (int)d.var_2E, (int)d.var_2A);
			rs_rgba = NULL;
			return -1;
		}
	}
	open = 1;
	while (open) {
		SDL_Event ev;
		rdialog_draw(&d);
		replaybar_present();
		while (SDL_PollEvent(&ev)) {
			int16_t key = 0;
			if (ev.type == SDL_QUIT) {
				SDL_Event q = ev;
				SDL_PushEvent(&q);        /* the host's loop wants it too */
				d.result = -1;
				open = 0;
				break;
			}
			if (ev.type == SDL_MOUSEMOTION ||
			    ev.type == SDL_MOUSEBUTTONDOWN) {
				int16_t mx, my;
				menu_point(rb_win,
				           ev.type == SDL_MOUSEMOTION ? ev.motion.x
				                                      : ev.button.x,
				           ev.type == SDL_MOUSEMOTION ? ev.motion.y
				                                      : ev.button.y,
				           &mx, &my);
				open = rdialog_mouse(&d, mx, my,
				                     ev.type == SDL_MOUSEBUTTONDOWN);
				if (!open) break;
				continue;
			}
			if (ev.type != SDL_KEYDOWN) continue;
			switch (ev.key.keysym.sym) {
			case SDLK_ESCAPE:   key = 0x1B;   break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER: key = 0x0D;   break;
			case SDLK_SPACE:    key = 0x20;   break;
			case SDLK_UP:       key = 0x4800; break;
			case SDLK_DOWN:     key = 0x5000; break;
			case SDLK_LEFT:     key = 0x4B00; break;
			case SDLK_RIGHT:    key = 0x4D00; break;
			default:
				if (ev.key.keysym.sym > 0 && ev.key.keysym.sym < 0x80)
					key = (int16_t)ev.key.keysym.sym;
				break;
			}
			if (key == 0) continue;
			open = rdialog_key(&d, key);
			if (!open) break;
		}
		SDL_Delay(10);
	}
	rs_rgba = NULL;
	return d.result;
}

/* The five show_dialog sites loop_game has. "men", "mdo" and "con" are
 * looked up in GAME.RES and "fex" and "ser" in MAIN.RES, which is the
 * original's own split (seg005 pushes gameresptr for the first three and
 * mainresptr for the other two). */
static int16_t rb_dialog_hook(const struct replaybar_dialog* d)
{
	extern void far* gameresptr;
	const char* text;
	const char* scripted;
	int16_t enable[16];
	int i;

	/* STUNTS_REPLAYBAR_DIALOG="5,1" answers one dialog per entry, in the
	 * order they come up, so the whole menu can be driven headlessly. */
	scripted = rb_script_pop("STUNTS_REPLAYBAR_DIALOG");
	if (scripted) {
		int v = atoi(scripted);
		printf("dialog \"%s\" besvarad med %d (skript)\n", d->resid, v);
		return (int16_t)v;
	}

	text = res_text(gameresptr, d->resid);
	if (!*text) text = res_text(mainresptr, d->resid);
	if (!*text) return -1;                    /* as a cancelled dialog */

	/* [ODDITY] seg008:27E4C and :27DA4 both skip a button whose arg_E word
	 * is NON-zero, so the original's array - and rreplaybar.h's `disabled`
	 * - reads "1 means greyed out". rdialog.c has that test the other way
	 * round in all four places, so what it wants is an ENABLE array. The
	 * inversion happens here rather than being papered over: fix rdialog.c
	 * and this line is what has to change with it. */
	if (d->disabled && d->nitems > 0 && d->nitems <= 16) {
		for (i = 0; i < d->nitems; i++)
			enable[i] = d->disabled[i] ? 0 : 1;
		return rb_run_dialog(text, d->kind, enable);
	}
	return rb_run_dialog(text, d->kind, NULL);
}

/* do_fileselect_dialog (seg008 1207..1984), for ".rpl" instead of ".trk" -
 * which is what the original passes too (seg005:24518). */
static int16_t rb_ask_loadname(char* name, int16_t nmax)
{
	const char* scripted = rb_script_pop("STUNTS_REPLAYBAR_LOADNAME");
	if (scripted) {
		snprintf(name, (size_t)nmax, "%s", scripted);
		printf("valde inspelning %s (skript)\n", name);
		return 1;
	}
	if (!rb_win || !rb_present_ren) return 0;
	return run_file_dialog(rb_win, rb_present_ren, rb_present_tex, rb_pal,
	                       rfileio_get_data_dir(), rb_font, ".RPL",
	                       name, (size_t)nmax) ? 1 : 0;
}

/* do_savefile_dialog (seg008 2043..2191). Its template is MAIN.RES's "sav",
 * "Save @]Path:]@                 ]Name:]@]" - a title, the directory and one
 * typed field, which is what this draws. [DEVIATION] show_dialog mode 3 and
 * call_read_line do the typing in the original; SDL_TEXTINPUT does it here,
 * exactly as the high-score name entry above already does. */
static int16_t rb_ask_savename(char* name, int16_t nmax)
{
	char typed[16] = {0};
	int len = 0;
	bool typing = true, ok = false;
	char seg[12][40];
	int nseg = 0;
	const char* tpl;

	{
		const char* scripted = rb_script_pop("STUNTS_REPLAYBAR_SAVENAME");
		if (scripted) {
			snprintf(name, (size_t)nmax, "%s", scripted);
			printf("sparar som %s (skript)\n", name);
			return 1;
		}
	}
	if (!rb_present_ren || !rb_present_tex) return 0;

	tpl = res_text(mainresptr, "sav");
	if (*tpl) nseg = dlg_split(tpl, seg, 12);
	{   /* '@' marks where a field goes; it is not part of the label */
		int k, j;
		for (k = 0; k < nseg; k++) {
			for (j = 0; seg[k][j]; j++)
				if (seg[k][j] == '@') { seg[k][j] = 0; break; }
			while (j > 0 && seg[k][j - 1] == ' ') seg[k][--j] = 0;
		}
	}
	if (nseg < 4) {
		strcpy(seg[0], "Save"); strcpy(seg[1], "Path:");
		strcpy(seg[2], "");     strcpy(seg[3], "Name:");
		nseg = 4;
	}

	rs_rgba = frame_rgba;
	font_set_fontdef2(rb_font ? rb_font : fontdefptr);
	SDL_StartTextInput();
	while (typing) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				SDL_Event q = ev; SDL_PushEvent(&q);
				typing = false;
			} else if (ev.type == SDL_TEXTINPUT && len < 8) {
				char c = ev.text.text[0];
				if (c > 0x20 && c != '/' && c != '\\' && c != '.') {
					typed[len++] = (char)toupper((unsigned char)c);
					typed[len] = 0;
				}
			} else if (ev.type == SDL_KEYDOWN) {
				if (ev.key.keysym.sym == SDLK_BACKSPACE && len)
					typed[--len] = 0;
				else if (ev.key.keysym.sym == SDLK_RETURN ||
				         ev.key.keysym.sym == SDLK_KP_ENTER) {
					ok = len > 0; typing = false;
				} else if (ev.key.keysym.sym == SDLK_ESCAPE) typing = false;
			}
		}
		menu_fill(58, 60, 262, 140, 0);        /* the box */
		menu_fill(59, 61, 261, 139, 8);        /* a one-pixel frame */
		menu_fill(60, 62, 260, 138, 0);
		font_set_colour((uint16_t)dialog_fnt_colour, 0);
		font_draw_text(seg[0], 64, 66);                     /* "Save"  */
		font_draw_text(seg[1], 64, 82);                     /* "Path:" */
		{
			const char* dir = rfileio_get_data_dir();
			const char* leaf = strrchr(dir, '/');
			font_draw_text((char*)(leaf ? leaf + 1 : dir), 110, 82);
		}
		font_draw_text(seg[3], 64, 102);                    /* "Name:" */
		{
			char line[32];
			snprintf(line, sizeof line, "%s_", typed);
			font_draw_text(line, 110, 102);
		}
		font_draw_text("Enter=spara Esc=avbryt", 64, 122);
		{   /* one-shot render, as everywhere else in this file */
			const char* shot = getenv("STUNTS_REPLAYSAVE_SHOT");
			if (shot) {
				write_bmp(shot, rb_pal);
				SDL_StopTextInput();
				rs_rgba = NULL;
				return 0;
			}
		}
		replaybar_present();
		SDL_Delay(16);
	}
	SDL_StopTextInput();
	rs_rgba = NULL;
	if (!ok) return 0;
	snprintf(name, (size_t)nmax, "%s", typed);
	return 1;
}

/* file_build_path + file_find (seg005:2687) - the "is there one already?"
 * that the "fex" warning hangs on. */
static int16_t rb_replay_exists(const char* name)
{
	char p[700];
	FILE* f;
	snprintf(p, sizeof p, "%s/%s.RPL", rfileio_get_data_dir(), name);
	f = fopen(p, "rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

/* ported_file_load_replay_ (seg005:1925). One read into td13_rpl_header;
 * see THE FILE FORMAT above. Returns the original's sense: 0 read it. */
static int16_t rb_load_replay(const char* name)
{
	char p[700];
	FILE* f;
	long len;
	size_t n;
	uint16_t frames;
	snprintf(p, sizeof p, "%s/%s.RPL", rfileio_get_data_dir(), name);
	f = fopen(p, "rb");
	if (!f) { printf("kan inte oppna %s\n", p); return 1; }
	fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
	if (len < (long)sizeof(struct GAMEINFO)) { fclose(f); return 1; }

	/* The header first, so the length in it can say which of the two
	 * layouts this file has before anything is written over the maps. */
	if (fread(td13_rpl_header, 1, sizeof(struct GAMEINFO), f)
	    != sizeof(struct GAMEINFO)) { fclose(f); return 1; }
	memcpy(&gameconfig, td13_rpl_header, sizeof(struct GAMEINFO));
	frames = gameconfig.game_recordedframes;
	if (frames > RPL_MAX) frames = RPL_MAX;

	if (len == (long)(RPL_DATA_OFS + gameconfig.game_recordedframes)) {
		/* The game's own layout: track then inputs, contiguous, which is
		 * exactly what one read into td14 lands in the right places. */
		n = fread(td14_elem_map_main, 1,
		          (size_t)(RPL_DATA_OFS - sizeof(struct GAMEINFO)) + frames, f);
		fclose(f);
		if (n < (size_t)(RPL_DATA_OFS - sizeof(struct GAMEINFO))) return 1;
		n -= (size_t)(RPL_DATA_OFS - sizeof(struct GAMEINFO));
		if (n < frames) gameconfig.game_recordedframes = (uint16_t)n;
	} else {
		/* [DEVIATION] a recording this port wrote before the format was
		 * understood: header + inputs, no track. Its inputs must not go
		 * anywhere near the maps, and the track already loaded is left
		 * alone - which is right, since such a file cannot carry one. */
		printf("varning: %s ar utan bana (%ld byte), laser bara styrningen\n",
		       p, len);
		n = fread(td16_rpl_buffer, 1, frames, f);
		fclose(f);
		if (n < frames) gameconfig.game_recordedframes = (uint16_t)n;
	}
	rb_reloaded = 1;
	printf("laste inspelning %s: bana %.8s, bil %.4s, %u rutor\n", p,
	       gameconfig.game_trackname, gameconfig.game_playercarid,
	       gameconfig.game_recordedframes);
	return 0;
}

/* ported_file_write_replay_ (seg005:1967): gameconfig into td13, then
 * game_recordedframes + 724h bytes straight out of trakdata. Returns the
 * original's sense: 0 wrote it, non-zero puts up "ser". */
static int16_t rb_save_replay(const char* name)
{
	char p[700];
	FILE* f;
	size_t want = (size_t)RPL_DATA_OFS + gameconfig.game_recordedframes;
	size_t n;
	memcpy(td13_rpl_header, &gameconfig, sizeof(struct GAMEINFO));
	snprintf(p, sizeof p, "%s/%s.RPL", rfileio_get_data_dir(), name);
	f = fopen(p, "wb");
	if (!f) { printf("kan inte skriva %s\n", p); return 1; }
	n = fwrite(td13_rpl_header, 1, want, f);
	fclose(f);
	if (n != want) { printf("kort skrivning till %s\n", p); return 1; }
	printf("sparade inspelning %s (%u rutor, %zu byte)\n", p,
	       gameconfig.game_recordedframes, want);
	return 0;
}

/* seg005 loc_2460D: ported_free_player_cars_ + ported_setup_player_cars_,
 * which between them reload the two cars' data and shapes, the cockpit and
 * the horizon. shape3d_load_all() is in there too and is not repeated here:
 * nothing it loads depends on the track or the car. */
static void rb_rebuild_cars(void)
{
	printf("bygger om bilarna for %.4s / %.4s\n",
	       gameconfig.game_playercarid,
	       gameconfig.game_opponenttype ? gameconfig.game_opponentcarid
	                                    : "-");
	if (!setup_player_cars()) return;
	load_car_shapes();
	load_skybox((char)td14_elem_map_main[0x384]);
}

/* [DEVIATION] the placing half of init_game_state; see rreplaybar.h. */
static void rb_place_cars(void) { place_cars_at_start(); }

static void replaybar_install_hooks(SDL_Window* win, SDL_Renderer* ren,
                                    SDL_Texture* tex,
                                    const stunts_palette_t* pal,
                                    void far* dlgfont)
{
	rb_win = win;
	rb_present_ren = ren;
	rb_present_tex = tex;
	rb_pal = pal;
	rb_font = dlgfont;
	replaybar_hook_present       = replaybar_present;
	replaybar_hook_dialog        = rb_dialog_hook;
	replaybar_hook_ask_loadname  = rb_ask_loadname;
	replaybar_hook_ask_savename  = rb_ask_savename;
	replaybar_hook_replay_exists = rb_replay_exists;
	replaybar_hook_load_replay   = rb_load_replay;
	replaybar_hook_save_replay   = rb_save_replay;
	replaybar_hook_rebuild_cars  = rb_rebuild_cars;
	replaybar_hook_place_cars    = rb_place_cars;
}

static bool game_init(const char* data_dir, const char* track, const char* car)
{
	rfileio_set_data_dir(data_dir);
	rblit_init();

	/* restunts.c:1256-1264, the "// Video" block of the original's start-up.
	 * These are not decoration: video_flag3_isFFFF is an all-ones mask that
	 * the strip loop in skybox_op ANDs into every strip's right edge, so
	 * leaving it 0 made every strip zero-width and the near-horizontal
	 * rolled horizon painted nothing at all. video_flag2_is1 selects the
	 * only code path rect_union and rectlist_add_rect accept; the others
	 * fatal_error. */
	video_flag2_is1 = 1;
	video_flag3_isFFFF = -1;
	video_flag5_is0 = 0;
	/* seg031.asm:236, the same "// Video" block. end_hiscore multiplies the
	 * opponent portrait's width by this (seg000:6077), and so do the track
	 * editor's icons; a zero would place the picture at x = 312. */
	video_flag1_is1 = 1;

	for (int i = 0; i < 30; i++) {                 /* init_row_tables */
		trackrows[i] = (int16_t)(30 * (29 - i));
		terrainrows[i] = (int16_t)(30 * i);
		trackpos[i] = (int16_t)((29 - i) << 10);
		trackpos2[i] = (int16_t)(i << 10);
		trackcenterpos[i] = (int16_t)(((29 - i) << 10) + 0x200);
		trackcenterpos2[i] = (int16_t)((i << 10) + 0x200);
		/* The two tables the original's init_row_tables also fills
		 * (restunts.c:244-245). Missing them left elem_zCenter = 0 in
		 * build_track_object, which broke every plane with index >= 4 -
		 * i.e. all slopes, ramps and banked corners. Flat ground never
		 * noticed: planes 0-3 short-circuit before touching the centre. */
		terrainpos[i] = (int16_t)(i << 10);
		terraincenterpos[i] = (int16_t)((i << 10) + 0x200);
	}
	sfdata_init_track_tables();

	/* One trakdata block carved exactly as init_trackdata() does, so every
	 * slice track_setup() writes actually exists. */
	sfdata_init_trackdata();

	{
		static uint8_t el[900], he[900];
		char p[600];
		snprintf(p, sizeof(p), "%s/%s.TRK", data_dir, track);
		if (!stunts_load_track(p, el, he)) {
			fprintf(stderr, "kan inte ladda banan %s\n", p);
			return false;
		}
		memcpy(td14_elem_map_main, el, 900);
		memcpy(td15_terr_map_main, he, 900);

		/* The byte between the two halves of the .TRK names the landscape
		 * for the horizon panorama - restunts.c:801 reads it as
		 * td14_elem_map_main[0x384]. stunts_load_track() copies only the
		 * 900 grid bytes, so fetch it from the file directly. */
		{
			FILE* tf = fopen(p, "rb");
			if (tf) {
				uint8_t sel = 0;
				if (fseek(tf, 900, SEEK_SET) == 0)
					if (fread(&sel, 1, 1, tf) != 1) sel = 0;
				fclose(tf);
				td14_elem_map_main[0x384] = sel;
			}
		}
	}

	/* Build the track path tables. This is what makes loops, penalties and
	 * the opponent's racing line work; without it sub_18D60 and
	 * detect_penalty have nothing to read. It also derives the real start
	 * tile, so the old "search for element 01..03" guess is gone. */
	{
		int16_t err = track_setup();
		if (err != 0) {
			fprintf(stderr, "banan %s går inte att bygga (felkod %d, ruta %d,%d)\n",
			        track, err, byte_45D90, byte_45E16);
			return false;
		}
	}
	int start_row = startrow2, start_col = startcol2;

	{
		char p[600]; stunts_plane_t* planes = NULL; uint16_t n = 0;
		snprintf(p, sizeof(p), "%s/GAME.PRE", data_dir);
		if (stunts_load_collision_data(p, &planes, &n)) {
			planptr = (struct PLANE*)planes; current_planptr = planptr; planindex = 0;
		}
		/* restunts.c:797. The message strings draw_ingame_text prints
		 * live in here too, so it is loaded through the resource path
		 * rather than only scraped for collision data. */
		{
			extern void far* gameresptr;
			gameresptr = file_load_resfile("game");
		}
		{
			/* Wall table - see the identical load in dump_native_states.c. */
			extern int16_t far* wallptr;
			stunts_res_archive_t* wa = stunts_asset_load_archive(p);
			const stunts_sub_resource_t* w =
				wa ? stunts_asset_find_resource(wa, "wall") : NULL;
			if (w && w->data) {
				int16_t* copy = malloc(w->size);
				memcpy(copy, w->data, w->size);
				wallptr = copy;
			}
		}
	}

	memcpy(gameconfig.game_playercarid, car, 4);
	/* The track name is what file_build_path turns into "<track>.HIG" and
	 * what a .RPL header carries; it had never been filled in. */
	snprintf(gameconfig.game_trackname, sizeof gameconfig.game_trackname,
	         "%-8.8s", track);
	gameconfig.game_playertransmission = 1;
	gameconfig.game_opponenttype = (char)opt_opponent;
	/* seg000 loc_12EA2: with no opponent the car id is the 0xFF sentinel,
	 * which is what the shipped DEFAULT.RPL header carries. */
	if (opt_opponent == 0) {
		memset(gameconfig.game_opponentcarid, 0, 4);
		gameconfig.game_opponentcarid[0] = (char)0xFF;
	} else {
		memcpy(gameconfig.game_opponentcarid, opt_opponent_car, 4);
	}
	gameconfig.game_opponenttransmission = 1;
	framespersec = 20;

	if (!setup_player_cars()) return false;

	/* The clock's font. restunts.c:785 loads it the same way; without it
	 * font_draw_text has nothing to draw and the timer stays invisible. */
	{
		extern void far* fontledresptr;
		extern void far* fontdefptr;
		extern void* file_load_resource(int16_t restype, const char* filename);
		/* A .FNT is a plain file, not one of the game's resource archives,
		 * so it must be read raw - file_load_resource() would try to parse
		 * an archive directory that is not there. */
		char fp[600];
		snprintf(fp, sizeof(fp), "%s/FONTLED.FNT", data_dir);
		FILE* ff = fopen(fp, "rb");
		if (ff) {
			fseek(ff, 0, SEEK_END); long fl = ftell(ff); fseek(ff, 0, SEEK_SET);
			uint8_t* fb = malloc((size_t)fl);
			if (fb && fread(fb, 1, (size_t)fl, ff) == (size_t)fl) fontledresptr = fb;
			fclose(ff);
		}
		/* FONTLED is the seven-segment face the lap clock uses and it has
		 * only digits and a colon - letters drawn in it vanish silently.
		 * The default font, which font_set_fontdef() selects and every
		 * message goes through, is FONTDEF. */
		fontdefptr = load_font_raw(data_dir, "FONTDEF.FNT");
		if (!fontdefptr) fontdefptr = load_font_raw(data_dir, "FONTN.FNT");
		if (!fontdefptr) fontdefptr = fontledresptr;
		if (!fontledresptr) fprintf(stderr, "varning: FONTLED.FNT saknas, ingen klocka\n");
	}

	copy_material_list_pointers(material_clrlist_ptr, material_clrlist2_ptr,
	                            material_patlist_ptr, material_patlist2_ptr, 0);
	load_sdgame2_shapes();   /* restunts.c:800 - explosion frames and arrows */
	/* restunts.c:801. This sets the sky, ground, water and needle colours
	 * as well as loading the four horizon strips, exactly as seg003
	 * loc_1D88E does - which is why the four assignments that used to sit
	 * here are gone. */
	load_skybox((char)td14_elem_map_main[0x384]);

	init_polyinfo();
	if (shape3d_load_all() != 0) { fprintf(stderr, "kan inte ladda 3D-former\n"); return false; }
	load_car_shapes();

	/* restunts.c:1031, set_projection(0x23, dashbmp_y_copy / 6, 0x140,
	 * dashbmp_y_copy). The first two arguments are the horizontal and
	 * vertical half-FOV in DEGREES - they define how much of the world is
	 * visible and must NOT scale with resolution. Only the last two, the
	 * pixel dimensions, do. With the dash showing, the view is only
	 * dashbmp_y pixels tall, so the vertical FOV narrows to match. */
	set_projection(0x23, view_bottom() / 6, VIEW_W, view_bottom() * RFB_SCALE);
	cameramode = 0; followOpponentFlag = 0; game_replay_mode = 0;
	dashb_toggle = 1;
	detail_level = 0; run_game_random = 0;

	/* init_game_state's preamble, in the original's order: the rewind ring
	 * and word_45A00 (seg000.asm 436..442, seg001.asm 3873..3875), then the
	 * state flags and the four helicopter vectors (seg001.asm 3885..4021),
	 * and only then the car placement. Before 2026-08-17 the port had only
	 * the car placement, which left state.game_vec1/3/4 - the helicopter
	 * cameras - permanently at the origin. */
	word_45A00 = (int16_t)(30 * framespersec);
	if (!cvxptr) cvxptr = calloc(20, sizeof(struct GAMESTATE));
	init_game_state_vars();

	place_cars_at_start();
	/* 0 means "not driving yet", which is what puts "Fasten your Seatbelt!"
	 * on the screen; the first input flips it to 1. Headless runs start at
	 * 1 so every physics baseline in tests/ stays exactly as recorded. */
	state.game_inputmode = 1;
	state.game_frame = 0;
	printf("start: ruta (%d,%d), vinkel %d, kulle %d, %d banrutor\n",
	       start_col, start_row, track_angle, hillFlag, track_pieces_counter);
	return true;
}

int main(int argc, char** argv)
{
	const char* data_dir = "extracted/stunts/stunts";
	const char* track = "DEFAULT";
	const char* car = "COUN";
	int scale = 4, headless = 0;
	/* Phase 4. Sound is OFF in headless runs unless --dump-audio asks for
	 * it, so every recorded baseline and every oracle dump is unaffected. */
	int opt_nosound = 0, opt_audio_trace = 0, opt_camera = 0;
	const char* opt_dump_audio = NULL;
	/* Off by default: the resolution increase is for the 3D world, which is
	 * genuinely re-rendered, while the 2D artwork stays the original's own
	 * pixels simply made bigger. Measured, this is the right call - Scale2x
	 * gains little on this material and the AI experiments gained less
	 * still, or went backwards on the dithered photographs. --smooth turns
	 * it back on, --import-cockpit loads an external set. */
	rshape2d_smooth = 0;
	const char* replay_path = NULL;
	const char* record_path = NULL;
	int play_windowed = 0;
	const char* shots_dir = NULL;
	int shot_from = 0, shot_step = 1, paint_check = 0, nodash = 0;

	/* Launched from Finder the working directory is "/", so the relative
	 * default cannot resolve. Fall back to the assets inside the .app bundle,
	 * which tools/build_app_bundle.sh copies to Contents/Resources/data. */
	static char bundle_data[PATH_MAX];
	{
		char probe[PATH_MAX];
		snprintf(probe, sizeof(probe), "%s/GAME.PRE", data_dir);
		FILE* f = fopen(probe, "rb");
		if (f) {
			fclose(f);
		} else {
			uint32_t n = sizeof(bundle_data);
			char exe[PATH_MAX];
			if (_NSGetExecutablePath(exe, &n) == 0) {
				char* slash = strrchr(exe, '/');
				if (slash) {
					*slash = 0;
					snprintf(bundle_data, sizeof(bundle_data),
					         "%s/../Resources/data", exe);
					data_dir = bundle_data;
				}
			}
		}
	}

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i+1 < argc) data_dir = argv[++i];
		else if (!strcmp(argv[i], "--track") && i+1 < argc) track = argv[++i];
		else if (!strcmp(argv[i], "--car") && i+1 < argc) car = argv[++i];
		else if (!strcmp(argv[i], "--scale") && i+1 < argc) scale = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--headless") && i+1 < argc) headless = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--replay") && i+1 < argc) replay_path = argv[++i];
		else if (!strcmp(argv[i], "--record") && i+1 < argc) record_path = argv[++i];
		/* --replay alone runs headless (that is what every baseline in
		 * tests/ uses); --play watches the same recording in the window,
		 * which is what the option menu's "Load replay" does. */
		else if (!strcmp(argv[i], "--play") && i+1 < argc) {
			replay_path = argv[++i]; play_windowed = 1;
		}
		else if (!strcmp(argv[i], "--shots") && i+1 < argc) shots_dir = argv[++i];
		else if (!strcmp(argv[i], "--shot-from") && i+1 < argc) shot_from = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--shot-step") && i+1 < argc) shot_step = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--opponent") && i+1 < argc) opt_opponent = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--opponent-car") && i+1 < argc)
			snprintf(opt_opponent_car, sizeof opt_opponent_car, "%s", argv[++i]);
		else if (!strcmp(argv[i], "--nosound")) opt_nosound = 1;
		else if (!strcmp(argv[i], "--nointro")) opt_nointro = 1;
		else if (!strcmp(argv[i], "--intro"))   opt_intro   = 1;
		else if (!strcmp(argv[i], "--dump-audio") && i+1 < argc)
			opt_dump_audio = argv[++i];
		else if (!strcmp(argv[i], "--audio-trace")) opt_audio_trace = 1;
		else if (!strcmp(argv[i], "--paint-check")) paint_check = 1;
		else if (!strcmp(argv[i], "--camera") && i+1 < argc)
			opt_camera = atoi(argv[++i]) & 3;   /* 0 cockpit, 1 helicopter, 2/3 trackside */
		else if (!strcmp(argv[i], "--nodash")) nodash = 1;
		else if (!strcmp(argv[i], "--smooth")) rshape2d_smooth = 1;
		else if (!strcmp(argv[i], "--sharp")) rshape2d_smooth = 0;
		else if (!strcmp(argv[i], "--export-cockpit") && i+1 < argc)
			rshape2d_export_dir = argv[++i];
		else if (!strcmp(argv[i], "--import-cockpit") && i+1 < argc)
			rshape2d_import_dir = argv[++i];
	}

	/* --replay drives the headless run from a recorded .RPL instead of
	 * holding the throttle straight, which is the only way to reach the
	 * parts of a track that need steering. The track and car come from the
	 * recording's own 26-byte GAMEINFO header. */
	uint8_t* replay_inputs = NULL;
	uint16_t replay_len = 0;
	stunts_game_info_t replay_info;
	char replay_track[16], replay_car[8];
	if (replay_path) {
		if (!load_replay_file(replay_path, &replay_info, &replay_inputs, &replay_len)) {
			fprintf(stderr, "kan inte ladda inspelningen %s\n", replay_path);
			return 1;
		}
		snprintf(replay_track, sizeof(replay_track), "%s", replay_info.track_name);
		for (char* q = replay_track + strlen(replay_track) - 1;
		     q >= replay_track && *q == ' '; q--) *q = 0;
		memcpy(replay_car, replay_info.player_car_id, 4);
		replay_car[4] = 0;
		track = replay_track;
		car = replay_car;
		if (!play_windowed && (headless <= 0 || headless > replay_len))
			headless = replay_len;
		printf("inspelning: bana %s, bil %s, %u rutor\n", track, car, replay_len);
	}

	/* Truecolour compositing is armed before game_init, because
	 * setup_car_shapes(0) builds the blended copies while loading. It is
	 * armed unconditionally: the cockpit is drawn after the 3D view has
	 * been converted to RGB, so it has to write there whether or not the
	 * artwork was smoothed. Without a blended copy every pixel is just
	 * pal[index], which is bit-identical to the indexed path. */
	{
		char pp[600];
		stunts_palette_t p0;
		int k;
		snprintf(pp, sizeof(pp), "%s/SDMAIN.PVS", data_dir);
		if (!stunts_palette_load(pp, &p0)) stunts_palette_init_default(&p0);
		for (k = 0; k < 256; k++)
			pal_rgba[k] = 0xFF000000u | ((uint32_t)p0.colors[k].r << 16)
			            | ((uint32_t)p0.colors[k].g << 8) | p0.colors[k].b;
		rs_pal = pal_rgba;
		/* rs_rgba is NOT armed here. It selects the truecolour target,
		 * and only the things drawn *after* the 3D frame has been
		 * converted may use it - the cockpit and the menus. Anything
		 * drawn from inside update_frame, such as the lap clock and the
		 * messages draw_ingame_text prints, is part of the 3D frame and
		 * belongs in the palette-indexed buffer; arming this globally
		 * sent that text into a buffer present() then overwrote, which
		 * is how the clock went missing. */
	}

	stunts_palette_t pal;
	{
		char p[600];
		snprintf(p, sizeof(p), "%s/SDMAIN.PVS", data_dir);
		if (!stunts_palette_load(p, &pal)) stunts_palette_init_default(&pal);
	}

	/* Interactively the main menu comes first, and it needs the resource
	 * loader and the framebuffer but not a track. game_init sets both up
	 * again when a race actually starts; both are idempotent. */
	void far* dlgfont = NULL;
	static char track_buf[32];
	static char car_buf[8];
	SDL_Window* win = NULL;
	SDL_Renderer* ren = NULL;
	/* Phase 10 moved this up from just below game_init. The title song
	 * has to be audible over the intro and the menus, and it is only
	 * audible once the audio device is open; opening it after the menu
	 * is why every screen before the starting line used to be silent.
	 * Nothing here depends on a loaded track. */
	/* Phase 4. mode 0 = nothing runs at all (headless default, --nosound),
	 * 1 = an SDL audio device, 2 = deterministic render to a .wav. */
	{
		int amode = 0;
		if (opt_dump_audio)          amode = 2;
		else if (!opt_nosound && headless <= 0) amode = 1;
		audio_native_init(data_dir, amode, opt_dump_audio);
		audio_native_set_trace(opt_audio_trace);
		/* Phase 8. The sequencer and the OPL core come up with the audio
		 * device; the option menu's "Music on/off" drives
		 * music_native_set_enabled(). Silent in headless runs, where
		 * amode is 0. */
		if (amode != 0) {
			music_native_init(data_dir, 44100);
			printf("musik: OPL-karna %s (akta=%d)\n",
			       music_native_opl_core(), music_native_opl_is_real());
		}
		/* The original calls audio_carstate from inside update_gamestate
		 * (seg001.asm 4494 / 4500), not from the frame loop. */
		sim_hook_audio_frame = audio_native_frame;
	}

	SDL_Texture* tex = NULL;
	if (headless <= 0) {
		rfileio_set_data_dir(data_dir);
		rblit_init();
		if (SDL_Init(SDL_INIT_VIDEO) != 0) {
			fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
			return 1;
		}
		/* Phase 11: the gameport. Opens its own SDL subsystem and is a
		 * no-op with nothing attached, which is the normal case; the
		 * stick still has to be selected in the option menu before
		 * get_joy_flags answers anything (byte_3FE00 bit 0). */
		if (rjoy_init())
			printf("styrspak hittad\n");
		win = SDL_CreateWindow("Stunts — Mac-port (originalmotor)",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			(VIEW_W / RFB_SCALE) * scale,
			(VIEW_H / RFB_SCALE) * scale * 6 / 5,
			SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
		ren = SDL_CreateRenderer(win, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
		tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, VIEW_W, VIEW_H);

		/* The dialog font. FONTLED is the seven-segment face the lap
		 * clock uses; menus and dialogs use FONTDEF. FONTN is the
		 * narrow face seg000 switches to for the high-score table's
		 * four columns and the opponent's profile text. */
		dlgfont = load_font_raw(data_dir, "FONTDEF.FNT");
		if (!dlgfont) dlgfont = load_font_raw(data_dir, "FONTN.FNT");
		fontnptr = load_font_raw(data_dir, "FONTN.FNT");
		if (!fontnptr) fontnptr = dlgfont;
		/* The two language files every dialog in seg000 reads its
		 * wording out of: MAIN.RES for the templates and MISC.PRE for
		 * the button labels. */
		mainresptr = file_load_resfile("main");
		miscptr    = file_load_resfile("misc");

		/* Phase 10: the attract sequence - two stills, the 3D logo
		 * animation and the credits, looping until a key is pressed.
		 * Named rintro_run_looped because externs.h already declares a
		 * zero-argument run_intro_looped that nothing implements.
		 * Skipped for the one-shot render hooks, which must not have to
		 * sit through it. */
		if (opt_intro && !opt_nointro && !getenv("STUNTS_NO_INTRO")
		    && !getenv("STUNTS_MENU_SHOT")) {
			/* macOS hands a freshly created window focus only once the app
			 * is activated; without this the first keypress goes to whatever
			 * was in front and the intro looks unskippable. */
			SDL_RaiseWindow(win);
			rintro_run_looped(win, ren, tex, &pal, data_dir);
		}

		while (!play_windowed) {
			/* One-shot render hooks, one per screen, so each can be
			 * checked without a mouse (as STUNTS_MENU_SHOT does). */
			int pick = getenv("STUNTS_TRACK_SHOT")  ? 3
			         : getenv("STUNTS_CAR_SHOT")    ? 1
			         : getenv("STUNTS_OPP_SHOT")    ? 2
			         : getenv("STUNTS_OPTION_SHOT") ? 4
			         : getenv("STUNTS_GRAPHICS_SHOT") ? 4
			         /* Phase 11: the joystick screen is behind the
			          * option sign, the other three are drawn after
			          * game_init and so want "Let's Drive". */
			         : getenv("STUNTS_JOYCAL_SHOT") ? 4
			         : (getenv("STUNTS_HISCORE_SHOT") ||
			            getenv("STUNTS_HISCORE_TEST") ||
			            getenv("STUNTS_RESULT_SHOT") ||
			            getenv("STUNTS_ENDSCREEN_SHOT") ||
			            getenv("STUNTS_ENDSCREEN_TIME") ||
			            getenv("STUNTS_EDITOR_SHOT") ||
			            getenv("STUNTS_ICONS_SHOT") ||
			            getenv("STUNTS_MAP2D_SHOT") ||
			            getenv("STUNTS_REPLAYBAR_SHOT")) ? 0
			         : run_main_menu(win, ren, tex, &pal);
			if (pick < 0) { SDL_Quit(); return 0; }
			if (pick == 0) break;          /* Let's Drive */
			if (pick == 3 && dlgfont) {    /* the "Track" sign */
				char chosen[32];
				if (run_tracks_dialog(win, ren, tex, &pal, data_dir,
				                      dlgfont, chosen, sizeof chosen)) {
					snprintf(track_buf, sizeof track_buf, "%s", chosen);
					track = track_buf;
					printf("bana vald: %s\n", track);
				}
				continue;
			}
			if (pick == 1 && dlgfont) {    /* the "Car" sign */
				char chosen[8];
				if (run_car_dialog(win, ren, tex, &pal, data_dir,
				                   dlgfont, chosen, sizeof chosen)) {
					snprintf(car_buf, sizeof car_buf, "%s", chosen);
					car = car_buf;
					snprintf(g_player_car, sizeof g_player_car, "%s", chosen);
					printf("bil vald: %s\n", car);
				}
				continue;
			}
			if (pick == 2 && dlgfont) {    /* the red "Opponent" stop sign */
				snprintf(g_player_car, sizeof g_player_car, "%s", car);
				run_opponent_dialog(win, ren, tex, &pal, data_dir, dlgfont);
				printf(opt_opponent ? "motstandare vald: %d, bil %s\n"
				                    : "mot klockan\n",
				       opt_opponent, opt_opponent_car);
				continue;
			}
			if (pick == 4 && dlgfont) {    /* the green "Option" sign */
				static char rp[700];
				if (run_option_dialog(win, ren, tex, &pal, data_dir,
				                      dlgfont, rp, sizeof rp)) {
					replay_path = rp;
					printf("spelar upp %s\n", replay_path);
					break;
				}
				continue;
			}
			printf("den menyn ar inte portad an\n");
		}
	}

	/* run_option_menu's "Load replay" can hand back a recording after the
	 * command line has already been parsed, so the load happens here. */
	if (replay_path && !replay_inputs) {
		if (!load_replay_file(replay_path, &replay_info,
		                      &replay_inputs, &replay_len)) {
			fprintf(stderr, "kan inte ladda inspelningen %s\n", replay_path);
			replay_path = NULL;
		} else {
			snprintf(replay_track, sizeof(replay_track), "%s",
			         replay_info.track_name);
			for (char* q = replay_track + strlen(replay_track) - 1;
			     q >= replay_track && *q == ' '; q--) *q = 0;
			memcpy(replay_car, replay_info.player_car_id, 4);
			replay_car[4] = 0;
			track = replay_track;
			car = replay_car;
			opt_opponent = replay_info.opponent_type;
			memcpy(opt_opponent_car, replay_info.opponent_car_id, 4);
			opt_opponent_car[4] = 0;
			game_replay_mode = 1;
		}
	}

	if (!game_init(data_dir, track, car)) return 1;

	/* Phase 11 test hooks, one per new screen, in the same spirit as the
	 * Phase 7 ones below: draw the screen once into the framebuffer, write
	 * it out and leave, so each can be looked at without a mouse. All four
	 * need game_init to have run - the editor and the 2D map read the track
	 * that is loaded, the icon palette and the recording strip read
	 * SDTEDIT.PES and SDGAME.PVS through the resource loader. */
	if (getenv("STUNTS_MAP2D_SHOT")) {
		/* seg009 draw_2DtrackMap. STUNTS_MAP2D_COL / _ROW scroll it. */
		extern int trackmap2d_shot(const char* path);
		return trackmap2d_shot(getenv("STUNTS_MAP2D_SHOT")) > 0 ? 0 : 1;
	}
	if (getenv("STUNTS_ICONS_SHOT"))       /* seg009 preRender_icons */
		return editor_icons_shot(&pal) > 0 ? 0 : 1;
	if (reditor_shot(pal.colors)) return 0;   /* seg009, the whole editor */
	if (replaybar_shot(NULL)) return 0;       /* seg005 loop_game's strip */

	/* Phase 7 test hook: draw_track_preview() renders the whole track from a
	 * fixed camera. game_init has already loaded the maps, the shapes and the
	 * horizon, which is everything seg000:1780 sets up before calling it -
	 * apart from the projection, which the preview wants at its own size. */
	/* Phase 9 test hook: STUNTS_REWIND=<frame> runs to that frame, records
	 * the state, runs on, rewinds with restore_gamestate and reports whether
	 * the restored state is byte-identical to what was there before. That is
	 * the whole claim rewinding has to make. */
	if (getenv("STUNTS_REWIND")) {
		extern void restore_gamestate(int16_t frame);
		int target = atoi(getenv("STUNTS_REWIND"));
		static uint8_t snap[0x460];
		int f, diff = 0, i;
		for (f = 0; f < target; f++) update_gamestate();
		memcpy(snap, &state, sizeof snap);
		for (f = 0; f < 120; f++) update_gamestate();
		restore_gamestate((int16_t)target);
		for (i = 0; i < (int)sizeof snap; i++)
			if (((uint8_t*)&state)[i] != snap[i]) diff++;
		printf("spolning till ruta %d: %d av %d byte skiljer, ruta nu %d\n",
		       target, diff, (int)sizeof snap, state.game_frame);
		return diff == 0 ? 0 : 1;
	}

	if (getenv("STUNTS_PREVIEW_SHOT")) {
		extern void draw_track_preview(void);
		extern int16_t skybox_grd_color;
		int i;
		set_projection(0x28, 0x28, VIEW_W, VIEW_H);
		sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
		sprite_clear_1_color((uint8_t)skybox_grd_color);
		draw_track_preview();
		for (i = 0; i < VIEW_W * VIEW_H; i++) {
			stunts_color_rgba_t c = pal.colors[rfb_pixels[i]];
			frame_rgba[i] = 0xFF000000u | ((uint32_t)c.r << 16)
			              | ((uint32_t)c.g << 8) | c.b;
		}
		write_bmp(getenv("STUNTS_PREVIEW_SHOT"), &pal);
		printf("banforhandsvisning skriven\n");
		return 0;
	}

	/* Phase 7 test hook: the car picker's showroom. game_init has already put
	 * the chosen car's "car0" in game3dshapes[124], which is the slot the
	 * turntable draws, so all that is left is the projection and the angle.
	 * STUNTS_SHOWROOM_SHOT=<dir> writes eight frames plus the graph. */
	if (getenv("STUNTS_SHOWROOM_SHOT")) {
		extern void carmenu_set_projection(void);
		extern void carmenu_draw_turntable(int16_t rotation, uint8_t* material);
		extern int  carmenu_draw_accel_graph(void);
		const char* dir = getenv("STUNTS_SHOWROOM_SHOT");
		uint8_t mat = 0;
		int k, i, pts = 0;
		carmenu_set_projection();
		for (k = 0; k < 8; k++) {
			char p[700];
			sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
			sprite_clear_1_color(0);
			carmenu_draw_turntable((int16_t)(k * (0x400 / 8)), &mat);
			if (k == 7) pts = carmenu_draw_accel_graph();
			for (i = 0; i < VIEW_W * VIEW_H; i++) {
				stunts_color_rgba_t c = pal.colors[rfb_pixels[i]];
				frame_rgba[i] = 0xFF000000u | ((uint32_t)c.r << 16)
				              | ((uint32_t)c.g << 8) | c.b;
			}
			snprintf(p, sizeof p, "%s/turn%d.bmp", dir, k);
			write_bmp(p, &pal);
		}
		printf("visningsrum skrivet, grafen gav %d punkter\n", pts);
		return 0;
	}
	if (headless <= 0) state.game_inputmode = 0;   /* waiting at the line */
	cameramode = (char)opt_camera;   /* test hook: which camera to render from */
	/* update_gamestate reads its input byte straight out of td16_rpl_buffer
	 * at the current game_frame, which is where file_load_replay puts it. */
	if (replay_inputs)
		memcpy(td16_rpl_buffer, replay_inputs,
		       replay_len < RPL_MAX ? replay_len : RPL_MAX);

	/* STUNTS_DETAIL=<0..4>: the graphics menu's detail_level, from the
	 * command line, so the five levels can be rendered and compared. */
	{ const char* d = getenv("STUNTS_DETAIL");
	  if (d) { int v = atoi(d); detail_level = (uint8_t)(v < 0 ? 0 : v > 4 ? 4 : v); } }
	if (nodash) dashb_toggle = 0;   /* full-height view, for scaling tests */

	/* The visible area, in render pixels - must follow the framebuffer. */
	/* rect_windshield: the 3D view is only the glass between the roof
	 * strip and the dash panel (restunts.c:1032). */
	struct RECTANGLE windshield = { 0, VIEW_W,
	                                view_top() * RFB_SCALE,
	                                view_bottom() * RFB_SCALE };

	if (headless > 0) {
		/* Throttle held straight, or the recorded inputs if --replay. */
		for (int f = 1; f <= headless; f++) {
			/* Without a recording the throttle is held; the byte goes
			 * into td16_rpl_buffer because that is where
			 * update_gamestate reads it from. --record then writes the
			 * same buffer out as a .RPL, so every recorded baseline is
			 * byte-for-byte what it was. */
			if (!replay_inputs && state.game_frame < RPL_MAX)
				td16_rpl_buffer[state.game_frame] = (char)1;
			if (record_path && gameconfig.game_recordedframes < RPL_MAX)
				gameconfig.game_recordedframes++;
			/* Was: player_op(in); opponent_op(); game_frame++;
			 * audio_native_frame(). All four are update_gamestate's job,
			 * and calling them by hand meant move_helicopters never ran. */
			update_gamestate();
			elapsed_time2 = state.game_frame;   /* restunts.c:616 */
			/* Every pixel of the windshield must be painted by the
			 * background fill or by geometry drawn over it - the
			 * renderer has no clear-screen step, so anything the
			 * horizon quads fail to cover keeps the previous frame.
			 * Pre-fill with a colour the scene cannot produce and see
			 * whether any of it survives. */
			if (paint_check)
				memset(rfb_pixels, PAINT_MARKER,
				       (size_t)VIEW_W * VIEW_H);
			{   /* test hook: force the crash bitmap so the cracked
			     * windscreen and the sinking effect can be checked
			     * without arranging a real collision */
				const char* cb = getenv("STUNTS_CRASH");
				const char* ar = getenv("STUNTS_ARROW");
				if (cb) state.playerstate.car_crashBmpFlag = (char)atoi(cb);
				if (ar) state.field_45D = (char)atoi(ar);
				/* STUNTS_GEAR=<n>: hold the gear gate open on gear n, which
				 * the game otherwise shows only during the half-second of a
				 * shift. knobpoints comes from the car's own data. */
				{
					const char* gr = getenv("STUNTS_GEAR");
					if (gr) {
						int g = atoi(gr);
						if (g < 0) g = 0;
						if (g > 5) g = 5;   /* [6] is unused padding */
						state.playerstate.car_changing_gear = 1;
						state.playerstate.car_knob_x =
							simd_player.knob_points[g].px;
						state.playerstate.car_knob_y =
							simd_player.knob_points[g].py;
					}
				}
			}
			apply_view(&windshield);
			update_frame(1, &windshield);
			{   /* test hook: draw an explosion frame directly, since
			     * frame.c only fires one from a car's own crash state */
				const char* ex = getenv("STUNTS_EXPLODE");
				if (ex) {
					extern void far* sdgame2shapes[5];
					extern void shape_op_explosion(int16_t, void far*, int16_t, int16_t);
					sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
					shape_op_explosion((int16_t)atoi(ex),
						sdgame2shapes[(state.game_frame >> 2) % 3],
						VIEW_W / 2, VIEW_H / 3);
				}
			}
			if (paint_check) {
				long left = 0;
				for (int y = windshield.top; y < windshield.bottom; y++)
					for (int x = 0; x < VIEW_W; x++)
						if (rfb_pixels[y * VIEW_W + x] == PAINT_MARKER) left++;
				if (left) {
					paint_bad++;
					if (paint_bad <= 5)
						printf("OMÅLAT ruta %d: %ld pixlar, rull=%d\n",
						       f, left, state.playerstate.car_rotate.z);
				}
			}
			present(&pal);
			if (shots_dir && f >= shot_from && (f - shot_from) % shot_step == 0) {
				char sp[700];
				snprintf(sp, sizeof(sp), "%s/f%05d.bmp", shots_dir, f);
				write_bmp(sp, &pal);
			}
			if (f % (headless / 30 ? headless / 30 : 1) == 0)
				printf("ruta %4d: ruta(%2d,%2d) Y=%6ld rot=(%4d,%4d,%4d) mark=%4d plan=%3d modell=0x%02X fart=%5u\n",
				       f, dbg_col, dbg_row,
				       (long)state.playerstate.car_posWorld1.ly,
				       state.playerstate.car_rotate.x, state.playerstate.car_rotate.y,
				       state.playerstate.car_rotate.z,
				       terrainHeight, planindex, dbg_model & 0xFF,
				       state.playerstate.car_speed2);
			/* The opponent's own trace, so its progress along the
			 * racing line can be read without a window. */
			if (gameconfig.game_opponenttype != 0 &&
			    f % (headless / 30 ? headless / 30 : 1) == 0)
				printf("        motst: X=%7ld Z=%7ld rotY=%5d styr=%5d fart=%5u wp=%3d mal=%3u varv=%d\n",
				       (long)(state.opponentstate.car_posWorld1.lx >> 6),
				       (long)(state.opponentstate.car_posWorld1.lz >> 6),
				       state.opponentstate.car_rotate.y,
				       state.opponentstate.car_steeringAngle,
				       state.opponentstate.car_speed2,
				       state.opponentstate.car_trackdata3_index,
				       (unsigned)(uint8_t)state.field_3F9,
				       (int)(uint8_t)state.opponentstate.field_CD);
		}
		if (paint_check)
			printf("måltest: %ld av %d rutor hade omålade pixlar\n",
			       paint_bad, headless);
		printf("attrapper: ");
		for (int i = 0; i < 8; i++) if (sfstub_hits[i]) printf("[%d]=%d ", i, sfstub_hits[i]);
		printf("\n");
		if (record_path) {
			gameconfig.game_framespersec = framespersec;
			if (file_write_replay(record_path))
				printf("sparade inspelning %s (%u rutor)\n",
				       record_path, gameconfig.game_recordedframes);
			else
				printf("kunde inte spara %s\n", record_path);
		}
		audio_native_shutdown();
		/* dump the last rendered frame for inspection */
		write_bmp("tests/render_native/native_drive.bmp", &pal);
		printf("sparade tests/render_native/native_drive.bmp\n");
		return 0;
	}

	/* The window, renderer and texture were made before the menu. The 6/5
	 * there is the original's pixel aspect: DOS mode 13h pixels were taller
	 * than they were wide, so 320x200 filled a 4:3 screen. */

	/* Test hooks for the two post-race screens: pretend the line has just
	 * been crossed, so they can be rendered once and inspected. */
	/* STUNTS_HISCORE_TEST="<frames>:<name>" runs the whole record path -
	 * load, rank, insert, name, write, reload - and prints the table, so
	 * it can be checked with numbers rather than by hand. */
	if (getenv("STUNTS_HISCORE_TEST")) {
		char spec[64], *colon;
		int16_t r;
		snprintf(spec, sizeof spec, "%s", getenv("STUNTS_HISCORE_TEST"));
		colon = strchr(spec, ':');
		if (colon) *colon++ = 0; else colon = (char*)"TEST";
		if (highscore_write_a(0) != 0 && highscore_write_a(1) != 0) {
			printf("hig: kan varken lasa eller skapa tabellen\n");
			return 1;
		}
		r = highscore_would_enter((int16_t)atoi(spec));
		printf("hig: tid %s -> plats %d\n", spec, r);
		if (r >= 0) {
			highscore_insert(r, "Lamborghini Countach",
			                 opt_opponent != 0 ? 1 : 0, "XX/COUN");
			highscore_set_name(colon);
			highscore_write_b();
		}
		highscore_write_a(0);
		{
			uint8_t off[4]; char b[256]; int i;
			for (i = 0; i < 7; i++) {
				print_highscore_entry((int16_t)i, off, b, sizeof b);
				printf("  %d | %-17s | %-22s | %-8s | %s\n", i,
				       b + off[0], b + off[1], b + off[2], b + off[3]);
			}
		}
		return 0;
	}

	if (getenv("STUNTS_HISCORE_SHOT")) {
		if (highscore_write_a(0) != 0) highscore_write_a(1);
		run_highscore_screen(win, ren, tex, &pal, dlgfont);
		SDL_Quit();
		return 0;
	}
	/* Phase 6's one-shot hook. statecrs.c's update_crash_state is what
	 * normally fills the gState_* copy the results screen reads, so a
	 * synthetic finish is exactly those ten numbers:
	 *   STUNTS_ENDSCREEN_SHOT   = the .bmp to write
	 *   STUNTS_ENDSCREEN_TIME   = total finish time in frames  (0 = DNF)
	 *   STUNTS_ENDSCREEN_OPP    = the opponent's time          (0 = its DNF)
	 *   STUNTS_ENDSCREEN_PEN / _TOP / _JUMP / _IMP / _DIST
	 *   STUNTS_ENDSCREEN_PAGE   = 0 the evaluation, 1 the table
	 *   STUNTS_ENDSCREEN_FRAME  = which portrait frame to draw
	 * STUNTS_RESULT_SHOT / _TIME are the Phase 3 names, still honoured. */
	if ((getenv("STUNTS_ENDSCREEN_SHOT") || getenv("STUNTS_RESULT_SHOT") ||
	     getenv("STUNTS_ENDSCREEN_TIME")) && !play_windowed) {
		#define ENVI(n, d) (getenv(n) ? (int16_t)atoi(getenv(n)) : (int16_t)(d))
		int16_t t = getenv("STUNTS_ENDSCREEN_TIME")
		          ? (int16_t)atoi(getenv("STUNTS_ENDSCREEN_TIME"))
		          : ENVI("STUNTS_RESULT_SHOT_TIME", 1400);
		state.game_total_finish   = t;
		gState_total_finish_time  = t;
		gState_penalty            = ENVI("STUNTS_ENDSCREEN_PEN", 40);
		gState_144                = ENVI("STUNTS_ENDSCREEN_OPP", 0);
		gState_topSpeed           = (int16_t)(ENVI("STUNTS_ENDSCREEN_TOP", 148) << 8);
		gState_impactSpeed        = (int16_t)(ENVI("STUNTS_ENDSCREEN_IMP", 0) << 8);
		gState_jumpCount          = ENVI("STUNTS_ENDSCREEN_JUMP", 3);
		gState_pEndFrame          = t ? t : 1200;
		gState_oEndFrame          = gState_pEndFrame;
		gState_travDist           = (int32_t)ENVI("STUNTS_ENDSCREEN_DIST", 4000)
		                          * 1000;
		gState_frame              = (uint16_t)gState_pEndFrame;
		{
			int16_t pick = run_result_screen(win, ren, tex, &pal, data_dir,
			                                 dlgfont, track, car);
			printf("resultatskarm gav %d\n", (int)pick);
		}
		SDL_Quit();
		return 0;
		#undef ENVI
	}

	bool quit = false;
	uint32_t last = SDL_GetTicks(), accum = 0;
	/* Recording. The original keeps the inputs in trakdata's own
	 * td16_rpl_buffer and counts them in gameconfig.game_recordedframes,
	 * which is the 26-byte header's last field; file_write_replay then
	 * writes header + that many bytes. Same here. */
	bool paused = false;
	gameconfig.game_framespersec = framespersec;
	if (!replay_inputs) gameconfig.game_recordedframes = 0;

	/* Phase 11: the recording strip, seg005 loop_game. The original raises
	 * it on the replay screen and nowhere else - replaybar_view_height()
	 * asks for game_replay_mode == 2 - and watching a loaded recording is
	 * this host's replay screen, so that is where it goes. The call
	 * sequence is ported_run_game_'s own (seg005 74..889):
	 *
	 *   loop_game(0, 0, 0)   load the 23 shapes out of SDGAME.PVS and fall
	 *                        through into mode 2 with arg_2 = 4
	 *   loop_game(2, n, 0)   latch one transport button
	 *   is_in_replay         the strip's PAUSED flag, not its running one:
	 *                        loc_24C5A (play) clears it, loc_24C74 (stop)
	 *                        sets it, and statecrs.c reads it that way too.
	 *
	 * ported_run_game_ has two entries into game_replay_mode 2 and they
	 * differ in exactly that flag:
	 *   loc_21BCA  the attract demo - loads a recording and watches it,
	 *              is_in_replay left at 0 from :98, so it plays at once;
	 *   loc_21C00  the results screen's "watch the recording you just
	 *              drove" - is_in_replay = 1, so it opens paused.
	 * --replay <file> is the command line asking to watch a recording now,
	 * which is the first of those, so the Play button is the one latched
	 * (loc_24C5A latches 3) and the strip's Stop button pauses it. The
	 * results screen is not wired, so the second entry has no caller here.
	 *
	 * The strip needs the recording's length for its scrub trough; the
	 * .RPL header field lands in replay_len, not in gameconfig. */
	if (replay_inputs) {
		game_replay_mode = 2;               /* seg005:498's third test */
		gameconfig.game_recordedframes =
			(uint16_t)(replay_len < RPL_MAX ? replay_len : RPL_MAX);
		elapsed_time1 = 0;
		elapsed_time2 = state.game_frame;
		replaybar_install_hooks(win, ren, tex, &pal, dlgfont);
		/* the 3D view has just shrunk to rows 0..0x96; set_projection
		 * and the windshield rectangle both have to follow it */
		apply_view(&windshield);
		loop_game(0, 0, 0);
		loop_game(2, 3, 0);
		is_in_replay = 0;
		printf("inspelningslist: %u rutor, blaupptagning = spolningslage\n",
		       gameconfig.game_recordedframes);
	}

	while (!quit) {
		SDL_Event ev;
		/* [DEVIATION] While the strip is up, loop_game owns the keyboard:
		 * it is the original's own input handler for the replay screen and
		 * every key this loop knows has a control on the strip - Escape
		 * raises the pause menu (loc_24346), 'c' is the camera button,
		 * Stop is the pause and the two scrub buttons are the rewind. So
		 * the keys are set aside here and put back once the queue has been
		 * drained, rather than acted on: Escape would otherwise quit the
		 * race instead of opening the menu. SDL_QUIT still ends the run.
		 *
		 * They are put back AFTER the drain, not as they arrive. SDL 2.0.18
		 * ends each pump with a POLL SENTINEL and SDL_PollEvent answers 0
		 * when it reaches one; leaving the loop early - which pushing a
		 * fresh event and breaking would do - leaves that sentinel in place
		 * and the NEXT call answers "nothing queued" whatever is really
		 * there. Measured: with the early break, the quit event that
		 * followed a keypress went unseen for the rest of the run.
		 * rreplaybar.c's input_checking has the same note. */
		SDL_Event held[16];
		int nheld = 0, hi;
		if (game_replay_mode == 2) rb_key_pump();
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) quit = true;
			else if (ev.type == SDL_KEYDOWN && game_replay_mode == 2) {
				if (nheld < 16) held[nheld++] = ev;
			}
			else if (ev.type == SDL_KEYDOWN) {
				switch (ev.key.keysym.sym) {
					case SDLK_ESCAPE: quit = true; break;
					case SDLK_c: cameramode = (char)((cameramode + 1) & 3); break;
					/* Phase 9. loop_game is the recording bar; its two
					 * controls that matter mid-race are pause and rewind,
					 * and both are here rather than in a drawn strip.
					 * Rewind is restore_gamestate, which puts back one of
					 * the twenty 1120-byte snapshots update_gamestate
					 * writes every word_45A00 frames - seed included, so
					 * the simulation carries on identically. */
					case SDLK_p: paused = !paused; break;
					case SDLK_BACKSPACE: {
						extern void restore_gamestate(int16_t frame);
						int16_t back = (int16_t)(state.game_frame - word_45A00);
						if (back < 0) back = 0;
						restore_gamestate(back);
						elapsed_time2 = state.game_frame;
						accum = 0;
						break;
					}
					default: break;
				}
			}
		}
		for (hi = 0; hi < nheld; hi++) SDL_PushEvent(&held[hi]);

		/* Original replay input bitfield: bit0 accel, bit1 brake,
		 * bits2-3 steer (01 right, 10 left), bit4 down, bit5 up. */
		const Uint8* k = SDL_GetKeyboardState(NULL);
		char input = 0;
		if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) input |= 0x01;
		if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) input |= 0x02;
		if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) input |= 0x04;
		else if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) input |= 0x08;
		if (k[SDL_SCANCODE_LSHIFT]) input |= 0x10;
		if (k[SDL_SCANCODE_SPACE])  input |= 0x20;
		/* Phase 11: the stick, ORed into the same byte before it is
		 * recorded - seg005 1319..1352 reads it at exactly this point,
		 * and the steering servo (replay_unk) needs the car's current
		 * steering angle, which is this frame's. Untouched with no
		 * device open or the stick not selected in the option menu. */
		input = rjoy_or_input(input);
		/* the flag drops on the first thing the driver does */
		if (input != 0 && state.game_inputmode == 0) state.game_inputmode = 1;

		uint32_t now = SDL_GetTicks();
		accum += now - last;
		last = now;
		if (accum > 250) accum = 250;
		/* The world stops; the frame still draws. On the replay screen
		 * the strip owns that decision: is_in_replay non-zero is its
		 * paused state (loc_24C74 sets it, loc_24C5A clears it), and
		 * statecrs.c reads the same flag. */
		if (paused || is_in_replay) accum = 0;
		while (accum >= 50) {                 /* 20 Hz, as the original */
			if (replay_inputs) {
				if (state.game_frame >= replay_len) { quit = true; break; }
				state.game_inputmode = 1;
			} else if (state.game_frame < RPL_MAX) {
				td16_rpl_buffer[state.game_frame] = input;
				if (gameconfig.game_recordedframes < RPL_MAX)
					gameconfig.game_recordedframes++;
			}
			/* Was: player_op/opponent_op/game_frame++/audio_native_frame
			 * inline. update_gamestate does all of it, in the original's
			 * order, and adds move_helicopters. */
			update_gamestate();
			elapsed_time2 = state.game_frame;   /* restunts.c:616 */
			accum -= 50;
		}

		/* statecrs.c:243 sets game_total_finish the frame the line is
		 * crossed; that is the only finish signal the original has, and
		 * it is what ends the race here. */
		if (state.game_total_finish != 0) quit = true;

		update_frame(1, &windshield);
		present(&pal);
		/* The strip goes on last, over the cockpit.
		 *
		 * seg005 loc_21FF6 leaves the cockpit switched on while the
		 * strip is up (byte_449E2 = 1, roofbmpheight_copy and
		 * dashbmp_y_copy both taken) and simply draws the strip over
		 * it afterwards - the strip is a full 320x49 band at y = 0x97
		 * and the dash starts there, so nothing of the dash survives
		 * while the roof does. This port composites the cockpit in
		 * truecolour after the indexed frame has been converted, so
		 * the strip has to be drawn the same way round or the cockpit
		 * would cover it; rs_rgba is what selects that target, exactly
		 * as present() does for the cockpit itself.
		 *
		 * Mode 3 is one pass of the interactive loop: poll, hit-test,
		 * act, and it calls loop_game(1, ...) itself to redraw. Two
		 * things around it are this host's and not the original's:
		 * sprite1 is opened to the whole screen because update_frame
		 * leaves it clipped to the windshield and the strip lives
		 * below that (seg005 loc_21FC2 does the same before its own
		 * calls), and the invalidate says "the strip's rows have just
		 * been overwritten", which in the original can never happen. */
		if (game_replay_mode == 2) {
			sprite_set_1_size(0, VIEW_W, 0, VIEW_H);
			replaybar_invalidate();
			rs_rgba = frame_rgba;
			loop_game(3, 0, 0);
			rs_rgba = NULL;
			/* seg005 loc_221C2: byte_449DA == 2 is how the pause menu's
			 * "Return to Evaluation" and "Main Menu" (loc_24748 and
			 * loc_24760) tell the race loop to stop. */
			if (byte_449DA == 2) quit = true;
		}
		/* [DEVIATION] loc_2450A has just read another recording in, and
		 * this loop keeps two things the original keeps in globals: how
		 * many frames there are to play (the original reads
		 * game_recordedframes, which the header has just refreshed) and
		 * the buffer the inputs came from (they are already in
		 * td16_rpl_buffer, where update_gamestate reads them, so the
		 * pointer only has to stay non-NULL). loc_2458B also cleared
		 * dashb_toggle, which changes how tall the 3D view is. */
		if (rb_reloaded) {
			rb_reloaded = 0;
			replay_len = gameconfig.game_recordedframes;
			if (!replay_inputs) replay_inputs = (uint8_t*)td16_rpl_buffer;
			elapsed_time1 = 0;
			elapsed_time2 = state.game_frame;
			accum = 0;
			apply_view(&windshield);
			printf("spelar upp den inlasta inspelningen: %u rutor\n",
			       replay_len);
		}
		/* --shots works here too, so the replay screen can be looked at
		 * without a mouse (the headless loop below has had it since
		 * Phase 3). */
		if (shots_dir && state.game_frame >= shot_from
		    && (state.game_frame - shot_from) % shot_step == 0) {
			char sp[700];
			snprintf(sp, sizeof(sp), "%s/f%05d.bmp", shots_dir,
			         (int)state.game_frame);
			write_bmp(sp, &pal);
		}
		SDL_UpdateTexture(tex, NULL, frame_rgba, VIEW_W * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(1);
	}

	/* The race is over - either the line was crossed or the driver quit.
	 * Either way the results screen is where the time is scored and the
	 * recording can be kept.  seg000:104C0 acts on its return value; the
	 * two branches that need a fresh race are Phase 9's job (run_game is
	 * called again there), so what this host does with 0 and 1 is print
	 * them and leave.  [Not done - see docs/FAITHFUL_RENDERER.md] */
	if (dlgfont) {
		int16_t pick = run_result_screen(win, ren, tex, &pal, data_dir,
		                                 dlgfont, track, car);
		static const char* what[3] = { "visa inspelning", "kor igen",
		                               "huvudmeny" };
		if (pick >= 0 && pick <= 2) printf("resultatskarm: %s\n", what[pick]);
	}
	music_native_stop();

	audio_native_shutdown();
	/* the hook points at a function that is about to lose its renderer */
	replaybar_hook_present = NULL;
	rb_present_ren = NULL;
	rb_present_tex = NULL;
	rjoy_shutdown();
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
