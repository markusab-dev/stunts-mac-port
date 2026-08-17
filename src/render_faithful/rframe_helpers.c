/*
 * rframe_helpers.c - Instruction-exact C translations of five remaining asm
 * functions used by the ported renderer frame loop (frame.c):
 *
 *   1. skybox_op_helper2            seg003.asm lines 3792-3941
 *   2. skybox_op                    seg003.asm lines 3942-4705
 *   3. transformed_shape_add_for_sort seg003.asm lines 4706-4759
 *   4. build_track_object           seg004.asm lines 61-2755
 *   5. subst_hillroad_track         seg004.asm lines 6109-6275
 *
 * Translation rules (matching rasm_port.c):
 *   - 16-bit registers are modelled as int16_t/uint16_t locals named after
 *     the registers where the flow needs them.
 *   - jl/jg/jle/jge are signed compares (int16_t); jb/ja/jbe/jnb unsigned.
 *   - Original label names are preserved as C labels with goto structure.
 *   - Nontrivial asm lines are kept as comments beside their C translation.
 *   - "jmp <label>" where <label> is only a conditional jump that consumes
 *     flags from a *different* compare at each jump site (e.g. loc_1E5E5,
 *     loc_1E8DC, loc_1C56A) is translated by duplicating the conditional at
 *     each site.
 *   - 16x16 imul followed by cwd+idiv discards the high product word
 *     (cwd overwrites dx), so those are translated as
 *     (int16_t)(a * b) / c  -- exactly the original behaviour.
 *   - The MSC longint helpers __aFlmul/__aFldiv implement
 *     (int32)a * (int32)b / (int32)c with truncating signed division;
 *     x86 idiv also truncates toward zero, as does C, so plain int32_t
 *     arithmetic is exact.
 *
 * External drawing calls made by skybox_op / skybox_op_helper2:
 *   - sprite_set_1_size / sprite_clear_1_color  (already in C, externs.h)
 *   - skybox_op_helper                          (already in C, shape3d.c)
 *   - draw_line_related                         (already in C, shape3d.c ->
 *                                                rasm_port.c)
 *   - rect_intersect / rectlist_add_rects       (already in C, math.c)
 *   - sprite_putimage_and_alt (seg012.asm line 11762, video driver bitmap
 *     blitter: args = far ptr to SHAPE2D bitmap, x, y).  NOT yet ported to
 *     C; every call is replaced by the stub rstub_skybox_bitmap_blit()
 *     declared below.  This affects ONLY the mountain-panorama bitmap path
 *     in skybox_op_helper2 (loc_1C392..loc_1C432); the polygon/solid-fill
 *     paths are fully functional.
 */

#include <stdint.h>
#include "externs.h"
#include "rfbsize.h"
#include "shape3d.h"

/* ------------------------------------------------------------------ */
/* Prototypes (matching the declarations in frame.c)                   */
/* ------------------------------------------------------------------ */
void skybox_op_helper2(struct RECTANGLE* arg_rectptr, int16_t arg_2, int16_t arg_4);
int16_t skybox_op(int16_t arg_0, struct RECTANGLE* arg_rectptr, int16_t arg_4,
                  struct MATRIX* arg_6, int16_t arg_8, int16_t arg_A, int16_t arg_C);
void transformed_shape_add_for_sort(int16_t arg_zadjust, int16_t arg_2);
void build_track_object(struct VECTOR* arg_posWorldCrds, struct VECTOR* arg_nextPosWorldCrds);
uint8_t subst_hillroad_track(uint8_t arg_0, uint8_t arg_2);

/* ------------------------------------------------------------------ */
/* External functions                                                  */
/* ------------------------------------------------------------------ */
/* shape3d.c */
extern void skybox_op_helper(uint16_t arg_color, uint16_t arg_vertlinecount,
                             struct POINT2D arg_vertlines[]);
extern uint16_t draw_line_related(uint16_t, uint16_t, uint16_t, uint16_t, int16_t*);
/* math.c */
extern void rectlist_add_rects(char arg_rectcount, char* arg_rectarray_indices,
	struct RECTANGLE* arg_rectarray1, struct RECTANGLE* arg_rectarray2,
	struct RECTANGLE* arg_rectptr, char* arg_rect_array_length_ptr,
	struct RECTANGLE* arg_rect_array_ptr);
/*
 * Stub for the 2D bitmap panorama blitter.  The original calls
 *   sprite_putimage_and_alt(word ptr skyboxes[n], word ptr skyboxes[n]+2, x, y)
 * (far pointer to a SHAPE2D mountain bitmap, x position along the unrolled
 * panorama, y position of the horizon slice).  seg012.asm line 11762.
 * To be replaced with the real blitter when the 2D bitmap path is ported.
 */
extern void rstub_skybox_bitmap_blit(void far* shape2d_farptr, int16_t x, int16_t y);

/* ------------------------------------------------------------------ */
/* Data segment variables.                                             */
/* Types follow existing declarations in frame.c/externs.h; symbols    */
/* that exist only in dseg.asm use the dw/db/dd storage size.          */
/* Definitions are provided by rdata.c (generated separately).         */
/* ------------------------------------------------------------------ */

/* --- skybox --- */
extern int16_t skybox_current;         /* dw */
extern int16_t skybox_sky_color;       /* dw */
extern int16_t skybox_grd_color;       /* dw */
extern int16_t word_454CE;             /* dw - skybox bitmap slice height */
extern int16_t skybox_ptr1;            /* dw */
extern int16_t skybox_ptr2;            /* dw */
extern int16_t skybox_ptr3;            /* dw */
extern int16_t skybox_ptr4;            /* dw */
extern void far* skyboxes[4];          /* rskybox.c */
extern void sprite_putimage_and_alt(void far* shape, int16_t x, int16_t y);
extern struct RECTANGLE rect_skybox;   /* 4 x dw */
extern struct RECTANGLE rect_array_unk3[];
extern char rect_array_unk3_length;    /* db */
extern char rect_array_unk_indices[];  /* 15 x db */
extern struct RECTANGLE rect_unk[];    /* declared in frame.c */
extern int16_t word_449FC[];           /* declared in frame.c */
extern int16_t word_463D6;             /* declared in frame.c */
/* byte_454A4, rectptr_unk, detail_level, slow_video_mgmt_copy,
 * video_flag3_isFFFF come from externs.h */

/* --- transformed shape sort (declared in frame.c) --- */
extern struct MATRIX mat_temp;
extern struct TRANSFORMEDSHAPE3D* curtransshape_ptr;
extern int16_t transformedshape_zarray[];
extern int16_t transformedshape_indices[];
extern char transformedshape_arg2array[];
extern char transformedshape_counter;

/* --- build_track_object --- */
extern int16_t planindex;              /* declared in frame.c */
/* TEMPORARY DIAGNOSTIC - remove after the frame-64 investigation */
int16_t dbg_model = -99, dbg_terrain = -99, dbg_plan = -99, dbg_col = -99, dbg_row = -99;
int16_t dbg_bto_elem0, dbg_bto_elem1, dbg_bto_terr;
/* One row per build_track_object() call; the simulation makes one call per
 * wheel, so a frame's four rows show what surface each wheel landed on. */
int16_t dbg_bto[8][8];
int     dbg_bto_n;
extern int16_t terrainHeight;          /* declared in frame.c */
extern char byte_4392C;                /* declared in frame.c */
extern struct TRACKOBJECT trkObjectList[215]; /* declared in frame.c */
extern int16_t wallindex;              /* dw */
extern int16_t wallHeight;             /* dw */
extern int16_t elRdWallRelated;        /* dw */
extern int16_t wallOrientation;        /* dw */
extern int16_t wallStartX;             /* dw */
extern int16_t wallStartZ;             /* dw */
extern char corkFlag;                  /* db */
extern char current_surf_type;         /* db: 4 = grass, 5 = water (structs.inc) */
extern int16_t elem_xCenter;           /* dw */
extern int16_t elem_zCenter;           /* dw */
extern struct PLANE far* planptr;      /* dd */
extern struct PLANE far* current_planptr; /* dd */
/*
 * wallptr (dd) points to an array of 6-byte wall records:
 *   word 0 = wall angle, word 1 = start X, word 2 = start Z
 * Accessed in the original as es:[wallindex*6 + {0,2,4}].
 */
extern int16_t far* wallptr;
extern int16_t highEntrZBounds0[];     /* dw arrays */
extern int16_t highEntrZBounds1[];
extern int16_t highEntrXInnBounds0[];
extern int16_t highEntrXInnBounds1[];
extern int16_t highEntrXOutBounds0[];
extern int16_t highEntrXOutBounds1[];
extern int16_t bkRdEntr_triang_zAdjust[];
extern int16_t loopSurface_maxZ;
extern int16_t loopSurface_ZBounds0[];
extern int16_t loopSurface_ZBounds1[];
extern int16_t loopSurface_XBounds0[];
extern int16_t loopSurface_XBounds1[];
extern int16_t loopBase_ZBounds0[];
extern int16_t loopBase_ZBounds1[];
extern int16_t loopBae_InnXBounds0[];  /* [sic] - name as in dseg.asm */
extern int16_t loopBase_InnXBounds1[];
extern int16_t loopBase_OutXBounds0[];
extern int16_t loopBase_OutXBounds1[];
extern int16_t corkLR_negZBound[];
extern int16_t corkLR_posZBound[];
/* trackrows/terrainrows/terrainpos/terraincenterpos/trackpos2/trackcenterpos2,
 * td14_elem_map_main, td15_terr_map_main, hillHeightConsts, state
 * come from externs.h */

/* ------------------------------------------------------------------ */
/* seg003.asm 3792-3941: skybox_op_helper2                             */
/*                                                                     */
/* Draws one vertical slice of the horizon: sky-colored band above the */
/* horizon line, the mountain panorama bitmaps across the horizon      */
/* (stubbed, see rstub_skybox_bitmap_blit), and ground-colored band    */
/* below.                                                              */
/*   arg_rectptr = clip rect for the slice                             */
/*   arg_2       = camera yaw angle (panorama scroll)                  */
/*   arg_4       = screen y of the horizon within this slice           */
/* ------------------------------------------------------------------ */
void skybox_op_helper2(struct RECTANGLE* arg_rectptr, int16_t arg_2, int16_t arg_4)
{
	int16_t si, di, ax;
	struct RECTANGLE* bx;

	if (detail_level == 4) goto loc_1C320;      /* cmp detail_level, 4 ; jz */
	si = arg_4;                                  /* mov si, [bp+arg_4] */
	bx = arg_rectptr;                            /* mov bx, [bp+arg_rectptr] */
	si = (int16_t)(si - bx->top);                /* sub si, [bx+RECTANGLE.rc_top] */
	si = (int16_t)(si - skybox_current);         /* sub si, skybox_current */
	goto loc_1C329;
loc_1C320:
	si = arg_4;
	bx = arg_rectptr;
	si = (int16_t)(si - bx->top);
loc_1C329:
	ax = (int16_t)(bx->bottom - bx->top);        /* mov ax,[bx+rc_bottom] ; sub ax,[bx+rc_top] */
	if (ax >= si) goto loc_1C339;                /* cmp ax, si ; jge */
	si = (int16_t)(bx->bottom - bx->top);
loc_1C339:
	if (si <= 0) goto loc_1C35F;                 /* or si, si ; jle */
	/* sky-colored band from rc_top to rc_top+si */
	sprite_set_1_size(bx->left, bx->right, bx->top, (int16_t)(bx->top + si));
	sprite_clear_1_color((uint8_t)skybox_sky_color);
loc_1C35F:
	if (detail_level != 4) goto loc_1C369;       /* cmp detail_level, 4 ; jnz */
	goto loc_1C432;
loc_1C369:
	/* panorama scroll position:
	 * mov si,[bp+arg_2] ; add si,200h ; and si,3FFh ; sub si,400h */
	si = (int16_t)(((arg_2 + 0x200) & 0x3FF) - 0x400);
	bx = arg_rectptr;
	ax = arg_4;
	if (bx->top < ax) goto loc_1C386;            /* cmp [bx+rc_top], ax ; jl */
	goto loc_1C432;
loc_1C386:
	ax = (int16_t)(ax - word_454CE);             /* sub ax, word_454CE */
	if (ax <= bx->bottom) goto loc_1C392;        /* cmp ax, [bx+rc_bottom] ; jle */
	goto loc_1C432;
loc_1C392:
	sprite_set_1_size(bx->left, bx->right, bx->top, bx->bottom);
	/*
	 * Mountain panorama: the four strips tile side by side into a band
	 * 0x400 pixels around, with the first repeated at the wrap point, and
	 * each is placed at `horizon - its own height` so it stands on the
	 * horizon rather than hanging from it. The positions are in the
	 * original's 320x200 space; sprite_putimage_and_alt scales them.
	 */
	sprite_putimage_and_alt(skyboxes[0], si,                    (int16_t)(arg_4 - skybox_ptr1));
	sprite_putimage_and_alt(skyboxes[1], (int16_t)(si + 0x140), (int16_t)(arg_4 - skybox_ptr2));
	sprite_putimage_and_alt(skyboxes[2], (int16_t)(si + 0x200), (int16_t)(arg_4 - skybox_ptr3));
	sprite_putimage_and_alt(skyboxes[3], (int16_t)(si + 0x340), (int16_t)(arg_4 - skybox_ptr4));
	sprite_putimage_and_alt(skyboxes[0], (int16_t)(si + 0x400), (int16_t)(arg_4 - skybox_ptr1));
loc_1C432:
	bx = arg_rectptr;
	ax = arg_4;
	if (bx->top <= ax) goto loc_1C442;           /* cmp [bx+rc_top], ax ; jle */
	di = bx->top;
	goto loc_1C445;
loc_1C442:
	di = arg_4;
loc_1C445:
	si = (int16_t)(bx->bottom - di);             /* mov si,[bx+rc_bottom] ; sub si,di */
	if (si <= 0) goto loc_1C46D;                 /* or si, si ; jle */
	/* ground-colored band from di to di+si */
	sprite_set_1_size(bx->left, bx->right, di, (int16_t)(di + si));
	sprite_clear_1_color((uint8_t)skybox_grd_color);
loc_1C46D:
	return;
}

/* ------------------------------------------------------------------ */
/* seg003.asm 3942-4705: skybox_op                                     */
/*                                                                     */
/* Draws the horizon/panorama background.                              */
/*   arg_0   = frame/page index (indexes word_449FC)                   */
/*   arg_rectptr = clip rectangle                                      */
/*   arg_4   = skybox parameter (+1 normal, -1 inverted)               */
/*   arg_6   = camera rotation matrix                                  */
/*   arg_8   = camera roll angle (0 = level horizon fast path)         */
/*   arg_A   = camera pitch angle (passed to skybox_op_helper2)        */
/*   arg_C   = camera world y                                          */
/* Returns 1 if it fully repainted the background (var_5C), else 0.    */
/* ------------------------------------------------------------------ */

