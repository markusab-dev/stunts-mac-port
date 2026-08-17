/*
 * rstubs.c - Stubs for HUD/2D-overlay routines called by update_frame that
 * are not part of the 3D scene (timer text, crash cracks, water sinking,
 * explosion sprites, fonts). These draw dashboard-era 2D resources; they can
 * be ported later without affecting 3D fidelity.
 *
 * sprite_set_1_size is a real implementation: update_frame uses it to set
 * the render-target clip window from the windshield rect.
 */
#include <stdint.h>
#include "externs.h"
#include "shape2d.h"
#include "math.h"

extern struct SPRITE sprite1;

static struct RECTANGLE s_dummy_rect;

/* Car resource handles (set by shape3d_load_car_shapes) and the slow-video
 * dirty-rect flag (always 0 natively: full-frame redraw). */
int16_t far* carresptr;
int16_t far* car2resptr;
uint16_t slow_video_mgmt_copy;

/* update_frame calls: sprite_set_1_size(0, 0x140, cliprect->top, cliprect->bottom) */
void sprite_set_1_size(uint16_t left, uint16_t right, uint16_t top, uint16_t height)
{
	/* The original writes each edge to both of its aliases (seg012
	 * sprite_set_1_size): the span blitters read left2/widthsum, the
	 * SHAPE2D blitters in rshape2d.c read left/right. */
	sprite1.sprite_left2 = left;
	sprite1.sprite_left = left;
	sprite1.sprite_widthsum = right;
	sprite1.sprite_right = right;
	sprite1.sprite_top = top;
	sprite1.sprite_height = height;
}

/* draw_ingame_text: now implemented in ringame_text.c */


/* init_crak: now implemented in rcrash.c */

/* do_sinking: now implemented in rcrash.c */

/* intro_draw_text: now implemented in rfont.c */

/* font_set_fontdef: now implemented in rfont.c */
/* font_set_fontdef2: now implemented in rfont.c */

/* format_frame_as_string: now implemented in rfont.c */

/* shape_op_explosion: now implemented in rexplode.c */

