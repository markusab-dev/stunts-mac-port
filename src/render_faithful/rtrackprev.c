/*
 * rtrackprev.c - the 3D track preview in the track picker.
 *
 * Ported from seg003.asm draw_track_preview (4760..5360), called from
 * seg000.asm:1813 inside the track menu.
 *
 * WHAT IT IS
 * ----------
 * A single still frame of the whole track seen from a fixed camera high above
 * one corner, drawn at half scale into the picker's window. It is not a
 * separate renderer: it walks the 30x30 element and terrain grids exactly as
 * update_frame does and hands every tile to the same transformed_shape_op.
 * That is why the plan called this the cheapest visible value in the whole
 * phase - all twelve routines it calls were already ported.
 *
 * THE CAMERA IS SIX CONSTANTS
 * ---------------------------
 * dseg 0x3C108..0x3C113 are two fixed points, and 0x3C114 a vector:
 *
 *     from = (15360, 20200, -2800)      word_3C108 / _3C10A / _3C10C
 *     to   = (15360,  2800, 10960)      word_3C10E / _3C110 / _3C112
 *     horizon vector = (0, -10100, 16760)                  unk_3C114
 *
 * (word_3C10C reads 62736 in the dump, which is -2800 as int16.) The routine
 * derives one pitch angle from those - polarAngle of the height difference
 * against the ground distance, the same argument order the chase camera and
 * the intro use - rotates the horizon vector by it, and projects that to get
 * the horizon's screen row. Everything else follows from that row: sky above,
 * ground below, two of the four panorama strips across it.
 *
 * Note it draws only skyboxes[2] and skyboxes[3], not all four, and places
 * the second at x = 0x140 - off the right edge, where it wraps.
 *
 * HALF SCALE
 * ----------
 * Every position is `(world - camera) >> 1`. The preview shows a whole 30x30
 * track in one screen, so the world is halved rather than the projection
 * changed.
 *
 * THE RECORD LAYOUT THE OFFSETS IMPLY
 * -----------------------------------
 * The asm indexes TRACKOBJECT by byte offset and multiplies the index by 14,
 * which pins the DOS layout: the two shape pointers and the info pointer are
 * *near* (2 bytes), not far. That gives
 *
 *     +0 ss_trkObjInfoPtr  +2 ss_rotY        +4 ss_shapePtr
 *     +6 ss_loShapePtr     +8 ss_ssOvelay    +9 ss_surfaceType
 *     +0Ah ss_ignoreZBias  +0Bh ss_multiTileFlag
 *
 * = 14, and every offset in the routine lands on a named field. Written
 * through the names here, since this port's pointers are 64-bit and the
 * struct is a different size.
 *
 * The preview draws `ss_loShapePtr` - the low-detail shape - for track pieces
 * and scenery, and `ss_shapePtr` for the terrain corners. game3dshapes[43] is
 * the flat tile under a one-tile object; [91], [92] and [93] are the two- and
 * four-tile bases, selected by ss_multiTileFlag.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "externs.h"
#include "math.h"
#include "shape3d.h"
#include "rfbsize.h"

extern struct TRACKOBJECT sceneshapes2[];
extern struct TRACKOBJECT trkObjectList[215];
extern struct SHAPE3D game3dshapes[];

extern void far* skyboxes[4];
extern int16_t skybox_ptr3, skybox_ptr4;
extern int16_t skybox_current, skybox_sky_color, skybox_grd_color;

extern uint8_t subst_hillroad_track(uint8_t a, uint8_t b);
extern void sprite_putimage_and_alt(void far* shape, int16_t x, int16_t y);
extern void sprite_clear_1_color(uint8_t color);

/* dseg 0x3C108 and 0x3C10E - the fixed camera, and dseg 0x3C114, the vector
 * whose projection gives the horizon row. */
static const struct VECTOR tp_from = { 15360, 20200, -2800 };
static const struct VECTOR tp_to   = { 15360,  2800, 10960 };
static struct VECTOR tp_horizon    = {     0, -10100, 16760 };

/* dseg 0x3C11A. restunts2 has it symbolically:
 *   trackpreview_cliprect RECTANGLE <0x0000, 0x0140, 0x0000, 0x00C8> */
static struct RECTANGLE trackpreview_cliprect = { 0, 0x140, 0, 0xC8 };

/* The original works in the 320x200 screen; scale as every other 2D path in
 * this port does so the preview keeps pace with RFB_SCALE. */
#define TPY(v) ((int16_t)((v) * RFB_SCALE))

