/*
 * music_opl_stub.c - a register-honest TONE PROBE. It is NOT an OPL2
 * emulator and must never be described as one.
 *
 * WHAT IT DOES:
 *   It keeps the OPL2 register file, decodes the three things needed to
 *   check that the sequencer and the driver port agree with the score -
 *   per-channel F-Number, Block and Key-On, plus the carrier operator's
 *   Total Level - and emits one sine per keyed channel at
 *
 *       f = fnum * 49716 / 2^(20 - block)
 *
 *   which is the documented YM3812 pitch formula. That makes the output
 *   analysable: an FFT of the rendered .wav yields fundamentals that can be
 *   compared against the note numbers decoded from the .KMS, which is the
 *   whole point of the exercise.
 *
 * WHAT IT DOES NOT DO:
 *   No FM. No operator envelopes (ADSR is ignored; the driver's own
 *   software envelope in sub_39700 still runs and still moves the Total
 *   Level, so the amplitude does move). No feedback, no waveform select,
 *   no key-scaling, no rhythm mode, no vibrato/tremolo depth, no
 *   multiplier, no percussion. Timbre is meaningless.
 *
 * So: the pitches and the note timing coming out of this file are real
 * measurements of the port. The SOUND is not the game's sound.
 *
 * Replace it - see src/music_opl.h and src/vendored/opl/README.
 */

#include <math.h>
#include <string.h>

#include "music_opl.h"

void (*opl_write_hook)(uint8_t reg, uint8_t val, void *ud) = 0;
void  *opl_write_hook_ud = 0;

#define NCH 9

/* Operator slot offsets, channel -> modulator slot. Carrier = +3.
 * This is the standard OPL2 layout. */
static const uint8_t slot_of_ch[NCH] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

static uint8_t regs[256];
static double  phase[NCH];
static int     host_rate = 44100;

void opl_reset(int host_sample_rate)
{
    memset(regs, 0, sizeof regs);
    memset(phase, 0, sizeof phase);
    host_rate = host_sample_rate > 0 ? host_sample_rate : 44100;
}

void opl_write(uint8_t reg, uint8_t val)
{
    regs[reg] = val;
    if (opl_write_hook) opl_write_hook(reg, val, opl_write_hook_ud);
}

const char *opl_core_name(void) { return "stub (tone probe, not an emulator)"; }
int         opl_core_is_real(void) { return 0; }

void opl_render(int16_t *buf, int frames)
{
    int i, c;
    double freq[NCH], amp[NCH];

    for (c = 0; c < NCH; c++) {
        uint8_t b   = regs[0xB0 + c];
        int     on  = (b & 0x20) != 0;
        int     blk = (b >> 2) & 7;
        int     fn  = ((b & 3) << 8) | regs[0xA0 + c];
        uint8_t car = (uint8_t)(slot_of_ch[c] + 3);
        int     tl  = regs[0x40 + car] & 0x3F;
        /* When the channel is in additive mode (C0 bit 0 set) the modulator
         * is audible too; ignored - carrier only. */
        freq[c] = on ? (double)fn * (double)OPL_NATIVE_RATE
                     / (double)(1u << (20 - blk))
                    : 0.0;
        /* Total Level is 0.75 dB per step, 0 = loudest. */
        amp[c] = (on && fn) ? pow(10.0, -0.75 * tl / 20.0) : 0.0;
    }

    for (i = 0; i < frames; i++) {
        double s = 0.0;
        for (c = 0; c < NCH; c++) {
            if (amp[c] <= 0.0) continue;
            s += amp[c] * sin(phase[c]);
            phase[c] += 2.0 * M_PI * freq[c] / host_rate;
            if (phase[c] > 2.0 * M_PI) phase[c] -= 2.0 * M_PI;
        }
        s *= 3000.0;                       /* headroom for 9 voices */
        if (s >  32000.0) s =  32000.0;
        if (s < -32000.0) s = -32000.0;
        buf[i] = (int16_t)s;
    }
}
