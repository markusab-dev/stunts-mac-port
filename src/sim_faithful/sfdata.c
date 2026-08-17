/*
 * sfdata.c - Data-segment definitions the vendored simulation needs that are
 * not already provided by the renderer port's rdata.c.
 *
 * Values transcribed from reference/restunts/src/restunts/asm/dseg.asm.
 * Everything here is `dw 0` / `dd 0` / `db 0` in the original except the four
 * lookup tables at the top, whose contents are copied verbatim.
 */
#include <stdint.h>
#include "../render_faithful/externs.h"

/* ------------------------------------------------------------------
 * Initialised lookup tables (dseg 0x3BD5A..0x3BD79).
 * Stored as bytes in the original; each entry is a 16-bit value in a
 * byte array, hence the interleaved zeros.
 * ------------------------------------------------------------------ */
char unk_3BD5A[8] = { 5, 0, 40, 0, 5, 0, 10, 0 };
char unk_3BD62[8] = { 6, 0, 121, 0, 6, 0, 9, 0 };
char unk_3BD6A[8] = { 1, 0, 10, 0, 1, 0, 10, 0 };
int16_t word_3BD72[4] = { 21, 21, 15, 15 };


/* ------------------------------------------------------------------
 * Physics lookup tables (dseg.asm), transcribed verbatim.
 * ------------------------------------------------------------------ */

/* Grass/off-road deceleration divisors, indexed by surface type. */
int16_t grassDecelDivTab[5] = { 0x00FF, 0x0100, 0x00C0, 0x0080, 0x0040 };

/* Steering-wheel response curves: 4-byte groups of signed deltas, one set
 * per tick rate. restunts.c picks the 20 fps table unless framespersec==10. */
static const char s_steerResp10[62] = {
	0, 16, 240, 0, 0, 14, 242, 0, 0, 12, 244, 0, 0, 10, 246, 0,
	0, 8, 248, 0, 0, 8, 248, 0, 0, 6, 250, 0, 0, 6, 250, 0,
	0, 4, 252, 0, 0, 4, 252, 0, 0, 4, 252, 0, 0, 2, 254, 0,
	0, 2, 254, 0, 0, 1, 255, 0, 0, 1, 255, 0, 0, 1
};
static const char s_steerResp20[64] = {
	0, 8, 248, 0, 0, 7, 249, 0, 0, 6, 250, 0, 0, 5, 251, 0,
	0, 4, 252, 0, 0, 4, 252, 0, 0, 3, 253, 0, 0, 3, 253, 0,
	0, 2, 254, 0, 0, 2, 254, 0, 0, 2, 254, 0, 0, 1, 255, 0,
	0, 1, 255, 0, 0, 1, 255, 0, 0, 1, 255, 0, 0, 1, 255, 0
};
/* externs.h types these as void*; the underlying dseg storage is a byte
 * table, so the tables live in static arrays and these alias their start. */
void* steerWhlRespTable_10fps = (void*)s_steerResp10;
void* steerWhlRespTable_20fps = (void*)s_steerResp20;
void* steerWhlRespTable_ptr   = (void*)s_steerResp20;

/* ------------------------------------------------------------------
 * Frame/state working variables (all `dw 0` / `dd 0` in dseg).
 * The gState_* set is a scratch copy the update path works through
 * before committing into `struct GAMESTATE state`.
 * ------------------------------------------------------------------ */
int16_t gState_144;
uint16_t gState_frame;   /* externs.h declares this unsigned */
int16_t gState_impactSpeed;
int16_t gState_jumpCount;
int16_t gState_oEndFrame;
int16_t gState_pEndFrame;
int16_t gState_penalty;
int16_t gState_topSpeed;
int16_t gState_total_finish_time;
int32_t gState_travDist;

int32_t pState_lvec1_x;
int32_t pState_lvec1_y;
int32_t pState_lvec1_z;
int16_t pState_minusRotate_x_1;
int16_t pState_minusRotate_y_1;
int16_t pState_minusRotate_z_1;

