#include "externs.h"
#include "math.h"

extern struct RECTANGLE select_rect_rc;
extern struct MATRIX mat_z_rot;
extern struct MATRIX mat_x_rot;
extern struct MATRIX mat_y_rot;
extern struct MATRIX mat_rot_temp;
extern uint16_t mat_y_rot_angle;
extern int32_t sin80, cos80;

int16_t sintab[] = {
	0, 101, 201, 302, 402, 503, 603, 704, 804, 904, 1005, 1105, 1205, 1306, 1406, 1506, 1606, 1706, 1806, 1906, 2006, 2105, 2205, 2305, 2404, 2503, 2603, 2702, 2801, 2900, 2999, 3098, 3196, 3295, 3393, 3492, 3590, 3688, 3786, 3883, 3981, 4078, 4176, 4273, 4370, 4467, 4563, 4660, 4756, 4852, 4948, 5044, 5139, 5235, 5330, 5425, 5520, 5614, 5708, 5803, 5897, 5990, 6084, 6177, 6270, 6363, 6455, 6547, 6639, 6731, 6823, 6914, 7005, 7096, 7186, 7276, 7366, 7456, 7545, 7635, 7723, 7812, 7900, 7988, 8076, 8163, 8250, 8337, 8423, 8509, 8595, 8680, 8765, 8850, 8935, 9019, 9102, 9186, 9269, 9352, 9434, 9516, 9598, 9679, 9760, 9841, 9921, 10001, 10080, 10159, 10238, 10316, 10394, 10471, 10549, 10625, 10702, 10778, 10853, 10928, 11003, 11077, 11151, 11224, 11297, 11370, 11442, 11514, 11585, 11656, 11727, 11797, 11866, 11935, 12004, 12072, 12140, 12207, 12274, 12340, 12406, 12472, 12537, 12601, 12665, 12729, 12792, 12854, 12916, 12978, 13039, 13100, 13160, 13219, 13279, 13337, 13395, 13453, 13510, 13567, 13623, 13678, 13733, 13788, 13842, 13896, 13949, 14001, 14053, 14104, 14155, 14206, 14256, 14305, 14354, 14402, 14449, 14497, 14543, 14589, 14635, 14680, 14724, 14768, 14811, 14854, 14896, 14937, 14978, 15019, 15059, 15098, 15137, 15175, 15213, 15250, 15286, 15322, 15357, 15392, 15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649, 15679, 15707, 15736, 15763, 15791, 15817, 15843, 15868, 15893, 15917, 15941, 15964, 15986, 16008, 16029, 16049, 16069, 16088, 16107, 16125, 16143, 16160, 16176, 16192, 16207, 16221, 16235, 16248, 16261, 16273, 16284, 16295, 16305, 16315, 16324, 16332, 16340, 16347, 16353, 16359, 16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384, 16384
};

extern uint8_t atantable[];

int16_t sin_fast(uint16_t s) {
	uint8_t c = s & 0xFF;
	switch ((s >> 8) & 3) {
		case 0:
			return sintab[c];
		case 1:
			return sintab[0x100 - c];
		case 2:
			return -sintab[c];
		case 3:
			return -sintab[0x100 - c];
	}
}

int16_t cos_fast(uint16_t s) {
	return sin_fast(s + 0x100);
}

int16_t polarAngle(int16_t z, int16_t y) {
	
	uint16_t flag;
	int16_t temp, result;
	uint32_t index;
	
	flag = 0;
	
	if (z < 0) {
		flag |= 4;
		z = -z;
	}
	
	if (y < 0) {
		flag |= 2;
		y = -y;
	}
	
	if (z == y) {
		if (z == 0) return result; // orig code has undefined return value here as well!
		result = 0x80;
	} else {
		if (z > y) {
			temp = z;
			z = y;
			y = temp;
			flag |= 1;
		}
		index = (((uint32_t)z << 16) / y);
		if ((index & 0xFF) >= 0x80) // round upwards
			index += 0x100;
		result = atantable[index >> 8];
	}
	
	switch (flag) {
		case 0:
			return result;
		case 1:
			return -result + 0x100;
		case 2:
			return -result + 0x200;
		case 3:
			return result + 0x100;
		case 4:
			return -result;
		case 5:
			return result - 0x100;
		case 6:
			return result - 0x200;
		case 7:
			return -(result + 0x100);
	}
	
}

int16_t polarRadius2D(int16_t z, int16_t y) {
	int32_t result;
	
	result = polarAngle(z, y);
	
	if (result < 0) {
		result = -result;
	}
	
	if (result >= 0x100) {
		result = -(result - 0x200);
	}
	
	if (result <= 0x80) {
		result = cos_fast(result);
		if (y < 0)
			y = -y;
		return (((uint32_t)y) << 14) / result;
	} else {
		result = sin_fast(result);
		if (z < 0)
			z = -z;
		return (((uint32_t)z) << 14) / result;
	}
}