/*
 * dseg word table at ds:098Ch, read by the horizon-quad construction loop
 * (seg003.asm line 4402/4425: mov ax, [bx+98Ch] with bx = var_26*2,
 * var_26 = 2..5).
 *
 * The data segment starts at linear 3B770h - both disassemblies open their
 * dseg with `word_3B770` at offset 0 - so ds:098Ch is linear 3C0FCh and the
 * four words actually read live at 3C100h..3C107h.  restunts and restunts2
 * agree on those bytes:
 *
 *     3C100h:  80 00  80 01  80 02  80 03
 *
 * that is 0080h, 0180h, 0280h, 0380h = 128, 384, 640, 896 in the game's
 * 1024-step circle: the four 45-degree diagonals, which is exactly what
 * extending a horizon line into a pair of covering half-plane quads needs.
 *
 * [FIXED 2026-08-16] This table previously held 0070h, 6570h, 006Eh, 7072h,
 * from assuming a dseg base of 3B7B0h - 40h too high, which landed the read
 * inside the string constants aOpp/aPen/aRpl.  Reduced mod 400h those gave
 * 112, 368, 110, 114: the first two near enough to 128 and 384 that the sky
 * quad still roughly covered its half, while the ground quad got two
 * directions four steps apart and collapsed to a slice of nothing.  On screen
 * that was a wedge of sky in one corner and unpainted background everywhere
 * else, but only while the camera was rolled - the level-horizon fast path
 * never reads this table, so it showed up solely in banked corners, loops and
 * corkscrews.
 */
static const uint16_t skybox_quadangle_98C[6] = {
	0x0100, 0x0101, 0x0080, 0x0180, 0x0280, 0x0380
};
int16_t skybox_op(int16_t arg_0, struct RECTANGLE* arg_rectptr, int16_t arg_4,
                  struct MATRIX* arg_6, int16_t arg_8, int16_t arg_A, int16_t arg_C)
{
	int16_t var_78buf[14];         /* var_78 (bp-120): draw_line_related edge
	                                * descriptor; var_76=word[1] (x at top),
	                                * var_72=word[3] (top y), var_6E=word[5]
	                                * (bottom y) */
	int16_t var_5C;                /* bp-92: return value */
	int16_t var_5A;                /* bp-90: near-horizontal-horizon flag */
	struct VECTOR var_58vec;       /* bp-88..-84: var_58/var_56/var_54 */
	int16_t var_skyheight;         /* bp-80 */
	struct POINT2D pts[6];         /* bp-78: var_point..var_point6 */
	int16_t var_34;                /* bp-52: strip left edge */
	int16_t var_32;                /* bp-50: horizon y at one screen edge */
	struct RECTANGLE var_rect;     /* bp-48 */
	struct RECTANGLE* var_rectptr; /* bp-40 */
	int16_t var_26;                /* bp-38: horizon dy across screen / loop ctr */
	struct VECTOR var_vec;         /* bp-36 */
	struct VECTOR var_vec2;        /* bp-30 */
	struct POINT2D quad[4];        /* stack image of skybox_op_helper args */
	int16_t si, di, ax;
	struct RECTANGLE* bx;

	rect_array_unk3_length = 0;               /* mov rect_array_unk3_length, 0 */
	var_5C = 0;
	bx = arg_rectptr;
	/* push [bx+rc_bottom] ; push [bx+rc_top] ; mov ax,140h ; push ax ;
	 * sub ax,ax ; push ax ; call sprite_set_1_size */
	sprite_set_1_size(0, RFB_VIEW_W, bx->top, bx->bottom);
	if (arg_8 != 0) goto loc_1C4A7;           /* cmp [bp+arg_8], 0 ; jnz */
	goto loc_1C958;
loc_1C4A7:
	/* mov ax, 4650h ; imul [bp+arg_4]  (low word only) */
	var_58vec.x = (int16_t)(0x4650 * arg_4);
	var_58vec.y = (int16_t)(-arg_C);          /* mov ax,[bp+arg_C] ; neg ax */
	var_58vec.z = (int16_t)(0x3A98 * arg_4);  /* mov ax, 3A98h ; imul arg_4 */
	mat_mul_vector(&var_58vec, arg_6, &var_vec);
	/* mov ax, 0B9B0h ; imul arg_4  (0B9B0h = -4650h) */
	var_58vec.x = (int16_t)(-0x4650 * arg_4);
	mat_mul_vector(&var_58vec, arg_6, &var_vec2);
	if (var_vec.z < 0) goto loc_1C4FC;        /* cmp [bp+var_vec.vz], 0 ; jl */
	if (var_vec2.z >= 0) goto loc_1C51C;      /* cmp [bp+var_vec2.vz],0 ; jge */
loc_1C4FC:
	di = skybox_sky_color;                    /* mov di, skybox_sky_color */
loc_1C500:
	bx = arg_rectptr;
	sprite_set_1_size(0, RFB_VIEW_W, bx->top, bx->bottom);
	sprite_clear_1_color((uint8_t)di);        /* push di ; jmp loc_1CB6A */
	goto loc_1CB72;
loc_1C51C:
	vector_to_point(&var_vec, &pts[0]);
	vector_to_point(&var_vec2, &pts[1]);
	if (pts[0].px <= RFB_VIEW_W) goto loc_1C558;   /* cmp var_point.x2, 140h ; jle */
	if (pts[1].px <= RFB_VIEW_W) goto loc_1C558;
	if (pts[0].py < pts[1].py) goto loc_1C4FC;/* cmp var_point.y2, var_point2.y2 ; jl */
loc_1C552:
	di = skybox_grd_color;                    /* mov di, skybox_grd_color */
	goto loc_1C500;
loc_1C558:
	if (pts[0].px >= 0) goto loc_1C56E;       /* cmp var_point.x2, 0 ; jge */
	if (pts[1].px >= 0) goto loc_1C56E;
	/* cmp var_point.y2, var_point2.y2 ; (loc_1C56A) jle grd / jmp sky */
	if (pts[0].py <= pts[1].py) goto loc_1C552;
	goto loc_1C4FC;
loc_1C56E:
	bx = arg_rectptr;
	if (bx->bottom >= pts[0].py) goto loc_1C58A; /* cmp [bx+rc_bottom], y0 ; jge */
	if (bx->bottom >= pts[1].py) goto loc_1C58A;
	/* cmp var_point.x2, var_point2.x2 ; jmp loc_1C56A */
	if (pts[0].px <= pts[1].px) goto loc_1C552;
	goto loc_1C4FC;
loc_1C58A:
	if (bx->top <= pts[0].py) goto loc_1C5A6;    /* cmp [bx+rc_top], y0 ; jle */
	if (bx->top <= pts[1].py) goto loc_1C5A6;
	if (pts[0].px >= pts[1].px) goto loc_1C552;  /* jge loc_1C552 */
	goto loc_1C4FC;
loc_1C5A6:
	var_5A = 0;
	if (detail_level == 4) goto loc_1C61D;       /* cmp detail_level, 4 ; jz */
	if (pts[1].px >= 0) goto loc_1C61D;          /* cmp var_point2.x2, 0 ; jge */
	if (pts[0].px <= RFB_VIEW_W) goto loc_1C61D;      /* cmp var_point.x2, 140h ; jle */
	/* push &var_78 ; push y0 ; push x0 ; push y1 ; push x1 ;
	 * call draw_line_related */
	ax = (int16_t)draw_line_related((uint16_t)pts[1].px, (uint16_t)pts[1].py,
	                                (uint16_t)pts[0].px, (uint16_t)pts[0].py,
	                                var_78buf);
	if (ax != 0) goto loc_1C61D;                 /* or ax, ax ; jnz */
	di = (int16_t)(var_78buf[3] - var_78buf[5]); /* mov di,var_72 ; sub di,var_6E */
	if (di >= 0) goto loc_1C5EA;                 /* jns */
	ax = (int16_t)(-di);
	goto loc_1C5EC;
loc_1C5EA:
	ax = di;
loc_1C5EC:
	if (ax >= 0x60) goto loc_1C61D;              /* cmp ax, 60h ; jge */
	if (var_78buf[1] != 0) goto loc_1C602;       /* cmp var_76, 0 ; jnz */
	var_32 = var_78buf[3];                       /* mov ax,var_72 ; mov var_32,ax */
	ax = var_78buf[5];                           /* mov ax, var_6E */
	goto loc_1C612;
loc_1C602:
	if (var_78buf[1] != 0x13F) goto loc_1C61D;   /* cmp var_76, 13Fh ; jnz */
	var_32 = var_78buf[5];
	ax = var_78buf[3];
loc_1C612:
	var_26 = (int16_t)(ax - var_32);             /* sub ax, var_32 */
	var_5A = 1;
loc_1C61D:
	if (var_5A != 0) goto loc_1C626;             /* cmp var_5A, 0 ; jnz */
	goto loc_1C852;
loc_1C626:
	if (slow_video_mgmt_copy != 0) goto loc_1C630;
	goto loc_1C7B6;
loc_1C630:
	var_rect.left = 0;
	rect_skybox.left = 0;
	var_rect.right = RFB_VIEW_W;
	rect_skybox.right = RFB_VIEW_W;
	if (byte_454A4 == 0) goto loc_1C660;         /* cmp byte_454A4, 0 ; jz */
	bx = arg_rectptr;
	rect_skybox.top = bx->top;
	rect_skybox.bottom = bx->bottom;
	goto loc_1C7AA;
loc_1C660:
	ax = (int16_t)(var_32 + var_26);             /* mov ax,var_32 ; add ax,var_26 */
	if (ax <= var_32) goto loc_1C66E;            /* cmp ax, var_32 ; jle */
	ax = var_32;
loc_1C66E:
	ax = (int16_t)(ax - word_454CE);             /* sub ax, word_454CE */
	rect_skybox.top = ax;
	bx = arg_rectptr;
	if (bx->top <= ax) goto loc_1C683;           /* cmp [bx+rc_top], ax ; jle */
	rect_skybox.top = bx->top;
loc_1C683:
	ax = (int16_t)(var_32 + var_26);
	if (ax >= var_32) goto loc_1C691;            /* jge */
	ax = var_32;
loc_1C691:
	rect_skybox.bottom = ax;
	si = 0;                                      /* sub si, si */
loc_1C696:
	rect_array_unk_indices[si] = 1;
	si++;
	if (si < 0x0F) goto loc_1C696;               /* cmp si, 0Fh ; jl */
	rect_array_unk_indices[5] = 3;               /* mov rect_array_unk_indices+5, 3 */
	var_rect.top = 0;
	var_rect.bottom = rect_skybox.top;           /* mov ax, rect_skybox.rc_top */
	/* push arg_rectptr ; push &var_rect ; call rect_intersect ; or al,al ; jnz */
	if ((uint8_t)rect_intersect(&var_rect, arg_rectptr) != 0) goto loc_1C728;
	rect_array_unk3_length = 0;
	/* rectlist_add_rects(0Fh, indices, rectptr_unk, rect_unk, &var_rect,
	 *                    &rect_array_unk3_length, rect_array_unk3) */
	rectlist_add_rects(0x0F, rect_array_unk_indices, rectptr_unk, rect_unk,
	                   &var_rect, &rect_array_unk3_length, rect_array_unk3);
	di = 0;                                      /* sub di, di */
	goto loc_1C720;
loc_1C6F2:
	/* mov ax,di ; shl ax,3 ; add ax,offset rect_array_unk3 */
	var_rectptr = &rect_array_unk3[di];
	sprite_set_1_size(var_rectptr->left, var_rectptr->right,
	                  var_rectptr->top, var_rectptr->bottom);
	sprite_clear_1_color((uint8_t)skybox_sky_color);
	di++;
loc_1C720:
	/* mov al, rect_array_unk3_length ; cbw ; cmp ax, di ; jg */
	if ((int16_t)(int8_t)rect_array_unk3_length > di) goto loc_1C6F2;
loc_1C728:
	var_rect.top = rect_skybox.bottom;           /* mov ax, rect_skybox.rc_bottom */
	var_rect.bottom = RFB_VIEW_H;                      /* mov var_rect.rc_bottom, 0C8h */
	if ((uint8_t)rect_intersect(&var_rect, arg_rectptr) != 0) goto loc_1C7AA;
	rect_array_unk3_length = 0;
	rectlist_add_rects(0x0F, rect_array_unk_indices, rectptr_unk, rect_unk,
	                   &var_rect, &rect_array_unk3_length, rect_array_unk3);
	di = 0;
	goto loc_1C7A2;
loc_1C774:
	var_rectptr = &rect_array_unk3[di];
	sprite_set_1_size(var_rectptr->left, var_rectptr->right,
	                  var_rectptr->top, var_rectptr->bottom);
	sprite_clear_1_color((uint8_t)skybox_grd_color);
	di++;
loc_1C7A2:
	if ((int16_t)(int8_t)rect_array_unk3_length > di) goto loc_1C774;
loc_1C7AA:
	var_rect.top = rect_skybox.top;
	ax = rect_skybox.bottom;
	goto loc_1C7C2;
loc_1C7B6:
	bx = arg_rectptr;
	var_rect.top = bx->top;                      /* mov ax, [bx+4] */
	ax = bx->bottom;                             /* mov ax, [bx+6] */
loc_1C7C2:
	var_rect.bottom = ax;
	var_rect.left = 0;
	var_rect.right = RFB_VIEW_W;
	/* push arg_rectptr ; push &var_rect ; call rect_intersect ; or al,al ; jz */
	if ((uint8_t)rect_intersect(&var_rect, arg_rectptr) == 0) goto loc_1C7E5;
	goto loc_1CB77;
loc_1C7E5:
	var_34 = 0;
	if (var_26 >= 0) goto loc_1C7F8;             /* cmp var_26, 0 ; jge */
	ax = (int16_t)(-var_26);
	goto loc_1C7FB;
loc_1C7F8:
	ax = var_26;
loc_1C7FB:
	di = (int16_t)(ax + 1);                      /* mov di, ax ; inc di */
	if (di <= 0x20) goto loc_1C806;              /* cmp di, 20h ; jle */
	di = 0x20;
loc_1C806:
	si = 0;
	goto loc_1C84B;
loc_1C80A:
	var_rect.left = var_34;
	/* mov ax,140h ; imul si ; add ax,140h ; cwd ; idiv di
	 * (cwd overwrites dx: the dividend is the sign-extended low product) */
	ax = (int16_t)((int16_t)(RFB_VIEW_W * si + RFB_VIEW_W) / di);
	ax &= video_flag3_isFFFF;                    /* and ax, video_flag3_isFFFF */
	var_rect.right = ax;
	if (var_rect.left == ax) goto loc_1C84A;     /* cmp var_rect.rc_left, ax ; jz */
	/* mov ax,var_26 ; imul si ; cwd ; idiv di ; add ax,var_32 */
	ax = (int16_t)((int16_t)(var_26 * si) / di + var_32);
	var_skyheight = ax;
	/* push ax ; push arg_A ; push &var_rect ; call skybox_op_helper2 */
	skybox_op_helper2(&var_rect, arg_A, ax);
	var_34 = var_rect.right;
loc_1C84A:
	si++;
loc_1C84B:
	if (si < di) goto loc_1C80A;                 /* cmp si, di ; jl */
	goto loc_1CB77;
loc_1C852:
	/* horizon crosses the screen at an angle: build sky and ground quads */
	/* push (y0-y1) ; push (x0-x1) ; call polarAngle */
	si = polarAngle((int16_t)(pts[0].px - pts[1].px),
	                (int16_t)(pts[0].py - pts[1].py));
	si &= 0x3FF;                                 /* and si, 3FFh */
	var_26 = 2;
	goto loc_1C8F0;
loc_1C876:
	di = 1;
	goto loc_1C879;
loc_1C8FF:
	di = 0;                                      /* sub di, di */
loc_1C879:
	/* mov bx,var_26 ; shl bx,1 ; mov ax,[bx+98Ch] ; add ax,si ;
	 * call sin_fast ; push ; mov ax,3E80h ; call multiply_and_scale */
	ax = (int16_t)(skybox_quadangle_98C[var_26] + (uint16_t)si);
	/* pts[var_26].px = pts[di].px + scaled sin */
	pts[var_26].px = (int16_t)(pts[di].px +
	                 multiply_and_scale(0x3E80, sin_fast((uint16_t)ax)));
	ax = (int16_t)(skybox_quadangle_98C[var_26] + (uint16_t)si);
	/* pts[var_26].py = pts[di].py + scaled cos */
	pts[var_26].py = (int16_t)(pts[di].py +
	                 multiply_and_scale(0x3E80, cos_fast((uint16_t)ax)));
	var_26++;
loc_1C8F0:
	if (var_26 >= 6) goto loc_1C904;             /* cmp var_26, 6 ; jge */
	if (var_26 >= 4) goto loc_1C876;             /* cmp var_26, 4 ; jl 1C8FF */
	goto loc_1C8FF;
loc_1C904:
	/* push p3.y p3.x p4.y p4.x p2.y p2.x p1.y p1.x ; push 4 ;
	 * push skybox_sky_color ; call skybox_op_helper
	 * (var_point3 = pts[2], var_point4 = pts[3]) */
	quad[0] = pts[0];
	quad[1] = pts[1];
	quad[2] = pts[3];
	quad[3] = pts[2];
	skybox_op_helper((uint16_t)skybox_sky_color, 4, quad);
	/* push p6.y p6.x p5.y p5.x p2.y p2.x p1.y p1.x ; push 4 ;
	 * push skybox_grd_color (var_point5 = pts[4], var_point6 = pts[5]) */
	quad[0] = pts[0];
	quad[1] = pts[1];
	quad[2] = pts[4];
	quad[3] = pts[5];
	skybox_op_helper((uint16_t)skybox_grd_color, 4, quad);
	goto loc_1CB72;
loc_1C958:
	/* level-horizon fast path (arg_8 == 0) */
	var_58vec.x = 0;
	var_58vec.y = (int16_t)(-arg_C);             /* mov ax, arg_C ; neg ax */
	var_58vec.z = (int16_t)(0x3A98 * arg_4);     /* mov ax,3A98h ; imul arg_4 */
	mat_mul_vector(&var_58vec, arg_6, &var_vec);
	if (var_vec.z >= 0) goto loc_1C9C0;          /* cmp var_vec.vz, 0 ; jge */
	sprite_clear_1_color((uint8_t)skybox_sky_color);
	if (slow_video_mgmt_copy != 0) goto loc_1C99D;
	goto loc_1CB77;
loc_1C99D:
	var_5C = 1;
	rect_skybox.left = 0;
	rect_skybox.right = RFB_VIEW_W;
	bx = arg_rectptr;
	rect_skybox.top = bx->top;
	rect_skybox.bottom = bx->bottom;
	goto loc_1CB77;
loc_1C9C0:
	vector_to_point(&var_vec, &pts[0]);
	var_skyheight = pts[0].py;                   /* mov ax, var_point.y2 */
	bx = arg_rectptr;
	if (bx->top <= var_skyheight) goto loc_1C9E4;/* cmp [bx+rc_top], ax ; jle */
	var_skyheight = bx->top;
loc_1C9E4:
	if (arg_4 == 1) goto loc_1C9ED;              /* cmp arg_4, 1 ; jz */
	goto loc_1CB00;
loc_1C9ED:
	if (slow_video_mgmt_copy == 0) goto loc_1CA25;
	if (detail_level != 4) goto loc_1CA02;       /* cmp detail_level, 4 ; jnz */
	ax = (int16_t)(var_skyheight - 1);           /* mov ax,var_skyheight ; dec ax */
	goto loc_1CA09;
loc_1CA02:
	ax = (int16_t)(var_skyheight - word_454CE);
loc_1CA09:
	rect_skybox.top = ax;
	rect_skybox.left = 0;
	rect_skybox.right = RFB_VIEW_W;
	rect_skybox.bottom = var_skyheight;
	if (byte_454A4 == 0) goto loc_1CA52;         /* cmp byte_454A4, 0 ; jz */
loc_1CA25:
	var_rect.left = 0;
	var_rect.right = RFB_VIEW_W;
	bx = arg_rectptr;
	var_rect.top = bx->top;
	var_rect.bottom = bx->bottom;
	/* push var_skyheight ; push arg_A ; push &var_rect ; call helper2 */
	skybox_op_helper2(&var_rect, arg_A, var_skyheight);
	goto loc_1CB77;
loc_1CA52:
	si = 0;
loc_1CA54:
	rect_array_unk_indices[si] = 1;
	si++;
	if (si < 0x0F) goto loc_1CA54;
	if (detail_level != 4) goto loc_1CA72;
	/* mov bx,arg_0 ; shl bx,1 ; mov ax,word_463D6 ; mov word_449FC[bx],ax */
	word_449FC[arg_0] = word_463D6;
loc_1CA72:
	if (word_449FC[arg_0] != arg_A) goto loc_1CAAC;
	/* mov bx, rectptr_unk ; cmp [bx+28h..2Eh] = rectptr_unk[5] */
	if (rectptr_unk[5].left != rect_skybox.left) goto loc_1CAAC;
	if (rectptr_unk[5].right != rect_skybox.right) goto loc_1CAAC;
	if (rectptr_unk[5].top != rect_skybox.top) goto loc_1CAAC;
	if (rectptr_unk[5].bottom != rect_skybox.bottom) goto loc_1CAAC;
	rect_array_unk_indices[5] = 0;
	goto loc_1CAB1;
loc_1CAAC:
	rect_array_unk_indices[5] = 3;
loc_1CAB1:
	rect_array_unk3_length = 0;
	/* note: fifth argument here is arg_rectptr itself, not var_rect */
	rectlist_add_rects(0x0F, rect_array_unk_indices, rectptr_unk, rect_unk,
	                   arg_rectptr, &rect_array_unk3_length, rect_array_unk3);
	di = 0;
	goto loc_1CAF6;
loc_1CADE:
	/* push var_skyheight ; push arg_A ; push &rect_array_unk3[di] */
	skybox_op_helper2(&rect_array_unk3[di], arg_A, var_skyheight);
	di++;
loc_1CAF6:
	if ((int16_t)(int8_t)rect_array_unk3_length > di) goto loc_1CADE;
	goto loc_1CB77;
loc_1CB00:
	/* inverted skybox (arg_4 != 1): ground above the horizon, sky below */
	si = var_skyheight;
	bx = arg_rectptr;
	si = (int16_t)(si - bx->top);
	ax = (int16_t)(bx->bottom - bx->top);
	if (ax >= si) goto loc_1CB19;                /* cmp ax, si ; jge */
	si = (int16_t)(bx->bottom - bx->top);
loc_1CB19:
	if (si <= 0) goto loc_1CB41;                 /* or si, si ; jle */
	sprite_set_1_size(0, RFB_VIEW_W, bx->top, (int16_t)(bx->top + si));
	sprite_clear_1_color((uint8_t)skybox_grd_color);
loc_1CB41:
	bx = arg_rectptr;
	si = (int16_t)(bx->bottom - var_skyheight);
	if (si <= 0) goto loc_1CB72;                 /* or si, si ; jle */
	sprite_set_1_size(0, RFB_VIEW_W, var_skyheight, (int16_t)(var_skyheight + si));
	sprite_clear_1_color((uint8_t)skybox_sky_color); /* loc_1CB6A */
loc_1CB72:
	var_5C = 1;
loc_1CB77:
	return var_5C;                               /* mov ax, var_5C */
}