int16_t penalty_time;
int16_t nextPosAndNormalIP;      /* dseg: dw 0 */
char show_penalty_counter;       /* dseg: db 0 */
int16_t word_43964;
int16_t word_4408C[513];
char byte_43966;
char byte_459D8[2];
char is_in_replay;       /* externs.h: single char (dseg reserves 2) */
char oppnentSped[16];

/* dseg has `framespersec dw 0` followed by framespersec2 and one more word.
 * Set to the original 20 Hz tick rate (restunts.c: framespersec = 20). */
uint16_t framespersec = 20;
uint16_t framespersec2 = 20;

/* ------------------------------------------------------------------
 * Track tables normally carved out of the "trakdata" chunk by
 * track_setup() (seg004.asm, not yet ported).
 *
 * [STUB] These are far pointers in the original (`dd 0`). They drive lap /
 * checkpoint / penalty bookkeeping, not the per-frame vehicle dynamics, so
 * neutral storage lets the physics run while that logic stays inert.
 * Sizes are the ones init_trackdata() reserves (0x70A bytes each).
 * ------------------------------------------------------------------ */
static int16_t s_td01[0x70A / 2];
static int16_t s_td02[0x70A / 2];
/* trackdata3: another trakdata slice track_setup() fills (dseg: dd 0).
 * Declared char* in externs.h. NULL until the real chunk is loaded. */
char far* trackdata3;

int16_t far* td01_track_file_cpy = s_td01;
int16_t far* td02_penalty_related = s_td02;

void sfdata_init_track_tables(void)
{
	/* -1 is the original's "no segment here" sentinel (see state.c, which
	 * tests `td02_penalty_related[...] == -1`). */
	for (unsigned i = 0; i < sizeof(s_td01) / sizeof(s_td01[0]); i++) {
		s_td01[i] = -1;
		s_td02[i] = -1;
	}
}

/* ------------------------------------------------------------------
 * dseg objects newly required by the sub_18D60 / bto_auxiliary1
 * translations in sfasm_port.c.  Every value below is the LITERAL dseg
 * initial value from reference/restunts/src/restunts/asm/dseg.asm --
 * nothing is derived, guessed or filled in.
 * ------------------------------------------------------------------ */

/* dseg.asm 40125: `word_45D3E dw 0`, immediately followed on line 40126 by
 * `trackrows dw 0`.  bto_auxiliary1 indexes `word_45D3E[i*2]`, which is
 * therefore trackrows[i-1] for i >= 1 and this word itself for i == 0 (the
 * asm listing carries exactly that comment).  Never written by
 * init_row_tables (which starts at trackrows[0]), so it stays 0. */
int16_t word_45D3E;

/* dseg.asm 13272..13385: element-dependent VECTOR tables, `db` storage read
 * as little-endian int16 triples.  Byte counts (6/48/12/12/12/24) equal
 * 6 * the entry counts bto_auxiliary1 loads into DI (1/8/2/2/2/4).
 * Bytes transcribed verbatim, one `db` per value, in source order. */
const uint8_t unk_3E640[6] = {
	0, 0, 0, 0, 0, 0,
};
const uint8_t unk_3E646[48] = {
	136, 255, 0, 0, 231, 254,   136, 255, 0, 0,  25, 255,
	136, 255, 0, 0,  25,   1,   136, 255, 0, 0, 231,   0,
	120,   0, 0, 0, 231, 254,   120,   0, 0, 0,  25, 255,
	120,   0, 0, 0,  25,   1,   120,   0, 0, 0, 231,   0,
};
const uint8_t unk_3E676[12] = {
	196, 255, 0, 0, 0, 254,    60, 0, 0, 0, 0, 2,
};
const uint8_t unk_3E682[12] = {
	120, 254, 0, 0, 0, 0,     136, 253, 0, 0, 0, 0,
};
const uint8_t unk_3E68E[12] = {
	136, 1, 0, 0, 0, 0,       120, 2, 0, 0, 0, 0,
};
const uint8_t unk_3E69A[24] = {
	 23,   0, 0, 0,   1, 255,    97,   0, 0, 0,   1, 255,
	159, 255, 0, 0, 255,   0,   233, 255, 0, 0, 255,   0,
};

