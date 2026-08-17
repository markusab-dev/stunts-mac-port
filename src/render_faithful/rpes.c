/*
 * rpes.c - the .PES container, ported from the restunts C sources that were
 * themselves transcribed from seg012:
 *
 *   file_unflip_shape2d_pes        shape2d.c 345..385
 *   file_load_shape2d_expand       shape2d.c 387..455
 *   file_load_shape2d_expandedsize shape2d.c 476..491
 *   file_load_shape2d_palmap_init  shape2d.c 491..497
 *   file_load_shape2d_palmap_apply shape2d.c 499..516
 *   file_load_shape2d_esh          shape2d.c 518..541
 *   file_load_shape2d, the .PES arm shape2d.c 583..592
 *
 * ------------------------------------------------------------------------
 * THE FORMAT, read out of SDCRED.PES rather than assumed
 *
 * A .PES shape's SHAPE2D header is the ordinary 16-byte one with two
 * differences:
 *
 *   * s2d_width counts BYTES, not pixels - the expanded shape is eight
 *     times as wide;
 *   * the four bytes s2d_unk3..s2d_unk6 carry, in their LOW nibbles, the
 *     colour each of the four bitplanes contributes.  The first zero ends
 *     the list, so a shape has 1..4 planes and its payload is
 *     width*height*planes bytes.  s2d_unk4's HIGH nibble is the background
 *     the expansion starts from, and s2d_unk5's HIGH nibble is the same
 *     "this plane is stored transposed" mask a .PVS shape carries.
 *
 * For SDCRED.PES, measured:
 *
 *     name  w  h  unk3 unk4 unk5 unk6  planes  bytes  expands to
 *     !cg0 16  4   01   02   04   08     4      272   128x4
 *     arow 10 65   01   06   78   00     3     1968    80x65  at (16,132)
 *     arrw 25 29   61   08   00   00     2     1466   200x29  at (40,170)
 *     arw1 26 31   61   08   00   00     2     1628   208x31  at (32,168)
 *     arw2 25 26   61   08   00   00     2     1316   200x26  at (24,168)
 *     arw3 26 29   61   08   00   00     2     1524   208x29  at (16,165)
 *     arw4 23 45   61   08   30   00     2     2086   184x45  at  (8,150)
 *     arw5 23 50   61   08   30   00     2     2316   184x50  at  (8,145)
 *     arw6 18 64   61   08   30   00     2     2320   144x64  at (16,130)
 *     arw7 18 63   61   08   30   00     2     2284   144x63  at (16,131)
 *     arw8 12 63   61   08   30   00     2     1528    96x63  at (16,131)
 *     type 11 28   0F   00   10   00     1      324    88x28  at (96,168)
 *
 * so every arrow frame is two planes wide (colours 1 and 8) over a black
 * background, and `arow` adds a third (colour 8 out of s2d_unk5's low
 * nibble, on top of 1 and 6).
 *
 * palmap[] is the 16-entry remap file_load_shape2d_esh applies at the end.
 * It is initialised from an "!MGA" resource when the archive has one, and
 * dseg.asm:21699 seeds it with the identity 0..15.  SDCRED.PES carries
 * "!cg0" (the CGA map) and no "!MGA", so in VGA mode the identity stands
 * and the expansion is what reaches the screen.  Checked, not assumed:
 * no file in the shipped data set contains the bytes "!MGA".
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] - two, both stated plainly.
 *
 *  1. The original expands the WHOLE archive into one new chunk at load
 *     time and frees the planar one; this port expands one shape at a time,
 *     on first lookup, and caches the result.  Same pixels - the expansion
 *     is per-shape in the original too (file_load_shape2d_expand's outer
 *     loop) - but the archive handle the caller holds still points at the
 *     planar bytes, which is why the shapes must be fetched through
 *     pes_locate_shape() rather than locate_shape_alt().
 *
 *  2. The expanded header keeps the source header (that is what
 *     `*dstshape = *srcshape` does) except that s2d_width is multiplied by
 *     8 - and, here only, s2d_unk5's HIGH nibble is cleared.  The original
 *     never runs file_unflip_shape2d over a .PES result, so the stale flip
 *     mask does it no harm; this port's house rule is to put every 2D shape
 *     through unflip_shape(), and a stale mask of 7 would make that routine
 *     transpose an already-correct picture.  Clearing it makes unflip_shape
 *     the no-op it should be.  The low nibble - plane 2's colour, already
 *     consumed - is left alone.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "shape2d.h"
#include "rpes.h"

extern void* file_load_resource(int16_t restype, const char* filename);
extern void* locate_shape_nofatal(void* resptr, const char* shapename);

/* dseg.asm:21699 - the identity, which is what a VGA run keeps. */
uint8_t palmap[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* shape2d.c:491 */
void file_load_shape2d_palmap_init(uint8_t far* pal)
{
	int i;
	for (i = 0; i < 0x10; ++i) palmap[i] = pal[i];
}

/* ------------------------------------------------------------------ */
/* The expanded-shape cache                                            */
/* ------------------------------------------------------------------ */
struct pes_entry {
	void far* res;
	char name[8];
	void far* shape;
	struct pes_entry* next;
};
static struct pes_entry* s_cache;

void pes_release(void far* res)
{
	struct pes_entry** pp = &s_cache;
	while (*pp) {
		if ((*pp)->res == res) {
			struct pes_entry* e = *pp;
			*pp = e->next;
			free(e->shape);
			free(e);
		} else {
			pp = &(*pp)->next;
		}
	}
}

/* ------------------------------------------------------------------ */
/* shape2d.c:583 - the .PES arm of file_load_shape2d.                  */
/* ------------------------------------------------------------------ */
void far* file_load_resource_pes(const char* basename)
{
	char name[80];
	void far* res;
	void far* mga;

	snprintf(name, sizeof name, "%s.pes", basename);
	res = file_load_resource(2, name);
	if (!res) return NULL;

	/* shape2d.c:525 - the palette map, when the archive carries one. */
	mga = locate_shape_nofatal(res, "!MGA");
	if (!mga) mga = locate_shape_nofatal(res, "!mga");
	if (mga)
		file_load_shape2d_palmap_init((uint8_t far*)mga
		                              + sizeof(struct SHAPE2D));
	return res;
}

/* How many bitplanes this shape has: the first zero colour ends the list
 * (file_load_shape2d_expand's `else break`). */
static int pes_planes(const struct SHAPE2D far* s)
{
	const uint8_t far* nib = &s->s2d_unk3;
	int j;
	for (j = 0; j < 4; ++j)
		if ((nib[j] & 0x0F) == 0) break;
	return j;
}

void far* pes_locate_shape(void far* res, const char* nm)
{
	struct pes_entry* e;
	const struct SHAPE2D far* src;
	struct SHAPE2D far* dst;
	uint8_t* planes;
	uint8_t* out;
	int16_t w, h;
	int32_t length;
	int nplanes, j;
	uint32_t k;

	if (!res || !nm) return NULL;
	for (e = s_cache; e; e = e->next)
		if (e->res == res && !strcmp(e->name, nm)) return e->shape;

	src = (const struct SHAPE2D far*)locate_shape_nofatal(res, nm);
	if (!src) return NULL;

	w = (int16_t)src->s2d_width;
	h = (int16_t)src->s2d_height;
	length = (int32_t)w * h;
	nplanes = pes_planes(src);
	if (w <= 0 || h <= 0 || nplanes <= 0) return NULL;
	/* file_load_shape2d_expand's own guard (shape2d.c:422). */
	if (length > 8000) return NULL;

	/* ---- file_unflip_shape2d_pes, on a private copy so the archive's
	 * own bytes stay planar and a second intro loop expands identically.
	 * The original transposes in place, which it can because it throws
	 * the planar chunk away immediately afterwards. ---------------- */
	planes = (uint8_t*)malloc((size_t)length * nplanes);
	if (!planes) return NULL;
	memcpy(planes, (const uint8_t far*)src + sizeof(struct SHAPE2D),
	       (size_t)length * nplanes);

	if (!(src->s2d_unk6 & 0xF0)) {
		uint8_t val = (uint8_t)((src->s2d_unk5 >> 4) & 0x0F);
		if (val) {
			uint8_t* p = planes;
			uint8_t* tmp = (uint8_t*)malloc((size_t)length);
			if (!tmp) { free(planes); return NULL; }
			for (j = 0; j < 4; ++j) {
				if (j < nplanes && (val & 0x01)) {
					int16_t x, y;
					for (y = 0; y < h; ++y)
						for (x = 0; x < w; ++x)
							tmp[y * w + x] = p[x * h + y];
					memcpy(p, tmp, (size_t)length);
				}
				p += length;
				val >>= 1;
			}
			free(tmp);
		}
	}

	/* ---- file_load_shape2d_expand, one shape ---------------------- */
	out = (uint8_t*)malloc(sizeof(struct SHAPE2D) + (size_t)length * 8);
	if (!out) { free(planes); return NULL; }
	dst = (struct SHAPE2D far*)out;
	*dst = *src;
	dst->s2d_width = (uint16_t)(w * 8);
	dst->s2d_unk5 = (uint8_t)(dst->s2d_unk5 & 0x0F);   /* [DEVIATION] 2 */

	memset(out + sizeof(struct SHAPE2D),
	       (int)((src->s2d_unk4 >> 4) & 0x0F), (size_t)length * 8);

	{
		const uint8_t far* nib = &src->s2d_unk3;
		const uint8_t* sp = planes;
		for (j = 0; j < 4; ++j) {
			uint8_t pat = (uint8_t)(nib[j] & 0x0F);
			uint8_t* dp;
			if (!pat) break;
			dp = out + sizeof(struct SHAPE2D);
			for (k = 0; k < (uint32_t)length; ++k) {
				uint8_t px = *sp++;
				int l;
				for (l = 0; l < 8; ++l) {
					if (px & 0x80) *dp |= pat;
					px = (uint8_t)(px << 1);
					dp++;
				}
			}
		}
	}
	free(planes);

	/* ---- file_load_shape2d_palmap_apply --------------------------- */
	{
		uint8_t* dp = out + sizeof(struct SHAPE2D);
		for (k = 0; k < (uint32_t)length * 8; ++k, ++dp)
			*dp = palmap[*dp & 0x0F];
	}

	e = (struct pes_entry*)malloc(sizeof *e);
	if (!e) { free(out); return NULL; }
	e->res = res;
	snprintf(e->name, sizeof e->name, "%s", nm);
	e->shape = out;
	e->next = s_cache;
	s_cache = e;
	return out;
}

void pes_locate_many(void far* res, const char* names, void far** result)
{
	size_t i;
	for (i = 0; names[i * 4]; i++) {
		char name[5];
		memcpy(name, names + i * 4, 4);
		name[4] = 0;
		result[i] = pes_locate_shape(res, name);
	}
}