/* ------------------------------------------------------------------ */
/* seg003.asm 4706-4759: transformed_shape_add_for_sort                */
/*                                                                     */
/* Registers the shape at curtransshape_ptr for depth sorting: rotates */
/* its position through mat_temp, stores z + zadjust into the sort key */
/* array and advances curtransshape_ptr.                               */
/* ------------------------------------------------------------------ */
void transformed_shape_add_for_sort(int16_t arg_zadjust, int16_t arg_2)
{
	struct VECTOR transformedpos;      /* bp-12 */
	struct VECTOR shapepos;            /* bp-6 */
	int16_t si, di;

	/* mov ax, curtransshape_ptr ; mov si, ax ; movsw x3:
	 * copy the first member (pos VECTOR) of the current TRANSFORMEDSHAPE */
	shapepos = curtransshape_ptr->pos;
	/* push &transformedpos ; push offset mat_temp ; push &shapepos */
	mat_mul_vector(&shapepos, &mat_temp, &transformedpos);
	/* mov al, transformedshape_counter ; cbw */
	si = (int16_t)(int8_t)transformedshape_counter;
	di = (int16_t)(si << 1);           /* mov di, si ; shl di, 1 (word offset) */
	(void)di;
	/* mov ax, transformedpos.vz ; add ax, arg_zadjust ;
	 * mov transformedshape_zarray[di], ax */
	transformedshape_zarray[si] = (int16_t)(transformedpos.z + arg_zadjust);
	/* mov al, [bp+arg_2] ; mov transformedshape_arg2array[si], al */
	transformedshape_arg2array[si] = (char)arg_2;
	/* mov transformedshape_indices[di], si */
	transformedshape_indices[si] = si;
	transformedshape_counter++;        /* inc transformedshape_counter */
	/* add curtransshape_ptr, 14h  (sizeof TRANSFORMEDSHAPE in the
	 * original's 16-bit layout -> advance one element) */
	curtransshape_ptr++;
}

/* ------------------------------------------------------------------ */
/* seg004.asm 6109-6275: subst_hillroad_track                          */
/*                                                                     */
/* Substitutes a hill-road track element code for the base element     */
/* when it sits on a hill-slope terrain tile (terrain codes 7..0Ah).   */
/*   arg_0 = terrain tile code, arg_2 = element tile code.             */
/* Returns the substituted element code, or 0 for no substitution.     */
/* ------------------------------------------------------------------ */
uint8_t subst_hillroad_track(uint8_t arg_0, uint8_t arg_2)
{
	int16_t ax;

	ax = arg_0;                            /* mov al,[bp+arg_0] ; sub ah,ah */
	if (ax == 7) goto loc_21A80;
	if (ax == 8) goto loc_21ABE;
	if (ax == 9) goto loc_21AFE;           /* jnz loc_21A74 ; jmp loc_21AFE */
	if (ax == 0x0A) goto loc_21B3C;        /* loc_21A74 */
loc_21A7C:
	return 0;                              /* sub ax, ax ; retf */
loc_21A80:
	ax = arg_2;                            /* mov al,[bp+arg_2] ; sub ah,ah */
	if (ax == 0x04) return 0xB6;           /* loc_21AA6 */
	if (ax == 0x0E) return 0xBA;           /* loc_21AAC */
	if (ax == 0x18) return 0xBE;           /* loc_21AB2 */
	if (ax == 0x27) return 0xC2;           /* loc_21AB8 */
	if (ax == 0x3B) return 0xC2;
	if (ax == 0x62) return 0xC2;
	goto loc_21A7C;
loc_21ABE:
	ax = arg_2;
	if (ax == 0x05) return 0xB7;           /* loc_21AE4 */
	if (ax == 0x0F) return 0xBB;           /* loc_21AEA */
	if (ax == 0x19) return 0xBF;           /* loc_21AF0 */
	if (ax == 0x24) return 0xC3;           /* loc_21AF8 */
	if (ax == 0x38) return 0xC3;
	if (ax == 0x5F) return 0xC3;
	goto loc_21A7C;
loc_21AFE:
	ax = arg_2;
	if (ax == 0x04) return 0xB8;           /* loc_21B24 */
	if (ax == 0x0E) return 0xBC;           /* loc_21B2A */
	if (ax == 0x18) return 0xC0;           /* loc_21B30 */
	if (ax == 0x26) return 0xC4;           /* loc_21B36 */
	if (ax == 0x3A) return 0xC4;
	if (ax == 0x61) return 0xC4;
	goto loc_21A7C;
loc_21B3C:
	ax = arg_2;
	if (ax == 0x05) return 0xB9;           /* loc_21B62 */
	if (ax == 0x0F) return 0xBD;           /* loc_21B68 */
	if (ax == 0x19) return 0xC1;           /* loc_21B6E */
	if (ax == 0x25) return 0xC5;           /* loc_21B74 */
	if (ax == 0x39) return 0xC5;
	if (ax == 0x60) return 0xC5;
	goto loc_21A7C;
}

