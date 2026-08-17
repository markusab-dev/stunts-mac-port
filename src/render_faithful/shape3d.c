#ifdef RESTUNTS_DOS
#include <dos.h>
#endif
#include <stddef.h>
#include <limits.h>
#include "externs.h"
#include "rfbsize.h"
#include "fileio.h"
#include "memmgr.h"
#include "shape3d.h"
#include "shape2d.h"

/*

TODO:

  lines function (calls)
- ----- --------- -------
X   159 mat_rot_zxy (16)
X    60 mat_multiply (0)
X   122 mat_mul_vector (0)
X    48 mat_invert (0)
X   126 vector_op_unk2 (14)
X    97 vector_to_point (0)
X    32 rect_compare_point (0)
X    40 vector_op_unk (0)
X    86 is_facing_camera (4)
X    33 rect_adjust_from_point(0)
X    47 polarRadius2D (6)
X    13 polarRadius3D (4)
X     7 projectiondata9_times_ratio (0)
X    74 insert_newest_poly_in_poly_linked_list_40ED6 (0)
X     - polarAngle (0)
X     - mat_rot_z (4)
X     - mat_rot_x (4)
X     - mat_rot_y (4)
X     - set_projection (10)
X     - select_cliprect_rotate (10)

*/

extern char far* game1ptr;
extern char far* game2ptr;
extern char far* curshapeptr;
extern struct SHAPE3D game3dshapes[130];

extern void shape3d_init_shape(char far* shapeptr, struct SHAPE3D* gameshape);
extern uint32_t mmgr_get_res_ofs_diff_scaled(void);

extern char aBarn[];

int16_t shape3d_load_all() {
	int16_t i;
	uint32_t mmgrofsdiff;
	char* shapename;

	game1ptr = 0;
	game2ptr = 0;
	
	mmgrofsdiff = mmgr_get_res_ofs_diff_scaled();
	
	if (mmgrofsdiff < 0xFDE8)	// ??
		return 1;

	game1ptr = file_load_3dres("game1");
	game2ptr = file_load_3dres("game2");
	
	for (i = 0; i < 0x74; i++) {
		shapename = &aBarn[i * 5];
		curshapeptr = locate_shape_nofatal(game1ptr, shapename);
		if (curshapeptr == 0)
			curshapeptr = locate_shape_fatal(game2ptr, shapename);
		shape3d_init_shape(curshapeptr, &game3dshapes[i]);
	}
	return 0;
}

void shape3d_free_all() {
	if (game1ptr != 0)
		mmgr_free(game1ptr);
	if (game2ptr != 0)
		mmgr_free(game2ptr);
}

void shape3d_init_shape(char far* shapeptr, struct SHAPE3D* gameshape) {
	struct SHAPE3DHEADER far* hdr = shapeptr;
	gameshape->shape3d_numverts = hdr->header_numverts;
	gameshape->shape3d_numprimitives = hdr->header_numprimitives;
	gameshape->shape3d_numpaints = hdr->header_numpaints;
	gameshape->shape3d_verts = shapeptr + 4; // TODO: deserialize from 16 bit to "int16_t" (on 32bit)
	gameshape->shape3d_cull1 = shapeptr + hdr->header_numverts * 6 + 4;
	gameshape->shape3d_cull2 = shapeptr + hdr->header_numprimitives * 4 + hdr->header_numverts * 6 + 4;
	gameshape->shape3d_primitives = shapeptr + hdr->header_numprimitives * 8 + hdr->header_numverts * 6 + 4;
}


extern char is_facing_camera(struct POINT2D far*);
extern uint16_t insert_newest_poly_in_poly_linked_list_40ED6(uint16_t, uint16_t);
extern uint16_t projectiondata9_times_ratio(uint16_t, int16_t);
extern void __aFuldiv();

extern uint16_t word_40ECE;
extern uint16_t transshapenumverts;
extern uint8_t far* transshapeprimitives;
extern struct VECTOR far* transshapeverts;
extern uint16_t transshapenumpaints;
extern uint8_t transshapematerial;
extern uint8_t transshapeflags;
extern struct RECTANGLE* transshaperectptr;
extern struct MATRIX mat_temp;
extern int32_t invpow2tbl[32];
extern uint8_t byte_4393D;

// Four iterators in the linked list. Their roles are just the best guess
// Starting index for some kinds of scans
extern uint16_t poly_linklist_40ED6_iter1;
// Unknown
extern uint16_t poly_linklist_40ED6_iter2;
// Scan counter
extern uint16_t poly_linklist_40ED6_iter3;
// After insertion, contains the index of the newly inserted primitive
extern uint16_t poly_linklist_40ED6_iter4;

extern uint8_t transshapenumvertscopy;
extern struct POINT2D* polyvertpointptrtab[];
extern uint16_t select_rect_param;
extern uint8_t primidxcounttab[];
extern uint8_t primtypetab[];
extern char far* transshapeprimptr;
extern uint16_t polyinfoptrnext;
extern char far* polyinfoptr;
extern char far* transshapepolyinfo;
struct POINT2D far* transshapepolyinfopts;
extern int16_t far* polyinfoptrs[];
extern uint16_t polyinfonumpolys;
extern char transprimitivepaintjob;
extern uint8_t far* transshapeprimindexptr;
extern char backlights_paint_override;

// ASMVECTOR is used temporarily for the inline assembly since x and y are ambigous struct member names
struct ASMVECTOR {
	int16_t vx, vy, vz;
};

