/*
 * rdialog.c - seg008 show_dialog (asm lines 334..1206, 872 lines), ported.
 *
 * ========================================================================
 * WHY THIS IS A PORT AND NOT AN EIGHTH BESPOKE SDL DIALOG
 * ========================================================================
 *
 * Phase 11 exists to decide that.  The eight call sites in seg009's editor
 * were read first (asm lines 1282, 1892, 2039, 2081, 2157, 2281, 2330,
 * 2374); then the text resources they pass, out of TEDIT.PRE with
 * tools/dump_textres.c.  The decision follows straight from the data:
 *
 *   emss = "Select Horizon]}[Desert][Tropical][Alpine][City][Country][Cancel]"
 *   emen = "Select Terrain]}[Terrain 1][Terrain 2][Terrain 3][Terrain 4]"
 *          "[Terrain 5][Cancel]"
 *   echl = "Track has been changed]}[Save First[Load New Track]"
 *   echx = "Track has been changed]}[Save First[Exit Editor]"
 *   eeok = "Track ok.]"                (and fourteen more error texts)
 *
 * Every one of those is a complete dialog specification.  The parser below
 * is what turns them into pixels, and three of its behaviours could not be
 * guessed from the call sites:
 *
 *  1. `][` starts a new line, so emss's six options stack vertically - but
 *     `[` on its own does NOT, because the inner loop leaves var_82 (the
 *     pen) where it was.  That is why echl's "[Save First[Load New Track]"
 *     puts two buttons SIDE BY SIDE on one line while emss's put one per
 *     line, from the same code, with no flag anywhere.  A hand-written
 *     dialog would have got this backwards.
 *  2. A two-button dialog gets keyboard shortcuts for free: the first
 *     non-space letter of each label, lowercased through g_ascii_props,
 *     becomes an accelerator (loc_27BF5..loc_27C68).  "S" loads "Save
 *     First", "L" loads "Load New Track" - straight out of the text.
 *  3. Pass 1 measures the box by treating every `]` as a line break,
 *     INCLUDING the ones that close buttons, and it never sees `[` at all.
 *     So "[Desert" is measured as a line of its own, leading bracket and
 *     all, and the box comes out one line taller than the drawn content
 *     and a bracket-width wider.  That is the original's own arithmetic
 *     and it is what makes the dialogs the size they are.  [ODDITY]
 *
 * Reimplementing that as an SDL widget would have meant reimplementing the
 * mini-language anyway - and getting (1), (2) and (3) wrong.  So: ported,
 * instruction by instruction, into this new file so nothing else collides.
 *
 * ========================================================================
 * THE ARGUMENTS
 * ========================================================================
 * The call sites push nine words; in frame order they are
 *
 *   arg_0  mode     0 = draw and return 0
 *                   1 = wait for any key; ESC returns 0, anything else 1
 *                   2 = pick a button; returns its index, 0xFF on ESC
 *                   3 = return the '@' field count / 2
 *                   4 = draw, sub_2EB1E(8) delay, return 1
 *   arg_2  save the background first (sub_274B0) and put it back after
 *          (sub_275C6); 0xFFFF is returned when the save fails
 *   arg_4  the text, far
 *   arg_8  left, or 0xFFFF to centre on the 320-wide screen
 *   arg_A  top,  or 0xFFFF to centre on the 200-high screen
 *   arg_C  the one-pixel frame's colour
 *   arg_E  a word array, one per button: 0 = greyed out and unreachable
 *   arg_10 the initially selected button
 *
 * The editor uses mode 1 for its fifteen track_setup verdicts and mode 2
 * for the five choices; nothing in seg009 uses 0, 3 or 4, but all five are
 * here because the same routine serves the whole game.
 *
 * ========================================================================
 * [DEVIATION] - four, each stated plainly, because a dialog has no oracle.
 * ========================================================================
 *
 *  1. No offscreen sprite and no background save.  The original saves the
 *     box's rectangle with sub_274B0, draws into sprite1, and puts the
 *     saved pixels back with sub_275C6 on the way out.  This port draws
 *     the whole screen every frame, exactly as rendscreen.c and rintro.c
 *     do, so arg_2 is recorded and the two calls are no-ops.  Same pixels;
 *     the caller repaints what was underneath.
 *
 *  2. No modal loop.  loc_27C6D..loc_27ED3 is a `while (var_84)` around
 *     input_checking / mouse_multi_hittest, and this port has neither the
 *     DOS mouse nor a PIT.  The body of that loop is rdialog_key() and
 *     rdialog_mouse(), called once per SDL event; the state that lived in
 *     locals lives in struct RDIALOG.  Every branch of the switch is here,
 *     including the wrap-around and the "skip disabled buttons" retry.
 *
 *  3. The selected button is inverse video, painted here.  font_set_unk
 *     writes TWO colours into the font header - fontdefseg:0 foreground and
 *     fontdefseg:2 background - and sub_345BC's glyph blit stores `ah` (the
 *     background) for every clear bit, so the selected button really is
 *     black-on-white.  rfont.c's font_set_colour drops the second argument
 *     and font_draw_text skips clear bits entirely, which would draw the
 *     selected button in colour 0 on colour 0 - invisible.  Rather than
 *     change rfont.c under another agent's feet, rdialog.c lays the
 *     background bar down itself with sprite_1_unk before each label.  The
 *     bar is font_op2(label) x fontdef_unk_0E at the pen, which is the
 *     rectangle the glyph blit would have covered.
 *
 *  4. sprite_1_unk4's known asymmetry (rwidgets.c) is inherited: the frame's
 *     verticals stop one pixel short of the bottom corners.  That is the
 *     original's, not this port's.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "externs.h"
#include "rdialog.h"
#include "rfbsize.h"
#include "rwidgets.h"

extern int16_t dialog_fnt_colour;      /* rdata.c, = 15                  */
extern int16_t performGraphColor;      /* rdata.c, = 1                   */
extern int16_t fontdef_unk_0E;         /* rfont.c: the font's line height */
extern uint16_t font_op2(const char* str);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern void font_set_colour(uint16_t colour, uint16_t background);

