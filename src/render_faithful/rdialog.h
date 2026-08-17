#ifndef RESTUNTS_RDIALOG_H
#define RESTUNTS_RDIALOG_H

#include <stdint.h>

/*
 * rdialog.h - seg008 show_dialog (334..1206), the game's one modal dialog.
 *
 * See rdialog.c for provenance and deviations.  The short version: the
 * dialog's whole layout - its wording, its line breaks, how many buttons it
 * has, whether they stack or sit side by side, and what each one returns -
 * comes out of a text resource written in a four-character mini-language,
 * not out of any code.  So this is a parser, not a widget.
 *
 *     ]   end this line, advance one font line
 *     }   end this line, advance 4 pixels
 *     [   start a button; it runs to the next '[' or ']'
 *     @   a text-entry field marker (mode 3 only)
 *
 * The modal loop itself is split the way rendscreen.c splits end_hiscore:
 * open / draw / hittest / key, so an SDL host drives it.
 */

#define RDLG_MAXBUTTONS 12
#define RDLG_MAXFIELDS  16
#define RDLG_LINEBUF    128

struct RDIALOG {
	/* --- the arguments, under show_dialog's own names --- */
	int16_t arg_0;                  /* mode: 0 draw only, 1 any key,
	                                 * 2 pick a button, 3 field count,
	                                 * 4 draw + short delay              */
	int16_t arg_2;                  /* save/restore the background       */
	const char* arg_4;              /* the text resource                 */
	int16_t arg_8, arg_A;           /* top-left, or 0xFFFF to centre     */
	int16_t arg_C;                  /* frame colour                      */
	const int16_t* arg_E;           /* per-button enable words, or NULL  */
	uint8_t arg_10;                 /* initially selected button         */

	/* --- pass 1: the box --- */
	int16_t var_1D6;                /* font line height + 2              */
	int16_t var_194;                /* text width, then box width - 0x10 */
	int16_t var_1C6;                /* running y inside the box          */
	int16_t var_30, var_2E;         /* box left / right                  */
	int16_t var_2C, var_2A;         /* box top / bottom                  */

	/* --- pass 2: the buttons --- */
	int16_t var_140;                /* how many                          */
	const char* var_13E[RDLG_MAXBUTTONS];  /* first character of each    */
	uint8_t var_98[RDLG_MAXBUTTONS];       /* its length                 */
	int16_t var_28[RDLG_MAXBUTTONS];       /* x1                         */
	int16_t var_C6[RDLG_MAXBUTTONS];       /* x2                         */
	int16_t var_1BE[RDLG_MAXBUTTONS];      /* y1                         */
	int16_t var_EE[RDLG_MAXBUTTONS];       /* y2                         */

	/* --- pass 2: the '@' fields, mode 3 --- */
	int16_t var_9E;                 /* twice the field count             */
	int16_t fields[RDLG_MAXFIELDS];  /* x, y, x, y, ...                  */

	/* --- the loop's state --- */
	int16_t var_1D4;                /* the selection, 0xFF = cancelled   */
	int16_t var_1C8, var_1CC;       /* the two keyboard shortcuts        */
	int16_t var_84;                 /* 1 while the loop is running       */
	int16_t result;                 /* what show_dialog returns          */
};

/* seg008 334..27B56: both measuring passes and the button layout.  Draws
 * nothing.  Returns 0 when the dialog cannot be laid out (the original's
 * sub_274B0 out-of-memory arm, which returns 0xFFFF). */
int rdialog_open(struct RDIALOG* d, int16_t mode, int16_t save_bg,
                 const char* text, int16_t x, int16_t y, int16_t colour,
                 const int16_t* enable, uint8_t initial);

/* One complete picture: the cleared box, its one-pixel frame, the body text
 * and every button with the selected one in inverse video. */
void rdialog_draw(const struct RDIALOG* d);

/* seg008 mouse_multi_hittest over the button rectangles.  Returns the
 * button under (mx, my) honouring the enable array, or -1. */
int16_t rdialog_hittest(const struct RDIALOG* d, int16_t mx, int16_t my);

/* seg008 27D6D..27ED3 - one pass of the modal loop's key handling.
 * `key` is the game's own code: an ASCII byte, or a scancode<<8 for the
 * arrows (0x4800 up, 0x4B00 left, 0x4D00 right, 0x5000 down).  Returns 1
 * when the dialog is still open, 0 when it has closed - `d->result` then
 * holds what show_dialog would have returned. */
int rdialog_key(struct RDIALOG* d, int16_t key);

/* The mouse's half of the same loop: move the selection onto a button and,
 * with `click` set, accept it. */
int rdialog_mouse(struct RDIALOG* d, int16_t mx, int16_t my, int click);

/* The value show_dialog returns for a dialog closed without any input -
 * mode 0 returns 0, mode 3 returns the field count. */
int16_t rdialog_immediate_result(const struct RDIALOG* d);

#endif /* RESTUNTS_RDIALOG_H */