/* ------------------------------------------------------------------ */
/* seg004.asm 61-2755: build_track_object                              */
/*                                                                     */
/* Given the car's world position (and next position), determines the  */
/* track tile it is on and sets up the physics environment globals:    */
/*   planindex / current_planptr  - driving surface plane              */
/*   current_surf_type            - surface material (grass default)   */
/*   wallindex / wallHeight / wallStartX / wallStartZ / wallOrientation*/
/*   terrainHeight, corkFlag, byte_4392C, elem_x/zCenter               */
/*                                                                     */
/* Uses two code-segment jump tables in the original:                  */
/*   bto_branches (seg004.asm 2348-2422, 75 entries, indexed by the    */
/*     element's physical-model code 0..4Ah) -> C switch below.        */
/*   off_1F87E (seg004.asm 2489-2500, 12 entries, hill-slope           */
/*     orientation dispatch, pattern of 4 repeated 3x) -> C switch.    */
/* ------------------------------------------------------------------ */
void build_track_object(struct VECTOR* arg_posWorldCrds, struct VECTOR* arg_nextPosWorldCrds)
{
	int16_t far* var_curr_wallptr;  /* bp-64 (far ptr into wallptr array) */
	int16_t var_misc3C;             /* bp-60 */
	struct TRACKOBJECT* var_trkObjList; /* bp-58 */
	int16_t var_wallOrientMod;      /* bp-56 */
	int16_t var_36;                 /* bp-54 (set, never read - as original) */
	uint8_t var_tileTerr;           /* bp-52 */
	int16_t var_absXElemCrds;       /* bp-50 */
	int16_t var_absZElemCrds;       /* bp-46 */
	struct VECTOR var_posElemCrds;  /* bp-44 */
	int16_t var_physModel;          /* bp-36 */
	int16_t var_misc22;             /* bp-34 */
	int8_t var_trkRow;              /* bp-32 */
	int16_t var_misc1E;             /* bp-30 */
	int16_t var_misc1C;             /* bp-28 */
	int8_t var_trkCol;              /* bp-26 */
	struct VECTOR var_nextPosElemCrds; /* bp-24 */
	int8_t var_surfaceType;         /* bp-18 */
	int16_t var_elemOrient;         /* bp-16 */
	int16_t var_E;                  /* bp-14 */
	int16_t var_C;                  /* bp-12 */
	int16_t var_turnRadius;         /* bp-10 */
	uint8_t var_tileElem;           /* bp-8 */
	int16_t var_06effX;             /* bp-6 */
	int16_t var_02effZ;             /* bp-2 */
	int16_t si, di, ax;
	uint8_t al;

	planindex = 0;
	wallindex = -1;                        /* mov wallindex, 0FFFFh */
	wallHeight = -12;                      /* mov wallHeight, 0FFF4h */
	elRdWallRelated = -1000;               /* mov elRdWallRelated, 0FC18h */
	corkFlag = 0;
	current_surf_type = 4;                 /* grass (structs.inc: grass = 4) */
	byte_4392C = 1;
	si = 0;                                /* sub si, si */
	var_wallOrientMod = si;
	var_elemOrient = si;
	terrainHeight = si;
	/* mov ax,[bx] ; mov cl,0Ah ; sar ax,cl ; mov var_trkCol, al */
	var_trkCol = (int8_t)(arg_posWorldCrds->x >> 10);
	/* mov ax,[bx+4] ; sar ax,cl ; mov var_trkRow, al */
	var_trkRow = (int8_t)(arg_posWorldCrds->z >> 10);
	var_physModel = -1;                    /* mov var_physModel, 0FFFFh */
	if (var_trkCol >= 0) goto loc_1E1FD;   /* cmp var_trkCol, 0 ; jge */
	goto loc_1F8CD;
loc_1E1FD:
	if (var_trkCol <= 0x1D) goto loc_1E206;/* cmp var_trkCol, 1Dh ; jle */
	goto loc_1F8CD;
loc_1E206:
	if (var_trkRow >= 0) goto loc_1E20D;   /* or al, al ; jge */
	goto loc_1F8CD;
loc_1E20D:
	if (var_trkRow <= 0x1D) goto loc_1E214;
	goto loc_1F8CD;
loc_1E214:
	di = (int16_t)var_trkCol;              /* mov al,var_trkCol ; cbw ; mov di,ax */
	elem_xCenter = trackcenterpos2[di];    /* mov ax, trackcenterpos2[di*2] */
	var_misc3C = (int16_t)((int16_t)var_trkRow << 1); /* shl ax, 1 */
	elem_zCenter = terraincenterpos[var_trkRow];      /* terraincenterpos[bx] */
	/* mov bx, trackrows[bx] ; add bx, td15_terr_map_main ; al = es:[bx+di] */
	var_tileTerr = td15_terr_map_main[trackrows[var_trkRow] + di];
	al = var_tileTerr;
	if (al == 0) goto loc_1E276;           /* or al, al ; jz */
	ax = al;                               /* sub ah, ah */
	if (ax != 1) goto loc_1E257;           /* cmp ax, 1 ; jnz */
	goto loc_1E2FB;
loc_1E257:
	if (ax == 2) goto loc_1E29A;           /* coast */
	if (ax == 3) goto loc_1E2A0;
	if (ax == 4) goto loc_1E2A6;
	if (ax == 5) goto loc_1E2AC;
	if (ax != 6) goto loc_1E276;
/* code_addHillHeight: */
	terrainHeight = hillHeightConsts[1];   /* mov ax, hillHeightConsts+2 */
loc_1E276:
	/* bx = terrainrows[trkRow] + trkCol ; les di, td14_elem_map_main */
	var_tileElem = td14_elem_map_main[terrainrows[var_trkRow] + (int16_t)var_trkCol];
	if (var_tileElem != 0) goto loc_1E304; /* or al, al ; jnz */
	goto code_bto_blank;
loc_1E29A:
	si = 0x80;
	goto loc_1E2AF;
loc_1E2A0:
	si = -0x280;                           /* mov si, 0FD80h */
	goto loc_1E2AF;
loc_1E2A6:
	si = -0x180;                           /* mov si, 0FE80h */
	goto loc_1E2AF;
loc_1E2AC:
	si = -0x80;                            /* mov si, 0FF80h */
loc_1E2AF:
	var_posElemCrds.x = (int16_t)(arg_posWorldCrds->x - elem_xCenter);
	var_posElemCrds.z = (int16_t)(arg_posWorldCrds->z - elem_zCenter);
	/* push posz ; push si ; call sin_fast ; push ax ; call multiply_and_scale */
	di = multiply_and_scale(sin_fast((uint16_t)si), var_posElemCrds.z);
	/* push posx ; push si ; call cos_fast ; push ax ; call multiply_and_scale ;
	 * add ax, di */
	ax = (int16_t)(multiply_and_scale(cos_fast((uint16_t)si), var_posElemCrds.x) + di);
	var_misc22 = ax;
	if (ax < 0) goto loc_1E2FB;            /* or ax, ax ; jl */
	goto loc_1E276;
loc_1E2FB:
	current_surf_type = 5;                 /* water (structs.inc: water = 5) */
	goto loc_1E276;
loc_1E304:
	/* multi-tile filler codes 0FDh/0FEh/0FFh */
	if (var_tileElem >= 0xFD) goto loc_1E30D; /* cmp var_tileElem,0FDh ; jnb */
	goto loc_1E40C;
loc_1E30D:
	ax = var_tileElem;                     /* mov al,var_tileElem ; sub ah,ah */
	if (ax == 0xFD) goto loc_1E328;
/* loc_1E317: */
	if (ax == 0xFE) goto loc_1E390;
	if (ax == 0xFF) goto loc_1E3CC;        /* jnz loc_1E324 ; jmp loc_1E3CC */
	goto loc_1E464;                        /* loc_1E324 */
loc_1E328:
	/* al = td14[terrainrows[row+1] + col - 1] */
	di = (int16_t)((int16_t)var_trkRow << 1);
	(void)di;
	var_tileElem = td14_elem_map_main[terrainrows[var_trkRow + 1] +
	                                  (int16_t)var_trkCol - 1];
	/* bx = tileElem*14 ; test trkObjectList.ss_multiTileFlag[bx], 1 */
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 1)) goto loc_1E365;
	ax = terrainpos[var_trkRow + 1];       /* mov ax, (terrainpos+2)[di] */
/* loc_1E362: */
	elem_zCenter = ax;
loc_1E365:
	if (trkObjectList[var_tileElem].ss_multiTileFlag & 2) goto loc_1E380;
	goto loc_1E464;
loc_1E380:
	ax = trackpos2[(int16_t)var_trkCol];   /* mov ax, trackpos2[col*2] */
	goto loc_1E461;
loc_1E390:
	/* al = td14[terrainrows[row+1] + col] */
	var_tileElem = td14_elem_map_main[terrainrows[var_trkRow + 1] +
	                                  (int16_t)var_trkCol];
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 1)) goto loc_1E43D;
	ax = terrainpos[var_trkRow + 1];       /* mov ax, (terrainpos+2)[di] */
	goto loc_1E43A;
loc_1E3CC:
	/* al = td14[terrainrows[row] + col - 1] */
	var_tileElem = td14_elem_map_main[terrainrows[var_trkRow] +
	                                  (int16_t)var_trkCol - 1];
	if (trkObjectList[var_tileElem].ss_multiTileFlag & 1) goto loc_1E405;
	goto loc_1E365;
loc_1E405:
	ax = terrainpos[var_trkRow];           /* mov ax, terrainpos[di] */
	elem_zCenter = ax;                     /* loc_1E362 */
	goto loc_1E365;
loc_1E40C:
	/* mov al, trkObjectList.ss_multiTileFlag[bx] ; mov byte(var_misc3C), al */
	al = (uint8_t)trkObjectList[var_tileElem].ss_multiTileFlag;
	var_misc3C = (int16_t)((var_misc3C & (int16_t)0xFF00) | al);
	if (al == 0) goto loc_1E464;           /* cmp al, ah ; jz */
	if (!(var_misc3C & 1)) goto loc_1E43D; /* test byte(var_misc3C), 1 ; jz */
	ax = terrainpos[var_trkRow];           /* mov ax, terrainpos[row*2] */
loc_1E43A:
	elem_zCenter = ax;
loc_1E43D:
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 2)) goto loc_1E464;
	ax = trackpos2[(int16_t)var_trkCol + 1]; /* mov ax, (trackpos2+2)[col*2] */
loc_1E461:
	elem_xCenter = ax;
loc_1E464:
	var_posElemCrds.x = (int16_t)(arg_posWorldCrds->x - elem_xCenter);
	var_posElemCrds.z = (int16_t)(arg_posWorldCrds->z - elem_zCenter);
	var_nextPosElemCrds.x = (int16_t)(arg_nextPosWorldCrds->x - elem_xCenter);
	var_nextPosElemCrds.z = (int16_t)(arg_nextPosWorldCrds->z - elem_zCenter);
	dbg_bto_elem0 = var_tileElem; dbg_bto_terr = var_tileTerr;
	if (var_tileElem == 0) goto loc_1E4B6;     /* cmp var_tileElem, 0 ; jz */
	if (var_tileTerr < 7) goto loc_1E4B6;      /* cmp var_tileTerr, 7 ; jb */
	if (var_tileTerr >= 0x0B) goto loc_1E4B6;  /* cmp var_tileTerr, 0Bh ; jnb */
	/* push tileElem ; push tileTerr ; call subst_hillroad_track */
	var_tileElem = subst_hillroad_track(var_tileTerr, var_tileElem);
loc_1E4B6:
	dbg_bto_elem1 = var_tileElem;
	/* ax = tileElem*14 + offset trkObjectList */
	var_trkObjList = &trkObjectList[var_tileElem];
	/* mov al, [bx+TRACKOBJECT.ss_physicalModel] ; cbw */
	var_physModel = (int16_t)(int8_t)var_trkObjList->ss_physicalModel;
	dbg_model = var_physModel; dbg_col = var_trkCol; dbg_row = var_trkRow;
	if (dbg_bto_n < 8) {
		int16_t* r = dbg_bto[dbg_bto_n++];
		r[0] = var_trkCol; r[1] = var_trkRow; r[2] = dbg_bto_terr;
		r[3] = dbg_bto_elem0; r[4] = dbg_bto_elem1; r[5] = var_physModel;
		r[6] = 0;
	}
	var_elemOrient = var_trkObjList->ss_rotY;  /* mov ax,[bx+ss_rotY] */
	ax = var_elemOrient;
	if (ax == 0) goto loc_1E4EF;               /* or ax, ax ; jz */
	if (ax == 0x100) goto loc_1E562;
	if (ax == 0x200) goto loc_1E540;
	if (ax == 0x300) goto loc_1E516;
loc_1E4EF:
	var_36 = 0;
	(void)var_36;
	/* mov al,[bx+ss_surfaceType] ; inc al ; cmp al,1 ; jge */
	var_surfaceType = (int8_t)(var_trkObjList->ss_surfaceType + 1);
	if (var_surfaceType < 1) var_surfaceType = 1;
/* loc_1E507: */
	if (var_posElemCrds.x >= 0) goto loc_1E58A;
	ax = (int16_t)(-var_posElemCrds.x);
	goto loc_1E58D;
loc_1E516:
	/* rotY = 300h: (x,z) -> (z,-x), same for next */
	ax = var_posElemCrds.x;
	var_misc1E = ax;
	var_posElemCrds.x = var_posElemCrds.z;
	ax = (int16_t)(-var_misc1E);
	var_posElemCrds.z = ax;
	ax = var_nextPosElemCrds.x;
	var_misc1E = ax;
	var_nextPosElemCrds.x = var_nextPosElemCrds.z;
	ax = (int16_t)(-var_misc1E);
loc_1E53B:
	var_nextPosElemCrds.z = ax;
	goto loc_1E4EF;
loc_1E540:
	/* rotY = 200h: negate both */
	var_posElemCrds.z = (int16_t)(-var_posElemCrds.z);
	var_posElemCrds.x = (int16_t)(-var_posElemCrds.x);
	var_nextPosElemCrds.z = (int16_t)(-var_nextPosElemCrds.z);
	var_nextPosElemCrds.x = (int16_t)(-var_nextPosElemCrds.x);
	goto loc_1E4EF;
loc_1E562:
	/* rotY = 100h: (x,z) -> (-z,x), same for next */
	ax = var_posElemCrds.x;
	var_misc1E = ax;
	var_posElemCrds.x = (int16_t)(-var_posElemCrds.z);
	var_posElemCrds.z = var_misc1E;
	ax = var_nextPosElemCrds.x;
	var_misc1E = ax;
	var_nextPosElemCrds.x = (int16_t)(-var_nextPosElemCrds.z);
	ax = var_misc1E;
	goto loc_1E53B;
loc_1E58A:
	ax = var_posElemCrds.x;
loc_1E58D:
	var_absXElemCrds = ax;
	if (var_posElemCrds.z >= 0) goto loc_1E59E;
	ax = (int16_t)(-var_posElemCrds.z);
	goto loc_1E5A1;
loc_1E59E:
	ax = var_posElemCrds.z;
loc_1E5A1:
	var_absZElemCrds = ax;
	ax = var_physModel;
	/* cmp ax, 4Ah ; jbe code_bto_root (unsigned) */
	if ((uint16_t)ax > 0x4A) goto code_bto_blank;
/* code_bto_root: jmp word ptr cs:bto_branches[ax*2] */
	switch ((uint16_t)ax) {
	case 0x00: goto code_bto_sfLine;        /* start/finish line */
	case 0x01: goto code_bto_road;          /* road */
	case 0x02: goto code_bto_sCorner;       /* sharp corner */
	case 0x03: goto code_bto_lCorner;       /* corner */
	case 0x04: goto code_bto_chicaneRL;     /* chicane B */
	case 0x05: goto code_bto_chicaneLR;     /* chicane A */
	case 0x06: goto code_bto_sSplitA;       /* sharp split A */
	case 0x07: goto code_bto_sSplitB;       /* sharp split B */
	case 0x08: goto code_bto_lSplitA;       /* split A */
	case 0x09: goto code_bto_lSplitB;       /* split B */
	case 0x0A: goto code_bto_highEntrance;  /* highway entrance */
	case 0x0B: goto code_bto_highway;       /* highway */
	case 0x0C: goto code_bto_crossroad;     /* crossroad */
	case 0x10: goto code_bto_ramp;          /* ramp */
	case 0x11: goto code_bto_solidRamp;     /* solid ramp */
	case 0x12:                              /* elevated road */
	case 0x13: goto code_bto_elevRoad;      /* elevated span */
	case 0x14: goto code_bto_solidRoad;     /* solid road */
	case 0x15: goto code_bto_elevCorner;    /* elevated corner */
	case 0x16: goto code_bto_overpass;      /* overpass */
	case 0x17: goto code_bto_bankEntranceB; /* bank rd. entrance B */
	case 0x18: goto code_bto_bankEntranceA; /* bank rd. entrance A */
	case 0x19: goto code_bto_bankRoad;      /* banked road */
	case 0x1A: goto code_bto_bankCorner;    /* banked corner */
	case 0x1B: goto code_bto_loop;          /* loop */
	case 0x1C: goto code_bto_tunnel;        /* tunnel */
	case 0x1D: goto code_bto_pipeEntrance;  /* pipe entrance */
	case 0x1E: goto code_bto_pipe;          /* pipe */
	case 0x1F: goto code_bto_halfPipe;      /* half-pipe */
	case 0x20: goto code_bto_corkUdLH;      /* cork u/d A */
	case 0x21: goto code_bto_corkUdRH;      /* cork u/d B */
	case 0x22: goto code_bto_slalom;        /* slalom */
	case 0x23: goto code_bto_corkLr;        /* cork l/r */
	case 0x41: goto code_bto_barn;          /* barn */
	case 0x42: goto code_bto_gasStation;    /* gas station */
	case 0x43: goto code_bto_joes;          /* Joe's */
	case 0x44: goto code_bto_office;        /* office */
	case 0x45: goto code_bto_windmill;      /* windmill */
	case 0x46: goto code_bto_ship;          /* ship */
	default:   goto code_bto_blank;         /* 0D-0F, 24-40, 47-4A: blank */
	}

