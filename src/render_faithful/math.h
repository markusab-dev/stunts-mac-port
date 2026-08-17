#ifndef RESTUNTS_MATH_H
#define RESTUNTS_MATH_H

#pragma pack (push, 1)

struct RECTANGLE {
	int16_t left, right;
	int16_t top, bottom;
	//int16_t x1, y1;
	//int16_t x2, y2;
};

struct VECTOR {
	int16_t x, y, z;
};

struct VECTORLONG {
	int32_t lx, ly, lz;
};

struct POINT2D {
	int16_t px, py;
};

struct MATRIX {
	union {
		int16_t vals[9];
		struct {
			int16_t _11, _21, _31;
			int16_t _12, _22, _32;
			int16_t _13, _23, _33;
		} m;
	};
};

struct PLANE {
	int16_t plane_yz;
	int16_t plane_xy;
	struct VECTOR plane_origin;
	struct VECTOR plane_normal;
	struct MATRIX plane_rotation;
};

#pragma pack (pop)

int16_t sin_fast(uint16_t s);
int16_t cos_fast(uint16_t s);

int16_t polarAngle(int16_t z, int16_t y);
int16_t polarRadius2D(int16_t z, int16_t y);
int16_t polarRadius3D(struct VECTOR* vec);

uint16_t rect_compare_point(struct POINT2D* pt);

void mat_mul_vector(struct VECTOR* invec, struct MATRIX* mat, struct VECTOR* outvec);
void mat_mul_vector2(struct VECTOR* invec, struct MATRIX far* mat, struct VECTOR* outvec);
void mat_multiply(struct MATRIX* rmat, struct MATRIX* lmat, struct MATRIX* outmat);
void mat_invert(struct MATRIX* inmat, struct MATRIX* outmat);
void mat_rot_x(struct MATRIX* outmat, int16_t angle);
void mat_rot_y(struct MATRIX* outmat, int16_t angle);
void mat_rot_z(struct MATRIX* outmat, int16_t angle);
struct MATRIX* mat_rot_zxy(int16_t z, int16_t x, int16_t y, int16_t unk);

void rect_adjust_from_point(struct POINT2D* pt, struct RECTANGLE* rc);

int16_t vector_op_unk2(struct VECTOR* vec);
void vector_to_point(struct VECTOR* vec, struct POINT2D* outpt);
void vector_op_unk(struct VECTOR* vec1, struct VECTOR* vec2, struct VECTOR* outvec, int16_t i);

int16_t multiply_and_scale(int16_t a1, int16_t a2);

void rect_union(struct RECTANGLE* r1, struct RECTANGLE* r2, struct RECTANGLE* outrc);
int16_t rect_intersect(struct RECTANGLE* r1, struct RECTANGLE* r2);

void plane_rotate_op(void);
int16_t plane_origin_op(int16_t index, int16_t b, int16_t c, int16_t d);

#endif