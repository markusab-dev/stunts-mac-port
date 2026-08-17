/*
 * rhighscore.c - the fastest-times table: loading, formatting, drawing and
 * writing the per-track .HIG file.  Phase 3.
 *
 * Ported from reference/restunts/src/restunts/asm/seg000.asm:
 *
 *   highscore_write_a       2214..2333   (119 lines)   load, or create fresh
 *   highscore_text_unk      2334..2571   (237 lines)   draw the whole table
 *   print_highscore_entry   2572..2722   (150 lines)   one row's four strings
 *   enter_hiscore           2723..2907   (185 lines)   insert a new time
 *   highscore_write_b       2908..2974   ( 66 lines)   write in sorted order
 *
 * plus seg008.asm hiscore_draw_text (3264..3345), which is the outlined
 * heading style the table's captions use.
 *
 * ------------------------------------------------------------------------
 * WHAT THE DATA TURNED OUT TO BE
 *
 * A .HIG is exactly 364 bytes: seven 52-byte records and nothing else.  The
 * record layout is not guessed - it falls straight out of the stack frame
 * highscore_write_a builds its blank template in (var_38 at -56, var_27 at
 * -39, var_F at -15, var_E at -14, var_6 at -6, i.e. 52 bytes ending at -4)
 * and out of print_highscore_entry's copy of it (var_4A .. var_18):
 *
 *     +0   char[17]  driver's name
 *     +17  char[24]  car name          (the car's own `gnam`)
 *     +41  byte      1 = parenthesise the opponent field
 *     +42  char[8]   opponent          ("<initials>/<car short name>")
 *     +50  uint16    time in frames at 20 Hz, 0xFFFF = empty
 *
 * Checked against the shipped DEFAULT.HIG, which decodes cleanly:
 *     "JTK"  "Porsche/March INDY"  flag 0  " "        1443 frames
 *     "katsauto" ...                                  1449
 *     "JTK"  ...                    flag 0  "SV/INDY" 1497
 *
 * The blank template the original writes is the one oddity worth recording:
 * it strcpy's TWENTY dots into a SEVENTEEN-byte name field, so the name and
 * the car name run together into one 40-dot string until a real entry
 * overwrites them.  That is what the instructions say and it is reproduced
 * (see hig_blank below).  [ODDITY - faithful]
 *
 * The captions are data too, in MAIN.RES under the 'e' language prefix:
 *     ehs1 "FASTEST TIMES for"   the heading, + " '<track>'"
 *     ehs2 "NAME"   ehs3 "CAR"   ehs5 "OPP."   ehs4 "TIME"
 * drawn at x = 16, 120, 224, 272 - which is the column layout, from dseg
 * rather than from taste.  Rows are at y = 25 + 10*i.
 *
 * ------------------------------------------------------------------------
 * [DEVIATION] The original draws into a window sprite and its captions go
 * through hiscore_draw_text, which four-way outlines the text by drawing it
 * at (x+-1, y+-1) in colour 0 and then at (x, y) in dialog_fnt_colour.  That
 * outlining IS reproduced.  What is not is the sprite plumbing
 * (sprite_copy_wnd_to_1 / sprite_blit_to_video) and the DOS mouse driver:
 * the port draws into the presented frame like the other menus do.  There is
 * no oracle for any of this, so it is behaviour-exact and said to be.
 *
 * [DEVIATION] file_build_path() joins with a backslash and takes the game
 * directory from a dseg offset.  Here the path is <data dir>/<TRACK>.HIG.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "externs.h"
#include "rfbsize.h"

extern char far* td11_highscores;
extern struct GAMEINFO gameconfig;
extern uint16_t framespersec;
extern int16_t dialog_fnt_colour;
extern void far* mainresptr;
extern void far* fontnptr;
extern void far* fontdefptr;
extern char far* locate_text_res(char far* res, char* key);
extern void font_set_fontdef2(void far* data);
extern void font_set_fontdef(void);
extern void font_set_colour(uint16_t colour, uint16_t unused);
extern void font_draw_text(const char* str, int16_t x, int16_t y);
extern uint16_t font_op2(const char* str);
extern void format_frame_as_string(char* buf, int16_t frames, int16_t centiseconds);
extern const char* rfileio_get_data_dir(void);
extern int16_t fontdef_unk_0E;

#define HIG_RECSZ   52
#define HIG_COUNT    7
#define HIG_BYTES   (HIG_RECSZ * HIG_COUNT)   /* 0x16C = 364 */

