# Sound effects: what is wrong and what the data says

The music is right — real FM through a faithful YM3812 emulation, note events
verified against the ported sequencer. The engine, skid and impact sounds are
not, and no amount of tuning will fix them, because they do not go through the
chip at all.

## The actual state

`src/audio_native.c` says so in its own header:

> A small SDL2 synth: a two-saw engine tone through a one-pole lowpass,
> band-passed noise for the two skid variants, and three decaying noise bursts
> for crash / bump / scrape.

and `grep -c opl_write src/audio_native.c` returns **0**.

So the effects are somebody's approximation of how they should sound, driven
by correctly ported numbers. The pitch is now right — `ADENG1.VCE`'s divisor
of 11 replaced the PC speaker's 6, measured 556 Hz → 304 Hz, a ratio of 11/6
exactly — but the *timbre* is a different instrument playing the right note.

## What the data actually contains

Both `.VCE` files are archives with the game's usual 4-char tag layout: a
6-byte header, `count` 4-byte tags, `count` 4-byte offsets, then the records.
Every record is **100 bytes**.

**`ADENG1.VCE` holds all eight effects**, not just the engine:

    STOP  STAR  ENGI  BLOW  SKID  SCRA  BUMP  CRAS

engine stop, engine start, engine running, blow, skid, scrape, bump, crash.
The port reads two bytes of one of them.

**`ADSKIDMS.VCE` is not skid sounds.** It is the *music* instrument bank — 25
patches:

    ELPI BRAS PLK1 KOTO HRM1 SNAR BASD HIHT DRUM TOMM HRN1 LED2 HRN2 HRN9
    ORGN GUIT LEAD BASS KEYS GUT2 STRT CHHT OHHT RIDE CRSH

Electric piano, brass, koto, snare, bass drum, hihat, organ, guitar, strings,
crash cymbal. The filename misleads exactly the way `load_tracks_menu_shapes`
did when it turned out to be the track editor.

## The lead worth following first

The eight effect records share a prefix and differ where it matters:

    STOP  64 00 01 00 00 00 00 00 00 00 00 00 FC 00 00 7F ...
    STAR  64 00 01 00 00 00 00 00 00 00 00 00 FC 00 00 7F ...
    ENGI  64 00 01 00 00 00 00 00 00 00 00 00 FE 00 0B 00 ...

Offset 0x0E is `0x0B` = 11 in ENGI — the divisor we just proved is real by
measurement. So the record *is* the per-effect parameter block, offset 0x0E is
a known-good landmark inside it, and the surrounding bytes are the rest of the
FM patch.

That gives a way in that does not depend on guessing: change one byte in a
known field, hear the predicted change, and work outwards.

## The job

1. **Decode the 100-byte record** as OPL operator settings. `AD15.DRV` (3571
   bytes of 8086) is what maps it to register writes; the music path already
   solved the equivalent problem for the sequencer, so the technique and the
   `music_opl.h` boundary already exist.
2. **Route the effects through `opl_write`** instead of the SDL synth, keeping
   the ported trigger logic (which is faithful) and replacing only the sound
   generation.
3. **Verify the way the music was verified**: render the same sequence before
   and after, and compare against a DOSBox capture of the real driver. Note
   starts and pitch first, timbre second.

## Two cautions

* `tools/verify.sh` has no audio check at all. Whatever is built here needs
  one, or it will drift silently the way the replay format did.
* The trigger logic in `audio_native.c` — when each effect starts and stops,
  and the rpm and Doppler maths — is ported and behaves correctly. Only the
  synthesis is wrong. Do not rewrite what already works.
