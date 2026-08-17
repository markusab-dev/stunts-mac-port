/*
 * rshape2d.c - the cockpit: 2D bitmap shapes and the dashboard panel.
 *
 * Ported from
 *   seg012.asm  shape2d_op_unk3 (218), shape2d_op_unk (85)
 *   seg005.asm  setup_car_shapes (950) - modes 0 and 1 only; see below
 *   seg003.asm  locate_many_resources
 *
 * WHAT THE COCKPIT IS MADE OF
 * ---------------------------
 * Two resource archives per car, named after the four-character car id:
 *
 *   STDA<id>.PVS   whl1 whl2 whl3 ins2 gbox ins1 ins3 inm1 inm3, plus
 *                  "dash", "roof" and the two palette maps !cg0 / !eg0
 *   STDB<id>.PVS   gnob gnab dot<sp> dota dot1 dot2  (the gear knob)
 *
 * Every shape is a 16-byte SHAPE2D header followed by width*height bytes of
 * 8-bit pixels - no compression and no VGA plane interleave, because
 * file_load_resource(3, ...) already ran the shapes through parse_shape2d at
 * load time. Verified, not assumed: for all eleven STDA*.PVS files, every
 * shape's slot in the archive is exactly width*height+16 bytes.
 *
 * The header's s2d_pos_x / s2d_pos_y place the shape on the 320x200 screen
 * directly, so the cockpit needs no layout logic of its own. For the Countach:
 *
 *   roof  320x9  at (0,0)      dash 320x70 at (0,130)
 *   gbox   64x56 at (256,144)  whl1 184x32 at (40,168)
 *
 * That is where roofbmpheight (= roof's height) and dashbmp_y (= dash's y)
 * come from, and those two numbers are what shrink the 3D view to the
 * windshield: rows [roofbmpheight, dashbmp_y).
 *
 * [ODDITY] Mode 1 draws "roof" through shape2d_op_unk, which is an RLE
 * blitter (positive byte = run of one colour, negative = literal run, 0 =
 * end), while "dash" and "gbox" go through the raw blitter shape2d_op_unk3.
 * But the roof shapes on disk are raw like all the others - decoding one as
 * RLE terminates after 37 of its 2880 pixels. Since parse_shape2d has already
 * expanded everything by this point, the RLE path cannot be reachable with
 * these assets, so roof is drawn raw here and shape2d_op_unk is a synonym for
 * shape2d_op_unk3. If a car's roof ever renders as garbage, this is the note
 * to come back to.
 *
 * Mode 2 (the moving parts - needles, steering wheel, gear knob) is not here
 * yet; it needs the offscreen sprite-window compositing that mode 2 uses.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "rfbsize.h"
#include "shape2d.h"

extern uint8_t rfb_pixels[];
extern struct SPRITE sprite1;

void far* stdaresptr;
void far* stdbresptr;
void far* whlshapes[9];   /* whl1 whl2 whl3 ins2 gbox ins1 ins3 inm1 inm3 */
void far* gnobshapes[6];  /* gnob gnab "dot " dota dot1 dot2              */

/* Set by setup_car_shapes(0); main_native.c reads them to size the view. */
extern int16_t roofbmpheight;   /* rdata.c */
extern int16_t dashbmp_y;       /* rdata.c */
extern int16_t meter_needle_color;
extern struct SIMD simd_player;

extern uint32_t* rs_rgba;
extern const uint32_t* rs_pal;
static const uint8_t* find_hires(const void far* shape, int16_t* w);
static const uint32_t* find_hires_rgb(const void far* shape, int16_t* w);
static void put(int32_t off, uint8_t idx);

/* ------------------------------------------------------------------ */
/* seg003 locate_many_resources: `names` is a run of four-character
 * resource names with no separators; each is looked up in turn.        */
/* ------------------------------------------------------------------ */
void locate_many_resources(char far* data, char* names, char far** result)
{
	size_t i;
	for (i = 0; names[i * 4]; i++) {
		char name[5];
		memcpy(name, names + i * 4, 4);
		name[4] = 0;
		result[i] = locate_shape_nofatal(data, name);
	}
}

/* ------------------------------------------------------------------ */
/* seg012 shape2d_op_unk3: blit a raw shape at its own s2d_pos, clipped
 * to the sprite1 window. Opaque - every source byte is written, colour
 * 0 included; that is what lets the dash panel overwrite the 3D view.  */
