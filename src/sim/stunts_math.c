#include "stunts_math.h"
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Trigonometric Lookup Tables (Exact DSI 14-bit fixed point, 1.0 = 16384)
 * ------------------------------------------------------------------------- */

static const int16_t s_sintab[257] = {
    0, 101, 201, 302, 402, 503, 603, 704, 804, 904, 1005, 1105, 1205, 1306, 1406, 1506, 
    1606, 1706, 1806, 1906, 2006, 2105, 2205, 2305, 2404, 2503, 2603, 2702, 2801, 2900, 
    2999, 3098, 3196, 3295, 3393, 3492, 3590, 3688, 3786, 3883, 3981, 4078, 4176, 4273, 
    4370, 4467, 4563, 4660, 4756, 4852, 4948, 5044, 5139, 5235, 5330, 5425, 5520, 5614, 
    5708, 5803, 5897, 5990, 6084, 6177, 6270, 6363, 6455, 6547, 6639, 6731, 6823, 6914, 
    7005, 7096, 7186, 7276, 7366, 7456, 7545, 7635, 7723, 7812, 7900, 7988, 8076, 8163, 
    8250, 8337, 8423, 8509, 8595, 8680, 8765, 8850, 8935, 9019, 9102, 9186, 9269, 9352, 
    9434, 9516, 9598, 9679, 9760, 9841, 9921, 10001, 10080, 10159, 10238, 10316, 10394, 
    10471, 10549, 10625, 10702, 10778, 10853, 10928, 11003, 11077, 11151, 11224, 11297, 
    11370, 11442, 11514, 11585, 11656, 11727, 11797, 11866, 11935, 12004, 12072, 12140, 
    12207, 12274, 12340, 12406, 12472, 12537, 12601, 12665, 12729, 12792, 12854, 12916, 
    12978, 13039, 13100, 13160, 13219, 13279, 13337, 13395, 13453, 13510, 13567, 13623, 
    13678, 13733, 13788, 13842, 13896, 13949, 14001, 14053, 14104, 14155, 14206, 14256, 
    14305, 14354, 14402, 14449, 14497, 14543, 14589, 14635, 14680, 14724, 14768, 14811, 
    14854, 14896, 14937, 14978, 15019, 15059, 15098, 15137, 15175, 15213, 15250, 15286, 
    15322, 15357, 15392, 15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649, 15679, 
    15707, 15736, 15763, 15791, 15817, 15843, 15868, 15893, 15917, 15941, 15964, 15986, 
    16008, 16029, 16049, 16069, 16088, 16107, 16125, 16143, 16160, 16176, 16192, 16207, 
    16221, 16235, 16248, 16261, 16273, 16284, 16295, 16305, 16315, 16324, 16332, 16340, 
    16347, 16353, 16359, 16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384, 16384
};

static const uint8_t s_atantable[258] = {
    0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A,
    0x0A, 0x0B, 0x0B, 0x0C, 0x0D, 0x0D, 0x0E, 0x0F, 0x0F, 0x10, 0x10, 0x11, 0x12, 0x12, 0x13, 0x14,
    0x14, 0x15, 0x16, 0x16, 0x17, 0x17, 0x18, 0x19, 0x19, 0x1A, 0x1B, 0x1B, 0x1C, 0x1C, 0x1D, 0x1E,
    0x1E, 0x1F, 0x1F, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x24, 0x24, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x30, 0x30, 0x31,
    0x31, 0x32, 0x33, 0x33, 0x34, 0x34, 0x35, 0x35, 0x36, 0x37, 0x37, 0x38, 0x38, 0x39, 0x39, 0x3A,
    0x3A, 0x3B, 0x3C, 0x3C, 0x3D, 0x3D, 0x3E, 0x3E, 0x3F, 0x3F, 0x40, 0x41, 0x41, 0x42, 0x42, 0x43,
    0x43, 0x44, 0x44, 0x45, 0x45, 0x46, 0x46, 0x47, 0x47, 0x48, 0x48, 0x49, 0x4A, 0x4A, 0x4B, 0x4B,
    0x4C, 0x4C, 0x4D, 0x4D, 0x4E, 0x4E, 0x4F, 0x4F, 0x50, 0x50, 0x51, 0x51, 0x52, 0x52, 0x53, 0x53,
    0x54, 0x54, 0x54, 0x55, 0x55, 0x56, 0x56, 0x57, 0x57, 0x58, 0x58, 0x59, 0x59, 0x5A, 0x5A, 0x5B,
    0x5B, 0x5B, 0x5C, 0x5C, 0x5D, 0x5D, 0x5E, 0x5E, 0x5F, 0x5F, 0x60, 0x60, 0x60, 0x61, 0x61, 0x62,
    0x62, 0x63, 0x63, 0x63, 0x64, 0x64, 0x65, 0x65, 0x66, 0x66, 0x66, 0x67, 0x67, 0x68, 0x68, 0x68,
    0x69, 0x69, 0x6A, 0x6A, 0x6A, 0x6B, 0x6B, 0x6C, 0x6C, 0x6C, 0x6D, 0x6D, 0x6E, 0x6E, 0x6E, 0x6F,
    0x6F, 0x70, 0x70, 0x70, 0x71, 0x71, 0x71, 0x72, 0x72, 0x73, 0x73, 0x73, 0x74, 0x74, 0x74, 0x75,
    0x75, 0x76, 0x76, 0x76, 0x77, 0x77, 0x77, 0x78, 0x78, 0x78, 0x79, 0x79, 0x79, 0x7A, 0x7A, 0x7A,
    0x7B, 0x7B, 0x7B, 0x7C, 0x7C, 0x7C, 0x7D, 0x7D, 0x7D, 0x7E, 0x7E, 0x7E, 0x7F, 0x7F, 0x7F, 0x80,
    0x80, 0x00
};