uint16_t transformed_shape_op(struct TRANSFORMEDSHAPE3D* arg_transshapeptr) {
	int32_t far* var_cull1;
	int32_t far* var_cull2;
		
	uint8_t var_vertflagtbl[256];
	struct MATRIX* var_rotmatptr;
	struct MATRIX var_mat;
	struct MATRIX var_mat2;
	struct VECTOR var_vec;
	struct VECTOR var_vec2;
	struct VECTOR var_vec3;
	struct VECTOR var_vec4;
	int32_t var_45C;
	int32_t var_A;
	uint16_t var_45E, var_460, var_1A;
	uint8_t var_ptrectflag, var_primtype;
	struct VECTOR var_vecarr[255];
	uint16_t var_primitiveflags, var_fileprimtype, var_4, var_polyvertcounter, var_C, var_448, var_B7C, var_462;
	int16_t var_polyvertX, var_polyvertY;
	struct POINT2D far* var_transshapepolyinfoptptr;
	int32_t var_18;
	struct POINT2D var_574, var_450;
	struct POINT2D var_vecarr2[255];
	struct POINT2D** var_polyvertunktabptr;
	struct POINT2D far* var_polyvertsptr;

	/*
    var_B7C = word ptr -2940
    var_polyvertunktabptr = word ptr -2938
    var_cull1 = dword ptr -2936
    var_vec4 = VECTOR ptr -2932
    var_vecarr = VECTOR ptr -2926
    var_574 = POINT2D ptr -1396
    var_polyvertY = word ptr -1392
    var_polyvertsptr = dword ptr -1390
    var_vec3 = VECTOR ptr -1386
    var_polyvertX = word ptr -1380
    var_vertflagtbl = byte ptr -1378
    var_462 = word ptr -1122
    var_460 = word ptr -1120
    var_45E = word ptr -1118
    var_45C = dword ptr -1116
    var_vec2 = VECTOR ptr -1112
    var_450 = POINT2D ptr -1104
    var_ptrectflag = byte ptr -1098
    var_448 = word ptr -1096
    var_mat = MATRIX ptr -1094
    var_cull2 = dword ptr -1076
    var_transshapepolyinfoptptr = dword ptr -1072
    var_rotmatptr = word ptr -1068
    var_mat2 = MATRIX ptr -1066
    var_fileprimtype = word ptr -1048
    var_vecarr2 = VECTOR ptr -1046
    var_1A = word ptr -26
    var_18 = dword ptr -24
    var_vec = VECTOR ptr -20
    var_polyvertcounter = word ptr -14
    var_C = word ptr -12
    var_A = dword ptr -10
    var_primtype = byte ptr -6
    var_4 = word ptr -4
    var_primitiveflags = word ptr -2
     s = byte ptr 0
     r = byte ptr 2
    arg_transshapeptr = word ptr 6
*/

	uint16_t result, i;
	uint16_t temp, temp0, temp1;
	const char* errorstr = "errorstr %i";
	const char* errorstr2 = "errorstr2 %i %i";

	//result = ported_transformed_shape_op_(arg_transshapeptr);
	//return result;

	if (word_40ECE != 0) return 1;
	transshapenumverts = arg_transshapeptr->shapeptr->shape3d_numverts;
	transshapeprimitives = arg_transshapeptr->shapeptr->shape3d_primitives;
	transshapeverts = arg_transshapeptr->shapeptr->shape3d_verts;
	transshapenumpaints = arg_transshapeptr->shapeptr->shape3d_numpaints;
	var_cull1 = arg_transshapeptr->shapeptr->shape3d_cull1;
	var_cull2 = arg_transshapeptr->shapeptr->shape3d_cull2;
	transshapematerial = arg_transshapeptr->material;
	if (transshapematerial >= transshapenumpaints)
		transshapematerial = 0;
	transshapeflags = arg_transshapeptr->ts_flags;
	
	if ((transshapeflags & 8) != 0) {
		transshaperectptr = arg_transshapeptr->rectptr;
	}
	
	for (i = 0; i < transshapenumverts; i++) {
		var_vertflagtbl[i] = 0xff;
	}
	
	if ((transshapeflags & 2) == 0) {
		var_rotmatptr = mat_rot_zxy(arg_transshapeptr->rotvec.x, arg_transshapeptr->rotvec.y, arg_transshapeptr->rotvec.z, 0);
		mat_mul_vector(&arg_transshapeptr->pos, &mat_temp, &var_vec);
		mat_multiply(var_rotmatptr, &mat_temp, &var_mat2);
		mat_invert(&var_mat2, &var_mat);
		var_vec2.x = 0;
		var_vec2.y = 0;
		var_vec2.z = 0x1000;
		mat_mul_vector(&var_vec2, &var_mat, &var_vec3);
		if ((var_vec3.y <= 0 || arg_transshapeptr->pos.y >= 0) && (arg_transshapeptr->unk * 2 <= abs(var_vec.x) || arg_transshapeptr->unk * 2 <= abs(var_vec.z))) {
			byte_4393D = vector_op_unk2(&var_vec3);
			var_45C = invpow2tbl[byte_4393D];
			var_A = invpow2tbl[byte_4393D];
		} else {
			var_45C = -1;
			var_A = 0;
		}
	} else {
		var_rotmatptr = mat_rot_zxy(arg_transshapeptr->rotvec.x, arg_transshapeptr->rotvec.y, arg_transshapeptr->rotvec.z, 0);
		mat_multiply(var_rotmatptr, &mat_temp, &var_mat2);
		var_vec = arg_transshapeptr->pos;
		var_45C = -1;
		var_A = 0;
	}
	
// loc_250A3:
	poly_linklist_40ED6_iter1 = poly_linklist_40ED6_iter2;
	poly_linklist_40ED6_iter4 = poly_linklist_40ED6_iter2;
	poly_linklist_40ED6_iter3 = 0;
	var_45E = 0;
	
	if (transshapenumverts <= 8) {
		transshapenumvertscopy = transshapenumverts;
	} else {
		transshapenumvertscopy = 8;
	}
	
	if (transshapenumvertscopy > 4 && transshapeverts[0].y == transshapeverts[4].y) {
		transshapenumvertscopy = 4;
	}

	goto loc_250E6;

loc_250E6:
	var_ptrectflag = 0x0f;
	var_460 = 1;
	var_1A = 0;
	i = 0;
	goto loc_2513F;

loc_2513E:
	i++;
loc_2513F:
	if (transshapenumvertscopy > i) goto loc_2514B;
	//goto loc_251F6;
	if (var_460 != 0 || var_1A == 0 || arg_transshapeptr->unk < abs(var_vec.x)) return -1;
	goto loc_25220;

loc_2514B:	

	polyvertpointptrtab[i] = &var_vecarr2[i];
	var_vec2 = transshapeverts[i];
	if (select_rect_param != 0) {
		var_vec2.x /= 2;
		var_vec2.y /= 2;
		var_vec2.z /= 2;
	}
	mat_mul_vector(&var_vec2, &var_mat2, &var_vec3);
	var_vec3.x += var_vec.x;
	var_vec3.y += var_vec.y;
	var_vec3.z += var_vec.z;
	var_vecarr[i] = var_vec3;
	if (var_vec3.z < 0xc) {
		var_vertflagtbl[i] = 1;
		var_1A = 1;
		goto loc_2513E;
	}
	goto loc_250FA;
	
loc_250FA:
	var_460 = 0;
	var_vertflagtbl[i] = 0;
	vector_to_point(&var_vec3, polyvertpointptrtab[i]);
	if (var_ptrectflag != 0)
		var_ptrectflag &= rect_compare_point(polyvertpointptrtab[i]);
	if (var_ptrectflag != 0) 
		goto loc_2513E;
	goto loc_25220;

	
	
/*
asm {
    cmp     word_40ECE, 0
    jz      int16_t loc_24EB8
loc_24EAE:
    mov     ax, 1

    jmp the_end
    retf

loc_24EB8:
    mov     bx, [arg_transshapeptr]
    mov     bx, [bx + .shapeptr]
    mov     ax, [bx + .shape3d_numverts]
    mov     transshapenumverts, ax

    mov     bx, [arg_transshapeptr]
    mov     bx, [bx+.shapeptr]
    mov     ax, word ptr [bx+.shape3d_primitives]
    mov     dx, word ptr [bx+(.shape3d_primitives+2)]
    mov     word ptr transshapeprimitives, ax
    mov     word ptr transshapeprimitives+2, dx

loc_24ED6:
    mov     bx, [arg_transshapeptr]
    mov     bx, [bx+.shapeptr]
    mov     ax, word ptr [bx+.shape3d_verts]
    mov     dx, word ptr [bx+(.shape3d_verts+2)]
    mov     word ptr transshapeverts, ax
    mov     word ptr transshapeverts+2, dx
    mov     bx, [arg_transshapeptr]
    mov     bx, [bx+.shapeptr]
    mov     al, byte ptr [bx+.shape3d_numpaints]
    cbw
    mov     transshapenumpaints, ax
    mov     bx, [arg_transshapeptr]
    mov     bx, [bx+.shapeptr]
    mov     ax, word ptr [bx+.shape3d_cull1]
    mov     dx, word ptr [bx+(.shape3d_cull1+2)]
    mov     word ptr [var_cull1], ax
    mov     word ptr [var_cull1+2], dx
    mov     bx, [arg_transshapeptr]
    mov     bx, [bx+.shapeptr]
    mov     ax, word ptr [bx+.shape3d_cull2]
    mov     dx, word ptr [bx+(.shape3d_cull2+2)]
    mov     word ptr [var_cull2], ax
    mov     word ptr [var_cull2+2], dx
    mov     bx, [arg_transshapeptr]
    mov     al, [bx+.material]
    mov     transshapematerial, al
    cmp     byte ptr transshapenumpaints, al
    ja      int16_t loc_24F32
    mov     transshapematerial, 0
loc_24F32:
    mov     al, [bx+.ts_flags]
    mov     transshapeflags, al
    test    transshapeflags, 8
    jz      int16_t loc_24F45
    mov     ax, [bx+.rectptr]
    mov     transshaperectptr, ax

loc_24F45:
    sub     si, si
    jmp     int16_t loc_24F50

loc_24F4A:
    mov     byte ptr [si+var_vertflagtbl], 0FFh
    inc     si

loc_24F50:

    mov     ax, si
    cmp     ax, transshapenumverts
    jb      int16_t loc_24F4A

    test    byte ptr transshapeflags, 2
    jz      int16_t loc_24FB6
    sub     ax, ax
    push    ax
    mov     bx, [arg_transshapeptr]
    push    word ptr [bx+.rotvec.z]
    push    word ptr [bx+.rotvec.y]
    push    word ptr [bx+.rotvec.x]
    //push    cs

    call far ptr mat_rot_zxy
    add     sp, 8
    mov     [var_rotmatptr], ax
    lea     ax, [var_mat2]
    push    ax
    mov     ax, offset mat_temp
    push    ax
    push    word ptr [var_rotmatptr]
    call    far ptr mat_multiply
    add     sp, 6
    mov     ax, [arg_transshapeptr]
    push    si
    push    di
    lea     di, [var_vec]
    mov     si, ax
    push    ss
    pop     es
    movsw
    movsw
    movsw
    pop     di
    pop     si
loc_24F9F:
    mov     word ptr [var_45C], 0FFFFh
    mov     word ptr [var_45C+2], 0FFFFh
    sub     ax, ax
    mov     word ptr [var_A+2], ax
    mov     word ptr [var_A], ax
    jmp     loc_250A3
loc_24FB6:
    sub     ax, ax
    push    ax
    mov     bx, [arg_transshapeptr]
    push    word ptr [bx+.rotvec.z]
    push    word ptr [bx+.rotvec.y]
    push    word ptr [bx+.rotvec.x]
    //push    cs
    call far ptr mat_rot_zxy
    add     sp, 8

    mov     [var_rotmatptr], ax
    lea     ax, [var_vec]
    push    ax
    mov     ax, offset mat_temp
    push    ax
    push    word ptr [arg_transshapeptr]
    call    far ptr mat_mul_vector
    add     sp, 6
    lea     ax, [var_mat2]
    push    ax
    mov     ax, offset mat_temp
    push    ax
    push    word ptr [var_rotmatptr]
    call    far ptr mat_multiply
    add     sp, 6
    lea     ax, [var_mat]
    push    ax
    lea     ax, [var_mat2]
    push    ax
    call    far ptr mat_invert
    add     sp, 4
    mov     word ptr [var_vec2.vx], 0
    mov     word ptr [var_vec2.vy], 0
    mov     word ptr [var_vec2.vz], 1000h
    lea     ax, [var_vec3]
    push    ax
    lea     ax, [var_mat]
    push    ax
    lea     ax, [var_vec2]
    push    ax
    call    far ptr mat_mul_vector
    add     sp, 6
    cmp     word ptr [var_vec3.y], 0
    jle     int16_t loc_25046
    mov     bx, [arg_transshapeptr]
    cmp     word ptr [bx+.pos.y], 0
    jge     int16_t loc_25046
    jmp     loc_24F9F
loc_25046:
    push    word ptr [var_vec.vx] // int16_t
    call    far ptr abs
    add     sp, 2
    mov     bx, [arg_transshapeptr]
    mov     cx, [bx+.unk]
    shl     cx, 1
    cmp     cx, ax
    jle     int16_t loc_25077
    push    word ptr [var_vec.z] // int16_t
    call    far ptr abs
    add     sp, 2
    mov     bx, [arg_transshapeptr]
    mov     cx, [bx+.unk]
    shl     cx, 1
    cmp     cx, ax
    jle     int16_t loc_25077
    jmp     loc_24F9F
loc_25077:
    lea     ax, [var_vec3]
    push    ax
    //push    cs
    call far ptr vector_op_unk2
    add     sp, 2
    mov     byte_4393D, al
    cbw
    mov     bx, ax
    shl     bx, 1
    shl     bx, 1
    mov     ax, word ptr invpow2tbl[bx]
    mov     dx, word ptr (invpow2tbl+2)[bx]
    mov     word ptr [var_45C], ax
    mov     word ptr [var_45C+2], dx
    mov     word ptr [var_A], ax
    mov     word ptr [var_A+2], dx

loc_250A3:
    mov     ax, poly_linklist_40ED6_iter2
    mov     poly_linklist_40ED6_iter1, ax
    mov     poly_linklist_40ED6_iter4, ax
    mov     poly_linklist_40ED6_iter3, 0
    mov     word ptr [var_45E], 0
    cmp     transshapenumverts, 8
    jbe     int16_t loc_250C6
    mov     transshapenumvertscopy, 8
    jmp     int16_t loc_250CC

loc_250C6:
    mov     al, byte ptr transshapenumverts
    mov     transshapenumvertscopy, al
loc_250CC:
    cmp     transshapenumvertscopy, 4
    jbe     int16_t loc_250E6
    les     bx, transshapeverts
    mov     ax, es:[bx+1Ah]
    cmp     es:[bx+2], ax
    jnz     int16_t loc_250E6
    mov     transshapenumvertscopy, 4

}

loc_250E6:
	asm {
    mov     byte ptr [var_ptrectflag], 0Fh
    mov     word ptr [var_460], 1
    mov     word ptr [var_1A], 0
    sub     si, si
    jmp     int16_t loc_2513F


loc_2513E:
    inc     si
loc_2513F:
    mov     al, transshapenumvertscopy
    sub     ah, ah
    cmp     ax, si
    ja      int16_t loc_2514B
    jmp     loc_251F6
loc_2514B:

    mov     bx, si
    shl     bx, 1 // * 2 = sizeof ptr

    mov     ax, si
    shl     ax, 1
    shl     ax, 1 // * 4? (sizeof(POINT2D)) ??
    
    add ax, bp
    add ax, offset var_vecarr2 // LOOKS STRANGE IN ASSEMBEL - this is maybe similar to sub ax, 0xyxy
    //add     ax, bp
    //sub     ax, 416h        // array access in var_416, but dunno how to make IDA show this

	mov     polyvertpointptrtab[bx], ax
    

    mov     ax, si
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1 // * 6 = sizeof(VECTOR)
    add     ax, word ptr transshapeverts
    mov     dx, word ptr transshapeverts+2
    push    si
    push    di
    lea     di, [var_vec2]
    mov     si, ax
    push    ss
    pop     es
    push    ds
    mov     ds, dx
    movsw
    movsw
    movsw
    pop     ds
    pop     di
    pop     si
    cmp     select_rect_param, 0
    jz      int16_t loc_25196
    sar     word ptr [var_vec2.vx], 1
    sar     word ptr [var_vec2.vy], 1
    sar     word ptr [var_vec2.vz], 1
loc_25196:

    lea     ax, [var_vec3]
    push    ax
    lea     ax, [var_mat2]
    push    ax
    lea     ax, [var_vec2]
    push    ax
    call    far ptr mat_mul_vector
    add     sp, 6
    mov     ax, word ptr [var_vec.vx]  // DOES NOT APPEAR IN ASSEMBLE WO word ptr
    add     word ptr [var_vec3.vx], ax
    mov     ax, word ptr [var_vec.vy]
    add     word ptr [var_vec3.vy], ax
    mov     ax, word ptr [var_vec.vz]
    add     word ptr [var_vec3.vz], ax
    mov     bx, si
    mov     ax, bx
    shl     bx, 1
    add     bx, ax
    shl     bx, 1           // bx = vertex index * 6
    add     bx, bp
    push    si
    push    di
    lea     di, [bx+offset var_vecarr] // DOES NOT APPEAR IN ASSEMBEL
    lea     si, [var_vec3]
    push    ss
    pop     es
    movsw
    movsw
    movsw
    pop     di
    pop     si
    cmp     word ptr [var_vec3.vz], 0Ch
    jl      int16_t loc_251E9
//    jmp     loc_250FA

//loc_250FA:

    mov     word ptr [var_460], 0
    mov     byte ptr [si+var_vertflagtbl], 0
    mov     bx, si
    shl     bx, 1
    push    word ptr polyvertpointptrtab[bx]
    lea     ax, [var_vec3]
    push    ax
    call    far ptr vector_to_point
    add     sp, 4
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_25134
    mov     bx, si
    shl     bx, 1
    push    word ptr polyvertpointptrtab[bx]
    //push    cs
    call far ptr rect_compare_point
    add     sp, 2
    and     byte ptr [var_ptrectflag], al
loc_25134:
    cmp     byte ptr [var_ptrectflag], 0
    jnz     int16_t loc_2513E
    jmp     loc_25220

}

loc_251E9:
asm {

    mov     byte ptr [si+var_vertflagtbl], 1
    mov     word ptr [var_1A], 1
    jmp     loc_2513E // continue
}
    
// end of loop, for i = 0 .. transshapenumvertscopy

loc_251F6:
	asm {

    cmp     word ptr [var_460], 0
    jnz     int16_t _done_ret_neg1
    cmp     word ptr [var_1A], 0
    jz      int16_t _done_ret_neg1
    push    word ptr [var_vec.vx] // int16_t
    call    far ptr abs
    add     sp, 2
    mov     bx, [arg_transshapeptr]
    cmp     [bx+.unk], ax
    jge     int16_t loc_25220

asm {
_done_ret_neg1:
    mov     ax, 0FFFFh
    //pop     si
    //pop     di
    jmp the_end
    retf
	}
*/

loc_25220: // // in the loop, for i = 0 .. transshapenumvertscopy
	
	transshapeprimitives = arg_transshapeptr->shapeptr->shape3d_primitives;
	
/*		
	asm {
    mov     bx, [arg_transshapeptr]
    mov     bx, [bx+.shapeptr]
    mov     ax, word ptr [bx+.shape3d_primitives]
    mov     dx, word ptr [bx+(.shape3d_primitives+2)]
    mov     word ptr transshapeprimitives, ax
    mov     word ptr transshapeprimitives+2, dx*/

loc_25233:
	transshapeprimptr = transshapeprimitives + primidxcounttab[transshapeprimitives[0]] + transshapenumpaints + 2;
	var_primitiveflags = transshapeprimitives[1];
	var_4 = 0;
	if ((var_cull1[0] & var_45C) != 0) {
		goto loc_25282;
	}
	goto loc_25801;
/*
asm {
	les     bx, transshapeprimitives
    mov     bl, es:[bx]     // primitives+0 = primitive type
    sub     bh, bh

    mov     al, primidxcounttab[bx] // look up maybe indexcount from a table? FELL OUT OF ASSEMBY
    sub     ah, ah
    add     ax, transshapenumpaints
    add     ax, word ptr transshapeprimitives
    mov     dx, es
    add     ax, 2
    mov     word ptr transshapeprimptr, ax
    mov     word ptr transshapeprimptr+2, dx
    mov     bx, word ptr transshapeprimitives
    mov     al, es:[bx+1]   // primitives+1 = primitive flags
    sub     ah, ah
    mov     [var_primitiveflags], ax
    mov     word ptr [var_4], 0
    les     bx, [var_cull1]
    mov     ax, es:[bx]
    mov     dx, es:[bx+2]
    and     ax, word ptr [var_45C]
    and     dx, word ptr [var_45C+2]
    or      dx, ax
    jnz     int16_t loc_25282

    jmp     loc_25801
}*/
loc_25282:

	var_fileprimtype = transshapeprimitives[0];
	transshapenumvertscopy = primidxcounttab[var_fileprimtype];
	var_primtype = primtypetab[var_fileprimtype];

	transshapepolyinfo = polyinfoptr + polyinfoptrnext;
	polyinfoptrs[polyinfonumpolys] = transshapepolyinfo;

	transprimitivepaintjob = transshapeprimitives[2 + transshapematerial];
	transshapeprimitives += 2 + transshapenumpaints; // <- skip header and materials, -> point at indices

	var_ptrectflag = 0x0f;
	var_460 = 1;
	var_1A = 0;
	transshapeprimindexptr = transshapeprimitives;
	var_polyvertcounter = 0;
	goto loc_25328;

/*
asm {
    les     bx, transshapeprimitives
    mov     al, es:[bx]     // primitives+0 = type
    sub     ah, ah
    mov     [var_fileprimtype], ax
    mov     bx, ax
    mov     al, primidxcounttab[bx]
    mov     transshapenumvertscopy, al
    mov     al, primtypetab[bx]
    mov     [var_primtype], al   // primunktab maps from file-based primitive type to internal type:

    mov     ax, polyinfoptrnext
    add     ax, word ptr polyinfoptr
    mov     dx, word ptr polyinfoptr+2
    mov     word ptr transshapepolyinfo, ax
    mov     word ptr transshapepolyinfo+2, dx

    mov     bx, polyinfonumpolys
    shl     bx, 1
    shl     bx, 1
    mov     word ptr polyinfoptrs[bx], ax
    mov     word ptr (polyinfoptrs+2)[bx], dx

    mov     bl, transshapematerial
    sub     bh, bh
    add     bx, word ptr transshapeprimitives
    mov     es, word ptr transshapeprimitives+2
    mov     al, es:[bx+2]   // primitives+2+X = paint job color, X in [0..numpaints]
    mov     transprimitivepaintjob, al

    mov     ax, transshapenumpaints
    add     ax, 2
    add     word ptr transshapeprimitives, ax // <- skip header and materials, -> point at indices

    mov     byte ptr [var_ptrectflag], 0Fh
    mov     word ptr [var_460], 1
    mov     word ptr [var_1A], 0
    mov     ax, word ptr transshapeprimitives
    mov     dx, es
    mov     word ptr transshapeprimindexptr, ax
    mov     word ptr transshapeprimindexptr+2, dx
    mov     word ptr [var_polyvertcounter], 0
    jmp     int16_t loc_25328
*/
loc_25304:
	var_460 = 0;
/*asm {
    mov     word ptr [var_460], 0
}*/
loc_2530A:
	if (var_ptrectflag != 0) {
		var_ptrectflag &= rect_compare_point(polyvertpointptrtab[var_polyvertcounter]);
	}
/*asm {
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_25325
    mov     bx, [var_polyvertcounter]
    shl     bx, 1
    push    word ptr polyvertpointptrtab[bx]
    //push    cs
    call far ptr rect_compare_point
    add     sp, 2
    and     [var_ptrectflag], al
}*/
loc_25325:
	var_polyvertcounter++;
/*asm {
    inc     word ptr [var_polyvertcounter]
}*/
loc_25328:
	if (var_polyvertcounter >= transshapenumvertscopy) goto loc_2542A;
/*asm {
    mov     al, transshapenumvertscopy
    sub     ah, ah
    cmp     [var_polyvertcounter], ax
    jb      int16_t loc_25335

    jmp     loc_2542A
}*/

loc_25335:
	temp = transshapeprimindexptr[0];
	//fatal_error("%i", temp);
	transshapeprimindexptr++;
	polyvertpointptrtab[var_polyvertcounter] = &var_vecarr2[temp];
	
	if (var_vertflagtbl[temp] == 0xff) { 
		goto loc_25370; 
	}
	if (var_vertflagtbl[temp] == 0) { 
		goto loc_25304;
	}
	if (var_vertflagtbl[temp] == 1) { 
		var_1A = 1;
		goto loc_25325;
	}
	//goto loc_2536E; ->
	goto loc_25325;


/*asm {
	//mov si, temp
	//sub ah,ah
    mov     bx, word ptr transshapeprimindexptr
    inc     word ptr transshapeprimindexptr
    mov     es, word ptr transshapeprimindexptr+2
    mov     al, es:[bx]
    mov     si, ax
    mov     bx, [var_polyvertcounter]
    shl     bx, 1
    shl     ax, 1
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr2
    //add     ax, bp
    //sub     ax, 416h
    mov     polyvertpointptrtab[bx], ax
    mov     al, [si+var_vertflagtbl]
    cbw
    cmp     ax, 0FFFFh
    jz      int16_t loc_25370
    or      ax, ax
    jz      int16_t loc_25304
    cmp     ax, 1
    jnz     int16_t loc_2536E
    //jmp     loc_253FD
    mov word ptr [var_1A], 1
loc_2536E:
    jmp     int16_t loc_25325
}
*/
loc_25370:

	var_vec2 = transshapeverts[temp];
	if (select_rect_param != 0) {
		var_vec2.x /= 2;
		var_vec2.y /= 2;
		var_vec2.z /= 2;
	}
	mat_mul_vector(&var_vec2, &var_mat2, &var_vec3);
	var_vec3.x += var_vec.x;
	var_vec3.y += var_vec.y;
	var_vec3.z += var_vec.z;
	var_vecarr[temp] = var_vec3;

	if (var_vec3.z >= 0xc) {
		// goto loc_25406; ->
		var_460 = 0;
		var_vertflagtbl[temp] = 0;
		vector_to_point(&var_vec3, polyvertpointptrtab[var_polyvertcounter]);
		goto loc_2530A;

	}
	var_vertflagtbl[temp] = 1;
	var_1A = 1;
	goto loc_25325;

/*	
asm {
	mov si, temp
    mov     ax, si
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add     ax, word ptr transshapeverts
    mov     dx, word ptr transshapeverts+2
    push    si
    push    di
    lea     di, [var_vec2]
    mov     si, ax
    push    ss
    pop     es
    push    ds
    mov     ds, dx
    movsw
    movsw
    movsw
    pop     ds
    pop     di
    pop     si
    cmp     select_rect_param, 0
    jz      int16_t loc_253A8
    sar     word ptr [var_vec2.vx], 1
    sar     word ptr [var_vec2.vy], 1
    sar     word ptr [var_vec2.vz], 1

loc_253A8:
    lea     ax, [var_vec3]
    push    ax
    lea     ax, [var_mat2]
    push    ax
    lea     ax, [var_vec2]
    push    ax
    call    far ptr mat_mul_vector
    add     sp, 6

    mov     ax, word ptr [var_vec.vx]
    add     word ptr [var_vec3.vx], ax
    mov     ax, word ptr [var_vec.vy]
    add     word ptr [var_vec3.vy], ax
    mov     ax, word ptr [var_vec.vz]
    add     word ptr [var_vec3.vz], ax
    mov     bx, si
    mov     ax, bx
    shl     bx, 1
    add     bx, ax
    shl     bx, 1
    add     bx, bp
    push    si
    push    di
    lea     di, [bx+offset var_vecarr]
    lea     si, [var_vec3]
    push    ss
    pop     es
    movsw
    movsw
    movsw
    pop     di
    pop     si

    cmp     word ptr [var_vec3.z], 0Ch
    jge     int16_t loc_25406
    mov     byte ptr [si+var_vertflagtbl], 1
}

loc_253FD:
asm {
    mov     word ptr [var_1A], 1
    jmp     loc_25325
}

loc_25406:
asm {
	mov si, temp
    mov     word ptr [var_460], 0
    mov     byte ptr [si+var_vertflagtbl], 0
    mov     bx, [var_polyvertcounter]
    shl     bx, 1
    push    word ptr polyvertpointptrtab[bx]
    lea     ax, [var_vec3]
    push    ax
    call    far ptr vector_to_point
    add     sp, 4
    jmp     loc_2530A
}*/

// end of 2nd loop


loc_2542A:

	if (var_460 != 0) goto loc_25801;
	if (var_ptrectflag != 0 && var_1A == 0) goto loc_25801;
	if (var_primtype == 0) goto _primtype_poly;
	if (var_primtype == 1) goto _primtype_line;
	if (var_primtype == 2) goto _primtype_sphere;
	if (var_primtype == 3) goto _primtype_wheel;
	if (var_primtype == 5) goto loc_25CE0;
	goto loc_25801;

/*
asm {
    cmp     word ptr [var_460], 0
    jz      int16_t loc_25434
    jmp     loc_25801
loc_25434:
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_25444
    cmp     word ptr [var_1A], 0
    jnz     int16_t loc_25444
    jmp     loc_25801
loc_25444:
    mov     al, [var_primtype]
    sub     ah, ah
    or      ax, ax
    jz      int16_t _primtype_poly  // al = 0 for polygons,
    cmp     ax, 1           // 1 = lines
    jnz     int16_t loc_25455
    jmp     _primtype_line
loc_25455:
    cmp     ax, 2
    jnz     int16_t loc_2545D
    jmp     _primtype_sphere // 2 = sphere
loc_2545D:
    cmp     ax, 3
    jnz     int16_t loc_25465
    jmp     _primtype_wheel // 3 = wheel
loc_25465:
    cmp     ax, 5
    jnz     int16_t loc_2546D
    jmp     loc_25CE0       // 5 = particle
loc_2546D:
    jmp     loc_25801       // everything else? (4?)

// ------------------------------------ primtype_poly ------------------------------------
}
*/

_primtype_poly:
	var_transshapepolyinfoptptr = transshapepolyinfo + 6;
	transshapeprimindexptr = transshapeprimitives;

	var_18 = 0;
	var_ptrectflag = 0x0f;

	if (var_1A != 0) goto loc_25518;
	i = 0;
	goto loc_254A7;
loc_254A6:
	i++;
loc_254A7:
	if (transshapenumvertscopy <= i) goto loc_2571A;
	var_C = transshapeprimindexptr[0];
	transshapeprimindexptr++;
	var_18 += var_vecarr[var_C].z;
	var_polyvertunktabptr = &polyvertpointptrtab[i];
	*var_transshapepolyinfoptptr = **var_polyvertunktabptr;
	if (var_ptrectflag != 0) {
		var_ptrectflag &= rect_compare_point(*var_polyvertunktabptr);
	}
	
	var_transshapepolyinfoptptr++;
	goto loc_254A6;

	/*
asm {
    mov     ax, word ptr transshapepolyinfo
    mov     dx, word ptr transshapepolyinfo+2
    add     ax, 6
    mov     word ptr [var_transshapepolyinfoptptr], ax
    mov     word ptr [var_transshapepolyinfoptptr+2], dx
    mov     ax, word ptr transshapeprimitives
    mov     dx, word ptr transshapeprimitives+2
    mov     word ptr transshapeprimindexptr, ax
    mov     word ptr transshapeprimindexptr+2, dx
    sub     ax, ax
    mov     word ptr [var_18+2], ax
    mov     word ptr [var_18], ax
    mov     byte ptr [var_ptrectflag], 0Fh
    cmp     [var_1A], ax
    jnz     int16_t loc_25518
    sub     si, si
    jmp     int16_t loc_254A7
loc_254A6:
    inc     si
loc_254A7:
    mov     al, transshapenumvertscopy
    sub     ah, ah
    cmp     ax, si
    ja      int16_t loc_254B3
    jmp     loc_2571A

loc_254B3:
//	mov ax, [var_polyvertunktabptr]
    mov     bx, word ptr transshapeprimindexptr
    inc     word ptr transshapeprimindexptr
    mov     es, word ptr transshapeprimindexptr+2
    mov     al, es:[bx]
    mov     [var_C], ax
    mov     bx, ax
    shl     bx, 1
    add     bx, ax
    shl     bx, 1
    add     bx, bp
    mov     ax, [bx+offset var_vecarr.z]
    cwd
    add     word ptr [var_18], ax
    adc     word ptr [var_18+2], dx
    mov     ax, si
    shl     ax, 1
    add     ax, offset polyvertpointptrtab
    mov     [var_polyvertunktabptr], ax // 
    mov     bx, ax // ax = bx = POINT2D**
    mov     bx, [bx] // bx = POINT2D*
    mov     ax, [bx] // ax = x
    mov     dx, [bx+2] // dx = y
    les     bx, [var_transshapepolyinfoptptr]
    mov     es:[bx], ax
    mov     es:[bx+2], dx
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_25511
    mov     bx, [var_polyvertunktabptr]
    push    word ptr [bx]
    call far ptr rect_compare_point
    add     sp, 2
    and     [var_ptrectflag], al
loc_25511:
    add     word ptr [var_transshapepolyinfoptptr], 4
    jmp     int16_t loc_254A6
}
*/
loc_25518:

	var_polyvertcounter = 0;
	var_448 = transshapeprimitives[transshapenumvertscopy - 1];
	i = 0;
	goto loc_255EE;
/*
asm {
    mov     word ptr [var_polyvertcounter], 0
    mov     bl, transshapenumvertscopy
    sub     bh, bh
    add     bx, word ptr transshapeprimitives
    mov     es, word ptr transshapeprimitives+2
    mov     al, es:[bx-1]
    sub     ah, ah
    mov     [var_448], ax
    sub     si, si
    jmp     loc_255EE
}*/
loc_2553A:


	if (var_vertflagtbl[var_448] != 0) goto loc_255E6;

	vector_op_unk(&var_vecarr[var_448], &var_vecarr[var_C], &var_vec2, 0x0C);
	vector_to_point(&var_vec2, &var_574);

	if (var_574.px == var_vecarr2[var_448].px && var_574.py == var_vecarr2[var_448].py) goto loc_255E6;

	if (var_ptrectflag != 0) {
		var_ptrectflag &= rect_compare_point(&var_574);
	}
	
	*var_transshapepolyinfoptptr = var_574;
	
loc_255DE:
	var_transshapepolyinfoptptr++;
	var_polyvertcounter++;
/*	
	asm {
    mov     bx, [var_448]
    add     bx, bp

    add bx, offset var_vertflagtbl
    mov al, [bx]
    cmp     al, 0  // does not compile!!!
    jz      int16_t loc_2554A
    jmp     loc_255E6
loc_2554A:
    mov     ax, 0Ch
    push    ax
    lea     ax, [var_vec2]
    push    ax
    mov     ax, [var_C]
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    mov     ax, [var_448]
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    call    far ptr vector_op_unk
    add     sp, 8
    lea     ax, [var_574]
    push    ax
    lea     ax, [var_vec2]
    push    ax
    call    far ptr vector_to_point
    add     sp, 4
    mov     ax, [var_448]
    shl     ax, 1
    shl     ax, 1
    add     ax, bp
    mov     [var_B7C], ax
mov ax, var_B7C
    mov     bx, ax
    mov     ax, word ptr [var_574.px]
    cmp     [bx+offset var_vecarr2.vx], ax
    jnz     int16_t loc_255B4
    mov     ax, word ptr [var_574.py]
    cmp     [bx+offset var_vecarr2.y], ax
    jz      int16_t loc_255E6
loc_255B4:
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_255CB
    lea     ax, [var_574]
    push    ax
    ;push    cs
    call far ptr rect_compare_point
    add     sp, 2
    and     [var_ptrectflag], al

loc_255CB:
    les     bx, [var_transshapepolyinfoptptr]
    mov     ax, word ptr [var_574.px]
    mov     dx, word ptr [var_574.py]
    mov     es:[bx], ax
    mov     es:[bx+2], dx
loc_255DE:
    add     word ptr [var_transshapepolyinfoptptr], 4
    inc     word ptr [var_polyvertcounter]
}
*/

loc_255E6:
	var_448 = var_C;
	i++;
/*asm {
    mov     ax, [var_C]
    mov     [var_448], ax
    inc     si
}*/
loc_255EE:

	if (transshapenumvertscopy <= i) goto loc_25714;
	var_C = transshapeprimindexptr[0];
	transshapeprimindexptr++;

	var_18 += var_vecarr[var_C].z;

	if (var_vertflagtbl[var_C] != 0) goto loc_2553A;

	if (var_vertflagtbl[var_448] == 0) goto loc_256D7;
/*asm {
	
mov si, i
	
    mov     al, transshapenumvertscopy
    sub     ah, ah
    cmp     ax, si
    ja      int16_t loc_255FA
    jmp     loc_25714
loc_255FA:
    mov     bx, word ptr transshapeprimindexptr
    inc     word ptr transshapeprimindexptr
    mov     es, word ptr transshapeprimindexptr+2
    mov     al, es:[bx]
    mov     [var_C], ax

//mov ax, var_C
//mov si, i
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add     ax, bp
    mov     [var_polyvertunktabptr], ax

mov cx, var_C
    mov     ax, [var_polyvertunktabptr]
    mov     bx, ax
    mov     ax, [bx+offset var_vecarr.z]
    cwd
    add     word ptr [var_18], ax
    adc     word ptr [var_18+2], dx

mov cx, var_C
    mov     bx, cx
    add     bx, bp
    add bx, offset var_vertflagtbl
    mov al, [bx]
    cmp     al, 0 // issue
    jz      int16_t loc_25635
    jmp     loc_2553A

loc_25635:
    mov     bx, [var_448]
    add     bx, bp
    add bx, offset var_vertflagtbl
    mov al, [bx]
    cmp     al, 0 // issue
    jnz     int16_t loc_25645
    jmp     loc_256D7
loc_25645:

}
*/
// &var_vecarr[var_C] is equivalent to var_polyvertunktabptr-stuff+var_vecarr... becoz var_polyvertunktabptr is an array-indexer relative to the stack frame
	vector_op_unk(&var_vecarr[var_C], &var_vecarr[var_448], &var_vec2, 0x0C);
	vector_to_point(&var_vec2, &var_574);

	if (var_574.px == var_vecarr2[var_C].px && var_574.py == var_vecarr2[var_C].py) goto loc_256D7;

	if (var_ptrectflag != 0) {
		var_ptrectflag &= rect_compare_point(&var_574);
	}
	
	*var_transshapepolyinfoptptr = var_574;
	var_transshapepolyinfoptptr++;
	var_polyvertcounter++;
/*
asm {
    mov     ax, 0Ch
    push    ax
    lea     ax, [var_vec2]
    push    ax
    mov     ax, [var_448]
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    mov     ax, [var_polyvertunktabptr]
    ;sub     ax, 0B6Eh
    add ax, offset var_vecarr
    push    ax
    call    far ptr vector_op_unk
    add     sp, 8
    lea     ax, [var_574]
    push    ax
    lea     ax, [var_vec2]
    push    ax
    call    far ptr vector_to_point
    add     sp, 4
    mov     ax, [var_C]
    shl     ax, 1
    shl     ax, 1
    add     ax, bp
    mov     [var_B7C], ax // another wicked bp-relative indexer thingy
    
mov ax, var_B7C
    mov     bx, ax
    mov     ax, word ptr [var_574.px]
    cmp     [bx+offset var_vecarr2.vx], ax
    jnz     int16_t loc_256A5
    mov     ax, word ptr [var_574.py]
    cmp     [bx+offset var_vecarr2.y], ax
    jz      int16_t loc_256D7
loc_256A5:
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_256BC
    lea     ax, [var_574]
    push    ax
    ;push    cs
    call far ptr rect_compare_point
    add     sp, 2
    and     [var_ptrectflag], al
loc_256BC:
    les     bx, [var_transshapepolyinfoptptr]
    mov     ax, word ptr [var_574.px]
    mov     dx, word ptr [var_574.py]
    mov     es:[bx], ax
    mov     es:[bx+2], dx
    add     word ptr [var_transshapepolyinfoptptr], 4
    inc     word ptr [var_polyvertcounter]

}
*/
loc_256D7:
	*var_transshapepolyinfoptptr = *polyvertpointptrtab[i];
	if (var_ptrectflag != 0) {
		var_ptrectflag &= rect_compare_point(polyvertpointptrtab[i]);
	}
	goto loc_255DE;
/*asm {
mov si, i
    mov     ax, si
    shl     ax, 1
    add     ax, offset polyvertpointptrtab
    mov     [var_B7C], ax
    mov     bx, ax
    mov     bx, [bx]
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, [var_transshapepolyinfoptptr]
    mov     es:[bx], ax
    mov     es:[bx+2], dx
    cmp     byte ptr [var_ptrectflag], 0
    jnz     int16_t loc_25700
    jmp     loc_255DE
loc_25700:
    mov     bx, [var_B7C]
    push    word ptr [bx]
    ;push    cs
    call far ptr rect_compare_point
    add     sp, 2
    and     [var_ptrectflag], al
    jmp     loc_255DE
}*/
loc_25714:
	transshapenumvertscopy = var_polyvertcounter;
	
/*asm {
	mov si, i
	
    mov     al, byte ptr [var_polyvertcounter]
    mov     transshapenumvertscopy, al
}*/
loc_2571A:

	if (transshapenumvertscopy == 0) goto loc_25801;
	if (var_ptrectflag != 0) goto loc_25801;
	if ((var_primitiveflags & 1) != 0) goto loc_25760;
	if ((var_A & *var_cull2) != 0) goto loc_25760;
	
	if (is_facing_camera(transshapepolyinfo + 6) == 0) goto loc_25763;

/*asm {
    cmp     transshapenumvertscopy, 0
    jnz     int16_t loc_25724
    jmp     loc_25801
loc_25724:
    cmp     byte ptr [var_ptrectflag], 0
    jz      int16_t loc_2572E
    jmp     loc_25801
loc_2572E:
    test    byte ptr [var_primitiveflags], 1
    jnz     int16_t loc_25760
    les     bx, [var_cull2]
    mov     ax, es:[bx]
    mov     dx, es:[bx+2]
    and     ax, word ptr [var_A]
    and     dx, word ptr [var_A+2]
    or      dx, ax
    jnz     int16_t loc_25760
    mov     ax, word ptr transshapepolyinfo
    mov     dx, word ptr transshapepolyinfo+2
    add     ax, 6
    push    dx
    push    ax
    ;push    cs
    call far ptr is_facing_camera
    add     sp, 4
    or      al, al
    jz      int16_t loc_25763
}*/
loc_25760:
	var_4++;
/*asm {
    inc     word ptr [var_4]
}*/
loc_25763:

	if (var_4 == 0) goto loc_25801;
	if ((transshapeflags & 8) == 0) goto loc_25801;
	
	var_polyvertsptr = transshapepolyinfo + 6;
	var_polyvertcounter = 0;
	goto loc_257F7;

/*asm {
    cmp     word ptr [var_4], 0
    jnz     int16_t loc_2576C
    jmp     loc_25801
loc_2576C:
    test    transshapeflags, 8
    jnz     int16_t loc_25776
    jmp     loc_25801
loc_25776:
    mov     ax, word ptr transshapepolyinfo
    mov     dx, word ptr transshapepolyinfo+2
    add     ax, 6
    mov     word ptr [var_polyvertsptr], ax
    mov     word ptr [var_polyvertsptr+2], dx
    mov     word ptr [var_polyvertcounter], 0
    jmp     int16_t loc_257F7
}*/

loc_25790:

	var_polyvertX = var_polyvertsptr->px;
	var_polyvertY = var_polyvertsptr->py;
	var_polyvertsptr++;
	
	if (var_polyvertX < transshaperectptr->left) {
		transshaperectptr->left = var_polyvertX;
	}
	
	if (transshaperectptr->right < var_polyvertX + 1) {
		transshaperectptr->right = var_polyvertX + 1;
	}
	
	if (transshaperectptr->top > var_polyvertY) {
		transshaperectptr->top = var_polyvertY;
	}
	
	if (transshaperectptr->bottom < var_polyvertY + 1) {
		transshaperectptr->bottom = var_polyvertY + 1;
	}
	
	var_polyvertcounter++;

/*asm {
    les     bx, [var_polyvertsptr]
    mov     ax, es:[bx+.px] // x2 in point2d
    mov     [var_polyvertX], ax
    mov     ax, es:[bx+.py]
    mov     [var_polyvertY], ax
    add     word ptr [var_polyvertsptr], 4
    mov     bx, transshaperectptr
    mov     ax, [bx+.x1]
    cmp     [var_polyvertX], ax
    jge     int16_t loc_257BA
    mov     ax, [var_polyvertX]
    mov     [bx+.x1], ax
loc_257BA:
    mov     ax, [var_polyvertX]
    inc     ax
    mov     [var_B7C], ax
    mov     bx, transshaperectptr
    cmp     [bx+.y1], ax
    jge     int16_t loc_257CF
    mov     [bx+.y1], ax
loc_257CF:
    mov     bx, transshaperectptr
    mov     ax, [var_polyvertY]
    cmp     [bx+.x2], ax
    jle     int16_t loc_257DF
    mov     [bx+.x2], ax
loc_257DF:
    mov     ax, [var_polyvertY]
    inc     ax
    mov     [var_B7C], ax
    mov     bx, transshaperectptr
    cmp     [bx+.y2], ax
    jge     int16_t loc_257F4
    mov     [bx+.y2], ax
loc_257F4:
    inc     word ptr [var_polyvertcounter]
}*/
loc_257F7:
	if (var_polyvertcounter < transshapenumvertscopy) goto loc_25790;

/*asm {
    mov     al, transshapenumvertscopy
    sub     ah, ah
    cmp     [var_polyvertcounter], ax
    jb      int16_t loc_25790
}*/

// ------------------------------------ no primtype or unknown- skip this ------------------------------------

loc_25801:

	transshapeprimitives = transshapeprimptr;
	var_cull1++;
	var_cull2++;
	if (var_4 != 0) goto loc_25D3C;
	if ((var_primitiveflags & 2) != 0) goto loc_25E04;
loc_2582B:

	if ((transshapeprimitives[1] & 2) == 0) goto loc_25E04;
	transshapeprimitives += primidxcounttab[transshapeprimitives[0]] + transshapenumpaints + 2;
	var_cull1++;
	var_cull2++;
	goto loc_2582B;

/*asm {
    mov     ax, word ptr transshapeprimptr
    mov     dx, word ptr transshapeprimptr+2

    mov     word ptr transshapeprimitives, ax
    mov     word ptr transshapeprimitives+2, dx
    add     word ptr [var_cull2], 4
    add     word ptr [var_cull1], 4
    cmp     word ptr [var_4], 0
    jz      int16_t loc_25822
    jmp     loc_25D3C
loc_25822:
    test    byte ptr [var_primitiveflags], 2
    jz      int16_t loc_2582B
    jmp     loc_25E04
loc_2582B:

    les     bx, transshapeprimitives
    test    byte ptr es:[bx+1], 2
    jnz     int16_t loc_25839

    jmp     loc_25E04

loc_25839:

    mov     bl, es:[bx]
    sub     bh, bh
    mov     al, primidxcounttab[bx]
    sub     ah, ah
    add     ax, transshapenumpaints
    add     ax, 2
    add     word ptr transshapeprimitives, ax
    add     word ptr [var_cull1], 4
    add     word ptr [var_cull2], 4
    jmp     int16_t loc_2582B

// ------------------------------------ primtype_line ------------------------------------
}*/
_primtype_line:

	temp0 = transshapeprimitives[0];
	temp1 = transshapeprimitives[1];
	if (var_vertflagtbl[temp0] +var_vertflagtbl[temp1] == 2) {
		goto loc_25801;
	}
	if (var_vertflagtbl[temp0] == 0) {
		goto loc_258BC;
	}
	vector_op_unk(&var_vecarr[temp1], &var_vecarr[temp0], &var_vec2, 0x0C);
	temp = temp0;
	goto loc_258F6;

/*
asm {
    les     bx, transshapeprimitives
    mov     al, es:[bx]
    sub     ah, ah
    mov     si, ax
    mov     al, es:[bx+1]
    mov     di, ax
    mov     al, [di+var_vertflagtbl]
    cbw
    mov     cx, ax
    mov     al, [si+var_vertflagtbl]
    cbw
    add     ax, cx
    cmp     ax, 2
    jz      int16_t loc_25801
    cmp     byte ptr [si+var_vertflagtbl], 0
    jz      int16_t loc_258BC
    mov     ax, 0Ch
    push    ax
    lea     ax, [var_vec2]
    push    ax
    mov     ax, si
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    mov     ax, di
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    call    far ptr vector_op_unk
    add     sp, 8
    mov     ax, si
    jmp     int16_t loc_258F6
}
*/
loc_258BC:

	if (var_vertflagtbl[temp1] == 0) goto loc_2590D;
	vector_op_unk(&var_vecarr[temp0], &var_vecarr[temp1], &var_vec2, 0x0C);

	temp = temp1;
/*	
asm {
	
    les     bx, transshapeprimitives
    mov     al, es:[bx]
    sub     ah, ah
    mov     si, ax
    mov     al, es:[bx+1]
    mov     di, ax

	
	
    cmp     byte ptr [di+var_vertflagtbl], 0
    jz      int16_t loc_2590D
    mov     ax, 0Ch
    push    ax
    lea     ax, [var_vec2]
    push    ax
    mov     ax, di
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    mov     ax, si
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr
    ;add     ax, bp
    ;sub     ax, 0B6Eh
    push    ax
    call    far ptr vector_op_unk
    add     sp, 8
    mov     ax, di
}
*/
loc_258F6:

	vector_to_point(&var_vec2, &var_vecarr2[temp]);
/*
asm {
    shl     ax, 1
    shl     ax, 1
    add ax, bp
    add ax, offset var_vecarr2
    ;add     ax, bp
    ;sub     ax, 416h
    push    ax
    lea     ax, [var_vec2]
    push    ax
    call    far ptr vector_to_point
    add     sp, 4
}
*/
loc_2590D:
	
	// NOTE: when temp0 and temp1 were negative (ie bogus var_18), there was a sorting error with some of the wheels on the lamborghini LM-002
	var_18 = var_vecarr[temp0].z + var_vecarr[temp1].z;
	transshapepolyinfopts = transshapepolyinfo +6;
	transshapepolyinfopts[0] = *polyvertpointptrtab[0];
	transshapepolyinfopts[1] = *polyvertpointptrtab[1];

	if ((transshapeflags & 8) == 0) goto loc_25983;
	rect_adjust_from_point(polyvertpointptrtab[0], transshaperectptr);
	rect_adjust_from_point(polyvertpointptrtab[1], transshaperectptr);

/*asm {
	
	
    les     bx, transshapeprimitives
    mov     al, es:[bx]
    sub     ah, ah
    mov     si, ax
    mov     al, es:[bx+1]
    mov     di, ax


    mov     bx, si
    mov     ax, bx
    shl     bx, 1
    add     bx, ax
    shl     bx, 1
    add     bx, bp
    mov     ax, [bx+offset var_vecarr.z]
    mov     bx, di
    mov     cx, bx
    shl     bx, 1
    add     bx, cx
    shl     bx, 1
    add     bx, bp
    add     ax, [bx+offset var_vecarr.z]
    cwd
    mov     word ptr [var_18], ax
    mov     word ptr [var_18+2], dx

    mov     bx, polyvertpointptrtab
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+6], ax
    mov     es:[bx+8], dx
    mov     bx, polyvertpointptrtab+2
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+0Ah], ax
    mov     es:[bx+0Ch], dx
    test    transshapeflags, 8
    jz      int16_t loc_25983
    push    word ptr transshaperectptr
    push    word ptr polyvertpointptrtab
    call far ptr rect_adjust_from_point
    add     sp, 4
    push    word ptr transshaperectptr
    push    word ptr polyvertpointptrtab+2
// injected start
    call far ptr rect_adjust_from_point
    add     sp, 4
// injected end

loc_2597C:
    //call far ptr rect_adjust_from_point // been injected in respective calls
    //add     sp, 4
}*/

loc_25983:
	transshapenumvertscopy = 2;
/*asm {
    mov     transshapenumvertscopy, 2
}*/

loc_25988:
	var_4++;
	goto loc_25801;
/*asm {
    inc     word ptr [var_4]
    jmp     loc_25801
}*/
// ------------------------------------ primtype_wheel ------------------------------------

_primtype_wheel:

	if (var_1A != 0) goto loc_25801;
	
	transshapepolyinfopts = transshapepolyinfo +6;
	transshapepolyinfopts[0] = *polyvertpointptrtab[0];
	transshapepolyinfopts[1] = *polyvertpointptrtab[1];
	transshapepolyinfopts[2] = *polyvertpointptrtab[2];
	transshapepolyinfopts[3] = *polyvertpointptrtab[3];

	if (is_facing_camera(transshapepolyinfopts) != 0) goto loc_25A7C;

	transshapepolyinfopts[0] = *polyvertpointptrtab[3];
	transshapepolyinfopts[1] = *polyvertpointptrtab[4];
	transshapepolyinfopts[2] = *polyvertpointptrtab[5];
	transshapepolyinfopts[3] = *polyvertpointptrtab[0];

	var_18 = var_vecarr[transshapeprimitives[3]].z << 2;
	goto loc_25A9E;
/*	
asm {
    cmp     word ptr [var_1A], 0
    jz      int16_t loc_25997
    jmp     loc_25801
loc_25997:
    mov     bx, polyvertpointptrtab
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+6], ax
    mov     es:[bx+8], dx
    mov     bx, polyvertpointptrtab+2
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+0Ah], ax
    mov     es:[bx+0Ch], dx
    mov     bx, polyvertpointptrtab+4
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+0Eh], ax
    mov     es:[bx+10h], dx
    mov     bx, polyvertpointptrtab+6
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+12h], ax
    mov     es:[bx+14h], dx
    mov     ax, word ptr transshapepolyinfo
    mov     dx, word ptr transshapepolyinfo+2
    add     ax, 6
    push    dx
    push    ax
    call far ptr is_facing_camera
    add     sp, 4
    or      al, al
    jnz     int16_t loc_25A7C
    mov     bx, polyvertpointptrtab+6
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+6], ax
    mov     es:[bx+8], dx
    mov     bx, polyvertpointptrtab+8
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+0Ah], ax
    mov     es:[bx+0Ch], dx
    mov     bx, polyvertpointptrtab+0Ah
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+0Eh], ax
    mov     es:[bx+10h], dx
    mov     bx, polyvertpointptrtab
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+12h], ax
    mov     es:[bx+14h], dx
    les     bx, transshapeprimitives
    mov     al, es:[bx+3]   // primitives+3 = paintjob 1? [0..x]
    sub     ah, ah
    mov     bx, ax
    shl     bx, 1
    add     bx, ax
    shl     bx, 1
    add     bx, bp
    mov     ax, [bx+offset var_vecarr.z]
    cwd
    mov     cl, 2
loc_25A71:
    shl     ax, 1
    rcl     dx, 1
    dec     cl
    jnz     int16_t loc_25A71
    jmp     int16_t loc_25A9E
}*/
loc_25A7C:
	var_18 = var_vecarr[transshapeprimitives[0]].z << 2;
/*asm {
    les     bx, transshapeprimitives
    mov     al, es:[bx]     // primitives+0 = primitivetype
    sub     ah, ah
    mov     bx, ax
    shl     bx, 1
    add     bx, ax
    shl     bx, 1
    add     bx, bp
    mov     ax, [bx+offset var_vecarr.z]
    cwd
    mov     cl, 2
loc_25A96:
    shl     ax, 1
    rcl     dx, 1
    dec     cl
    jnz     int16_t loc_25A96
}*/
loc_25A9E:

	transshapepolyinfopts = transshapepolyinfo +6;
	temp = polarRadius2D(transshapepolyinfopts[0].px - transshapepolyinfopts[1].px, transshapepolyinfopts[0].py - transshapepolyinfopts[1].py);
	temp1 = polarRadius2D(transshapepolyinfopts[0].px - transshapepolyinfopts[2].px, transshapepolyinfopts[0].py - transshapepolyinfopts[2].py);
	
	if (temp1 > temp) temp = temp1;

	if ((transshapeflags & 8) == 0) goto loc_25B9C;
	
	var_450.px = transshapepolyinfopts[0].px - temp - 1;
	var_450.py = transshapepolyinfopts[0].py - temp - 1;
	rect_adjust_from_point(&var_450, transshaperectptr);

	var_450.px = transshapepolyinfopts[0].px + temp + 1;
	var_450.py = transshapepolyinfopts[0].py + temp + 1;
	rect_adjust_from_point(&var_450, transshaperectptr);
	
	var_450.px = transshapepolyinfopts[3].px - temp - 1;
	var_450.py = transshapepolyinfopts[3].py - temp - 1;
	rect_adjust_from_point(&var_450, transshaperectptr);

	var_450.px = transshapepolyinfopts[3].px + temp + 1;
	var_450.py = transshapepolyinfopts[3].py + temp + 1;
	rect_adjust_from_point(&var_450, transshaperectptr);

/*
asm {
    mov     word ptr [var_18], ax
    mov     word ptr [var_18+2], dx
    les     bx, transshapepolyinfo
    mov     ax, es:[bx+8]
    sub     ax, es:[bx+0Ch]
    push    ax
    mov     ax, es:[bx+6]
    sub     ax, es:[bx+0Ah]
    push    ax
    call    far ptr polarRadius2D
    add     sp, 4
    mov     si, ax
    les     bx, transshapepolyinfo
    mov     ax, es:[bx+8]
    sub     ax, es:[bx+10h]
    push    ax
    mov     ax, es:[bx+6]
    sub     ax, es:[bx+0Eh]
    push    ax
    call    far ptr polarRadius2D
    add     sp, 4
    mov     di, ax
    cmp     di, si
    jle     int16_t loc_25AEA
    mov     si, di
loc_25AEA:
    test    transshapeflags, 8
    jnz     int16_t loc_25AF4
    jmp     loc_25B9C
loc_25AF4:
    les     bx, transshapepolyinfo
    mov     ax, es:[bx+6]
    sub     ax, si
    dec     ax
    mov     word ptr [var_450+.vx], ax
    mov     ax, es:[bx+8]
    sub     ax, si
    dec     ax
    mov     word ptr [var_450+.vy], ax
    push    word ptr transshaperectptr
    lea     ax, [var_450]
    push    ax
    call far ptr rect_adjust_from_point
    add     sp, 4
    les     bx, transshapepolyinfo
    mov     ax, es:[bx+8]
    add     ax, si
    inc     ax
    mov     word ptr [var_450+.vy], ax
    mov     ax, es:[bx+6]
    add     ax, si
    inc     ax
    mov     word ptr [var_450+.vx], ax
    push    word ptr transshaperectptr
    lea     ax, [var_450]
    push    ax
    call far ptr rect_adjust_from_point
    add     sp, 4
    les     bx, transshapepolyinfo
    mov     ax, es:[bx+12h]
    sub     ax, si
    dec     ax
    mov     word ptr [var_450+.vx], ax
    mov     ax, es:[bx+14h]
    sub     ax, si
    dec     ax
    mov     word ptr [var_450+.vy], ax
    push    word ptr transshaperectptr
    lea     ax, [var_450]
    push    ax
    call far ptr rect_adjust_from_point
    add     sp, 4
    les     bx, transshapepolyinfo
    mov     ax, es:[bx+14h]
    add     ax, si
    inc     ax
    mov     word ptr [var_450+.vy], ax
    mov     ax, es:[bx+12h]
    add     ax, si
    inc     ax
    mov     word ptr [var_450+.vx], ax

    push    word ptr transshaperectptr
    lea     ax, [var_450]
    push    ax
    ;push    cs
    call far ptr rect_adjust_from_point   // denne fucker opppppp litt inni
    add     sp, 4
}*/
loc_25B9C:
	transshapenumvertscopy = 4;
	var_4 = 1;
	goto loc_25801;

/*asm {
    mov     transshapenumvertscopy, 4
    mov     word ptr [var_4], 1
    jmp     loc_25801

}*/

// ------------------------------------ primtype_sphere ------------------------------------
_primtype_sphere:

	temp0 = transshapeprimitives[0];
	temp1 = transshapeprimitives[1];
//fatal_error("anders: %i %i", temp0, temp1);
	var_18 = var_vecarr[temp0].z + var_vecarr[temp1].z;
	if (var_vertflagtbl[temp0] + var_vertflagtbl[temp1] != 0) goto loc_25801;

	transshapepolyinfopts = transshapepolyinfo +6;
	transshapepolyinfopts[0] = *polyvertpointptrtab[0];
	var_vec3 = var_vecarr[temp0];
	var_vec4 = var_vecarr[temp1];

	var_vec2.x = var_vec3.x - var_vec4.x;
	var_vec2.y = var_vec3.y - var_vec4.y;
	var_vec2.z = var_vec3.z - var_vec4.z;
	var_462 = projectiondata9_times_ratio(polarRadius3D(&var_vec2), var_vec3.z);
	transshapepolyinfopts[1].px = var_462;
	if ((transshapeflags & 8) == 0) goto loc_25983;

	var_450.py = polyvertpointptrtab[0]->py - var_462;
	var_450.px = polyvertpointptrtab[0]->px - var_462;

	rect_adjust_from_point(&var_450, transshaperectptr);

	var_450.py = polyvertpointptrtab[0]->py + var_462;
	var_450.px = polyvertpointptrtab[0]->px + var_462;

	rect_adjust_from_point(&var_450, transshaperectptr);
	goto loc_25983;
/*	
asm {
    les     bx, transshapeprimitives
    mov     al, es:[bx]
    sub     ah, ah
    mov     si, ax
    mov     al, es:[bx+1]
    mov     di, ax
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add     ax, bp
    mov     [var_B7C], ax
    mov     ax, si
    mov     cx, ax
    shl     ax, 1
    add     ax, cx
    shl     ax, 1
    add     ax, bp
    mov     [var_polyvertunktabptr], ax
    mov     bx, ax
    mov     ax, [bx+offset var_vecarr.z]
    mov     bx, [var_B7C]
    add     ax, [bx+offset var_vecarr.z]
    cwd
    mov     word ptr [var_18], ax
    mov     word ptr [var_18+2], dx
    mov     al, [di+var_vertflagtbl]
    cbw
    mov     cx, ax
    mov     al, [si+var_vertflagtbl]
    cbw
    add     ax, cx
    jz      int16_t loc_25C01
    jmp     loc_25801
loc_25C01:
    mov     bx, polyvertpointptrtab
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+6], ax
    mov     es:[bx+8], dx
    mov     bx, [var_polyvertunktabptr]
    push    si
    push    di
    lea     di, [var_vec3]
    lea     si, [bx+offset var_vecarr]
    push    ss
    pop     es
    movsw
    movsw
    movsw
    pop     di
    pop     si
    mov     bx, [var_B7C]
    push    si
    push    di
    lea     di, [var_vec4]
    lea     si, [bx + offset var_vecarr]
    //lea     si, [bx-0B6Eh]
    movsw
    movsw
    movsw
    pop     di
    pop     si

    mov     ax, word ptr [var_vec3.vx]
    sub     ax, word ptr [var_vec4.vx]
    mov     word ptr [var_vec2.vx], ax

    mov     ax, word ptr [var_vec3.y]
    sub     ax, word ptr [var_vec4.y]
    mov     word ptr [var_vec2.y], ax

    mov     ax, word ptr [var_vec3.z]
    sub     ax, word ptr [var_vec4.z]
    mov     word ptr [var_vec2.z], ax

    push    word ptr [var_vec3.z]
    lea     ax, [var_vec2]
    push    ax
    call    far ptr polarRadius3D
    add     sp, 2
    push    ax
    call    far ptr projectiondata9_times_ratio
    add     sp, 4
    mov     [var_462], ax
    
    les     bx, transshapepolyinfo
    mov     es:[bx+0Ah], ax
    test    transshapeflags, 8
    jnz     int16_t loc_25C92
    jmp     loc_25983
loc_25C92:

    mov     bx, polyvertpointptrtab
    mov     ax, [bx+2]
    sub     ax, [var_462]
    mov     word ptr [var_450+.vy], ax
    mov     ax, [bx]
    sub     ax, [var_462]
    mov     word ptr [var_450+.vx], ax
    push    word ptr transshaperectptr
    lea     ax, [var_450]
    push    ax
    ;push    cs
    call far ptr rect_adjust_from_point
    add     sp, 4
    mov     ax, [var_462]
    mov     bx, polyvertpointptrtab
    add     ax, [bx]
    mov     word ptr [var_450+.vx], ax
    mov     ax, [bx+2]
    add     ax, [var_462]
    mov     word ptr [var_450+.vx], ax
    push    word ptr transshaperectptr
    lea     ax, [var_450]
    push    ax
// injected start
    call far ptr rect_adjust_from_point
    add     sp, 4
// injected end
    jmp     loc_25983
}
*/
// ------------------------------------ primtype 5 - unknown / particle? ------------------------------------

loc_25CE0:
	fatal_error("unhandled primitive type 5");
	goto loc_25988; // var4++ | goto 25801
/*asm {
    les     bx, transshapeprimitives
    mov     al, es:[bx]     // primitives+ 0 = type
    sub     ah, ah
    mov     si, ax
    cmp     [si+var_vertflagtbl], ah
    jz      int16_t loc_25CF4
    jmp     loc_25801
loc_25CF4:
    mov     bx, si
    shl     bx, 1
    add     bx, ax
    shl     bx, 1
    add     bx, bp
    mov     ax, [bx+offset var_vecarr.z]
    cwd
    mov     word ptr [var_18], ax
    mov     word ptr [var_18+2], dx
    mov     bx, polyvertpointptrtab
    mov     ax, [bx]
    mov     dx, [bx+2]
    les     bx, transshapepolyinfo
    mov     es:[bx+6], ax
    mov     es:[bx+8], dx
    test    transshapeflags, 8
    jz      int16_t loc_25D34
    push    word ptr transshaperectptr
    push    word ptr polyvertpointptrtab
    ;push    cs
    call far ptr rect_adjust_from_point
    add     sp, 4
loc_25D34:
    mov     transshapenumvertscopy, 1
    jmp     loc_25988
}    */


// ------------------------------------ jumps here from inside the loc_25801-case if var_4 != 0------------------------------------

loc_25D3C:
	var_45E++;
	transshapepolyinfo[3] = transshapenumvertscopy;
	transshapepolyinfo[4] = var_primtype;
	if (transprimitivepaintjob == 0x2D) {
		transshapepolyinfo[2] = backlights_paint_override;
	} else {
		transshapepolyinfo[2] = transprimitivepaintjob;
	}
	
	if (transshapenumvertscopy == 1) {
		// goto loc_25D9C;
		temp0 = var_18;
	} else 
	if (transshapenumvertscopy == 2) {
		//goto loc_25DB8;
		temp0 = var_18 >> 1;
	} else
	if (transshapenumvertscopy == 4) {
		//goto loc_25DC6;
		temp0 = var_18 >> 2;
	} else
	if (transshapenumvertscopy == 8) {
		// goto loc_25DDA;
		temp0 = var_18 >> 3;
	} else {
		temp0 = var_18 / transshapenumvertscopy;
	}
	
	((uint16_t far*)transshapepolyinfo)[0] = temp0;
	
	
	if ((transshapeflags & 1) != 0 || (var_primitiveflags & 2) != 0) {
		temp = 0;
	} else
		temp = 1;

	word_40ECE = insert_newest_poly_in_poly_linked_list_40ED6(temp0, temp);
	
	if (word_40ECE == 0) goto loc_25E04;
	return 1;

/*	
asm {
    inc     word ptr [var_45E]
    les     bx, transshapepolyinfo
    mov     al, transshapenumvertscopy
    mov     es:[bx+3], al   // polyinfo+3 = numverts
    les     bx, transshapepolyinfo
    mov     al, [var_primtype]
    mov     es:[bx+4], al   // polyinfo+4 = primtype
    cmp     transprimitivepaintjob, 2Dh // '-'
    jnz     int16_t loc_25D66
    les     bx, transshapepolyinfo
    mov     al, byte_45514
    jmp     int16_t loc_25D6D
loc_25D66:
    les     bx, transshapepolyinfo
    mov     al, transprimitivepaintjob
loc_25D6D:
    mov     es:[bx+2], al   // polyinfo+2 = paintjob
    mov     al, transshapenumvertscopy
    sub     ah, ah
    cmp     ax, 1
    jz      int16_t loc_25D9C
    cmp     ax, 2
    jz      int16_t loc_25DB8
    cmp     ax, 4
    jz      int16_t loc_25DC6
    cmp     ax, 8
    jz      int16_t loc_25DDA
    
    mov     al, transshapenumvertscopy
    sub     ah, ah

    sub     cx, cx
    push    cx
    push    ax
    push    word ptr [var_18+2]
    push    word ptr [var_18]
    call    far ptr __aFuldiv
    jmp     int16_t loc_25DC2
}
loc_25D9C:
asm {
    mov     si, word ptr [var_18]

loc_25D9F:

    les     bx, transshapepolyinfo
    mov     es:[bx], si     // polyinfo+0 = ???

    test    transshapeflags, 1
    jnz     int16_t loc_25DB3
    test    byte ptr [var_primitiveflags], 2
    jz      int16_t loc_25DEE
loc_25DB3:
    sub     ax, ax
    jmp     int16_t loc_25DF1
}

loc_25DB8:
asm {
    mov     ax, word ptr [var_18]
    mov     dx, word ptr [var_18+2]
    sar     dx, 1
    rcr     ax, 1
loc_25DC2:
    mov     si, ax
    jmp     int16_t loc_25D9F
}
loc_25DC6:
asm {
    mov     ax, word ptr [var_18]
    mov     dx, word ptr [var_18+2]
    mov     cl, 2
loc_25DCE:
    or      cl, cl
    jz      int16_t loc_25DC2
    sar     dx, 1
    rcr     ax, 1
    dec     cl
    jmp     int16_t loc_25DCE
}
loc_25DDA:
asm {
    mov     ax, word ptr [var_18]
    mov     dx, word ptr [var_18+2]
    mov     cl, 3
loc_25DE2:
    or      cl, cl
    jz      int16_t loc_25DC2
    sar     dx, 1
    rcr     ax, 1
    dec     cl
    jmp     int16_t loc_25DE2
    
loc_25DEE:
    mov     ax, 1
loc_25DF1:
    push    ax
    push    si
    call far ptr insert_newest_poly_in_poly_linked_list_40ED6
    add     sp, 4
    mov     word_40ECE, ax
    or      ax, ax
    jz      int16_t loc_25E04
    
    //jmp     loc_24EAE // inject:
//loc_24EAE:
    mov     ax, 1

    jmp the_end
}
*/

loc_25E04:

	if (transshapeprimitives[0] != 0) goto loc_25233;

	if (var_45E != 0) return 0;
	return -1;
/*	
asm {
    les     bx, transshapeprimitives
    cmp     byte ptr es:[bx], 0
    jz      int16_t _transform_done
    jmp     loc_25233


_transform_done:
    cmp     word ptr [var_45E], 0
    jnz     int16_t _done_ret_0

    //jmp     _done_ret_neg1
    mov     ax, 0FFFFh
    jmp the_end

_done_ret_0:
    sub     ax, ax
    //pop     si
    //pop     di
    jmp the_end
    retf

}

the_end:
asm {
	mov result, ax
}
//fatal_error("result %i", result);
	//fatal_error("%i %i %i %i", transshaperectptr->x1, transshaperectptr->y1, transshaperectptr->x2, transshaperectptr->y2);
	//return result;
return result;
*/
}