/* ------------------------------------------------------------------ */
void shape2d_op_unk3(void far* arg_shape)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h, x, y;
	int16_t clip_l, clip_r, clip_t, clip_b;
	int16_t sx0, sy0, sx1, sy1;
	int16_t row;

	if (!shape) return;

	/* The header positions and sizes the shape in the original's 320x200
	 * screen. The framebuffer may be RFB_SCALE times that, so the
	 * destination rectangle scales and each source pixel becomes an
	 * RFB_SCALE x RFB_SCALE block. At scale 1 this is the plain copy it
	 * was, byte for byte. */
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	x = (int16_t)(shape->s2d_pos_x * RFB_SCALE);
	y = (int16_t)(shape->s2d_pos_y * RFB_SCALE);
	src = (const uint8_t far*)arg_shape + sizeof(struct SHAPE2D);

	clip_l = (int16_t)sprite1.sprite_left;
	clip_r = (int16_t)sprite1.sprite_right;
	clip_t = (int16_t)sprite1.sprite_top;
	clip_b = (int16_t)sprite1.sprite_height;
	if (clip_l < 0) clip_l = 0;
	if (clip_t < 0) clip_t = 0;
	if (clip_r > RFB_VIEW_W) clip_r = RFB_VIEW_W;
	if (clip_b > RFB_VIEW_H) clip_b = RFB_VIEW_H;

	/* Intersect the shape's rectangle with the window. */
	sx0 = x > clip_l ? x : clip_l;
	sy0 = y > clip_t ? y : clip_t;
	sx1 = (int16_t)(x + w * RFB_SCALE) < clip_r
	      ? (int16_t)(x + w * RFB_SCALE) : clip_r;
	sy1 = (int16_t)(y + h * RFB_SCALE) < clip_b
	      ? (int16_t)(y + h * RFB_SCALE) : clip_b;
	if (sx0 >= sx1 || sy0 >= sy1) return;

	{
		int16_t hw = 0, hwr = 0;
		const uint8_t* hi = find_hires(arg_shape, &hw);
		const uint32_t* hirgb = rs_rgba ? find_hires_rgb(arg_shape, &hwr) : NULL;
		for (row = sy0; row < sy1; row++) {
			int32_t base = (int32_t)row * RFB_VIEW_W;
			int16_t col;
			if (hirgb) {
				const uint32_t* srow = hirgb + (int32_t)(row - y) * hwr;
				for (col = sx0; col < sx1; col++)
					rs_rgba[base + col] = srow[col - x];
			} else if (hi) {
				const uint8_t* srow = hi + (int32_t)(row - y) * hw;
				for (col = sx0; col < sx1; col++)
					put(base + col, srow[col - x]);
			} else {
				const uint8_t far* srow =
					src + (int32_t)((row - y) / RFB_SCALE) * w;
				for (col = sx0; col < sx1; col++)
					put(base + col, srow[(col - x) / RFB_SCALE]);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* file_unflip_shape2d (restunts shape2d.c:244, from seg012)            */
/*                                                                      */
/* Some shapes are stored transposed - column-major instead of          */
/* row-major - and a nibble in the header says so:                      */
/*                                                                      */
/*     if ((s2d_unk6 & 0xF0) == 0) flip = s2d_unk5 >> 4;                */
/*         1 = plain transpose                                          */
/*         2 = transpose with the rows interlaced into two halves       */
/*         3 = a third form the original also refuses to handle         */
/*                                                                      */
/* This is why the earlier "every shape is width*height+16 bytes, so    */
/* they must all be raw" check was not enough: it ruled out compression */
/* but a transposed image occupies exactly the same number of bytes.    */
/* Only a few shapes per car carry it - for the Countach just "gbox"    */
/* and the gear-knob pieces in STDB, but for the Porsche 962 and the    */
/* Corvette the instrument cluster "ins2" as well.                      */
/*                                                                      */
/* [DEVIATION] The original unflips every shape once as part of loading */
/* the archive. Ours does it per shape as setup_car_shapes(0) looks     */
/* them up, and clears the nibble afterwards so that a second pass over */
/* a cached archive cannot transpose the same image twice.              */
/* ------------------------------------------------------------------ */
static uint8_t s_unflip_buf[320 * 200];

/* ------------------------------------------------------------------ */
/* Optional: Scale2x (also called EPX) on the cockpit artwork.          */
/*                                                                      */
/* The 3D view genuinely renders at RFB_SCALE resolution - real         */
/* geometry, real edges - while the cockpit is 320-wide art from 1990   */
/* enlarged by pixel replication, so the panel looks blocky next to it. */
/*                                                                      */
/* Scale2x is the right tool for this specific job rather than anything */
/* cleverer, for one reason: it NEVER invents a colour. Each output     */
/* pixel is a copy of an input pixel, chosen by looking at the four     */
/* orthogonal neighbours, so the result still lives entirely inside the */
/* game's 256-colour palette and needs no change to the framebuffer.    */
/* A blending scaler - or an AI model - would produce colours with no   */
/* palette index and force the whole compositing path to truecolour.    */
/*                                                                      */
/*   E0 = (C==A && C!=D && A!=B) ? A : P      A = above                 */
/*   E1 = (A==B && A!=C && B!=D) ? B : P      B = right                 */
/*   E2 = (D==C && D!=B && C!=A) ? C : P      C = left                  */
/*   E3 = (B==D && B!=A && D!=C) ? D : P      D = below                 */
/*                                                                      */
/* Applied once for 2x and twice for 4x. Edges clamp.                   */
/* ------------------------------------------------------------------ */
int rshape2d_smooth;          /* set from the command line before loading */

/* Truecolour output. When rs_rgba is set, the cockpit is composited into a
 * 32-bit buffer instead of the palette-indexed framebuffer, which is what
 * lets the enlarged artwork carry colours the game's 256-entry palette does
 * not contain. The 3D view is untouched: it renders indexed as always and is
 * converted to RGB first, and the cockpit is drawn over the result. */
/* defined here; forward-declared above */
uint32_t* rs_rgba;
const uint32_t* rs_pal;       /* 256 packed 0xFFRRGGBB entries */

#define HIRES_MAX 32
static struct {
	const void far* shape;
	uint8_t* bits;            /* palette indices, RFB_SCALE times up   */
	uint32_t* rgb;            /* the same, blended - truecolour only   */
	int16_t w, h;
} s_hires[HIRES_MAX];
static int s_hires_n;

static void scale2x(const uint8_t* src, int16_t w, int16_t h, uint8_t* dst)
{
	int16_t x, y;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			uint8_t P = src[y * w + x];
			uint8_t A = src[(y > 0 ? y - 1 : y) * w + x];
			uint8_t D = src[(y < h - 1 ? y + 1 : y) * w + x];
			uint8_t C = src[y * w + (x > 0 ? x - 1 : x)];
			uint8_t B = src[y * w + (x < w - 1 ? x + 1 : x)];
			uint8_t e0 = P, e1 = P, e2 = P, e3 = P;
			if (C == A && C != D && A != B) e0 = A;
			if (A == B && A != C && B != D) e1 = B;
			if (D == C && D != B && C != A) e2 = C;
			if (B == D && B != A && D != C) e3 = D;
			dst[(y * 2) * (w * 2) + x * 2]         = e0;
			dst[(y * 2) * (w * 2) + x * 2 + 1]     = e1;
			dst[(y * 2 + 1) * (w * 2) + x * 2]     = e2;
			dst[(y * 2 + 1) * (w * 2) + x * 2 + 1] = e3;
		}
	}
}

/* One 1-6-1 separable pass over an RGB image.
 *
 * Run on the Scale2x output this is exactly an anti-alias: inside a flat area
 * every neighbour is the same colour, so averaging them changes nothing, and
 * the only pixels that move are the ones on a colour boundary. That is what
 * turns the remaining staircase on a gauge bezel into a curve without
 * touching the numerals printed inside it. The centre weight is deliberately
 * high - the art is full of one-pixel-wide white digits and needles, and a
 * flatter kernel turns them to mush. */
static void blend_pass(uint32_t* img, int16_t w, int16_t h, uint32_t* tmp)
{
	int16_t x, y;
	int c;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			uint32_t l = img[y * w + (x > 0 ? x - 1 : x)];
			uint32_t m = img[y * w + x];
			uint32_t r = img[y * w + (x < w - 1 ? x + 1 : x)];
			uint32_t o = 0xFF000000u;
			for (c = 0; c < 24; c += 8) {
				unsigned v = (((l >> c) & 0xFF) + 6 * ((m >> c) & 0xFF)
				              + ((r >> c) & 0xFF) + 4) / 8;
				o |= v << c;
			}
			tmp[y * w + x] = o;
		}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			uint32_t u = tmp[(y > 0 ? y - 1 : y) * w + x];
			uint32_t m = tmp[y * w + x];
			uint32_t d = tmp[(y < h - 1 ? y + 1 : y) * w + x];
			uint32_t o = 0xFF000000u;
			for (c = 0; c < 24; c += 8) {
				unsigned v = (((u >> c) & 0xFF) + 6 * ((m >> c) & 0xFF)
				              + ((d >> c) & 0xFF) + 4) / 8;
				o |= v << c;
			}
			img[y * w + x] = o;
		}
}

