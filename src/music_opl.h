/*
 * music_opl.h - the OPL2 (Yamaha YM3812) register interface that the ported
 * AdLib driver writes to.
 *
 * Everything above this line is Stunts. Everything below it is a chip.
 * The split exists so that a real, externally maintained OPL core can be
 * dropped in without touching a line of the sequencer or driver port.
 *
 * ==================================================================
 * THE CORE BEHIND THIS INTERFACE IS *NOT* PRESENT IN THIS REPOSITORY.
 * ==================================================================
 *
 * Phase 8 was specified as "vendor Nuked-OPL3 or ymfm, do not write your
 * own". Neither could be fetched: this machine has no vendored copy, and
 * downloading one was not something this session was permitted to do. So
 * what is compiled in today is src/music_opl_stub.c, which is deliberately
 * NOT a synthesiser - see the header of that file. It exists so that the
 * note stream can be *measured* (pitch and timing) without a chip.
 *
 * To vendor the real thing:
 *   1. put the core under  src/vendored/opl/  with its licence intact
 *      (Nuked-OPL3: nuked_opl3.c/.h, LGPL-2.1; ymfm: ymfm_opl.*, BSD-3),
 *   2. write a ~40 line src/music_opl_<core>.c implementing the five
 *      functions below on top of it,
 *   3. swap music_opl_stub.c for it in tools/build_native.sh and
 *      tools/build_music_tool.sh, and make opl_core_is_real() return 1.
 * Nothing else changes. See src/vendored/opl/README for the details.
 */
#ifndef STUNTS_MUSIC_OPL_H
#define STUNTS_MUSIC_OPL_H

#include <stdint.h>

/* The YM3812's own sample rate: 3.579545 MHz / 72. Any core renders at
 * this rate natively; resampling to the host rate is this module's job. */
#define OPL_NATIVE_RATE 49716

/* Bring the chip up and silence it. Called again on every song start. */
void opl_reset(int host_sample_rate);

/* One register write: index `reg` (0x00..0xF5), value `val`.
 * On real hardware this is `out 0x388, reg` / `out 0x389, val` with the
 * mandated delays; the delays do not exist here. */
void opl_write(uint8_t reg, uint8_t val);

/* Render `frames` mono samples at the host sample rate given to
 * opl_reset(), WRITING (not adding) into buf. */
void opl_render(int16_t *buf, int frames);

/* Human-readable name of the core, for logs and for
 * music_native_opl_core(). */
const char *opl_core_name(void);

/* 1 for a real OPL emulation, 0 for the verification stub. Anything that
 * claims "the music sounds right" must check this first. */
int opl_core_is_real(void);

/* ---- introspection, used by music_native_dump_opl() ---------------- */

/* Set to non-NULL to receive every register write. */
extern void (*opl_write_hook)(uint8_t reg, uint8_t val, void *ud);
extern void  *opl_write_hook_ud;

#endif /* STUNTS_MUSIC_OPL_H */