int16_t polarRadius3D(struct VECTOR* vec) {
	return polarRadius2D( polarRadius2D(vec->x, vec->y), vec->z );
}

uint16_t rect_compare_point(struct POINT2D* pt) {
	char flag;
	if (pt->py < select_rect_rc.top)
		flag = 1;
	else if (pt->py > select_rect_rc.bottom)
		flag = 2; 
	else
		flag = 0;
	
	if (pt->px < select_rect_rc.left)
		flag |= 4;
	else if (pt->px > select_rect_rc.right)
		flag |= 8;
	return flag;
}

void mat_mul_vector(struct VECTOR* invec, struct MATRIX* mat, struct VECTOR* outvec) {

	if (mat->m._11 != 0 && invec->x != 0)
		outvec->x = ((int32_t)mat->m._11 * invec->x) >> 14;
	else
		outvec->x = 0;

	if (mat->m._12 != 0 && invec->y != 0)
		outvec->x += ((int32_t)mat->m._12 * invec->y) >> 14;

	if (mat->m._13 != 0 && invec->z != 0)
		outvec->x += ((int32_t)mat->m._13 * invec->z) >> 14;


	if (mat->m._21 != 0 && invec->x != 0)
		outvec->y = ((int32_t)mat->m._21 * invec->x) >> 14;
	else
		outvec->y = 0;

	if (mat->m._22 != 0 && invec->y != 0)
		outvec->y += ((int32_t)mat->m._22 * invec->y) >> 14;

	if (mat->m._23 != 0 && invec->z != 0)
		outvec->y += ((int32_t)mat->m._23 * invec->z) >> 14;


	if (mat->m._31 != 0 && invec->x != 0)
		outvec->z = ((int32_t)mat->m._31 * invec->x) >> 14;
	else
		outvec->z = 0;

	if (mat->m._32 != 0 && invec->y != 0)
		outvec->z += ((int32_t)mat->m._32 * invec->y) >> 14;

	if (mat->m._33 != 0 && invec->z != 0)
		outvec->z += ((int32_t)mat->m._33 * invec->z) >> 14;

}

void mat_mul_vector2(struct VECTOR* invec, struct MATRIX far* mat, struct VECTOR* outvec)
{
	struct MATRIX tmpmat = *mat;
	
	mat_mul_vector(invec, &tmpmat, outvec);
}

void mat_multiply(struct MATRIX* rmat, struct MATRIX* lmat, struct MATRIX* outmat) {
	int16_t counter;
	int16_t* rmatvals = rmat->vals;
	int16_t* lmatvals = lmat->vals;
	int16_t* outmatvals = outmat->vals;
	
	counter = 9;
	while (counter > 0) {
		if (rmatvals[0] != 0 && lmatvals[0] != 0)
			outmatvals[0] = ((int32_t)rmatvals[0] * lmatvals[0]) >> 14; else
			outmatvals[0] = 0;

		if (rmatvals[1] != 0 && lmatvals[3] != 0)
			outmatvals[0] += ((int32_t)rmatvals[1] * lmatvals[3]) >> 14;

		if (rmatvals[2] != 0 && lmatvals[6] != 0)
			outmatvals[0] += ((int32_t)rmatvals[2] * lmatvals[6]) >> 14;
		
		outmatvals++;
		if (counter != 7 && counter != 4) {
			lmatvals++;
		} else {
			lmatvals -= 2;
			rmatvals += 3;
		}
		counter--;
	}
	
}

void mat_invert(struct MATRIX* inmat, struct MATRIX* outmat) {
	int16_t temp;
	if (inmat == outmat) {
		temp = outmat->m._21;
		outmat->m._21 = outmat->m._12;
		outmat->m._12 = temp;

		temp = outmat->m._31;
		outmat->m._31 = outmat->m._13;
		outmat->m._13 = temp;

		temp = outmat->m._32;
		outmat->m._32 = outmat->m._23;
		outmat->m._23 = temp;
	} else {
		outmat->m._11 = inmat->m._11;
		outmat->m._12 = inmat->m._21;
		outmat->m._13 = inmat->m._31;

		outmat->m._21 = inmat->m._12;
		outmat->m._22 = inmat->m._22;
		outmat->m._23 = inmat->m._32;

		outmat->m._31 = inmat->m._13;
		outmat->m._32 = inmat->m._23;
		outmat->m._33 = inmat->m._33;
	}
}


void mat_rot_x(struct MATRIX* outmat, int16_t angle) {
	int16_t c, s;
	
	c = cos_fast(angle);
	s = sin_fast(angle);
	outmat->m._11 = 0x4000;
	outmat->m._21 = 0;
	outmat->m._31 = 0;
	outmat->m._12 = 0;
	outmat->m._22 = c;
	outmat->m._32 = s;
	outmat->m._13 = 0;
	outmat->m._23 = -s;
	outmat->m._33 = c;
}