/* ------------------------------------------------------------------
 * [BLOCKED] trackdata slots 17/18/21/22.
 *
 * dseg.asm declares all four as `dd 0`; they are carved out of the
 * "trakdata" allocation by init_trackdata() and then FILLED IN by
 * track_setup() (seg004.asm), which this port does not have.  The DOS
 * oracle gets them because tools/oracle/repldrv.asm calls the real
 * track_setup.  Defined here at their dseg initial value (NULL) so the
 * tree links; sub_18D60 dereferences them on its first instruction, so it
 * cannot run until something populates them.  No content is invented.
 * ------------------------------------------------------------------ */
char far* td17_trk_elem_ordered;
char far* trackdata18;
char far* td21_col_from_path;
char far* td22_row_from_path;

/* The remaining trakdata slices track_setup() fills. dseg.asm declares these
 * `dd 0` too; init_trackdata() carves them out of the same allocation.
 * trackdata6/7 are declared `char far*` in externs.h but the original writes
 * WORDS through them, so sftrack_setup.c casts at the point of use. */
char far* trackdata6;
char far* trackdata7;

/* Phase 3. init_trackdata() carves these out of the same allocation:
 * td11_highscores is the 7 x 52-byte record table a .HIG file holds and
 * td13_rpl_header the 26-byte GAMEINFO a .RPL starts with. Both were
 * commented-out placeholders until the high-score and replay code landed. */
char far* td11_highscores;
char far* td13_rpl_header;
char far* td16_rpl_buffer;

/* ------------------------------------------------------------------ */
/* init_trackdata (restunts.c:262) - carve the one "trakdata" block into
 * the 23 slices the game addresses by name. Offsets transcribed from the
 * original; the running totals are kept in the comments so the layout can
 * be checked against restunts.c line by line.
 * ------------------------------------------------------------------ */
void sfdata_init_trackdata(void)
{
	static uint8_t blk[0x6BF3];
	uint8_t* p = blk;
	memset(blk, 0, sizeof(blk));
	td01_track_file_cpy    = (int16_t far*)(p + 0x0000);
	td02_penalty_related   = (int16_t far*)(p + 0x070A);
	trackdata3             = (char far*)   (p + 0x0E14);
	/* td04_aerotable_pl 0x151E, td05_aerotable_op 0x159E: owned elsewhere */
	trackdata6             = (char far*)   (p + 0x161E);
	trackdata7             = (char far*)   (p + 0x169E);
	td08_direction_related = (int16_t far*)(p + 0x171E);
	trackdata9             = (int16_t far*)(p + 0x177E);
	td10_track_check_rel   = (int16_t far*)(p + 0x18FE);
	td11_highscores        = (char far*)   (p + 0x1A1E);  /* 0x16C bytes */
	/* trackdata12 0x1B8A */
	td13_rpl_header        = (char far*)   (p + 0x1C7A);  /* 0x1A bytes  */
	td14_elem_map_main     =               (p + 0x1C94);
	td15_terr_map_main     =               (p + 0x2019);
	td16_rpl_buffer        = (char far*)   (p + 0x239E);  /* 0x2EE0 = 12000 */
	td17_trk_elem_ordered  = (char far*)   (p + 0x527E);
	trackdata18            = (char far*)   (p + 0x5603);
	trackdata19            =               (p + 0x5988);
	/* td20_trk_file_appnd 0x5D0D */
	td21_col_from_path     = (char far*)   (p + 0x64B9);
	td22_row_from_path     = (char far*)   (p + 0x683E);
	trackdata23            =               (p + 0x6BC3);
	/* + 0x30 = 0x6BF3 total */
}
