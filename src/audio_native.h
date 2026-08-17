/*
 * audio_native.h - the port's sound layer.
 *
 * Two halves, and the split matters:
 *
 *  - The DECISION half is an instruction-level port of the original's
 *    audio_carstate (seg001), audio_unk3, audio_function2_wrap and the
 *    ring-buffer consumer sub_18D06 / audio_op_unk2. It decides what should
 *    sound, how loud, and at what pitch. See src/audio_native.c.
 *
 *  - The PLAYBACK half is written fresh on SDL2. The original's .VCE / .SFX
 *    files are OPL2 / MT-32 / PC-speaker synthesis parameter sets, not
 *    samples; playing them would need an OPL emulator, which is out of
 *    scope by explicit choice. So the tones are synthesised here.
 */
#ifndef STUNTS_AUDIO_NATIVE_H
#define STUNTS_AUDIO_NATIVE_H

#include <stdint.h>

/* mode: 0 = silent (nothing runs, no state touched at all)
 *       1 = play through an SDL audio device
 *       2 = render deterministically to a .wav, no device            */
int  audio_native_init(const char* data_dir, int mode, const char* wav_path);

/* Called once per simulation frame, AFTER player_op()/opponent_op(). */
void audio_native_frame(void);

/* The option menu's "Sound effects on/off" entry. */
void audio_native_set_enabled(int on);
int  audio_native_enabled(void);

/* Print one trace line per frame: rpm, frequency, volume, flags. */
void audio_native_set_trace(int on);

void audio_native_shutdown(void);

#endif
