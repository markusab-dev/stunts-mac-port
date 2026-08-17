/*
 * sfstubs.c - TEMPORARY stand-ins for the remaining seg001.asm helpers that the
 * vendored simulation calls but which are not yet translated to C.
 *
 * These are NOT ports. Each returns a neutral value so the simulation links
 * and the ported physics can be exercised; any behaviour they gate is
 * therefore missing or wrong. They must be replaced by instruction-exact
 * translations (see sfport.h for the seg001.asm line ranges) before the
 * simulation can be compared against the DOSBox oracle.
 *
 * Remaining stubs and their sfstub_hits[] slots:
 *     [4] audio_unk3  (sound only; slot [3] is now a live counter, see below)
 * Slots 0 (detect_penalty), 1 (carState_rc_op), 5 (sub_18D60) and 7
 * (bto_auxiliary1) are retired: those four now have instruction-exact
 * translations in sfasm_port.c, and slots 2 (car_car_speed_adjust_maybe)
 * and 6 (car_car_coll_detect_maybe) are retired into sfopponent.c. The
 * counters are deliberately NOT renumbered, so existing indices stay valid.
 *
 * Exception: audio_unk3 is genuinely a no-op here — it drives sound, which
 * this port does not implement, and it has no effect on simulation state.
 */
#include <stdint.h>
#include "../render_faithful/externs.h"

int16_t sfstub_hits[8];   /* call counters, so tests can detect reliance */

/* detect_penalty (formerly sfstub_hits[0]) is no longer stubbed: it is now
 * translated instruction-exactly in sfasm_port.c. Slot 0 of sfstub_hits is
 * left unused rather than renumbering the remaining counters. */

/* carState_rc_op (formerly sfstub_hits[1]) is no longer stubbed: it is now
 * translated instruction-exactly in sfasm_port.c. Slot 1 of sfstub_hits is
 * left unused rather than renumbering the remaining counters. */

/* car_car_speed_adjust_maybe (formerly sfstub_hits[2]) is no longer stubbed:
 * it is now translated instruction-exactly in sfopponent.c. Slot 2 of
 * sfstub_hits is left unused rather than renumbering the remaining counters. */

/* state_op_unk (formerly the only simulation-affecting stub left, slot
 * sfstub_hits[3]) is no longer stubbed: it is now translated
 * instruction-exactly in sfasm_port.c, which still bumps slot 3 so the
 * harnesses' "attrapper:" line keeps counting crash-debris spawns. It is
 * the crash-debris spawner, and its partner sub_19BA0 - which
 * update_gamestate runs every frame while state.field_42A is set - is in
 * the same file. */

/* sub_18D60 (formerly sfstub_hits[5]) is no longer stubbed: it is now
 * translated instruction-exactly in sfasm_port.c. Slot 5 of sfstub_hits is
 * left unused rather than renumbering the remaining counters. */

/* car_car_coll_detect_maybe (formerly sfstub_hits[6]) is no longer stubbed:
 * it is now translated instruction-exactly in sfopponent.c. Slot 6 of
 * sfstub_hits is left unused rather than renumbering the remaining counters. */

/* bto_auxiliary1 (formerly sfstub_hits[7]) is no longer stubbed: it is now
 * translated instruction-exactly in sfasm_port.c. Slot 7 of sfstub_hits is
 * left unused rather than renumbering the remaining counters. */

/* Sound triggers (seg007.asm / seg001.asm). These touch no simulation
 * state, so they stay no-ops here and forward to src/audio_native.c when
 * that has installed itself. The indirection keeps bin/dump_native_states -
 * which links this file but not SDL - building and behaving exactly as
 * before: with no audio layer the hooks are NULL and nothing happens. */
void (*audio_hook_unk3)(int16_t, int16_t);
void (*audio_hook_fn2wrap)(int16_t);

void audio_function2_wrap(int16_t a)
{
	if (audio_hook_fn2wrap) audio_hook_fn2wrap(a);
}

/* Engine/skid sound trigger. No simulation state. */
void audio_unk3(int16_t a, int16_t b)
{
	sfstub_hits[4]++;
	if (audio_hook_unk3) audio_hook_unk3(a, b);
}