int16_t stunts_sin(uint16_t angle) {
    uint8_t c = (uint8_t)(angle & 0xFF);
    switch ((angle >> 8) & 3) {
        case 0: return s_sintab[c];
        case 1: return s_sintab[0x100 - c];
        case 2: return -s_sintab[c];
        case 3: return -s_sintab[0x100 - c];
        default: return 0;
    }
}

int16_t stunts_cos(uint16_t angle) {
    return stunts_sin(angle + 0x100);
}

int16_t stunts_polar_angle(int16_t z, int16_t y) {
    uint8_t flag = 0;
    int32_t temp, result;
    uint32_t index;

    if (z < 0) {
        flag |= 4;
        z = -z;
    }
    if (y < 0) {
        flag |= 2;
        y = -y;
    }

    if (z == y) {
        if (z == 0) return 0;
        result = 0x80;
    } else {
        if (z > y) {
            temp = z;
            z = y;
            y = (int16_t)temp;
            flag |= 1;
        }
        if (y == 0) return 0;
        index = (((uint32_t)z << 16) / (uint32_t)y);
        if ((index & 0xFF) >= 0x80) {
            index += 0x100;
        }
        uint32_t tab_idx = index >> 8;
        if (tab_idx > 256) tab_idx = 256;
        result = s_atantable[tab_idx];
    }

    switch (flag) {
        case 0: return (int16_t)result;
        case 1: return (int16_t)(-result + 0x100);
        case 2: return (int16_t)(-result + 0x200);
        case 3: return (int16_t)(result + 0x100);
        case 4: return (int16_t)(-result);
        case 5: return (int16_t)(result - 0x100);
        case 6: return (int16_t)(result - 0x200);
        case 7: return (int16_t)(-(result + 0x100));
        default: return 0;
    }
}

int32_t stunts_polar_radius_2d(int16_t z, int16_t y) {
    int32_t angle = stunts_polar_angle(z, y);
    if (angle < 0) angle = -angle;
    if (angle >= 0x100) angle = 0x200 - angle;
    if (angle == 0) return abs(y);
    int16_t c = stunts_cos((uint16_t)angle);
    if (c == 0) return abs(z);
    return ((int32_t)abs(y) * 16384) / c;
}

int32_t stunts_polar_radius_3d(const stunts_vector_t* vec) {
    int32_t r2d = stunts_polar_radius_2d(vec->z, vec->x);
    return stunts_polar_radius_2d((int16_t)r2d, vec->y);
}

/* -------------------------------------------------------------------------
 * Matrix Transformations
 * ------------------------------------------------------------------------- */

void stunts_mat_identity(stunts_matrix_t* mat) {
    memset(mat, 0, sizeof(stunts_matrix_t));
    mat->m[0][0] = 16384;
    mat->m[1][1] = 16384;
    mat->m[2][2] = 16384;
}

void stunts_mat_rot_x(stunts_matrix_t* out, int16_t angle) {
    stunts_mat_identity(out);
    int16_t s = stunts_sin((uint16_t)angle);
    int16_t c = stunts_cos((uint16_t)angle);
    out->m[1][1] = c;
    out->m[1][2] = -s;
    out->m[2][1] = s;
    out->m[2][2] = c;
}

void stunts_mat_rot_y(stunts_matrix_t* out, int16_t angle) {
    stunts_mat_identity(out);
    int16_t s = stunts_sin((uint16_t)angle);
    int16_t c = stunts_cos((uint16_t)angle);
    out->m[0][0] = c;
    out->m[0][2] = s;
    out->m[2][0] = -s;
    out->m[2][2] = c;
}

