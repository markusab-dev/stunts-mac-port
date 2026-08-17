/*
 * rdraw_dispatch.c - Instruction-exact C translation of the polygon-list
 * dispatch loop `get_a_poly_info` (seg006.asm lines 2062-2292) plus the
 * wheel-fill call chain it needs that existed only in asm:
 *
 *   get_a_poly_info            seg006.asm 2062-2292
 *   preRender_wheel            seg022.asm   50-300
 *   preRender_wheel_helper     seg023.asm   50-136
 *   preRender_wheel_helper2    seg019.asm   50-173
 *   preRender_wheel_helper3    seg014.asm   53-312
 *   preRender_sphere           seg012.asm 10164-10367
 *   preRender_sphere_helper    seg020.asm   53-88
 *   preRender_sphere_helper2   seg015.asm   50-440
 *
 * get_a_poly_info walks the depth-sorted linked list built by
 * transformed_shape_op / insert_newest_poly_in_poly_linked_list_40ED6 and
 * dispatches every stored polyinfo record to a fill routine.
 *
 * Polyinfo record layout (written in transformed_shape_op, loc_25D3C):
 *   +0  uint16  average depth (var_18 / numverts) - heap/sort key
 *   +2  uint8   material / paintjob index (backlight override for 0x2D)
 *   +3  uint8   vertex count
 *   +4  uint8   primitive type: 0=polygon, 1=line, 2=sphere, 3=wheel,
 *               5=single pixel (particle)
 *   +5  uint8   (padding, never read)
 *   +6  ...     vertex count x POINT2D { int16 px, py } screen points
 *               (wheel records: exactly 4 points)
 *
 * Translation rules (match rasm_port.c):
 *   - 16-bit registers modelled as int16_t/uint16_t locals named after the
 *     register; far pointers become native pointers.
 *   - jl/jg/jle/jge = signed int16 compares; jb/ja = unsigned.
 *   - Original label names preserved; goto structure kept 1:1.
 *   - Original asm kept as comments beside nontrivial translations.
 *
 * Calling convention note: the original C segments were built with
 * `bcc /u-` (Borland cdecl, right-to-left push), so the last value pushed
 * in the asm is the callee's FIRST parameter.  All call sites below were
 * mapped that way.
 *
 * `offset trkObjectList.ss_ssOvelay+460h` (seg006.asm 2261) and
 * `offset unk_3F0AE` (seg014.asm 143) are disassembler-mislabeled numeric
 * constants: with dseg based at 0x3B770, trkObjectList sits at dseg offset
 * 0x2098, so the operands assemble to 0x2098+8+0x460 = 0x2500 and
 * 0x3F0AE-0x3B770 = 0x393E.  Both are only ever used as multiply_and_scale
 * factors (n/0x4000 fixed point), never dereferenced.
 */

#include "externs.h"   /* material_*_ptr_cpy, fatal_error */
#include "shape3d.h"   /* polyinfo_reset(), get_a_poly_info() prototypes */
#include "math.h"      /* struct POINT2D, multiply_and_scale */
#include "shape2d.h"   /* struct SPRITE (preRender_sphere's clip window) */
#include "rfbsize.h"   /* RFB_SPANROWS */

/* ------------------------------------------------------------------ */
/* Globals shared with shape3d.c (declarations match shape3d.c)        */
/* ------------------------------------------------------------------ */
extern int16_t poly_linked_list_40ED6[];   /* shape3d.c:2574 */
extern int16_t far* polyinfoptrs[];        /* shape3d.c:133  */
extern uint16_t polyinfonumpolys;          /* shape3d.c:134  */

/* ------------------------------------------------------------------ */
/* Fill routines already ported in shape3d.c                           */
/* (prototypes copied verbatim from their definitions)                 */
/* ------------------------------------------------------------------ */
extern void preRender_default(uint16_t arg_color, uint16_t arg_vertlinecount,
                              uint16_t* arg_vertlines);
extern void preRender_default_alt(uint16_t arg_color,
                                  uint16_t arg_vertlinecount,
                                  uint16_t* arg_vertlines);
extern void preRender_patterned(uint16_t unk, uint16_t arg_color,
                                uint16_t arg_vertlinecount,
                                struct POINT2D* arg_vertlines);
extern void preRender_unk(uint16_t arg_color, uint16_t unk,
                          uint16_t arg_vertlinecount,
                          struct POINT2D* arg_vertlines, uint16_t unk2);
extern void preRender_wheel_helper4(uint16_t arg_color,
                                    uint16_t arg_vertlinecount,
                                    struct POINT2D arg_vertlines[]);
/* preRender_line comes from rport.h (rblit.c). */

/* ------------------------------------------------------------------ */
/* The sphere fill                                                     */
/*                                                                     */
/* [CORRECTED 2026-08-17] Until today this file carried the comment    */
/* "preRender_sphere and putpixel_single_maybe are `extrn ...:proc` in */
/* seg012.inc but have no proc body in any asm/ or asmorig/ file", and */
/* both routines called fatal_error().  That comment was false.  All   */
/* of it is in the reference tree:                                     */
/*                                                                     */
/*   preRender_sphere          seg012.asm 10164-10367 (public at :287) */
/*   preRender_sphere_helper   seg020.asm    53-88                     */
/*   preRender_sphere_helper2  seg015.asm    50-440                    */
/*   putpixel_single_maybe     seg012.asm 17884-17929 (public at :374) */
/*                                                                     */
/* How the wrong claim survived: seg001, seg004, seg009, seg010,       */
/* seg012, seg023 and seg028 contain bytes that make grep treat them   */
/* as binary, so a plain `grep preRender_sphere .../asm/*.asm` prints  */
/* only the seg006 call site and the seg012.inc `extrn` - and silently */
/* skips the one file that holds the body.  `grep -a` finds it.  The   */
/* `extrn` in seg012.inc is the *importing* side of the symbol; the    */
/* matching `public` is in seg012.asm itself.                          */
/*                                                                     */
/* putpixel_single_maybe lives in rblit.c, next to the framebuffer it  */
/* writes.                                                             */
/*                                                                     */
/* How reachable was the crash?  Measured, by walking every loaded     */
/* SHAPE3D's primitive stream (stride 2 + numpaints +                  */
/* primidxcounttab[type]) for all 116 scenery shapes and all 11 cars:  */
/* the shipped data contains exactly ONE primitive of file type 11     */
/* (-> primtype 2), in `car0` of PMIN (game3dshapes[124]), and NO      */
/* primitive of file type 1 (-> primtype 5, the single pixel).  Slot   */
/* 124 is written only by run_car_menu (seg000.asm:3029), i.e. the     */
/* rotating model on the car-selection turntable, and the other two    */
/* putpixel_single_maybe call sites are in run_car_menu (seg000:3699)  */
/* and intro_op (seg003:7003, 7110).  None of those three screens is   */
/* ported yet, so measured over all 12 replays in tests/replays with   */
/* every camera mode and detail level the call count is 0 - the crash  */
/* was latent rather than live.  It becomes live the moment the car    */
/* turntable or the intro lands, which is why it is fixed now.         */
/* ------------------------------------------------------------------ */