void mat_rot_y(struct MATRIX* outmat, int16_t angle) {
	int16_t c, s;
	
	c = cos_fast(angle);
	s = sin_fast(angle);
	outmat->m._11 = c;
	outmat->m._21 = 0;
	outmat->m._31 = -s;
	outmat->m._12 = 0;
	outmat->m._22 = 0x4000;
	outmat->m._32 = 0;
	outmat->m._13 = s;
	outmat->m._23 = 0;
	outmat->m._33 = c;
}

void mat_rot_z(struct MATRIX* outmat, int16_t angle) {
	int16_t c, s;
	
	c = cos_fast(angle);
	s = sin_fast(angle);
	outmat->m._11 = c;
	outmat->m._21 = s;
	outmat->m._31 = 0;
	outmat->m._12 = -s;
	outmat->m._22 = c;
	outmat->m._32 = 0;
	outmat->m._13 = 0;
	outmat->m._23 = 0;
	outmat->m._33 = 0x4000;
}

// mat_rot_zxy was originally optimized, using pre-calced y-matrices and only 
// multiplying the non-zero axes. currently not optimized except for the y cache:

struct MATRIX* mat_rot_zxy(int16_t z, int16_t x, int16_t y, int16_t unk) {
	int16_t flag;
	
	mat_rot_z(&mat_z_rot, z);
	mat_rot_x(&mat_x_rot, x);
	
	// y rotation matrix cache
	/*if (mat_y_rot_angle != y) {
		mat_rot_y(&mat_y_rot, y);
		mat_y_rot_angle = y;
	}*/
	mat_y_rot_angle = y; // dont forget this!!
	mat_rot_y(&mat_y_rot, y);
	
	if ((unk & 1) != 0) {
		mat_multiply(&mat_y_rot, &mat_x_rot, &mat_rot_temp);
		mat_multiply(&mat_rot_temp, &mat_z_rot, &mat_x_rot);
		return &mat_x_rot;
	} else {
		mat_multiply(&mat_z_rot, &mat_x_rot, &mat_rot_temp);
		mat_multiply(&mat_rot_temp, &mat_y_rot, &mat_z_rot);
		return &mat_z_rot;
	}
}

void rect_adjust_from_point(struct POINT2D* pt, struct RECTANGLE* rc) {
	int16_t temp;
	
	if (rc->left > pt->px) {
		rc->left = pt->px;
	}
	
	temp = pt->px + 1;
	if (rc->right < temp) {
		rc->right = temp;
	}
	
	if (rc->top > pt->py) {
		rc->top = pt->py;
	}
	
	temp = pt->py + 1;
	if (rc->bottom < temp) {
		rc->bottom = temp;
	}
}

void rect_union(struct RECTANGLE* r1, struct RECTANGLE* r2, struct RECTANGLE* outrc) {
	if (r1->left <= r2->left) {
		outrc->left = r1->left;
	} else {
		outrc->left = r2->left;
	}

	if (r1->right >= r2->right) {
		outrc->right = r1->right;
	} else {
		outrc->right = r2->right;
	}
	
	if (r1->top <= r2->top) {
		outrc->top = r1->top;
	} else {
		outrc->top = r2->top;
	}

	if (r1->bottom >= r2->bottom) {
		outrc->bottom = r1->bottom;
	} else {
		outrc->bottom = r2->bottom;
	}
	
	if (video_flag2_is1 == 1) {
		return ;
	}

	fatal_error("rect_union: unexpected code path");
	/*
	mov     bx, [bp+arg_outrectptr]
	mov     si, bx
	mov     ax, [si+RECTANGLE.rc_right]
	add     ax, video_flag2_is1
	dec     ax
	and     ax, video_flag3_isFFFF
	mov     [bx+RECTANGLE.rc_right], ax
*/
}

int16_t rect_intersect(struct RECTANGLE* r1, struct RECTANGLE* r2) {
	if (r1->right <= r1->left) return 1;
	if (r2->right <= r1->left) return 1;
	if (r1->right <= r2->left) return 1;
	if (r1->top >= r2->bottom) return 1;
	if (r1->bottom <= r2->top) return 1;
	
	if (r1->left < r2->left) {
		r1->left = r2->left;
	}
	
	if (r1->right > r2->right) {
		r1->right = r2->right;
	}
	
	if (r1->top < r2->top) {
		r1->top = r2->top;
	}
	
	if (r1->bottom > r2->bottom) {
		r1->bottom = r2->bottom;
	}
	return 0;
}

int16_t rect_is_inside(struct RECTANGLE* r1, struct RECTANGLE* r2) {
	if (r1->right > r2->right) {
		return 0;
	}
	
	if (r1->left < r2->left) {
		return 0;
	}
	
	if (r1->top < r2->top) {
		return 0;
	}
	
	if (r1->bottom > r2->bottom) {
		return 0;
	}
	
	return 1;
}

int16_t rect_is_overlapping(struct RECTANGLE* r1, struct RECTANGLE* r2) {
	if (r1->right <= r2->left) {
		return 0;
	}
	
	if (r2->right <= r1->left) {
		return 0;
	}
	
	if (r1->top >= r2->bottom) {
		return 0;
	}
	
	if (r1->bottom <= r2->top) {
		return 0;
	}
	
	return 1;
}