// parameter points to a far array of 2d points
char is_facing_camera(struct POINT2D far* pts) {
	int32_t dx0, dy0, dx1, dy1;
	int32_t temp;

	dx0 = (int32_t)pts[0].px - pts[1].px;
	dx1 = (int32_t)pts[2].px - pts[1].px;
	
	if (dx0 == 0 && dx1 == 0) return 0;
		
	dy0 = (int32_t)pts[0].py - pts[1].py;
	dy1 = (int32_t)pts[2].py - pts[1].py;

	if (dy0 == 0 && dy1 == 0) return 0;
	temp = (dx1 * dy0) - (dx0 * dy1);
	return temp <= 0 ? 0 : 1;
}

extern uint16_t projectiondata1;
extern uint16_t projectiondata2;
extern uint16_t projectiondata3;
extern uint16_t projectiondata4;
extern uint16_t projectiondata5;
extern uint16_t projectiondata6;
extern uint16_t projectiondata7;
extern uint16_t projectiondata8;
extern uint16_t projectiondata9;
extern uint16_t projectiondata10;

uint16_t projectiondata9_times_ratio(uint16_t i1, int16_t i2) {
	return projectiondata9 * i1 / i2;
}

extern int16_t poly_linked_list_40ED6[];

extern uint16_t insert_newest_poly_in_poly_linked_list_40ED6(uint16_t arg_0, uint16_t arg_2) {
	uint16_t result;
	int16_t regdi, regsi, regax;

	//return ported_insert_newest_poly_in_poly_linked_list_40ED6_(arg_0, arg_2);

	if (arg_2 == 0) {
		regdi = poly_linked_list_40ED6[poly_linklist_40ED6_iter4];
	} else {
		poly_linklist_40ED6_iter4 = poly_linklist_40ED6_iter1;
		regdi = poly_linked_list_40ED6[poly_linklist_40ED6_iter1];
		regsi = poly_linklist_40ED6_iter3;

		while (regdi >= 0) {
			regax = regsi;
			regsi--;
			if (regax == 0) break;
			if (polyinfoptrs[regdi][0] < (int16_t)arg_0) break;
			poly_linklist_40ED6_iter4 = regdi;
			regdi = poly_linked_list_40ED6[regdi];
		}
	}

	poly_linked_list_40ED6[polyinfonumpolys] = regdi;
	poly_linked_list_40ED6[poly_linklist_40ED6_iter4] = polyinfonumpolys;
	poly_linklist_40ED6_iter3++;
	if (regdi < 0) {
		poly_linklist_40ED6_iter2 = polyinfonumpolys;
	}
	poly_linklist_40ED6_iter4 = poly_linked_list_40ED6[poly_linklist_40ED6_iter4];
	polyinfonumpolys++;
	polyinfoptrnext += (transshapenumvertscopy * sizeof(struct POINT2D)) + 6; // TODO: sizeof POINT2D?
	if (polyinfonumpolys == 0x190) return 1;
	if (polyinfoptrnext <= 0x2872) return 0;
	return 1;
}