extern const uint16_t word_3F3C6;              /* rdata.c: dseg 0x3C56 = 40 */
extern const uint8_t* const off_3F3C8[40];     /* rdata.c: dseg 0x3C58     */
extern struct SPRITE far sprite1;              /* rblit.c                  */

/* ------------------------------------------------------------------ */
/* preRender_sphere_helper2 - seg015.asm 50-440                        */
/*                                                                     */
/* arg_0 is three POINT2D: the ellipse centre and the two half-axis    */
/* endpoints p1 and p2.  arg_2 receives 32 POINT2D forming a 32-gon.   */
/*                                                                     */
/* Construction, in relative coordinates (P1 = p1-c, P2 = p2-c):       */
/* points 0..15 sweep the half turn P1 -> P2 -> -P1 as normalized sums */
/* P1 + k/4 P2 (k = 0..4) and k/4 P1 + P2 down to -P1 + k/4 P2, with   */
/* one fixed-point scale per k that renormalizes the sum back to the   */
/* radius: 3E17h/4000h = 0.9702 = 1/sqrt(1+1/16), 393Eh = 0.8955 =     */
/* 1/sqrt(1+1/4), 3333h = 0.8000 = 1/sqrt(1+9/16), 2D41h = 0.7071 =    */
/* 1/sqrt(2).  Points 16..31 are the reflection through the centre.    */
/* Identical in shape to preRender_wheel_helper3 below, which builds   */
/* the 16-gon wheel from the same idea with half the resolution.       */
/* ------------------------------------------------------------------ */
void preRender_sphere_helper2(int16_t* arg_0, int16_t* arg_2)
{
	int16_t var_1A, var_18, var_16, var_14, var_12, var_10;
	int16_t var_E, var_C, var_A, var_8, var_6, var_4, var_2;
	int16_t si, di, ax;
	int16_t* bx;

	bx = arg_0;                                     /* mov bx, [bp+arg_0] */
	si = bx[0];                                     /* mov si, [bx] */
	ax = bx[2] - si;                                /* mov ax,[bx+4] ; sub ax,si */
	arg_2[0] = ax;                                  /* mov [bx], ax */
	ax = arg_0[3] - arg_0[1];                       /* [bx+6] - [bx+2] */
	arg_2[1] = ax;                                  /* mov [bx+2], ax */
	ax = arg_0[4] - si;                             /* [bx+8] - si */
	arg_2[16] = ax;                                 /* mov [bx+20h], ax */
	ax = arg_0[5] - arg_0[1];                       /* [bx+0Ah] - [bx+2] */
	arg_2[17] = ax;                                 /* mov [bx+22h], ax */

	/* P1 and P2 pre-divided by 2, 4 and (3/4) - `sar` is arithmetic. */
	ax = arg_2[0] >> 1;   var_2  = ax;              /* mov ax,[bx] ; sar ax,1 */
	ax >>= 1;             var_A  = ax;              /* sar ax,1 */
	var_6 = var_2 + var_A;
	ax = arg_2[1] >> 1;   var_4  = ax;              /* mov ax,[bx+2] ; sar ax,1 */
	ax >>= 1;             var_C  = ax;
	var_8 = var_4 + var_C;
	ax = arg_2[16] >> 1;  var_10 = ax;              /* mov ax,[bx+20h] ; sar ax,1 */
	ax >>= 1;             var_18 = ax;
	var_14 = var_10 + var_18;
	ax = arg_2[17] >> 1;  var_12 = ax;              /* mov ax,[bx+22h] ; sar ax,1 */
	ax >>= 1;             var_1A = ax;
	var_16 = var_12 + var_1A;

	/* point 4  = (P1 + P2) * 2D41h                                    */
	arg_2[8]  = multiply_and_scale(arg_2[16] + arg_2[0], 0x2D41);
	arg_2[9]  = multiply_and_scale(arg_2[1] + arg_2[17], 0x2D41);
	/* point 2  = (P1 + P2/2) * 393Eh                                  */
	arg_2[4]  = multiply_and_scale(arg_2[0] + var_10, 0x393E);
	arg_2[5]  = multiply_and_scale(arg_2[1] + var_12, 0x393E);
	/* point 6  = (P2 + P1/2) * 393Eh                                  */
	arg_2[12] = multiply_and_scale(arg_2[16] + var_2, 0x393E);
	arg_2[13] = multiply_and_scale(arg_2[17] + var_4, 0x393E);
	/* point 1  = (P1 + P2/4) * 3E17h                                  */
	arg_2[2]  = multiply_and_scale(arg_2[0] + var_18, 0x3E17);
	arg_2[3]  = multiply_and_scale(arg_2[1] + var_1A, 0x3E17);
	/* point 7  = (P2 + P1/4) * 3E17h                                  */
	arg_2[14] = multiply_and_scale(arg_2[16] + var_A, 0x3E17);
	arg_2[15] = multiply_and_scale(arg_2[17] + var_C, 0x3E17);
	/* point 3  = (P1 + 3P2/4) * 3333h                                 */
	arg_2[6]  = multiply_and_scale(arg_2[0] + var_14, 0x3333);
	arg_2[7]  = multiply_and_scale(arg_2[1] + var_16, 0x3333);
	/* point 5  = (P2 + 3P1/4) * 3333h                                 */
	arg_2[10] = multiply_and_scale(arg_2[16] + var_6, 0x3333);
	arg_2[11] = multiply_and_scale(arg_2[17] + var_8, 0x3333);

	/* second quadrant: the same sums with P1 negated                  */
	si = -arg_2[0];                                 /* mov ax,[bx] ; neg ax ; mov si,ax */
	/* point 12 = (P2 - P1) * 2D41h                                    */
	arg_2[24] = multiply_and_scale(arg_2[16] + si, 0x2D41);
	di = -arg_2[1];                                 /* mov ax,[bx+2] ; neg ax ; mov di,ax */
	arg_2[25] = multiply_and_scale(arg_2[17] + di, 0x2D41);
	/* point 14 = (P2/2 - P1) * 393Eh                                  */
	arg_2[28] = multiply_and_scale(var_10 + si, 0x393E);
	arg_2[29] = multiply_and_scale(var_12 + di, 0x393E);
	/* point 10 = (P2 - P1/2) * 393Eh                                  */
	arg_2[20] = multiply_and_scale(arg_2[16] - var_2, 0x393E);
	arg_2[21] = multiply_and_scale(arg_2[17] - var_4, 0x393E);
	si = -arg_2[0];                                 /* reloaded by the original */
	/* point 15 = (P2/4 - P1) * 3E17h                                  */
	arg_2[30] = multiply_and_scale(var_18 + si, 0x3E17);
	di = -arg_2[1];
	arg_2[31] = multiply_and_scale(var_1A + di, 0x3E17);
	/* point 9  = (P2 - P1/4) * 3E17h                                  */
	arg_2[18] = multiply_and_scale(arg_2[16] - var_A, 0x3E17);
	arg_2[19] = multiply_and_scale(arg_2[17] - var_C, 0x3E17);
	/* point 13 = (3P2/4 - P1) * 3333h                                 */
	arg_2[26] = multiply_and_scale(var_14 + si, 0x3333);
	arg_2[27] = multiply_and_scale(var_16 + di, 0x3333);
	/* point 11 = (P2 - 3P1/4) * 3333h                                 */
	arg_2[22] = multiply_and_scale(arg_2[16] - var_6, 0x3333);
	arg_2[23] = multiply_and_scale(arg_2[17] - var_8, 0x3333);

	/* loc_36776: mirror 0..15 into 16..31 and translate by the centre */
	for (var_E = 0; var_E < 0x10; var_E++) {         /* cmp var_E,10h ; jl */
		int16_t* sip = arg_2 + var_E * 2;        /* si = var_E<<2 + arg_2 */
		di = arg_0[0];                           /* mov di, [bx] */
		/* [si+40h]/[si+42h] are BYTE displacements: 0x40 bytes is 32
		 * int16 on, i.e. POINT2D var_E+16. */
		sip[32] = (int16_t)(di - sip[0]);        /* mov [si+40h], ax */
		sip[33] = (int16_t)(arg_0[1] - sip[1]);  /* mov [si+42h], ax */
		sip[0] = (int16_t)(sip[0] + di);         /* add [si], di */
		sip[1] = (int16_t)(sip[1] + arg_0[1]);   /* add [si+2], ax */
	}
}