/* ------------------------------------------------------------------ */
/* Exporting the artwork, and importing an upscaled replacement.        */
/*                                                                      */
/* Nothing in this port can run an image model, so the intelligent      */
/* upscale - the one that looks at a gauge and understands it is a      */
/* circular bezel with a needle over it - has to happen outside. These  */
/* two hooks are the seam for that:                                     */
/*                                                                      */
/*   --export-cockpit <dir>  writes every cockpit shape as a 24-bit BMP */
/*   --import-cockpit <dir>  loads <tag>.bmp back, at RFB_SCALE times   */
/*                           the original size, and uses it instead of  */
/*                           anything computed here                     */
/*                                                                      */
/* Whatever produced the replacement is irrelevant to the engine: a     */
/* super-resolution model, xBRZ, or a hand redraw all land in the same  */
/* slot. The import checks the dimensions and refuses anything else, so */
/* a wrong-sized file fails loudly instead of rendering skewed.         */
/* ------------------------------------------------------------------ */
const char* rshape2d_export_dir;
const char* rshape2d_import_dir;

static void bmp_write(const char* path, const uint8_t* idx, int16_t w, int16_t h)
{
	FILE* f = fopen(path, "wb");
	uint32_t rowb, pad, img, fsz;
	uint8_t hdr[54] = {0};
	int32_t ww = w, hh = h;
	int16_t x, y;
	if (!f || !rs_pal) { if (f) fclose(f); return; }
	rowb = (uint32_t)w * 3; pad = (4 - (rowb % 4)) % 4;
	img = (rowb + pad) * (uint32_t)h; fsz = 54 + img;
	hdr[0]='B'; hdr[1]='M'; memcpy(hdr+2,&fsz,4); hdr[10]=54; hdr[14]=40;
	memcpy(hdr+18,&ww,4); memcpy(hdr+22,&hh,4); hdr[26]=1; hdr[28]=24;
	memcpy(hdr+34,&img,4); fwrite(hdr,1,54,f);
	for (y = (int16_t)(h - 1); y >= 0; y--) {
		for (x = 0; x < w; x++) {
			uint32_t c = rs_pal[idx[y * w + x]];
			uint8_t bgr[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
			fwrite(bgr, 1, 3, f);
		}
		for (uint32_t q = 0; q < pad; q++) fputc(0, f);
	}
	fclose(f);
}

/* Read a 24-bit BMP into packed 0xFFRRGGBB. Returns NULL unless it is
 * exactly want_w x want_h. */
static uint32_t* bmp_read(const char* path, int16_t want_w, int16_t want_h)
{
	FILE* f = fopen(path, "rb");
	uint8_t hdr[54];
	uint32_t off, rowb, pad;
	int32_t w, h;
	uint16_t bpp;
	uint32_t* out;
	int16_t x, y;
	if (!f) return NULL;
	if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
		fclose(f); return NULL;
	}
	memcpy(&off, hdr + 10, 4);
	memcpy(&w, hdr + 18, 4);
	memcpy(&h, hdr + 22, 4);
	memcpy(&bpp, hdr + 28, 2);
	if (bpp != 24 || w != want_w || (h < 0 ? -h : h) != want_h) {
		fprintf(stderr, "import: %s ar %dx%d/%u-bit, vantade %dx%d/24-bit\n",
		        path, (int)w, (int)h, (unsigned)bpp, want_w, want_h);
		fclose(f); return NULL;
	}
	out = (uint32_t*)malloc((size_t)want_w * want_h * 4);
	if (!out) { fclose(f); return NULL; }
	rowb = (uint32_t)want_w * 3; pad = (4 - (rowb % 4)) % 4;
	for (y = (int16_t)(want_h - 1); y >= 0; y--) {
		int16_t ty = (h < 0) ? (int16_t)(want_h - 1 - y) : y;
		fseek(f, (long)off + (long)(want_h - 1 - y) * (rowb + pad), SEEK_SET);
		for (x = 0; x < want_w; x++) {
			uint8_t bgr[3];
			if (fread(bgr, 1, 3, f) != 3) { free(out); fclose(f); return NULL; }
			out[ty * want_w + x] = 0xFF000000u | ((uint32_t)bgr[2] << 16)
			                     | ((uint32_t)bgr[1] << 8) | bgr[0];
		}
	}
	fclose(f);
	return out;
}

