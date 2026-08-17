/*
 * sfasm_port.c - Instruction-exact C translations of two 16-bit x86
 * assembly routines from the Stunts simulation core.
 *
 *   1. update_grip                     seg001.asm 6174..6687 (update_grip_asm_)
 *      Per-frame tyre-grip core of the vehicle physics.
 *
 *   2. upd_statef20_from_steer_input   seg001.asm 7054..7185
 *      Per-frame steering-input processing.
 *
 * SOURCE FILE NOTE: the task brief named
 *   reference/restunts/src/restunts/asm/seg001.asm
 * but neither `update_grip` nor `upd_statef20_from_steer_input` has a body
 * there (that tree only carries `extrn update_grip:proc` in seg001.inc).
 * The bodies live in the restunts2 export:
 *   reference/restunts2/src/asm/seg001.asm
 * where they are named `update_grip_asm_` / `upd_statef20_from_steer_input_asm_`.
 * All line numbers below refer to that file.
 *
 * Translation rules used throughout (matching src/render_faithful/rasm_port.c):
 *   - 16-bit registers are modelled as int16_t/uint16_t locals carrying the
 *     original register names (ax, bx, cx, dx, si, di).
 *   - Original asm label names survive as C labels; the goto structure of the
 *     original is preserved verbatim.
 *   - jl/jg/jle/jge are SIGNED int16 compares; jb/ja/jbe/jae/jc are UNSIGNED.
 *   - `mul`/`div` are unsigned, `imul`/`idiv` signed with x86 truncation
 *     (quotient truncates toward zero, matching C99 `/`).
 *   - 32-bit values living in the dx:ax register pair become int32_t/uint32_t
 *     with explicit low-word extraction where the original only keeps `ax`.
 *   - The original asm line is quoted as a side comment next to any
 *     non-obvious translation.
 *   - Oddities are reproduced, not corrected.  They are flagged [ODDITY].
 *
 * MSC 16-bit long-arithmetic helpers used by the original:
 *   __aFlmul(a, b)  -> dx:ax = a * b            (order irrelevant)
 *   __aFldiv(a, b)  -> dx:ax = dividend/divisor.  Argument order was pinned
 *                      from the already-translated call sites in
 *                      src/sim_faithful/stateply.c (loc_155A1): the operand
 *                      pushed LAST (closest to sp at the call) is the
 *                      DIVIDEND, the operand pushed FIRST is the DIVISOR.
 */

#include "sfport.h"
#include <stdio.h>
#include <stdlib.h>

extern int16_t sfstub_hits[8];
#include "externs.h"

/* ------------------------------------------------------------------ */
/* Externs.                                                            */
/*                                                                     */
/* Type authority, in the order mandated by the task:                  */
/*   state, terrainrows, td14_elem_map_main, steerWhlRespTable_ptr,    */
/*   framespersec        -> src/render_faithful/externs.h              */
/*   grassDecelDivTab    -> no existing declaration anywhere; taken    */
/*                          from dseg.asm storage size (`dw`).         */
/* Nothing is *defined* here.                                          */
/* ------------------------------------------------------------------ */

/*
 * dseg.asm:
 *     grassDecelDivTab     dw 0x00FF, 0x0100, 0x00C0, 0x0080
 *                          dw 0x0040
 * Address check: dseg's first label is word_3B770, and the label right after
 * the table is byte_3BE02, so the table starts at 0x3BE02 - 10 = 0x3BDF8,
 * i.e. dseg offset 0x688 - exactly the literal displacement the original
 * `div word ptr [bx+0x688]` uses.  (The same arithmetic confirms the other
 * literal in this proc: 0x8C3C == `terrainrows`, which sits at 0x443AC and
 * IS named symbolically 22 lines further down in the same proc.)
 *
 * `dw` -> int16_t per the task's mapping rule, even though the original
 * consumes it as the operand of an UNSIGNED `div`; the read below casts.
 */
extern int16_t grassDecelDivTab[];

/* ------------------------------------------------------------------ */
/* x86 shift helpers                                                   */
/* ------------------------------------------------------------------ */

/* `sar r16, n` - arithmetic (sign-propagating) right shift.
 *
 * NOTE: the obvious `~((~(uint16_t)v) >> n)` is WRONG in C. A uint16_t is
 * promoted to int before `~`, so the complement is taken over 32 bits and the
 * shift drags the wrong bits down. It silently turned sar16(-14247,1) into
 * 25644 instead of -7124 (and a second shift into 12822) — which is how a
 * wheel's suspension force became five figures and threw the car off a hill.
 * Shift the unsigned value, then fill the vacated high bits by hand. */
static inline int16_t sar16(int16_t v, int n)
{
	if (n <= 0) return v;
	if (n >= 16) return (int16_t)(v < 0 ? -1 : 0);
	uint16_t r = (uint16_t)((uint16_t)v >> n);
	if (v < 0) r |= (uint16_t)~(uint16_t)(0xFFFFu >> n);
	return (int16_t)r;
}

/* `sar dx,1 / rcr ax,1` repeated n times - arithmetic right shift of dx:ax. */
static inline int32_t sar32(int32_t v, int n)
{
	return (int32_t)(v < 0 ? ~((~(uint32_t)v) >> n) : ((uint32_t)v >> n));
}

/*
 * `mov ax, word ptr [bx+si+0xB4]` where si = surfaceType * 2.
 *
 * [ODDITY] 0xB4 is offsetof(struct SIMD, sliding); the surface_grip[4] array
 * begins at 0xB6.  So the original indexes one element BEFORE surface_grip:
 * a wheel surface type of 0 reads `simd->sliding`, and surface types 1..3 read
 * surface_grip[0..2].  surface_grip[3] is never reachable through this path.
 * Reproduced verbatim with byte-offset arithmetic rather than "corrected".
 */
#define SIMD_GRIP_AT(p, byte_index) \
	(*(int16_t*)((char*)(p) + 0xB4 + (byte_index)))