/* ------------------------------------------------------------------ */
/* preRender_sphere_helper - seg020.asm 53-88                          */
/* ------------------------------------------------------------------ */
void preRender_sphere_helper(int16_t* arg_0, uint16_t arg_2)
{
	int16_t var_80[64];                             /* sub sp, 80h */

	preRender_sphere_helper2(arg_0, var_80);        /* push &var_80 ; push arg_0 */
	preRender_default_alt(arg_2, 0x20, (uint16_t*)var_80);
}

/* ------------------------------------------------------------------ */
/* preRender_sphere - seg012.asm 10164-10367                           */
/*                                                                     */
/* arg_0/arg_2 are the projected centre; arg_4 is the projected size    */
/* that transformed_shape_op stored at polyinfo+0Ah; arg_6 the colour.  */
/*                                                                     */
/* The vertical diameter is arg_4*13/16 (3/4 + 1/16) and the horizontal */
/* radius is 5/4 of the vertical one, i.e. arg_4/2 - the 320x200 pixel  */
/* aspect.  Under a vertical radius of word_3F3C6 (= 40) the shape is   */
/* scan-converted straight out of the off_3F3C8 half-width tables; at   */
/* or above it, a 32-gon is built and handed to preRender_default_alt.  */
/*                                                                     */
/* [ODDITY] seg012.asm 10184-10188: a diameter that rounds to zero      */
/* draws a single pixel; a *negative* one returns having drawn nothing, */
/* because `or dx,dx / jg` is a signed test.                            */
/*                                                                     */
/* [ODDITY] seg012.asm 10184: the horizontal-clip skip at loc_331AD is  */
/* taken *after* the left-edge x has already been stored for the row    */
/* pair.  The stale left value is harmless in practice - the table is   */
/* monotonically increasing, so skipped rows are always the outermost   */
/* ones and end up outside the [0, var_1A) range finally drawn - but    */
/* the original never clears them.                                      */
/*                                                                     */
/* [DEVIATION] the edge buffers are RFB_SPANROWS entries rather than    */
/* the original's fixed 480, so they still fit at RFB_SCALE > 1.  The   */
/* table path can never write past index 2*39-1 = 77 in any case, since */
/* word_3F3C6 caps it.                                                  */
/* ------------------------------------------------------------------ */
void preRender_sphere(uint16_t arg_0, uint16_t arg_2, uint16_t arg_4,
                      uint16_t arg_6)
{
	int16_t var_79A[RFB_SPANROWS];  /* [bp-1946] right x per scanline */
	int16_t var_3DA[RFB_SPANROWS];  /* [bp-986]  left  x per scanline */
	/* var_12..var_8 are six adjacent words passed by address as the
	 * {centre, p1, p2} triple preRender_sphere_helper2 wants. */
	int16_t var_12[6];              /* [bp-18] = var_12,10,E,C,A,8 */
	int16_t var_1A, var_18, var_14, var_4, var_2;
	int16_t var_6;
	const uint8_t* var_16;          /* running pointer into off_3F3C8[bx] */
	int16_t ax, bx, cx, dx;
	int16_t* si;
	int16_t* di;

	dx = (int16_t)arg_4;                    /* mov dx, [bp+arg_4] */
	ax = (int16_t)((uint16_t)dx >> 2);      /* mov ax,dx ; shr ax,1 ; shr ax,1 */
	dx = (int16_t)(dx - ax);                /* sub dx, ax */
	ax = (int16_t)((uint16_t)ax >> 2);      /* shr ax,1 ; shr ax,1 */
	dx = (int16_t)(dx + ax);                /* add dx, ax */
	if (!(dx > 0))                          /* or dx,dx ; jg short loc_33096 */
		return;

/* loc_33096: */
	var_6 = dx;                             /* mov [bp+var_6], dx */
	bx = dx;                                /* mov bx, dx */
	dx = (int16_t)((uint16_t)dx >> 1);      /* shr dx, 1 */
	if (dx != 0)                            /* jnz short loc_330B6 */
		goto loc_330B6;
	putpixel_single_maybe(arg_0, arg_2, arg_6);
/* loc_330B0: */
	return;

loc_330B6:
	bx = (int16_t)(bx - dx);                        /* sub bx, dx */
	var_4 = (int16_t)sprite1.sprite_left2;          /* mov [bp+var_4], ax */
	var_2 = (int16_t)(sprite1.sprite_widthsum - 1); /* dec ax ; mov [bp+var_2], ax */
	cx = (int16_t)arg_2;                            /* mov cx, [bp+arg_2] */
	ax = (int16_t)(cx - dx);                        /* sub ax, dx */
	if (ax >= (int16_t)sprite1.sprite_height)       /* jge short loc_330B0 */
		return;
	var_14 = ax;                                    /* mov [bp+var_14], ax */
	cx = (int16_t)(cx + bx);                        /* add cx, bx */
	if (cx <= (int16_t)sprite1.sprite_top)          /* jle short loc_330B0 */
		return;
	dx = bx;                                        /* mov dx, bx */
	ax = (int16_t)((uint16_t)dx >> 2);              /* shr ax,1 ; shr ax,1 */
	dx = (int16_t)(dx + ax);                        /* add dx, ax  (= bx * 5/4) */
	cx = (int16_t)arg_0;                            /* mov cx, [bp+arg_0] */
	ax = (int16_t)(cx - dx);
	if (ax > var_2)                                 /* jg short loc_330B0 */
		return;
	cx = (int16_t)(cx + dx);
	if (cx < var_4)                                 /* jl short loc_330B0 */
		return;
	if (bx < (int16_t)word_3F3C6)                   /* cmp bx,ds:3C56h ; jl */
		goto loc_33148;

	/* --- large sphere: 32-gon through preRender_default_alt --- */
	ax = (int16_t)arg_0;
	var_12[0] = ax;                                 /* var_12 = centre.x */
	var_12[2] = ax;                                 /* var_E  = p1.x     */
	dx = (int16_t)((uint16_t)arg_4 >> 1);           /* mov dx,[bp+arg_4] ; shr dx,1 */
	var_12[4] = (int16_t)(ax + dx);                 /* var_A  = p2.x     */
	ax = (int16_t)arg_2;
	var_12[1] = ax;                                 /* var_10 = centre.y */
	var_12[5] = ax;                                 /* var_8  = p2.y     */
	dx = (int16_t)((uint16_t)var_6 >> 1);           /* mov dx,[bp+var_6] ; shr dx,1 */
	var_12[3] = (int16_t)(ax + dx);                 /* var_C  = p1.y     */
	/* `add sp,780h` around the call frees the two edge buffers for the
	 * duration of the call - stack economy with no effect in C. */
	preRender_sphere_helper(var_12, arg_6);
	return;

loc_33148:
	var_16 = off_3F3C8[bx];                 /* shl bx,1 ; mov ax,off_3F3C8[bx] */
	var_1A = var_6;                         /* mov [bp+var_1A], ax  (row count) */
	var_18 = (int16_t)((var_6 - 1) * 2);    /* dec ax ; shl ax,1 */
	si = var_3DA;                           /* lea si, [bp+var_3DA] */
	di = var_79A;                           /* lea di, [bp+var_79A] */

loc_33165:
	dx = (int16_t)*var_16;                  /* mov dl,[bx] ; xor dh,dh */
	var_16++;                               /* inc [bp+var_16] */
	bx = var_18;                            /* mov bx, [bp+var_18] */
	ax = (int16_t)(arg_0 - (uint16_t)dx);   /* mov ax,[bp+arg_0] ; sub ax,dx */
	if (ax > var_2)                         /* jg short loc_331AD */
		goto loc_331AD;
	if (ax >= var_4)                        /* jge short loc_33184 */
		goto loc_33184;
	ax = var_4;
loc_33184:
	si[0] = ax;                             /* mov [si], ax */
	*(int16_t*)((uint8_t*)si + bx) = ax;    /* mov [bx+si], ax */
	ax = (int16_t)(arg_0 + (uint16_t)dx);   /* mov ax,[bp+arg_0] ; add ax,dx */
	if (ax < var_4)                         /* jl short loc_331AD */
		goto loc_331AD;
	if (ax <= var_2)                        /* jle short loc_3319A */
		goto loc_3319A;
	ax = var_2;
loc_3319A:
	di[0] = ax;                             /* mov [di], ax */
	*(int16_t*)((uint8_t*)di + bx) = ax;    /* mov [bx+di], ax */
	si++;                                   /* add si, 2 */
	di++;                                   /* add di, 2 */
	var_18 = (int16_t)(var_18 - 4);         /* sub [bp+var_18], 4 */
	if (var_18 >= 0)                        /* jge short loc_33165 */
		goto loc_33165;
	goto loc_331C0;

loc_331AD:
	var_14++;                               /* inc [bp+var_14] */
	var_1A = (int16_t)(var_1A - 2);         /* sub [bp+var_1A], 2 */
	var_18 = (int16_t)(var_18 - 4);         /* sub [bp+var_18], 4 */
	if (var_18 >= 0)                        /* jge short loc_33165 */
		goto loc_33165;
	return;

loc_331C0:
	dx = 0;                                          /* xor dx, dx */
	ax = (int16_t)(sprite1.sprite_top - var_14);
	if (ax <= 0)                                     /* jle short loc_331D9 */
		goto loc_331D9;
	var_1A = (int16_t)(var_1A - ax);
	ax = (int16_t)(ax * 2);                          /* shl ax, 1 */
	dx = ax;
	var_14 = (int16_t)sprite1.sprite_top;
loc_331D9:
	ax = (int16_t)(var_14 + var_1A - (int16_t)sprite1.sprite_height);
	if (ax <= 0)                                     /* jle short loc_331E9 */
		goto loc_331E9;
	var_1A = (int16_t)(var_1A - ax);
loc_331E9:
	draw_filled_lines((int16_t*)((uint8_t*)var_3DA + dx),
	                  (int16_t*)((uint8_t*)var_79A + dx),
	                  (uint16_t)var_14, (uint16_t)var_1A, arg_6);
}

