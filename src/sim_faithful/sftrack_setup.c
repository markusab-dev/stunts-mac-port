/*
 * sftrack_setup.c - track_setup(), translated instruction-exactly.
 *
 * reference/restunts2/src/asm/seg004.asm lines 4008 (`track_setup_asm_ proc
 * far`) .. 5764 (`endp`); 1484 instructions. The only routine it calls is
 * subst_hillroad_track (already ported, rframe_helpers.c) plus the memory
 * manager for one scratch buffer.
 *
 * What it produces, from the element and terrain maps:
 *   - startcol2 / startrow2 / hillFlag / track_angle  (where the car starts)
 *   - td01_track_file_cpy / td02_penalty_related      (path successor + branch)
 *   - td17_trk_elem_ordered                           (element per path index)
 *   - trackdata18                                     (connStatus<<4 | subTOI)
 *   - td21_col_from_path / td22_row_from_path         (tile per path index)
 *   - trackdata19                                     (check index per tile)
 *   - td08_direction_related / td10_track_check_rel   (opponent checkpoints)
 *   - trackdata6 / trackdata7 / trackdata9            (opponent target points)
 *   - track_pieces_counter
 * and returns a track error code (0 = OK, see TRACK_ERR_* below).
 *
 * These are exactly the tables sub_18D60 and detect_penalty need, which is why
 * loops and penalties do not work in the interactive build without this.
 *
 * The original walks the track as a *path*, not row by row: from the start
 * tile it follows each element's exit point to the next tile, pushing branch
 * points onto a scratch stack ("tcomp", 64 x 14 bytes) so alternative routes
 * are explored afterwards.
 *
 * Frame layout, verified against the proc header. Three unnamed arrays live in
 * the gaps between declared locals, and the original indexes them off bp:
 *   bp-2772 var_AD4[902]  `[bp+di+0F52Ch]`, `[bx+var_AD4]`
 *   bp-1848 var_738[904]  `[bp+si+0F8C8h]`
 *   bp-920  var_398[902]  `[bp+di+0FC68h]`, `[bx+var_398]`
 * var_AD4 serves two purposes in sequence, exactly as in the original: the
 * sub-TOI block per path index while walking, then a per-tile visited flag in
 * the final opponent-checkpoint pass (which clears it first).
 */
#include <stdint.h>
#include <string.h>

#include "sfport.h"
#include "externs.h"

extern uint8_t subst_hillroad_track(uint8_t terr, uint8_t elem);
extern void far* mmgr_alloc_resbytes(const char* name, int32_t size);
extern void mmgr_release(char far* ptr);
extern struct TRACKOBJECT trkObjectList[];

/* structs.inc:12-22 */
enum {
	TRACK_OK = 0x00, TRACK_ERR_NO_SF = 0x01, TRACK_ERR_INT = 0x02,
	TRACK_ERR_MANY_SF = 0x03, TRACK_ERR_ELEM_MISM = 0x04,
	TRACK_ERR_WRONG_WAY = 0x05, TRACK_ERR_MANY_ELEM = 0x06,
	TRACK_ERR_NO_PATH = 0x07, TRACK_ERR_MANY_PATH = 0x08,
	TRACK_ERR_NO_RUNWAY = 0x09, TRACK_ERR_LONG_JUMP = 0x0A,
	TRACK_ERR_TERR_MISM = 0x0B,
};

/* dseg 0x3E71E / 0x3E724: 6 bytes each, indexed by si_opp3. */
static const uint8_t byte_3E71E[6] = { 0, 0, 1, 0, 1, 0 };
static const uint8_t byte_3E724[6] = { 0, 1, 0, 0, 1, 0 };

/* dseg: terrain connectivity per terrain code, one entry per code 0..0x12.
 * A tile's east edge must present the same code as its neighbour's west edge,
 * and likewise north/south, or the track is rejected as "terrain mismatch".
 * terrConnDataStoN is 19 bytes in dseg (the others 20); index 19 is never
 * reached because terrain codes stop at 0x12, so the pad byte is harmless. */
static const uint8_t terrConnDataEtoW[20] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02, 0x01,0x03,0x00,0x02,0x03,0x00,0x00,0x01,
	0x01,0x03,0x02,0x00 };
static const uint8_t terrConnDataWtoE[20] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02, 0x00,0x03,0x01,0x00,0x00,0x03,0x02,0x02,
	0x03,0x01,0x01,0x00 };
static const uint8_t terrConnDataNtoS[20] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01, 0x05,0x00,0x04,0x05,0x00,0x00,0x04,0x01,
	0x05,0x04,0x01,0x00 };
static const uint8_t terrConnDataStoN[20] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00, 0x05,0x01,0x04,0x00,0x05,0x04,0x00,0x05,
	0x01,0x01,0x04, 0x00 };