/* ==================================================================== */
/* update_grip                                                          */
/*                                                                      */
/* seg001.asm lines 6174..6687 (`update_grip_asm_ proc far` ..           */
/* `update_grip_asm_ endp`).                                            */
/*                                                                      */
/* Stack frame verified against the original:                           */
/*     carstate_  = word ptr  6                                         */
/*     simd_      = word ptr  8                                         */
/*     is_player  = word ptr 10                                         */
/* A `far` frame puts [bp+0]=saved bp, [bp+2..5]=far return address, so  */
/* [bp+6] is the first argument.  `.model medium` => code far / data     */
/* near, hence the two pointer arguments occupy one word each.  This     */
/* matches the prototype declared in src/sim_faithful/state.c:           */
/*     void update_grip(struct CARSTATE*, struct SIMD*, int16_t);        */
/* NO DISCREPANCY.                                                       */
/* ==================================================================== */
void update_grip(struct CARSTATE* carstate_, struct SIMD* simd_, int16_t is_player)
{
	/* Locals, named after the original's frame comments. */
	int16_t  var_addf20f36Initial;   /* word ptr -16 */
	uint8_t  var_E;                  /* byte ptr -14 */
	int16_t  var_C;                  /* word ptr -12 */
	uint8_t  var_A;                  /* byte ptr -10 */
	int16_t  var_8;                  /* word ptr  -8 */
	int16_t  var_combinedGrip;       /* word ptr  -6 */
	int16_t  var_4;                  /* word ptr  -4 */
	uint16_t var_speedshr8;          /* word ptr  -2 */

	int16_t  ax, cx, dx;
	uint16_t bx, si, di;
	uint32_t u32;
	int32_t  s32;

	/* mov bx, carstate_ ; cmp byte ptr [bx+0xc1], 0 ; jnz LAB_1471_38ec */
	if (carstate_->car_sumSurfAllWheels == 0) {
		/* If the car is flying the whole subroutine is skipped. */
		carstate_->car_40MfrontWhlAngle = 0;   /* mov word [bx+0x40], 0 */
		carstate_->car_slidingFlag = 0;        /* mov byte [bx+0xc7], 0 */
		return;
	}

LAB_1471_38ec:
	/* Wheels on grass... */
	var_8 = 0;
	if (carstate_->car_surfaceWhl[0] == 4) var_8++;   /* cmp byte [bx+0xc2], 4 */
LAB_1471_38fe:
	if (carstate_->car_surfaceWhl[1] == 4) var_8++;   /* cmp byte [bx+0xc3], 4 */
LAB_1471_3908:
	if (carstate_->car_surfaceWhl[2] == 4) var_8++;   /* cmp byte [bx+0xc4], 4 */
LAB_1471_3912:
	if (carstate_->car_surfaceWhl[3] == 4) var_8++;   /* cmp byte [bx+0xc5], 4 */
LAB_1471_391c:
	if (var_8 == 0) goto LAB_1471_3941;
	{
		/* Grass slowdown!
		 *   mov ax, word ptr [bx+0x2c]        ; car_speed2
		 *   sub dx, dx                        ; dx:ax = zero-extended speed2
		 *   mov bx, var_8 ; shl bx, 1
		 *   div word ptr [bx+0x688]           ; UNSIGNED 32/16 divide
		 */
		uint16_t divisor = (uint16_t)grassDecelDivTab[var_8];
		uint16_t axu;
		u32 = (uint32_t)carstate_->car_speed2;   /* dx = 0 */
		axu = (uint16_t)(u32 / divisor);         /* ax = quotient */
		carstate_->car_speed2 -= axu;            /* sub word [bx+0x2c], ax */
		/* mov si,bx ; mov ax,[si+0x2c] ; mov [bx+0x2a], ax */
		carstate_->car_speed = carstate_->car_speed2;
	}

LAB_1471_3941:
	/* mov ax,[bx+0x20] ; add ax,[bx+0x36] */
	ax = (int16_t)((uint16_t)carstate_->car_steeringAngle +
	               (uint16_t)carstate_->car_36MwhlAngle);
	var_addf20f36Initial = ax;
	var_C = ax;
	/* mov ax,[bx+0x2a] ; mov cl,8 ; shr ax,cl   (UNSIGNED shift) */
	var_speedshr8 = (uint16_t)(carstate_->car_speed >> 8);

	/* cmp var_C, 0 ; jge LAB_1471_3968 ; neg */
	if (var_C >= 0)
		ax = var_C;                      /* LAB_1471_3968 */
	else
		ax = (int16_t)(-(uint16_t)var_C);

LAB_1471_396b:
	/* Grip modifiers... */
	ax = sar16(ax, 3);                   /* mov cl,3 ; sar ax,cl */
	var_8 = ax;
	{
		/*   mov ax, var_speedshr8
		 *   mul ax            ; dx:ax = speedshr8^2 (UNSIGNED)
		 *   mov cl, 6
		 *   shr ax, cl        ; only AX is shifted - DX (the high half of
		 *                     ; the square) is silently discarded
		 *   mul var_8         ; dx:ax = that * var_8 (UNSIGNED)
		 *   mov var_4, ax     ; only the low word is kept
		 * [ODDITY] the original comment claims "/2^9", but the discarded DX
		 * makes this a truncating 16-bit computation, not a 32-bit one.
		 */
		uint16_t axu = var_speedshr8;
		u32 = (uint32_t)axu * (uint32_t)axu;
		axu = (uint16_t)u32;             /* ax = low half; dx = u32 >> 16 */
		axu = (uint16_t)(axu >> 6);      /* shr ax, 6 */
		u32 = (uint32_t)axu * (uint32_t)(uint16_t)var_8;
		var_4 = (int16_t)(uint16_t)u32;  /* mov var_4, ax */
	}

	{
		/* mov bx, simd_ ; mov ax, [bx+0xa4] ; shl ax,1 */
		int16_t sum;
		ax = (int16_t)((uint16_t)simd_->grip << 1);
		var_combinedGrip = ax;
		/* cwd ; push dx ; push ax        -> long(2 * grip)  [pushed FIRST] */

		/* mov al,[bx+0xc5] ; cbw ; mov si,ax ; shl si,1 */
		si = (uint16_t)((int16_t)(signed char)carstate_->car_surfaceWhl[3] << 1);
		/* mov al,[di+0xc4] ; cbw ; mov di,ax ; shl di,1 */
		di = (uint16_t)((int16_t)(signed char)carstate_->car_surfaceWhl[2] << 1);
		cx = SIMD_GRIP_AT(simd_, (int16_t)si);   /* mov cx, [bx+si+0xb4] */
		/* mov al,[bx+0xc3] ; cbw ; mov si,ax ; shl si,1 */
		si = (uint16_t)((int16_t)(signed char)carstate_->car_surfaceWhl[1] << 1);
		dx = SIMD_GRIP_AT(simd_, (int16_t)si);   /* mov dx, [bx+si+0xb4] */
		/* mov al,[bx+0xc2] ; cbw ; mov si,ax ; shl si,1 */
		si = (uint16_t)((int16_t)(signed char)carstate_->car_surfaceWhl[0] << 1);
		ax = SIMD_GRIP_AT(simd_, (int16_t)si);
		/* add ax,dx ; add ax,[bx+di+0xb4] ; add ax,cx  (16-bit wraparound) */
		sum = (int16_t)((uint16_t)ax + (uint16_t)dx);
		sum = (int16_t)((uint16_t)sum + (uint16_t)SIMD_GRIP_AT(simd_, (int16_t)di));
		sum = (int16_t)((uint16_t)sum + (uint16_t)cx);
		/* cwd ; push dx ; push ax        [pushed SECOND] ; call __aFlmul */
		s32 = (int32_t)var_combinedGrip * (int32_t)sum;
		/* Returns 2 * (baseGrip * sumWhlSurfGrip) */

LAB_1471_39eb:
		/* mov cl,0xa ; { sar dx,1 ; rcr ax,1 ; dec cl ; jnz } -> >>10 */
		s32 = sar32(s32, 10);
		var_combinedGrip = (int16_t)(uint16_t)(uint32_t)s32;  /* mov var, ax */
	}

	carstate_->car_demandedGrip = var_4;              /* mov [bx+0x44], ax */
	carstate_->car_surfacegrip_sum = var_combinedGrip;/* mov [bx+0x46], ax */

	/* cmp is_player, 0 ; jnz LAB_1471_3a11 ; jmp LAB_1471_3c4e */
	if (is_player == 0) goto LAB_1471_3c4e;

LAB_1471_3a11:
	/* cmp word ptr [bx+0x20], 0 ; jnz LAB_1471_3a58 */
	if (carstate_->car_steeringAngle != 0) goto LAB_1471_3a58;
	/*   mov al, byte ptr [bx+0x18]   ; LOW BYTE of car_rotate.x
	 *   sub ah, ah                   ; ZERO-extended
	 * [ODDITY] the word field car_rotate.x is read a byte at a time and then
	 * hand-wrapped into -128..127 below, while the inc/dec at the end write
	 * the FULL word.
	 */
	ax = (int16_t)(uint8_t)(uint16_t)carstate_->car_rotate.x;
	var_8 = ax;
	if (ax > 0x7F)                     /* cmp ax,0x7f ; jle LAB_1471_3a2c */
		var_8 = (int16_t)((uint16_t)var_8 - 0x100);
LAB_1471_3a2c:
	if (var_8 == 0) goto LAB_1471_3a58;
	if (var_8 >= 0)
		ax = var_8;                                   /* LAB_1471_3a3c */
	else
		ax = (int16_t)(-(uint16_t)var_8);
LAB_1471_3a3f:
	if (ax >= 8) goto LAB_1471_3a58;   /* cmp ax,8 ; jge */
	if (var_8 > 0)                     /* cmp var_8,0 ; jle LAB_1471_3a52 */
		carstate_->car_rotate.x--;     /* dec word ptr [bx+0x18] */
	else
		carstate_->car_rotate.x++;     /* LAB_1471_3a52: inc word [bx+0x18] */

LAB_1471_3a58:
	/* mov ax, var_combinedGrip ; cmp var_4, ax ; jle LAB_1471_3abe (SIGNED) */
	if (var_4 <= var_combinedGrip) goto LAB_1471_3abe;
	{
		/* This must have something to do with spinning / sliding. */
		int32_t prod, divd;
		carstate_->car_slidingFlag = 1;      /* mov byte [bx+0xc7], 1 */

		/* ax = var_speedshr8 ; cwd ; push ; cwd ; push ; call __aFlmul */
		prod = (int32_t)(int16_t)var_speedshr8 * (int32_t)(int16_t)var_speedshr8;
		/* push dx ; push ax          -> prod is the DIVISOR (pushed first) */

		/*   mov ax, var_combinedGrip ; cwd
		 *   mov dh,dl ; mov dl,ah ; mov ah,al ; sub al,al    ; *2^8
		 * i.e. the sign-extended 32-bit value shifted left by 8.
		 */
		divd = (int32_t)((uint32_t)(int32_t)var_combinedGrip << 8);
		/* push dx ; push ax          -> divd is the DIVIDEND (pushed last) */
		/* call far ptr __aFldiv
		 * [ODDITY] prod == 0 when the car is below 256 speed units; the
		 * original would raise a divide error there just as this does.  Not
		 * guarded, to stay faithful.  (Unreachable in practice: var_4 is 0
		 * when speedshr8 is 0, and this branch needs var_4 > combinedGrip.) */
		var_C = (int16_t)(uint16_t)(uint32_t)(divd / prod);   /* mov var_C, ax */

		/* cmp var_addf20f36Initial,0 ; jge LAB_1471_3a9d */
		if (var_addf20f36Initial < 0) {
			/* mov ax,0xffff ; imul var_C ; mov var_C, ax  (low word only) */
			var_C = (int16_t)(-(uint16_t)var_C);
		}
LAB_1471_3a9d:
		/* mov cx,ax ; shl ax,1 ; add ax,cx  -> 3*var_C  (16-bit wrap) */
		cx = var_C;
		ax = (int16_t)((uint16_t)var_C << 1);
		ax = (int16_t)((uint16_t)ax + (uint16_t)cx);
		ax = (int16_t)((uint16_t)ax + (uint16_t)var_addf20f36Initial);
		ax = sar16(ax, 1);
		ax = sar16(ax, 1);             /* two separate `sar ax,1` */
		var_C = ax;
		/* mov ax, var_addf20f36Initial ; sub ax, var_C ; mov [bx+0x42], ax */
		carstate_->field_42 =
			(int16_t)((uint16_t)var_addf20f36Initial - (uint16_t)var_C);
		goto LAB_1471_3af7;
	}

LAB_1471_3abe:
	carstate_->car_slidingFlag = 0;                 /* mov byte [bx+0xc7], 0 */
	if (carstate_->field_42 == 0) goto LAB_1471_3af7;
	/* mov ax,[si+0x42] ; mov cl,4 ; sar ax,cl ; sub [bx+0x42], ax */
	ax = sar16(carstate_->field_42, 4);
	carstate_->field_42 = (int16_t)((uint16_t)carstate_->field_42 - (uint16_t)ax);
	if (carstate_->field_42 >= 0)
		ax = carstate_->field_42;                   /* LAB_1471_3aec */
	else
		ax = (int16_t)(-(uint16_t)carstate_->field_42);
LAB_1471_3aef:
	if (ax >= 0x10) goto LAB_1471_3af7;             /* cmp ax,0x10 ; jge */
	carstate_->field_42 = sar16(carstate_->field_42, 1);  /* sar word [bx+0x42],1 */

LAB_1471_3af7:
	/* cmp word [bx+0x3e],0 ; jnz LAB_1471_3b10
	 * cmp byte [bx+0xc9],1 ; jz  LAB_1471_3b10 */
	if (carstate_->car_angle_z == 0 && carstate_->car_crashBmpFlag != 1) {
		carstate_->car_40MfrontWhlAngle = var_C;    /* mov [bx+0x40], ax */
	} else {
LAB_1471_3b10:
		carstate_->car_40MfrontWhlAngle = 0;        /* mov word [bx+0x40], 0 */
	}

LAB_1471_3b18:
	/* cmp word ptr [bx+0x1c], 0  ; car_rotate.z */
	if (carstate_->car_rotate.z == 0) goto LAB_1471_3bad;
LAB_1471_3b24:
	/* jge LAB_1471_3b2e - flags still come from the cmp above */
	if (carstate_->car_rotate.z >= 0)
		ax = carstate_->car_rotate.z;               /* LAB_1471_3b2e */
	else
		ax = (int16_t)(-(uint16_t)carstate_->car_rotate.z);
LAB_1471_3b31:
	if (ax <= 4) goto LAB_1471_3bad;                /* cmp ax,4 ; jle */
	{
		uint16_t elem;
		/* mov al, byte ptr [bx+0x2]  - byte 2 of car_posWorld1.lx, i.e. the
		 * tile column; the world position is a 16.16 fixed-point long and
		 * the tile index sits in the low byte of its high word. */
		var_A = (uint8_t)((uint32_t)carstate_->car_posWorld1.lx >> 16);
		/* mov al, byte ptr [bx+0xa]  - byte 2 of car_posWorld1.lz */
		var_E = (uint8_t)((uint32_t)carstate_->car_posWorld1.lz >> 16);

		/* mov bl,al ; sub bh,bh ; shl bx,1 ; mov bx,[bx+0x8c3c]
		 * 0x8C3C is dseg offset of `terrainrows` (see the note at the top). */
		bx = (uint16_t)terrainrows[var_E];
		/* mov al, var_A ; sub ah,ah ; add bx, ax   (ZERO-extended) */
		bx = (uint16_t)(bx + (uint16_t)var_A);
		/* les si,[td14_elem_map_main] ; mov al, byte ptr es:[bx+si]
		 * ah is still 0 from the `sub ah,ah` above, so ax is the
		 * zero-extended byte. */
		elem = (uint16_t)td14_elem_map_main[bx];

		if (elem == 0xFD) goto LAB_1471_3b6c;
		if (elem == 0xFE) goto LAB_1471_3b6f;
		if (elem == 0xFF) goto LAB_1471_3b98;
		goto LAB_1471_3b72;

LAB_1471_3b6c:
		var_A--;                                    /* dec byte var_A */
		/* falls through */
LAB_1471_3b6f:
		var_E++;                                    /* inc byte var_E */
		goto LAB_1471_3b72;

LAB_1471_3b98:
		var_A--;
		goto LAB_1471_3b72;

LAB_1471_3b72:
		/* mov bl,var_E ; sub bh,bh ; shl bx,1 ; mov bx,[bx+terrainrows] */
		bx = (uint16_t)terrainrows[var_E];
		bx = (uint16_t)(bx + (uint16_t)var_A);
		elem = (uint16_t)td14_elem_map_main[bx];
		/* Test for banked corners...! */
		if (elem < 0x34) goto LAB_1471_3bad;        /* cmp ax,0x34 ; jc  (UNSIGNED) */
		if (elem <= 0x37) goto LAB_1471_3b9e;       /* cmp ax,0x37 ; jbe (UNSIGNED) */
		goto LAB_1471_3bad;
	}

LAB_1471_3b9e:
	/* That's what a banked corner does: it makes the car turn by itself by an
	 * amount proportional to the banking slope (tested and confirmed). */
	/* mov ax,[bx+0x1c] ; cwd ; mov cx,5 ; idiv cx ; add [bx+0x40], ax */
	ax = (int16_t)(carstate_->car_rotate.z / 5);
	carstate_->car_40MfrontWhlAngle =
		(int16_t)((uint16_t)carstate_->car_40MfrontWhlAngle + (uint16_t)ax);

LAB_1471_3bad:
	/* mov ax,var_combinedGrip ; add ax,0x3e8 ; cmp ax,var_4 ; jge (SIGNED) */
	ax = (int16_t)((uint16_t)var_combinedGrip + 0x3E8);
	if (ax >= var_4) goto LAB_1471_3bda;
	/* mov ax,var_C ; sub ax,var_addf20f36Initial ; cwd ; mov cx,0xe ; idiv cx */
	ax = (int16_t)((uint16_t)var_C - (uint16_t)var_addf20f36Initial);
	ax = (int16_t)(ax / 14);
	carstate_->car_angle_z =
		(int16_t)((uint16_t)carstate_->car_angle_z + (uint16_t)ax);   /* add [bx+0x3e], ax */
	/* mov cx,2 ; mov ax,[bx+0x3e] ; cwd ; idiv cx */
	ax = (int16_t)(carstate_->car_angle_z / 2);
	goto LAB_1471_3c7b;

LAB_1471_3bda:
	if (carstate_->car_angle_z == 0) goto LAB_1471_3c7e;
LAB_1471_3be6:
	ax = (int16_t)((uint16_t)var_C - (uint16_t)var_addf20f36Initial);
	ax = (int16_t)(ax / 14);                        /* cwd ; mov cx,0xe ; idiv cx */
	carstate_->car_angle_z =
		(int16_t)((uint16_t)carstate_->car_angle_z + (uint16_t)ax);
	ax = (int16_t)(carstate_->car_angle_z / 2);     /* cwd ; mov cx,2 ; idiv cx */
	carstate_->car_angle_z = ax;
	if (carstate_->car_angle_z != 0) goto LAB_1471_3c7e;
	{
		/*   push [bx+0x2c]         ; car_speed2  -> 2nd arg (pushed first)
		 *   push [bx+0x36]         ; car_36MwhlAngle
		 *   call far ptr int_cos   ; == cos_fast() in math.c
		 *   add sp,2
		 *   push ax                ; -> 1st arg
		 *   call far ptr multiply_and_scale
		 *   add sp,4
		 */
		int16_t c = cos_fast((uint16_t)carstate_->car_36MwhlAngle);
		carstate_->car_speed2 =
			(uint16_t)multiply_and_scale(c, (int16_t)carstate_->car_speed2);

		/* push [bx+0x36] ; call int_cos ; add sp,2 ; or ax,ax ; jge */
		ax = cos_fast((uint16_t)carstate_->car_36MwhlAngle);
		if (ax < 0)
			carstate_->car_speed2 = 0;              /* mov word [bx+0x2c], 0 */
LAB_1471_3c44:
		carstate_->car_36MwhlAngle = 0;             /* mov word [bx+0x36], 0 */
		goto LAB_1471_3c7e;
	}

LAB_1471_3c4e:
	/* mov ax,[si+0x20] ; shl ax,1 ; shl ax,1 ; mov [bx+0x40], ax */
	ax = (int16_t)((uint16_t)carstate_->car_steeringAngle << 2);
	carstate_->car_40MfrontWhlAngle = ax;
	if (carstate_->car_angle_z == 0) goto LAB_1471_3c7e;
	/* mov ax,[si+0x3e] ; mov cx,ax ; shl ax,1 x4 ; sub ax,cx ; sar ax,4
	 * -> (15 * car_angle_z) >> 4 */
	cx = carstate_->car_angle_z;
	ax = (int16_t)((uint16_t)cx << 4);
	ax = (int16_t)((uint16_t)ax - (uint16_t)cx);
	ax = sar16(ax, 4);
LAB_1471_3c7b:
	carstate_->car_angle_z = ax;                    /* mov [bx+0x3e], ax */

LAB_1471_3c7e:
	/* cmp [bx+0x36],0 ; jz .. ; cmp [bx+0x3e],0 ; jnz .. */
	if (carstate_->car_36MwhlAngle != 0 && carstate_->car_angle_z == 0) {
		/* (15 * car_36MwhlAngle) >> 4 */
		cx = carstate_->car_36MwhlAngle;
		ax = (int16_t)((uint16_t)cx << 4);
		ax = (int16_t)((uint16_t)ax - (uint16_t)cx);
		ax = sar16(ax, 4);
		carstate_->car_36MwhlAngle = ax;
	}

LAB_1471_3ca5:
	if (carstate_->car_angle_z != 0) {
		/* mov ax,[si+0x3e] ; sub [bx+0x36], ax */
		ax = carstate_->car_angle_z;
		carstate_->car_36MwhlAngle =
			(int16_t)((uint16_t)carstate_->car_36MwhlAngle - (uint16_t)ax);
	}

LAB_1471_3cb6:
	/* cmp byte [bx+0xc7],0 ; jnz LAB_1471_3cc3 ; jmp LAB_1471_3d48 */
	if (carstate_->car_slidingFlag == 0) goto LAB_1471_3d48;
LAB_1471_3cc3:
	if (carstate_->field_42 >= 0)
		ax = carstate_->field_42;                   /* LAB_1471_3cd0 */
	else
		ax = (int16_t)(-(uint16_t)carstate_->field_42);
LAB_1471_3cd3:
	ax = (int16_t)((uint16_t)ax << 1);              /* shl ax,1 */
	var_8 = ax;
	/* cmp [bx+0x2a], ax ; jbe LAB_1471_3d38   (UNSIGNED - car_speed) */
	if (carstate_->car_speed <= (uint16_t)ax) goto LAB_1471_3d38;
	/* cmp [bx+0x2c], ax ; jbe LAB_1471_3cf0   (UNSIGNED - car_speed2) */
	if (carstate_->car_speed2 <= (uint16_t)ax) goto LAB_1471_3cf0;
	carstate_->car_speed  = (uint16_t)(carstate_->car_speed  - (uint16_t)ax);
	carstate_->car_speed2 = (uint16_t)(carstate_->car_speed2 - (uint16_t)var_8);
	goto LAB_1471_3d00;

LAB_1471_3cf0:
	carstate_->car_speed = 0;
	carstate_->car_speed2 = 0;

LAB_1471_3d00:
	if (carstate_->car_crashBmpFlag != 0) goto LAB_1471_3d48;
	if (carstate_->car_surfaceWhl[0] == 1) goto LAB_1471_3d26;
	if (carstate_->car_surfaceWhl[1] == 1) goto LAB_1471_3d26;
	if (carstate_->car_surfaceWhl[2] == 1) goto LAB_1471_3d26;
	if (carstate_->car_surfaceWhl[3] != 1) goto LAB_1471_3d2e;
LAB_1471_3d26:
	carstate_->field_CF |= 2;                       /* or byte [bx+0xcf], 2 */
	goto LAB_1471_3d48;
LAB_1471_3d2e:
	carstate_->field_CF |= 4;                       /* or byte [bx+0xcf], 4 */
	goto LAB_1471_3d48;

LAB_1471_3d38:
	carstate_->car_speed = 0;
	carstate_->car_speed2 = 0;

LAB_1471_3d48:
	carstate_->field_42 = 0;                        /* mov word [bx+0x42], 0 */
}