void set_projection(int16_t i1, int16_t i2, int16_t i3, int16_t i4) {
	
	projectiondata1 = (((int32_t)i1 << 11) / 0x168) >> 1;
	projectiondata2 = (((int32_t)i2 << 11) / 0x168) >> 1;
	projectiondata3 = i3 >> 1;
	projectiondata5 = projectiondata3 + projectiondata4;
	projectiondata6 = i4 >> 1;
	projectiondata8 = projectiondata6 + projectiondata7;
	projectiondata9 = (int32_t)cos_fast(projectiondata1) * projectiondata3 / sin_fast(projectiondata1);

	if (projectiondata2 != 0) {
		projectiondata10 = (int32_t)cos_fast(projectiondata2) * projectiondata6 / sin_fast(projectiondata2);
	} else {
		projectiondata10 = projectiondata9 - (projectiondata9 >> 3) - (projectiondata9 >> 4);
		projectiondata2 = polarAngle(projectiondata10, projectiondata6);
	}
	
}

extern struct RECTANGLE select_rect_rc;
//extern uint16_t word_411F6;
extern struct MATRIX mat_y0, mat_y100, mat_y200, mat_y300;
extern int32_t sin80, cos80, sin80_2, cos80_2;

uint16_t select_cliprect_rotate(int16_t angZ, int16_t angX, int16_t angY, struct RECTANGLE* cliprect, int16_t unk) {
	struct MATRIX* matptr;
	struct VECTOR vec, vec2;

	//return ported_select_cliprect_rotate_(angX, angY, angZ, cliprect, unk);
	
	mat_temp = *mat_rot_zxy(angZ, angX, angY, 1);
	polyinfo_reset();
	select_rect_rc = *cliprect;
	select_rect_param = unk;
	matptr = mat_rot_zxy(-angZ, -angX, -angY, 0);
	vec.z = 0x2710;
	vec.y = 0;
	vec.x = 0;
	mat_mul_vector(&vec, matptr, &vec2);
	return polarAngle(vec2.x, vec2.z) & 0x3FF;
}