code_bto_sfLine:
	if (state.game_inputmode != 0) goto code_bto_road;
	if (var_posElemCrds.x <= 0) goto code_bto_road;
	if (var_posElemCrds.z >= -0x17C) goto loc_1E5D4; /* cmp z, 0FE84h ; jge */
	planindex = 0x83;
	goto code_bto_road;
loc_1E5D4:
	if (var_posElemCrds.z >= -0x12C) goto code_bto_road; /* cmp z, 0FED4h */
	planindex = 0x84;
code_bto_road:
	/* cmp var_absXElemCrds, 78h ; (loc_1E5E5) jl set_pavement / jmp blank */
	if (var_absXElemCrds < 0x78) goto code_set_pavement;
	goto code_bto_blank;
code_set_pavement:
	current_surf_type = var_surfaceType;
	goto code_bto_blank;
code_bto_crossroad:
	if (var_absXElemCrds < 0x78) goto code_set_pavement;
	/* cmp var_absZElemCrds, 78h ; jmp loc_1E5E5 */
	if (var_absZElemCrds < 0x78) goto code_set_pavement;
	goto code_bto_blank;
code_bto_chicaneLR:
	var_posElemCrds.x = (int16_t)(-var_posElemCrds.x);
code_bto_chicaneRL:
	current_surf_type = var_surfaceType;
	if (var_posElemCrds.x <= 0) goto code_bto_lCorner;
	var_posElemCrds.z = (int16_t)(-var_posElemCrds.z);
	var_posElemCrds.x = (int16_t)(-var_posElemCrds.x);
code_bto_lCorner:
	ax = (int16_t)(var_posElemCrds.x + 0x400);   /* add ah, 4 */
code_lCorner_radius:
	/* push (z+400h) ; push ax ; call polarRadius2D */
	ax = polarRadius2D(ax, (int16_t)(var_posElemCrds.z + 0x400));
	var_turnRadius = ax;
	if (ax > 0x588) goto loc_1E645;              /* cmp ax, 588h ; jg */
	goto code_bto_blank;
loc_1E645:
	/* cmp ax, 678h ; jmp loc_1E5E5 */
	if (ax < 0x678) goto code_set_pavement;
	goto code_bto_blank;
code_bto_sSplitA:
	if (var_absXElemCrds < 0x78) goto code_set_pavement;
code_bto_sCorner:
	ax = (int16_t)(var_posElemCrds.x + 0x200);   /* add ah, 2 */
code_sCorner_radius:
	ax = polarRadius2D(ax, (int16_t)(var_posElemCrds.z + 0x200));
	var_turnRadius = ax;
	if (ax > 0x188) goto loc_1E671;
	goto code_bto_blank;
loc_1E671:
	/* cmp ax, 278h ; jmp loc_1E5E5 */
	if (ax < 0x278) goto code_set_pavement;
	goto code_bto_blank;
code_bto_sSplitB:
	if (var_absXElemCrds >= 0x78) goto loc_1E681;
	goto code_set_pavement;
loc_1E681:
	ax = (int16_t)(0x200 - var_posElemCrds.x);   /* mov ax,200h ; sub ax,x */
	goto code_sCorner_radius;
code_bto_lSplitA:
	if (var_posElemCrds.x < 0x188) goto code_bto_lCorner;
	if (var_posElemCrds.x > 0x278) goto loc_1E6A1;
	goto code_set_pavement;
loc_1E6A1:
	goto code_bto_lCorner;
code_bto_lSplitB:
	if (var_posElemCrds.x < -0x278) goto loc_1E6B5;  /* cmp x, 0FD88h ; jl */
	if (var_posElemCrds.x > -0x188) goto loc_1E6B5;  /* cmp x, 0FE78h ; jg */
	goto code_set_pavement;
loc_1E6B5:
	ax = (int16_t)(0x400 - var_posElemCrds.x);   /* mov ax,400h ; sub ax,x */
	goto code_lCorner_radius;
code_bto_highEntrance:
	if (var_posElemCrds.x >= 0) goto loc_1E6D4;
	ax = (int16_t)(-var_posElemCrds.x);
	goto loc_1E6D7;
loc_1E6D4:
	ax = var_posElemCrds.x;
loc_1E6D7:
	var_misc1C = ax;
	si = 0;
	goto loc_1E6DF;
loc_1E6DE:
	si++;
loc_1E6DF:
	/* cmp highEntrZBounds1[si*2], posz ; jl */
	if (highEntrZBounds1[si] < var_posElemCrds.z) goto loc_1E6DE;
	di = si;                                     /* mov di, si (word index) */
	ax = highEntrXInnBounds0[di];
	if (highEntrXInnBounds1[di] == ax) goto loc_1E72E;
	/* __aFlmul/__aFldiv: (XInn1-XInn0)*(posz-Z0)/(Z1-Z0) + XInn0 */
	ax = (int16_t)(((int32_t)(int16_t)(highEntrXInnBounds1[di] - highEntrXInnBounds0[di]) *
	                (int32_t)(int16_t)(var_posElemCrds.z - highEntrZBounds0[di])) /
	               (int32_t)(int16_t)(highEntrZBounds1[di] - highEntrZBounds0[di]));
	ax = (int16_t)(ax + highEntrXInnBounds0[di]);
loc_1E72E:
	var_misc1E = ax;
	ax = highEntrXOutBounds0[di];
	if (highEntrXOutBounds1[di] == ax) goto loc_1E773;
	ax = (int16_t)(((int32_t)(int16_t)(highEntrXOutBounds1[di] - highEntrXOutBounds0[di]) *
	                (int32_t)(int16_t)(var_posElemCrds.z - highEntrZBounds0[di])) /
	               (int32_t)(int16_t)(highEntrZBounds1[di] - highEntrZBounds0[di]));
	ax = (int16_t)(ax + highEntrXOutBounds0[di]);
loc_1E773:
	var_misc22 = ax;
	if (var_misc1C <= var_misc1E) goto loc_1E789;    /* cmp misc1C, misc1E ; jle */
	if (var_misc1C >= var_misc22) goto loc_1E789;    /* cmp misc1C, misc22 ; jge */
	goto code_set_pavement;
loc_1E789:
	if (var_posElemCrds.z >= 0) goto loc_1E792;
	goto code_bto_blank;
loc_1E792:
	if (var_misc1C <= 0x78) goto loc_1E79B;
	goto code_bto_blank;
loc_1E79B:
	planindex = 1;
/* loc_1E7A1: */
	if (var_posElemCrds.z < 0x14E) goto loc_1E7C0;
	if (var_nextPosElemCrds.x <= -0x78) goto loc_1E7F9;  /* cmp nx, 0FF88h ; jle */
loc_1E7AE:
	if (var_nextPosElemCrds.x >= 0x78) goto loc_1E7B7;
	goto code_bto_blank;
loc_1E7B7:
	wallindex = 0xBA;
	goto code_bto_blank;
loc_1E7C0:
	if (var_nextPosElemCrds.x < 0) goto loc_1E7D0;
	wallindex = 0xBB;
	goto code_bto_blank;
loc_1E7D0:
	wallindex = 0xBD;
	goto code_bto_blank;
code_bto_highway:
	if (var_absXElemCrds <= 0x168) goto loc_1E7E4;
	goto code_bto_blank;
loc_1E7E4:
	if (var_absXElemCrds <= 0x78) goto loc_1E7ED;
	goto code_set_pavement;
loc_1E7ED:
	planindex = 1;
	if (var_nextPosElemCrds.x > -0x78) goto loc_1E7AE;   /* cmp nx, 0FF88h ; jg */
loc_1E7F9:
	wallindex = 0xBC;
	goto code_bto_blank;
code_bto_ramp:
	if (var_posElemCrds.z <= 0) goto loc_1E810;
	byte_4392C = 0;
	goto loc_1E82B;
loc_1E810:
	if (var_nextPosElemCrds.z < 0) goto loc_1E82B;
	wallindex = 0x66;
	goto loc_1E82B;
code_bto_solidRamp:
	if (var_nextPosElemCrds.z < 0x1DC) goto loc_1E82B;
	wallindex = 0x67;
loc_1E82B:
	if (var_nextPosElemCrds.x >= 0) goto loc_1E838;
	ax = var_nextPosElemCrds.x;
/* loc_1E834: */
	ax = (int16_t)(-ax);
	goto loc_1E83B;
loc_1E838:
	ax = var_nextPosElemCrds.x;
loc_1E83B:
	if (ax >= 0x78) goto loc_1E886;
	planindex = 3;
	current_surf_type = var_surfaceType;
	if (wallindex == -1) goto loc_1E856;   /* cmp wallindex, 0FFFFh ; jz */
	goto code_bto_blank;
loc_1E856:
	if (var_posElemCrds.z >= 0) goto loc_1E85F;
	goto code_bto_blank;
loc_1E85F:
	if (var_absXElemCrds >= 0x78) goto loc_1E868;
	goto code_bto_blank;
loc_1E868:
	wallHeight = 0x2A;
	elRdWallRelated = -12;                 /* mov elRdWallRelated, 0FFF4h */
	if (var_posElemCrds.x >= 0) goto loc_1E87D;
	goto loc_1E96F;
loc_1E87D:
	wallindex = 0x65;
	goto code_bto_blank;
loc_1E886:
	if (byte_4392C != 0) goto loc_1E890;
	goto code_bto_blank;
loc_1E890:
	if (var_absXElemCrds <= 0x78) goto loc_1E899;
	goto code_bto_blank;
loc_1E899:
	planindex = 3;
	if (wallindex == -1) goto loc_1E8A9;
	goto code_bto_blank;
loc_1E8A9:
	var_wallOrientMod = 0x200;
loc_1E8AE:
	/* cmp posx, 0 ; (loc_1E96A) jl loc_1E96F / jmp loc_1E87D */
	if (var_posElemCrds.x < 0) goto loc_1E96F;
	goto loc_1E87D;
code_bto_overpass:
	/* mov bx,arg_pos ; mov ax,[bx+2] ; sub ax,terrainHeight */
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax <= 0x186) goto loc_1E8D8;
loc_1E8C5:
	byte_4392C = 0;
code_bto_solidRoad:
	if (var_nextPosElemCrds.x >= 0) goto loc_1E8F8;
	ax = (int16_t)(-var_nextPosElemCrds.x);
	goto loc_1E8FB;
loc_1E8D8:
	/* cmp var_absZElemCrds, 78h ; (loc_1E8DC) jle set_pavement / jmp blank */
	if (var_absZElemCrds <= 0x78) goto code_set_pavement;
	goto code_bto_blank;
code_bto_elevRoad:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax > 0x186) goto loc_1E8F6;
	goto code_bto_blank;
loc_1E8F6:
	goto loc_1E8C5;
loc_1E8F8:
	ax = var_nextPosElemCrds.x;
loc_1E8FB:
	if (ax > 0x78) goto loc_1E942;
	planindex = 2;
	current_surf_type = var_surfaceType;
	if (byte_4392C == 0) goto loc_1E92F;
	if (var_nextPosElemCrds.z < 0x1DC) goto loc_1E922;
	wallindex = 0x67;
	goto loc_1E92F;
loc_1E922:
	if (var_nextPosElemCrds.z > -0x1DC) goto loc_1E92F;  /* cmp nz, 0FE24h ; jg */
	wallindex = 0x68;
loc_1E92F:
	if (var_absXElemCrds >= 0x78) goto loc_1E938;
	goto code_bto_blank;
loc_1E938:
	wallHeight = 0x2A;
	goto loc_1E8AE;
loc_1E942:
	if (byte_4392C != 0) goto loc_1E94C;
	goto code_bto_blank;
loc_1E94C:
	if (var_absXElemCrds <= 0x78) goto loc_1E955;
	goto code_bto_blank;
loc_1E955:
	planindex = 2;
	wallHeight = 0x2A;
	var_wallOrientMod = 0x200;
	/* cmp nx, 0 ; (loc_1E96A) jl loc_1E96F / jmp loc_1E87D */
	if (var_nextPosElemCrds.x < 0) goto loc_1E96F;
	goto loc_1E87D;
loc_1E96F:
	wallindex = 0x64;
	goto code_bto_blank;
code_bto_elevCorner:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax > 0x186) goto loc_1E98A;
	goto code_bto_blank;
loc_1E98A:
	/* polarRadius2D(x+400h, z+400h) - 600h */
	ax = polarRadius2D((int16_t)(var_posElemCrds.x + 0x400),
	                   (int16_t)(var_posElemCrds.z + 0x400));
	ax = (int16_t)(ax - 0x600);
	var_turnRadius = ax;
	if (ax > -0x96) goto loc_1E9AE;        /* cmp ax, 0FF6Ah ; jg */
	goto code_bto_blank;
loc_1E9AE:
	if (ax < 0x96) goto loc_1E9B6;
	goto code_bto_blank;
loc_1E9B6:
	current_surf_type = var_surfaceType;
	planindex = 2;
	byte_4392C = 0;
	if (var_turnRadius < -0x6C) goto loc_1E9D6;  /* cmp tr, 0FF94h ; jl */
	if (var_turnRadius > 0x6C) goto loc_1E9D6;
	goto code_bto_blank;
loc_1E9D6:
	ax = polarAngle((int16_t)(var_posElemCrds.x + 0x400),
	                (int16_t)(var_posElemCrds.z + 0x400));
	ax = (int16_t)(ax & 0xFF);             /* sub ah, ah */
	ax = (int16_t)(ax * 0x12);             /* mov cx,12h ; imul cx (low word) */
	var_misc22 = ax;
	ax = (int16_t)(ax >> 8);               /* mov cl, 8 ; sar ax, cl */
	ax = (int16_t)(0x11 - ax);             /* sub ax, 11h ; neg ax */
	var_misc1E = ax;
	wallHeight = 0x2A;
	elRdWallRelated = -12;
	if (var_turnRadius >= 0) goto loc_1EA1A;
	ax = (int16_t)(ax + 0x69);
	goto loc_1EA20;