/* dseg 21105: `dialogarg2 dw 4` - the frame colour the two picker dialogs
 * pass.  rhighscore.c keeps its own copy of the same word for the same
 * reason; nothing in this port defines it globally. */
const uint16_t rdialog_dialogarg2 = 4;

/* dseg 14426: `word_3EB90 dw 0`.  show_dialog assigns it 0 at loc_27835
 * before it is ever read, so it is the background colour of an unselected
 * label - black. */
static int16_t word_3EB90;

/* seg008 27BF5: g_ascii_props bit 0 marks a letter; the routine adds 0x20
 * to fold it to lower case.  dseg 15377 starts the table at 32, and only
 * bit 0 of the upper-case range matters here, so the test is written out
 * rather than the 256-byte table transcribed.  [DEVIATION - equivalent] */
static int16_t fold_case(int16_t ax)
{
	if (ax >= 'A' && ax <= 'Z') return (int16_t)(ax + 0x20);
	return ax;
}

/* ------------------------------------------------------------------ */
/* Pass 1 - loc_276C0..loc_2776C.  Measure the box.                     */
/* ------------------------------------------------------------------ */
static void rdialog_measure(struct RDIALOG* d)
{
	char var_80[RDLG_LINEBUF];
	int16_t var_82 = 0;                    /* the pen into var_80        */
	int16_t var_1C2;                       /* one line's width           */
	const char* p = d->arg_4;
	uint8_t var_1D8;

	d->var_1D6 = (int16_t)(fontdef_unk_0E + 2);
	d->var_1C6 = 0;
	d->var_194 = 0x20;

	for (;;) {                             /* loc_2775A                  */
		var_1D8 = (uint8_t)*p;
		if (var_1D8 == 0) break;
		if (var_1D8 == ']') {                          /* loc_276C0      */
			var_80[var_82] = 0;
			var_1C2 = (int16_t)font_op2(var_80);
			if (var_1C2 > d->var_194) d->var_194 = var_1C2;
			var_82 = 0;
			d->var_1C6 = (int16_t)(d->var_1C6 + d->var_1D6);
		} else if (*p == '}') {                        /* loc_27702      */
			var_80[var_82] = 0;
			var_1C2 = (int16_t)font_op2(var_80);
			if (var_1C2 > d->var_194) d->var_194 = var_1C2;
			var_82 = 0;
			d->var_1C6 = (int16_t)(d->var_1C6 + 4);
		} else {                                       /* loc_27744      */
			if (var_82 < RDLG_LINEBUF - 1) var_80[var_82++] = *p;
		}
		p++;                                           /* loc_27756      */
	}

	/* loc_2776C: round the width up to the next multiple of eight, plus
	 * 0x18 of margin.  `and al, 0F8h` touches only the low byte, which is
	 * the original's own truncation and is reproduced. */
	{
		int16_t ax = (int16_t)(d->var_194 + 0x18);
		ax = (int16_t)((ax & ~0xFF) | (ax & 0xF8));
		d->var_194 = ax;
	}
	if ((uint16_t)d->arg_8 == 0xFFFF) {
		int16_t ax = (int16_t)(0x140 - d->var_194);
		ax = (int16_t)(ax - (ax < 0 ? -1 : 0));        /* cwd / sub ax,dx */
		ax = (int16_t)(ax >> 1);
		ax = (int16_t)((ax & ~0xFF) | (ax & 0xF8));
		d->arg_8 = ax;
	}
	if ((uint16_t)d->arg_A == 0xFFFF) {                /* loc_27790      */
		int16_t ax = (int16_t)(0xC8 - d->var_1C6);
		ax = (int16_t)(ax - (ax < 0 ? -1 : 0));
		d->arg_A = (int16_t)(ax >> 1);
	}

	d->var_30 = d->arg_8;                              /* loc_277A5      */
	d->var_2E = (int16_t)(d->arg_8 + d->var_194);
	d->var_2C = (int16_t)(d->arg_A - 8);
	d->var_2A = (int16_t)(d->arg_A + d->var_1C6 + 8);
	d->arg_8 = (int16_t)(d->arg_8 + 8);
	d->var_194 = (int16_t)(d->var_194 - 0x10);
}

