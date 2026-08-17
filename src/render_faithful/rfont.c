/*
 * rfont.c - bitmap text: the on-screen clock and the routines around it.
 *
 * Replaces the font stubs in rstubs.c. Ported from
 *   seg012.asm  font_draw_text (89), font_op2 (32), set_fontdefseg (15)
 *   seg008.asm  intro_draw_text (49), format_frame_as_string (97),
 *               font_set_fontdef (8), font_set_fontdef2 (14)
 *
 * [DEVIATION - deliberate, and the only place in the port where one is taken]
 * This is behaviour-exact, not instruction-exact. The original keeps its
 * drawing state *inside the font file's own header bytes* (it points DS at the
 * font resource and scribbles the pen position over offsets 0..5), which the
 * two disassemblies label inconsistently. Reproducing that byte-for-byte buys
 * nothing here: unlike the simulation, HUD text has no oracle to match - it is
 * either legible in the right place or it is not. What must be exact, and is,
 * is the *data format*, decoded from FONTLED.FNT and verified glyph by glyph:
 *
 *   +0x0E  word   line height          (8 in FONTLED.FNT)
 *   +0x10  word   advance width        (6)
 *   +0x14  byte   0 = fixed width, else per-glyph width byte precedes the rows
 *   +0x16  word[] glyph offsets, indexed by character; 0 = no such glyph
 *   glyph: `height` rows of ceil(width/8) bytes, 1 bit per pixel, MSB leftmost
 *
 * Rendering matches the original's: set bits are painted in the current
 * colour, clear bits leave the framebuffer alone (the text is transparent, not
 * boxed), and intro_draw_text draws the string twice - once offset by (1,1) in
 * the shadow colour, then at (x,y) in the text colour.
 */
#include <stdint.h>
#include <string.h>

#include "externs.h"
#include "rfbsize.h"
#include "shape2d.h"
#include "math.h"

extern uint8_t rfb_pixels[];
extern uint32_t* rs_rgba;      /* rshape2d.c: truecolour cockpit target */
extern const uint32_t* rs_pal;
extern void far* fontdefptr;
extern uint16_t framespersec;

/* Set by font_set_fontdef2; frame.c reads it to place the clock. */
int16_t fontdef_unk_0E;

static const uint8_t* s_font;      /* current font resource            */
static uint8_t s_colour;           /* colour set by font_set_colour    */
static struct RECTANGLE s_textrect;/* dirty rect returned to the caller*/

static uint16_t fld(const uint8_t* f, int off)
{
	return (uint16_t)(f[off] | ((uint16_t)f[off + 1] << 8));
}

/* seg012 set_fontdefseg + seg008 font_set_fontdef2 */
void font_set_fontdef2(void far* data)
{
	s_font = (const uint8_t*)data;
	fontdef_unk_0E = s_font ? (int16_t)fld(s_font, 0x0E) : 0;
}

void font_set_fontdef(void)
{
	font_set_fontdef2(fontdefptr);
}

void font_set_colour(uint16_t colour, uint16_t unused)
{
	(void)unused;
	s_colour = (uint8_t)colour;
}

/* seg012 font_op2: pixel width of a string in the current font. */
uint16_t font_op2(const char* str)
{
	const uint8_t* f = s_font;
	uint16_t total = 0, deflt;
	uint8_t varwidth;
	if (!f || !str) return 0;
	deflt = fld(f, 0x10);
	varwidth = f[0x14];
	for (; *str; str++) {
		uint16_t g = fld(f, 0x16 + (uint8_t)*str * 2);
		if (g == 0) continue;               /* character not in this font */
		total = (uint16_t)(total + (varwidth ? f[g] : deflt));
	}
	return total;
}

/* seg012 font_draw_text: blit each glyph at the pen, advancing per character.
 * Clipped to the framebuffer; the original relies on the caller's clip rect. */