void draw_track_preview(void)
{
	struct TRANSFORMEDSHAPE3D var_transshape;
	struct VECTOR var_vec;
	struct POINT2D var_pt;
	struct MATRIX* var_28;
	struct TRACKOBJECT* var_3E = NULL;
	struct TRACKOBJECT* var_2;
	int16_t var_4, var_34, var_30, var_12 = 0, var_2A = 0, si = 0, di = 0;
	int8_t var_38, var_32, var_36 = 0, var_3C = 0;
	uint8_t var_3A = 0, var_10 = 0;

	memset(&var_transshape, 0, sizeof var_transshape);

	/* seg003:4788. polarAngle takes the height difference first and the
	 * ground distance second - see the note in rintro3d.c, which had this
	 * order reversed and pitched the camera 100 degrees off. */
	var_4  = polarRadius2D((int16_t)(tp_to.x - tp_from.x),
	                       (int16_t)(tp_to.z - tp_from.z));
	var_34 = polarAngle((int16_t)(tp_to.y - tp_from.y), var_4);
	var_28 = mat_rot_zxy(0, var_34, 0, 1);
	mat_mul_vector(&tp_horizon, var_28, &var_vec);
	vector_to_point(&var_vec, &var_pt);
	var_30 = var_pt.py;
	if (var_30 < 0) var_30 = 0;   /* loc_1CC5B */

	/* ---- the horizon, sky and ground (loc_1CC5B..) ---- */
	sprite_set_1_size(0, RFB_VIEW_W, 0, TPY(var_30 - skybox_current));
	sprite_clear_1_color((uint8_t)skybox_sky_color);

	sprite_set_1_size(0, RFB_VIEW_W, 0, TPY(0x64));
	sprite_putimage_and_alt(skyboxes[2], 0, (int16_t)(var_30 - skybox_ptr3));
	sprite_putimage_and_alt(skyboxes[3], 0x140, (int16_t)(var_30 - skybox_ptr4));

	sprite_set_1_size(0, RFB_VIEW_W, TPY(var_30), RFB_VIEW_H);
	sprite_clear_1_color((uint8_t)skybox_grd_color);

	sprite_set_1_size(0, RFB_VIEW_W, 0, RFB_VIEW_H);
	select_cliprect_rotate(0, var_34, 0, &trackpreview_cliprect, 1);

	var_transshape.rotvec.x = 0;
	var_transshape.rotvec.y = 0;
	var_transshape.unk = 0x400;
	var_38 = 0;
	goto loc_1D11B;

loc_1CD34:
	var_3A = 0;
loc_1CD38:
	var_10 = 0;
loc_1CD3C:
	if (var_3A == 6) goto loc_1CD45;
	goto loc_1CE04;

loc_1CD45:
	di = hillHeightConsts[1];
	if (var_10 != 0) goto loc_1CD53;
loc_1CD4F:
	var_3A = 0;
loc_1CD53:
	if (var_3A == 0) goto loc_1CDCA;
	/* the scenery standing on this tile */
	var_3E = &sceneshapes2[var_3A];
	var_transshape.shapeptr = var_3E->ss_loShapePtr;
	var_transshape.pos.x =
		(int16_t)((int16_t)(trackcenterpos2[var_32] - tp_from.x) >> 1);
	var_transshape.pos.y = (int16_t)((int16_t)(di - tp_from.y) >> 1);
	var_transshape.pos.z =
		(int16_t)((int16_t)(trackcenterpos[var_38] - tp_from.z) >> 1);
	var_transshape.rotvec.z = var_3E->ss_rotY;
loc_1CDB6:
	var_transshape.ts_flags = 5;
	var_transshape.material = 0;
	transformed_shape_op(&var_transshape);

loc_1CDCA:
	if (var_10 == 0) goto loc_1D086;
	var_3E = &trkObjectList[var_10];
	if (!(var_3E->ss_multiTileFlag & 1)) goto loc_1CF14;
	var_12 = trackpos[var_38];            /* loc_1CDF5 */
	goto loc_1CF20;

loc_1CE04:
	di = 0;
	if (var_10 < 0x69) goto loc_1CD53;
	if (var_10 > 0x6C) goto loc_1CD53;
	/* Elements 0x69..0x6C are the four terrain-corner tiles: each draws the
	 * terrain shape at all four of its corners rather than once. */
	var_2A = 0;
	goto loc_1CED0;

loc_1CE22:
	var_36 = var_32;
loc_1CE25:
	var_3C = var_38;
loc_1CE2E:
	var_3A = td15_terr_map_main[terrainrows[var_3C] + var_36];
	if (var_3A == 0) goto loc_1CECD;
	var_3E = &sceneshapes2[var_3A];
	var_transshape.shapeptr = var_3E->ss_shapePtr;
	var_transshape.pos.x =
		(int16_t)((int16_t)(trackcenterpos2[var_36] - tp_from.x) >> 1);
	var_transshape.pos.y = (int16_t)((int16_t)(-tp_from.y) >> 1);
	var_transshape.pos.z =
		(int16_t)((int16_t)(trackcenterpos[var_3C] - tp_from.z) >> 1);
	var_transshape.ts_flags = 5;
	var_transshape.rotvec.x = 0;
	var_transshape.rotvec.y = 0;
	var_transshape.rotvec.z = var_3E->ss_rotY;
	var_transshape.unk = 0x400;
	var_transshape.material = 0;
	transformed_shape_op(&var_transshape);
loc_1CECD:
	var_2A++;
loc_1CED0:
	if (var_2A >= 4) goto loc_1CD4F;
	switch (var_2A) {
	case 0:  goto loc_1CE22;
	case 1:  var_36 = (int8_t)(var_32 + 1); goto loc_1CE25;    /* loc_1CEF6 */
	case 2:  var_36 = var_32;                                  /* loc_1CEFE */
	         var_3C = (int8_t)(var_38 + 1); goto loc_1CE2E;
	case 3:  var_36 = (int8_t)(var_32 + 1);                    /* loc_1CF0C */
	         var_3C = (int8_t)(var_38 + 1); goto loc_1CE2E;
	default: goto loc_1CE2E;
	}

loc_1CF14:
	var_12 = trackcenterpos[var_38];
loc_1CF20:
	if (var_3E->ss_multiTileFlag & 2)
		si = trackpos2[var_32 + 1];
	else
		si = trackcenterpos2[var_32];   /* loc_1CF3A */

	var_vec.x = (int16_t)((int16_t)(si - tp_from.x) >> 1);
	var_vec.y = (int16_t)((int16_t)(di - tp_from.y) >> 1);
	var_vec.z = (int16_t)((int16_t)(var_12 - tp_from.z) >> 1);

	if (di == 0) goto loc_1CFBF;
	/* On a hill the piece needs a base under it. Which one depends on how
	 * many tiles it covers - loc_1CF92 / loc_1D01A / loc_1D022 / loc_1D02A
	 * name game3dshapes+3B2h, +7D2h, +7E8h and +7FEh, and SHAPE3D is 22
	 * bytes in the DOS build, so those are entries 43, 91, 92 and 93. */
	switch ((int8_t)var_3E->ss_multiTileFlag) {
	case 0:  var_transshape.shapeptr = &game3dshapes[43]; break;
	case 1:  var_transshape.shapeptr = &game3dshapes[91]; break;
	case 2:  var_transshape.shapeptr = &game3dshapes[92]; break;
	case 3:  var_transshape.shapeptr = &game3dshapes[93]; break;
	default: break;   /* loc_1CF8F: keeps whatever was there */
	}
	/* loc_1CF97: `movsw` x3 - the three words of the position */
	var_transshape.pos = var_vec;
	var_transshape.rotvec.z = 0;
	var_transshape.ts_flags = 5;
	var_transshape.material = 0;
	transformed_shape_op(&var_transshape);

loc_1CFBF:
	if (var_3E->ss_ssOvelay == 0) goto loc_1D042;
	var_2 = &trkObjectList[(uint8_t)var_3E->ss_ssOvelay];
	if (var_2->ss_loShapePtr == 0) goto loc_1D042;
	var_transshape.shapeptr = var_2->ss_loShapePtr;
	var_transshape.pos = var_vec;
	var_transshape.rotvec.z = var_3E->ss_rotY;
	var_transshape.ts_flags = 5;
	var_transshape.material =
		var_2->ss_surfaceType >= 0 ? (uint8_t)var_2->ss_surfaceType : 0;
	transformed_shape_op(&var_transshape);

loc_1D042:
	var_transshape.shapeptr = var_3E->ss_loShapePtr;
	var_transshape.pos = var_vec;
	var_transshape.rotvec.z = var_3E->ss_rotY;
	var_transshape.ts_flags = (uint8_t)(var_3E->ss_ignoreZBias | 4);
	var_transshape.material =
		var_3E->ss_surfaceType >= 0 ? (uint8_t)var_3E->ss_surfaceType : 0;
	transformed_shape_op(&var_transshape);

loc_1D086:
	get_a_poly_info();
	var_32++;
loc_1D08E:
	if (var_32 >= 30) goto loc_1D118;
	var_10 = td14_elem_map_main[trackrows[var_38] + var_32];
	var_3A = td15_terr_map_main[terrainrows[var_38] + var_32];
	if (var_10 == 0) goto loc_1CD3C;
	if (var_3A >= 7 && var_3A < 0x0B) {
		var_10 = subst_hillroad_track(var_3A, var_10);
		var_3A = 0;
	}
loc_1D100:
	if (var_10 < 0xFD) goto loc_1CD3C;
	goto loc_1CD34;   /* 0xFD..0xFF: neither element nor terrain is drawn */

loc_1D118:
	var_38++;
loc_1D11B:
	if (var_38 >= 30) goto done;
	var_32 = 0;
	goto loc_1D08E;

done:
	return;
}
