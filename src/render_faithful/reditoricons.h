#ifndef RESTUNTS_REDITORICONS_H
#define RESTUNTS_REDITORICONS_H

#include <stdint.h>
#include "../render/stunts_palette.h"

/*
 * reditoricons.h - the track editor's icon palette and its two map-repair
 * helpers.  See reditoricons.c for the provenance of every routine.
 */

/* ---- seg012 blitters the icon palette needs ---------------------- */
/* All three take 320x200 screen coordinates and do NOT read the shape's
 * own s2d_pos_x / s2d_pos_y (the "_alt" entry points do that; the one for
 * sprite_shape_to_1 already lives in rwidgets.c). */
void sprite_shape_to_1(void far* shape, uint16_t x, uint16_t y);
void putpixel_iconMask(void far* shape, uint16_t x, uint16_t y);
void putpixel_iconFillings(void far* shape, uint16_t x, uint16_t y);

/* ---- the dseg globals the palette is drawn from ------------------ */
extern void far* tracksmenushapes1[19];      /* dseg 0x70CC */
extern void far* tracksmenushape2dunk[186];  /* dseg 0x6AE8, from "snam" */
extern void far* tracksmenushape2dunk2[186]; /* dseg 0x6DD4, from "mnam" */
extern uint8_t far* pboxshape;               /* dseg 0x6DD0, from "pbox" */

/* pbox is 396 bytes = 11 pages of 6x6.  seg009:2A8CA caps the editor's page
 * selector at 0x0A, which is the same eleven. */
#define EDITOR_ICON_PAGES 11

/* seg009 load_tracks_menu_shapes 2054..2270, split out so the palette can be
 * drawn without opening the whole editor.  Returns 1 on success, 0 if either
 * archive is missing.  editor_free_icon_shapes() undoes it. */
int  editor_load_icon_shapes(void);
void editor_free_icon_shapes(void);

/* seg009 preRender_icons (2954..3178): draw page `arg_0` of the palette into
 * sprite 1, at x = 0xDC + col*16, y = 0x24 + row*16. */
void preRender_icons(int16_t arg_0);

/* seg009 sub_2C81C (4007..4200): walk the 30x30 maps and delete every element
 * that cannot legally stand on the terrain under it.  Returns 0 when nothing
 * was changed, or the code of the last repair (0x0C / 0x0D / 0x0E). */
int16_t sub_2C81C(void);

/* seg009 sub_2C9B4 (4201..4499): make the multi-tile elements consistent -
 * every anchor must own its 0xFF/0xFE/0xFD continuation cells, and every
 * continuation cell must have an anchor.  Called by sub_2C81C. */
void sub_2C9B4(void);

/* STUNTS_ICONS_SHOT=<dir>: render all eleven palette pages and write them as
 * BMPs, plus one contact sheet.  Returns the number of files written, 0 when
 * the variable is not set, -1 on failure. */
int editor_icons_shot(const stunts_palette_t* pal);

#endif /* RESTUNTS_REDITORICONS_H */