loc_1EA1A:
	ax = (int16_t)(var_misc1E + 0x7B);
loc_1EA20:
	wallindex = ax;
	goto code_bto_blank;
code_bto_bankEntranceA:
	var_misc1C = 0x23;
	var_misc1E = 0;
	si = -0x2A0;                           /* mov si, 0FD60h */
	goto loc_1EA43;
code_bto_bankEntranceB:
	var_misc1C = 0x19;
	var_misc1E = 1;
	si = 0xA0;
loc_1EA43:
	if (var_absXElemCrds <= 0x78) goto loc_1EA4C;
	goto code_bto_blank;
loc_1EA4C:
	if (var_misc1E != 0) goto loc_1EA66;
	if (var_nextPosElemCrds.x > -0x78) goto loc_1EA66;   /* cmp nx, 0FF88h ; jg */
	var_wallOrientMod = 0x200;
	wallindex = 0x64;
	goto loc_1EA7D;
loc_1EA66:
	if (var_misc1E == 0) goto loc_1EA7D;
	if (var_nextPosElemCrds.x < 0x78) goto loc_1EA7D;
	var_wallOrientMod = 0x200;
	wallindex = 0x65;
loc_1EA7D:
	current_surf_type = var_surfaceType;
	if (var_posElemCrds.z >= -0x14E) goto loc_1EA90;     /* cmp z, 0FEB2h ; jge */
	ax = var_misc1C;
	goto loc_1EA9D;
loc_1EA90:
	if (var_posElemCrds.z < 0x14E) goto loc_1EAA4;
	ax = (int16_t)(var_misc1C + 9);
loc_1EA9D:
	planindex = ax;
	goto code_bto_blank;
loc_1EAA4:
	if (var_posElemCrds.z >= -0xA8) goto loc_1EABA;      /* cmp z, 0FF58h ; jge */
	ax = (int16_t)(var_misc1C + 1);
	planindex = ax;
	var_misc1E = 0;
	goto loc_1EAFD;
loc_1EABA:
	if (var_posElemCrds.z >= 0) goto loc_1EAD0;
	ax = (int16_t)(var_misc1C + 3);
	planindex = ax;
	var_misc1E = 1;
	goto loc_1EAFD;
loc_1EAD0:
	if (var_posElemCrds.z >= 0xA8) goto loc_1EAE8;
	ax = (int16_t)(var_misc1C + 5);
	planindex = ax;
	var_misc1E = 2;
	goto loc_1EAFD;
loc_1EAE8:
	if (var_posElemCrds.z >= 0x14E) goto loc_1EAFD;
	ax = (int16_t)(var_misc1C + 7);
	planindex = ax;
	var_misc1E = 3;
loc_1EAFD:
	/* ax = posz - bkRdEntr_triang_zAdjust[misc1E] */
	ax = (int16_t)(var_posElemCrds.z - bkRdEntr_triang_zAdjust[var_misc1E]);
	/* push ax ; push si ; call sin_fast ; push ; call multiply_and_scale */
	di = multiply_and_scale(sin_fast((uint16_t)si), ax);
	/* push posx ; push si ; call cos_fast ; push ; mul ; add ax, di */
	ax = (int16_t)(multiply_and_scale(cos_fast((uint16_t)si), var_posElemCrds.x) + di);
	var_misc22 = ax;
	if (ax > 0) goto loc_1EB3F;            /* or ax, ax ; jg */
	goto code_bto_blank;
loc_1EB3F:
	planindex++;                           /* inc planindex */
	goto code_bto_blank;
code_bto_bankRoad:
	if (var_absXElemCrds <= 0x78) goto loc_1EB4F;
	goto code_bto_blank;
loc_1EB4F:
	current_surf_type = var_surfaceType;
	planindex = 6;
	if (var_nextPosElemCrds.x >= 0x78) goto loc_1EB64;
	goto code_bto_blank;
loc_1EB64:
	var_wallOrientMod = 0x200;
	goto loc_1E87D;                        /* wallindex = 0x65 */
code_bto_bankCorner:
	ax = polarRadius2D((int16_t)(var_posElemCrds.x + 0x400),
	                   (int16_t)(var_posElemCrds.z + 0x400));
	ax = (int16_t)(ax - 0x600);
	var_turnRadius = ax;
	if (ax > -0x78) goto loc_1EB90;        /* cmp ax, 0FF88h ; jg */
	goto code_bto_blank;
loc_1EB90:
	if (ax < 0x7E) goto loc_1EB98;
	goto code_bto_blank;
loc_1EB98:
	ax = polarAngle((int16_t)(var_posElemCrds.x + 0x400),
	                (int16_t)(var_posElemCrds.z + 0x400));
	ax = (int16_t)(ax & 0xFF);             /* sub ah, ah */
	ax = (int16_t)(ax * 0x12);             /* imul cx (12h), low word */
	var_misc22 = ax;
	ax = (int16_t)(ax >> 8);               /* sar ax, 8 */
	ax = (int16_t)(0x11 - ax);             /* sub ax,11h ; neg ax */
	var_misc1E = ax;
	ax = (int16_t)(ax + 7);
	planindex = ax;
	current_surf_type = var_surfaceType;
	if (var_turnRadius > 0x66) goto loc_1EBD9;
	goto code_bto_blank;
loc_1EBD9:
	var_wallOrientMod = 0x200;
	ax = (int16_t)(var_misc1E + 0x7B);
	wallindex = ax;
loc_1EBE7:
	byte_4392C = 0;
	goto code_bto_blank;
code_bto_loop:
	if (var_posElemCrds.z >= 0) goto loc_1EC0A;
	var_misc1C = 0x33;
	var_06effX = (int16_t)(-var_posElemCrds.x);
	ax = (int16_t)(-var_posElemCrds.z);
	goto loc_1EC18;
loc_1EC0A:
	var_misc1C = 0x2D;
	var_06effX = var_posElemCrds.x;
	ax = var_posElemCrds.z;
loc_1EC18:
	var_02effZ = ax;
	ax = (int16_t)(loopSurface_maxZ - 1);  /* mov ax, loopSurface_maxZ ; dec ax */
	if (var_02effZ <= ax) goto loc_1EC38;
	ax = (int16_t)(loopSurface_maxZ + 0x64);
	if (var_02effZ <= ax) goto loc_1EC32;
	goto code_bto_loopBase;
loc_1EC32:
	ax = (int16_t)(loopSurface_maxZ - 1);
	goto loc_1EC3B;
loc_1EC38:
	ax = var_02effZ;
loc_1EC3B:
	var_misc1E = ax;
	si = 0;
	goto loc_1EC43;
loc_1EC42:
	si++;
loc_1EC43:
	if (loopSurface_ZBounds1[si] < var_misc1E) goto loc_1EC42;
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax > 0x20C) goto loc_1EC62;        /* upside-down limit */
	goto loc_1ED04;
loc_1EC62:
	si = (int16_t)(5 - si);                /* mov ax,5 ; sub ax,si ; mov si,ax */
	di = si;
	if (loopSurface_XBounds0[di] <= var_06effX) goto loc_1EC79;
	goto code_bto_blank;
loc_1EC79:
	ax = (int16_t)(loopSurface_XBounds1[di] + 0x190);
	if (ax >= var_06effX) goto loc_1EC88;
	goto code_bto_blank;
loc_1EC88:
	if (loopSurface_XBounds1[di] >= var_06effX) goto loc_1EC9D;
	ax = (int16_t)(loopSurface_XBounds0[di] + 0x190);
	if (ax > var_06effX) goto loc_1ECF3;
loc_1EC9D:
	/* (X0-X1)*(Z0-misc1E)/(Z1-Z0) */
	var_misc22 = (int16_t)(((int32_t)(int16_t)(loopSurface_XBounds0[di] - loopSurface_XBounds1[di]) *
	                        (int32_t)(int16_t)(loopSurface_ZBounds0[di] - var_misc1E)) /
	                       (int32_t)(int16_t)(loopSurface_ZBounds1[di] - loopSurface_ZBounds0[di]));
	var_misc3C = (int16_t)(loopSurface_XBounds0[di] + var_misc22);
	if (var_misc3C < var_06effX) goto loc_1ECE5;
	goto code_bto_blank;
loc_1ECE5:
	ax = (int16_t)(var_misc3C + 0x190);
	if (ax > var_06effX) goto loc_1ECF3;
	goto code_bto_blank;
loc_1ECF3:
	ax = (int16_t)(var_misc1C + si);
	planindex = ax;
	current_surf_type = var_surfaceType;
	goto loc_1EBE7;
loc_1ED04:
	if (si <= 1) goto loc_1ED1B;
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax >= 0x64) goto loc_1ED1B;
	goto code_bto_loopBase;
loc_1ED1B:
	di = si;
	if (loopSurface_XBounds0[di] <= var_06effX) goto loc_1ED2B;
	goto code_bto_loopBase;
loc_1ED2B:
	ax = (int16_t)(loopSurface_XBounds1[di] + 0x190);
	if (ax < var_06effX) goto code_bto_loopBase;
	if (loopSurface_XBounds1[di] >= var_06effX) goto loc_1ED4C;
	ax = (int16_t)(loopSurface_XBounds0[di] + 0x190);
	if (ax > var_06effX) goto loc_1ECF3;
loc_1ED4C:
	if (loopSurface_XBounds1[di] == loopSurface_XBounds0[di]) goto code_bto_loopBase;
	var_misc22 = (int16_t)(((int32_t)(int16_t)(loopSurface_XBounds0[di] - loopSurface_XBounds1[di]) *
	                        (int32_t)(int16_t)(loopSurface_ZBounds0[di] - var_misc1E)) /
	                       (int32_t)(int16_t)(loopSurface_ZBounds1[di] - loopSurface_ZBounds0[di]));
	var_misc3C = (int16_t)(loopSurface_XBounds0[di] + var_misc22);
	if (var_misc3C >= var_06effX) goto code_bto_loopBase;
	ax = (int16_t)(var_misc3C + 0x190);
	if (ax <= var_06effX) goto code_bto_loopBase;
	goto loc_1ECF3;
code_bto_loopBase:
	si = 0;
	goto loc_1EDB3;
loc_1EDB2:
	si++;
loc_1EDB3:
	if (loopBase_ZBounds1[si] < var_02effZ) goto loc_1EDB2;
	di = si;
	/* (Inn1-Inn0)*(effZ-Z0)/(Z1-Z0) + Inn0 */
	var_misc1E = (int16_t)((int16_t)(((int32_t)(int16_t)(loopBase_InnXBounds1[di] - loopBae_InnXBounds0[di]) *
	                                  (int32_t)(int16_t)(var_02effZ - loopBase_ZBounds0[di])) /
	                                 (int32_t)(int16_t)(loopBase_ZBounds1[di] - loopBase_ZBounds0[di])) +
	                       loopBae_InnXBounds0[di]);
	/* (Out1-Out0)*(effZ-Z0)/(Z1-Z0) + Out0 */
	var_misc22 = (int16_t)((int16_t)(((int32_t)(int16_t)(loopBase_OutXBounds1[di] - loopBase_OutXBounds0[di]) *
	                                  (int32_t)(int16_t)(var_02effZ - loopBase_ZBounds0[di])) /
	                                 (int32_t)(int16_t)(loopBase_ZBounds1[di] - loopBase_ZBounds0[di])) +
	                       loopBase_OutXBounds0[di]);
	if (var_06effX >= var_misc1E) goto loc_1EE35;
	goto code_bto_blank;
loc_1EE35:
	/* cmp effX, misc22 ; (loc_1E8DC) jle set_pavement / jmp blank */
	if (var_06effX <= var_misc22) goto code_set_pavement;
	goto code_bto_blank;
code_bto_tunnel:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax >= 0x90) goto loc_1EE5C;
	ax = (int16_t)(arg_nextPosWorldCrds->y - terrainHeight);
	if (ax < 0x90) goto loc_1EE76;
loc_1EE5C:
	if (var_absXElemCrds < 0x10E) goto loc_1EE66;
	goto code_bto_blank;
loc_1EE66:
	current_surf_type = var_surfaceType;
	planindex = 0x85;
	goto code_bto_blank;
loc_1EE76:
	if (var_absXElemCrds >= 0x78) goto loc_1EE82;
	current_surf_type = var_surfaceType;
loc_1EE82:
	if (var_posElemCrds.x < 0x78) goto loc_1EEC6;
	if (var_posElemCrds.x > 0x10E) goto loc_1EEC6;
	wallHeight = 0x90;
	if (var_nextPosElemCrds.z <= -0x200) goto loc_1EEE6;   /* cmp nz, 0FE00h */
	if (var_nextPosElemCrds.z >= 0x200) goto loc_1EEF7;
	if (var_nextPosElemCrds.x > 0x78) goto loc_1EEB2;
	wallindex = 0x98;
	goto code_bto_blank;
loc_1EEB2:
	if (var_nextPosElemCrds.x >= 0x10E) goto loc_1EEBC;
	goto code_bto_blank;
loc_1EEBC:
	wallindex = 0x96;
	goto code_bto_blank;
loc_1EEC6:
	if (var_posElemCrds.x <= -0x78) goto loc_1EECF;        /* cmp x, 0FF88h */
	goto code_bto_blank;
loc_1EECF:
	if (var_posElemCrds.x >= -0x10E) goto loc_1EED9;       /* cmp x, 0FEF2h */
	goto code_bto_blank;
loc_1EED9:
	wallHeight = 0x90;
	if (var_nextPosElemCrds.z > -0x200) goto loc_1EEF0;
loc_1EEE6:
	wallindex = 0x9A;
	goto code_bto_blank;
loc_1EEF0:
	if (var_nextPosElemCrds.z < 0x200) goto loc_1EF00;
loc_1EEF7:
	wallindex = 0x99;
	goto code_bto_blank;
loc_1EF00:
	if (var_nextPosElemCrds.x < -0x78) goto loc_1EF10;     /* cmp nx, 0FF88h */
	wallindex = 0x97;
	goto code_bto_blank;
loc_1EF10:
	if (var_nextPosElemCrds.x <= -0x10E) goto loc_1EF1A;   /* cmp nx, 0FEF2h */
	goto code_bto_blank;
loc_1EF1A:
	wallindex = 0x95;
	goto code_bto_blank;
code_bto_pipeEntrance:
	if (var_nextPosElemCrds.x >= 0) goto loc_1EF32;
	ax = (int16_t)(-var_nextPosElemCrds.x);
	goto loc_1EF35;
loc_1EF32:
	ax = var_nextPosElemCrds.x;
loc_1EF35:
	if (ax < 0x73) goto loc_1EF60;
	if (var_absXElemCrds > 0xA4) goto loc_1EF60;
	wallHeight = 0x97;
	if (var_nextPosElemCrds.x <= 0) goto loc_1EF56;
	wallindex = 0x9F;
	goto code_bto_blank;
loc_1EF56:
	wallindex = 0xA0;
	goto code_bto_blank;
loc_1EF60:
	if (var_absXElemCrds < 0x73) goto loc_1EF69;
	goto code_bto_blank;