void polyinfo_reset(void) {
	polyinfonumpolys = 0;
	polyinfoptrnext = 0;
	word_40ECE = 0;
	poly_linked_list_40ED6[0x190] = 0xFFFF;
	poly_linklist_40ED6_iter2 = 0x190;
}

void calc_sincos80(void) {
	sin80 = sin_fast(0x80);
	cos80 = cos_fast(0x80);
	sin80_2 = sin_fast(0x80);
	cos80_2 = cos_fast(0x80);
}

void init_polyinfo(void) {
	/* 0x28A0 is sized for 320x200. A larger framebuffer generates more spans
	 * per polygon, so the buffer has to grow with the pixel count or the edge
	 * lists run past the end - which shows up as long horizontal streaks. */
	polyinfoptr = mmgr_alloc_resbytes("polyinfo", 0x28A0L * RFB_SCALE * RFB_SCALE);
	
	mat_rot_y(&mat_y0, 0);
	mat_rot_y(&mat_y100, 0x100);
	mat_rot_y(&mat_y200, 0x200);
	mat_rot_y(&mat_y300, 0x300);
	calc_sincos80();
}

#if 0
void get_a_poly_info(void) {

	int16_t counter;
	int16_t materialtype; // var_6
	int16_t materialcolor; // var_8
	int16_t materialpattern;
	int16_t objtype;
	int16_t somecount; // var_2
	uint16_t regdi;
	uint8_t far* polyinfoptr;
	int16_t far* polyptr;
	int16_t i;
	int16_t var_32[256];

	return ported_get_a_poly_info_();
/*
	regdi = 0x190;
	counter = 0;
	while (counter < polyinfonumpolys) {
		regdi = poly_linked_list_40ED6[regdi];
		polyinfoptr = polyinfoptrs[regdi];
		materialtype = polyinfoptr[2];

		materialcolor = material_clrlist_ptr_cpy[materialtype];
		objtype = polyinfoptr[4];

		if (objtype == 0) {
			somecount = polyinfoptr[3];
			polyptr = polyinfoptr + 6;
			
			for (i = 0; i < somecount; i++) {
				var_32[i * 2 + 0] = polyptr[i * 2 + 0];
				var_32[i * 2 + 1] = polyptr[i * 2 + 1];
			}
			materialpattern = material_patlist_ptr_cpy[materialtype];
			if (materialpattern == 0) {
				for (i = 0; i < somecount; i++) {
					printf("count: %i color: %i, %i, %i\n", somecount, materialcolor, var_32[i * 2 + 0], var_32[i * 2 + 1]);
				}
				//printf("somecount %i @ %i", somecount, polyinfonumpolys);
				preRender_default(materialcolor, somecount, var_32);
			} else
			if (materialpattern == 1) {
				// fill poatterned
			} else {
				// fill unk
			}
		} else
		if (objtype == 1) {
			// goto fill_solid
		} else
		if (objtype == 2) {
			// goto fill_sphere
		} else
		if (objtype == 3) {
			// goto fill_wheel0
		} else
		if (objtype == 5) {
			// goto fill_next
		} else {
			// goto fill_pixel
		}
		
		counter++;
	}
	
	polyinfo_reset(); // polyinfo_reset();
*/
}

