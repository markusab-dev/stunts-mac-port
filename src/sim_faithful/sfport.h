/*
 * sfport.h - Native-port prelude for the vendored restunts simulation
 * sources (state.c, statecar.c, statecrs.c, stateply.c).
 *
 * Force-included via `clang -include sfport.h`, mirroring the renderer's
 * rport.h. The simulation sources contain NO inline assembly, so this is
 * purely declarations.
 */
#ifndef STUNTS_SFPORT_H
#define STUNTS_SFPORT_H

#include "../render_faithful/rport.h"

struct CARSTATE;
struct SIMD;
struct VECTOR;

/* Defined in statecrs.c */
void update_crash_state(int16_t arg_someFlag, int16_t arg_MplayerFlag);

/* Defined in the renderer port (rframe_helpers.c) */
void build_track_object(struct VECTOR* a, struct VECTOR* b);

/* ------------------------------------------------------------------
 * Helpers that exist only as 16-bit assembly in seg001.asm and are not
 * yet translated. Signatures derived from the call sites and the asm
 * stack frames; temporary implementations live in sfstubs.c.
 *
 *   detect_penalty              seg001.asm 5307-5587  (280 lines)
 *   carState_rc_op              seg001.asm 6863-7056  (193 lines)
 *   car_car_speed_adjust_maybe  seg001.asm 6689-6862  (173 lines)
 *   state_op_unk                seg001.asm 9215-9385  (170 lines)
 *   audio_unk3                  seg001.asm 7677-7701   (24 lines, audio)
 * ------------------------------------------------------------------ */
int16_t detect_penalty(int16_t* a, int16_t* penaltyCounter);
int16_t carState_rc_op(struct CARSTATE* pState, int16_t arg2, int16_t wheelIndex);
int16_t car_car_speed_adjust_maybe(struct CARSTATE* pState, struct CARSTATE* oState);
void state_op_unk(int16_t a, int16_t b, int16_t c);
void audio_unk3(int16_t a, int16_t b);
void audio_function2_wrap(int16_t a);   /* seg007.asm, sound only */

/* Further seg001/seg004-only helpers, stubbed in sfstubs.c:
 *   sub_18D60                   seg001.asm 383 lines - start/reset path
 *   car_car_coll_detect_maybe   seg001.asm 404 lines - car-to-car collision
 *   bto_auxiliary1              seg004.asm 426 lines - track-object geometry
 * Signatures from the declarations already present in state.c / stateply.c. */
struct POINT2D;
int16_t car_car_coll_detect_maybe(struct POINT2D* a, struct VECTOR* b,
                                  struct POINT2D* c, struct VECTOR* d);
int16_t bto_auxiliary1(int16_t x, int16_t z, struct VECTOR* out);

#endif /* STUNTS_SFPORT_H */