/* dsegu scalars this routine owns. */
uint8_t  byte_45635;   /* opponent checkpoint write index */
uint8_t  byte_45D90;   /* column reported on a track error */
uint8_t  byte_45E16;   /* row reported on a track error */
uint8_t  byte_4616E;   /* number of opponent target points */
int16_t  track_pieces_counter;

/* One entry of the "tcomp" branch stack. */
struct TCOMP {
	uint8_t  colIndex;        /* +0 */
	uint8_t  rowIndex;        /* +1 */
	uint8_t  tileElem;        /* +2 */
	uint8_t  subTOIBlock;     /* +3 */
	uint8_t  connStatus;      /* +4 */
	uint8_t  f3A8;            /* +5 */
	uint8_t  prevColIndex;    /* +6 */
	uint8_t  prevRowIndex;    /* +7 */
	uint8_t  prevTileElem;    /* +8 */
	uint8_t  f3AA;            /* +9 */
	uint8_t  prevConnStatus;  /* +0xA */
	uint8_t  prevConnCode;    /* +0xB */
	int16_t  f3AC;            /* +0xC */
};

/* `mov ax, [bx+n]` against a byte table: little-endian 16-bit load. */
static inline int16_t ldw(const uint8_t* p, int off)
{
	return (int16_t)((uint16_t)p[off] | ((uint16_t)p[off + 1] << 8));
}