/* ==================================================================== */
/* upd_statef20_from_steer_input                                        */
/*                                                                      */
/* seg001.asm lines 7054..7185                                          */
/* (`upd_statef20_from_steer_input_asm_ proc far` .. `endp`).           */
/*                                                                      */
/* Stack frame verified against the original:                           */
/*     var_speed2shr0AandFC = byte ptr -4                               */
/*     steering             = byte ptr  6                               */
/* One byte-sized argument, matching the prototype declared in           */
/* src/sim_faithful/state.c:                                            */
/*     void upd_statef20_from_steer_input(char);                         */
/* NO DISCREPANCY.  (The parameter is named arg_carInputByte in the task */
/* brief and `steering` in the asm; the asm name is kept here.)          */
/*                                                                      */
/* No jump tables: the only tabular data is steerWhlRespTable_ptr, a     */
/* near data pointer into a 0x40-entry response table (2 bytes per       */
/* entry pair as used below), read through `((char*)ptr)[index]`.        */
/* ==================================================================== */
void upd_statef20_from_steer_input(char steering)
{
	int8_t   var_speed2shr0AandFC;   /* byte ptr -4 */
	int16_t  ax, si, di;
	uint16_t bx;
	const char* table = (const char*)steerWhlRespTable_ptr;

	/* mov di, word ptr [state.playerstate.car_steeringAngle] */
	di = state.playerstate.car_steeringAngle;
	/* mov ax,[state.playerstate.car_speed2] ; mov cl,0xa ; shr ax,cl */
	ax = (int16_t)(uint16_t)(state.playerstate.car_speed2 >> 10);
	/* and al, 0xfc  (ah is already 0: speed2>>10 <= 0x3F) */
	ax = (int16_t)(uint16_t)((uint16_t)ax & 0x00FC);
	var_speed2shr0AandFC = (int8_t)(uint8_t)(uint16_t)ax;
	/* cbw ; mov bx, ax */
	bx = (uint16_t)(int16_t)var_speed2shr0AandFC;
	/* mov al,[bp+steering] ; cbw ; add bx, ax */
	bx = (uint16_t)(bx + (uint16_t)(int16_t)(signed char)steering);
	/* After (add bx, ax) the low bx byte contains speed2 / 4096 on the middle
	 * 4 bits and the steering input on the lowest-significant two.
	 *   add bx, word ptr [steerWhlRespTable_ptr]
	 * That means there are 64 possible values for it, which makes sense
	 * considering how it is used... */
	/* mov al,[bx] ; cbw ; mov si, ax */
	si = (int16_t)(signed char)table[bx];

	/* or si,si ; jle LAB_1471_40d8 */
	if (si > 0) {
		if (di >= -1) goto LAB_1471_40e5;   /* cmp di,-1 ; jge (SIGNED) */
		goto LAB_1471_40e1;
	}
LAB_1471_40d8:
	if (si == 0) goto LAB_1471_40e5;
	if (di <= 1) goto LAB_1471_40e5;        /* cmp di,1 ; jle (SIGNED) */
LAB_1471_40e1:
	si = (int16_t)((uint16_t)si << 2);      /* mov cl,2 ; shl si,cl */

LAB_1471_40e5:
	if (si != 0) goto LAB_1471_4125;
	/* This parenthetical block seems to be a corrective procedure for low
	 * speeds. */
	if (state.playerstate.car_speed2 == 0) goto LAB_1471_4125;
	/* di = field20 */
	if (di == 0) goto LAB_1471_4125;
	/* mov al,var_speed2shr0AandFC ; cbw ; mov bx,ax
	 *   add bx, steerWhlRespTable_ptr
	 * NOTE: the `steering` component is deliberately NOT added here, so this
	 * always reads the entry-0 slot of the current speed row. */
	bx = (uint16_t)(int16_t)var_speed2shr0AandFC;
	/* mov al,[bx+1] ; cbw ; mov si,ax ; shl si,1  (tables?! With 40h values!!) */
	si = (int16_t)(signed char)table[bx + 1];
	si = (int16_t)((uint16_t)si << 1);

	if (di >= 0)
		ax = di;                            /* LAB_1471_4110 */
	else
		ax = (int16_t)(-(uint16_t)di);
LAB_1471_4112:
	if (ax <= si) goto LAB_1471_411e;       /* cmp ax,si ; jle (SIGNED) */
	if (di <= 0) goto LAB_1471_4125;        /* or di,di ; jle */
	ax = si;
	goto LAB_1471_4121;
LAB_1471_411e:
	/* [ODDITY] re-reads the global instead of using di, which holds the same
	 * value (di has not been modified since it was loaded). */
	ax = state.playerstate.car_steeringAngle;
LAB_1471_4121:
	ax = (int16_t)(-(uint16_t)ax);          /* neg ax */
	si = ax;

LAB_1471_4125:
	if (framespersec == 0xA) {
		if (si > 0x00A0) si = 0x00A0;                 /* cmp si,0xa0 ; jle */
LAB_1471_4135:
		if (si < (int16_t)0xFF60) si = (int16_t)0xFF60;  /* -160 */
		goto LAB_1471_4150;
	}
LAB_1471_4140:
	if (si > 0x50) si = 0x50;               /* cmp si,0x50 ; jle */
LAB_1471_4148:
	if (si < -0x50) si = (int16_t)0xFFB0;   /* cmp si,-0x50 ; jge */

LAB_1471_4150:
	di = (int16_t)((uint16_t)di + (uint16_t)si);      /* add di, si */
	if (di > 0xF0) di = 0xF0;                         /* cmp di,0xf0 ; jle */
LAB_1471_415B:
	if (di < (int16_t)0xFF10) di = (int16_t)0xFF10;   /* -240 */

LAB_1471_4164:
	/* mov al,var_speed2shr0AandFC ; cbw ; mov bx,ax
	 * mov al,[bp+steering] ; cbw ; add bx,ax ; add bx, steerWhlRespTable_ptr */
	bx = (uint16_t)(int16_t)var_speed2shr0AandFC;
	bx = (uint16_t)(bx + (uint16_t)(int16_t)(signed char)steering);
	if (table[bx] != 0) goto LAB_1471_4189;   /* cmp byte [bx],0 ; jnz */
	/* push di ; call far ptr _abs ; add sp,2 ; cmp ax,8 ; jge */
	ax = (int16_t)abs((int)di);
	if (ax >= 8) goto LAB_1471_4189;
	di = 0;                                   /* sub di, di */

LAB_1471_4189:
	state.playerstate.car_steeringAngle = di;
}

/* ==================================================================== */
/* carState_rc_op                                                       */
/*                                                                      */
/* reference/restunts2/src/asm/seg001.asm lines 6858..7046              */
/* (`; int __cdecl16far carState_rc_op(...)` / `carState_rc_op_asm_     */
/* proc far` at 6859 .. `carState_rc_op_asm_ endp` at 7046).            */
/* The restunts tree (reference/restunts/src/restunts/asm/seg001.inc)   */
/* only carries `extrn carState_rc_op:proc`; there is no body there.    */
/*                                                                      */
/* Stack frame verified against the original:                           */
/*     var_6     = word ptr  -6                                         */
/*     var_4     = word ptr  -4                                         */
/*     var_2     = word ptr  -2                                         */
/*     car       = word ptr   6                                         */
/*     unk       = word ptr   8                                         */
/*     wheel_idx = word ptr  10                                         */
/* A far frame puts [bp+0]=saved bp and [bp+2..5]=far return address,   */
/* so [bp+6] is the first argument; `.model medium` makes data near, so */
/* the CARSTATE* occupies a single word.  This matches the requested    */
/* prototype exactly - NO DISCREPANCY.                                  */
/*                                                                      */
/* Field offsets (struct CARSTATE, #pragma pack(1)):                    */
/*     0x54 = car_rc2[]  (the asm even comments "54 = car_rc2")         */
/*     0x64 = car_rc4[]  ("64 = car_rc4")                               */
/*     0x6c = car_rc5[]                                                 */
/* Every access is `wheel_idx*2 + car + <offset>`, i.e. plain [wheel].  */
/*                                                                      */
/* No dseg data tables are referenced by this proc; every constant is   */
/* an immediate (4, 0x80, 0xc0, 0x180, 0xfe80, 0xfee0).                 */
/* ==================================================================== */
int16_t carState_rc_op(struct CARSTATE* car, int16_t unk, int16_t wheel_idx)
{
	/* TEMPORARY probe: rc2 is clamped to +-0x180 by every path in here, so a
	 * larger value means someone else wrote the slot. */
	/* Defined here rather than in the harness, so the playable build (which
	 * does not link tools/dump_native_states.c) still resolves it. */
	extern int dbg_rc_probe;
	int16_t dbg_before = (wheel_idx >= 0 && wheel_idx < 4) ? car->car_rc2[wheel_idx] : -1;
	if (dbg_rc_probe && (wheel_idx < 0 || wheel_idx > 3))
		printf("!! carState_rc_op med wheel_idx=%d\n", wheel_idx);
	if (dbg_rc_probe && (dbg_before > 0x180 || dbg_before < -0x180))
		printf("!! rc2[%d] var redan %d VID INGÅNG (unk=%d)\n", wheel_idx, dbg_before, unk);
	int16_t var_6;   /* word ptr -6 */
	int16_t var_4;   /* word ptr -4 */
	int16_t var_2;   /* word ptr -2 */

	int16_t ax;
	/* `si` and `di` are used by the original both as scratch index
	 * registers and, in a few places, as a near pointer to the rc slot.
	 * Since every such pointer resolves to &car->car_rcN[wheel_idx], the
	 * dereferences below are written as the equivalent field accesses. */
	int16_t si;

	/* mov si,[bp+wheel_idx] ; shl si,1 ; add si,[bp+car]
	 * mov ax,[si+0x54]      ; "rc accesses are offset by the wheel index." */
	ax = car->car_rc2[wheel_idx];
	var_2 = ax;
	var_4 = 0;
	var_6 = 0;

	/* cmp word ptr [si+0x6c], 0 ; jz / jge */
	if (car->car_rc5[wheel_idx] == 0) goto LAB_1471_3f49;
	if (car->car_rc5[wheel_idx] >= 0) goto LAB_1471_3f26;

	/* rc5 < 0: creep back towards zero from below. */
	car->car_rc5[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc5[wheel_idx] + 4);  /* add word [si+0x6c],4 */
	/* si is recomputed to point AT car_rc5[wheel_idx] here. */
	ax = car->car_rc5[wheel_idx];
	if (var_6 >= ax) goto LAB_1471_3f49;      /* cmp var_6,ax ; jge (SIGNED) */
	goto LAB_1471_3f44;

LAB_1471_3f26:
	/* rc5 > 0: creep back towards zero from above. */
	car->car_rc5[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc5[wheel_idx] - 4);  /* sub word [bx+di+0x6c],4 */
	ax = car->car_rc5[wheel_idx];
	if (var_6 <= ax) goto LAB_1471_3f49;      /* cmp var_6,ax ; jle (SIGNED) */

LAB_1471_3f44:
	/* mov ax,[bp+var_6] ; mov [si],ax
	 * [ODDITY] var_6 is initialised to 0 and never written again, so this
	 * is always a clamp to literal 0.  Kept as the variable it really is. */
	car->car_rc5[wheel_idx] = var_6;

LAB_1471_3f49:
	/* mov di,wheel_idx ; shl di,1 ; mov bx,car
	 * mov si,[bx+di+0x6c] ; mov di,wheel_idx ; shl di,1
	 * mov [bx+di+0x6c],si
	 * [ODDITY] loads car_rc5[wheel_idx] into si and immediately stores it
	 * back to the very same slot - a complete no-op.  Reproduced (as a
	 * genuine read/write, since the original really does touch memory). */
	si = car->car_rc5[wheel_idx];
	car->car_rc5[wheel_idx] = si;

	if (unk >= 0) goto LAB_1471_3f79;         /* cmp [bp+unk],0 ; jge */
	/* mov ax,[bp+unk] ; neg ax ; cmp [bx+di+0x54],ax ; jle */
	ax = (int16_t)(-(uint16_t)unk);
	if (car->car_rc2[wheel_idx] <= ax) goto LAB_1471_3f79;   /* SIGNED jle */
	unk = 0;

LAB_1471_3f79:
	if (unk != 0) goto LAB_1471_3fea;         /* cmp [bp+unk],0 ; jnz */

	/* ---- unk == 0: relax rc2 towards rc5 in steps of 0x80. ---- */
	ax = car->car_rc5[wheel_idx];
	if (car->car_rc2[wheel_idx] <= ax) goto LAB_1471_3fb8;   /* SIGNED jle */

	/* rc2 > rc5: step down. */
	car->car_rc2[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc2[wheel_idx] - 0x80);  /* sub [si+0x54],0x80 */
	ax = car->car_rc5[wheel_idx];
	if (car->car_rc2[wheel_idx] >= ax) goto LAB_1471_3fa7;    /* SIGNED jge */
	car->car_rc2[wheel_idx] = ax;

LAB_1471_3fa7:
	/* mov ax,[bp+var_2] ; sub ax,[bx+si+0x54] */
	ax = (int16_t)((uint16_t)var_2 - (uint16_t)car->car_rc2[wheel_idx]);
	goto LAB_1471_4092;

LAB_1471_3fb8:
	ax = car->car_rc5[wheel_idx];
	if (car->car_rc2[wheel_idx] < ax) goto LAB_1471_3fcb;     /* SIGNED jl */
	goto LAB_1471_4095;

LAB_1471_3fcb:
	/* rc2 < rc5: step up.
	 * [ODDITY] this mirror of the step-down branch never assigns var_4, so
	 * an upward relaxation contributes nothing to the return value while a
	 * downward one does.  The asymmetry is in the original. */
	car->car_rc2[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc2[wheel_idx] + 0x80);  /* add [si+0x54],0x80 */
	ax = car->car_rc5[wheel_idx];
	if (car->car_rc2[wheel_idx] > ax) goto LAB_1471_3fe3;     /* SIGNED jg */
	goto LAB_1471_4095;

LAB_1471_3fe3:
	car->car_rc2[wheel_idx] = ax;
	goto LAB_1471_4095;

LAB_1471_3fea:
	if (unk <= 0) goto LAB_1471_4038;         /* cmp [bp+unk],0 ; jle (SIGNED) */

	/* ---- unk > 0: compression, capped per tick at 0xc0. ---- */
	if (unk <= 0xc0) goto LAB_1471_4006;      /* cmp [bp+unk],0xc0 ; jle (SIGNED) */
	car->car_rc2[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc2[wheel_idx] + 0xc0);  /* add [bx+si+0x54],0xc0 */
	goto LAB_1471_4014;

LAB_1471_4006:
	car->car_rc2[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc2[wheel_idx] + (uint16_t)unk);

LAB_1471_4014:
	/* si -> &car_rc2[wheel_idx]  ("54 = car_rc2") */
	if (car->car_rc2[wheel_idx] > 0x180)      /* cmp [si],0x180 ; jle (SIGNED) */
		car->car_rc2[wheel_idx] = 0x180;

LAB_1471_4029:
	car->car_rc4[wheel_idx] = 0;              /* "64 = car_rc4" */
	goto LAB_1471_4095;

LAB_1471_4038:
	/* ---- unk < 0: extension. ---- */
	/* mov ax,[bp+unk] ; add ax,[si] ; cmp ax,0xfee0 ; jle (SIGNED, -288) */
	ax = (int16_t)((uint16_t)unk + (uint16_t)car->car_rc2[wheel_idx]);
	if (ax <= (int16_t)0xfee0) goto LAB_1471_4054;
	car->car_rc2[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc2[wheel_idx] + (uint16_t)unk);  /* add [si],ax */
	goto LAB_1471_4081;

LAB_1471_4054:
	/* Would overshoot the floor: apply only 3/4 of unk.
	 *   mov ax,[bp+unk] ; mov cx,ax ; shl ax,1 ; add ax,cx ; sar ax,1 ; sar ax,1
	 * i.e. ax = (unk*3) >> 2, arithmetic, with 16-bit wraparound in the *3. */
	ax = (int16_t)((uint16_t)((uint16_t)unk << 1) + (uint16_t)unk);
	ax = sar16(ax, 1);
	ax = sar16(ax, 1);
	car->car_rc2[wheel_idx] =
		(int16_t)((uint16_t)car->car_rc2[wheel_idx] + (uint16_t)ax);
	if (car->car_rc2[wheel_idx] < (int16_t)0xfe80)   /* cmp [si],0xfe80 ; jge (-384) */
		car->car_rc2[wheel_idx] = (int16_t)0xfe80;

LAB_1471_4081:
	/* mov ax,[bp+var_2] ; sub ax,[bx+si+0x54] ; add ax,[bp+unk] */
	ax = (int16_t)((uint16_t)var_2 - (uint16_t)car->car_rc2[wheel_idx]);
	ax = (int16_t)((uint16_t)ax + (uint16_t)unk);

LAB_1471_4092:
	var_4 = ax;

LAB_1471_4095:
	/* mov ax,[bp+var_2] ; add ax,[bp+var_4]
	 * [ODDITY] the result is the ORIGINAL rc2 plus the delta term, not the
	 * updated rc2; in the LAB_1471_3fa7 path that makes it
	 * old + (old - new).  Reproduced exactly as disassembled. */
	return (int16_t)((uint16_t)var_2 + (uint16_t)var_4);
}

