/*
 * ringame_text.c - the messages the game writes over the road.
 *
 * Ported from
 *   seg003.asm  draw_ingame_text (428)
 *   seg008.asm  font_op2_alt (17), locate_text_res (33)
 *
 * Every message is a text resource in GAME.PRE, looked up through the same
 * language prefix the dialogs use - `textresprefix` is 'e', so "pre" resolves
 * to "epre". GAME.PRE carries twenty resources and these are among them:
 *
 *   edm1 edm2   "Professional Driver" / "on Closed Circuit"
 *   epre        "Fasten Your Seatbelt"
 *   ewww        "Wrong Way"
 *   eopp        "Opponent Near"
 *   ese1 ese2   the security-system warning
 *   epen        the penalty message
 *   erpl        the replay banner
 *
 * The layout is data too. Each block loads its string, then jumps to one
 * shared tail (loc_1D1D5) that centres it with font_op2_alt and draws it with
 * intro_draw_text, so the only thing a block chooses is the string and the y:
 *
 *   dm1 170   dm2 182     the attract-mode disclaimer, full-height view
 *   pre  90               before the flag drops
 *   se1  93   se2 105     the security warning
 *   www  93               wrong way
 *   opp 116               opponent near
 *
 * font_op2_alt is exactly `(0x140 - font_op2(str)) / 2` - centring, nothing
 * more.
 *
 * [DEVIATION - partial port] Everything except the penalty counter, which
 * formats a number into the string rather than printing a fixed one. Marked
 * below. The turn arrows arrived with rexplode.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "externs.h"
#include "memmgr.h"
#include "rfbsize.h"

extern int16_t dialog_fnt_colour;
extern char resID_byte1[];
extern void far* gameresptr;
extern uint16_t font_op2(const char* str);
extern void font_set_fontdef(void);
extern void far* sdgame2shapes[5];
extern void sprite_putimage_transparent(void far* shape, int16_t x, int16_t y);
extern struct RECTANGLE* intro_draw_text(char* str, int16_t x, int16_t y,
                                         int16_t colour_text, int16_t colour_shadow);
extern struct RECTANGLE cliprect_unk;

struct RECTANGLE rect_ingame_text;

/* seg008 locate_text_res: the resource name is the language prefix followed
 * by the three-character key, exactly as MAIN.RES's dialog templates are. */
char far* locate_text_res(char far* res, char* key)
{
	char name[8];
	if (!res) return NULL;
	name[0] = (char)textresprefix;
	name[1] = key[0]; name[2] = key[1]; name[3] = key[2];
	name[4] = 0;
	return locate_shape_nofatal(res, name);
}

/* seg008 font_op2_alt: the x that centres `str` on a 320-wide screen. */
static int16_t font_op2_alt(const char* str)
{
	int16_t w = (int16_t)font_op2(str);
	return (int16_t)((0x140 - w) / 2);
}

/* seg003 loc_1D1D5: the shared tail every message block jumps to - centre it,
 * draw it, and fold the result into the dirty rectangle. */
static void say(const char* key, int16_t y)
{
	char* text = (char*)locate_text_res((char far*)gameresptr, (char*)key);
	if (!text || !*text) return;
	/* The clock block in frame.c leaves the LED font selected, and skips
	 * font_set_fontdef() entirely before the flag drops - so select the
	 * default face here rather than depending on what ran last. */
	font_set_fontdef();
	snprintf(resID_byte1, 32, "%s", text);
	rect_union(intro_draw_text(resID_byte1, font_op2_alt(resID_byte1), y,
	                           dialog_fnt_colour, 0),
	           &rect_ingame_text, &rect_ingame_text);
}

struct RECTANGLE* draw_ingame_text(void)
{
	rect_ingame_text = cliprect_unk;

	if (idle_expired != 0) {
		/* attract mode: the disclaimer, over a full-height view */
		say("dm1", 0xAA);
		say("dm2", 0xB6);
		return &rect_ingame_text;
	}

	if (game_replay_mode != 0) {
		/* loc_1D4B0: the replay banner. Its own block formats a frame
		 * count as well; only the label is drawn here. */
		if (game_replay_mode == 2) say("rpl", 0x0F);
		return &rect_ingame_text;
	}

	if (state.game_inputmode == 0) {
		say("pre", 0x5A);                  /* "Fasten Your Seatbelt" */
		return &rect_ingame_text;
	}

	if (passed_security == 0) {
		say("se1", 0x5D);
		say("se2", 0x69);
		return &rect_ingame_text;
	}

	/* loc_1D2BE: the rest is only for the player's own cockpit view */
	if (followOpponentFlag != 0 || cameramode != 0 ||
	    state.playerstate.car_crashBmpFlag != 0)
		return &rect_ingame_text;

	/* loc_1D2DC: field_45D is the turn hint - 1 and 2 are the left and
	 * right arrows from SDGAME2.PVS, 3 is "Wrong Way". */
	if (state.field_45D == 1)
		sprite_putimage_transparent(sdgame2shapes[3], 0x94, 0x5D);
	else if (state.field_45D == 2)
		sprite_putimage_transparent(sdgame2shapes[4], 0x94, 0x5D);
	else if (state.field_45D == 3)
		say("www", 0x5D);

	/* loc_1D31E: field_45E flags the opponent being close. */
	if (state.field_45E == 1 || state.field_45E == 2)
		say("opp", 0x74);

	/* loc_1D422: show_penalty_counter drives the "pen" message, which
	 * formats a count into the string - NOT PORTED. */

	return &rect_ingame_text;
}