/* ------------------------------------------------------------------ */
/* Pass 2 - loc_27890..loc_27B56.  Draw the body, lay the buttons out.  */
/*                                                                      */
/* Called twice: once from rdialog_open with draw = 0 to fill in the     */
/* geometry, once per repaint from rdialog_draw with draw = 1.  The two  */
/* walks are the same code, which is why the geometry cannot drift.      */
/* ------------------------------------------------------------------ */
static void rdialog_body(struct RDIALOG* dd, int draw)
{
	struct RDIALOG* d = dd;
	char var_80[RDLG_LINEBUF];
	int16_t var_82 = 0;
	int16_t var_1C2;
	int16_t var_1CA;
	const char* p = d->arg_4;
	uint8_t var_1D8;

	d->var_9E = 0;
	d->var_1C6 = 1;
	d->var_140 = 0;

	/* --- the body, up to the first '[' --- */
	for (;;) {                                         /* loc_2798E      */
		var_1D8 = (uint8_t)*p;
		if (var_1D8 == 0 || var_1D8 == '[') break;     /* loc_27890      */
		if (var_1D8 == ']') {                          /* loc_2789A      */
			var_80[var_82] = 0;
			if (draw) font_draw_text(var_80, d->arg_8,
			                         (int16_t)(d->arg_A + d->var_1C6));
			var_82 = 0;
			d->var_1C6 = (int16_t)(d->var_1C6 + d->var_1D6);
		} else if (var_1D8 == '}') {                   /* loc_278E2      */
			var_80[var_82] = 0;
			if (draw) font_draw_text(var_80, d->arg_8,
			                         (int16_t)(d->arg_A + d->var_1C6));
			var_82 = 0;
			d->var_1C6 = (int16_t)(d->var_1C6 + 4);
		} else if (var_1D8 == '@') {                   /* loc_27918      */
			if (d->arg_0 == 3) {
				var_80[var_82] = 0;
				if (d->var_9E + 1 < RDLG_MAXFIELDS) {
					d->fields[d->var_9E] =
						(int16_t)(font_op2(var_80) + d->arg_8);
					d->fields[d->var_9E + 1] =
						(int16_t)(d->arg_A + d->var_1C6);
				}
				d->var_9E = (int16_t)(d->var_9E + 2);
			}
			/* loc_2796A: the marker itself prints as a space. */
			if (var_82 < RDLG_LINEBUF - 1) var_80[var_82++] = ' ';
		} else {                                       /* loc_27978      */
			if (var_82 < RDLG_LINEBUF - 1) var_80[var_82++] = (char)var_1D8;
		}
		p++;                                           /* loc_2798A      */
	}

	/* --- the buttons - loc_279A0..loc_27B08 --- */
	while (*p == '[' && d->var_140 < RDLG_MAXBUTTONS) {   /* loc_27B08   */
		int16_t si = d->var_140;                       /* loc_279A8      */
		int16_t ytop;
		p++;
		d->var_13E[si] = p;
		var_80[var_82] = 0;
		d->var_28[si] = (int16_t)(font_op2(var_80) + d->arg_8);
		ytop = (int16_t)(d->arg_A + d->var_1C6);
		d->var_1BE[si] = ytop;
		d->var_EE[si] = (int16_t)(d->var_1D6 + ytop);
		var_1CA = 0;
		if (var_82 < RDLG_LINEBUF - 1) var_80[var_82++] = ' ';
		var_1C2 = 0;

		for (;;) {                                     /* loc_27AAE      */
			var_1D8 = (uint8_t)*p;
			if (var_1D8 == 0 || var_1D8 == '[') break; /* loc_27A2A      */
			if (var_1D8 == ']') {                      /* loc_27A34      */
				var_80[var_82] = 0;
				var_1C2 = (int16_t)font_op2(var_80);
				var_82 = 0;
				d->var_1C6 = (int16_t)(d->var_1C6 + d->var_1D6);
			} else if (var_1D8 == '}') {               /* loc_27A64      */
				var_80[var_82] = 0;
				var_1C2 = (int16_t)font_op2(var_80);
				var_82 = 0;
				d->var_1C6 = (int16_t)(d->var_1C6 + 3);
			} else {                                   /* loc_27A94      */
				if (var_82 < RDLG_LINEBUF - 1)
					var_80[var_82++] = (char)var_1D8;
				var_1CA++;
			}
			p++;                                       /* loc_27AAA      */
		}

		d->var_98[si] = (uint8_t)var_1CA;              /* loc_27AC0      */
		var_80[var_82] = 0;
		if (var_1C2 == 0) var_1C2 = (int16_t)font_op2(var_80);
		d->var_C6[si] = (int16_t)(d->var_28[si] + var_1C2);
		d->var_140++;
	}

	/*
	 * loc_27B15: three or more buttons whose x1 all came out the same -
	 * the vertical stack - get their right edge pushed out to the box's
	 * full width so the whole row is clickable.  The test compares
	 * var_28[0]==var_28[1]==var_28[2], which is what a stack produces and
	 * a side-by-side pair does not.
	 */
	if (d->var_140 > 2 &&
	    d->var_28[1] == d->var_28[0] && d->var_28[2] == d->var_28[1]) {
		int16_t var_196;
		for (var_196 = 0; var_196 < d->var_140; var_196++)
			d->var_C6[var_196] =
				(int16_t)(d->var_28[var_196] + d->var_194);
	}
}

