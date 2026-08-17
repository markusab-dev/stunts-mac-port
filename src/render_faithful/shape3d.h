#ifndef RESTUNTS_SHAPE3D_H
#define RESTUNTS_SHAPE3D_H

#include "math.h"

#pragma pack (push, 1)

struct SHAPE3D {
	uint16_t shape3d_numverts;
	struct VECTOR far* shape3d_verts;
	uint16_t shape3d_numprimitives;
	uint16_t shape3d_numpaints;
	char far* shape3d_primitives;
	char far* shape3d_cull1;
	char far* shape3d_cull2;
};

struct SHAPE3DHEADER {
	uint8_t header_numverts;
	uint8_t header_numprimitives;
	uint8_t header_numpaints;
	uint8_t header_reserved;
};

struct TRANSFORMEDSHAPE3D {
	struct VECTOR pos;
	struct SHAPE3D* shapeptr;
	struct RECTANGLE* rectptr;
	struct VECTOR rotvec;
	uint16_t unk;
	uint8_t ts_flags;
	uint8_t material;
};

#pragma pack (pop)

int16_t shape3d_load_all(void);
void shape3d_free_all(void);
void shape3d_init_shape(char far* shapeptr, struct SHAPE3D* gameshape);
uint16_t transformed_shape_op(struct TRANSFORMEDSHAPE3D* arg_transshapeptr);
void set_projection(int16_t i1, int16_t i2, int16_t i3, int16_t i4);
int16_t polarAngle(int16_t z, int16_t y);
uint16_t select_cliprect_rotate(int16_t angZ, int16_t angX, int16_t angY, struct RECTANGLE* cliprect, int16_t unk);
void init_polyinfo(void);
void polyinfo_reset(void);
void get_a_poly_info(void);
void sub_204AE(struct VECTOR far* arg_verts, int16_t arg_4, int16_t* arg_6, int16_t* arg_8, struct VECTOR* arg_vecarray, struct VECTOR* arg_vecptr);

#endif