int16_t rect_is_adjacent(struct RECTANGLE* r1, struct RECTANGLE* r2) {
	if (r1->bottom == r2->top || r1->top == r2->bottom) {
		if (r1->left == r2->left && r1->right == r2->right)
			return 1;
		return 0;
	}

	if (r1->right == r2->left || r2->right == r2->left) {
		if (r1->top == r2->top && r1->bottom == r2->bottom)
			return 1;
	}
	return 0;
}

void rectlist_add_rect(char* arg_rect_array_length_ptr, struct RECTANGLE* arg_rect_array_ptr, struct RECTANGLE* rect) {	
	int16_t var_counter;
	struct RECTANGLE var_rect;
	struct RECTANGLE var_rect2;
	struct RECTANGLE var_rect3;
	struct RECTANGLE* var_rectptr;
	int16_t var_22, var_18, var_12;

	if (video_flag2_is1 != 1) {
		fatal_error("rectlist_add_rect: unexpected code path");
		/*
		mov     bx, [bp+arg_rectptr]
		mov     si, bx
		mov     ax, [si+RECTANGLE.rc_right]
		add     ax, video_flag2_is1
		dec     ax
		and     ax, video_flag3_isFFFF
		mov     [bx+RECTANGLE.rc_right], ax*/
	}
	
	for (var_counter = 0; var_counter < *arg_rect_array_length_ptr; var_counter++) {
		var_rectptr = &arg_rect_array_ptr[var_counter];
		if (rect_is_overlapping(rect, var_rectptr) == 0)
			continue;
		if (rect_is_inside(rect, var_rectptr) != 0)
			return ;

		if (rect_is_inside(var_rectptr, rect) != 0) {
			var_12 = var_counter;

			while (((*arg_rect_array_length_ptr) - 1) > var_12) {
				arg_rect_array_ptr[var_12] = arg_rect_array_ptr[var_12 + 1];
				var_12++;
			}
			(*arg_rect_array_length_ptr)--;
			continue;
		}

		var_rect = *var_rectptr;
		if (var_rectptr->top >= rect->top) {
			if (rect->top < var_rectptr->top) {
				var_rect2 = *rect;
				var_rect2.bottom = var_rectptr->top;
				var_18 = 1;
			} else {
				var_18 = 0;
			}
		} else {	
			var_rect2 = *var_rectptr;
			var_rect2.bottom = rect->top;
			var_rect.top = rect->top;
			var_18 = 1;
		}

		if (var_rectptr->bottom <= rect->bottom) {
			if (rect->bottom > var_rectptr->bottom) {
				var_rect3 = *rect;
				var_rect3.top = var_rectptr->bottom;
				var_22 = 1;
			} else {
				var_22 = 0;
			}
		} else {
			var_rect3 = *var_rectptr;
			var_rect3.top = rect->bottom;
			var_rect.bottom = rect->bottom;
			var_22 = 1;
		}

		if (rect->left <= var_rectptr->left)
			var_rect.left = rect->left;
		else
			var_rect.left = var_rectptr->left;

		if (rect->right >= var_rectptr->right)
			var_rect.right = rect->right;
		else
			var_rect.right = var_rectptr->right;

		var_12 = var_counter;

		while (((*arg_rect_array_length_ptr) - 1) > var_12) {
			arg_rect_array_ptr[var_12] = arg_rect_array_ptr[var_12 + 1];
			var_12 ++;
		}
		(*arg_rect_array_length_ptr)--;
		if (var_18 != 0) {
			rectlist_add_rect(arg_rect_array_length_ptr, arg_rect_array_ptr, &var_rect2);
		}

		rectlist_add_rect(arg_rect_array_length_ptr, arg_rect_array_ptr, &var_rect);
		if (var_22 != 0) {
			rectlist_add_rect(arg_rect_array_length_ptr, arg_rect_array_ptr, &var_rect3);
			return ;
		}
		return ;
	}

	for (var_counter = 0; var_counter < *arg_rect_array_length_ptr; var_counter++) {
		var_rectptr = &arg_rect_array_ptr[var_counter];

		if (rect_is_adjacent(var_rectptr, rect) == 0) {
			continue;
		}
		if (var_rectptr->left <= rect->left)
			var_rect.left = var_rectptr->left;
		else
			var_rect.left = rect->left;

		if (var_rectptr->right >= rect->right)
			var_rect.right = var_rectptr->right;
		else
			var_rect.right = rect->right;

		if (var_rectptr->top <= rect->top)
			var_rect.top = var_rectptr->top;
		else
			var_rect.top = rect->top;
		
		if (var_rectptr->bottom >= rect->bottom)
			var_rect.bottom = var_rectptr->bottom;
		else
			var_rect.bottom = rect->bottom;

		var_12 = var_counter;

		while (((*arg_rect_array_length_ptr) - 1) > var_12) {
			arg_rect_array_ptr[var_12] = arg_rect_array_ptr[var_12 + 1];
			var_12 ++;
		}
		(*arg_rect_array_length_ptr)--;
		rectlist_add_rect(arg_rect_array_length_ptr, arg_rect_array_ptr, &var_rect);
		return ;
	}

	arg_rect_array_ptr[*arg_rect_array_length_ptr] = *rect;
	(*arg_rect_array_length_ptr)++;
}