/* ------------------------------------------------------------------ */
/* preRender_wheel_helper3 - seg014.asm 53-312                         */
/*                                                                     */
/* arg_0: three POINT2D (ellipse center p0, axis endpoints p1, p2).    */
/* arg_2: out buffer; on return words 0..31 hold 16 POINT2D forming a  */
/* 16-gon approximation of the ellipse around p0.                      */
/* Scale constants: 0x2D41/0x4000 = sin(45 deg), 0x393E/0x4000 =       */
/* 2/sqrt(5) (0x393E assembles from `offset unk_3F0AE`, see header).   */
/* ------------------------------------------------------------------ */
void preRender_wheel_helper3(int16_t* arg_0, int16_t* arg_2)
{
	int16_t var_4, var_6, var_8, var_A;
	int16_t var_2;
	int16_t si, di, ax;
	int16_t* sip;

	si = arg_0[0];                                  /* mov si, [bx] */
	ax = arg_0[2] - si;                             /* mov ax, [bx+4] ; sub ax, si */
	arg_2[0] = ax;                                  /* mov [bx], ax */
	ax = arg_0[3] - arg_0[1];                       /* mov ax, [bx+6] ; sub ax, [bx+2] */
	arg_2[1] = ax;                                  /* mov [bx+2], ax */
	ax = arg_0[4] - si;                             /* mov ax, [bx+8] ; sub ax, si */
	arg_2[8] = ax;                                  /* mov [bx+10h], ax */
	ax = arg_0[5] - arg_0[1];                       /* mov ax, [bx+0Ah] ; sub ax, [bx+2] */
	arg_2[9] = ax;                                  /* mov [bx+12h], ax */

	/* mas(out[8]+out[0], 2D41h) -> [bx+8] */
	arg_2[4] = multiply_and_scale(arg_2[8] + arg_2[0], 0x2D41);
	/* mas(out[1]+out[9], 2D41h) -> [bx+0Ah] */
	arg_2[5] = multiply_and_scale(arg_2[1] + arg_2[9], 0x2D41);

	si = arg_2[8] >> 1;                             /* mov si, [bx+10h] ; sar si, 1 */
	/* mov ax, offset unk_3F0AE (== 393Eh) ; mas(out[0]+si, 393Eh) -> [bx+4] */
	arg_2[2] = multiply_and_scale(arg_2[0] + si, 0x393E);
	di = arg_2[9] >> 1;                             /* mov di, [bx+12h] ; sar di, 1 */
	arg_2[3] = multiply_and_scale(arg_2[1] + di, 0x393E);   /* -> [bx+6] */

	var_4 = arg_2[0] >> 1;                          /* mov ax, [bx] ; sar ax, 1 */
	arg_2[6] = multiply_and_scale(arg_2[8] + var_4, 0x393E); /* -> [bx+0Ch] */
	var_6 = arg_2[1] >> 1;                          /* mov ax, [bx+2] ; sar ax, 1 */
	arg_2[7] = multiply_and_scale(arg_2[9] + var_6, 0x393E); /* -> [bx+0Eh] */

	var_8 = -arg_2[0];                              /* mov ax, [bx] ; neg ax */
	arg_2[12] = multiply_and_scale(arg_2[8] + var_8, 0x2D41); /* -> [bx+18h] */
	var_A = -arg_2[1];                              /* mov ax, [bx+2] ; neg ax */
	arg_2[13] = multiply_and_scale(arg_2[9] + var_A, 0x2D41); /* -> [bx+1Ah] */

	arg_2[14] = multiply_and_scale(var_8 + si, 0x393E);       /* -> [bx+1Ch] */
	arg_2[15] = multiply_and_scale(var_A + di, 0x393E);       /* -> [bx+1Eh] */
	arg_2[10] = multiply_and_scale(arg_2[8] - var_4, 0x393E); /* -> [bx+14h] */
	arg_2[11] = multiply_and_scale(arg_2[9] - var_6, 0x393E); /* -> [bx+16h] */

	var_2 = 0;                                      /* mov [bp+var_2], 0 */
loc_363DC:
	/* mov si, [bp+var_2] ; shl si, 2 ; add si, [bp+arg_2] */
	sip = arg_2 + var_2 * 2;
	di = arg_0[0];                                  /* mov di, [bx] */
	sip[16] = di - sip[0];                          /* mov [si+20h], ax */
	sip[17] = arg_0[1] - sip[1];                    /* mov [si+22h], ax */
	sip[0] += di;                                   /* add [si], di */
	sip[1] += arg_0[1];                             /* add [si+2], ax */
	var_2++;                                        /* inc [bp+var_2] */
	if (var_2 < 8)                                  /* cmp [bp+var_2], 8 ; jl */
		goto loc_363DC;
}

