/*
 * music_native.h - Phase 8. Music.
 *
 * The original's music is a small MIDI-like sequencer (seg027/seg028/seg029)
 * reading .KMS song files and .VCE instrument banks, driving a loadable
 * device driver (AD15.DRV for AdLib/OPL2, MT15.DRV, PC15.DRV, TD15.DRV).
 *
 * This module is that sequencer, ported instruction by instruction, plus a
 * re-implementation of the AdLib driver's register programming. See
 * src/music_native.c for the provenance table and every [DEVIATION].
 *
 * Nothing here touches audio_native.c's state; the two are independent.
 * The caller owns the audio device: call music_native_mix() from whatever
 * already produces samples, and it will ADD the music into the buffer.
 * The 100 Hz driver timer that the original ran off the PIT is derived
 * from the sample count inside music_native_mix(), so the music tempo is
 * independent of the video/simulation frame rate.
 */
#ifndef STUNTS_MUSIC_NATIVE_H
#define STUNTS_MUSIC_NATIVE_H

#include <stdint.h>
#include <stdio.h>

/* The four songs the original ships. The names are the .KMS basenames. */
typedef enum {
    MUSIC_SONG_TITLE   = 0,   /* SKIDTITL - title screen              */
    MUSIC_SONG_SELECT  = 1,   /* SKIDSLCT - car/track selection       */
    MUSIC_SONG_GAMEOVER= 2,   /* SKIDOVER - "game over"               */
    MUSIC_SONG_VICTORY = 3,   /* SKIDVICT - you won                   */
    MUSIC_SONG_COUNT   = 4
} music_song_t;

/* Load AD*.VCE plus the four .KMS files from data_dir and bring the OPL
 * core up at sample_rate Hz. Returns 0 on success, non-zero on failure
 * (in which case every other call here is a silent no-op).
 * Safe to call twice; the second call is a no-op. */
int  music_native_init(const char *data_dir, int sample_rate);

void music_native_shutdown(void);

/* Start a song from its beginning. Any song already playing is stopped
 * first. No-op when music is disabled. */
void music_native_play(music_song_t song);

/* Silence everything and park the sequencer. */
void music_native_stop(void);

/* Non-zero while a song is running (a song that has run off the end of all
 * its tracks reports 0; the three looping songs never do). */
int  music_native_playing(void);

/* The option menu's "Music on/off". Turning it off stops any current song. */
void music_native_set_enabled(int on);
int  music_native_enabled(void);

/* Master attenuation, 0..127, applied on top of the per-track volumes.
 * Mirrors the original's byte_45950. Default 127. */
void music_native_set_volume(int vol_0_127);

/* ADD `frames` mono samples of music into out[0..frames-1] (or interleaved
 * stereo when channels == 2; the music is mono and is written to both).
 * Advances the emulated 100 Hz driver timer by frames/sample_rate seconds.
 * Never blocks, never allocates. Safe to call when nothing is playing (it
 * then does nothing at all). */
void music_native_mix(int16_t *out, int frames, int channels);

/* ---------------- verification entry points ---------------------- */

/* Print the fully decoded event stream of one song to `out`, one line per
 * event, with running tick counts and per-track summaries. This is the
 * primary correctness check on the sequencer: it needs no audio device. */
int  music_native_dump_events(music_song_t song, FILE *out);

/* Print every OPL register write the driver layer makes while rendering
 * `seconds` of the song. */
int  music_native_dump_opl(music_song_t song, double seconds, FILE *out);

/* Render `seconds` of the song to a 16-bit mono PCM .wav at wav_path,
 * deterministically and as fast as the CPU allows. Returns 0 on success. */
int  music_native_render_wav(music_song_t song, double seconds,
                             const char *wav_path);

/* Name of the OPL core actually compiled in, and whether it is a real
 * emulation (1) or the built-in verification stub (0). */
const char *music_native_opl_core(void);
int         music_native_opl_is_real(void);

#endif /* STUNTS_MUSIC_NATIVE_H */