/* Build an RFB_SCALE-times copy of a shape, if smoothing is on. */
static void make_hires(void far* arg_shape, const char* tag)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t* src;
	uint8_t *a = NULL, *b = NULL;
	int16_t w, h, step;

	if (!shape || s_hires_n >= HIRES_MAX) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	src = (const uint8_t*)arg_shape + sizeof(struct SHAPE2D);

	if (rshape2d_export_dir) {
		char path[600];
		snprintf(path, sizeof(path), "%s/%s.bmp", rshape2d_export_dir, tag);
		bmp_write(path, src, w, h);
	}
	if (rshape2d_import_dir && rs_rgba) {
		char path[600];
		uint32_t* got;
		snprintf(path, sizeof(path), "%s/%s.bmp", rshape2d_import_dir, tag);
		got = bmp_read(path, (int16_t)(w * RFB_SCALE), (int16_t)(h * RFB_SCALE));
		if (got) {
			s_hires[s_hires_n].shape = arg_shape;
			s_hires[s_hires_n].bits = NULL;
			s_hires[s_hires_n].rgb = got;
			s_hires[s_hires_n].w = (int16_t)(w * RFB_SCALE);
			s_hires[s_hires_n].h = (int16_t)(h * RFB_SCALE);
			s_hires_n++;
			return;
		}
	}
	if (!rshape2d_smooth || RFB_SCALE < 2) return;

	for (step = 2; step <= RFB_SCALE; step *= 2) {
		b = (uint8_t*)malloc((size_t)w * 2 * h * 2);
		if (!b) { free(a); return; }
		scale2x(src, w, h, b);
		free(a);
		a = b; src = a; w = (int16_t)(w * 2); h = (int16_t)(h * 2);
	}
	if (!a) return;

	s_hires[s_hires_n].shape = arg_shape;
	s_hires[s_hires_n].bits = a;
	s_hires[s_hires_n].rgb = NULL;
	s_hires[s_hires_n].w = w;
	s_hires[s_hires_n].h = h;

	/* In truecolour, follow Scale2x with as many blend passes as the scale
	 * factor supports - one per doubling. */
	if (rs_rgba && rs_pal) {
		uint32_t* rgb = (uint32_t*)malloc((size_t)w * h * 4);
		uint32_t* tmp = (uint32_t*)malloc((size_t)w * h * 4);
		if (rgb && tmp) {
			int32_t k, n = (int32_t)w * h;
			int pass;
			for (k = 0; k < n; k++) rgb[k] = rs_pal[a[k]];
			for (pass = 2; pass <= RFB_SCALE; pass *= 2)
				blend_pass(rgb, w, h, tmp);
			s_hires[s_hires_n].rgb = rgb;
		} else {
			free(rgb);
		}
		free(tmp);
	}
	s_hires_n++;
}