int16_t track_setup(void)
{
	/* --- locals, named after the original's frame comments --- */
	struct TCOMP* tcomp;                 /* var_tcompPtr / var_tcompPtr2 */
	uint8_t  var_AD4[902];
	uint8_t  var_738[904];
	uint8_t  var_398[902];

	int16_t  var_AEC, var_AEA;
	uint8_t  var_AE8;
	uint8_t  var_MprevTileElem;
	int8_t   var_connCheckFlag;
	int16_t  var_ADA, var_AD8, var_AD6;  /* the copied VECTOR (x, y, z) */
	const uint8_t* var_ADE;              /* shapedata pointer */
	uint8_t  var_tileTerr;
	uint8_t  var_74C;
	uint8_t  var_746;                    /* tcomp stack depth */
	uint8_t  var_McurrExitPoint;
	uint8_t  var_subTOIBlock;
	uint8_t  var_MprevConnStatus;
	uint8_t  var_MconnStatus;
	int8_t   var_trkRowIndex;
	uint8_t  var_trackErrorCode;
	struct TRKOBJINFO* var_ptrCurrTOInfo;
	int16_t  var_3AC;
	uint8_t  var_3AA;
	uint8_t  var_3A8;
	uint8_t  var_sfCount;
	int8_t   var_trkColIndex;
	int16_t  var_3A2;
	struct TCOMP* var_MinternalTOI1;
	struct TRKOBJINFO* var_ptrTOInfo;
	uint8_t  var_MprevRowIndex;
	uint8_t  var_prevConnCode;
	uint8_t  var_MprevColIndex;
	uint8_t  var_tileElem;
	int16_t  var_C, var_A;
	int16_t  var_trackDirection;
	uint8_t  var_tileEntryPoint;
	uint8_t  var_4;
	uint8_t  var_2;

	int16_t  si, di, ax;
	uint8_t  al;

	/* mov ax,380h ; push "tcomp" ; call mmgr_alloc_resbytes */
	tcomp = (struct TCOMP*)mmgr_alloc_resbytes("tcomp", 0x380);
	if (tcomp == 0)
		return 2;                        /* mov ax,2 ; retf */

	/* LAB_1e1a_2564 */
	var_sfCount = 0;
	var_4 = 0;
	track_pieces_counter = 0;
	for (si = 0; si < 0x385; si++)       /* LAB_1e1a_2585 */
		trackdata19[si] = 0xFF;

	/* ---------------------------------------------------------------- */
	/* Terrain connectivity, west to east then north to south.           */
	/* ---------------------------------------------------------------- */
	var_trkRowIndex = 0;
	goto L2604;
L259C:
	var_prevConnCode = terrConnDataWtoE[var_tileTerr];
	var_trkColIndex++;
L25AD:
	if (var_trkColIndex >= 0x1E) goto L2600;
	var_tileTerr = td15_terr_map_main[terrainrows[var_trkRowIndex] + var_trkColIndex];
	if (terrConnDataEtoW[var_tileTerr] == var_prevConnCode) goto L259C;
	if (var_prevConnCode == 0x63) goto L259C;   /* 63h = unset */
L25E8:
	var_trackErrorCode = TRACK_ERR_TERR_MISM;
L25ED:
	if ((uint8_t)var_trkColIndex == 0xFF) goto L25F7;
	goto L35DA;
L25F7:
	var_trkColIndex = 0;
	goto L35E6;
L2600:
	var_trkRowIndex++;
L2604:
	if (var_trkRowIndex >= 0x1E) goto L2616;
	var_prevConnCode = 0x63;
	var_trkColIndex = 0;
	goto L25AD;
L2616:
	var_trkColIndex = 0;
	goto L2672;
L261E:
	var_prevConnCode = terrConnDataStoN[var_tileTerr];
	var_trkRowIndex++;
L262F:
	if (var_trkRowIndex >= 0x1E) goto L266E;
	var_tileTerr = td15_terr_map_main[terrainrows[var_trkRowIndex] + var_trkColIndex];
	if (terrConnDataNtoS[var_tileTerr] == var_prevConnCode) goto L261E;
	if (var_prevConnCode == 0x63) goto L261E;
	goto L25E8;
L266E:
	var_trkColIndex++;
L2672:
	if (var_trkColIndex >= 0x1E) goto L2684;
	var_prevConnCode = 0x63;
	var_trkRowIndex = 0;
	goto L262F;
L2684:
	var_trkRowIndex = 0;
	goto L27B6;

	/* ---------------------------------------------------------------- */
	/* Find the start/finish tile and the initial track orientation.     */
	/* ---------------------------------------------------------------- */
L268C:
	track_angle = 0;
L2692:
	if (var_sfCount == 0) goto L26DA;
	var_trackErrorCode = TRACK_ERR_MANY_SF;
	goto L25ED;
L26A2:
	track_angle = 0x200;
	goto L2692;
L26AA:
	track_angle = 0x100;
	goto L2692;
L26B2:
	track_angle = 0x300;
	goto L2692;
L26BA:
	if (ax == 0x94) goto L26A2;
	if (ax == 0x95) goto L26AA;
	if (ax == 0x96) goto L26B2;
	if (ax == 0xB3) goto L26A2;
	if (ax == 0xB4) goto L26AA;
	if (ax == 0xB5) goto L26B2;   /* LAB_1e1a_26d6 */
	goto L271B;
L26DA:
	startcol2 = (char)var_trkColIndex;
	startrow2 = (char)var_trkRowIndex;
	var_tileTerr = td15_terr_map_main[terrainrows[var_trkRowIndex] + var_trkColIndex];
	if (var_tileTerr != 6)                 /* hilltop */
		hillFlag = 0;
	else
		hillFlag = 1;
	var_sfCount++;
L271B:
	var_trkColIndex++;
L271F:
	if (var_trkColIndex >= 0x1E) goto L27B2;
	var_tileElem = td14_elem_map_main[trackrows[var_trkRowIndex] + var_trkColIndex];
	if (var_tileElem >= 0xFD)              /* filler */
		var_tileElem = 0;
	if (var_tileElem >= 0xB6) {
		/* B5 < elem < FD is illegal: replaced by plain road, in the map too */
		var_tileElem = 4;
		td14_elem_map_main[trackrows[var_trkRowIndex] + var_trkColIndex] = 4;
	}
	ax = (int16_t)var_tileElem;
	if (ax == 0x93) goto L268C;
	if (ax > 0x93) goto L26BA;
	if (ax == 0x01) goto L268C;
	if (ax == 0x86) goto L268C;
	if (ax == 0x87) goto L26A2;
	if (ax == 0x88) goto L26AA;
	if (ax == 0x89) goto L26B2;            /* falls into LAB_1e1a_26d6 */
	goto L271B;
L27B2:
	var_trkRowIndex++;
L27B6:
	if (var_trkRowIndex >= 0x1E) goto L27C6;
	var_trkColIndex = 0;
	goto L271F;
L27C6:
	if (var_sfCount != 0) goto L27D6;
	var_trackErrorCode = TRACK_ERR_NO_SF;
	goto L25ED;

	/* ---------------------------------------------------------------- */
	/* Walk the track as a path from the start tile.                     */
	/* ---------------------------------------------------------------- */
L27D6:
	track_pieces_counter = 0;
	var_746 = 0;
	byte_45635 = 0;
	byte_4616E = 0;
	var_3A8 = 0;
	var_AE8 = 0;
	for (si = 0; si < 0x385; si++) {       /* LAB_1e1a_27f7 */
		var_738[si] = 0;
		td01_track_file_cpy[si] = -1;
		td02_penalty_related[si] = -1;
	}
	var_trkColIndex = startcol2;
	var_trkRowIndex = startrow2;
	var_trackDirection = track_angle;
	var_prevConnCode = 0;
	var_3AC = -1;

L2849:
	var_2 = 0;
	/* Escapes for a track that runs off the 30x30 grid. */
	if (var_trkColIndex < 0) goto L286C;
	if (var_trkRowIndex < 0) goto L286C;
	if (var_trkColIndex > 0x1D) goto L286C;
	if (var_trkRowIndex > 0x1D) goto L286C;
	goto L2922;
L286C:
	if (var_746 == 0) goto L2D80;
	/* Pop a branch point: read the internal TOI back. */
	var_746--;
	var_MinternalTOI1 = &tcomp[var_746];
	var_trkColIndex     = (int8_t)var_MinternalTOI1->colIndex;
	var_trkRowIndex     = (int8_t)var_MinternalTOI1->rowIndex;
	var_tileElem        = var_MinternalTOI1->tileElem;
	var_subTOIBlock     = var_MinternalTOI1->subTOIBlock;
	var_MconnStatus     = var_MinternalTOI1->connStatus;
	var_prevConnCode    = var_MinternalTOI1->prevConnCode;
	var_3AC             = var_MinternalTOI1->f3AC;
	var_3A8             = var_MinternalTOI1->f3A8;
	var_MprevColIndex   = var_MinternalTOI1->prevColIndex;
	var_MprevRowIndex   = var_MinternalTOI1->prevRowIndex;
	var_MprevTileElem   = var_MinternalTOI1->prevTileElem;
	var_3AA             = var_MinternalTOI1->f3AA;
	var_MprevConnStatus = var_MinternalTOI1->prevConnStatus;
	var_2 = 1;
L2908:
	if (var_2 == 0) goto L2849;
	if (var_4 > 1) {                       /* ja (unsigned) */
		var_trackErrorCode = TRACK_ERR_LONG_JUMP;
		goto L25ED;
	}
	goto L2DCA;

	/* Normal procedure: load the tile. */
L2922:
	var_AEA = (int16_t)var_trkColIndex;    /* cbw */
	var_AEC = (int16_t)(var_trkRowIndex << 1);
	var_tileElem = td14_elem_map_main[trackrows[var_trkRowIndex] + var_AEA];
	var_tileTerr = td15_terr_map_main[terrainrows[var_trkRowIndex] + var_AEA];
	if (var_tileElem == 0) goto L2990;
	if (var_tileTerr == 0) goto L2990;
	if (var_tileTerr < 7) goto L2990;
	if (var_tileTerr >= 0x0B) goto L2990;
	var_tileElem = subst_hillroad_track(var_tileTerr, var_tileElem);
L2990:
	if (var_tileElem < 0xFD) goto L2A72;

	/* Filler tiles: step back to the element's main tile and pre-set the
	 * entry point from the direction we arrived in. */
	ax = (int16_t)var_tileElem;
	if (ax == 0xFD) goto L29D6;
	if (ax == 0xFE) goto L2A02;
	if (ax == 0xFF) goto L2A34;
L29B0:
	var_tileElem = td14_elem_map_main[trackrows[var_trkRowIndex] + var_trkColIndex];
	goto L2A8E;
L29D6:
	var_trkColIndex--;
	var_trkRowIndex--;
	ax = var_trackDirection;
	if (ax == 0)     goto L29F6;
	if (ax == 0x100) goto L2A62;
	if (ax == 0x200) goto L2A62;
	if (ax == 0x300) goto L29FC;
	goto L29B0;
L29F6:
	var_tileEntryPoint = 0x0C; goto L29B0;
L29FC:
	var_tileEntryPoint = 0x09; goto L29B0;
L2A02:
	var_trkRowIndex--;
	ax = var_trackDirection;
	if (ax == 0)     goto L2A1E;
	if (ax == 0x100) goto L2A24;
	if (ax == 0x200) goto L2A62;
	if (ax == 0x300) goto L2A2C;
	goto L29B0;
L2A1E:
	var_tileEntryPoint = 0x0B; goto L29B0;
L2A24:
	var_tileEntryPoint = 0x06; goto L29B0;
L2A2C:
	var_tileEntryPoint = 0x07; goto L29B0;
L2A34:
	var_trkColIndex--;
	ax = var_trackDirection;
	if (ax == 0)     goto L2A52;
	if (ax == 0x100) goto L2A62;
	if (ax == 0x200) goto L2A5A;
	if (ax == 0x300) goto L2A6A;
	goto L29B0;
L2A52:
	var_tileEntryPoint = 0x0A; goto L29B0;
L2A5A:
	var_tileEntryPoint = 0x05; goto L29B0;
L2A62:
	var_tileEntryPoint = 0x00; goto L29B0;
L2A6A:
	var_tileEntryPoint = 0x08; goto L29B0;

	/* Entry point from the direction of travel. */
L2A72:
	ax = var_trackDirection;
	if (ax == 0)     goto L2A8A;
	if (ax == 0x100) goto L2AA8;
	if (ax == 0x200) goto L2AA2;
	if (ax == 0x300) goto L2AAE;
	goto L2A8E;
L2A8A:
	var_tileEntryPoint = 2;
L2A8E:
	if (var_4 != 0) goto L2AB4;
	if (var_tileEntryPoint != 0) goto L2AB4;
	var_trackErrorCode = TRACK_ERR_INT;
	goto L25ED;
L2AA2:
	var_tileEntryPoint = 1; goto L2A8E;
L2AA8:
	var_tileEntryPoint = 4; goto L2A8E;
L2AAE:
	var_tileEntryPoint = 3; goto L2A8E;

L2AB4:
	var_2 = 0;
	var_ptrTOInfo = trkObjectList[var_tileElem].ss_trkObjInfoPtr;
	if (var_ptrTOInfo == 0) goto L2CE2;    /* elements without connectivity */
	si = 0;
	goto L2C9A;

L2ADE:
	var_connCheckFlag = 0;
	goto L2B02;
L2AE6:
	if (var_ptrCurrTOInfo->si_exitPoint != (char)var_tileEntryPoint) goto L2B02;
	if (var_ptrCurrTOInfo->si_exitType != (char)var_prevConnCode) goto L2CD9;
	var_connCheckFlag = 1;
L2B02:
	if (var_connCheckFlag < 0) goto L2BA8;
	/* Have we been on this tile before? */
	if (var_738[trackrows[var_trkRowIndex] + var_trkColIndex] == 0) goto L2BA8;
	di = 0;
	goto L2B6E;
L2B2E:
	var_connCheckFlag = -1;
	if (td01_track_file_cpy[var_3AC] == -1)
		td01_track_file_cpy[var_3AC] = di;
	else
		td02_penalty_related[var_3AC] = di;
	if (di == 0)
		var_AE8 = 1;
	di++;
L2B6E:
	if (track_pieces_counter <= di) goto L2BA8;
	if (td21_col_from_path[di] != (char)var_trkColIndex) { di++; goto L2B6E; }
	if (td22_row_from_path[di] != (char)var_trkRowIndex) { di++; goto L2B6E; }
	if (var_AD4[di] != (uint8_t)si) { di++; goto L2B6E; }
	if (var_398[di] == (uint8_t)var_connCheckFlag) goto L2B2E;
	var_trackErrorCode = TRACK_ERR_WRONG_WAY;
	goto L25ED;
L2BA8:
	if (var_connCheckFlag < 0) goto L2C99;
	if (var_2 != 0) goto L2BCA;
	var_subTOIBlock = (uint8_t)si;
	var_MconnStatus = (uint8_t)var_connCheckFlag;
	goto L2C96;
L2BCA:
	if (var_746 == 0x40) {
		var_trackErrorCode = TRACK_ERR_MANY_PATH;
		goto L25ED;
	}
	/* Push a branch point. */
	var_MinternalTOI1 = &tcomp[var_746];
	var_MinternalTOI1->colIndex       = (uint8_t)var_trkColIndex;
	var_MinternalTOI1->rowIndex       = (uint8_t)var_trkRowIndex;
	var_MinternalTOI1->tileElem       = var_tileElem;
	var_MinternalTOI1->subTOIBlock    = (uint8_t)si;
	var_MinternalTOI1->connStatus     = (uint8_t)var_connCheckFlag;
	var_MinternalTOI1->prevConnCode   = var_prevConnCode;
	var_MinternalTOI1->f3AC           = var_3AC;
	var_MinternalTOI1->f3A8           = var_3A8;
	var_MinternalTOI1->prevColIndex   = var_MprevColIndex;
	var_MinternalTOI1->prevRowIndex   = var_MprevRowIndex;
	var_MinternalTOI1->prevTileElem   = var_MprevTileElem;
	var_MinternalTOI1->f3AA           = var_3AA;
	var_MinternalTOI1->prevConnStatus = var_MprevConnStatus;
	var_746++;
L2C96:
	var_2++;
L2C99:
	si++;
L2C9A:
	/* Loop over the element's connected TOInfo blocks. */
	if ((int16_t)(uint8_t)var_ptrTOInfo->si_noOfBlocks <= si) goto L2CE2;
	var_connCheckFlag = -1;
	var_ptrCurrTOInfo = &var_ptrTOInfo[si];
	if (var_ptrCurrTOInfo->si_entryPoint != (char)var_tileEntryPoint) goto L2AE6;
	if (var_ptrCurrTOInfo->si_entryType == (char)var_prevConnCode) goto L2ADE;
L2CD9:
	var_trackErrorCode = TRACK_ERR_ELEM_MISM;
	goto L25ED;
L2CE2:
	if (var_2 != 0) goto L2908;
	if (var_prevConnCode != 1) goto L286C;
	if (var_4 >= 2) goto L286C;
	if (var_3A8 < 2) {
		var_trackErrorCode = TRACK_ERR_NO_RUNWAY;
		goto L25ED;
	}
	var_3A8++;
	var_4++;
	ax = var_trackDirection;
	if (ax == 0)     goto L2D2C;
	if (ax == 0x100) goto L2D56;
	if (ax == 0x200) goto L2D44;
	if (ax == 0x300) goto L2D68;
	goto L2849;
L2D2C:
	var_trkColIndex = (int8_t)var_MprevColIndex;
	al = (uint8_t)(var_MprevRowIndex - var_4 - 1);
	goto L2D3C;
L2D44:
	var_trkColIndex = (int8_t)var_MprevColIndex;
	al = (uint8_t)(var_MprevRowIndex + var_4 + 1);
L2D3C:
	var_trkRowIndex = (int8_t)al;
	goto L2849;
L2D56:
	var_trkRowIndex = (int8_t)var_MprevRowIndex;
	al = (uint8_t)(var_MprevColIndex + var_4 + 1);
	goto L2D78;
L2D68:
	var_trkRowIndex = (int8_t)var_MprevRowIndex;
	al = (uint8_t)(var_MprevColIndex - var_4 - 1);
L2D78:
	var_trkColIndex = (int8_t)al;
	goto L2849;

	/* ---------------------------------------------------------------- */
	/* Path walk finished: lay out the opponent's target points.         */
	/* ---------------------------------------------------------------- */
L2D80:
	if (var_AE8 == 0) {
		var_trackErrorCode = TRACK_ERR_NO_PATH;
		goto L25ED;
	}
	byte_45D90 = (uint8_t)startcol2;
	byte_45E16 = (uint8_t)startrow2;
	si = (int16_t)(track_pieces_counter / 3);
	if (si > 0x40) si = 0x40;
	byte_4616E = (uint8_t)si;
	for (si = 0; si < 0x385; si++)         /* LAB_1e1a_2db6 */
		var_AD4[si] = 0;                   /* re-used as a per-tile visited flag */
	di = 0;
	si = 0;
	goto L34E8;

	/* Record the tile we just walked onto. */
L2DCA:
	var_4 = 0;
	var_738[trackrows[var_trkRowIndex] + var_trkColIndex] = 1;
	var_AD4[track_pieces_counter] = var_subTOIBlock;
	var_398[track_pieces_counter] = var_MconnStatus;
	if (var_3AC != -1) {
		if (td01_track_file_cpy[var_3AC] == -1)
			td01_track_file_cpy[var_3AC] = track_pieces_counter;
		else
			td02_penalty_related[var_3AC] = track_pieces_counter;
	}
/* L2E40: */
	var_3AC = track_pieces_counter;
	td21_col_from_path[track_pieces_counter] = (char)var_trkColIndex;
	td22_row_from_path[track_pieces_counter] = (char)var_trkRowIndex;
	trackdata18[track_pieces_counter] =
		(char)(uint8_t)((var_MconnStatus << 4) + var_subTOIBlock);
	td17_trk_elem_ordered[track_pieces_counter] = (char)var_tileElem;

	var_ptrTOInfo = trkObjectList[var_tileElem].ss_trkObjInfoPtr;
	var_ptrCurrTOInfo = &var_ptrTOInfo[var_subTOIBlock];
	var_74C = (uint8_t)var_ptrCurrTOInfo->si_opp3;
	if (var_74C == 0) {
		var_3A8++;
		goto L31C7;
	}
	if (var_74C == 0xFF) goto L31C2;
	if (var_3A8 <= 3) goto L31C2;
	if (byte_45635 == 0x30) goto L31C2;

	var_ptrTOInfo = trkObjectList[var_tileElem].ss_trkObjInfoPtr;
	var_ptrCurrTOInfo = &var_ptrTOInfo[var_subTOIBlock];
	var_74C = (uint8_t)var_ptrCurrTOInfo->si_opp3;

	var_ptrTOInfo = trkObjectList[var_MprevTileElem].ss_trkObjInfoPtr;
	var_ptrCurrTOInfo = &var_ptrTOInfo[var_3AA];
	if (var_MprevConnStatus != 0 &&
	    shapeinfo_opp_ptr[var_ptrCurrTOInfo - shapeinfos] != 0)
		var_ADE = shapeinfo_opp_ptr[var_ptrCurrTOInfo - shapeinfos];
	else
		var_ADE = (const uint8_t*)var_ptrCurrTOInfo->si_cameraDataOffset;
	{
		/* ax = si_arrowType*12 + var_ADE, then +0Ch or +6, then movsw x3 */
		const uint8_t* p = var_ADE +
			(uint16_t)(uint8_t)var_ptrCurrTOInfo->si_arrowType * 12 +
			(var_MprevConnStatus != 0 ? 0x0C : 0x06);
		var_ADA = ldw(p, 0);
		var_AD8 = ldw(p, 2);
		var_AD6 = ldw(p, 4);
	}
	if (var_MconnStatus != 0)
		var_74C = byte_3E724[var_74C];
	else
		var_74C = byte_3E71E[var_74C];

	var_trackDirection = var_ptrCurrTOInfo->si_arrowOrient;
	if (var_trackDirection == 0x100) {
		var_3A2 = var_ADA;
		var_ADA = var_AD6;
		var_AD6 = (int16_t)(-(uint16_t)var_3A2);
	} else if (var_trackDirection == 0x200) {
		var_AD6 = (int16_t)(-(uint16_t)var_AD6);
		var_ADA = (int16_t)(-(uint16_t)var_ADA);
	} else if (var_trackDirection == 0x300) {
		var_3A2 = var_ADA;
		var_ADA = (int16_t)(-(uint16_t)var_AD6);
		var_AD6 = var_3A2;
	}
/* L303E: */
	td08_direction_related[byte_45635] =
		(var_MprevConnStatus != 0) ? (int16_t)(var_trackDirection ^ 0x0200)
		                           : var_trackDirection;
	trackdata23[byte_45635] = var_74C;
	if (td15_terr_map_main[terrainrows[var_MprevRowIndex] + var_MprevColIndex] == 6)
		var_AD8 = (int16_t)(var_AD8 + 0x1C2);
	td10_track_check_rel[byte_45635 * 3 + 1] = var_AD8;

	ax = (trkObjectList[var_MprevTileElem].ss_multiTileFlag & 1)
	   ? trackpos[var_MprevRowIndex] : trackcenterpos[var_MprevRowIndex];
	td10_track_check_rel[byte_45635 * 3 + 2] = (int16_t)(ax + var_AD6);

	ax = (trkObjectList[var_MprevTileElem].ss_multiTileFlag & 2)
	   ? trackpos2[var_MprevColIndex + 1] : trackcenterpos2[var_MprevColIndex];
	td10_track_check_rel[byte_45635 * 3] = (int16_t)(ax + var_ADA);

	trackdata19[trackrows[var_MprevRowIndex] + var_MprevColIndex] = byte_45635;
	byte_45635++;
L31C2:
	var_3A8 = 0;
L31C7:
	track_pieces_counter++;
	if (track_pieces_counter == 0x385) {
		var_trackErrorCode = TRACK_ERR_MANY_ELEM;
		goto L25ED;
	}
	var_ptrTOInfo = trkObjectList[var_tileElem].ss_trkObjInfoPtr;
	var_ptrCurrTOInfo = &var_ptrTOInfo[var_subTOIBlock];
	if (var_MconnStatus != 0) {
		var_McurrExitPoint = (uint8_t)var_ptrCurrTOInfo->si_entryPoint;
		al = (uint8_t)var_ptrCurrTOInfo->si_entryType;
	} else {
		var_McurrExitPoint = (uint8_t)var_ptrCurrTOInfo->si_exitPoint;
		al = (uint8_t)var_ptrCurrTOInfo->si_exitType;
	}
/* L3232: end of the thread; the big loop over track elements cycles here */
	var_prevConnCode    = al;
	var_MprevColIndex   = (uint8_t)var_trkColIndex;
	var_MprevRowIndex   = (uint8_t)var_trkRowIndex;
	var_MprevConnStatus = var_MconnStatus;
	var_3AA             = var_subTOIBlock;
	var_MprevTileElem   = var_tileElem;

	ax = (int16_t)((int16_t)var_McurrExitPoint - 1);
	if ((uint16_t)ax > 0x0B) goto L2849;   /* jbe / else fall through */
	/* jmp word ptr cs:off_1F896[bx] - the 12 entries in source order */
	switch (ax) {
	case 0:  goto L3274;   case 1:  goto L329E;   case 2:  goto L32B6;
	case 3:  goto L328E;   case 4:  goto L3280;   case 5:  goto L328A;
	case 6:  goto L32C2;   case 7:  goto L32CC;   case 8:  goto L32D4;
	case 9:  goto L329A;   case 10: goto L32AE;   case 11: goto L32AA;
	}
	goto L2849;
L3274:
	var_trkRowIndex--;
L3278:
	var_trackDirection = 0x000;
	goto L2849;
L3280:
	var_trkRowIndex--;
	var_trkColIndex++;
	goto L3278;
L328A:
	var_trkRowIndex++;
L328E:
	var_trkColIndex--;
	var_trackDirection = 0x300;
	goto L2849;
L329A:
	var_trkColIndex++;
L329E:
	var_trkRowIndex++;
L32A2:
	var_trackDirection = 0x200;
	goto L2849;
L32AA:
	var_trkColIndex++;
L32AE:
	var_trkRowIndex = (int8_t)(var_trkRowIndex + 2);
	goto L32A2;
L32B6:
	var_trkColIndex++;
L32BA:
	var_trackDirection = 0x100;
	goto L2849;
L32C2:
	var_trkColIndex++;
L32C6:
	var_trkRowIndex++;
	goto L32BA;
L32CC:
	var_trkColIndex = (int8_t)(var_trkColIndex + 2);
	goto L32BA;
L32D4:
	var_trkColIndex = (int8_t)(var_trkColIndex + 2);
	goto L32C6;

	/* ---------------------------------------------------------------- */
	/* Opponent target points, sampled evenly along the path.            */
	/* ---------------------------------------------------------------- */
L32F8:
	/* var_ptrCurrTOInfo holds the element's BASE TOInfo here (set at L34E8);
	 * the block is base[subTOIBlock]. */
	var_ADE = (const uint8_t*)
		var_ptrCurrTOInfo[var_subTOIBlock].si_cameraDataOffset;
L3311:
	{
		struct TRKOBJINFO* blk = &var_ptrCurrTOInfo[var_subTOIBlock];
		const uint8_t* p = var_ADE + (uint16_t)(uint8_t)blk->si_arrowType * 12;
		var_ADA = ldw(p, 0);
		var_AD8 = ldw(p, 2);
		var_AD6 = ldw(p, 4);
		var_trackDirection = blk->si_arrowOrient;
	}
	if (var_trackDirection == 0x100) {
		var_3A2 = var_ADA;
		var_ADA = var_AD6;
		var_AD6 = (int16_t)(-(uint16_t)var_3A2);
	} else if (var_trackDirection == 0x200) {
		var_AD6 = (int16_t)(-(uint16_t)var_AD6);
		var_ADA = (int16_t)(-(uint16_t)var_ADA);
	} else if (var_trackDirection == 0x300) {
		var_3A2 = var_ADA;
		var_ADA = (int16_t)(-(uint16_t)var_AD6);
		var_AD6 = var_3A2;
	}
/* L338A: */
	((int16_t far*)trackdata7)[di] =
		(td15_terr_map_main[terrainrows[var_trkRowIndex] + var_trkColIndex] == 6)
		? 0x1C2 : 0;
/* L33FF: */
	((int16_t far*)trackdata6)[di] = 0;
	trackdata9[di * 3 + 1] =
		(int16_t)(((int16_t far*)trackdata7)[di] + var_AD8);

	ax = (trkObjectList[var_C].ss_multiTileFlag & 1)
	   ? trackpos[var_trkRowIndex] : trackcenterpos[var_trkRowIndex];
	trackdata9[di * 3 + 2] = (int16_t)(ax + var_AD6);

	ax = (trkObjectList[var_C].ss_multiTileFlag & 2)
	   ? trackpos2[var_trkColIndex + 1] : trackcenterpos2[var_trkColIndex];
	trackdata9[di * 3] = (int16_t)(ax + var_ADA);
	di++;
L34E7:
	si++;
L34E8:
	if ((int16_t)byte_4616E <= si) goto L35CE;
	/* mov ax,track_pieces_counter ; imul si ; cwd ; idiv cx
	 * [ODDITY] the cwd throws away DX from the imul, so the product is
	 * truncated to 16 bits before the divide. Reproduced deliberately. */
	ax = (int16_t)((uint16_t)track_pieces_counter * (uint16_t)si);
	var_A = (int16_t)(ax / (int16_t)byte_4616E);
	var_trkColIndex = (int8_t)td21_col_from_path[var_A];
	var_trkRowIndex = (int8_t)td22_row_from_path[var_A];
	if (var_AD4[terrainrows[var_trkRowIndex] + var_trkColIndex] != 0) goto L34E7;
	var_AD4[terrainrows[var_trkRowIndex] + var_trkColIndex] = 1;
	var_C = (int16_t)(uint8_t)td17_trk_elem_ordered[var_A];
	var_subTOIBlock   = (uint8_t)(trackdata18[var_A] & 0x0F);
	var_connCheckFlag = (int8_t)(trackdata18[var_A] & 0x10);
	var_ptrCurrTOInfo = trkObjectList[var_C].ss_trkObjInfoPtr;
	if (var_connCheckFlag == 0) goto L32F8;
	/* mov ax,[bx+0Ah] - the si_opp1/si_opp2 pair read as one word, which is
	 * a relocated pointer here exactly as in sub_18D60. */
	var_ADE = shapeinfo_opp_ptr[&var_ptrCurrTOInfo[var_subTOIBlock] - shapeinfos];
	if (var_ADE == 0) goto L32F8;
	goto L3311;
L35CE:
	byte_4616E = (uint8_t)di;
	var_trackErrorCode = TRACK_OK;
	goto L360E;

	/* ---------------------------------------------------------------- */
L35DA:
	if (var_trkColIndex == 0x1E) var_trkColIndex = 0x1D;
L35E6:
	if ((uint8_t)var_trkRowIndex == 0xFF) {
		var_trkRowIndex = 0;
	} else if (var_trkRowIndex == 0x1E) {
		var_trkRowIndex = 0x1D;
	}
/* L3600: */
	byte_45D90 = (uint8_t)var_trkColIndex;
	byte_45E16 = (uint8_t)var_trkRowIndex;
L360E:
	mmgr_release((char far*)tcomp);
	return (int16_t)(int8_t)var_trackErrorCode;
}
