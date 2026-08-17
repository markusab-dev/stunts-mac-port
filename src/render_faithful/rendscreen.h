#ifndef RESTUNTS_RENDSCREEN_H
#define RESTUNTS_RENDSCREEN_H

#include <stdint.h>

/*
 * rendscreen.h - the post-race results screen, seg000 end_hiscore
 * (5126..7089).  See rendscreen.c for provenance and deviations.
 *
 * The locals keep their disassembly names so the port can be read against
 * the listing.
 */
struct ENDSCREEN {
	void far* var_4E;            /* MISC.PRE                             */
	void far* var_68;            /* OPP<n>.PRE, NULL when racing a clock */
	void far* var_1C;            /* OPP<n>WIN.PVS or OPP<n>LOSE.PVS      */
	const uint8_t far* var_5A;   /* its `winn` / `lose` frame script     */

	int16_t var_18;              /* 0 you won, 1 the opponent won, 2 no
	                              * verdict (the default)                */
	int16_t var_16;              /* show the opponent's page at all      */
	int16_t var_6E;              /* 0 nothing, 1 enter a high score,
	                              * 2 show the table, 0xFF unavailable   */
	int16_t var_14;              /* 1 = the table is on the top panel    */
	int16_t var_88;              /* the time offered to the table        */
	int16_t var_9C;              /* button x offset: 0 or -36            */
	int16_t var_8C, var_90;      /* the portrait's top-left corner       */
	int16_t var_8E, var_6C;      /* script index, last drawn index       */
	int16_t var_42;              /* the 30-tick portrait frame timer     */
	int16_t var_6A;              /* 'v' (0x76) or 'd' (0x64)             */
	int16_t var_7A;              /* how many taunt lines                 */
	int16_t var_selectedmenu;    /* 0..3                                 */

	int16_t nbuttons;            /* 4, or 3 when button 0 is suppressed  */
	int16_t x1[4], x2[4];        /* var_64 / var_9A - the hit boxes      */
	int16_t y1, y2;              /* hiscore_buttons_y1 / _y2             */

	int16_t song;                /* music_song_t for the caller to start */
	int16_t hiscore_row;         /* the row a new record landed on, or -1*/
};

/* seg000 13190..13A1D - load the resources, draw both panels and every
 * statistic, pick the verdict, roll the taunt, decide whether a high score
 * is on offer.  `trackpath` is the <track>.TRK the "has this track been
 * edited" check re-reads (loc_138FF..1397F). */
void endscreen_open(struct ENDSCREEN* es, const char* trackpath);

/* seg000 14425..14479 - release everything endscreen_open took. */
void endscreen_close(struct ENDSCREEN* es);

/* One complete picture: the two panels, the statistics, whichever page the
 * top panel is showing, the buttons and the blinking selection outline.
 * `blink` selects word_407CE (5) over word_407D0 (14), see
 * mouse_timer_sprite_unk (seg008 4201). */
void endscreen_draw(struct ENDSCREEN* es, int blink);

/* seg000 13D83 / 1421B - advance the portrait's frame script by `ticks`
 * 20 Hz ticks.  Returns 1 when the displayed frame changed. */
int endscreen_advance(struct ENDSCREEN* es, int16_t ticks);

/* seg000 13D13 - the eval page under a single "Continue" (ebct) button,
 * shown while the portrait animates before the name entry.  Only reached
 * when there is an opponent AND var_6E == 1. */
void endscreen_draw_continue(struct ENDSCREEN* es);
int  endscreen_advance_continue(struct ENDSCREEN* es, int16_t ticks);

/* seg008 mouse_multi_hittest against the same rectangles, honouring the
 * three-button form.  Returns 0..3, or -1 for a miss. */
int16_t endscreen_hittest(const struct ENDSCREEN* es, int16_t x, int16_t y);

/* seg000 1447A / 144A4 - the left and right arrow keys. */
void endscreen_left(struct ENDSCREEN* es);
void endscreen_right(struct ENDSCREEN* es);

/* seg000 143C6 - Enter or Space on the current selection.  Returns -1 when
 * the press only toggled the top panel (button 0), otherwise the value
 * end_hiscore returns: 0 view replay, 1 race again, 2 main menu. */
int16_t endscreen_activate(struct ENDSCREEN* es);

#endif /* RESTUNTS_RENDSCREEN_H */