void font_draw_text(const char* str, int16_t x, int16_t y)
{
	const uint8_t* f = s_font;
	uint16_t deflt, height;
	uint8_t varwidth;
	if (!f || !str) return;
	deflt = fld(f, 0x10);
	height = fld(f, 0x0E);
	varwidth = f[0x14];

	for (; *str; str++) {
		uint16_t g = fld(f, 0x16 + (uint8_t)*str * 2);
		uint16_t w = deflt;
		const uint8_t* rows;
		if (g == 0) continue;
		if (varwidth) { w = f[g]; rows = f + g + 1; }
		else          { rows = f + g; }
		{
			/* Glyphs are authored for the 320x200 screen, so at
			 * RFB_SCALE > 1 each set bit becomes a block, exactly as
			 * the cockpit bitmaps do in rshape2d.c. And like them the
			 * text follows whichever target is active: the menus and
			 * dialogs composite in truecolour after the 3D view has
			 * been converted, the in-race clock into the indexed
			 * framebuffer. */
			uint16_t bytes_per_row = (uint16_t)((w + 7) >> 3);
			for (uint16_t ry = 0; ry < height; ry++) {
				for (uint16_t b = 0; b < bytes_per_row; b++) {
					uint8_t bits = rows[ry * bytes_per_row + b];
					for (int bit = 0; bit < 8; bit++) {
						int16_t px0, py0, dx, dy;
						if (!(bits & (0x80 >> bit))) continue;
						px0 = (int16_t)((x + b * 8 + bit) * RFB_SCALE);
						py0 = (int16_t)((y + ry) * RFB_SCALE);
						for (dy = 0; dy < RFB_SCALE; dy++) {
							int16_t py = (int16_t)(py0 + dy);
							if (py < 0 || py >= RFB_VIEW_H) continue;
							for (dx = 0; dx < RFB_SCALE; dx++) {
								int16_t px = (int16_t)(px0 + dx);
								int32_t o;
								if (px < 0 || px >= RFB_VIEW_W) continue;
								o = (int32_t)py * RFB_VIEW_W + px;
								if (rs_rgba) rs_rgba[o] = rs_pal[s_colour];
								else         rfb_pixels[o] = s_colour;
							}
						}
					}
				}
			}
		}
		x = (int16_t)(x + w);
	}
}

/* seg008 intro_draw_text: shadow pass at (x+1, y+1), then the text itself. */
struct RECTANGLE* intro_draw_text(char* str, int16_t x, int16_t y,
                                  int16_t colour_text, int16_t colour_shadow)
{
	s_textrect.left = x;
	s_textrect.top = y;
	s_textrect.bottom = (int16_t)(y + fontdef_unk_0E + 1);
	s_textrect.right = (int16_t)(x + font_op2(str) + 1);

	font_set_colour((uint16_t)colour_shadow, 0);
	font_draw_text(str, (int16_t)(x + 1), (int16_t)(y + 1));
	font_set_colour((uint16_t)colour_text, 0);
	font_draw_text(str, x, y);
	return &s_textrect;
}

/* seg008 string_fmt_int: value right-aligned into `width`, `pad` selects the
 * fill (0 = spaces are suppressed entirely, i.e. no leading blanks; non-zero
 * = leading zeroes). This is what makes the clock read 1:05 rather than 1:5. */
static void string_fmt_int(char* buf, int16_t value, int16_t pad, int16_t width)
{
	char tmp[8];
	int n = 0, i;
	if (value < 0) value = 0;
	do { tmp[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < 7);
	i = 0;
	if (pad) for (; n + i < width; i++) buf[i] = '0';
	while (n > 0) buf[i++] = tmp[--n];
	buf[i] = '\0';
}

/*
 * seg008 print_int_as_string_maybe (4097..4200), the OTHER integer
 * formatter - not the same routine as string_fmt_int above.
 *
 *     itoa(value, dst, 10)
 *     if (width) while (strlen(dst) != width) {
 *         width < len -> drop the first character
 *         width > len -> shift right one and put a space in front
 *     }
 *     if (pad) replace every leading space with '0'
 *
 * The results screen calls it as (buf, v, 0, 3): right-aligned in exactly
 * three characters, blank-filled.  That the string is then always exactly
 * three characters long matters - end_hiscore reuses the same stack buffer
 * as a three-character resource id and relies on the NUL landing at [3].
 * [ODDITY - faithful, see rendscreen.c]
 */
void print_int_as_string_maybe(char* dst, int16_t value,
                               int16_t pad, int16_t width)
{
	int len, i;
	snprintf(dst, 12, "%d", (int)value);
	if (width != 0) {
		len = (int)strlen(dst);
		while (width != len) {
			if (width < len) {
				for (i = 0; i < len; i++) dst[i] = dst[i + 1];
				len--;
			} else {
				for (i = len; i >= 0; i--) dst[i + 1] = dst[i];
				dst[0] = ' ';
				len++;
			}
		}
	}
	if (pad != 0)
		for (i = 0; dst[i] == ' '; i++) dst[i] = '0';
}

/* seg008 format_frame_as_string: frame count -> "M:SS" (+ ".cc" if asked). */
void format_frame_as_string(char* buf, int16_t frames, int16_t centiseconds)
{
	uint16_t fps = framespersec ? framespersec : 20;
	uint16_t per_minute = (uint16_t)(60 * fps);
	uint16_t rest = (uint16_t)frames;
	uint16_t minutes = (uint16_t)(rest / per_minute);
	uint16_t seconds;
	char part[8];

	rest = (uint16_t)(rest - minutes * per_minute);
	seconds = (uint16_t)(rest / fps);
	rest = (uint16_t)(rest - seconds * fps);

	string_fmt_int(part, (int16_t)minutes, 0, 2);
	strcpy(buf, part);
	strcat(buf, ":");
	string_fmt_int(part, (int16_t)seconds, 1, 2);
	strcat(buf, part);

	if (centiseconds != 0) {
		strcat(buf, ".");
		string_fmt_int(part, (int16_t)((100 / fps) * rest), 1, 2);
		strcat(buf, part);
	}
}