void rectlist_add_rects(char arg_rectcount, char* arg_rectarray_indices, 
	struct RECTANGLE* arg_rectarray1, struct RECTANGLE* arg_rectarray2, 
	struct RECTANGLE* arg_rectptr, char* arg_rect_array_length_ptr, struct RECTANGLE* arg_rect_array_ptr) 
{
	struct RECTANGLE* var_rectptr3;
	struct RECTANGLE* var_rectptr;
	struct RECTANGLE* var_rectptr2;
	struct RECTANGLE var_rect;
	struct RECTANGLE var_rect2;
	int16_t var_2, var_rectcounter;
	int16_t var_rectarray_index;
	struct RECTANGLE* temprectptr;
/*
	return ported_rect_clip_combined_(
		arg_rectcount, arg_rectarray_indices, arg_rectarray1, arg_rectarray2, arg_rectptr,
		arg_rect_array_length_ptr, arg_rect_array_ptr);
	*/
	for (var_rectcounter = 0; var_rectcounter < arg_rectcount; var_rectcounter++) {

		var_rectarray_index = arg_rectarray_indices[var_rectcounter];
		if ((var_rectarray_index & 1) != 0) {
			var_rectptr = &arg_rectarray1[var_rectcounter];
		}

		if ((var_rectarray_index & 2) != 0) {
			var_rectptr3 = &arg_rectarray2[var_rectcounter];
		}

		if (((var_rectarray_index & 1) == 0) || var_rectptr->right <= var_rectptr->left) {
			if (((var_rectarray_index & 2) == 0) || var_rectptr3->right <= var_rectptr3->left) {
				var_2 = 0;
			} else {
				var_rectptr2 = var_rectptr3;
				var_2 = 1;
			}
		} else if ((var_rectarray_index & 2) == 0) {
			var_rectptr2 = var_rectptr;
			var_2 = 1;
		} else if (var_rectptr3->right <= var_rectptr3->left) {
			var_rectptr2 = var_rectptr;
			var_2 = 1;
		} else {
			rect_union(var_rectptr, var_rectptr3, &var_rect2);
			var_rectptr2 = &var_rect2;
			var_2 = 1;
		}

		if (var_2 != 0) {
			var_rect = *var_rectptr2;
			if (rect_intersect(&var_rect, arg_rectptr) == 0) {
				rectlist_add_rect(arg_rect_array_length_ptr, arg_rect_array_ptr, &var_rect);
			}
		}
	}

}

void rect_array_sort_by_top(char arg_array_length, struct RECTANGLE* arg_rect_array, int16_t* arg_array_indices) {
	int16_t i;
	int16_t intbuffer[256];
	//return ported_rect_array_indexed_op_(arg_array_length, arg_rect_array, arg_array_indices);
	if (arg_array_length > 1) {
		for (i = 0; i < arg_array_length; i++) {
			intbuffer[i] = -arg_rect_array[i].top;
			arg_array_indices[i] = i;
		}
		heapsort_by_order(arg_array_length, intbuffer, arg_array_indices);
	} else {
		arg_array_indices[0] = 0;
	}
}


int16_t vector_op_unk2(struct VECTOR* vec) {
	int32_t y;
	int32_t temp;
	int16_t flag;
	int16_t result;
	int32_t angle;
	
	y = abs(vec->y);
	
	temp = polarRadius2D(abs(vec->x), abs(vec->z));
	
	if (sin80 != cos80) {
		//fatal_error("sin80 != cos80 - not observed");
		y = y * sin80;
		temp = temp * cos80;
	} 

	if (temp >= y) {
		flag = 0;
	} else {
		flag = 1;
	}
	
	if (vec->y < 0) {
		if (flag != 0) return 0x1E;
	} else
	if (vec->y > 0) {
		if (flag != 0) return 0x1F;
	}

	if (vec->y > 0) {
		result = 0x0F;
	} else {
		result = 0;
	}
	
	angle = -polarAngle(vec->z, -vec->x);
	if (angle < 0) {
		angle += 0x400;
	}
	
	result += (((angle << 4) - angle) >> 10);
	
	return result;
}

extern int16_t projectiondata5, projectiondata8, projectiondata9, projectiondata10;