void stunts_mat_rot_z(stunts_matrix_t* out, int16_t angle) {
    stunts_mat_identity(out);
    int16_t s = stunts_sin((uint16_t)angle);
    int16_t c = stunts_cos((uint16_t)angle);
    out->m[0][0] = c;
    out->m[0][1] = -s;
    out->m[1][0] = s;
    out->m[1][1] = c;
}

void stunts_mat_multiply(const stunts_matrix_t* a, const stunts_matrix_t* b, stunts_matrix_t* out) {
    stunts_matrix_t res;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int32_t sum = ((int32_t)a->m[r][0] * b->m[0][c] +
                           (int32_t)a->m[r][1] * b->m[1][c] +
                           (int32_t)a->m[r][2] * b->m[2][c]);
            res.m[r][c] = (int16_t)(sum >> 14);
        }
    }
    *out = res;
}

void stunts_mat_rot_zxy(stunts_matrix_t* out, int16_t z, int16_t x, int16_t y) {
    stunts_matrix_t mz, mx, my, temp;
    stunts_mat_rot_z(&mz, z);
    stunts_mat_rot_x(&mx, x);
    stunts_mat_rot_y(&my, y);
    stunts_mat_multiply(&mz, &mx, &temp);
    stunts_mat_multiply(&temp, &my, out);
}

void stunts_mat_invert(const stunts_matrix_t* in, stunts_matrix_t* out) {
    /* For orthogonal rotation matrices, inverse is transpose */
    stunts_matrix_t res;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            res.m[r][c] = in->m[c][r];
        }
    }
    *out = res;
}

void stunts_mat_mul_vector(const stunts_vector_t* in_vec, const stunts_matrix_t* mat, stunts_vector_t* out_vec) {
    int32_t x = ((int32_t)mat->m[0][0] * in_vec->x + (int32_t)mat->m[0][1] * in_vec->y + (int32_t)mat->m[0][2] * in_vec->z) >> 14;
    int32_t y = ((int32_t)mat->m[1][0] * in_vec->x + (int32_t)mat->m[1][1] * in_vec->y + (int32_t)mat->m[1][2] * in_vec->z) >> 14;
    int32_t z = ((int32_t)mat->m[2][0] * in_vec->x + (int32_t)mat->m[2][1] * in_vec->y + (int32_t)mat->m[2][2] * in_vec->z) >> 14;
    out_vec->x = (int16_t)x;
    out_vec->y = (int16_t)y;
    out_vec->z = (int16_t)z;
}

void stunts_mat_mul_vector_long(const stunts_vector_long_t* in_vec, const stunts_matrix_t* mat, stunts_vector_long_t* out_vec) {
    int64_t x = ((int64_t)mat->m[0][0] * in_vec->lx + (int64_t)mat->m[0][1] * in_vec->ly + (int64_t)mat->m[0][2] * in_vec->lz) >> 14;
    int64_t y = ((int64_t)mat->m[1][0] * in_vec->lx + (int64_t)mat->m[1][1] * in_vec->ly + (int64_t)mat->m[1][2] * in_vec->lz) >> 14;
    int64_t z = ((int64_t)mat->m[2][0] * in_vec->lx + (int64_t)mat->m[2][1] * in_vec->ly + (int64_t)mat->m[2][2] * in_vec->lz) >> 14;
    out_vec->lx = (int32_t)x;
    out_vec->ly = (int32_t)y;
    out_vec->lz = (int32_t)z;
}

/* -------------------------------------------------------------------------
 * Collision Planes & Geometry Operations
 * ------------------------------------------------------------------------- */

int32_t stunts_vec_normal_inner_product(int16_t x, int16_t y, int16_t z, const stunts_vector_t* normal) {
    return ((int32_t)x * normal->x + (int32_t)y * normal->y + (int32_t)z * normal->z) >> 14;
}

int32_t stunts_plane_height_at(const stunts_plane_t* plane, int16_t tile_x, int16_t tile_z) {
    if (plane->normal.y == 0) return plane->origin.y;
    int32_t dx = (int32_t)tile_x - plane->origin.x;
    int32_t dz = (int32_t)tile_z - plane->origin.z;
    int32_t num = (int32_t)plane->normal.x * dx + (int32_t)plane->normal.z * dz;
    return plane->origin.y - (num / plane->normal.y);
}

bool stunts_rect_intersect(const stunts_rectangle_t* r1, const stunts_rectangle_t* r2) {
    return !(r1->right < r2->left || r1->left > r2->right ||
             r1->bottom < r2->top || r1->top > r2->bottom);
}

bool stunts_rect_is_inside(const stunts_rectangle_t* inner, const stunts_rectangle_t* outer) {
    return (inner->left >= outer->left && inner->right <= outer->right &&
            inner->top >= outer->top && inner->bottom <= outer->bottom);
}
