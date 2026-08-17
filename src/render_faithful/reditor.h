#ifndef RESTUNTS_REDITOR_H
#define RESTUNTS_REDITOR_H

#include <stdint.h>

/*
 * reditor.h - the track editor, seg009 load_tracks_menu_shapes (54..2953).
 *
 * The name in the disassembly is a lie: the routine loads its shapes in its
 * first 320 lines and then IS the editor for the remaining 2500 - the map,
 * the palette, the cursor, every key, every dialog and the .TRK writer.
 *
 * Split the way rendscreen.c splits end_hiscore: open / draw / hittest /
 * key / activate, so an SDL host drives the loop.  See reditor.c for
 * provenance and deviations.
 *
 * A track is 1802 bytes and this screen edits exactly that:
 *      [   0 ..  899]  element codes, 30x30, addressed through trackrows[]
 *      [ 900        ]  the landscape (horizon) byte, td14[0x384]
 *      [ 901 .. 1800]  terrain codes, 30x30, through terrainrows[]
 *      [1801        ]  one pad byte
 * In this port td14_elem_map_main and td15_terr_map_main are the two halves
 * of one contiguous block (sfdata_init_trackdata), which is why the load
 * and save below are a single 1802-byte read and write, as they are in the
 * original.
 */

#define RED_CACHE   0x84    /* 11 rows x 12 columns of visible map tiles */

struct REDITOR {
	/* --- the resources, under the original's dseg names --- */
	void* sdtedit;                  /* SDTEDIT.PES, expanded on lookup  */
	void* tedit;                    /* TEDIT.PRE                        */
	const uint8_t* pboxshape;       /* 11 pages x 6 rows x 6 columns    */
	const uint8_t* snam;            /* 186 x 4-char icon-fill names     */
	const uint8_t* mnam;            /* 186 x 4-char icon-mask names     */
	const uint8_t* tnam;            /* 3-char text key per element      */
	void* tracksmenushapes1[19];    /* flat lake lak1..gou8 - terrain   */
	void* tracksmenushapes2[4];     /* crs0..crs3 - cursor backgrounds  */
	void* tracksmenushapes3[4];     /* ucr0..ucr3 - cursor outlines     */
	void* tracksmenushape2dunk[186];  /* snam -> the icon fillings      */
	void* tracksmenushape2dunk2[186]; /* mnam -> the icon masks         */

	/* --- the editor's state, under the disassembly's own names --- */
	uint8_t  var_18E;               /* map cursor column                */
	uint8_t  var_180;               /* map cursor row                   */
	uint8_t  var_8;                 /* map scroll column                */
	uint8_t  var_18C;               /* map scroll row                   */
	uint8_t  var_18D;               /* palette cursor column            */
	uint8_t  var_17F;               /* palette cursor row (6..9 = the
	                                 * page bar and the five buttons)   */
	uint8_t  var_C6;                /* palette page, 0 = terrain        */
	uint8_t  var_190;               /* the element/terrain being placed */
	uint8_t  var_34;                /* 0 = the map has focus, 1 = the
	                                 * palette                          */
	uint8_t  var_C4;                /* cursor shape 0..3                */
	uint8_t  var_14, var_6;         /* cursor size in tiles             */
	uint8_t  var_182;               /* element under the cursor         */
	uint8_t  var_D4;                /* the track has unsaved changes    */
	uint8_t  var_188;               /* 1 while the editor is running    */
	uint8_t  var_12;                /* pending track_setup verdict      */
	uint8_t  var_192;               /* what the last edit overwrote     */
	uint8_t  var_DA, var_C0;        /* and where, for the toggle        */
	uint8_t  var_30;                /* revalidate before the next draw  */
	int16_t  var_CA, var_DC;        /* cursor rectangle, x / y          */
	int16_t  var_2E, var_1A;        /* cursor rectangle, w / h          */
	int16_t  var_38;               /* width of the last name drawn      */
	int16_t  blink;                 /* the cursor's on/off phase        */