void vector_to_point(struct VECTOR* vec, struct POINT2D* outpt) {

	// the original code checks for several overflows (in a strange way) - this code does not, but seems to do well anyway

	int32_t proj;
	int32_t comp;
	
	if (vec->z <= 0) {
		outpt->px = 0x8000;
		outpt->py = 0x8000;
		return;
	}
	
	if (vec->x < 0) {
		proj = (int32_t)-vec->x * projectiondata9;
		comp = (proj >> 16) << 1;
		if (proj & 0xFFFF == 0) {
			fatal_error("%li  %i", proj, comp);
			comp++;
		}

		if (vec->z > comp) { 
			outpt->px = -(proj / vec->z) + projectiondata5;
		} else
			outpt->px = -0x7D00;
	} else {
		proj = (int32_t)vec->x * projectiondata9;
		comp = (proj >> 16) << 1;
		if (proj & 0xFFFF == 0) {
			fatal_error("%l  %i", proj, comp);
			comp++;
		}

		if (vec->z > comp) 
			outpt->px = (proj / vec->z) + projectiondata5;
		else
			outpt->px = 0x7D00;
	}

	if (vec->y < 0) {
		proj = (int32_t)-vec->y * projectiondata10;
		comp = (proj >> 16) << 1;
		if (proj & 0xFFFF == 0) {
			fatal_error("%l  %i", proj, comp);
			comp++;
		}

		if (vec->z > comp) 
			outpt->py = (proj / vec->z) + projectiondata8;
		else
			outpt->py = 0x7D00;
	} else {
		proj = (int32_t)vec->y * projectiondata10;
		comp = (proj >> 16) << 1;
		if (proj & 0xFFFF == 0) {
			fatal_error("%l  %i", proj, comp);
			comp++;
		}

		if (vec->z > comp) 
			outpt->py = -(proj / vec->z) + projectiondata8;
		else
			outpt->py = -0x7D00;
	}
}

/* ------------------------------------------------------------------ *
 * vector_op_unk - restunts2 seg012.asm 9592..9640 (vector_op_unk_asm_) *
 *                                                                      *
 * [FIXED 2026-08-17] restunts1 never disassembled this routine, so the *
 * vendored c/math.c carries a hand *reconstruction*, and that           *
 * reconstruction is wrong in two ways that matter:                      *
 *                                                                       *
 *   1. it declares var_4 / var_2 as `long`; the original keeps them in   *
 *      16-bit words and uses `imul word` / `idiv word`;                  *
 *   2. it halves them with `>>` on a signed long, i.e. ARITHMETIC; the   *
 *      original uses `shr`, i.e. LOGICAL, on 16 bits.                    *
 *                                                                       *
 * The difference is not cosmetic.  The halving only runs when            *
 * vec1->z - vec2->z is NEGATIVE, and a logical shift of a negative word  *
 * lands just under 0x8000.  So both var_4 and var_2 come out near        *
 * 32768, their ratio collapses to ~1, and the interpolation returns      *
 * (very nearly) vec1 instead of a point between vec1 and vec2.  That is  *
 * a real behaviour of the shipped game, not a rounding error.            *
 *                                                                       *
 * Found by the DOS oracle: T_HELL4 frame 374 is a wall scrape, and       *
 * update_player_state's loc_153AE feeds this routine (vec_1C =           *
 * (-129,0,-11), vec_C = (-82,0,9), i = 0).  The reconstruction returns   *
 * x = -105; an exhaustive search over every value the rest of the wall   *
 * path could accept showed the oracle needs exactly x = -129 = vec1->x,  *
 * which is what the instruction-exact version below produces:            *
 *   var_4 = 0xFFF7 shr 1 = 0x7FFB = 32763                                *
 *   var_2 = 0xFFEC shr 1 = 0x7FF6 = 32758                                *
 *   (-47 * 32763) / 32758 = -47, -47 + (-82) = -129.                     *
 *                                                                       *
 * [ODDITY] `idiv word` faults on 8086 if the quotient will not fit in    *
 * 16 bits.  The C below truncates instead; no call site reaches it.      *
 * ------------------------------------------------------------------ */
void vector_op_unk(struct VECTOR* vec1, struct VECTOR* vec2, struct VECTOR* outvec, int16_t i) {

	int16_t var_4, var_2, ax;

	ax = i;
	outvec->z = ax;                                  /* mov [di+4], ax */
	var_4 = (int16_t)(ax - vec2->z);                 /* sub ax,[bx+4] */
	ax = (int16_t)(vec1->z - vec2->z);               /* mov ax,[si+4] ; sub ax,[bx+4] */
	if (ax < 0) {                                    /* or ax,ax ; jge */
		var_4 = (int16_t)((uint16_t)var_4 >> 1); /* shr word ptr [bp+var_4],1 */
		ax    = (int16_t)((uint16_t)ax >> 1);    /* shr ax,1 */
	}
	var_2 = ax;

	/* imul word (16x16 -> dx:ax), idiv word (32/16 -> ax), add 16-bit */
	outvec->x = (int16_t)(((int32_t)(int16_t)(vec1->x - vec2->x) * (int32_t)var_4)
	                      / (int32_t)var_2 + vec2->x);
	outvec->y = (int16_t)(((int32_t)(int16_t)(vec1->y - vec2->y) * (int32_t)var_4)
	                      / (int32_t)var_2 + vec2->y);
}