static const uint8_t* find_hires(const void far* shape, int16_t* w)
{
	int k;
	for (k = 0; k < s_hires_n; k++)
		if (s_hires[k].shape == shape) { *w = s_hires[k].w; return s_hires[k].bits; }
	return NULL;
}

static const uint32_t* find_hires_rgb(const void far* shape, int16_t* w)
{
	int k;
	for (k = 0; k < s_hires_n; k++)
		if (s_hires[k].shape == shape) { *w = s_hires[k].w; return s_hires[k].rgb; }
	return NULL;
}

/* One cockpit pixel. rs_rgba is what present() puts on screen, but the index
 * buffer has to carry the cockpit as well, because a read-modify-write blit
 * reads it back: the gear knob's mask is ANDed into the gate that was blitted
 * a moment earlier, and an AND against a framebuffer that never received the
 * gate cleared the knob's surroundings to black. Writing both costs one store
 * and keeps the two views of the frame in agreement.
 *
 * Nothing downstream is disturbed by the extra store. present() rebuilds
 * frame_rgba from the index buffer at the top of each frame and only then
 * draws the cockpit over it, --paint-check refills the index buffer before
 * every frame, and the menus clear frame_rgba themselves and never read the
 * index buffer at all. */
static void put(int32_t off, uint8_t idx)
{
	rfb_pixels[off] = idx;
	if (rs_rgba) rs_rgba[off] = rs_pal[idx];
}

/* Exposed: the menus need it too. SDCSEL's showroom backdrop "stop" is one
 * of the transposed shapes, and drawing it raw gives the same diagonal
 * streaks "gbox" did. */
void far* unflip_shape(void far* arg_shape)
{
	struct SHAPE2D far* shape = (struct SHAPE2D far*)arg_shape;
	uint8_t* bits;
	int16_t w, h, i, j, flip;

	if (!shape) return NULL;
	if ((shape->s2d_unk6 & 0xF0) != 0) return arg_shape;
	flip = (int16_t)(shape->s2d_unk5 >> 4);
	if (flip == 0) return arg_shape;

	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	bits = (uint8_t*)arg_shape + sizeof(struct SHAPE2D);
	if ((int32_t)w * h > (int32_t)sizeof(s_unflip_buf)) return arg_shape;

	switch (flip) {
	case 1:
		for (j = 0; j < h; j++)
			for (i = 0; i < w; i++)
				s_unflip_buf[i + j * w] = bits[j + i * h];
		break;
	case 2:
		for (j = 0; j < h; j += 2)
			for (i = 0; i < w; i++)
				s_unflip_buf[i + j * w] = bits[(j / 2) + i * h];
		for (j = 0; j < h; j += 2)
			for (i = 0; i < w; i++)
				s_unflip_buf[w + i + j * w] =
					bits[((h + j + 1) / 2) + i * h];
		break;
	default:
		/* restunts calls fatal_error here too (loc_32BDE). Leaving the
		 * shape untouched shows it wrong rather than killing the game. */
		fprintf(stderr, "unflip_shape: okand vandning %d\n", flip);
		return arg_shape;
	}

	memcpy(bits, s_unflip_buf, (size_t)w * h);
	shape->s2d_unk5 = (uint8_t)(shape->s2d_unk5 & 0x0F);   /* done once */
	return arg_shape;
}

/* ------------------------------------------------------------------ */
/* A blit window, standing in for sprite_set_1_from_argptr.             */
/*                                                                      */
/* The original composes the gear gate into an offscreen sprite of      */
/* gbox's size and blits the finished window to the screen, so anything */
/* drawn into it is clipped to the gate. Rather than allocate a second  */
/* framebuffer, the two knob passes below draw straight to the screen   */
/* inside this rectangle, which clips identically. Coordinates are in   */
/* the original's 320x200 space.                                        */
/* ------------------------------------------------------------------ */
static int16_t s_win_l = 0, s_win_t = 0;
static int16_t s_win_r = RFB_VIEW_W, s_win_b = RFB_VIEW_H;

static void blit_window_set(int16_t x, int16_t y, int16_t w, int16_t h)
{
	s_win_l = (int16_t)(x * RFB_SCALE);
	s_win_t = (int16_t)(y * RFB_SCALE);
	s_win_r = (int16_t)((x + w) * RFB_SCALE);
	s_win_b = (int16_t)((y + h) * RFB_SCALE);
	if (s_win_l < 0) s_win_l = 0;
	if (s_win_t < 0) s_win_t = 0;
	if (s_win_r > RFB_VIEW_W) s_win_r = RFB_VIEW_W;
	if (s_win_b > RFB_VIEW_H) s_win_b = RFB_VIEW_H;
}

static void blit_window_reset(void)
{
	s_win_l = 0; s_win_t = 0; s_win_r = RFB_VIEW_W; s_win_b = RFB_VIEW_H;
}