/* ------------------------------------------------------------------ */
/* preRender_wheel_helper2 - seg019.asm 50-173                         */
/*                                                                     */
/* Builds the outer ellipse ring (arg_2 words 0..31) from the quad's   */
/* first three points, then a second ring (arg_2 words 32..63) whose   */
/* axis points are pulled toward p0 by arg_4/0x4000 (hub ellipse).     */
/* ------------------------------------------------------------------ */
void preRender_wheel_helper2(int16_t* arg_0, int16_t* arg_2, uint16_t arg_4)
{
	/* var_C..var_2 form one contiguous 6-word block (a 3-point quad):
	 * var_C[0]=var_C  var_C[1]=var_A  var_C[2]=var_8
	 * var_C[3]=var_6  var_C[4]=var_4  var_C[5]=var_2                  */
	int16_t var_C[6];
	int16_t si, ax;

	var_C[0] = arg_0[0];                            /* mov ax, [bx] */
	var_C[1] = arg_0[1];                            /* mov dx, [bx+2] */
	si = arg_0[0];                                  /* mov si, ax */

	/* push arg_4 ; mov ax,[bx+4] ; sub ax,si ; call multiply_and_scale */
	ax = multiply_and_scale(arg_0[2] - si, (int16_t)arg_4);
	var_C[2] = ax + si;                             /* add ax, si ; mov [bp+var_8], ax */

	ax = multiply_and_scale(arg_0[3] - arg_0[1], (int16_t)arg_4);
	var_C[3] = arg_0[1] + ax;                       /* mov cx,[bx+2] ; add cx,ax */

	ax = multiply_and_scale(arg_0[4] - si, (int16_t)arg_4);
	var_C[4] = ax + si;                             /* add ax, si ; mov [bp+var_4], ax */

	ax = multiply_and_scale(arg_0[5] - arg_0[1], (int16_t)arg_4);
	var_C[5] = arg_0[1] + ax;                       /* mov cx,[bx+2] ; add cx,ax */

	/* push [bp+arg_2] ; push bx */
	preRender_wheel_helper3(arg_0, arg_2);
	/* mov ax,[bp+arg_2] ; add ax,40h (bytes) ; lea ax,[bp+var_C] */
	preRender_wheel_helper3(var_C, arg_2 + 0x20);
}

/* ------------------------------------------------------------------ */
/* preRender_wheel_helper - seg023.asm 50-136                          */
/*                                                                     */
/* Rings A (arg_2+0) and B (arg_2+0x40 bytes) via helper2, then ring C */
/* (arg_2+0x80 bytes) = ring A translated by the axle vector p3 - p0.  */
/* ------------------------------------------------------------------ */
void preRender_wheel_helper(int16_t* arg_0, int16_t* arg_2, uint16_t arg_4)
{
	int16_t var_8, var_A;
	int16_t var_6;
	int16_t* var_2;
	int16_t* var_4;

	/* push arg_4 ; push arg_2 ; push arg_0 */
	preRender_wheel_helper2(arg_0, arg_2, arg_4);

	var_8 = arg_0[6] - arg_0[0];                    /* mov ax,[bx+0Ch] ; sub ax,[bx] */
	var_A = arg_0[7] - arg_0[1];                    /* mov ax,[bx+0Eh] ; sub ax,[bx+2] */
	var_2 = arg_2;                                  /* mov [bp+var_2], ax */
	var_4 = arg_2 + 0x40;                           /* add ax, 80h (bytes) */
	var_6 = 0;                                      /* mov [bp+var_6], 0 */
loc_36EFD:
	var_4[0] = var_2[0] + var_8;                    /* mov ax,[si] ; add ax,[bp+var_8] */
	var_4[1] = var_2[1] + var_A;                    /* mov ax,[si+2] ; add ax,[bp+var_A] */
	var_2 += 2;                                     /* add [bp+var_2], 4 */
	var_4 += 2;                                     /* add [bp+var_4], 4 */
	var_6++;                                        /* inc [bp+var_6] */
	if (var_6 < 0x10)                               /* cmp [bp+var_6], 10h ; jl */
		goto loc_36EFD;
}