/* ==================================================================== */
/* sub_18D60                                                            */
/*                                                                      */
/* reference/restunts/src/restunts/asmorig/seg001.asm                   */
/* lines 7740 (`sub_18D60 proc far`) .. 8123 (`sub_18D60 endp`).        */
/* Bounds confirmed with grep; 383 instruction lines + the endp.        */
/*                                                                      */
/* Stack frame verified against the original:                           */
/*     arg_0 = word ptr  6                                              */
/*     arg_2 = word ptr  8                                              */
/*     arg_4 = byte ptr 10                                              */
/*     arg_6 = word ptr 12                                              */
/* Every call site (seg001.asm 265, 524, 4151, 4274, 4942, 5037, 5056,  */
/* 5068, 5114, 8675) does `push cs ; call near ptr sub_18D60 ; add sp,8`*/
/* - i.e. a hand-built FAR frame with four word-sized arguments, so     */
/* [bp+6] really is argument 1 and the callee `retf`s.                  */
/*                                                                      */
/* Prototype declared at src/render_faithful/externs.h:404:             */
/*   int16_t sub_18D60(int16_t car_trackdata3_index,                    */
/*                     struct VECTOR* car_vec_unk3,                     */
/*                     int16_t field_CE, int16_t* unk);                 */
/* Argument COUNT and slot widths agree.  Two [DISCREPANCY] notes:      */
/*   (1) arg_4 is `byte ptr` in the frame and the body only ever touches*/
/*       its low byte (`mov al,[bp+arg_4]`).  Callers push a whole word,*/
/*       so int16_t is a legal spelling, but only bits 0..7 are read -  */
/*       reproduced below with explicit (uint8_t)/(int8_t) casts.       */
/*   (2) arg_6 is declared `int16_t*` but the only store through it is  */
/*       `mov [bx], al` - a single BYTE.  Writing a word there would    */
/*       clobber one byte too many.  The byte store is reproduced; the  */
/*       prototype is left untouched as instructed.  All six sim call   */
/*       sites pass 0, so nothing observable depends on it today.       */
/*   (3) arg_2 is declared `struct VECTOR*` (6 bytes) but the routine   */
/*       writes 0x14 bytes through it: VECTOR at +0, VECTOR at +6,      */
/*       VECTOR at +0xC and a word at +0x12.  With arg_2 ==             */
/*       &carstate->car_vec_unk3 that is exactly car_vec_unk3 /         */
/*       car_vec_unk4 / car_vec_unk5 / field_B6, and the two stack      */
/*       callers pass `struct VECTOR[4]` buffers, so it fits - but the  */
/*       declared type understates the write by 14 bytes.               */
/*                                                                      */
/* ---------------------------------------------------------------- */
/* [BLOCKED - CANNOT EXECUTE IN THIS TREE]                            */
/* The translation below is complete and instruction-exact, but four   */
/* of the objects it reads do not exist in the port:                   */
/*                                                                     */
/*  a) td17_trk_elem_ordered / trackdata18 / td21_col_from_path /      */
/*     td22_row_from_path are declared in externs.h but DEFINED        */
/*     NOWHERE, and they are produced at run time by track_setup()     */
/*     (seg004.asm), which is not ported.  The oracle gets them from   */
/*     tools/oracle/repldrv.asm, which calls the real track_setup.     */
/*  b) trkObjectList[].ss_trkObjInfoPtr still holds the RAW DOS dseg   */
/*     offset (rdata.c: `(struct TRKOBJINFO*)0x1A24`, ...).  The       */
/*     `shapeinfos` TRKOBJINFO table (dseg.asm line 8021, 120 x 14     */
/*     bytes) is deliberately out of scope for the renderer port -     */
/*     see the header comment of tools/extract_dseg_tables.py - so     */
/*     the very first dereference of var_6 below is a wild pointer.    */
/*  c) TRKOBJINFO.si_cameraDataOffset and the si_opp1/si_opp2 word are */
/*     themselves near dseg offsets pointing at further, unidentified  */
/*     VECTOR blobs in dseg; the port has no dseg image to resolve     */
/*     them against.                                                   */
/*  d) struct TRKOBJINFO is 14 bytes in DOS but 20 bytes here, because */
/*     si_cameraDataOffset is a native pointer.  The `subTOI * 14`     */
/*     stride is therefore written as C array indexing, which is the   */
/*     right thing once shapeinfos exists but is not byte-identical    */
/*     arithmetic.                                                     */
/*                                                                     */
/* Nothing is invented here to paper over (a)-(d): the code performs   */
/* exactly the operations the original performs and will fault on the  */
/* first call until the missing data is supplied.                      */
/* ==================================================================== */

/* [MISSING DEFINITION] `dw 0` in dseg.asm at 0x45D3E, immediately BEFORE
 * `trackrows` (dseg.asm lines 40125/40126 are adjacent).  Declared in
 * externs.h:251, defined nowhere.  Needed by bto_auxiliary1 below, where
 * `word_45D3E[bx]` with bx = index*2 is literally `trackrows[index - 1]`
 * (the asm listing carries that very comment). */
int dbg_rc_probe;   /* set by tools/dump_native_states.c */

extern int16_t word_45D3E;

/* [MISSING DEFINITION] element-dependent local VECTOR tables in dseg.asm.
 * Byte storage (`db`), read as little-endian int16 triples by
 * `mov ax,[bx] / [bx+2] / [bx+4]`.  dseg base is word_3B770 == 0x3B770, so
 * unk_3E640 is dseg offset 0x2ED0, unk_3E646 0x2ED6, unk_3E676 0x2F06,
 * unk_3E682 0x2F12, unk_3E68E 0x2F1E, unk_3E69A 0x2F2A; the asm references
 * them symbolically (`offset unk_3E640`), so no arithmetic is needed to
 * identify them.  Their byte lengths (6/48/12/12/12/24) match the entry
 * counts the code loads into DI (1/8/2/2/2/4) exactly. */
extern const uint8_t unk_3E640[];   /* 1 VECTOR  */
extern const uint8_t unk_3E646[];   /* 8 VECTORs */
extern const uint8_t unk_3E676[];   /* 2 VECTORs */
extern const uint8_t unk_3E682[];   /* 2 VECTORs */
extern const uint8_t unk_3E68E[];   /* 2 VECTORs */
extern const uint8_t unk_3E69A[];   /* 4 VECTORs */

/* Already defined in the port, but not declared in externs.h:
 *   trkObjectList -> src/render_faithful/rdata.c:320 (215 x TRACKOBJECT)
 *   oppnentSped   -> src/sim_faithful/sfdata.c (dseg.asm 36193: 16 x `db 0`)
 * Same spellings rframe_helpers.c:117 already uses. */
extern struct TRACKOBJECT trkObjectList[215];
extern char oppnentSped[16];

/* `mov ax, [bx+n]` against a `db` table: little-endian 16-bit load. */
static inline int16_t ldw(const uint8_t* p, int byteoff)
{
	return (int16_t)((uint16_t)p[byteoff] | ((uint16_t)p[byteoff + 1] << 8));
}

int16_t sub_18D60(int16_t arg_0, struct VECTOR* arg_2, int16_t arg_4, int16_t* arg_6)
{
	/* --- locals, named after the original's frame comments --- */
	int16_t* var_2C;                   /* word ptr -44 (:-42 = ds) */
	uint8_t  var_td18connStatus;       /* byte ptr -40 */
	struct TRACKOBJECT* var_offsetTrkObject; /* word ptr -38 */
	int16_t  var_24;                   /* word ptr -36 */
	int16_t  var_22, var_20, var_1E;   /* word ptr -34/-32/-30 */
	uint8_t  var_td18subTOI;           /* byte ptr -28 */
	uint8_t  var_1A;                   /* word ptr -26, only the low byte is
	                                    * ever written and every read either
	                                    * takes the low byte or ANDs 0FFh */
	uint8_t  var_18;                   /* byte ptr -24 */
	uint8_t  var_16;                   /* byte ptr -22 */
	struct TRKOBJINFO* var_TOInfoPtr;  /* word ptr -20 (:-18 = ds) */
	uint8_t  var_10;                   /* byte ptr -16 */
	int16_t  var_E;                    /* word ptr -14 */
	int16_t  var_C, var_A, var_8;      /* word ptr -12/-10/-8 */
	struct TRKOBJINFO* var_6;          /* dword ptr -6 */
	uint8_t  var_tileElem;             /* byte ptr -2 */

	int16_t  ax;
	uint16_t bx;
	uint8_t  al;
	const uint8_t* p;                  /* var_30 / si in the movsw blocks */
	char* out = (char*)arg_2;

	/* mov bx,[bp+arg_0] ; les si, td17_trk_elem_ordered ; mov al, es:[bx+si] */
	var_tileElem = (uint8_t)td17_trk_elem_ordered[arg_0];
	/* les si, trackdata18 ; mov al,es:[bx+si] ; and al,0Fh
	 *                      mov al,es:[bx+si] ; and al,10h
	 * (the same byte is fetched twice) */
	var_td18subTOI     = (uint8_t)((uint8_t)trackdata18[arg_0] & 0x0F);
	var_td18connStatus = (uint8_t)((uint8_t)trackdata18[arg_0] & 0x10);

	/* mov al,var_tileElem ; sub ah,ah ; ax = ax*14 ; add ax, offset trkObjectList */
	var_offsetTrkObject = &trkObjectList[var_tileElem];
	/* mov bx,ax ; mov ax,[bx] ; mov var_TOInfoPtr,ax ; mov var_12,ds */
	var_TOInfoPtr = var_offsetTrkObject->ss_trkObjInfoPtr;
	/* Tiles with no shapeinfo (blank, ghost cars) read dseg:0000 in DOS; here
	 * they read a zeroed block. Counted so the substitution can be shown not
	 * to matter - see sfshapeinfo.c. */
	if (var_TOInfoPtr >= shapeinfo_null &&
	    var_TOInfoPtr < shapeinfo_null + 16)
		shapeinfo_null_hits++;
	/* mov al,var_td18subTOI ; sub ah,ah ; ax = ax*14 ; add ax,var_TOInfoPtr
	 * -> far pointer var_6 = TOInfoPtr + subTOI * sizeof(TRKOBJINFO) */
	var_6 = &var_TOInfoPtr[var_td18subTOI];

	var_24 = 0;                                     /* mov var_24, 0 */
	/* les bx,var_6 ; mov al,es:[bx+si_arrowType] */
	var_18 = (uint8_t)var_6->si_arrowType;

	/* cmp var_td18connStatus,0 ; jnz loc_18DE2 */
	if (var_td18connStatus == 0) {
		/* mov al,[bp+arg_4] ; shl al,1     - 8-BIT shift */
		al = (uint8_t)((uint8_t)(uint16_t)arg_4 << 1);
	} else {
/* loc_18DE2: */
		/* mov al,var_18 ; sub al,[bp+arg_4] ; shl al,1 ; sub al,2 - all 8-bit */
		al = (uint8_t)(var_18 - (uint8_t)(uint16_t)arg_4);
		al = (uint8_t)(al << 1);
		al = (uint8_t)(al - 2);
	}
/* loc_18DEC: */
	var_10 = al;                                    /* mov var_10, al */

	/* cmp [bp+arg_6],0 ; jz loc_18E1A */
	if (arg_6 != 0) {
		/* es:bx still points at var_6 */
		var_16 = (uint8_t)var_6->si_oppSpedCode;
		/* mov bx,var_offsetTrkObject ; mov al,[bx+ss_surfaceType] */
		var_1A = (uint8_t)var_offsetTrkObject->ss_surfaceType;
		/* mov si,var_1A ; and si,0FFh ; mov bl,var_16 ; sub bh,bh
		 * mov al, oppnentSped[bx+si] */
		al = (uint8_t)oppnentSped[(uint16_t)var_16 + ((uint16_t)var_1A & 0xFF)];
		/* mov bx,[bp+arg_6] ; mov [bx],al
		 * [DISCREPANCY] BYTE store through an `int16_t*` parameter. */
		*(uint8_t*)arg_6 = al;
	}

/* loc_18E1A: */
	/* les bx,var_6 ; cmp word ptr es:[bx+si_opp1],0 ; jz loc_18E29
	 * si_opp1 sits at TRKOBJINFO offset 10 and si_opp2 at 11, so this is a
	 * WORD read spanning both: some entries use the pair as a near pointer. */
	{
		uint16_t opp1w = (uint16_t)((uint8_t)var_6->si_opp1 |
		                            ((uint16_t)(uint8_t)var_6->si_opp2 << 8));
		if (opp1w != 0)
			var_24 = 1;                             /* mov var_24, 1 */

/* loc_18E29: */
		/* cmp var_td18connStatus,0 ; jz loc_18E76 */
		if (var_td18connStatus == 0) goto loc_18E76;
		/* cmp var_24,0 ; jz loc_18E3C */
		if (var_24 == 0) goto loc_18E3C;
		/* mov ax, word ptr es:[bx+si_opp1] ; jmp loc_18E7D
		 * A near dseg offset re-used as a data pointer. It cannot live in the
		 * struct, because Ghidra splits it into two char fields, so it is
		 * relocated alongside shapeinfos - see tools/extract_shapeinfos.py.
		 * All six entries that use it point into shapedata84/_2 at +42. */
		var_2C = (int16_t*)shapeinfo_opp_ptr[var_6 - shapeinfos];
		goto loc_18E7D;
	}

loc_18E3C:
	/* les bx,var_6 ; mov ax,es:[bx+8]   (== si_cameraDataOffset) */
	var_2C = var_6->si_cameraDataOffset;

	/* mov al,var_10 ; sub ah,ah ; cx=ax ; ax*=2 ; ax+=cx ; ax*=2  -> var_10*6
	 * add ax,var_2C ; mov var_30,ax ; mov var_2E,dx */
	p = (const uint8_t*)var_2C + (uint16_t)var_10 * 6;
	/* add ax,6 ; lea di,[bp+var_C] ; mov si,ax ; movsw x3 */
	var_C = ldw(p, 6);
	var_A = ldw(p, 8);
	var_8 = ldw(p, 10);
	/* mov ax,var_30 ; jmp loc_18EAA   (ax is back at p, i.e. +0) */
	var_22 = ldw(p, 0);
	var_20 = ldw(p, 2);
	var_1E = ldw(p, 4);
	goto loc_18EBE;

loc_18E76:
	/* les bx,var_6 ; mov ax,es:[bx+si_cameraDataOffset] */
	var_2C = var_6->si_cameraDataOffset;
loc_18E7D:
	/* mov var_2C,ax ; mov var_2A,ds ; ax = var_10*6 + var_2C
	 * mov var_30,ax ; mov var_2E,dx ; lea di,[bp+var_C] ; movsw x3 */
	p = (const uint8_t*)var_2C + (uint16_t)var_10 * 6;
	var_C = ldw(p, 0);
	var_A = ldw(p, 2);
	var_8 = ldw(p, 4);
	/* add ax,6 ; loc_18EAA: lea di,[bp+var_22] ; mov si,ax ; movsw x3 */
	var_22 = ldw(p, 6);
	var_20 = ldw(p, 8);
	var_1E = ldw(p, 10);

/* loc_18EBE (the instruction after the second movsw block): */
loc_18EBE:
	/* les bx,var_6 ; mov ax,es:[bx+6]  (== si_arrowOrient) */
	ax = var_6->si_arrowOrient;
	if (ax == 0x100) goto loc_18F74;
	if (ax == 0x200) goto loc_18F52;
	if (ax != 0x300) goto loc_18EFA;

	/* --- arrowOrient == 300h: (x,z) -> (-z, x) on both triples --- */
	var_E = var_C;                                  /* mov var_E, var_C */
	var_C = (int16_t)(-(uint16_t)var_8);            /* mov ax,var_8 ; neg ax */
	var_8 = var_E;
	var_E = var_22;
	var_22 = (int16_t)(-(uint16_t)var_1E);
	ax = var_E;
loc_18EF7:
	var_1E = ax;

loc_18EFA:
	/* mov bx,[bp+arg_0] ; les si, td21_col_from_path ; mov al,es:[bx+si] */
	var_16 = (uint8_t)td21_col_from_path[arg_0];
	/* les si, td22_row_from_path ; mov al,es:[bx+si] ; mov byte var_1A, al */
	al = (uint8_t)td22_row_from_path[arg_0];
	var_1A = al;
	/* cmp var_A, 0FFFFh ; jz loc_18F3B */
	if (var_A == -1) goto loc_18F3B;
	/* mov bl,al ; sub bh,bh ; shl bx,1 ; mov bx, terrainrows[bx]
	 * mov al,var_16 ; sub ah,ah ; add bx,ax
	 * les si, td15_terr_map_main ; cmp byte ptr es:[bx+si], 6 ; jnz */
	bx = (uint16_t)terrainrows[al];
	bx = (uint16_t)(bx + (uint16_t)var_16);
	if (td15_terr_map_main[bx] != 6) goto loc_18F3B;
	/* mov ax, hillHeightConsts+2 ; add var_A,ax ; add var_20,ax */
	ax = hillHeightConsts[1];
	var_A  = (int16_t)((uint16_t)var_A + (uint16_t)ax);
	var_20 = (int16_t)((uint16_t)var_20 + (uint16_t)ax);

loc_18F3B:
	{
		int16_t si;
		/* mov bx,var_offsetTrkObject ; test byte ptr [bx+0Bh],1
		 * 0Bh == offsetof(struct TRACKOBJECT, ss_multiTileFlag) */
		if (var_offsetTrkObject->ss_multiTileFlag & 1) {
			/* mov bl,byte var_1A ; sub bh,bh ; shl bx,1 ; mov si, trackpos[bx] */
			si = trackpos[var_1A];
		} else {
/* loc_18F9C: */
			si = trackcenterpos[var_1A];
		}
/* loc_18FA7: */
		var_8  = (int16_t)((uint16_t)var_8  + (uint16_t)si);
		var_1E = (int16_t)((uint16_t)var_1E + (uint16_t)si);

		/* mov bx,var_offsetTrkObject ; test byte ptr [bx+0Bh],2 */
		if (var_offsetTrkObject->ss_multiTileFlag & 2) {
			/* mov si, (trackpos2+2)[bx]  -> trackpos2[var_16 + 1] */
			si = trackpos2[(uint16_t)var_16 + 1];
		} else {
/* loc_18FC4: */
			si = trackcenterpos2[var_16];
		}
/* loc_18FCF: */
		var_C  = (int16_t)((uint16_t)var_C  + (uint16_t)si);
		var_22 = (int16_t)((uint16_t)var_22 + (uint16_t)si);
	}

	/* mov ax,var_22 ; cwd ; mov cx,ax ; mov ax,var_C ; mov bx,dx ; cwd
	 * add ax,cx ; adc dx,bx ; sar dx,1 ; rcr ax,1
	 * -> 32-bit signed sum, arithmetic >>1, low word stored. */
	arg_2->x = (int16_t)(uint16_t)(uint32_t)
	           sar32((int32_t)var_C + (int32_t)var_22, 1);

	/* cmp var_A,0FFFFh ; jnz loc_18FFE */
	if (var_A == -1) {
		arg_2->y = -1;                              /* mov [bx+VECTOR.vy],0FFFFh */
	} else {
/* loc_18FFE: */
		arg_2->y = (int16_t)(uint16_t)(uint32_t)
		           sar32((int32_t)var_A + (int32_t)var_20, 1);
	}
/* loc_19018: */
	arg_2->z = (int16_t)(uint16_t)(uint32_t)
	           sar32((int32_t)var_8 + (int32_t)var_1E, 1);

	/* lea di,[bx+6] ; lea si,[bp+var_C] ; movsw x3 */
	*(int16_t*)(out + 0x06) = var_C;
	*(int16_t*)(out + 0x08) = var_A;
	*(int16_t*)(out + 0x0A) = var_8;
	/* lea di,[bx+0Ch] ; lea si,[bp+var_22] ; movsw x3 */
	*(int16_t*)(out + 0x0C) = var_22;
	*(int16_t*)(out + 0x0E) = var_20;
	*(int16_t*)(out + 0x10) = var_1E;
	/* mov ax,var_24 ; mov [bx+12h],ax */
	*(int16_t*)(out + 0x12) = var_24;

	/* mov al,[bp+arg_4] ; cbw          -> SIGN-extended low byte
	 * mov cl,var_18 ; sub ch,ch ; dec cx  -> ZERO-extended var_18, minus 1
	 * cmp cx,ax ; jnz loc_1906C */
	{
		int16_t cx = (int16_t)((uint16_t)var_18 - 1);
		ax = (int16_t)(int8_t)(uint8_t)(uint16_t)arg_4;
		if (cx != ax) goto loc_1906C;
		return 1;                                   /* mov ax,1 ; retf */
	}
loc_1906C:
	return 0;                                       /* sub ax,ax ; retf */

loc_18F52:
	/* --- arrowOrient == 200h: negate x and z of both triples --- */
	var_8  = (int16_t)(-(uint16_t)var_8);
	var_C  = (int16_t)(-(uint16_t)var_C);
	var_1E = (int16_t)(-(uint16_t)var_1E);
	var_22 = (int16_t)(-(uint16_t)var_22);
	goto loc_18EFA;

loc_18F74:
	/* --- arrowOrient == 100h: (x,z) -> (z, -x) on both triples --- */
	var_E = var_C;
	var_C = var_8;
	var_8 = (int16_t)(-(uint16_t)var_E);
	var_E = var_22;
	var_22 = var_1E;
	ax = (int16_t)(-(uint16_t)var_E);
	goto loc_18EF7;
}