/* ------------------------------------------------------------------ */
/* seg012 sprite_putimage_or_alt (12477) and sprite_putimage_and_alt2   */
/* (11246). Both take the destination position as arguments and both    */
/* begin the same way:                                                  */
/*                                                                      */
/*     mov ax, [bp+arg_6] / sub ax, [si+4]     ; x - s2d_unk1           */
/*     mov ax, [bp+arg_8] / sub ax, [si+6]     ; y - s2d_unk2           */
/*                                                                      */
/* so s2d_unk1/unk2 are an anchor and the caller's position names that  */
/* point in the shape, not the top-left corner. For the gear knob the   */
/* anchor is (12,11) in a 24x23 picture - its centre. Missing that      */
/* subtraction drew the knob 12 right and 11 down, hanging out below    */
/* the bottom of the gate.                                              */
/*                                                                      */
/* Note the two _alt entry points that do NOT subtract:                 */
/* sprite_putimage_and_alt (11762) and sprite_putimage_transparent      */
/* (12343) both store the argument unchanged, which is why the horizon  */
/* in rskybox.c and the explosions in rexplode.c must not do it either. */
/* ------------------------------------------------------------------ */
static void putimage_alt_common(void far* arg_shape, int16_t px, int16_t py,
                                int and_mode)
{
	const struct SHAPE2D far* shape = (const struct SHAPE2D far*)arg_shape;
	const uint8_t far* src;
	int16_t w, h, x, y, row, col;
	int16_t sx0, sy0, sx1, sy1;

	if (!shape) return;
	w = (int16_t)shape->s2d_width;
	h = (int16_t)shape->s2d_height;
	if (w <= 0 || h <= 0) return;
	/* the anchor subtraction, in the original's own units */
	px = (int16_t)(px - (int16_t)shape->s2d_unk1);
	py = (int16_t)(py - (int16_t)shape->s2d_unk2);
	x = (int16_t)(px * RFB_SCALE);
	y = (int16_t)(py * RFB_SCALE);
	src = (const uint8_t far*)arg_shape + sizeof(struct SHAPE2D);

	sx0 = x > s_win_l ? x : s_win_l;
	sy0 = y > s_win_t ? y : s_win_t;
	sx1 = (int16_t)(x + w * RFB_SCALE) < s_win_r
	      ? (int16_t)(x + w * RFB_SCALE) : s_win_r;
	sy1 = (int16_t)(y + h * RFB_SCALE) < s_win_b
	      ? (int16_t)(y + h * RFB_SCALE) : s_win_b;
	if (sx0 >= sx1 || sy0 >= sy1) return;

	for (row = sy0; row < sy1; row++) {
		const uint8_t far* srow = src + (int32_t)((row - y) / RFB_SCALE) * w;
		for (col = sx0; col < sx1; col++) {
			uint8_t c = srow[(col - x) / RFB_SCALE];
			int32_t o = (int32_t)row * RFB_VIEW_W + col;
			if (and_mode) put(o, (uint8_t)(rfb_pixels[o] & c));
			else if (c)   put(o, c);
		}
	}
}

void sprite_putimage_or_alt(void far* arg_shape, int16_t px, int16_t py)
{
	putimage_alt_common(arg_shape, px, py, 0);
}

/* The mask half of the pair: `lodsb / and es:[di], al`, no colour skipped.
 * gnab carries 0 where the knob is solid and 0xFF elsewhere, so the AND
 * punches the knob's silhouette out of the gate and the OR then fills it -
 * an ordinary masked sprite, done in two passes because the hardware had no
 * per-pixel test. */
void sprite_putimage_and_alt2(void far* arg_shape, int16_t px, int16_t py)
{
	putimage_alt_common(arg_shape, px, py, 1);
}

/* seg012 shape2d_op_unk. See the [ODDITY] note at the top of the file. */
void shape2d_op_unk(void far* arg_shape)
{
	shape2d_op_unk3(arg_shape);
}

/* seg005 loc_23456/loc_23485 draw each needle as a single preRender_line
 * from the dial centre to a tip position the car's own data supplies -
 * spdpoints/revpoints are lookup tables of screen points, so there is no
 * trigonometry anywhere in this. The line is widened to RFB_SCALE so the
 * needle stays visible when the cockpit is scaled up; at scale 1 this plots
 * exactly the pixels preRender_line would. */