/* ------------------------------------------------------------------ */
int rdialog_open(struct RDIALOG* d, int16_t mode, int16_t save_bg,
                 const char* text, int16_t x, int16_t y, int16_t colour,
                 const int16_t* enable, uint8_t initial)
{
	memset(d, 0, sizeof *d);
	d->arg_0 = mode;
	d->arg_2 = save_bg;
	d->arg_4 = text ? text : "";
	d->arg_8 = x;
	d->arg_A = y;
	d->arg_C = colour;
	d->arg_E = enable;
	d->arg_10 = initial;

	word_3EB90 = 0;                                    /* loc_27835      */
	rdialog_measure(d);
	rdialog_body(d, 0);

	/* loc_27B56: var_1D4 = 1, then mode 2 overwrites it with arg_10. */
	d->var_1D4 = 1;
	d->var_84 = 0;
	d->result = 0;

	if (d->arg_0 == 2) {                               /* loc_27BD4      */
		d->var_1D4 = (int16_t)(int8_t)d->arg_10;
		d->var_84 = 1;
		d->var_1C8 = -1;
		d->var_1CC = -1;
		if (d->var_140 == 2) {
			/* loc_27BF5 / loc_27C32: the first non-space character of
			 * each label, folded to lower case, is its accelerator. */
			const char* s = d->var_13E[0];
			int16_t k = 0;
			while (s[k] == ' ') k++;
			d->var_1C8 = fold_case((int16_t)(uint8_t)s[k]);
			s = d->var_13E[1];
			k = 0;
			while (s[k] == ' ') k++;
			d->var_1CC = fold_case((int16_t)(uint8_t)s[k]);
		}
	} else if (d->arg_0 == 1) {                        /* loc_27B98      */
		d->var_84 = 1;
	}
	return 1;
}