/* ==================================================================== */
/* bto_auxiliary1                                                       */
/*                                                                      */
/* reference/restunts/src/restunts/asmorig/seg004.asm                   */
/* lines 2756 (`bto_auxiliary1 proc far`) .. 3182 (`bto_auxiliary1      */
/* endp`).  Bounds confirmed with grep; 426 lines inclusive.            */
/*                                                                      */
/* Stack frame verified against the original:                           */
/*     arg_0 = word ptr  6                                              */
/*     arg_2 = word ptr  8                                              */
/*     arg_4 = word ptr 10                                              */
/* Three word arguments in a far frame; the call site at               */
/* src/sim_faithful/stateply.c:3001 (seg004 original: `push &var_DC ;   */
/* push vec_FC.vz ; push vec_FC.vx ; call bto_auxiliary1 ; add sp,6`)   */
/* matches the declared prototype                                       */
/*     int16_t bto_auxiliary1(int16_t x, int16_t z, struct VECTOR* out) */
/* exactly - NO DISCREPANCY, other than that the caller only keeps the  */
/* LOW BYTE of the result (`mov [bp+var_EC], al` into a `char`), which  */
/* is already how stateply.c stores it.                                 */
/*                                                                      */
/* asmorig vs. restunts2: reference/restunts2/src/asm/seg004.asm        */
/* 2694..3109 carries the same routine as `bto_auxiliary1_asm_`.  A     */
/* full diff shows ONLY cosmetic differences - label naming             */
/* (loc_1F... vs LAB_1e1a_...), operand-size spelling, hex vs decimal   */
/* literals, `jnb` vs the synonymous `jnc`, `(trackpos+2)` vs           */
/* `(trackpos+1*2)`, the `; align 2` comment vs a bare `db 0x90`, the   */
/* two unused `s`/`r` frame pseudo-symbols present only in asmorig, and */
/* one orphan label (`loc_1FDE8`) that asmorig names and restunts2 does */
/* not.  No instruction, operand, constant or branch target differs.    */
/* asmorig is used here as instructed.                                  */
/*                                                                      */
/* Data tables: trackrows, terrainrows, trackpos, trackpos2,            */
/* trackcenterpos, trackcenterpos2, hillHeightConsts, trkObjectList,    */
/* td14_elem_map_main and td15_terr_map_main all already exist in the   */
/* port (rdata.c).  word_45D3E and unk_3E640..unk_3E69A do not - see    */
/* the [MISSING DEFINITION] notes above sub_18D60.                      */
/* ==================================================================== */
int16_t bto_auxiliary1(int16_t arg_0, int16_t arg_2, struct VECTOR* arg_4)
{
	/* word ptr -20.  loc_1FC34 writes only its LOW byte and then only tests
	 * that byte, so the high half is stack garbage in the original; zeroed
	 * here to keep the C well-defined without changing any read. */
	uint16_t var_14 = 0;
	uint8_t  var_10;                /* byte ptr -16 */
	int16_t  var_C;                 /* word ptr -12 */
	int16_t  var_A;                 /* word ptr -10 */
	int16_t  var_elemOrient;        /* word ptr  -8 */
	int16_t  var_6;                 /* word ptr  -6 */
	const uint8_t* var_elemDepOffset = 0; /* word ptr -4 */
	uint8_t  var_tileElem;          /* byte ptr  -2 */

	int16_t ax;
	uint16_t bx;
	int16_t di, si;
	char* out = (char*)arg_4;

	/* mov bx,[bp+arg_2] ; shl bx,1 ; mov bx, trackrows[bx]
	 * add bx,[bp+arg_0] ; add bx, td14_elem_map_main ; mov al, es:[bx] */
	var_tileElem = td14_elem_map_main[(uint16_t)trackrows[arg_2] + (uint16_t)arg_0];
	/* or al,al ; jnz loc_1FB12 */
	if (var_tileElem != 0) goto loc_1FB12;
loc_1FB0A:
	return 0;                                       /* sub ax,ax ; retf */

loc_1FB12:
	/* mov bx,[bp+arg_0] ; shl bx,1 ; mov ax, trackcenterpos2[bx] */
	var_6 = trackcenterpos2[arg_0];
	/* mov bx,[bp+arg_2] ; shl bx,1 ; mov ax, trackcenterpos[bx] */
	var_C = trackcenterpos[arg_2];
	/* cmp var_tileElem,0FDh ; jnb loc_1FB33 ; jmp loc_1FC34   (UNSIGNED) */
	if (var_tileElem < 0xFD) goto loc_1FC34;

/* loc_1FB33: */
	/* mov al,var_tileElem ; sub ah,ah ; cmp ax,... */
	if (var_tileElem == 0xFD) goto loc_1FB4E;
	if (var_tileElem == 0xFE) goto loc_1FBB4;
	if (var_tileElem == 0xFF) goto loc_1FBF2;
	goto loc_1FC86;                                 /* loc_1FB4A (unreachable) */

loc_1FB4E:
	/* mov ax,[bp+arg_2] ; shl ax,1 ; mov var_14,ax ; mov bx,ax
	 * mov bx, word_45D3E[bx]      ; "is really trackrows[bx -1]"
	 * add bx,[bp+arg_0] ; add bx, td14_elem_map_main ; mov al, es:[bx-1] */
	var_14 = (uint16_t)((uint16_t)arg_2 << 1);
	bx = (uint16_t)(arg_2 == 0 ? word_45D3E : trackrows[arg_2 - 1]);
	bx = (uint16_t)(bx + (uint16_t)arg_0);
	var_tileElem = td14_elem_map_main[(uint16_t)(bx - 1)];
	/* bx = tileElem*14 ; test trkObjectList.ss_multiTileFlag[bx],1 ; jz */
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 1)) goto loc_1FB8D;
	/* mov bx,var_14 ; mov ax,(trackpos+2)[bx]  -> trackpos[arg_2 + 1] */
	ax = trackpos[arg_2 + 1];
loc_1FB8A:
	var_C = ax;
loc_1FB8D:
	/* mov al,var_tileElem ; ... ; test ss_multiTileFlag[bx],2 ; jnz loc_1FBA8 */
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 2)) goto loc_1FC86;
/* loc_1FBA8: */
	/* mov bx,[bp+arg_0] ; shl bx,1 ; mov ax, trackpos2[bx]
	 * [ODDITY] this arm reads trackpos2[arg_0] while the structurally
	 * identical arm at the tail of loc_1FC62 reads (trackpos2+2)[arg_0],
	 * i.e. trackpos2[arg_0 + 1].  Both then fall into loc_1FC83.  The
	 * asymmetry is in the original and is reproduced. */
	ax = trackpos2[arg_0];
	goto loc_1FC83;

loc_1FBB4:
	var_14 = (uint16_t)((uint16_t)arg_2 << 1);
	bx = (uint16_t)(arg_2 == 0 ? word_45D3E : trackrows[arg_2 - 1]);
	bx = (uint16_t)(bx + (uint16_t)arg_0);
	var_tileElem = td14_elem_map_main[bx];          /* no -1 in this arm */
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 1)) goto loc_1FC62;
	ax = trackpos[arg_2 + 1];                       /* (trackpos+2)[var_14] */
	goto loc_1FC5F;

loc_1FBF2:
	var_14 = (uint16_t)((uint16_t)arg_2 << 1);
	bx = (uint16_t)trackrows[arg_2];                /* trackrows, not word_45D3E */
	bx = (uint16_t)(bx + (uint16_t)arg_0);
	var_tileElem = td14_elem_map_main[(uint16_t)(bx - 1)];
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 1)) goto loc_1FB8D;
/* loc_1FC2A: */
	ax = trackpos[arg_2];                           /* trackpos[var_14] */
	goto loc_1FB8A;

loc_1FC34:
	/* mov al, trkObjectList.ss_multiTileFlag[bx] ; mov byte ptr var_14, al
	 * cmp al,ah    (ah == 0 from the preceding `sub ah,ah`) ; jz loc_1FC86 */
	{
		uint8_t flg = (uint8_t)trkObjectList[var_tileElem].ss_multiTileFlag;
		var_14 = (uint16_t)((var_14 & 0xFF00) | flg);
		if (flg == 0) goto loc_1FC86;
		/* test byte ptr var_14,1 ; jz loc_1FC62 */
		if (!(var_14 & 1)) goto loc_1FC62;
	}
	/* mov bx,[bp+arg_2] ; shl bx,1 ; mov ax, trackpos[bx] */
	ax = trackpos[arg_2];
loc_1FC5F:
	var_C = ax;
loc_1FC62:
	if (!(trkObjectList[var_tileElem].ss_multiTileFlag & 2)) goto loc_1FC86;
	/* mov bx,[bp+arg_0] ; shl bx,1 ; mov ax,(trackpos2+2)[bx] */
	ax = trackpos2[arg_0 + 1];
loc_1FC83:
	var_6 = ax;