#endif

/* Blitter prototypes now supplied by rport.h (native implementations in
 * rblit.c). */
extern uint16_t draw_line_related(uint16_t, uint16_t, uint16_t, uint16_t, int16_t*);
extern uint16_t draw_line_related_alt(uint16_t, uint16_t, uint16_t, uint16_t, int16_t*);
void generate_poly_edges(int16_t* var_18, int16_t* regsi, int16_t mode);
void preRender_default_impl_helper(int16_t* regsi, uint16_t var_A, uint16_t var_C, int16_t* var_18);

extern void (*spritefunc)(int16_t*, int16_t*, uint16_t, uint16_t, uint16_t);
extern void (*imagefunc)(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);

extern struct SPRITE far sprite1; // seg012
extern struct SPRITE far sprite2; // seg012

void preRender_default_impl(uint16_t arg_color, uint16_t arg_vertlinecount, int16_t* arg_vertlines, uint16_t var_A);

void preRender_default_alt(uint16_t arg_color, uint16_t arg_vertlinecount, uint16_t* arg_vertlines) {
	//return ported_preRender_default_alt_(arg_color, arg_vertlinecount, arg_vertlines);

	spritefunc = &draw_filled_lines;
	imagefunc = &preRender_line;
	preRender_default_impl(arg_color, arg_vertlinecount, arg_vertlines, 0);
}