	/* draw_2DtrackMap's two 132-byte caches, var_162 and var_BE. */
	uint8_t  var_162[RED_CACHE];
	uint8_t  var_BE[RED_CACHE];
};

/* seg009 132..375 - load SDTEDIT.PES and TEDIT.PRE, resolve pbox/snam/
 * mnam/tnam and all 186 icon pairs, then the initial state at loc_2A50D.
 * Returns 0 if a resource is missing. */
int  reditor_open(struct REDITOR* ed);
void reditor_close(struct REDITOR* ed);

/* One complete picture of the editor into the framebuffer. */
void reditor_draw(struct REDITOR* ed);

/* seg009 loc_2A7A0..loc_2A851 - keep the cursor inside the visible window
 * by moving the scroll, and loc_2AB1C..loc_2AD2B - the cursor rectangle and
 * the element whose name the bottom line shows. */
void reditor_update(struct REDITOR* ed);

/* seg009 loc_2B382..loc_2BE3A - one key.  `key` is the game's own code:
 * an ASCII byte, 0x0D/0x20/0x1B, a scancode<<8 for the arrows, or
 * 0x3B00..0x4400 for F1..F10.  Returns 0 when the editor has asked to
 * close (the "Done" button, var_188). */
int  reditor_key(struct REDITOR* ed, int16_t key);

/* seg009 loc_2B0E2 / loc_2B1B0 - a click in the map or in the palette.
 * Coordinates are the original's 320x200 ones.  Returns 1 if the click
 * landed in one of the editor's five regions. */
int  reditor_click(struct REDITOR* ed, int16_t mx, int16_t my);

/* seg009 loc_2B49A..loc_2BB46 - what Enter does: place the selected
 * terrain or element, or press whichever palette button is under the
 * cursor.  Returns one of the RED_ACT_* codes below so the host can put up
 * the dialog the original puts up inline. */
#define RED_ACT_NONE     0
#define RED_ACT_HORIZON  1      /* "mss" Select Horizon                  */
#define RED_ACT_NEW      2      /* "men" Select Terrain                  */
#define RED_ACT_LOAD     3      /* "chl" + the file browser              */
#define RED_ACT_SAVE     4      /* the save-name browser                 */
#define RED_ACT_EXIT     5      /* "chx" + leave                         */
int  reditor_activate(struct REDITOR* ed);

/* seg009 loc_2B5BA - set the landscape byte at td14[0x384]. */
void reditor_set_horizon(struct REDITOR* ed, uint8_t horizon);

/* seg009 loc_2B61C - zero all 900 element bytes and copy "ter<n>" over the
 * terrain map, which is exactly what the five presets are. */
void reditor_new_track(struct REDITOR* ed, uint8_t terrain);

/* seg009 loc_2B3EA - track_setup() and its fifteen verdicts.  Returns the
 * verdict code; reditor_verdict_key() turns it into the three-character
 * text key the message lives under in TEDIT.PRE. */
int16_t     reditor_check(struct REDITOR* ed);
const char* reditor_verdict_key(int16_t verdict);

/* seg009 loc_2B725 / loc_2B817 - the whole 1802-byte track image.
 * reditor_save_track is the writer this phase had to add; the game only
 * ever writes a track from here. */
int reditor_load_track(struct REDITOR* ed, const char* path);
int reditor_save_track(struct REDITOR* ed, const char* path);

/* locate_text_res against TEDIT.PRE, for the host's dialog texts. */
const char* reditor_text(struct REDITOR* ed, const char* key);

/* STUNTS_EDITOR_SHOT=<path>: open the editor on whatever track is already
 * in td14_elem_map_main, draw one frame and write it out as a 24-bit BMP.
 * Returns 0 when the variable is not set, so the host can write
 *
 *     if (reditor_shot(pal.colors)) return 0;
 *
 * anywhere after game_init.  `pal` is a stunts_color_rgba_t[256] - i.e.
 * main_native.c's `pal.colors`, not `&pal`. */
int reditor_shot(const void* pal);

#endif /* RESTUNTS_REDITOR_H */