loc_1FC86:
	di = 0;                                         /* sub di,di */
	/* mov al, trkObjectList.ss_physicalModel[bx] ; cbw  (SIGN-extended) */
	ax = (int16_t)(int8_t)trkObjectList[var_tileElem].ss_physicalModel;
	if (ax == 0x20) goto loc_1FCC2;                 /* jz  */
	if (ax >  0x20) goto loc_1FCF4;                 /* jg  (SIGNED) */
	if (ax == 0x0B) goto loc_1FCE0;
	if (ax == 0x12) goto loc_1FCEA;
	goto loc_1FCBA;

loc_1FCB2:
	di = 2; var_elemDepOffset = unk_3E676;
	/* falls through */
loc_1FCBA:
	/* or di,di ; jnz loc_1FD14 ; jmp loc_1FB0A */
	if (di == 0) goto loc_1FB0A;
	goto loc_1FD14;

loc_1FCC2:
	di = 2; var_elemDepOffset = unk_3E682; goto loc_1FCBA;
loc_1FCCC:
	di = 2; var_elemDepOffset = unk_3E68E; goto loc_1FCBA;
loc_1FCD6:
	di = 4; var_elemDepOffset = unk_3E69A; goto loc_1FCBA;
loc_1FCE0:
	di = 1; var_elemDepOffset = unk_3E640; goto loc_1FCBA;
loc_1FCEA:
	di = 8; var_elemDepOffset = unk_3E646; goto loc_1FCBA;

loc_1FCF4:
	if (ax == 0x22) goto loc_1FCD6;
	if (ax >  0x22) goto loc_1FD02;                 /* jg (SIGNED) */
	if (ax == 0x21) goto loc_1FCCC;
	goto loc_1FCBA;
loc_1FD02:
	if (ax == 0x23) goto loc_1FCB2;
	if (ax <  0x47) goto loc_1FCBA;                 /* jl  (SIGNED) */
	if (ax <= 0x4A) goto loc_1FCE0;                 /* jle (SIGNED) */
	goto loc_1FCBA;

loc_1FD14:
	/* mov bx,[bp+arg_2] ; shl bx,1 ; mov bx, terrainrows[bx]
	 * add bx,[bp+arg_0] ; add bx, td15_terr_map_main ; mov al,es:[bx]
	 * [ODDITY] the element map at the top of this proc is indexed through
	 * trackrows[] while the terrain map here is indexed through
	 * terrainrows[] - the opposite pairing from the one build_track_object
	 * uses (src/render_faithful/rframe_helpers.c:835/851).  Reproduced. */
	var_10 = td15_terr_map_main[(uint16_t)terrainrows[arg_2] + (uint16_t)arg_0];
	if (var_10 == 6)
		var_A = hillHeightConsts[1];                /* mov ax, hillHeightConsts+2 */
	else
		var_A = 0;                                  /* loc_1FD3A */
/* loc_1FD3F: */
	/* mov ax, trkObjectList.ss_rotY[bx] ; mov var_elemOrient,ax */
	var_elemOrient = trkObjectList[var_tileElem].ss_rotY;
	si = 0;                                         /* sub si,si */
	goto loc_1FDA6;

loc_1FD5C:
	/* ax = si*6 ; var_14 = ax ; bx = var_elemDepOffset + ax */
	var_14 = (uint16_t)((uint16_t)((uint16_t)si << 1) + (uint16_t)si);
	var_14 = (uint16_t)(var_14 << 1);
	ax = (int16_t)((uint16_t)ldw(var_elemDepOffset, (int)var_14) + (uint16_t)var_6);
	*(int16_t*)(out + var_14) = ax;
	ax = (int16_t)((uint16_t)ldw(var_elemDepOffset, (int)var_14 + 2) + (uint16_t)var_A);
	*(int16_t*)(out + var_14 + 2) = ax;
	ax = ldw(var_elemDepOffset, (int)var_14 + 4);
loc_1FD99:
	ax = (int16_t)((uint16_t)ax + (uint16_t)var_C);
	*(int16_t*)(out + var_14 + 4) = ax;
loc_1FDA5:
	si++;                                           /* inc si */
loc_1FDA6:
	/* cmp si,di ; jl loc_1FDAD ; jmp loc_1FE8C */
	if (si >= di) goto loc_1FE8C;                   /* SIGNED */
/* loc_1FDAD: */
	ax = var_elemOrient;
	if (ax == 0) goto loc_1FD5C;
	if (ax == 0x100) goto loc_1FE4A;
	if (ax == 0x200) goto loc_1FE08;
	if (ax != 0x300) goto loc_1FDA5;

	/* --- elemOrient == 300h --- */
	var_14 = (uint16_t)((uint16_t)((uint16_t)si << 1) + (uint16_t)si);
	var_14 = (uint16_t)(var_14 << 1);
	ax = (int16_t)(-(uint16_t)ldw(var_elemDepOffset, (int)var_14 + 4));
	ax = (int16_t)((uint16_t)ax + (uint16_t)var_6);
	*(int16_t*)(out + var_14) = ax;
/* loc_1FDE8: */
	ax = (int16_t)((uint16_t)ldw(var_elemDepOffset, (int)var_14 + 2) + (uint16_t)var_A);
	*(int16_t*)(out + var_14 + 2) = ax;
	ax = ldw(var_elemDepOffset, (int)var_14);
	goto loc_1FD99;

loc_1FE08:
	/* --- elemOrient == 200h --- */
	var_14 = (uint16_t)((uint16_t)((uint16_t)si << 1) + (uint16_t)si);
	var_14 = (uint16_t)(var_14 << 1);
	ax = (int16_t)(-(uint16_t)ldw(var_elemDepOffset, (int)var_14));
	ax = (int16_t)((uint16_t)ax + (uint16_t)var_6);
	*(int16_t*)(out + var_14) = ax;
	ax = (int16_t)((uint16_t)ldw(var_elemDepOffset, (int)var_14 + 2) + (uint16_t)var_A);
	*(int16_t*)(out + var_14 + 2) = ax;
	ax = ldw(var_elemDepOffset, (int)var_14 + 4);
	goto loc_1FE87;

loc_1FE4A:
	/* --- elemOrient == 100h --- */
	var_14 = (uint16_t)((uint16_t)((uint16_t)si << 1) + (uint16_t)si);
	var_14 = (uint16_t)(var_14 << 1);
	ax = (int16_t)((uint16_t)ldw(var_elemDepOffset, (int)var_14 + 4) + (uint16_t)var_6);
	*(int16_t*)(out + var_14) = ax;
	ax = (int16_t)((uint16_t)ldw(var_elemDepOffset, (int)var_14 + 2) + (uint16_t)var_A);
	*(int16_t*)(out + var_14 + 2) = ax;
	ax = ldw(var_elemDepOffset, (int)var_14);
loc_1FE87:
	ax = (int16_t)(-(uint16_t)ax);                  /* neg ax */
	goto loc_1FD99;

loc_1FE8C:
	return di;                                      /* mov ax,di ; retf */
}

/* ==================================================================== */
/* detect_penalty                                                       */
/*                                                                      */
/* reference/restunts2/src/asm/seg001.asm lines 5267 (`detect_penalty_  */
/* asm_ proc far`) .. 5538 (`endp`); 223 instructions, no calls.        */
/*                                                                      */
/* The name undersells it. This is the routine that walks the track     */
/* graph from the car's last known piece to find which piece it is on   */
/* now, and it maintains state.game_startcol/startcol2/startrow/        */
/* startrow2 as it goes. state.c only advances state.field_2F2 - the    */
/* car's progress along the track - through this function's `unk` out-  */
/* parameter, so while it was stubbed the car never moved forward       */
/* through the element list and every downstream lookup (sub_18D60      */
/* included) was fed the same stale index.                              */
/*                                                                      */
/* Signature verified against the one caller (state.c:54):              */
/*     int16_t detect_penalty(int16_t* unk, int16_t* penalty_counter)   */
/* `unk` is both in and out: it carries the piece index in and the      */
/* newly found one back out.                                            */
/*                                                                      */
/* Stack frame, from the proc header (`sub sp, 0x5a4`):                 */
/*     bp-1444 var_5A4   scratch word for the cbw comparisons           */
/*     bp-1442 var_5A2   visited[] flags, one byte per track piece      */
/*     bp-538  var_21A   walk stack depth                               */
/*     bp-536  var_218   row2 of the piece under test                   */
/*     bp-534           stkPiece[]  (the `[bx+0FDEAh]` array)           */
/*     bp-278  var_116   ss_multiTileFlag of the piece under test       */
/*     bp-276  var_114   col2 of the piece under test                   */
/*     bp-272  var_110   best piece found so far                        */
/*     bp-270           stkDepth[]  (the `[bx+0FEF2h]` array)           */
/*     bp-14   var_E     row      bp-12 var_C  col                      */
/*     bp-8    var_8     bp-6 var_6 best depth                          */
/*     bp-4    var_4     car tile column   bp-2 var_2  current piece    */
/* The two stack arrays are 128 entries each, which is what the gaps to */
/* the next declared local allow.                                       */
/*                                                                      */
/* [DEVIATION] The original clears visited[] only for indices below     */
/* `track_pieces_counter`, a dseg word that track_setup() fills and     */
/* that this port does not have. All 0x385 entries are cleared instead. */
/* Every index the walk uses comes out of td01/td02 and is therefore    */
/* below the counter, so the extra clearing cannot be observed; above   */
/* the counter the original would be reading uninitialised stack.       */
/* ==================================================================== */
int16_t detect_penalty(int16_t* unk, int16_t* penalty_counter)
{
	uint8_t  visited[0x385];   /* var_5A2 */
	int16_t  stkPiece[128];
	int16_t  stkDepth[128];
	int16_t  var_5A4, var_21A, var_110, var_8, var_6, var_2;
	uint8_t  var_218, var_116, var_114, var_E, var_C;
	int8_t   var_4, var_A;
	int16_t  si, di;

	/* mov al, byte ptr [car_posWorld1.lx+2]  - bits 16..23 of the long,
	 * i.e. the tile column, since one tile is 0x10000 posWorld units. */
	var_4 = (int8_t)(state.playerstate.car_posWorld1.lx >> 16);
	/* mov al,1Dh ; sub al, byte ptr [lz+2]   - 8-bit, then used via cbw */
	var_A = (int8_t)(0x1D - (uint8_t)(state.playerstate.car_posWorld1.lz >> 16));

	var_5A4 = (int16_t)var_4;                       /* cbw */
	if (var_5A4 == state.game_startcol)  goto L3138;
	if (var_5A4 != state.game_startcol2) goto L3162;
L3138:
	var_5A4 = (int16_t)var_A;                       /* cbw */
	if (var_5A4 == state.game_startrow)  goto L3152;
	if (var_5A4 != state.game_startrow2) goto L3162;
L3152:
	/* Still on the tile we were on: nothing to report. */
	*penalty_counter = 0;
	return 0;

L3162:
	if (var_4 < 0)    goto L321E;
	if (var_4 > 0x1D) goto L321E;
	if (var_A < 0)    goto L321E;
	if (var_A > 0x1D) goto L321E;

	var_6 = 0;
	var_21A = 0;
	di = 0;
	/* [DEVIATION] see header: original bounds this by track_pieces_counter */
	for (si = 0; si < 0x385; si++)
		visited[si] = 0;
	si = *unk;

L31A9:
	var_2 = td01_track_file_cpy[si];
	if (visited[var_2] == 0) goto L3228;
	/* Already been here - pop the walk stack and carry on. */
	if (var_21A == 0) goto L31EA;
	var_21A--;
	si = stkPiece[var_21A];
	di = stkDepth[var_21A];
	goto L31A9;

L31EA:
	if (var_6 == 0) goto L320A;
	*unk = var_110;
	*penalty_counter = var_6;
L3201:
	return 1;

L320A:
	state.game_startcol  = (int16_t)var_4;
	state.game_startcol2 = (int16_t)var_4;
	state.game_startrow  = (int16_t)var_A;
	state.game_startrow2 = (int16_t)var_A;
L321E:
	*penalty_counter = -2;
	goto L3201;

L3228:
	visited[var_2] = 1;
	var_E = (uint8_t)td22_row_from_path[var_2];
	var_116 = (uint8_t)trkObjectList[(uint8_t)td17_trk_elem_ordered[var_2]]
	                  .ss_multiTileFlag;
	/* bit 0 = two tiles vertically, bit 1 = two tiles horizontally, so the
	 * piece also covers row+1 / col+1. */
	var_218 = (var_116 & 1) ? (uint8_t)(var_E + 1) : var_E;
	var_C = (uint8_t)td21_col_from_path[var_2];
	var_114 = (var_116 & 2) ? (uint8_t)(var_C + 1) : var_C;

	if (var_C   == (uint8_t)var_4) goto L32AF;
	if (var_114 != (uint8_t)var_4) goto L3309;
L32AF:
	if (var_E   == (uint8_t)var_A) goto L32BD;
	if (var_218 != (uint8_t)var_A) goto L3309;
L32BD:
	/* The car is on this piece. */
	if (td02_penalty_related[si] != -1)
		var_2 = si;
	state.game_startcol  = (int16_t)(int8_t)var_C;
	state.game_startcol2 = (int16_t)(int8_t)var_114;
	state.game_startrow  = (int16_t)(int8_t)var_E;
	state.game_startrow2 = (int16_t)(int8_t)var_218;
	if (di <= 0) goto L334E;
	if (var_6 == 0)  goto L32FF;
	if (var_6 <= di) goto L3309;
L32FF:
	var_110 = var_2;
	var_6 = di;

L3309:
	/* Branch piece: remember it and come back to it later. */
	var_8 = td02_penalty_related[si];
	if (var_8 == -1) goto L333F;
	stkDepth[var_21A] = di;
	stkPiece[var_21A] = var_8;
	var_21A++;
L333F:
	if (var_2 == 0) goto L335E;
	if (di == -1)   goto L3361;
	di++;
	goto L3361;

L334E:
	*unk = var_2;
	*penalty_counter = di;
	goto L3201;

L335E:
	di = -1;
L3361:
	si = var_2;
	goto L31A9;
}

/* ==================================================================== */
/* PART 2 (2026-08-17): the per-frame driver the port used to bypass.    */
/*                                                                      */
/* Until today main_native.c and tools/dump_native_states.c called       */
/* player_op() and opponent_op() straight from their own frame loops.    */
/* The original never does that: both are called from                    */
/* update_gamestate(), which also runs the helicopter cameras, the       */
/* rewind checkpoints, the frame-rate autotune and the crash-debris      */
/* animator, in one fixed order.  Skipping it is why the helicopter      */
/* camera never moved and why state.game_vec1/3/4 differed from the DOS  */
/* oracle from frame 1 of every recording.                               */
/*                                                                      */
/*   ported_get_kevinrandom_seed_  seg002.asm  206..231                  */
/*   ported_get_kevinrandom_       seg002.asm  232..267                  */
/*   move_helicopters (sub_2298C)  seg005.asm 1552..1923                 */
/*   ported_update_gamestate_      seg001.asm 4395..4603                 */
/*   sub_19BA0                     seg001.asm 9386..9507                 */
/*   init_game_state (fragment)    seg001.asm 3885..4021                 */
/*                                                                      */
/* `sub_2298C` is the restunts1 name; restunts2's Ghidra export calls it */
/* `move_helicopters`, which is what it is: state.game_vec1[i] is the    */
/* helicopter that carries camera mode 1 for car i, and state.field_3F7  */
/* [i] is the trackside camera (mode 2/3) nearest that car.  frame.c     */
/* :194 and :214 read exactly those two.                                 */
/* ==================================================================== */

/* All of these already have declarations in externs.h (included above);
 * they are named here only to document what this part depends on. */