int16_t multiply_and_scale(int16_t a1, int16_t a2)
{
	int32_t mul = (int32_t)a1 * (int32_t)a2 * 4L;
	return (mul >> 16) + ((mul & 0x8000) >> 15);
}

extern int16_t planindex;
extern struct PLANE far* planptr;
extern struct PLANE far* current_planptr;
extern int16_t elem_xCenter;
extern int16_t elem_zCenter;
extern int16_t terrainHeight;

int16_t vec_normalInnerProduct(int16_t x, int16_t y, int16_t z, struct VECTOR far* normal) {
	return (
		((int32_t)normal->x * x) + 
		((int32_t)normal->z * z) +
		((int32_t)normal->y * y)) / 0x2000;
}

int16_t plane_origin_op(int16_t arg_planindex, int16_t x, int16_t y, int16_t z) {
	struct PLANE far* curplane;
	struct VECTOR a;
	struct VECTOR b;
	
	if (arg_planindex == planindex) {
		curplane = current_planptr;
	} else {
		curplane = &planptr[arg_planindex];
	}

	b.y = curplane->plane_origin.y + terrainHeight;
	a.y = y - b.y;
	if (arg_planindex < 4) {
		// NOTE: what is this
		return a.y;
	}
	b.x = curplane->plane_origin.x + elem_xCenter;
	b.z = curplane->plane_origin.z + elem_zCenter;
	a.x = x - b.x;
	a.z = z - b.z;
	return vec_normalInnerProduct(a.x, a.y, a.z, &curplane->plane_normal);
}

extern int16_t planindex_copy;
extern int16_t pState_minusRotate_z_2;
extern int16_t pState_minusRotate_y_2;
extern int16_t pState_minusRotate_x_2;
extern struct MATRIX mat_unk;
extern struct MATRIX mat_unk2;
extern struct VECTOR vec_unk2;
extern int16_t pState_f36Mminf40sar2;
extern struct VECTOR vec_planerotopresult;
extern int16_t word_3BE16;
extern struct MATRIX mat_planetmp;
extern int16_t pState_f36Mminf40sar2;
extern int16_t f36f40_whlData;