/* ------------------------------------------------------------------ */
/* preRender_wheel - seg022.asm 50-300                                 */
/*                                                                     */
/* arg_0: 4 wheel quad points from the polyinfo record.                */
/* arg_2: hub-ring scale factor (0x2500 from get_a_poly_info).         */
/* arg_4/arg_6/arg_8: material colors [mattype], [+1], [+2] (tread /   */
/* sidewall / hub).                                                    */
/*                                                                     */
/* Frame layout (sub sp, 11Ah): the three 16-point rings are one       */
/* contiguous 0xC0-byte buffer:                                        */
/*   var_10E (bp-10Eh) ring A - outer ellipse, near face               */
/*   var_CE  (bp-0CEh) ring B - hub ellipse       = var_10E + 0x40     */
/*   var_8E  (bp-8Eh)  ring C - ring A + axle     = var_10E + 0x80     */
/* var_4E (bp-4Eh): 18-point sidewall list, 72 bytes; var_A (bp-0Ah)   */
/* aliases its last POINT2D; var_10C/var_10A/var_D2/var_92/var_8C are  */
/* in-buffer aliases as noted inline.                                  */
/*                                                                     */
/* The helper4 calls push the four quad points BY VALUE (8 words); the */
/* Borland-era preRender_wheel_helper4 recovers them with the          */
/* &arg_vertlines parameter-slot trick.  Natively the quad is built in */
/* a POINT2D[4] and passed by reference instead.                       */
/* ------------------------------------------------------------------ */
void preRender_wheel(int16_t* arg_0, uint16_t arg_2, uint16_t arg_4,
                     uint16_t arg_6, uint16_t arg_8)
{
	int16_t var_10E[96];    /* rings A/B/C, 0xC0 bytes */
	int16_t var_4E[36];     /* 18-point sidewall polygon */
	int16_t* var_2;
	int16_t* var_4;
	int16_t* var_6;
	int16_t* var_114;
	int16_t* var_116;
	int16_t var_110, var_112, var_118, var_11A;
	int16_t* bx;
	int16_t* si;
	struct POINT2D quad[4]; /* the 8 words pushed by value in the asm */

	/* push [bp+arg_2] ; lea ax,[bp+var_10E] ; push ax ; push [bp+arg_0] */
	preRender_wheel_helper(arg_0, var_10E, arg_2);

	var_2 = &var_10E[0];                            /* lea ax,[bp+var_10E] */
	var_110 = 0;                                    /* mov [bp+var_110], 0 */
loc_36CA6:
	bx = var_2;                                     /* mov bx, [bp+var_2] */
	/* push [bx+82h] [bx+80h] [bx+86h] [bx+84h] [bx+6] [bx+4] [bx+2] [bx] 4 arg_4
	 * -> helper4(color, 4, { A[i], A[i+1], C[i+1], C[i] })              */
	quad[0].px = bx[0];    quad[0].py = bx[1];      /* A[i]   */
	quad[1].px = bx[2];    quad[1].py = bx[3];      /* A[i+1] */
	quad[2].px = bx[0x42]; quad[2].py = bx[0x43];   /* C[i+1] ([bx+84h]/[bx+86h]) */
	quad[3].px = bx[0x40]; quad[3].py = bx[0x41];   /* C[i]   ([bx+80h]/[bx+82h]) */
	preRender_wheel_helper4(arg_4, 4, quad);
	var_2 += 2;                                     /* add [bp+var_2], 4 */
	var_110++;                                      /* inc [bp+var_110] */
	if (var_110 < 0x0F)                             /* cmp [bp+var_110], 0Fh ; jl */
		goto loc_36CA6;

	/* loc_36CE2: closing quad { A[15], A[0], C[0], C[15] } */
	bx = var_2;                                     /* mov bx, [bp+var_2] (= &A[15]) */
	quad[0].px = bx[0];       quad[0].py = bx[1];         /* A[15] */
	quad[1].px = var_10E[0];  quad[1].py = var_10E[1];    /* var_10E / var_10C = A[0] */
	quad[2].px = var_10E[64]; quad[2].py = var_10E[65];   /* var_8E / var_8C = C[0] */
	quad[3].px = bx[0x40];    quad[3].py = bx[0x41];      /* [bx+80h]/[bx+82h] = C[15] */
	preRender_wheel_helper4(arg_4, 4, quad);

	/* find ring A vertex with the smallest y */
	var_4 = &var_10E[2];                            /* lea ax,[bp+var_10A] = &A[1] */
	var_118 = var_10E[1];                           /* mov ax,[bp+var_10C] = A[0].py */
	/* loc_36D20: */
	var_11A = 0;                                    /* mov [bp+var_11A], 0 */
	var_110 = 1;                                    /* mov [bp+var_110], 1 */
loc_36D2C:
	bx = var_4;                                     /* mov bx, [bp+var_4] */
	if (bx[1] < var_118) {                          /* cmp [bx+2], ax ; jge loc_36D47 */
		var_118 = bx[1];                            /* mov [bp+var_118], ax */
		var_11A = var_110;                          /* mov [bp+var_11A], ax */
	}
	/* loc_36D47: */
	var_4 += 2;                                     /* add [bp+var_4], 4 */
	var_110++;                                      /* inc [bp+var_110] */
	if (var_110 < 0x10)                             /* cmp [bp+var_110], 10h ; jl */
		goto loc_36D2C;

	/* first sidewall: 9 ring-A points forward from the min-y index      */
	/* (wrapping to the ring start) + 9 ring-B points stored backwards   */
	/* mov si,[bp+var_11A] ; shl si,2 ; add si,bp */
	si = (int16_t*)((uint8_t*)var_10E + var_11A * 4);
	var_4 = si;                                     /* lea ax,[si-10Eh] = &A[idx] */
	var_114 = si + 0x20;                            /* lea ax,[si-0CEh] = &B[idx] */
	var_6 = &var_4E[0];                             /* lea ax,[bp+var_4E] */
	var_116 = &var_4E[34];                          /* lea ax,[bp+var_A] (last dword) */
	/* loc_36D78: */
	var_112 = var_11A;                              /* mov [bp+var_112], ax */
	var_110 = 0;                                    /* mov [bp+var_110], 0 */
	goto loc_36DA2;
loc_36D8C:
	var_4 += 2;                                     /* add [bp+var_4], 4 */
	var_114 += 2;                                   /* add [bp+var_114], 4 */
loc_36D95:
	var_6 += 2;                                     /* add [bp+var_6], 4 */
	var_116 -= 2;                                   /* sub [bp+var_116], 4 */
	var_110++;                                      /* inc [bp+var_110] */
loc_36DA2:
	if (var_110 > 8)                                /* cmp [bp+var_110], 8 ; jg */
		goto loc_36DEE;
	var_6[0] = var_4[0];                            /* mov ax,[si] ; mov [bx],ax */
	var_6[1] = var_4[1];                            /* mov ax,[si+2] ; mov [bx+2],ax */
	var_116[0] = var_114[0];
	var_116[1] = var_114[1];
	var_112++;                                      /* inc [bp+var_112] */
	if (var_112 < 0x10)                             /* cmp [bp+var_112], 10h ; jl */
		goto loc_36D8C;
	var_4 = &var_10E[0];                            /* lea ax,[bp+var_10E] */
	var_114 = &var_10E[32];                         /* lea ax,[bp+var_CE] */
	/* loc_36DE5: */
	var_112 = 0;                                    /* mov [bp+var_112], 0 */
	goto loc_36D95;
loc_36DEE:
	/* lea ax,[bp+var_4E] ; push ; push 12h ; push arg_6 */
	preRender_default_alt(arg_6, 0x12, (uint16_t*)var_4E);

	/* second sidewall: 9 ring-A/B points backward from the min-y index  */
	/* (wrapping to the ring end)                                        */
	si = (int16_t*)((uint8_t*)var_10E + var_11A * 4);
	var_4 = si;                                     /* lea ax,[si-10Eh] */
	var_114 = si + 0x20;                            /* lea ax,[si-0CEh] */
	var_6 = &var_4E[0];                             /* lea ax,[bp+var_4E] */
	var_116 = &var_4E[34];                          /* lea ax,[bp+var_A] */
	var_112 = var_11A;                              /* mov [bp+var_112], ax */
	var_110 = 0;                                    /* mov [bp+var_110], 0 */
	goto loc_36E4E;
loc_36E38:
	var_4 -= 2;                                     /* sub [bp+var_4], 4 */
	var_114 -= 2;                                   /* sub [bp+var_114], 4 */
loc_36E41:
	var_6 += 2;                                     /* add [bp+var_6], 4 */
	var_116 -= 2;                                   /* sub [bp+var_116], 4 */
	var_110++;                                      /* inc [bp+var_110] */
loc_36E4E:
	if (var_110 >= 9)                               /* cmp [bp+var_110], 9 ; jge */
		goto loc_36E94;
	var_6[0] = var_4[0];
	var_6[1] = var_4[1];
	var_116[0] = var_114[0];
	var_116[1] = var_114[1];
	var_112--;                                      /* dec [bp+var_112] */
	if (var_112 >= 0)                               /* jns loc_36E38 */
		goto loc_36E38;
	var_4 = &var_10E[30];                           /* lea ax,[bp+var_D2] = &A[15] */
	var_114 = &var_10E[62];                         /* lea ax,[bp+var_92] = &B[15] */
	var_112 = 0x10;                                 /* mov [bp+var_112], 10h */
	goto loc_36E41;
loc_36E94:
	/* lea ax,[bp+var_4E] ; push ; push 12h ; push arg_6 */
	preRender_default_alt(arg_6, 0x12, (uint16_t*)var_4E);
	/* lea ax,[bp+var_CE] ; push ; push 10h ; push arg_8 : hub ring */
	preRender_default_alt(arg_8, 0x10, (uint16_t*)&var_10E[32]);
}