/* record field offsets, see the header comment */
#define HIG_NAME     0
#define HIG_CAR     17
#define HIG_PAREN   41
#define HIG_OPP     42
#define HIG_TIME    50

/* seg000 word_46170: the display order.  highscore_write_a resets it to the
 * identity; enter_hiscore rewrites it so that the new record - which always
 * physically occupies slot 6 - appears at its ranked position. */
static int16_t s_order[HIG_COUNT];
/* seg000 byte_449CE: which displayed row is the one just entered, 0xFF for
 * none.  highscore_text_unk draws that row in dialogarg2 (= 4). */
static uint8_t s_highlight = 0xFF;

/* dseg 0x407FC..: dialogarg2 dw 4 */
static const uint16_t s_dialogarg2 = 4;

static char* hig_rec(int16_t i)
{
	return (char*)td11_highscores + (int)i * HIG_RECSZ;
}

static uint16_t hig_time(int16_t i)
{
	const unsigned char* p = (const unsigned char*)hig_rec(i) + HIG_TIME;
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void hig_path(char* dst, size_t n)
{
	char track[10];
	int k;
	for (k = 0; k < 8 && gameconfig.game_trackname[k] &&
	            gameconfig.game_trackname[k] != ' '; k++)
		track[k] = gameconfig.game_trackname[k];
	track[k] = 0;
	snprintf(dst, n, "%s/%s.HIG", rfileio_get_data_dir(), track);
}

/* seg000:11 60A - the blank record, transcribed including its overrun. */
static void hig_blank(char* rec)
{
	memset(rec, 0, HIG_RECSZ);
	strcpy(rec + 0,  "....................");   /* 20 dots into 17 bytes */
	strcpy(rec + 17, ".......................");/* 23 dots               */
	rec[41] = 0;
	strcpy(rec + 42, "../....");
	rec[50] = (char)0xFF; rec[51] = (char)0xFF;
}

/*
 * seg000 highscore_write_a.  arg 0 = load the track's .HIG into
 * td11_highscores; arg 1 = build a blank table and write it.
 * Returns 1 on failure, 0 on success - the original's own polarity
 * (loc_115F9 returns 1, loc_11602 returns 0).
 */
int16_t highscore_write_a(int16_t create)
{
	char path[700];
	int16_t i;
	FILE* f;

	s_highlight = 0xFF;
	for (i = 0; i < HIG_COUNT; i++) s_order[i] = i;

	hig_path(path, sizeof path);

	if (create == 0) {
		f = fopen(path, "rb");
		if (!f) return 1;
		if (fread(td11_highscores, 1, HIG_BYTES, f) != HIG_BYTES) {
			fclose(f);
			return 1;
		}
		fclose(f);
		return 0;
	}

	for (i = 0; i < HIG_COUNT; i++) hig_blank(hig_rec(i));

	f = fopen(path, "wb");
	if (!f) return 1;
	i = (fwrite(td11_highscores, 1, HIG_BYTES, f) == HIG_BYTES) ? 1 : 0;
	fclose(f);
	return i ? 0 : 1;
}

/*
 * seg000 highscore_write_b: write the seven records out in s_order, so the
 * file is always sorted even though the new entry lives in slot 6.
 */
int16_t highscore_write_b(void)
{
	char path[700];
	char buf[HIG_BYTES];
	int16_t i;
	FILE* f;

	for (i = 0; i < HIG_COUNT; i++)
		memcpy(buf + (int)i * HIG_RECSZ, hig_rec(s_order[i]), HIG_RECSZ);

	hig_path(path, sizeof path);
	f = fopen(path, "wb");
	if (!f) return 0;
	i = (fwrite(buf, 1, HIG_BYTES, f) == HIG_BYTES) ? 1 : 0;
	fclose(f);
	/* the file now IS in display order, so reset the permutation */
	memcpy(td11_highscores, buf, HIG_BYTES);
	for (i = 0; i < HIG_COUNT; i++) s_order[i] = i;
	return 1;
}

/*
 * seg000 print_highscore_entry: lay row `row` out as four NUL-separated
 * strings inside one buffer, and report each one's offset.  The original
 * packs them into its 0xAC74 scratch area; `buf` is that area here.
 */
void print_highscore_entry(int16_t row, uint8_t off[4], char* buf, size_t bufsz)
{
	char rec[HIG_RECSZ];
	uint8_t at;
	int16_t savefps;
	char timestr[16];

	memcpy(rec, hig_rec(s_order[row]), HIG_RECSZ);
	(void)bufsz;

	off[0] = 0;
	strcpy(buf, rec + HIG_NAME);
	at = (uint8_t)(strlen(buf) + 1);
	off[1] = at;

	strcpy(buf + at, rec + HIG_CAR);
	at = (uint8_t)(at + strlen(buf + at) + 1);
	off[2] = at;

	buf[at] = 0;
	if (rec[HIG_PAREN] == 1) strcat(buf + at, "(");
	strcat(buf + at, rec + HIG_OPP);
	if (rec[HIG_PAREN] == 1) strcat(buf + at, ")");
	at = (uint8_t)(at + strlen(buf + at) + 1);

	/* the stored time is always in 20 Hz frames, whatever the game is
	 * running at, so the formatter is forced to 20 for this call */
	savefps = (int16_t)framespersec;
	framespersec = 0x14;
	format_frame_as_string(timestr, (int16_t)(hig_time(row) == 0xFFFF
	                                          ? 0 : (int16_t)hig_time(row)), 1);
	off[3] = at;
	strcpy(buf + at, timestr);
	framespersec = (uint16_t)savefps;
}

/* seg008 hiscore_draw_text: `str` outlined at the four diagonal neighbours in
 * `shadow`, then drawn at (x, y) in `colour`. */
void hiscore_draw_text(const char* str, int16_t x, int16_t y,
                       int16_t colour, int16_t shadow)
{
	font_set_colour((uint16_t)shadow, 0);
	font_draw_text(str, (int16_t)(x + 1), (int16_t)(y + 1));
	font_draw_text(str, (int16_t)(x + 1), (int16_t)(y - 1));
	font_draw_text(str, (int16_t)(x - 1), (int16_t)(y + 1));
	font_draw_text(str, (int16_t)(x - 1), (int16_t)(y - 1));
	font_set_colour((uint16_t)colour, 0);
	font_draw_text(str, x, y);
}

/* seg008 font_op2_alt - the x that centres `str` on the 320-wide screen. */
static int16_t hs_centre(const char* str)
{
	return (int16_t)((0x140 - (int16_t)font_op2(str)) / 2);
}

static const char* hs_text(const char* key)
{
	char far* p = mainresptr ? locate_text_res((char far*)mainresptr, (char*)key)
	                         : NULL;
	return p ? (const char*)p : "";
}

/*
 * seg000 highscore_text_unk: the whole table.  Captions first, then seven
 * rows of four columns.
 */
void highscore_text_unk(void)
{
	char line[128];
	char buf[256];
	uint8_t off[4];
	int16_t i;
	char track[10];

	for (i = 0; i < 8 && gameconfig.game_trackname[i] &&
	            gameconfig.game_trackname[i] != ' '; i++)
		track[i] = gameconfig.game_trackname[i];
	track[i] = 0;

	font_set_fontdef2(fontdefptr);

	snprintf(line, sizeof line, "%s '%s'", hs_text("hs1"), track);
	hiscore_draw_text(line, hs_centre(line), 5, dialog_fnt_colour, 0);

	hiscore_draw_text(hs_text("hs2"), 0x10,  0x0F, dialog_fnt_colour, 0);
	hiscore_draw_text(hs_text("hs3"), 0x78,  0x0F, dialog_fnt_colour, 0);
	hiscore_draw_text(hs_text("hs5"), 0xE0,  0x0F, dialog_fnt_colour, 0);
	hiscore_draw_text(hs_text("hs4"), 0x110, 0x0F, dialog_fnt_colour, 0);

	/* the rows use fontn, the narrow face, so four columns fit */
	font_set_fontdef2(fontnptr ? fontnptr : fontdefptr);

	for (i = 0; i < HIG_COUNT; i++) {
		int16_t y = (int16_t)(i * 10 + 0x19);
		uint16_t colour = (i == (int16_t)s_highlight) ? s_dialogarg2 : 0;
		print_highscore_entry(i, off, buf, sizeof buf);
		font_set_colour(colour, 0);
		font_draw_text(buf + off[0], 0x10,  y);
		font_draw_text(buf + off[1], 0x78,  y);
		font_draw_text(buf + off[2], 0xE0,  y);
		font_draw_text(buf + off[3], 0x110, y);
	}
	font_set_fontdef();
}

/*
 * seg000 enter_hiscore, first half: does `time` make the table, and if so
 * where?  Returns the row it lands on, or -1.  The original's test is
 * `td11_highscores[0x16A] > time` - offset 0x16A is record 6's time field,
 * i.e. the slowest entry.
 */
static int16_t s_norm_time;   /* the arg_0 doubling happens exactly once */

int16_t highscore_would_enter(int16_t time)
{
	if (framespersec == 0x0A) time = (int16_t)(time * 2);
	s_norm_time = time;
	if ((uint16_t)hig_time(6) <= (uint16_t)time) return -1;
	{
		int16_t i = 0;
		while (i < HIG_COUNT && (uint16_t)hig_time(i) <= (uint16_t)time) {
			s_order[i] = i;
			i++;
		}
		return i;
	}
}

/*
 * seg000 enter_hiscore, second half: build the new record in physical slot 6
 * and point the order table at it.  `name` is filled in afterwards by the
 * caller (the original reads it with call_read_line, which is a DOS text
 * widget); highscore_set_name + highscore_write_b finish the job.
 *
 * The time used is the one highscore_would_enter() normalised, so the
 * `framespersec == 10` doubling can never be applied twice.
 */
void highscore_insert(int16_t row, const char* carname,
                      int16_t parenflag, const char* opponent)
{
	char* rec;
	int16_t i;
	int16_t time = s_norm_time;

	for (i = row; i < HIG_COUNT - 1; i++) s_order[i + 1] = i;
	s_order[row] = 6;
	s_highlight = (uint8_t)row;

	rec = hig_rec(6);
	memset(rec, 0, HIG_RECSZ);
	rec[HIG_NAME] = 0;
	snprintf(rec + HIG_CAR, 24, "%s", carname ? carname : "");
	rec[HIG_PAREN] = (char)parenflag;
	snprintf(rec + HIG_OPP, 8, "%s", opponent ? opponent : " ");
	rec[HIG_TIME]     = (char)(time & 0xFF);
	rec[HIG_TIME + 1] = (char)((time >> 8) & 0xFF);
}

void highscore_set_name(const char* name)
{
	char* rec = hig_rec(6);
	snprintf(rec + HIG_NAME, 17, "%s", name ? name : "");
}

int16_t highscore_highlight(void) { return (int16_t)(int8_t)s_highlight; }