int16_t rdialog_immediate_result(const struct RDIALOG* d)
{
	if (d->arg_0 == 0) return 0;                       /* loc_27BBC      */
	if (d->arg_0 == 3) return (int16_t)(d->var_9E / 2);/* loc_27BC4      */
	return (int16_t)(int8_t)d->var_1D4;                /* loc_27B92      */
}

/* ------------------------------------------------------------------ */
/* loc_277F6..loc_27835 (the box) and loc_27C92 (the labels).           */
/* ------------------------------------------------------------------ */
void rdialog_draw(const struct RDIALOG* d)
{
	struct RDIALOG tmp = *d;
	int16_t var_196;

	/* sprite_set_1_size(var_30, var_2E, var_2C, var_2A) then
	 * sprite_clear_1_color(0) - the box's own black ground.  Drawn here
	 * as the fill it is, because this port has no second sprite. */
	sprite_1_unk(d->var_30, d->var_2C,
	             (int16_t)(d->var_2E - d->var_30),
	             (int16_t)(d->var_2A - d->var_2C), 0);

	/* loc_27814: the one-pixel frame, four pixels outside the text. */
	sprite_1_unk4((int16_t)(d->arg_8 - 4), (int16_t)(d->arg_A - 4),
	              (int16_t)(d->arg_8 + d->var_194 + 4),
	              (int16_t)(d->arg_A + d->var_1C6 + 4), d->arg_C);

	font_set_colour((uint16_t)dialog_fnt_colour, (uint16_t)word_3EB90);
	rdialog_body(&tmp, 1);

	/* loc_27C92: every button, the selected one inverse. */
	for (var_196 = 0; var_196 < d->var_140; var_196++) {
		char var_192[RDLG_LINEBUF];
		int16_t fg, bg, n = (int16_t)d->var_98[var_196];
		int16_t k;

		if (n > RDLG_LINEBUF - 1) n = RDLG_LINEBUF - 1;
		for (k = 0; k < n; k++) var_192[k] = d->var_13E[var_196][k];
		var_192[n] = 0;

		if (d->arg_E && d->arg_E[var_196] == 0) {      /* loc_27D4A      */
			fg = performGraphColor;
			bg = word_3EB90;
		} else if ((int16_t)(int8_t)d->var_1D4 == var_196) {
			fg = word_3EB90;                           /* loc_27C9E      */
			bg = dialog_fnt_colour;
		} else {
			fg = dialog_fnt_colour;                    /* loc_27CA8      */
			bg = word_3EB90;
		}

		/* [DEVIATION 3] the background bar the original's glyph blit
		 * lays down for every clear bit. */
		if (bg != 0)
			sprite_1_unk(d->var_28[var_196], d->var_1BE[var_196],
			             (int16_t)font_op2(var_192), fontdef_unk_0E,
			             (int16_t)bg);

		font_set_colour((uint16_t)fg, (uint16_t)bg);
		font_draw_text(var_192, d->var_28[var_196], d->var_1BE[var_196]);
	}
	font_set_colour((uint16_t)dialog_fnt_colour, 0);
}