void plane_rotate_op(void) {
/*    var_planptr = dword ptr -54
    var_32 = word ptr -50
    var_2E = word ptr -46
    var_2C = byte ptr -44
    var_1A = byte ptr -26
    var_8 = byte ptr -8
     s = byte ptr 0
     r = byte ptr 2
*/
    struct PLANE far* var_planptr;
    struct VECTOR var_32;
    struct MATRIX var_2C;
    struct MATRIX var_1A;
    struct VECTOR var_8;
	int16_t si;

	//return ported_plane_rotate_op_();
	if (planindex_copy != -1)
		goto loc_197A6;
	goto loc_198C2;
/*    push    bp
    mov     bp, sp
    sub     sp, 36h
    push    di
    push    si
    cmp     planindex_copy, 0FFFFh
    jnz     int16_t loc_197A6
    jmp     loc_198C2
*/
loc_197A6:
	var_planptr = &planptr[planindex_copy];
	if (var_planptr->plane_xy != pState_minusRotate_x_2)
		goto loc_197D6;
	if (var_planptr->plane_yz != pState_minusRotate_z_2)
		goto loc_197D6;
	si = pState_minusRotate_y_2;
	goto loc_19845;
/*    mov     ax, 22h ; '"'
    imul    planindex_copy
    add     ax, word ptr planptr
    mov     dx, word ptr planptr+2
    mov     word ptr [bp+var_planptr], ax
    mov     word ptr [bp+var_planptr+2], dx
    les     bx, [bp+var_planptr]
    mov     ax, pState_minusRotate_x_2
    cmp     es:[bx+2], ax
    jnz     int16_t loc_197D6
    mov     ax, pState_minusRotate_z_2
    cmp     es:[bx], ax
    jnz     int16_t loc_197D6
    mov     si, pState_minusRotate_y_2
    jmp     int16_t loc_19845
    ; align 2
    db 144*/
loc_197D6:
	mat_mul_vector(&vec_unk2, &mat_unk, &var_8);
	var_1A = planptr[planindex_copy].plane_rotation;
	mat_invert(&var_1A, &var_2C);
/*    lea     ax, [bp+var_8]
    push    ax
    mov     ax, offset mat_unk
    push    ax
    mov     ax, offset vec_unk2
    push    ax
    call    mat_mul_vector
    add     sp, 6
    mov     ax, 22h ; '"'
    imul    planindex_copy
    add     ax, word ptr planptr
    mov     dx, word ptr planptr+2
    add     ax, 10h         ; plane rotation matrix
    push    si
    lea     di, [bp+var_1A]
    mov     si, ax
    push    ss
    pop     es
    push    ds
    mov     ds, dx
    mov     cx, 9
    repne movsw
    pop     ds
    pop     si
    lea     ax, [bp+var_2C]
    push    ax
    lea     ax, [bp+var_1A]
    push    ax
    call    mat_invert
    add     sp, 4*/

	mat_mul_vector(&var_8, &var_2C, &var_32);
	si = polarAngle(-var_32.x, var_32.z);
/*    lea     ax, [bp+var_32]
    push    ax
    lea     ax, [bp+var_2C]
    push    ax
    lea     ax, [bp+var_8]
    push    ax
    call    mat_mul_vector
    add     sp, 6
    push    [bp+var_32.vz]
    mov     ax, [bp+var_32.vx]
    neg     ax
    push    ax
    call    polarAngle
    add     sp, 4
    mov     si, ax*/
loc_19845:
	si += pState_f36Mminf40sar2;
	if (si == 0)
		goto loc_198A4;
	if (word_3BE16 == si)
		goto loc_19866;
	mat_rot_y(&mat_planetmp, -si);
	word_3BE16 = si;
    /*add     si, pState_f36Mminf40sar2
    jz      int16_t loc_198A4
    cmp     word_3BE16, si
    jz      int16_t loc_19866
    mov     ax, si
    neg     ax
    push    ax
    mov     ax, offset unk_40D58
    push    ax
    call    mat_rot_y
    add     sp, 4
    mov     word_3BE16, si*/
loc_19866:
	mat_mul_vector(&vec_unk2, &mat_planetmp, &var_32);
	mat_mul_vector2(&var_32, &planptr[planindex_copy].plane_rotation, &vec_planerotopresult);
	return ;
/*    lea     ax, [bp+var_32]
    push    ax
    mov     ax, offset unk_40D58
    push    ax
    mov     ax, offset vec_unk2
    push    ax
    call    mat_mul_vector
    add     sp, 6
    mov     ax, offset vec_planerotopresult
    push    ax
    mov     ax, 22h ; '"'
    imul    planindex_copy
    add     ax, word ptr planptr
    mov     dx, word ptr planptr+2
    add     ax, 10h         ; plane rotation matrix.
    push    dx
    push    ax
    lea     ax, [bp+var_32]
loc_19895:
    push    ax
    push    cs
    call near ptr ported_mat_mul_vector2_
    add     sp, 8
    pop     si
    pop     di
    mov     sp, bp
    pop     bp
    retf
    ; align 2
    db 144*/
loc_198A4:
/*    mov     ax, offset vec_planerotopresult
    push    ax
    mov     ax, 22h         ; sizeof plane
    imul    planindex_copy
    add     ax, word ptr planptr
    mov     dx, word ptr planptr+2
    add     ax, 10h
    push    dx
    push    ax
    mov     ax, offset vec_unk2
	*/
	mat_mul_vector2(&vec_unk2, &planptr[planindex_copy].plane_rotation, &vec_planerotopresult);
	return ;
/*    jmp     int16_t loc_19895
    ; align 2
    db 144*/
loc_198C2:
	if (pState_f36Mminf40sar2 == 0)
		goto loc_1990C;
	if (pState_f36Mminf40sar2 == f36f40_whlData)
		goto loc_198EA;
	mat_rot_y(&mat_unk2, -pState_f36Mminf40sar2);
	f36f40_whlData = pState_f36Mminf40sar2;
/*    cmp     pState_f36Mminf40sar2, 0
    jz      int16_t loc_1990C
    mov     ax, f36f40_whlData
    cmp     pState_f36Mminf40sar2, ax
    jz      int16_t loc_198EA
    mov     ax, pState_f36Mminf40sar2
    neg     ax
    push    ax
    mov     ax, offset mat_unk2
    push    ax
    call    mat_rot_y
    add     sp, 4
    mov     ax, pState_f36Mminf40sar2
    mov     f36f40_whlData, ax*/
loc_198EA:
	mat_mul_vector(&vec_unk2, &mat_unk2, &var_32);
	mat_mul_vector(&var_32, &mat_unk, &vec_planerotopresult);
	return ;
/*    lea     ax, [bp+var_32]
    push    ax
    mov     ax, offset mat_unk2
    push    ax
    mov     ax, offset vec_unk2
    push    ax
    call    mat_mul_vector
    add     sp, 6
    mov     ax, offset vec_planerotopresult
    push    ax
    mov     ax, offset mat_unk
    push    ax
    lea     ax, [bp+var_32]
    jmp     int16_t loc_19917
    ; align 2
    db 144*/
loc_1990C:
	mat_mul_vector(&vec_unk2, &mat_unk, &vec_planerotopresult);
}
/*
    mov     ax, offset vec_planerotopresult
    push    ax
    mov     ax, offset mat_unk
    push    ax
    mov     ax, offset vec_unk2
loc_19917:
    push    ax
    call    mat_mul_vector
    add     sp, 6
    pop     si
    pop     di
    mov     sp, bp
    pop     bp
    retf
plane_rotate_op endp
*/