loc_1EF69:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax < 0xAB) goto loc_1EF7B;
	goto code_bto_blank;
loc_1EF7B:
	current_surf_type = var_surfaceType;
	if (var_absXElemCrds >= 0x1F) goto loc_1EF90;
	planindex = 0x46;
	goto code_bto_blank;
loc_1EF90:
	if (var_posElemCrds.x >= -0x54) goto loc_1EFA6;        /* cmp x, 0FFACh */
	planindex = 0x49;
	var_misc1E = -0x64;                    /* mov misc1E, 0FF9Ch */
	si = -5;                               /* mov si, 0FFFBh */
	goto loc_1EFE0;
loc_1EFA6:
	if (var_posElemCrds.x >= 0) goto loc_1EFBC;
	planindex = 0x47;
	var_misc1E = -0x39;                    /* mov misc1E, 0FFC7h */
	si = -8;                               /* mov si, 0FFF8h */
	goto loc_1EFE0;
loc_1EFBC:
	if (var_posElemCrds.x <= 0x54) goto loc_1EFD2;
	planindex = 0x4D;
	var_misc1E = 0x64;
	si = 5;
	goto loc_1EFE0;
loc_1EFD2:
	planindex = 0x4B;
	var_misc1E = 0x39;
	si = 8;
loc_1EFE0:
	/* push posz ; push si ; call sin_fast ; push ; call multiply_and_scale */
	di = multiply_and_scale(sin_fast((uint16_t)si), var_posElemCrds.z);
	/* cx = posx - misc1E ; cos part ; add ax, di */
	ax = (int16_t)(multiply_and_scale(cos_fast((uint16_t)si),
	               (int16_t)(var_posElemCrds.x - var_misc1E)) + di);
	var_misc22 = ax;
	if (ax < 0) goto loc_1F01C;            /* or ax, ax ; jl */
	goto code_bto_blank;
loc_1F01C:
	goto loc_1EB3F;                        /* inc planindex ; blank */
code_bto_halfPipe:
	var_misc22 = 1;
	goto loc_1F02D;
code_bto_pipe:
	var_misc22 = 0;
loc_1F02D:
	if (var_nextPosElemCrds.x >= 0) goto loc_1F03A;
	ax = (int16_t)(-var_nextPosElemCrds.x);
	goto loc_1F03D;
loc_1F03A:
	ax = var_nextPosElemCrds.x;
loc_1F03D:
	if (ax < 0xA4) goto loc_1F068;
	if (var_absXElemCrds > 0xA4) goto loc_1F068;
	wallHeight = 0x97;
	if (var_nextPosElemCrds.x <= 0) goto loc_1F05E;
	wallindex = 0x9B;
	goto code_bto_blank;
loc_1F05E:
	wallindex = 0x9C;
	goto code_bto_blank;
loc_1F068:
	if (var_absXElemCrds < 0xA4) goto loc_1F072;
	goto code_bto_blank;
loc_1F072:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax < 0x109) goto loc_1F084;
	goto code_bto_blank;
loc_1F084:
	if (var_absXElemCrds >= 0x82) goto loc_1F091;
	current_surf_type = var_surfaceType;
loc_1F091:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax <= 0x97) goto loc_1F0A4;
	var_misc1E = 1;
	goto loc_1F0A9;
loc_1F0A4:
	var_misc1E = 0;
loc_1F0A9:
	if (var_misc22 == 0) goto loc_1F0E8;
	if (var_misc1E != 0) goto loc_1F0E8;
	if (var_absXElemCrds > 0x54) goto loc_1F0E8;
	if (var_absZElemCrds > 0x4B) goto loc_1F0E8;
	planindex = 0x45;
	if (var_nextPosElemCrds.z > -0x4B) goto loc_1F0D6;     /* cmp nz, 0FFB5h */
	wallindex = 0x9D;
	goto code_bto_blank;
loc_1F0D6:
	if (var_nextPosElemCrds.z >= 0x4B) goto loc_1F0DF;
	goto code_bto_blank;
loc_1F0DF:
	wallindex = 0x9E;
	goto code_bto_blank;
loc_1F0E8:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax <= 0x58) goto loc_1F116;
	if (var_misc1E != 0) goto loc_1F116;
	if (var_posElemCrds.x >= 0) goto loc_1F10C;
	planindex = 0x3C;
	goto code_bto_blank;
loc_1F10C:
	planindex = 0x42;
	goto code_bto_blank;
loc_1F116:
	if (var_absXElemCrds >= 0x1F) goto loc_1F136;
	if (var_misc1E == 0) goto loc_1F12C;
/* loc_1F122: */
	planindex = 0x3F;
	goto code_bto_blank;
loc_1F12C:
	planindex = 0x39;
	goto code_bto_blank;
loc_1F136:
	if (var_posElemCrds.x >= -0x54) goto loc_1F156;        /* cmp x, 0FFACh */
	if (var_misc1E == 0) goto loc_1F14C;
	planindex = 0x3D;
	goto code_bto_blank;
loc_1F14C:
	planindex = 0x3B;
	goto code_bto_blank;
loc_1F156:
	if (var_posElemCrds.x >= 0) goto loc_1F176;
	if (var_misc1E == 0) goto loc_1F16C;
	planindex = 0x3E;
	goto code_bto_blank;
loc_1F16C:
	planindex = 0x3A;
	goto code_bto_blank;
loc_1F176:
	if (var_posElemCrds.x <= 0x54) goto loc_1F196;
	if (var_misc1E == 0) goto loc_1F18C;
	planindex = 0x41;
	goto code_bto_blank;
loc_1F18C:
	planindex = 0x43;
	goto code_bto_blank;
loc_1F196:
	if (var_misc1E == 0) goto loc_1F1A6;
	planindex = 0x40;
	goto code_bto_blank;
loc_1F1A6:
	planindex = 0x44;
	goto code_bto_blank;
code_bto_corkLr:
	if (var_absXElemCrds < 0x96) goto loc_1F1BA;
	goto code_bto_blank;
loc_1F1BA:
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax < 0x109) goto loc_1F1CC;
	goto code_bto_blank;
loc_1F1CC:
	current_surf_type = var_surfaceType;
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax <= 0x97) goto loc_1F1E6;
	var_misc1E = 1;
	goto loc_1F1EB;
loc_1F1E6:
	var_misc1E = 0;
loc_1F1EB:
	var_misc22 = 0;
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax <= 0x58) goto loc_1F21A;
	if (var_misc1E != 0) goto loc_1F21A;
	if (var_posElemCrds.x >= 0) goto loc_1F212;
	var_misc22 = 3;
	goto loc_1F295;
loc_1F212:
	var_misc22 = 9;
	goto loc_1F295;
loc_1F21A:
	if (var_absXElemCrds >= 0x1F) goto loc_1F22E;
	if (var_misc1E == 0) goto loc_1F295;
	var_misc22 = 6;
	goto loc_1F295;
loc_1F22E:
	if (var_posElemCrds.x >= -0x54) goto loc_1F24A;        /* cmp x, 0FFACh */
	if (var_misc1E == 0) goto loc_1F242;
	var_misc22 = 4;
	goto loc_1F295;
loc_1F242:
	var_misc22 = 2;
	goto loc_1F295;
loc_1F24A:
	if (var_posElemCrds.x >= 0) goto loc_1F266;
	if (var_misc1E == 0) goto loc_1F25E;
	var_misc22 = 5;
	goto loc_1F295;
loc_1F25E:
	var_misc22 = 1;
	goto loc_1F295;
loc_1F266:
	if (var_posElemCrds.x <= 0x54) goto loc_1F282;
	if (var_misc1E == 0) goto loc_1F27A;
	var_misc22 = 8;
	goto loc_1F295;
loc_1F27A:
	var_misc22 = 0x0A;
	goto loc_1F295;
loc_1F282:
	if (var_misc1E == 0) goto loc_1F290;
	var_misc22 = 7;
	goto loc_1F295;
loc_1F290:
	var_misc22 = 0x0B;
loc_1F295:
	if (var_misc22 == 0) goto loc_1F2B8;
	di = var_misc22;                       /* mov di, misc22 ; shl di, 1 */
	ax = var_posElemCrds.z;
	if (corkLR_negZBound[di] >= ax) goto loc_1F2B8;
	if (corkLR_posZBound[di] <= ax) goto loc_1F2B8;
	ax = (int16_t)(var_misc22 + 0x39);
	planindex = ax;
loc_1F2B8:
	if (planindex == 0) goto loc_1F2C2;
	goto code_bto_blank;
loc_1F2C2:
	if (var_absZElemCrds < 0x200) goto loc_1F2CC;
	goto code_bto_blank;
loc_1F2CC:
	wallindex = 0xB9;
	corkFlag = 1;
	wallHeight = 0x75;
	goto code_bto_blank;
code_bto_corkUdLH:
	var_misc1E = (int16_t)(-var_posElemCrds.x);
	var_misc22 = 0x4F;
	var_C = 0x32;
	var_E = 0x4B;
	goto loc_1F30F;
code_bto_corkUdRH:
	var_misc1E = var_posElemCrds.x;
	var_misc22 = 0x69;
	var_C = 0;
	var_E = 0x19;
loc_1F30F:
	corkFlag = 1;
	if (var_posElemCrds.z >= 0) goto loc_1F350;
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax >= 0x64) goto loc_1F350;
	if (var_misc1E <= 0) goto loc_1F350;
	if (var_misc1E < 0x278) goto loc_1F339;
	goto code_bto_blank;
loc_1F339:
	if (var_misc1E > 0x188) goto loc_1F343;
	goto code_bto_blank;
loc_1F343:
	current_surf_type = var_surfaceType;
	ax = var_misc22;
	goto loc_1EA9D;                        /* planindex = ax ; blank */
loc_1F350:
	if (var_posElemCrds.z <= 0) goto loc_1F3A8;
	ax = (int16_t)(arg_posWorldCrds->y - terrainHeight);
	if (ax <= 0x15E) goto loc_1F3A8;
	if (var_misc1E >= 0x2B4) goto loc_1F3A8;
	if (var_misc1E <= 0x14C) goto loc_1F3A8;
	wallHeight = 0x2A;
	elRdWallRelated = -12;                 /* mov elRdWallRelated, 0FFF4h */
	if (var_misc1E <= 0x200) goto loc_1F38C;
	ax = var_C;
	goto loc_1F38F;
loc_1F38C:
	ax = var_E;
loc_1F38F:
	ax = (int16_t)(ax + 0x18);
	wallindex = ax;
	current_surf_type = var_surfaceType;
	ax = (int16_t)(var_misc22 + 0x19);
	planindex = ax;
	goto loc_1EBE7;                        /* byte_4392C = 0 ; blank */
loc_1F3A8:
	/* push posz ; push misc1E ; call polarRadius2D */
	ax = polarRadius2D(var_misc1E, var_posElemCrds.z);
	var_turnRadius = ax;
	if (ax > 0x14C) goto loc_1F3C1;
	goto code_bto_blank;
loc_1F3C1:
	if (ax < 0x2B4) goto loc_1F3C9;
	goto code_bto_blank;
loc_1F3C9:
	ax = polarAngle(var_misc1E, var_posElemCrds.z);
	ax = (int16_t)(0x100 - ax);            /* sub ax,100h ; neg ax */
	ax &= 0x3FF;                           /* and ah, 3 */
	ax = (int16_t)(ax * 0x18);             /* mov cx,18h ; imul cx (low word) */
	si = (int16_t)(ax >> 10);              /* mov si,ax ; mov cl,0Ah ; sar si,cl */
	ax = (int16_t)(var_misc22 + si + 1);   /* add ax, si ; inc ax */
	planindex = ax;
	current_surf_type = var_surfaceType;
	byte_4392C = 0;
	wallHeight = 0x2A;
	elRdWallRelated = -12;
	ax = (int16_t)(var_turnRadius - 0x200);
	if (ax <= 0x5A) goto loc_1F41E;
	ax = var_C;
loc_1F418:
	ax = (int16_t)(ax + si);
	goto loc_1EA20;                        /* wallindex = ax ; blank */
loc_1F41E:
	ax = (int16_t)(var_turnRadius - 0x200);
	if (ax < -0x5A) goto loc_1F42C;        /* cmp ax, 0FFA6h ; jl */
	goto code_bto_blank;
loc_1F42C:
	ax = var_E;
	goto loc_1F418;
code_bto_slalom:
	if (var_absXElemCrds >= 0x78) goto loc_1F43E;
	current_surf_type = var_surfaceType;
loc_1F43E:
	if (var_posElemCrds.x < 0x17) goto loc_1F4A0;
	if (var_posElemCrds.x > 0x61) goto loc_1F4A0;
	if (var_posElemCrds.z <= -0x10F) goto loc_1F4A0;       /* cmp z, 0FEF1h */
	if (var_posElemCrds.z >= -0xF1) goto loc_1F4A0;        /* cmp z, 0FF0Fh */
	wallHeight = 0x2A;
	if (var_nextPosElemCrds.z >= -0x10F) goto loc_1F46E;
	wallindex = 0x91;
	goto code_bto_blank;
loc_1F46E:
	if (var_nextPosElemCrds.z <= -0xF1) goto loc_1F47E;
	wallindex = 0x92;
	goto code_bto_blank;
loc_1F47E:
	if (var_nextPosElemCrds.x >= 0x17) goto loc_1F48E;
	wallindex = 0x94;
	goto code_bto_blank;
loc_1F48E:
	if (var_nextPosElemCrds.x > 0x61) goto loc_1F497;
	goto code_bto_blank;
loc_1F497:
	wallindex = 0x93;
	goto code_bto_blank;
loc_1F4A0:
	if (var_posElemCrds.x <= -0x17) goto loc_1F4A9;        /* cmp x, 0FFE9h */
	goto code_bto_blank;
loc_1F4A9:
	if (var_posElemCrds.x >= -0x61) goto loc_1F4B2;        /* cmp x, 0FF9Fh */
	goto code_bto_blank;
loc_1F4B2:
	if (var_posElemCrds.z < 0x10F) goto loc_1F4BC;
	goto code_bto_blank;
loc_1F4BC:
	if (var_posElemCrds.z > 0xF1) goto loc_1F4C6;
	goto code_bto_blank;
loc_1F4C6:
	wallHeight = 0x2A;
	if (var_nextPosElemCrds.z <= 0x10F) goto loc_1F4DC;
	wallindex = 0x8D;
	goto code_bto_blank;
loc_1F4DC:
	if (var_nextPosElemCrds.z >= 0xF1) goto loc_1F4EC;
	wallindex = 0x8E;
	goto code_bto_blank;
loc_1F4EC:
	if (var_nextPosElemCrds.x <= -0x17) goto loc_1F4FC;
	wallindex = 0x8F;
	goto code_bto_blank;
loc_1F4FC:
	if (var_nextPosElemCrds.x < -0x61) goto loc_1F505;
	goto code_bto_blank;
loc_1F505:
	wallindex = 0x90;
	goto code_bto_blank;