/* ------------------------------------------------------------------ */
/* get_a_poly_info - seg006.asm 2062-2292                              */
/* ------------------------------------------------------------------ */
void get_a_poly_info(void)
{
	/* frame: sub sp, 40h */
	uint16_t var_pattype2;          /* [bp-64] */
	uint8_t* var_polyinfoptrdata;   /* [bp-62] dword: cursor into record */
	uint8_t* var_polyinfoptr;       /* [bp-56] dword: current record */
	int16_t var_32[20];             /* [bp-50] 40-byte vertex scratch */
	int16_t* var_32ptr;             /* [bp-10] */
	int16_t var_matcolor;           /* [bp-8] */
	int16_t var_mattype;            /* [bp-6] */
	uint16_t var_counter;           /* [bp-4] */
	int16_t var_maxcount;           /* [bp-2] */
	uint16_t di;                    /* register di: linked-list cursor */
	uint16_t si;                    /* register si: polys dispatched */
	int16_t ax;
	uint8_t* bx;                    /* es:bx far pointer pairs */

	di = 0x190;                     /* mov di, 190h (list head sentinel) */
	si = 0;                         /* sub si, si */
	goto loc_260AC;                 /* jmp loc_260AC */

_fill_type0:
	bx = var_polyinfoptr;                       /* les bx, [bp+var_polyinfoptr] */
	var_maxcount = (int16_t)(int8_t)bx[3];      /* mov al, es:[bx+3] ; cbw */
	var_polyinfoptrdata = bx + 6;               /* polyinfoptrdata = polyinfoptr+6 */
	var_32ptr = var_32;                         /* lea ax, [bp+var_32] */
	var_counter = 0;                            /* mov [bp+var_counter], 0 */
	goto loc_26049;

loc_2602C:
	{
		/* copy one POINT2D (dword) from the record into var_32 */
		int16_t lo = *(int16_t*)(var_polyinfoptrdata);      /* mov ax, es:[bx] */
		int16_t hi = *(int16_t*)(var_polyinfoptrdata + 2);  /* mov dx, es:[bx+2] */
		var_32ptr[0] = lo;                      /* mov [bx], ax */
		var_32ptr[1] = hi;                      /* mov [bx+2], dx */
		var_32ptr += 2;                         /* add [bp+var_32ptr], 4 */
		var_polyinfoptrdata += 4;               /* add word ptr [bp+var_polyinfoptrdata], 4 */
		var_counter++;                          /* inc [bp+var_counter] */
	}
loc_26049:
	if (var_counter < (uint16_t)var_maxcount)   /* cmp [bp+var_counter], ax ; jb */
		goto loc_2602C;

	/* mov bx,[bp+var_mattype] ; shl bx,1 ; add bx,material_patlist_ptr_cpy */
	ax = material_patlist_ptr_cpy[var_mattype];
	if (ax == 0)                    /* jz _fill_default ; 0 normal 1 grille 2? 3 invisible */
		goto _fill_default;
	if (ax == 1)                    /* cmp ax, 1 ; jz */
		goto _fill_patterned;
	if (ax != 2)                    /* cmp ax, 2 ; jnz _do_fill_next */
		goto _do_fill_next;
	goto _fill_unk;                 /* jmp _fill_unk */
_do_fill_next:
	goto _fill_next;                /* jmp short _fill_next */

_fill_default:
	/* lea ax,[bp+var_32] ; push ax ; push maxcount ; push matcolor */
	preRender_default((uint16_t)var_matcolor, (uint16_t)var_maxcount,
	                  (uint16_t*)var_32);
_fill_next_eop6:                    /* add sp, 6 */
	goto _fill_next;

_fill_patterned:
	/* mov bx,[bp+var_mattype] ; shl bx,1 ; add bx,material_patlist2_ptr_cpy */
	var_pattype2 = (uint16_t)material_patlist2_ptr_cpy[var_mattype];
	if (var_pattype2 == 0)          /* or ax, ax ; jz _fill_next */
		goto _fill_next;
	/* push &var_32 ; push maxcount ; push matcolor ; push pattype2 */
	preRender_patterned(var_pattype2, (uint16_t)var_matcolor,
	                    (uint16_t)var_maxcount, (struct POINT2D*)var_32);
_fill_next_eop8:                    /* add sp, 8 (falls through) */
_fill_next:
	si++;                           /* inc si */
loc_260AC:
	if (si < polyinfonumpolys)      /* mov ax,si ; cmp ax,polyinfonumpolys ; jb */
		goto loc_260B7;
	goto _get_a_poly_info_done;     /* loc_260B4: jmp _get_a_poly_info_done */

loc_260B7:
	/* mov bx, di ; shl bx, 1 ; mov di, poly_linked_list_40ED6[bx] */
	di = (uint16_t)poly_linked_list_40ED6[di];
	/* mov bx, di ; shl bx, 1 ; shl bx, 1 ; mov ax/dx, polyinfoptrs[bx] */
	var_polyinfoptr = (uint8_t*)polyinfoptrs[di];
	bx = var_polyinfoptr;                       /* les bx, [bp+var_polyinfoptr] */
	var_mattype = (int16_t)bx[2];               /* mov al, es:[bx+2] ; sub ah, ah */
	/* mov bx, ax ; shl bx, 1 ; add bx, material_clrlist_ptr_cpy */
	var_matcolor = material_clrlist_ptr_cpy[var_mattype];
	ax = (int16_t)(int8_t)bx[4];                /* mov al, es:[bx+4] ; cbw */
	                                            /* (solid, sphere, wheel, pixel) */
	if (ax == 0)                    /* or ax, ax ; jnz _fill_nonzero */
		goto _fill_type0;           /* jmp _fill_type0 */
	/* _fill_nonzero: */
	if (ax == 1)                    /* cmp ax, 1 ; jz */
		goto _fill_solid;
	if (ax == 2)                    /* cmp ax, 2 ; jnz loc_26108 ; jmp _fill_sphere */
		goto _fill_sphere;
	/* loc_26108: */
	if (ax == 3)                    /* cmp ax, 3 ; jz */
		goto _fill_wheel0;
	if (ax == 5)                    /* cmp ax, 5 ; jnz _fill_next_jmp ; jmp _fill_pixel */
		goto _fill_pixel;
	goto _fill_next;                /* _fill_next_jmp: jmp short _fill_next */

_fill_unk:
	/* mov ax,[bp+var_mattype] ; shl ax,1 : byte offset into the material tables */
	var_pattype2 = (uint16_t)(var_mattype << 1);
	/* Push sequence: &var_32, maxcount, matcolor, clrlist2[mat], patlist2[mat]
	 * -> cdecl callee receives (patlist2[mat], clrlist2[mat], matcolor,
	 *    maxcount, &var_32) in declaration order.  The vendored
	 * preRender_unk prototype's parameter NAMES do not line up with these
	 * values (it was reconstructed blind and fatal_error()s on entry);
	 * the positional values of the original stack image are preserved
	 * here exactly, including the 16-bit-only pointer/int reuse of the
	 * last two slots.                                                    */
	preRender_unk(
		(uint16_t)*(int16_t*)((uint8_t*)material_patlist2_ptr_cpy + var_pattype2),
		(uint16_t)*(int16_t*)((uint8_t*)material_clrlist2_ptr_cpy + var_pattype2),
		(uint16_t)var_matcolor,
		(struct POINT2D*)(uintptr_t)(uint16_t)var_maxcount,
		(uint16_t)(uintptr_t)var_32);   /* originally the near offset of var_32 */
	goto _fill_next_eop10;          /* jmp short _fill_next_eop10 */

_fill_solid:
	bx = var_polyinfoptr;           /* les bx, [bp+var_polyinfoptr] */
	/* push matcolor ; push es:[bx+0Ch] [bx+0Ah] [bx+8] [bx+6] */
	preRender_line((uint16_t)*(int16_t*)(bx + 6),
	               (uint16_t)*(int16_t*)(bx + 8),
	               (uint16_t)*(int16_t*)(bx + 0x0A),
	               (uint16_t)*(int16_t*)(bx + 0x0C),
	               (uint16_t)var_matcolor);
_fill_next_eop10:                   /* add sp, 0Ah */
	goto _fill_next;                /* jmp _fill_next */

_fill_wheel0:
	var_counter = 0;                /* mov [bp+var_counter], 0 */
_fill_wheel:
	/* mov ax,[bp+var_counter] ; shl ax,1 ; shl ax,1 */
	var_pattype2 = (uint16_t)(var_counter << 2);
	/* mov bx, ax ; add bx, word ptr [bp+var_polyinfoptr] */
	bx = var_polyinfoptr + var_pattype2;
	{
		int16_t lo = *(int16_t*)(bx + 6);       /* mov ax, es:[bx+6] */
		int16_t hi = *(int16_t*)(bx + 8);       /* mov dx, es:[bx+8] */
		/* mov bx,[bp+var_pattype2] ; add bx,bp ; mov [bx-32h]/[bx-30h] */
		*(int16_t*)((uint8_t*)var_32 + var_pattype2) = lo;
		*(int16_t*)((uint8_t*)var_32 + var_pattype2 + 2) = hi;
	}
	var_counter++;                  /* inc [bp+var_counter] */
	if (var_counter < 4)            /* cmp [bp+var_counter], 4 ; jb */
		goto _fill_wheel;           /* b4 every car0 render */
	{
		/* mov ax,[bp+var_mattype] ; shl ax,1 ; add ax,material_clrlist_ptr_cpy
		 * (var_pattype2 held this near address in the original)          */
		int16_t* clrp = (int16_t*)((uint8_t*)material_clrlist_ptr_cpy
		                           + ((uint16_t)var_mattype << 1));
		/* push [bx+4] ; push [bx+2] ; push matcolor ;
		 * mov ax,(offset trkObjectList.ss_ssOvelay+460h) ; push ax
		 *   -> dseg offset 0x2098+8+0x460 = 0x2500, a numeric scale
		 *      constant (see file header), never dereferenced ;
		 * lea ax,[bp+var_32] ; push ax                                  */
		preRender_wheel(var_32, 0x2500, (uint16_t)var_matcolor,
		                (uint16_t)clrp[1], (uint16_t)clrp[2]);
	}
	goto _fill_next_eop10;          /* jmp short _fill_next_eop10 */

_fill_sphere:
	bx = var_polyinfoptr;           /* les bx, [bp+var_polyinfoptr] */
	/* push matcolor ; push es:[bx+0Ah] [bx+8] [bx+6] */
	preRender_sphere((uint16_t)*(int16_t*)(bx + 6),
	                 (uint16_t)*(int16_t*)(bx + 8),
	                 (uint16_t)*(int16_t*)(bx + 0x0A),
	                 (uint16_t)var_matcolor);
	goto _fill_next_eop8;           /* jmp _fill_next_eop8 */

_fill_pixel:
	bx = var_polyinfoptr;           /* les bx, [bp+var_polyinfoptr] */
	/* push matcolor ; push es:[bx+8] [bx+6] */
	putpixel_single_maybe((uint16_t)*(int16_t*)(bx + 6),
	                      (uint16_t)*(int16_t*)(bx + 8),
	                      (uint16_t)var_matcolor);
	goto _fill_next_eop6;           /* jmp _fill_next_eop6 */

_get_a_poly_info_done:
	/* push cs ; call near ptr ported_polyinfo_reset_
	 * (identical to polyinfo_reset(): word_411F6 IS
	 * poly_linked_list_40ED6[0x190], 0x40ED6+0x320 = 0x411F6)            */
	polyinfo_reset();
}