/* Defined below / in sfdata.c. */
int16_t word_45A00;                 /* dseg 0x45A00: framespersec * 30   */
/* dseg 0x4499C: 100 / framespersec, the original's 100 Hz tick budget.
 * Declared in externs.h, used by rintro3d.c, and defined nowhere until
 * init_game_state needed it - the fifth such gap this session. */
int16_t word_4499C = 5;
struct GAMESTATE far* cvxptr;       /* dseg: the 20-slot "cvx" rewind ring */
uint8_t g_kevinrandom_seed[6];      /* dseg 0x45942, six zero bytes       */

/* Installed by src/audio_native.c; NULL in bin/dump_native_states, which
 * does not link SDL.  Stands in for the `call audio_carstate` at
 * seg001.asm:4494 and :4500 (see sfstubs.c for the same pattern). */
void (*sim_hook_audio_frame)(void);

/* -------------------------------------------------------------------- *
 * ported_get_kevinrandom_seed_ - seg002.asm 206..231                    *
 * Copies the six-byte lagged-Fibonacci state into the caller's buffer;  *
 * update_gamestate parks it in state.kevinseed so a rewind can restore  *
 * the exact random stream.                                              *
 * -------------------------------------------------------------------- */
void get_kevinrandom_seed(char* arg_0)
{
	arg_0[0] = (char)g_kevinrandom_seed[0];
	arg_0[1] = (char)g_kevinrandom_seed[1];
	arg_0[2] = (char)g_kevinrandom_seed[2];
	arg_0[3] = (char)g_kevinrandom_seed[3];
	arg_0[4] = (char)g_kevinrandom_seed[4];
	arg_0[5] = (char)g_kevinrandom_seed[5];   /* loc_19E72 */
}

/* -------------------------------------------------------------------- *
 * ported_get_kevinrandom_ - seg002.asm 232..267                         *
 *                                                                       *
 * Returns g_kevinrandom_seed[0] (zero-extended) after folding the six    *
 * bytes downwards and incrementing them as a big-endian counter.  Note   *
 * the fold runs BEFORE the increment and the returned byte is the one    *
 * the fold just wrote.                                                   *
 * -------------------------------------------------------------------- */
int16_t get_kevinrandom(void)
{
	uint8_t al;

	al = (uint8_t)(g_kevinrandom_seed[5]);
	al = (uint8_t)(al + g_kevinrandom_seed[4]); g_kevinrandom_seed[4] = al;
	al = (uint8_t)(al + g_kevinrandom_seed[3]); g_kevinrandom_seed[3] = al;
	al = (uint8_t)(al + g_kevinrandom_seed[2]); g_kevinrandom_seed[2] = al;
	al = (uint8_t)(al + g_kevinrandom_seed[1]); g_kevinrandom_seed[1] = al;
	al = (uint8_t)(al + g_kevinrandom_seed[0]); g_kevinrandom_seed[0] = al;

	if (++g_kevinrandom_seed[5] == 0)               /* inc ; jnz loc_19EC3 */
	if (++g_kevinrandom_seed[4] == 0)
	if (++g_kevinrandom_seed[3] == 0)
	if (++g_kevinrandom_seed[2] == 0)
	if (++g_kevinrandom_seed[1] == 0)
	     ++g_kevinrandom_seed[0];
/* loc_19EC3: */
	return (int16_t)(uint16_t)g_kevinrandom_seed[0];   /* mov al,.. ; xor ah,ah */
}

/* -------------------------------------------------------------------- *
 * move_helicopters (sub_2298C) - seg005.asm 1552..1923                  *
 *                                                                       *
 * One pass per car (1, or 2 when there is an opponent).  For car i:      *
 *                                                                       *
 *  - game_vec3[i] <- game_vec1[i]   (last frame's position; game_vec3    *
 *    and game_vec4 are one two-element array in DOS, which is why the    *
 *    port indexes (&state.game_vec3)[i]).                                *
 *  - the target is the car's look-ahead point car_vec_unk3, except when  *
 *    the car is crashed / off the racing line / outside the field_48      *
 *    window, when it becomes the car's own position.                      *
 *  - height chases (car_y >> 6) + 0x10E at up to 30 units per frame.      *
 *  - horizontally it closes on the target at up to 0x78 units per frame   *
 *    (0xF0 at 10 fps), but never inside a radius of 0x1C2.                *
 *  - every framespersec/2 frames, state.field_3F7[i] is re-pointed at     *
 *    the nearest of the byte_4616E trackside cameras in trackdata9.       *
 * -------------------------------------------------------------------- */
void move_helicopters(void)
{
	int16_t far* var_34;           /* les bx,[bp+var_34]: trackdata9 + n*6 */
	int16_t var_30, var_2E, var_2C, var_2A, var_28, var_26;
	struct CARSTATE* var_opponentstateptr;
	int16_t var_22, var_20, var_1E;
	struct VECTOR var_1C;          /* var_1C = .x, var_1A = .y, var_18 = .z */
	int16_t var_14, var_10, var_E, var_C;
	int8_t  var_A;
	int16_t var_8;
	struct VECTOR var_6;           /* var_6 = .x, var_4 = .y, var_2 = .z */
	int16_t si, di, ax;
	int32_t v32;

	var_2A = 1;
	if (gameconfig.game_opponenttype != 0)
		var_2A = 2;
/* loc_229A5: */
	si = 0;
	goto loc_22C53;

loc_229AA:
	var_opponentstateptr = &state.opponentstate;
loc_229AF:
	/* three 32-bit `sar dx,1 / rcr ax,1` x6 chains, low word kept */
	var_1C.y = (int16_t)sar32(var_opponentstateptr->car_posWorld1.ly, 6);
	var_1C.x = (int16_t)sar32(var_opponentstateptr->car_posWorld1.lx, 6);
	var_1C.z = (int16_t)sar32(var_opponentstateptr->car_posWorld1.lz, 6);
	var_6 = var_opponentstateptr->car_vec_unk3;      /* movsw x3 */
	var_E = var_opponentstateptr->field_48;

	if (si != 0) goto loc_22A1E;
	if (state.field_45B != 0) goto loc_22A40;
	if (state.field_45C != 0) goto loc_22A40;
loc_22A1E:
	if (var_opponentstateptr->field_B6 != 0) goto loc_22A40;
	if (var_opponentstateptr->car_crashBmpFlag != 0) goto loc_22A40;
	if ((uint16_t)var_opponentstateptr->car_trackdata3_index == 0xFFFF)
		goto loc_22A40;
	if (var_E <= 0x80)  goto loc_22A4D;              /* jle */
	if (var_E >= 0x380) goto loc_22A4D;              /* jge */
loc_22A40:
	var_6 = var_1C;                                  /* movsw x3 from &var_1C */
loc_22A4D:
	var_22 = 0x1C2;
	var_14 = (int16_t)(var_1C.y + 0x10E);
	ax = (int16_t)(state.game_vec1[si].y - var_14);
	var_C = ax;
	if (ax != 0) {                                   /* or ax,ax ; jz loc_22A96 */
		di = ax;
		if (di > 0x1E) di = 0x1E;                /* loc_22A80 path */
		else if (di < (int16_t)0xFFE2) di = (int16_t)0xFFE2;   /* -30 */
		state.game_vec1[si].y = (int16_t)(state.game_vec1[si].y - di);
	}
loc_22A96:
	var_2E = (int16_t)(si * 6);
	var_10 = polarAngle((int16_t)(var_6.x - state.game_vec1[si].x),
	                    (int16_t)(var_6.z - state.game_vec1[si].z));
	var_30 = (int16_t)(si * 6);
	di = polarRadius2D((int16_t)(var_1C.x - state.game_vec1[si].x),
	                   (int16_t)(var_1C.z - state.game_vec1[si].z));
	if (var_22 >= di) goto loc_22B53;                /* jge */
	di = (int16_t)(di - var_22);
	if (framespersec == 0x14) {
		if (di > 0x78) di = 0x78;
	} else {
		if (di > 0xF0) di = 0xF0;
	}
/* loc_22B0D: */
	state.game_vec1[si].x = (int16_t)(state.game_vec1[si].x
	                       + multiply_and_scale(di, sin_fast((uint16_t)var_10)));
	state.game_vec1[si].z = (int16_t)(state.game_vec1[si].z
	                       + multiply_and_scale(di, cos_fast((uint16_t)var_10)));

loc_22B53:
	/* `mov cx,framespersec ; sar cx,1 ; div cx` - UNSIGNED divide of an
	 * unsigned game_frame by the signed-shifted half frame rate. */
	if ((uint16_t)state.game_frame % (uint16_t)sar16((int16_t)framespersec, 1) != 0)
		goto loc_22C52;
/* loc_22B67: */
	var_2C = 0x2710;
	var_A = 0;
	goto loc_22BE3;

loc_22B74:
	v32 = (int32_t)(((uint32_t)(uint16_t)var_20)
	              | ((uint32_t)(uint16_t)var_1E << 16));
	goto loc_22B7A;
loc_22B7A:
	if (v32 >= (int32_t)var_2C) goto loc_22BE0;
/* loc_22B8C: */
	{
		uint32_t u = ((uint32_t)(uint16_t)var_28)
		           | ((uint32_t)(uint16_t)var_26 << 16);
		if (var_26 < 0) u = 0u - u;              /* neg ax ; adc dx,0 ; neg dx */
		v32 = (int32_t)u;                        /* jge loc_22BA2 */
	}
/* loc_22BA8: */
	if (v32 >= (int32_t)var_2C) goto loc_22BE0;
/* loc_22BBA: */
	var_8 = polarRadius2D(var_20, var_28);
	if (var_8 >= var_2C) goto loc_22BE0;
	state.field_3F7[si] = (char)var_A;
	var_2C = var_8;
loc_22BE0:
	var_A++;
loc_22BE3:
	if (var_A >= (int8_t)byte_4616E) goto loc_22C52; /* jge, 8-bit signed */
	var_34 = trackdata9 + (int16_t)var_A * 3;        /* *6 bytes */
	/* (int32)trackdata9[n].x - (int32)var_1C.x, kept as var_20:var_1E */
	{
		int32_t d = (int32_t)var_34[0] - (int32_t)var_1C.x;
		var_20 = (int16_t)(uint16_t)(uint32_t)d;
		var_1E = (int16_t)(uint16_t)((uint32_t)d >> 16);
		d = (int32_t)var_34[2] - (int32_t)var_1C.z;
		var_28 = (int16_t)(uint16_t)(uint32_t)d;
		var_26 = (int16_t)(uint16_t)((uint32_t)d >> 16);
	}
	if (var_1E < 0) goto loc_22C41;
	goto loc_22B74;
loc_22C41:
	v32 = (int32_t)(0u - (((uint32_t)(uint16_t)var_20)
	                    | ((uint32_t)(uint16_t)var_1E << 16)));
	goto loc_22B7A;

loc_22C52:
	si++;
loc_22C53:
	if (var_2A <= si) goto loc_22C8C;                /* jle */
	var_2E = (int16_t)(si * 6);
	(&state.game_vec3)[si] = state.game_vec1[si];    /* movsw x3 */
	if (si != 0) goto loc_229AA;
/* loc_22C84: */
	var_opponentstateptr = &state.playerstate;
	goto loc_229AF;

loc_22C8C:
	(void)var_C; (void)var_2E; (void)var_30;
	return;
}

/* -------------------------------------------------------------------- *
 * sub_19BA0 - seg001.asm 9386..9507                                     *
 *                                                                       *
 * The crash-debris animator: 24 splinters whose positions live in        *
 * state.game_longs1/2/3 and whose spins live in field_2FE/32E/35E.       *
 * state_op_unk (seg001.asm 9215..9385, sfstubs.c) is what sets           *
 * state.field_42A to 1 and seeds them; this steps them and clears the    *
 * flag once every splinter has come to rest.                            *
 * -------------------------------------------------------------------- */
void sub_19BA0(void)
{
	struct VECTOR var_12;          /* var_12 = .x, var_10 = .y, var_E = .z */
	struct VECTOR var_C;           /* var_C  = .x, var_A  = .y, var_8 = .z */
	int16_t* var_14;
	struct MATRIX* var_4;
	int8_t var_2;
	int16_t si, di, bx;
	int32_t v;
	/* field_3BE is char[48] in externs.h but the original addresses it as
	 * 24 words (`shl ax,1 ; add offset state.field_3BE ; sub [bx],13h`). */
	int16_t* field_3BE_w = (int16_t*)state.field_3BE;

	var_2 = 0;
	si = 0;
	goto loc_19BC3;

loc_19BB0:
	var_2 = 1;
	di = (int16_t)(si * 2);
	state.field_2FE[si] = (int16_t)(state.field_2FE[si] + 0x10);
	state.field_32E[si] = (int16_t)(state.field_32E[si] + 0x10);
loc_19BC2:
	si++;
loc_19BC3:
	if (si >= 0x18) goto loc_19C96;
/* loc_19BCB: */
	di = (int16_t)(si * 2);
	if (state.field_38E[si] == 0) goto loc_19BC2;
/* loc_19BDA: */
	var_4 = mat_rot_zxy(0, 0, state.field_35E[si], 1);
	var_C.x = 0;
	var_C.y = 0;
	bx = si;
/* loc_19BF9 / loc_19BFB: */
	var_C.z = state.field_38E[si];
	mat_mul_vector(&var_C, var_4, &var_12);
	di = (int16_t)(si * 4);
	state.game_longs1[si] += (int32_t)var_12.x;      /* add/adc: cwd of ax */
	state.game_longs3[si] += (int32_t)var_12.z;
	var_14 = &field_3BE_w[si];
	*var_14 = (int16_t)(*var_14 - 0x13);
	state.game_longs2[si] += (int32_t)*var_14;
	if (framespersec == 0x0A) {
		*var_14 = (int16_t)(*var_14 - 0x13);
		state.game_longs2[si] += (int32_t)*var_14;
	}
loc_19C6B:
	v = state.game_longs2[si] + state.playerstate.car_posWorld1.ly;
	if (v < 0) goto loc_19C88;                       /* or dx,dx ; jl */
	goto loc_19BB0;
loc_19C88:
	state.field_38E[si] = 0;
/* loc_19C92: */
	goto loc_19BC2;

loc_19C96:
	state.field_42A = (char)var_2;
	(void)di; (void)bx;
}

/* -------------------------------------------------------------------- *
 * ported_update_gamestate_ - seg001.asm 4395..4603                      *
 *                                                                       *
 * The per-frame driver.  Three things it does that the port's own frame *
 * loops did not:                                                        *
 *   1. it reads the recorded input byte itself, out of td16_rpl_buffer   *
 *      at the CURRENT game_frame, and only then increments game_frame -  *
 *      so player_op runs with game_frame already advanced;               *
 *   2. it snapshots the whole 1120-byte GAMESTATE into the 20-slot "cvx" *
 *      ring every word_45A00 (= 30*fps) frames, which is what the rewind *
 *      in loop_game rewinds to;                                          *
 *   3. it runs move_helicopters, sub_19BA0 and audio_carstate after      *
 *      player_op/opponent_op, in that order.                             *
 *                                                                       *
 * The tail (game_replay_mode == 1 with no input) is the attract-mode     *
 * autopilot: it noses the car back towards the start tile and stops.     *
 * -------------------------------------------------------------------- */