static void needle(int16_t ox, int16_t oy, struct POINT2D centre,
                   const char* tip)
{
	int16_t x0 = (int16_t)((ox + centre.px) * RFB_SCALE);
	int16_t y0 = (int16_t)((oy + centre.py) * RFB_SCALE);
	int16_t x1 = (int16_t)((ox + (uint8_t)tip[0]) * RFB_SCALE);
	int16_t y1 = (int16_t)((oy + (uint8_t)tip[1]) * RFB_SCALE);
	int16_t dx = (int16_t)(x1 - x0), sx = dx >= 0 ? 1 : -1;
	int16_t dy = (int16_t)(y1 - y0), sy = dy >= 0 ? 1 : -1;
	int16_t adx = (int16_t)(dx >= 0 ? dx : -dx);
	int16_t ady = (int16_t)(dy >= 0 ? dy : -dy);
	int16_t err = (int16_t)((adx > ady ? adx : -ady) / 2), e2;
	uint8_t col = (uint8_t)meter_needle_color;

	for (;;) {
		int d;
		for (d = 0; d < RFB_SCALE; d++) {
			int16_t px = (int16_t)(x0 + (adx >= ady ? 0 : d));
			int16_t py = (int16_t)(y0 + (adx >= ady ? d : 0));
			if ((uint16_t)px < RFB_VIEW_W && (uint16_t)py < RFB_VIEW_H)
				put((int32_t)py * RFB_VIEW_W + px, col);
		}
		if (x0 == x1 && y0 == y1) break;
		e2 = err;
		if (e2 > -adx) { err = (int16_t)(err - ady); x0 = (int16_t)(x0 + sx); }
		if (e2 < ady)  { err = (int16_t)(err + adx); y0 = (int16_t)(y0 + sy); }
	}
}