/* ------------------------------------------------------------------ */
int16_t rdialog_hittest(const struct RDIALOG* d, int16_t mx, int16_t my)
{
	int16_t i;
	for (i = 0; i < d->var_140; i++)
		if (d->var_28[i] <= mx && d->var_C6[i] >= mx &&
		    d->var_1BE[i] <= my && d->var_EE[i] >= my) {
			if (d->arg_E && d->arg_E[i] == 0) return -1;  /* loc_27DA4  */
			return i;
		}
	return -1;
}

int rdialog_mouse(struct RDIALOG* d, int16_t mx, int16_t my, int click)
{
	int16_t hit;
	if (!d->var_84) return 0;
	hit = rdialog_hittest(d, mx, my);
	if (hit >= 0) d->var_1D4 = hit;                    /* loc_27DB8      */
	if (click && hit >= 0) return rdialog_key(d, 0x0D);
	return 1;
}

/* ------------------------------------------------------------------ */
/* loc_27DBC..loc_27ED3 - one turn of the modal loop's input handling.  */
/* ------------------------------------------------------------------ */
int rdialog_key(struct RDIALOG* d, int16_t key)
{
	if (!d->var_84) return 0;

	if (d->arg_0 == 1) {                               /* loc_27B98      */
		if (key == 0) return 1;
		if (key == 0x1B) d->var_1D4 = 0;
		d->var_84 = 0;
		d->result = (int16_t)(int8_t)d->var_1D4;
		return 0;
	}

	/* loc_27DBC: a two-button dialog's accelerators. */
	if (d->var_140 == 2 && key != 0) {
		int16_t var_1D2 = fold_case(key);
		if (var_1D2 == d->var_1C8)      { d->var_1D4 = 0; key = 0x0D; }
		else if (var_1D2 == d->var_1CC) { d->var_1D4 = 1; key = 0x0D; }
	}

	switch ((uint16_t)key) {
	case 0x20:                                         /* loc_27EA9      */
	case 0x0D:
		d->var_84 = 0;
		d->result = (int16_t)(int8_t)d->var_1D4;
		return 0;
	case 0x1B:                                         /* loc_27EA4      */
		d->var_1D4 = 0xFF;
		d->var_84 = 0;
		d->result = -1;                                /* cbw of 0xFF    */
		return 0;
	case 0x4800:                                       /* up             */
	case 0x4B00:                                       /* left           */
		/* loc_27E34: step back, wrapping, until an enabled button. */
		{
			int16_t guard = (int16_t)(d->var_140 + 1);
			do {
				if (d->var_1D4 != 0) d->var_1D4--;
				else d->var_1D4 = (int16_t)(d->var_140 - 1);
				if (!d->arg_E) break;
			} while (d->arg_E[d->var_1D4] == 0 && --guard > 0);
		}
		return 1;
	case 0x4D00:                                       /* right          */
	case 0x5000:                                       /* down           */
		{                                              /* loc_27E6A      */
			int16_t guard = (int16_t)(d->var_140 + 1);
			do {
				if ((int16_t)(d->var_1D4 + 1) < d->var_140) d->var_1D4++;
				else d->var_1D4 = 0;
				if (!d->arg_E) break;
			} while (d->arg_E[d->var_1D4] == 0 && --guard > 0);
		}
		return 1;
	default:
		return 1;
	}
}