void preRender_default(uint16_t arg_color, uint16_t arg_vertlinecount, uint16_t* arg_vertlines) {
	//return ported_preRender_default_(arg_color, arg_vertlinecount, arg_vertlines);

	spritefunc = &draw_filled_lines;
	imagefunc = &preRender_line;
	preRender_default_impl(arg_color, arg_vertlinecount, arg_vertlines, 1);
}

void skybox_op_helper(uint16_t arg_color, uint16_t arg_vertlinecount, struct POINT2D arg_vertlines[]) {
	//return ported_skybox_op_helper_(arg_color, arg_vertlinecount, &arg_vertlines);

	spritefunc = &draw_filled_lines;
	imagefunc = &preRender_line;
	preRender_default_impl(arg_color, arg_vertlinecount, (int16_t*)arg_vertlines, 1); /* native: was &arg_vertlines (DOS by-value stack trick) */
}

void preRender_wheel_helper4(uint16_t arg_color, uint16_t arg_vertlinecount, struct POINT2D arg_vertlines[]) {
	//return ported_preRender_wheel_helper4_(arg_color, arg_vertlinecount, &arg_vertlines);

	spritefunc = &draw_filled_lines;
	imagefunc = &preRender_line;
	preRender_default_impl(arg_color, arg_vertlinecount, (int16_t*)arg_vertlines, 0); /* native: was &arg_vertlines */
}


extern uint16_t word_4031E;
extern uint16_t word_40320;

void preRender_unk(uint16_t arg_color, uint16_t unk, uint16_t arg_vertlinecount, struct POINT2D* arg_vertlines, uint16_t unk2) {
	fatal_error("untested - what causes this?");
	spritefunc = &draw_unknown_lines;
	imagefunc = &preRender_line;

	word_4031E = unk;
	word_40320 = unk2;
	preRender_default_impl(arg_color, arg_vertlinecount, (int16_t*)arg_vertlines, 1); /* native: was &arg_vertlines */
}

void preRender_patterned(uint16_t unk, uint16_t arg_color, uint16_t arg_vertlinecount, struct POINT2D* arg_vertlines) {
	//return ported_preRender_patterned_(unk, arg_color, arg_vertlinecount, arg_vertlines);

	spritefunc = &draw_patterned_lines;
	imagefunc = &preRender_line;
	word_4031E = unk;
	
	preRender_default_impl(arg_color, arg_vertlinecount, arg_vertlines, 0);
}

void preRender_default_impl(uint16_t arg_color, uint16_t arg_vertlinecount, int16_t* arg_vertlines, uint16_t var_A) {
	uint16_t var_798[RFB_SPANROWS + RFB_SPANROWS];
	uint8_t var_7D0[56];

	uint16_t* var_18;
	uint16_t* var_16;
	uint16_t* var_14;
	uint16_t* var_10;
	int16_t var_E, var_12;
	uint16_t var_C;
	uint16_t* var_8;
	int16_t var_4, var_2;

	int16_t* var_vertlineptr;
	int16_t minx, maxx, i;
	int16_t temp0x, temp0y, temp1x, temp1y;

	int16_t sprite1_sprite_left2 = sprite1.sprite_left2;
	int16_t sprite1_sprite_widthsum = sprite1.sprite_widthsum;
	int16_t sprite1_sprite_top = sprite1.sprite_top;
	int16_t sprite1_sprite_height = sprite1.sprite_height;
	
	var_vertlineptr = arg_vertlines;
	var_8 = var_vertlineptr + ((arg_vertlinecount - 1) << 1); // asm does shl 2 for byte-offset - points at end of vertptr
	var_18 = &var_798;
	var_2 = sprite1_sprite_left2;
	var_4 = sprite1_sprite_widthsum - 1;
	var_12 = var_E = var_vertlineptr[1];
	maxx = minx = var_vertlineptr[0];
	var_10 = arg_vertlines;
	var_14 = arg_vertlines;
	if (arg_vertlinecount - 1 == 0) {
		fatal_error("untested imagefunc");
		imagefunc(var_vertlineptr[0], var_vertlineptr[1], var_vertlineptr[0], var_vertlineptr[0], arg_color);
		return ;
	}

	for (i = 1; i < arg_vertlinecount; i++) {
		if (arg_vertlines[i * 2 + 1] <= var_E) {
			var_E = arg_vertlines[i*2 + 1];
			var_10 = &arg_vertlines[i * 2];
		}
		if (arg_vertlines[i * 2 + 1] > var_12) {
			var_12 = arg_vertlines[i * 2 + 1];
			var_14 = &arg_vertlines[i * 2];
		}
		
		if (arg_vertlines[i * 2 + 0] < minx) {
			minx = arg_vertlines[i * 2 + 0];
		}
		if (arg_vertlines[i * 2 + 0] > maxx) {
			maxx = arg_vertlines[i * 2 + 0];
		}
		
	}

	if (maxx < var_2) return;
	if (minx >= var_4) return ;
	if (var_12 < sprite1_sprite_top) return ;
	if (var_E >= sprite1_sprite_height) return ;
	var_C = 0;

	if (maxx > var_4 || minx < var_2 || var_12 >= sprite1_sprite_height || var_E < sprite1_sprite_top) {
		var_C = 1;
	}
	if (var_12 == var_E || maxx == minx) {
		imagefunc(minx, var_E, maxx, var_12, arg_color);
		return ;
	}

	var_16 = var_10;
	
	do {
		temp0x = var_16[0];
		temp0y = var_16[1];
		var_16+=2;
		if (var_16 > var_8)
			var_16 = var_vertlineptr;
		
		temp1x = var_16[0];
		temp1y = var_16[1];
		if (temp1y > temp0y) {
			
			if (var_C != 0) {
				draw_line_related(temp0x, temp0y, temp1x, temp1y, var_7D0);
				generate_poly_edges(var_18, var_7D0, 0);
			} else {
				draw_line_related_alt(temp0x, temp0y, temp1x, temp1y, var_7D0);
				generate_poly_edges(var_18, var_7D0, 1);
			}
		}
		
	} while (var_16 != var_14);

	var_16 = var_10;
	do {
		temp0x = var_16[0];
		temp0y = var_16[1];
		var_16-=2;
		if (var_16 < var_vertlineptr)
			var_16 = var_8;
		temp1x = var_16[0];
		temp1y = var_16[1];
		if (temp1y > temp0y) {
			
			if (var_C != 0) {
				draw_line_related(temp0x, temp0y, temp1x, temp1y, var_7D0);
			} else {
				draw_line_related_alt(temp0x, temp0y, temp1x, temp1y, var_7D0);
			}
			preRender_default_impl_helper(var_7D0, var_A, var_C, var_18);
		}
	} while (var_16 != var_14);

	temp0y = var_12;

	if (temp0y >= sprite1_sprite_height) 
		temp0y = sprite1_sprite_height - 1;
	temp1y = var_E;
	if (temp1y < sprite1_sprite_top)
		temp1y = sprite1_sprite_top;
	
	temp0x = temp0y - temp1y;
	if (temp0x <= 0) return ;
	temp0x++;
	
	spritefunc(&var_798[temp1y], &var_798[RFB_SPANROWS + temp1y], temp1y, temp0x, arg_color);

}



extern void _printf(const char*, ...);

// generate_poly_edges is called preRender_helper in the IDB.
void generate_poly_edges(int16_t* var_18, int16_t* regsi, int16_t mode) {

	int16_t sprite1_sprite_left2 = sprite1.sprite_left2;
	int16_t sprite1_sprite_widthsum = sprite1.sprite_widthsum;
	int16_t sprite1_sprite_top = sprite1.sprite_top;
	int16_t sprite1_sprite_height = sprite1.sprite_height;
	int16_t i, count, ofs;
	uint32_t value;
	uint32_t temp;
	char* errorstr = "%i ";
	
	if (mode == 1) goto preRender_helper2; /* was: asm jmp preRender_helper2 */

	//fatal_error("%i %i", sprite1_sprite_left2, sprite1_sprite_widthsum);
	
	count = regsi[10];
	if (count > 0) {
		ofs = regsi[3] - count; // TODO: 32bit thingy?
		if (regsi[2] < 0) ofs++;
		for (i = 0; i < count; i++) {
			var_18[ofs + i] = sprite1_sprite_left2;
			var_18[RFB_SPANROWS + ofs + i] = sprite1_sprite_left2 - 1;
		}
	}
	
	count = regsi[12];
	if (count > 0) {
		ofs = regsi[3] - count; // TODO: 32bit thingy?
		if (regsi[2] < 0) ofs++;
		for (i = 0; i < count; i++) {
			var_18[ofs + i] = sprite1_sprite_widthsum;
			var_18[RFB_SPANROWS + ofs + i] = sprite1_sprite_widthsum - 1;
		}
	}
	
	count = regsi[11];
	if (count > 0) {
		ofs = regsi[5] + 1;
		for (i = 0; i < count; i++) {
			var_18[ofs + i] = sprite1_sprite_left2;
			var_18[RFB_SPANROWS + ofs + i] = sprite1_sprite_left2 - 1;
		}
	}
	
	count = regsi[13];
	if (count > 0) {
		ofs = regsi[5] + 1;
		for (i = 0; i < count; i++) {
			var_18[ofs + i] = sprite1_sprite_widthsum;
			var_18[RFB_SPANROWS + ofs + i] = sprite1_sprite_widthsum - 1;
		}
	}

/* vestigial 16-bit segment setup removed in native port */


preRender_helper2:

	count = regsi[7];
	if (count <= 0) return ;

	ofs = regsi[3];
	
	switch (regsi[9]) {
		case 0:
		case 1:
			return;
		case 2:
			for (i = 0; i < count; i++) {
				var_18[ofs + i] = regsi[1];
				var_18[RFB_SPANROWS + ofs + i] = regsi[1];
			}
			return ;
		case 3:
			for (i = 0; i < count; i++) {
				var_18[ofs + i] = regsi[1] - i;
				var_18[RFB_SPANROWS + ofs + i] = regsi[1] - i;
			}
			return ;
		case 4:
			for (i = 0; i < count; i++) {
				var_18[ofs + i] = regsi[1] + i;
				var_18[RFB_SPANROWS + ofs + i] = regsi[1] + i;
			}
			return ;
		case 5:
			value = ((uint32_t*)regsi)[0] + 0x8000;
			for (i = 0; i < count; i++) {
				var_18[ofs + i] = value >> 16;
				var_18[RFB_SPANROWS + ofs + i] = value >> 16;
				value -= (uint16_t)regsi[6];
			}
			return ;
		case 6:
			value = ((uint32_t*)regsi)[0] + 0x8000;
			for (i = 0; i < count; i++) {
				var_18[ofs + i] = value >> 16;
				var_18[RFB_SPANROWS + ofs + i] = value >> 16;
				
				value += (uint16_t)regsi[6];
			}
			return ;
		case 7:
			value = (uint16_t)regsi[1];
			temp = (uint16_t)regsi[2];
			if (temp + 0x8000 > USHRT_MAX)
				ofs++;
			temp = (temp + 0x8000) & 0xFFFF;
			var_18[RFB_SPANROWS + ofs] = value;
			for (i = 0; i < count; i++) {
				if (temp + (uint16_t)regsi[6] <= USHRT_MAX) {
					value--;
					if (i == count - 1) {
						var_18[ofs] = value + 1;
					}
				} else {
					var_18[ofs] = value;
					value--;
					ofs++;
					var_18[RFB_SPANROWS + ofs] = value;
				}
				temp = (temp + (uint16_t)regsi[6])  & 0xFFFF;
			}
			return ;

		case 8:
			value = (uint16_t)regsi[1];
			temp = (uint16_t)regsi[2];
			if (temp + 0x8000 > USHRT_MAX)
				ofs++;
			temp = (temp + 0x8000) & 0xFFFF;
			var_18[ofs] = value;
			for (i = 0; i < count; i++) {
				if (temp + (uint16_t)regsi[6] <= USHRT_MAX) {
					value++;
					if (i == count - 1) {
						var_18[RFB_SPANROWS+ofs] = value - 1;
					}
				} else {
					var_18[RFB_SPANROWS+ofs] = value;
					value++;
					ofs++;
					var_18[ofs] = value;
				}
				temp = (temp + (uint16_t)regsi[6])  & 0xFFFF;
			}
			return ;

			
		/*case 8:
			value = regsi[1];
			temp = regsi[2] + 0x8000;
			if (temp <= 0)
				ofs++;
			var_18[ofs] = value;
			for (i = 0; i < count; i++) {
				temp += (uint16_t)regsi[6];
				if (temp >= 0) {
					value++;
				} else {
					var_18[RFB_SPANROWS + ofs + i] = value;
					value++;
					var_18[ofs + i] = value;
				}
				
			}
			if (temp > 0) {
				var_18[RFB_SPANROWS + ofs + i] = value - 1;
			}
			return ;*/
		case 9:
		default:
			return ;
	}
	/*
asm {
    mov     ax, ss
    mov     es, ax

	mov si, regsi

    mov     cx, [si+0Eh]
    or      cx, cx
    jle     int16_t locret_31AA3
    mov     di, [si+6]
    shl     di, 1
    add     di, [var_18]
    mov     bl, [si+12h]
    xor     bh, bh
    
cmp bx, 7
    jne _noo7
    jmp loc_31B00
_noo7:
    cmp bx, 8
    jne _noo8
    jmp loc_31B2C
_noo8:
    cmp bx, 9
    jmp locret_31AA3 // same as 0


loc_31B00: // case 7
    mov     cx, [si+0Eh]
    mov     ax, [si+2]
    mov     bx, [si+0Ch]
    mov     dx, [si+4]
    add     dx, 8000h
    jnb     int16_t loc_31B15
    add     di, 2
loc_31B15:
    mov     [di+3C0h], ax
loc_31B19:
    add     dx, bx
    jnb     int16_t loc_31B25 // jnb = Jump int16_t if not below (CF=0) = jump if the adding operation overflowed
    stosw
    dec     ax
    loop    loc_31B15
    sub     di, 2
    jmp the_end

loc_31B25: 
    dec     ax
    loop    loc_31B19
    inc     ax
    mov     [di], ax
    jmp the_end

loc_31B2C:  // case 8
    mov     cx, [si+0Eh]
    mov     ax, [si+2]
    mov     bx, [si+0Ch]
    mov     dx, [si+4]
    add     dx, 8000h
    jnb     int16_t loc_31B41
    add     di, 2
loc_31B41:
    mov     [di], ax
loc_31B43:
    add     dx, bx
    jnb     int16_t loc_31B55
    mov     [di+3C0h], ax
    add     di, 2
    inc     ax
    loop    loc_31B41
    sub     di, 2
    jmp the_end

loc_31B55:
    inc     ax
    loop    loc_31B43
    dec     ax
    mov     [di+3C0h], ax
    jmp the_end
    
    }
locret_31AA3:
the_end:*/
}