/* ------------------------------------------------------------------ */
/* seg005 setup_car_shapes                                             */
/*   0 = load the cockpit archives and measure roof/dash                */
/*   1 = draw the static panel (roof, dash, gearbox surround)           */
/*   2 = draw the moving parts   [not ported yet]                       */
/*   3 = release the archives    [not ported yet]                       */
/* ------------------------------------------------------------------ */
void setup_car_shapes(int16_t arg_mode)
{
	switch (arg_mode) {
	case 0:
		break;
	case 1: {
		void far* roof;
		void far* dash;
		/* The original checks for "roof" with locate_shape_nofatal
		 * first: open-top cars have no roof strip, and then the
		 * windshield simply starts at row 0. */
		roof = locate_shape_nofatal((char far*)stdaresptr, "roof");
		if (roof) shape2d_op_unk(roof);
		dash = locate_shape_fatal((char far*)stdaresptr, "dash");
		shape2d_op_unk3(dash);
		shape2d_op_unk3(whlshapes[1]);   /* whl2, the centred wheel */
		/* gbox is deliberately NOT drawn here. It is the *source* art for
		 * the gear gate, which mode 2 composites with the knob inside an
		 * offscreen sprite window before blitting the result to screen.
		 * Blitting it straight to its screen position paints an opaque
		 * orange gate over the right-hand gauges - compare
		 * tests/dos_reference/dos_default_coun_cockpit_driving.png, where
		 * that corner shows the dash bitmap's own gauges and dark panel. */
		return;
	}
	case 2: {
		void far* ins2;
		/* seg005 loc_2319D: the wheel has three frames - hard left,
		 * centred, hard right - chosen by the steering angle divided by
		 * 8, with the division done in sign-magnitude so that it
		 * truncates towards zero on both sides. */
		int16_t a = state.playerstate.car_steeringAngle;
		int16_t mag = (int16_t)((a < 0 ? (int16_t)-a : a) >> 3);
		int16_t var_4 = a < 0 ? (int16_t)-mag : mag;
		int16_t var_2 = 1;
		if (var_4 < -10)     var_2 = 0;
		else if (var_4 > 10) var_2 = 2;
		shape2d_op_unk3(whlshapes[var_2]);

		/* The gauges. seg005 loc_2328A..loc_234BE.
		 *
		 * [DEVIATION - behaviour-exact] The original draws the dial face
		 * and its needles into an offscreen sprite window sized from
		 * "ins2", then blits the finished window to ins2's screen
		 * position. That exists to avoid a visible flicker on real VGA,
		 * where the screen is being scanned out while you draw. Painting
		 * straight to the framebuffer in the same order gives the same
		 * result here: the dial face is redrawn first, which is what
		 * erases the previous frame's needles, and the whole frame is
		 * presented at once. The offscreen window machinery
		 * (sprite_make_wnd and friends) is therefore not needed.
		 *
		 * spdcenter/revcenter and the needle-tip tables are in the
		 * window's own coordinates, so they are offset by ins2's screen
		 * position here. */
		ins2 = whlshapes[3];
		if (ins2 && simd_player.spdcenter.py != 0) {
			const struct SHAPE2D far* h = (const struct SHAPE2D far*)ins2;
			int16_t ox = (int16_t)h->s2d_pos_x;
			int16_t oy = (int16_t)h->s2d_pos_y;
			int16_t idx;

			shape2d_op_unk3(ins2);   /* dial face; erases the old needles */

			/* spdcenter.y == -1 marks a car with a digital speed
			 * readout instead of a sweep needle (loc_232B6). */
			if (simd_player.spdcenter.py != -1) {
				idx = (int16_t)((uint16_t)state.playerstate.car_speed / 0x280);
				if (idx >= simd_player.spdnumpoints)
					idx = (int16_t)(simd_player.spdnumpoints - 1);
				needle(ox, oy, simd_player.spdcenter,
				       &simd_player.spdpoints[idx * 2]);
			}

			idx = (int16_t)((uint16_t)state.playerstate.car_currpm >> 7);
			if (idx >= simd_player.revnumpoints)
				idx = (int16_t)(simd_player.revnumpoints - 1);
			needle(ox, oy, simd_player.revcenter,
			       &simd_player.revpoints[idx * 2]);
		}

		/* The gear gate and its knob. seg005 loc_230DE draws "gbox" into a
		 * window of its own size, composites the knob at the car's
		 * knob_x/knob_y - an H pattern; the Countach reads (22,42),
		 * (34,12), (34,42) for gears 1, 2 and 3 - and blits the result to
		 * gbox's screen position. Drawn straight to the framebuffer here
		 * for the same reason as the gauges above. The knob goes on
		 * transparently so its bounding box does not square off the gate.
		 *
		 * This only became readable once unflip_shape was added: "gbox"
		 * is one of the transposed shapes, and before that it rendered as
		 * diagonal streaks.
		 *
		 * It is drawn only while the car is actually changing gear. The
		 * gate sits on top of the right-hand gauges, and the original
		 * saves those pixels into an offscreen window before drawing it
		 * and blits them back when the shift ends (loc_23057, and the
		 * sprite_clear_shape_alt at loc_234BE) - so the gate is a
		 * transient overlay, not part of the resting dashboard. That is
		 * what tests/dos_reference/dos_default_coun_cockpit_driving.png
		 * shows: gauges visible, no gate, captured between shifts. Since
		 * mode 1 repaints the panel every frame here, restoring the
		 * background needs no saved copy - simply not drawing the gate
		 * has the same effect. */
		if (whlshapes[4] &&
		    (state.playerstate.car_changing_gear != 0 ||
		     state.playerstate.car_fpsmul2 != 0)) {
			const struct SHAPE2D far* g =
				(const struct SHAPE2D far*)whlshapes[4];
			int16_t kx = (int16_t)(g->s2d_pos_x
				+ state.playerstate.car_knob_x);
			int16_t ky = (int16_t)(g->s2d_pos_y
				+ state.playerstate.car_knob_y);
			shape2d_op_unk3(whlshapes[4]);
			/* Everything from here is clipped to the gate, because the
			 * original is composing inside a gbox-sized window. */
			blit_window_set((int16_t)g->s2d_pos_x, (int16_t)g->s2d_pos_y,
			                (int16_t)g->s2d_width, (int16_t)g->s2d_height);
			/* mask first, then art - the order at loc_23120 */
			if (gnobshapes[1])
				sprite_putimage_and_alt2(gnobshapes[1], kx, ky);
			if (gnobshapes[0])
				sprite_putimage_or_alt(gnobshapes[0], kx, ky);
			blit_window_reset();
		}
		return;
	}
	default:
		return;
	}

	/* mode 0 */
	{
		char id[5];
		char name[16];
		void far* roof;
		void far* dash;

		/* The original patches the four id characters straight into the
		 * literals "stdaxxxx" and "stdbxxxx". */
		memcpy(id, gameconfig.game_playercarid, 4);
		id[4] = 0;
		snprintf(name, sizeof(name), "stda%s.pvs", id);
		stdaresptr = file_load_resource(3, name);
		snprintf(name, sizeof(name), "stdb%s.pvs", id);
		stdbresptr = file_load_resource(2, name);
		if (!stdaresptr) {
			roofbmpheight = 0;
			dashbmp_y = RFB_VIEW_H;
			return;
		}

		locate_many_resources((char far*)stdaresptr,
			"whl1whl2whl3ins2gboxins1ins3inm1inm3",
			(char far**)whlshapes);
		if (stdbresptr)
			locate_many_resources((char far*)stdbresptr,
				"gnobgnabdot dotadot1dot2",
				(char far**)gnobshapes);
		{
			int k;
			for (k = 0; k < 9; k++) unflip_shape(whlshapes[k]);
			for (k = 0; k < 6; k++) unflip_shape(gnobshapes[k]);
			unflip_shape(locate_shape_nofatal(
				(char far*)stdaresptr, "dash"));
			unflip_shape(locate_shape_nofatal(
				(char far*)stdaresptr, "roof"));
			/* Smoothing must come after unflipping, or it would
			 * smooth a transposed image. */
			{
			static const char* wn[9] = {"whl1","whl2","whl3","ins2",
				"gbox","ins1","ins3","inm1","inm3"};
			static const char* gn[6] = {"gnob","gnab","dot","dota",
				"dot1","dot2"};
			for (k = 0; k < 9; k++) make_hires(whlshapes[k], wn[k]);
			for (k = 0; k < 6; k++) make_hires(gnobshapes[k], gn[k]);
			make_hires(locate_shape_nofatal((char far*)stdaresptr, "dash"), "dash");
			make_hires(locate_shape_nofatal((char far*)stdaresptr, "roof"), "roof");
			}
		}

		roof = locate_shape_nofatal((char far*)stdaresptr, "roof");
		roofbmpheight = roof
			? (int16_t)((const struct SHAPE2D far*)roof)->s2d_height : 0;

		dash = locate_shape_nofatal((char far*)stdaresptr, "dash");
		dashbmp_y = dash
			? (int16_t)((const struct SHAPE2D far*)dash)->s2d_pos_y
			: RFB_VIEW_H;
	}
}