void update_gamestate(void)
{
	int8_t var_carInputByte;
	int16_t ax, bx, cx, si;
	int32_t l;

	bx = state.game_frame;
	var_carInputByte = (int8_t)td16_rpl_buffer[bx];
	if (var_carInputByte != 0)                       /* or al,al ; jz */
		state.game_inputmode = 1;
loc_17027:
	if ((uint16_t)bx % (uint16_t)word_45A00 == 0) {  /* div ; or dx,dx ; jnz */
		si = (int16_t)((uint16_t)bx / (uint16_t)word_45A00);
		get_kevinrandom_seed(state.kevinseed);
		/* __aFlmul(si, 0x460) added to the far cvxptr, then
		 * `repne movsw` of 0x230 words = the whole GAMESTATE. */
		memcpy((uint8_t*)cvxptr + (int32_t)si * 0x460,
		       &state, 0x230 * 2);
	}
loc_17079:
	state.game_frame++;
	if (state.game_3F6autoLoadEvalFlag == 0) goto loc_170BE;
	ax = state.game_frames_per_sec;
	if (state.game_frame_in_sec >= ax) goto loc_170BE;    /* jge */
	state.game_frame_in_sec++;
	if (state.game_frame_in_sec != ax) goto loc_170BE;    /* jnz */
	if (byte_449DA != 0) goto loc_170BE;
	if (state.playerstate.car_crashBmpFlag != 1) goto loc_170B2;
	if (state.playerstate.car_speed2 == 0) goto loc_170B2;
	state.game_frames_per_sec++;
	goto loc_170BE;
loc_170B2:
	if (game_replay_mode != 0) goto loc_170BE;
	byte_449DA = 1;

loc_170BE:
	if (state.game_inputmode == 0) goto loc_170F6;
	player_op((char)var_carInputByte);               /* cbw ; push ax */
	if (gameconfig.game_opponenttype != 0) opponent_op();
/* loc_170DC: */
	move_helicopters();                              /* call sub_2298C */
	if (state.field_42A != 0) sub_19BA0();
/* loc_170EC: */
	if (sim_hook_audio_frame) sim_hook_audio_frame();   /* call audio_carstate */
	return;

loc_170F6:
	if (game_replay_mode != 1) goto loc_171E1;
/* loc_17100: */
	if (sim_hook_audio_frame) sim_hook_audio_frame();   /* call audio_carstate */
	if (byte_4393C == 0) goto loc_171E1;
loc_1710E:
	if (word_44DCA < 0x1C2) word_44DCA = (int16_t)(word_44DCA + 8);
loc_1711B:
	if (byte_4393C == 1 && word_44DCA > 0x180) byte_4393C++;
loc_1712E:
	if (byte_4393C != 2) goto loc_171E1;
loc_17138:
	/* si = mas(cos(track_angle), trackcenterpos[startrow2] - (z>>6))
	 *    + mas(sin(track_angle), trackcenterpos2[startcol2] - (x>>6)) */
	ax = (int16_t)(trackcenterpos[(int16_t)startrow2]
	     - (int16_t)sar32(state.playerstate.car_posWorld1.lz, 6));
	si = multiply_and_scale(cos_fast((uint16_t)track_angle), ax);
	ax = (int16_t)(trackcenterpos2[(int16_t)startcol2]
	     - (int16_t)sar32(state.playerstate.car_posWorld1.lx, 6));
	si = (int16_t)(si + multiply_and_scale(sin_fast((uint16_t)track_angle), ax));
	if (si <= 0xE4) goto loc_171D0;                  /* jle */
	if ((uint16_t)state.playerstate.car_speed >= 0x500) goto loc_171CC;  /* jnb */
	ax = 1;
loc_171BD:
	player_op((char)ax);
	return;
loc_171CC:
	ax = 0;
	goto loc_171BD;
loc_171D0:
	if (state.playerstate.car_speed != 0) { ax = 2; goto loc_171BD; }
/* loc_171DC: */
	byte_4393C = 0;
loc_171E1:
	(void)cx; (void)l;
	return;
}

/* -------------------------------------------------------------------- *
 * init_game_state, the part the port never had - seg001.asm 3885..4021  *
 *                                                                       *
 * The port's own start-up code (main_native.c game_init and             *
 * tools/dump_native_states.c) reproduces init_game_state's *car*        *
 * placement but not the block below, so state.field_3F4,                *
 * state.game_frames_per_sec and all four helicopter vectors stayed 0.   *
 * Those are exactly the GAMESTATE bytes that differed from the DOS      *
 * oracle at frame 1 of every recording.                                 *
 *                                                                       *
 * The helicopter starts 0x1000 units behind the start tile's centre     *
 * (track_angle + 0x200 is "backwards"), 0x200 to the right of it        *
 * (+0x300), and 0x3C0 above the hill height.                            *
 * -------------------------------------------------------------------- */
void init_game_state_vars(void)
{
	int16_t si = 0;
	int16_t ax, cx, var_A;
	int16_t di;

	state.field_3F4 = 1;
	state.game_frames_per_sec = 1;
	state.game_inputmode = (char)si;
	state.game_3F6autoLoadEvalFlag = (char)si;
	state.game_frame_in_sec = si;
	state.field_2F4 = si;
	state.field_3F7[0] = (char)si;
	state.field_3F7[1] = (char)si;
	for (di = 0; di < 48; di++) state.field_3FA[di] = (char)si;   /* loc_16BAC */
	for (di = 0; di < 24; di++) state.field_38E[di] = si;         /* loc_16BBC */

	var_A = multiply_and_scale(sin_fast((uint16_t)(track_angle + 0x300)), 0x200);
	ax    = multiply_and_scale(sin_fast((uint16_t)(track_angle + 0x200)), 0x1000);
	cx    = (int16_t)(ax + var_A);
	state.game_vec1[0].x = (int16_t)(cx + ((int16_t)startcol2 << 10));
	state.game_vec1[0].y = (int16_t)(hillHeightConsts[(int16_t)hillFlag] + 0x3C0);
	var_A = multiply_and_scale(cos_fast((uint16_t)(track_angle + 0x300)), 0x200);
	cx    = multiply_and_scale(cos_fast((uint16_t)(track_angle + 0x200)), 0x1000);
	cx    = (int16_t)(cx + trackpos[(int16_t)startrow2]);
	state.game_vec1[0].z = (int16_t)(cx + var_A);

	state.game_vec1[1]   = state.game_vec1[0];   /* dseg name: game_vec2 */
	state.game_vec3      = state.game_vec1[0];
	state.game_vec4      = state.game_vec1[0];

	/* seg001:4008..4021 - the tail. Everything the scoreboard reads is
	 * cleared here, which is why a restart shows a blank result screen
	 * rather than the previous run's. */
	state.game_travDist    = 0;
	state.game_frame       = si;
	state.game_total_finish = si;
	state.field_144        = si;
	state.game_pEndFrame   = si;
	state.game_oEndFrame   = si;
	state.game_penalty     = si;
	state.game_impactSpeed = si;
	state.game_topSpeed    = si;
	state.game_jumpCount   = si;
}

/*
 * seg001 ported_init_game_state_ (3885..4021), the callable form.
 *
 * Phase 9 needs this: loop_game and restore_gamestate both call it with an
 * argument, and only `init_game_state_vars` - the body above - existed. The
 * argument selects how much is reset; -3 (0FFFDh) takes the early exit at
 * loc_16B7A that leaves the state alone and only refreshes the frame-rate
 * derived tables, which is what the intro uses.
 */
void init_game_state(int16_t arg_0)
{
	/* loc_16B4D: the two tables that depend on the frame rate, refreshed
	 * whatever the argument. */
	word_45A00 = (int16_t)(framespersec * 30);
	word_4499C = (int16_t)(100 / (framespersec ? framespersec : 20));
	if (arg_0 == (int16_t)0xFFFD) return;      /* loc_16F34, the early exit */
	init_game_state_vars();
}

/* -------------------------------------------------------------------- *
 * state_op_unk - seg001.asm 9215..9385                                  *
 *                                                                       *
 * The last simulation-affecting stub in sfstubs.c (slot sfstub_hits[3]). *
 * It is the crash-debris spawner: it fills up to 18 (or 8) of the 24     *
 * splinter slots and sets state.field_42A, after which update_gamestate  *
 * runs sub_19BA0 every frame to fly and land them.                       *
 *                                                                       *
 * Per splinter i:                                                        *
 *   field_443[i]  the arg_0 that spawned it (which body part)            *
 *   field_42B[i]  (n & 3) + var_10, the shape variant                    *
 *   game_longs1/2/3[i]  position offset, zeroed here                     *
 *   field_2FE[i]  spin about one axis, random*4                          *
 *   field_32E[i]  spin about the other, random*4                         *
 *   field_35E[i]  launch heading, n/var_12 of the way round var_2        *
 *                 starting at var_6, wrapped to the 0x400 circle         *
 *   field_38E[i]  launch speed, random*6/4 + arg_4 + 0x180               *
 *   field_3BE[i]  initial rise, (var_8 * speed) >> 2                     *
 *                                                                       *
 * Two callers: update_crash_state (statecrs.c:136) passes arg_0 = 0 or 1 *
 * and gets the full 0x400 circle and 18 pieces; stateply.c:3143 passes   *
 * arg_0 = wheel+2 and gets a 0xC0 arc centred on the car's heading and 8 *
 * pieces.                                                                *
 *                                                                       *
 * [ODDITY] seg001.asm 9345: `and ah,3` masks the heading to 10 bits AND  *
 * to a 0x400 circle at the same time - the mask is applied to the high   *
 * byte only, so a negative var_6 stays negative in the low byte and the  *
 * result can be any of 0..0x3FF regardless of sign.  Reproduced.         *
 * -------------------------------------------------------------------- */
void state_op_unk(int16_t arg_0, int16_t arg_2, int16_t arg_4)
{
	int16_t var_18, var_16, var_14, var_12, var_10, var_E, var_C;
	int16_t var_8, var_6, var_2;
	int16_t si, di, ax;
	/* field_3BE is char[48] in externs.h, 24 words to the original. */
	int16_t* field_3BE_w = (int16_t*)state.field_3BE;

	sfstub_hits[3]++;      /* kept: the harnesses print this counter */

	if (arg_0 >= 2) goto loc_19A5E;                  /* jge */
	var_6 = arg_2;
	var_2 = 0x400;
	var_E = 0x12;
	var_10 = (int16_t)(arg_0 * 4 + 4);
	var_8 = 6;
	goto loc_19A7B;
loc_19A5E:
	var_6 = (int16_t)(arg_2 - 0x60);
	var_2 = 0xC0;
	var_E = 8;
	var_10 = 0;
	var_8 = 1;
loc_19A7B:
	state.field_42A = 1;
	var_12 = 0;
	for (si = 0; si < 0x18; si++)                    /* loc_19A87 */
		if (state.field_38E[si] == 0) var_12++;
	if (var_12 > var_E) var_12 = var_E;              /* jle loc_19AA6 */
loc_19AA6:
	var_C = 0;
	si = 0;
	goto loc_19AB1;
loc_19AB0:
	si++;
loc_19AB1:
	if (si >= 0x18) goto loc_19B99;
/* loc_19AB9: */
	var_14 = (int16_t)(si * 2);
	if (state.field_38E[si] != 0) goto loc_19AB0;
	state.field_443[si] = (char)arg_0;               /* mov al, byte ptr arg_0 */
	state.field_42B[si] = (char)((uint8_t)((uint8_t)var_C & 3)
	                            + (uint8_t)var_10);  /* 8-bit add */
	var_16 = (int16_t)(si * 4);
	state.game_longs1[si] = 0;
	state.game_longs2[si] = 0;
	state.game_longs3[si] = 0;
	state.field_2FE[si] = (int16_t)(get_kevinrandom() << 2);
	state.field_32E[si] = (int16_t)(get_kevinrandom() << 2);
	/* __aFlmul(var_C, var_2) then __aFldiv by var_12: the last operand
	 * pushed is the dividend, so it is (var_2 * var_C) / var_12.  var_12
	 * can only be 0 when every slot is taken, and then this line is
	 * unreachable - the original relies on exactly that. */
	ax = (int16_t)(((int32_t)var_2 * (int32_t)var_C) / (int32_t)var_12);
	ax = (int16_t)(ax + var_6);
	ax = (int16_t)((uint16_t)ax & 0x03FF);           /* and ah,3 */
	state.field_35E[si] = ax;
	di = get_kevinrandom();
	di = (int16_t)(((int16_t)(di * 2) + di) * 2);    /* cx=ax; shl; add; shl */
	di = sar16(di, 2);
	di = (int16_t)(di + arg_4);
	di = (int16_t)(di + 0x180);
	var_18 = (int16_t)(si * 2);
	state.field_38E[si] = di;
	ax = (int16_t)(var_8 * di);                      /* imul di, low word */
	ax = sar16(ax, 1);
	ax = sar16(ax, 1);
	field_3BE_w[si] = ax;
	var_C++;
	if (var_C == var_12) goto loc_19B99;             /* jz */
	goto loc_19AB0;
loc_19B99:
	(void)var_14; (void)var_16; (void)var_18;
}

/* ---------------------------------------------------------------------- *
 * Phase 9: rewinding.                                                     *
 *                                                                         *
 * The mechanism is a ring of twenty GAMESTATE snapshots at cvxptr, each    *
 * 0x460 = 1120 bytes - the same 1120 bytes the DOS oracle dumps per frame, *
 * which is why the two agree field for field. update_gamestate above       *
 * already writes them: every word_45A00 frames (= 30 * framespersec, so    *
 * 600 frames at 20fps) it stores the RNG seed into state.kevinseed and     *
 * copies the whole state into slot game_frame / word_45A00.               *
 *                                                                         *
 * Restoring is the other half, and the seed is the reason it works: the    *
 * game's randomness is six bytes carried *inside* the saved state, so      *
 * putting a snapshot back makes everything that follows reproduce exactly. *
 * Without that a rewind would diverge the moment anything asked for a      *
 * random number.                                                          *
 *                                                                         *
 *   ported_init_kevinrandom_     seg002.asm  175..205                      *
 *   ported_restore_gamestate_    seg001.asm 4286..4394                     *
 * ---------------------------------------------------------------------- */

void init_kevinrandom(const char* arg_0)
{
	g_kevinrandom_seed[0] = (uint8_t)arg_0[0];
	g_kevinrandom_seed[1] = (uint8_t)arg_0[1];
	g_kevinrandom_seed[2] = (uint8_t)arg_0[2];
	g_kevinrandom_seed[3] = (uint8_t)arg_0[3];
	g_kevinrandom_seed[4] = (uint8_t)arg_0[4];
	g_kevinrandom_seed[5] = (uint8_t)arg_0[5];
}

void restore_gamestate(int16_t arg_frame)
{
	int16_t si;

	if (arg_frame == 0 && elapsed_time1 == 0)
		init_game_state(0);              /* now the real thing, not the body */

loc_16F59:
	si = (int16_t)(arg_frame / word_45A00);          /* cwd ; idiv */
	if (si == 20) si--;                              /* the ring wraps */
	if ((uint16_t)arg_frame < (uint16_t)state.game_frame)   /* jb */
		goto loc_16FB1;

loc_16F73:
	/* Walk back from the nominal slot until one is marked valid. A slot
	 * whose field_3F4 byte is zero was never written. */
	if ((uint16_t)(int16_t)(word_45A00 * si) > (uint16_t)state.game_frame) {
		const uint8_t* slot = (const uint8_t*)cvxptr + (int32_t)si * 0x460;
		if (slot[0x3F4] == 0) {   /* asm: GAMESTATE.field_3F4 */
			si--;                                    /* loc_16FFE */
			goto loc_16F73;
		}
	} else {
		return;                                      /* loc_17002 */
	}

loc_16FB1:
	memcpy(&state, (const uint8_t*)cvxptr + (int32_t)si * 0x460, 0x230 * 2);
	init_kevinrandom(state.kevinseed);
	elapsed_time2 = state.game_frame;
}
