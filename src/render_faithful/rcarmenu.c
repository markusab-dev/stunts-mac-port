/*
 * rcarmenu.c - the two 3D pieces of the car picker: the car on its turntable
 * and the acceleration graph.
 *
 * Ported from seg000.asm run_car_menu (2975..4275). Not the whole routine -
 * this port's car picker already works and is kept - only the two parts that
 * are genuinely missing, which is what the plan asked for.
 *
 * THE TURNTABLE (seg000 3020..3040 and 12405..1248F)
 * --------------------------------------------------
 * Three constants and one accumulating angle. restunts2's dseg has them
 * symbolically, which saved decoding the raw bytes:
 *
 *     carmenu_carpos    VECTOR <0x0000, 0xFCB8, 0x0B40>   = (0, -840, 2880)
 *     carmenu_cliprect  RECTANGLE <0x0000, 0x0140, 0x0000, 0x005F>
 *     performGraphColor dw 0x0001
 *
 * The shape is `game3dshapes.shape3d_numverts+0AA8h`. SHAPE3D is 22 bytes in
 * the DOS build, and 0xAA8 / 22 = 124 exactly - which is the same slot
 * shape3d_load_car_shapes fills with the player car's "car0", so the picker
 * spins the very shape you will drive.
 *
 * Each frame: rotation += delta, then the camera's pitch is derived from the
 * car's own position - polarAngle(pos.y, pos.z), the height against the
 * distance - and handed to select_cliprect_rotate. There is no camera state
 * at all; the viewpoint follows from where the car is placed.
 *
 * ts_unk is 0x7530 (30000) rather than the 0x400 the world uses. That is the
 * detail-distance threshold: at 30000 the car is always drawn at full detail,
 * which is what a showroom wants.
 *
 * THE ACCELERATION GRAPH (seg000 12294..12344)
 * -------------------------------------------
 * Not a table and not measured data: the game *simulates* the car. It sets
 * framespersec to 20, resets the car, forces first gear, and then calls the
 * ordinary update_car_speed up to 800 times, plotting one pixel per step
 * until the curve leaves the top of the box. So the graph is exactly as
 * truthful as the physics is - which here is byte-identical to DOS on five
 * tracks.
 *
 *     x = (0x26 * step) / 0x320 + 0x1C          38 pixels wide
 *     y = 0xB5 - ((speed >> 8) << 6) / 0x96     181 at rest, up to 117
 *
 * and it stops the moment y goes below 0x75, so a fast car simply reaches the
 * top sooner. The 32-bit divide is __aFuldiv with the dividend pushed last.
 *
 * [DEVIATION] The original calls init_game_state(-2) to reset the car before
 * running the simulation. This port has no init_game_state, so the graph
 * saves the player state, clears it, runs, and puts it back. The simulation
 * itself is the real one; only the reset is stood in for.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "externs.h"
#include "math.h"
#include "shape3d.h"
#include "rfbsize.h"

extern struct SHAPE3D game3dshapes[];
extern struct SIMD simd_player;
extern void update_car_speed(char, int16_t, struct CARSTATE*, struct SIMD*);

/* dseg, via restunts2's symbolic listing - see the header comment. */
static const struct VECTOR carmenu_carpos = { 0, -840, 2880 };
static struct RECTANGLE carmenu_cliprect  = { 0, 0x140, 0, 0x5F };
/* performGraphColor (dw 1) already lives in rdata.c with its dseg address. */

/* seg000:11F63 set_projection(0x24, 0x11, 0x140, 0x64) - the showroom's own
 * field of view, narrower than the cockpit's. */
void carmenu_set_projection(void)
{
	set_projection(0x24, 0x11, RFB_VIEW_W, (int16_t)(0x64 * RFB_SCALE));
}

/*
 * One frame of the turntable. `rotation` is the accumulated angle in the
 * game's 0..0x400 turn; `material` is the paint job, which the original
 * wraps against the shape's own paint count:
 *
 *     mov al, byte ptr game3dshapes.shape3d_numpaints+0AA8h
 *     cmp [bx], al / jl ... / mov byte ptr [bx], 0
 */
void carmenu_draw_turntable(int16_t rotation, uint8_t* material)
{
	struct TRANSFORMEDSHAPE3D ts;
	int16_t var_carpospolarangle;

	memset(&ts, 0, sizeof ts);
	ts.pos = carmenu_carpos;                       /* movsw x3 */
	ts.shapeptr = &game3dshapes[124];              /* +0AA8h / 22 */
	ts.rotvec.x = 0;
	ts.rotvec.y = 0;
	ts.unk = 0x7530;
	ts.ts_flags = 0;      /* slow_video_mgmt is 0 here, so no rect pointer */

	/* loc_12423 */
	var_carpospolarangle = polarAngle(carmenu_carpos.y, carmenu_carpos.z);
	select_cliprect_rotate(0, var_carpospolarangle, 0, &carmenu_cliprect, 0);

	if (material) {
		if (*material >= (uint8_t)game3dshapes[124].shape3d_numpaints)
			*material = 0;
		ts.material = *material;
	}
	ts.rotvec.z = rotation;
	transformed_shape_op(&ts);
	/* seg000:3985. transformed_shape_op only queues polygons; get_a_poly_info
	 * is what rasterises them, and the original calls it once per frame after
	 * every shape is in - right after the "stop" backdrop and before
	 * sprite_copy_wnd_to_1. The car is the only shape queued here, so the
	 * flush belongs at the end of this function. Without it the turntable
	 * renders a perfectly empty black screen, which is how this was found. */
	get_a_poly_info();
}

/*
 * The acceleration curve. Returns the number of points plotted, which is how
 * long the car took to reach the top of the box - a smaller number is a
 * faster car, and it is the only number this screen really conveys.
 */
int carmenu_draw_accel_graph(void)
{
	struct CARSTATE saved;
	uint16_t saved_fps;
	int16_t var_4A;
	int plotted = 0;

	saved = state.playerstate;
	saved_fps = framespersec;
	framespersec = 0x14;                     /* seg000:12294 */

	/* [DEVIATION] stands in for init_game_state(-2) - see the file header. */
	memset(&state.playerstate, 0, sizeof state.playerstate);
	state.playerstate.car_transmission = 1;

	for (var_4A = 0; var_4A < 0x320; var_4A++) {   /* loc_122CE */
		int16_t var_carspeed, var_48, var_44;
		uint32_t q;

		update_car_speed(1, 0, &state.playerstate, &simd_player);
		var_carspeed = (int16_t)((uint16_t)state.playerstate.car_speed >> 8);

		/* __aFuldiv((carspeed << 6), 0x96) - the dividend is pushed last */
		q = ((uint32_t)(uint16_t)var_carspeed << 6) / 0x96u;
		var_48 = (int16_t)(0xB5 - (int16_t)q);
		if (var_48 < 0x75) break;              /* loc_12344 */

		var_44 = (int16_t)(((uint16_t)(0x26 * var_4A)) / 0x320u + 0x1C);
		putpixel_single_maybe((uint16_t)(var_44 * RFB_SCALE),
		                      (uint16_t)(var_48 * RFB_SCALE),
		                      (uint16_t)performGraphColor);
		plotted++;
	}

	framespersec = saved_fps;
	state.playerstate = saved;
	return plotted;
}