code_bto_barn:
	if (var_absXElemCrds <= 0x96) goto loc_1F518;
	goto code_bto_blank;
loc_1F518:
	if (var_absZElemCrds <= 0x96) goto loc_1F522;
	goto code_bto_blank;
loc_1F522:
	wallHeight = 0x1A9;
	if (var_nextPosElemCrds.z > -0x96) goto loc_1F538;     /* cmp nz, 0FF6Ah */
	wallindex = 0xA1;
	goto code_bto_blank;
loc_1F538:
	if (var_nextPosElemCrds.z < 0x96) goto loc_1F548;
	wallindex = 0xA2;
	goto code_bto_blank;
loc_1F548:
	if (var_nextPosElemCrds.x < 0x96) goto loc_1F558;
	wallindex = 0xA3;
	goto code_bto_blank;
loc_1F558:
	if (var_nextPosElemCrds.x <= -0x96) goto loc_1F562;
	goto code_bto_blank;
loc_1F562:
	wallindex = 0xA4;
	goto code_bto_blank;
code_bto_gasStation:
	if (var_posElemCrds.x >= -0xC8) goto loc_1F576;        /* cmp x, 0FF38h */
	goto code_bto_blank;
loc_1F576:
	if (var_posElemCrds.x <= 0x104) goto loc_1F580;
	goto code_bto_blank;
loc_1F580:
	if (var_absZElemCrds <= 0x50) goto loc_1F589;
	goto code_bto_blank;
loc_1F589:
	wallHeight = 0xE6;
	if (var_nextPosElemCrds.z > -0x50) goto loc_1F59E;     /* cmp nz, 0FFB0h */
	wallindex = 0xA5;
	goto code_bto_blank;
loc_1F59E:
	if (var_nextPosElemCrds.z < 0x50) goto loc_1F5AE;
	wallindex = 0xA8;
	goto code_bto_blank;
loc_1F5AE:
	if (var_nextPosElemCrds.x > -0xC8) goto loc_1F5BE;     /* cmp nx, 0FF38h */
	wallindex = 0xA6;
	goto code_bto_blank;
loc_1F5BE:
	if (var_nextPosElemCrds.x >= 0x104) goto loc_1F5C8;
	goto code_bto_blank;
loc_1F5C8:
	wallindex = 0xA7;
	goto code_bto_blank;
code_bto_joes:
	if (var_absXElemCrds <= 0xB4) goto loc_1F5DC;
	goto code_bto_blank;
loc_1F5DC:
	if (var_absZElemCrds <= 0x64) goto loc_1F5E5;
	goto code_bto_blank;
loc_1F5E5:
	wallHeight = 0xF8;
	if (var_nextPosElemCrds.z > -0x64) goto loc_1F5FA;     /* cmp nz, 0FF9Ch */
	wallindex = 0xA9;
	goto code_bto_blank;
loc_1F5FA:
	if (var_nextPosElemCrds.z < 0x64) goto loc_1F60A;
	wallindex = 0xAC;
	goto code_bto_blank;
loc_1F60A:
	if (var_nextPosElemCrds.x > -0xB4) goto loc_1F61A;     /* cmp nx, 0FF4Ch */
	wallindex = 0xAB;
	goto code_bto_blank;
loc_1F61A:
	if (var_nextPosElemCrds.x >= 0xB4) goto loc_1F624;
	goto code_bto_blank;
loc_1F624:
	wallindex = 0xAA;
	goto code_bto_blank;
code_bto_office:
	if (var_absXElemCrds <= 0xC8) goto loc_1F638;
	goto code_bto_blank;
loc_1F638:
	if (var_absZElemCrds <= 0xC8) goto loc_1F642;
	goto code_bto_blank;
loc_1F642:
	wallHeight = 0x226;
	if (var_nextPosElemCrds.z > -0xC8) goto loc_1F658;     /* cmp nz, 0FF38h */
	wallindex = 0xAD;
	goto code_bto_blank;
loc_1F658:
	if (var_nextPosElemCrds.z < 0xC8) goto loc_1F668;
	wallindex = 0xAE;
	goto code_bto_blank;
loc_1F668:
	if (var_nextPosElemCrds.x > -0xC8) goto loc_1F678;
	wallindex = 0xAF;
	goto code_bto_blank;
loc_1F678:
	if (var_nextPosElemCrds.x >= 0xC8) goto loc_1F682;
	goto code_bto_blank;
loc_1F682:
	wallindex = 0xB0;
	goto code_bto_blank;
code_bto_windmill:
	if (var_absXElemCrds <= 0x72) goto loc_1F695;
	goto code_bto_blank;
loc_1F695:
	if (var_absZElemCrds <= 0x72) goto loc_1F69E;
	goto code_bto_blank;
loc_1F69E:
	wallHeight = 0x1EF;
	if (var_nextPosElemCrds.z > -0x72) goto loc_1F6B4;     /* cmp nz, 0FF8Eh */
	wallindex = 0xB4;
	goto code_bto_blank;
loc_1F6B4:
	if (var_nextPosElemCrds.z < 0x72) goto loc_1F6C4;
	wallindex = 0xB2;
	goto code_bto_blank;
loc_1F6C4:
	if (var_nextPosElemCrds.x > -0x72) goto loc_1F6D4;
	wallindex = 0xB1;
	goto code_bto_blank;
loc_1F6D4:
	if (var_nextPosElemCrds.x >= 0x72) goto loc_1F6DD;
	goto code_bto_blank;
loc_1F6DD:
	wallindex = 0xB3;
	goto code_bto_blank;
code_bto_ship:
	if (var_posElemCrds.x >= -0xAA) goto loc_1F6F0;        /* cmp x, 0FF56h */
	goto code_bto_blank;
loc_1F6F0:
	if (var_posElemCrds.x <= 0x104) goto loc_1F6FA;
	goto code_bto_blank;
loc_1F6FA:
	if (var_absZElemCrds <= 0x6E) goto loc_1F703;
	goto code_bto_blank;
loc_1F703:
	wallHeight = 0xE6;
	if (var_nextPosElemCrds.z > -0x6E) goto loc_1F71A;     /* cmp nz, 0FF92h */
	wallindex = 0xB5;
	goto code_bto_blank;
loc_1F71A:
	if (var_nextPosElemCrds.z < 0x6E) goto loc_1F72A;
	wallindex = 0xB8;
	goto code_bto_blank;
loc_1F72A:
	if (var_nextPosElemCrds.x > -0xAA) goto loc_1F73C;
	wallindex = 0xB7;
	goto code_bto_blank;
loc_1F73C:
	if (var_nextPosElemCrds.x >= 0x104) goto loc_1F746;
	goto code_bto_blank;
loc_1F746:
	wallindex = 0xB6;
	goto code_bto_blank;
code_bto_blank:
	if (var_tileTerr >= 7) goto code_hillslope_parsing;    /* jnb (unsigned) */
	goto loc_1F8CD;
code_hillslope_parsing:
	/* pos crds relative to the raw tile center (before multi-tile fixups) */
	var_posElemCrds.x = (int16_t)(arg_posWorldCrds->x -
	                              trackcenterpos2[(int16_t)var_trkCol]);
	var_posElemCrds.z = (int16_t)(arg_posWorldCrds->z -
	                              terraincenterpos[(int16_t)var_trkRow]);
	ax = (int16_t)((uint8_t)var_tileTerr - 7);   /* sub ax, 7 */
	if ((uint16_t)ax > 0x0B) goto loc_1F896;     /* cmp ax, 0Bh ; ja */
	/* jmp cs:off_1F87E[ax*2]: table is {1F82A,1F832,1F84E,1F866} x3 */
	switch ((uint16_t)ax & 3) {
	case 0: goto loc_1F82A;
	case 1: goto loc_1F832;
	case 2: goto loc_1F84E;
	default: goto loc_1F866;
	}
loc_1F82A:
	var_elemOrient = 0;
	goto loc_1F896;
loc_1F832:
	var_elemOrient = 0x300;
	ax = var_posElemCrds.x;
	var_misc1E = ax;
	var_posElemCrds.x = var_posElemCrds.z;
	ax = (int16_t)(-var_misc1E);
loc_1F848:
	var_posElemCrds.z = ax;
	goto loc_1F896;
loc_1F84E:
	var_elemOrient = 0x200;
	var_posElemCrds.z = (int16_t)(-var_posElemCrds.z);
	var_posElemCrds.x = (int16_t)(-var_posElemCrds.x);
	goto loc_1F896;
loc_1F866:
	var_elemOrient = 0x100;
	ax = var_posElemCrds.x;
	var_misc1E = ax;
	var_posElemCrds.x = (int16_t)(-var_posElemCrds.z);
	ax = var_misc1E;
	goto loc_1F848;
loc_1F896:
	ax = var_tileTerr;                     /* mov al, var_tileTerr ; sub ah, ah */
	if ((uint16_t)ax < 7) goto loc_1F8CD;  /* jb */
	if ((uint16_t)ax <= 0x0A) goto loc_1F8C0;
	if ((uint16_t)ax < 0x0B) goto loc_1F8CD;
	if ((uint16_t)ax <= 0x0E) goto loc_1F8FC;
	if ((uint16_t)ax < 0x0F) goto loc_1F8CD;
	if ((uint16_t)ax <= 0x12) goto loc_1F940;
	goto loc_1F8CD;                        /* loc_1F8BC */
loc_1F8C0:
	if (planindex != 0) goto loc_1F8CD;
/* loc_1F8C7: */
	planindex = 3;
loc_1F8CD:
	if (planindex > 0) goto loc_1F8D7;
	goto loc_1F992;
loc_1F8D7:
	planindex = (int16_t)(planindex << 2); /* mov cl,2 ; shl planindex, cl */
	ax = var_elemOrient;
	if (ax != 0x100) goto loc_1F8E8;
	goto loc_1F9CC;
loc_1F8E8:
	if (ax != 0x200) goto loc_1F8F0;
	goto loc_1F9C4;
loc_1F8F0:
	if (ax != 0x300) goto loc_1F8F8;
	goto loc_1F98E;
loc_1F8F8:
	goto loc_1F992;
loc_1F8FC:
	/* downhill-facing slope check with angle 0FF80h */
	di = multiply_and_scale(sin_fast(0xFF80), var_posElemCrds.z);
	ax = (int16_t)(multiply_and_scale(cos_fast(0xFF80), var_posElemCrds.x) + di);
	var_misc22 = ax;
	if (ax >= 0) goto loc_1F8CD;           /* or ax, ax ; jge */
	planindex = 4;
	goto loc_1F8CD;
loc_1F940:
	di = multiply_and_scale(sin_fast(0xFF80), var_posElemCrds.z);
	ax = (int16_t)(multiply_and_scale(cos_fast(0xFF80), var_posElemCrds.x) + di);
	var_misc22 = ax;
	if (ax <= 0) goto loc_1F984;           /* or ax, ax ; jle */
	planindex = 5;
	goto loc_1F8CD;
loc_1F984:
	terrainHeight = 0x1C2;
	goto loc_1F8CD;
loc_1F98E:
	planindex++;                           /* inc planindex */
loc_1F992:
	/* mov ax,22h ; imul planindex ; add ax,word ptr planptr ...
	 * (22h = sizeof PLANE) -> current_planptr = &planptr[planindex] */
	current_planptr = &planptr[planindex];
	if (current_surf_type != 4) goto loc_1F9D4;  /* cmp current_surf_type, 4 */
	/* water shimmer: ax = ((worldZ ^ worldX) >> 8) & 1 ; terrainHeight += ax
	 * (note: surf type 4 is grass by the constants, but this matches the
	 * original compare against 4 exactly) */
	ax = (int16_t)(arg_posWorldCrds->z ^ arg_posWorldCrds->x);
	ax = (int16_t)(ax >> 8);               /* mov cl, 8 ; sar ax, cl */
	ax &= 1;                               /* and ax, 1 */
	terrainHeight = (int16_t)(terrainHeight + ax);
	goto loc_1F9D9;
loc_1F9C4:
	planindex = (int16_t)(planindex + 2);
	goto loc_1F992;
loc_1F9CC:
	planindex = (int16_t)(planindex + 3);
	goto loc_1F992;
loc_1F9D4:
	terrainHeight = (int16_t)(terrainHeight + 2);
loc_1F9D9:
	if (wallindex >= 0) goto loc_1F9E3;    /* cmp wallindex, 0 ; jge */
	goto loc_1FADE;
loc_1F9E3:
	/* bx = wallindex*6 ; les di, wallptr ; ax = es:[bx+di] (wall angle) ;
	 * neg ax ; add ax, elemOrient ; add ax, wallOrientMod ; and ah, 3 */
	ax = (int16_t)(var_elemOrient + var_wallOrientMod -
	               wallptr[wallindex * 3]);
	ax &= 0x3FF;
	wallOrientation = ax;
	ax = var_elemOrient;
	if (ax == 0) goto loc_1FA20;           /* or ax, ax ; jz */
	if (ax != 0x100) goto loc_1FA13;
	goto loc_1FAA6;
loc_1FA13:
	if (ax == 0x200) goto loc_1FA78;
	if (ax == 0x300) goto loc_1FA4C;
	goto loc_1FAD0;
loc_1FA20:
	/* var_curr_wallptr = wallptr + wallindex*6 ;
	 * wallStartX = rec[+2], wallStartZ = rec[+4] */
	var_curr_wallptr = &wallptr[wallindex * 3];
	wallStartX = var_curr_wallptr[1];
	ax = var_curr_wallptr[2];
loc_1FA45:
	wallStartZ = ax;
	goto loc_1FAD0;
loc_1FA4C:
	/* orient 300h: wallStartX = -rec[+4], wallStartZ = rec[+2] */
	var_curr_wallptr = &wallptr[wallindex * 3];  /* loc_1FA5B */
	ax = (int16_t)(-var_curr_wallptr[2]);
	wallStartX = ax;
	ax = var_curr_wallptr[1];
	goto loc_1FA45;
loc_1FA78:
	/* orient 200h: wallStartX = -rec[+2], wallStartZ = -rec[+4] */
	var_curr_wallptr = &wallptr[wallindex * 3];
	ax = (int16_t)(-var_curr_wallptr[1]);
	wallStartX = ax;
	ax = var_curr_wallptr[2];
/* loc_1FAA1: */
	ax = (int16_t)(-ax);
	goto loc_1FA45;
loc_1FAA6:
	/* orient 100h: wallStartX = rec[+4], wallStartZ = -rec[+2] */
	var_curr_wallptr = &wallptr[wallindex * 3];
	wallStartX = var_curr_wallptr[2];
	ax = var_curr_wallptr[1];
	ax = (int16_t)(-ax);                   /* loc_1FAA1 */
	goto loc_1FA45;
loc_1FAD0:
	wallStartX = (int16_t)(wallStartX + elem_xCenter);
	wallStartZ = (int16_t)(wallStartZ + elem_zCenter);
loc_1FADE:
	if (dbg_bto_n > 0 && dbg_bto_n <= 8) {
		dbg_bto[dbg_bto_n - 1][6] = terrainHeight;
		dbg_bto[dbg_bto_n - 1][7] = planindex;
	}
	return;
}