// aka preRender_helper3 in the IDB
void preRender_default_impl_helper(int16_t* regsi, uint16_t var_A, uint16_t var_C, int16_t* var_18) {
	/* Body was Watcom inline asm; translated instruction-exactly to C in
	 * rasm_port.c (rasm_preRender_default_impl_helper). */
	rasm_preRender_default_impl_helper(regsi, var_A, var_C, var_18);
}

extern uint16_t far word_2F448; // seg012
extern uint16_t far off_2F44A[]; // seg012

uint16_t draw_line_related_impl(uint16_t arg_startX, uint16_t arg_startY, uint16_t arg_endX, uint16_t arg_endY, int16_t* arg_8, uint16_t var_4);

uint16_t draw_line_related(uint16_t arg_startX, uint16_t arg_startY, uint16_t arg_endX, uint16_t arg_endY, int16_t* arg_8) {
	//return ported_draw_line_related_(arg_startX, arg_startY, arg_endX, arg_endY, arg_8);
	return draw_line_related_impl(arg_startX, arg_startY, arg_endX, arg_endY, arg_8, 0);
}

uint16_t draw_line_related_alt(uint16_t arg_startX, uint16_t arg_startY, uint16_t arg_endX, uint16_t arg_endY, int16_t* arg_8) {
	//return ported_draw_line_related_alt_(arg_startX, arg_startY, arg_endX, arg_endY, arg_8);
	return draw_line_related_impl(arg_startX, arg_startY, arg_endX, arg_endY, arg_8, 1);
}



uint16_t draw_line_related_impl(uint16_t arg_startX, uint16_t arg_startY, uint16_t arg_endX, uint16_t arg_endY, int16_t* arg_8, uint16_t var_4) {
	/* Body was Watcom inline asm; translated instruction-exactly to C in
	 * rasm_port.c (rasm_draw_line_related_impl). */
	return rasm_draw_line_related_impl(arg_startX, arg_startY, arg_endX, arg_endY, arg_8, var_4);
}

extern char aStxxx[];
extern int16_t far* carresptr;
extern int16_t far* car2resptr;
extern struct VECTOR carshapevec;
extern struct VECTOR carshapevec2;
extern struct VECTOR carshapevecs[];
extern struct VECTOR carshapevecs2[];
extern struct VECTOR carshapevecs3[];
extern struct VECTOR carshapevecs4[];

extern struct VECTOR oppcarshapevec;
extern struct VECTOR oppcarshapevec2;
extern struct VECTOR oppcarshapevecs[];
extern struct VECTOR oppcarshapevecs2[];
extern struct VECTOR oppcarshapevecs3[];
extern struct VECTOR oppcarshapevecs4[];
extern int16_t word_443E8[];
extern int16_t word_4448A[];

void sub_204AE(struct VECTOR far* arg_verts, int16_t arg_4, int16_t* arg_6, int16_t* arg_8, struct VECTOR* arg_vecarray, struct VECTOR* arg_vecptr);
void ported_sub_204AE_(struct VECTOR far* arg_verts, int16_t arg_4, int16_t* arg_6, int16_t* arg_8, struct VECTOR* arg_vecarray, struct VECTOR* arg_vecptr);

void shape3d_load_car_shapes(char arg_playercarid[], char arg_opponentcarid[]) {
	int16_t i;
	struct VECTOR far* var_E;
	uint32_t var_6;
	aStxxx[2] = arg_playercarid[0];
	aStxxx[3] = arg_playercarid[1];
	aStxxx[4] = arg_playercarid[2];
	aStxxx[5] = arg_playercarid[3];
	carresptr = file_load_3dres(aStxxx);
	shape3d_init_shape(locate_shape_fatal(carresptr, "car0"), &game3dshapes[124]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "car1"), &game3dshapes[126]);

	var_E = &(game3dshapes[126].shape3d_verts[8]);
	carshapevec.z = var_E[0].z;
	carshapevec.x = (var_E[3].x + var_E[0].x)/2;
	carshapevec2.z = var_E[6].z;
	carshapevec2.x = (var_E[6].x + var_E[9].x) /2;
	
	for (i = 0; i < 6; i++) {
		carshapevecs[i].x = carshapevec.x - var_E[i + 0].x;
		carshapevecs[i].z = carshapevec.z - var_E[i + 0].z;
		carshapevecs[i].y = var_E[i + 0].y;
		carshapevecs2[i].x = carshapevec2.x - var_E[i + 6].x;
		carshapevecs2[i].z = carshapevec2.z - var_E[i + 6].z;
		carshapevecs2[i].y = var_E[i + 6].y;
		carshapevecs3[i] = var_E[i + 12];
		carshapevecs4[i] = var_E[i + 18];
	}

	for (i = 0; i < 5; i++) {
		word_443E8[i] = 0;
	}

	shape3d_init_shape(locate_shape_fatal(carresptr, "car2"), &game3dshapes[128]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp0"), &game3dshapes[116]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp1"), &game3dshapes[117]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp2"), &game3dshapes[118]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp3"), &game3dshapes[119]);

	if (arg_opponentcarid[0] != -1) {
		if (arg_playercarid[0] == arg_opponentcarid[0] && arg_playercarid[1] == arg_opponentcarid[1] &&
			arg_playercarid[2] == arg_opponentcarid[2] && arg_playercarid[3] == arg_opponentcarid[3])
		{
			var_6 = mmgr_get_chunk_size_bytes(carresptr);
			car2resptr = mmgr_alloc_resbytes("car2", var_6);

			/* seg004 loc_20216 copies BYTES - `mov al, es:[bx+di]` /
			 * `mov es:[bx+di], al` - counting up to var_6 with the 32-bit
			 * counter var_A. carresptr and car2resptr are declared
			 * int16_t far* in this port, so indexing them directly copied
			 * var_6 *words*: twice the bytes that were allocated, running
			 * off the end of the buffer by exactly its own size on every
			 * race where the opponent drives the player's car.
			 *
			 * What it trampled was usually rfileio's chunk registry, which
			 * lives in the same heap, and the symptom was an intermittent
			 * "locate_shape: 'car0' not found" - 5 runs in 30, moving
			 * around as the heap layout changed. Byte pointers and a
			 * 32-bit counter, as the original has. */
			{
				uint8_t far* dst = (uint8_t far*)car2resptr;
				const uint8_t far* src = (const uint8_t far*)carresptr;
				uint32_t k;
				for (k = 0; k < var_6; k++) dst[k] = src[k];
			}
		} else {
			aStxxx[2] = arg_opponentcarid[0];
			aStxxx[3] = arg_opponentcarid[1];
			aStxxx[4] = arg_opponentcarid[2];
			aStxxx[5] = arg_opponentcarid[3];
			car2resptr = file_load_3dres(aStxxx);
		}

		shape3d_init_shape(locate_shape_fatal(car2resptr, "car0"), &game3dshapes[125]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "car1"), &game3dshapes[127]);

		var_E = &(game3dshapes[126].shape3d_verts[8]);
		oppcarshapevec.z = var_E[0].z;
		oppcarshapevec.x = (var_E[3].x + var_E[0].x)/2;
		oppcarshapevec2.z = var_E[6].z;
		oppcarshapevec2.x = (var_E[6].x + var_E[9].x) /2;

		for (i = 0; i < 6; i++) {
			oppcarshapevecs[i].x = oppcarshapevec.x - var_E[i + 0].x;
			oppcarshapevecs[i].z = oppcarshapevec.z - var_E[i + 0].z;
			oppcarshapevecs[i].y = var_E[i + 0].y;
			oppcarshapevecs2[i].x = oppcarshapevec2.x - var_E[i + 6].x;
			oppcarshapevecs2[i].z = oppcarshapevec2.z - var_E[i + 6].z;
			oppcarshapevecs2[i].y = var_E[i + 6].y;
			oppcarshapevecs3[i] = var_E[i + 12];
			oppcarshapevecs4[i] = var_E[i + 18];
		}
		for (i = 0; i < 5; i++) {
			word_4448A[i] = 0;
		}
		shape3d_init_shape(locate_shape_fatal(car2resptr, "car2"), &game3dshapes[129]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp0"), &game3dshapes[120]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp1"), &game3dshapes[121]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp2"), &game3dshapes[122]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp3"), &game3dshapes[123]);
	} else {
		car2resptr = 0;
	}
}

void sub_204AE(struct VECTOR far* arg_verts, int16_t arg_4, int16_t* arg_6, int16_t* arg_8, struct VECTOR* arg_vecarray, struct VECTOR* arg_vecptr) {
	int16_t i, j;
	int16_t var_C;
	int16_t var_2;
	int16_t var_14;
	int16_t var_10;
	int16_t var_8;
	int16_t var_4;
	//return ported_sub_204AE_(arg_verts, arg_4, arg_6, arg_8, arg_vecarray, arg_vecptr);
	if (arg_8[4] != 0) {
		var_C = sin_fast(arg_4 / 2);
		var_2 = cos_fast(arg_4 / 2);

		for (i = 0; i < 6; i++) {
			var_14 = multiply_and_scale(arg_vecarray[i].x, var_2);
			arg_verts[i].x = multiply_and_scale(arg_vecarray[i].z, var_C) + arg_vecptr[0].x + var_14;
			var_14 = multiply_and_scale(arg_vecarray[i].z, var_2);
			arg_verts[i].z = multiply_and_scale(arg_vecarray[i].x, var_C) + arg_vecptr[0].z + var_14;
		}
		for (i = 6; i < 0xC; i++) {
			var_10 = multiply_and_scale(arg_vecarray[i].x, var_2);
			arg_verts[i].x = multiply_and_scale(arg_vecarray[i].z, var_C) + arg_vecptr[1].x + var_10;
			var_10 = multiply_and_scale(arg_vecarray[i].z, var_2);
			arg_verts[i].z = multiply_and_scale(arg_vecarray[i].x, var_C) + arg_vecptr[1].z + var_10;
		}
		arg_8[4] = arg_4;
	}

	for (j = 0; j < 4; j++) {
		
		var_8 = _labs(_labs(arg_6[j]) >> 6);
		//var_8 = (arg_6[j]) >> 6;
		if (arg_8[j] == var_8)
			continue;
		i = j * 6;
		var_4 = (j * 6) + 6;

		for (; i < var_4; i++) {
			arg_verts[i].y = (arg_vecarray[i].y) - var_8;
		}
		arg_8[j] = var_8;
	}

	return ;
}

extern char unk_3E710[];

void shape3d_free_car_shapes() {
	if (car2resptr != 0) {
		sub_204AE(&(game3dshapes[127].shape3d_verts[8]), 0, unk_3E710, word_4448A, oppcarshapevecs, &oppcarshapevec);
		mmgr_release(car2resptr);
	}
	sub_204AE(&(game3dshapes[126].shape3d_verts[8]), 0, unk_3E710, word_443E8, carshapevecs, &carshapevec);
	mmgr_free(carresptr);
}
