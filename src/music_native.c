/*
 * music_native.c - Phase 8. The music sequencer.
 *
 * ====================================================================
 * PROVENANCE
 * ====================================================================
 *
 * Line numbers are reference/restunts/src/restunts/asm/ (restunts1), which
 * is the only one of the two disassemblies that carries bodies for the
 * audio segments. restunts2 agrees where it overlaps.
 *
 *   CONTAINER  (seg029, 367 lines - the .KMS/.VCE/.SFX chunk format)
 *     audioresource_compare_chunknames   seg029.asm 57..175
 *     audioresource_get_chunk_index      seg029.asm 176..247
 *     audioresource_find                 seg029.asm 248..321
 *     audioresource_copy_n_bytes         seg029.asm 322..365
 *
 *   LOADER / LINKER  (seg027, 2798 lines)
 *     init_audio_resources               seg027.asm  92..228
 *     load_audio_finalize                seg027.asm 229..320
 *     audio_load_driver                  seg027.asm 1077..1258
 *     audio_init_chunk                   seg027.asm 1867..1968
 *     audio_map_song_instruments         seg027.asm 1969..2100
 *     audio_map_song_tracks              seg027.asm 2354..2604
 *     audio_check_flag                   seg027.asm  ...
 *
 *   SEQUENCER  (seg028, 2590 lines)
 *     audiodriver_timer                  seg028.asm  71..126
 *     sub_3868A   (music tick)           seg028.asm 127..176
 *     sub_386D6   (sfx tick)             seg028.asm 177..215
 *     sub_38702   (per-track tick)       seg028.asm 216..671   <- the core
 *     off_38A1E   (18-entry opcode table)seg028.asm 555..573
 *     sub_38AC4   (instrument lookup)    seg028.asm 672..698
 *     sub_38AEA   (controller)           seg028.asm 699..817
 *     sub_38BEA   (pitch bend)           seg028.asm 818..870
 *     audio_unk2  (channel volume)       seg028.asm 871..959
 *     sub_38DE6   (note on)              seg028.asm 1073..1345
 *     off_38E7E   (drum-kit dispatch)    seg028.asm 1250..1267
 *     loc_390C8   (voice allocation)     seg028.asm 1435..1806
 *     sub_3945A   (event parser)         seg028.asm 1807..2034
 *     off_395A8   (18-entry parse table) seg028.asm 1975..1993
 *     sub_3963C   (age music voices)     seg028.asm 2035..2080
 *     sub_3968A   (one voice's duration) seg028.asm 2081..2140
 *     sub_39700   (envelope/vib/arp)     seg028.asm 2141..2402
 *     audio_driver_func1E (free voices)  seg028.asm 2403..2589
 *
 *   TIMER RATE  (reference/restunts2/src/asm/seg012.asm 3215..3307)
 *     timer_setup_interrupt programs PIT channel 0 with divisor 0x2E9C =
 *     11932, i.e. 1193182/11932 = 100.0 Hz. Everything below is driven at
 *     that rate. The comment in restunts2 says "11977"; it is wrong,
 *     0x2E9C is 11932.
 *
 * ====================================================================
 * THE FILE FORMAT, confirmed byte by byte against SKIDTITL.KMS
 * ====================================================================
 *
 * A chunk container (audioresource_find) is:
 *     +0  dword  length, counted from +0 to the end of the chunk
 *     +4  word   n = number of sub-chunks
 *     +6  n*4    four-character sub-chunk names
 *   +6+4n  n*4   dwords: each sub-chunk's offset relative to the data area
 *   +6+8n  ...   the data area; sub-chunk i starts at +6+8n+offset[i] and
 *                itself begins with its own length dword.
 * The format nests: a .KMS is a one-chunk container ("titl"/"slct"/"over"/
 * "vict") whose single chunk is another container holding "HDR1" and one
 * chunk per track ("t0s0".."t6s0").
 *
 * HDR1, relative to the start of the chunk (i.e. including its length dword):
 *     +0  dword  length
 *     +4  byte   unused
 *     +5  byte   "already linked" flag
 *     +6  byte   ninstr
 *     +7  ninstr*4  instrument names, looked up in the .VCE
 *   +7+4ni byte  ntracks
 *   +8+4ni ntracks*5  per track: 4-char chunk name + 1 unused byte
 * (SKIDTITL: length 58 = 4+3+5*4+1+6*5. Exact.)
 *
 * The original REWRITES those 4-char names in place with far pointers
 * (audio_map_song_instruments / audio_map_song_tracks). A 16-bit far
 * pointer does not fit a 64-bit host, so this port resolves names to
 * pointers at load time into side tables instead. [DEVIATION], mechanical.
 *
 * A .VCE is a flat container of 4-char-named 100-byte instrument records
 * (93 for the non-AdLib banks). ADSKIDMS.VCE: 25 records, ELPI BRAS PLK1
 * KOTO HRM1 SNAR BASD HIHT DRUM TOMM HRN1 LED2 HRN2 HRN9 ORGN GUIT LEAD
 * BASS KEYS GUT2 STRT CHHT OHHT RIDE CRSH.
 *
 * EVENT STREAM. Each track chunk is length dword + a stream of:
 *     delta   variable-length, 7 bits per byte, high bit = continue, MSB
 *             first (sub_3945A loc_39480)
 *     status  one byte
 *     then, by status:
 *       < 0xD9   a NOTE. status & 0x7F is the note number. If status > 0x80
 *                one velocity byte follows; otherwise the track's running
 *                default velocity (set by 0xE4) is used. Then a
 *                variable-length DURATION in ticks.
 *       0xD9 RETURN     pop the call stack, or end the track if empty
 *       0xDA STOP       stop the track and re-init it
 *       0xDB RESTART    reset both stacks, jump to the track start
 *       0xDC PATCH  b   select instrument b from HDR1's table
 *       0xDD TEMPO  b   b = BPM (see below)
 *       0xDE VOLUME b   channel volume
 *       0xDF CTRL   b,b controller number + value (0x40 = sustain pedal)
 *       0xE0        b   -> track[+0x16]
 *       0xE1 PRIO   b   -> track[+0x24], voice-stealing priority
 *       0xE2 LOOP   b   push loop point, repeat count b-1
 *       0xE3 ENDLOOP    pop/repeat
 *       0xE4 DEFVEL b   running default velocity
 *       0xE5 BEND   w   pitch bend, 14-bit
 *       0xE6 CALL   b,4 push return address, jump to a named chunk +4
 *       0xE7 COMMENT n,n bytes   track name; ignored at play time
 *       0xE8 TEXT    n,n bytes   copied to a buffer, MT-32 LCD display
 *       0xE9 CHAN   b   -> track[+0x47]
 *       0xEA        b   -> a per-track byte array
 *
 * TEMPO. 0xDD sets word_454BA = 32000 / b. Every 100 Hz timer tick adds
 * 0x80 to an accumulator and spends it in units of word_454BA, so
 *     sequencer ticks per second = 100 * 128 * b / 32000 = 0.4 * b
 * and with b = 120 that is 48 ticks/s = 24 ticks per quarter note. The
 * tempo byte is BPM and the resolution is 24 PPQN. Before any 0xDD,
 * word_454BA is 0x80, i.e. 100 ticks/s (load_audio_finalize).
 *
 * ====================================================================
 * WHAT IS NOT PORTED
 * ====================================================================
 *
 * - The MT-32, Tandy and PC-speaker drivers. Only the AdLib path
 *   (byte_40634 == 0) exists here; every MT-32 branch is dropped and
 *   marked [DEVIATION MT32].
 * - The sound-effect half. Tracks 0x10..0x16 and sub_386D6 belong to
 *   audio_native.c's world, not this one, and are not run.
 * - The .SFX container and load_sfx_file; likewise sound effects.
 * - security_check-style file fallbacks in load_sfx_ge (.DSF/.DVC/.GE
 *   variants of every name); this port opens the plain .KMS/.VCE.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music_native.h"
#include "music_opl.h"

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* container layer - seg029                                           */
/* ------------------------------------------------------------------ */

/* audioresource_get_chunk_index, seg029.asm 176..247. The original's
 * comparison is case-insensitive when its `casesensitive` argument is 0,
 * which is what every caller passes. */
static int res_chunk_index(const uint8_t *chunk, const char *name4)
{
    int n = rd16(chunk + 4), i, j;
    for (i = 0; i < n; i++) {
        const uint8_t *nm = chunk + 6 + 4 * i;
        for (j = 0; j < 4; j++) {
            int a = nm[j], b = (uint8_t)name4[j];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) break;
            if (a == 0) { j = 4; break; }   /* both terminated: match */
        }
        if (j >= 4) return i;
    }
    return -1;
}

/* audioresource_find, seg029.asm 248..321. `chunk` points at the length
 * dword of a container; returns a pointer to the named sub-chunk's own
 * length dword, or NULL. */
static const uint8_t *res_find(const uint8_t *chunk, const char *name4)
{
    int n, idx;
    uint32_t off;
    if (!chunk) return NULL;
    n = rd16(chunk + 4);
    idx = res_chunk_index(chunk, name4);
    if (idx < 0) return NULL;
    off = rd32(chunk + 6 + 4 * n + 4 * idx);
    return chunk + 6 + 8 * n + off;
}

/* ------------------------------------------------------------------ */
/* event parser - sub_3945A, seg028.asm 1807..2034                    */
/* ------------------------------------------------------------------ */

/* The 11-byte structure the original keeps at dseg 0x728E (and a second
 * copy at 0x7282 used only to peek the next delta). Field offsets below
 * are the original's. */
typedef struct {
    uint32_t delta;     /* +0x00 */
    uint8_t  status;    /* +0x04 */
    uint8_t  d1;        /* +0x05 */
    uint32_t d2;        /* +0x06 */
    uint8_t  len;       /* +0x0A  bytes consumed */
    const uint8_t *text;/* not in the original: 0xE7/0xE8 payload           */
    uint8_t  textlen;
} mus_event;

/* Bytes consumed after the status byte, indexed by status - 0xD9.
 * From off_395A8 (parser) and off_383A0 (the scanner in
 * audio_map_song_tracks); the two agree. */
enum { PARG_NONE = 0, PARG_B1, PARG_B1B2, PARG_W2, PARG_B1D4, PARG_TEXT };
static const uint8_t opcode_args[18] = {
/* D9 */ PARG_NONE, /* DA */ PARG_NONE, /* DB */ PARG_NONE,
/* DC */ PARG_B1,   /* DD */ PARG_B1,   /* DE */ PARG_B1,
/* DF */ PARG_B1B2, /* E0 */ PARG_B1,   /* E1 */ PARG_B1,
/* E2 */ PARG_B1,   /* E3 */ PARG_NONE, /* E4 */ PARG_B1,
/* E5 */ PARG_W2,   /* E6 */ PARG_B1D4, /* E7 */ PARG_TEXT,
/* E8 */ PARG_TEXT, /* E9 */ PARG_B1,   /* EA */ PARG_B1
};

static uint32_t read_vlq(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    uint32_t v = 0;
    for (;;) {
        uint8_t b = *p++;
        v = (v << 7) + (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    *pp = p;
    return v;
}

static void parse_event(mus_event *ev, const uint8_t *p)
{
    const uint8_t *start = p;
    memset(ev, 0, sizeof *ev);
    ev->delta = read_vlq(&p);
    ev->status = *p++;
    if (ev->status >= 0xD9) {
        int idx = ev->status - 0xD9;
        if (idx <= 0x11) {
            switch (opcode_args[idx]) {
            case PARG_NONE: break;
            case PARG_B1:   ev->d1 = *p++; break;
            case PARG_B1B2: ev->d1 = *p++; ev->d2 = *p++; break;
            case PARG_W2:   ev->d2 = rd16(p); p += 2; break;
            case PARG_B1D4: ev->d1 = *p++; ev->d2 = rd32(p); p += 4; break;
            case PARG_TEXT: ev->textlen = *p; ev->text = p + 1;
                            p += (size_t)ev->textlen + 1; break;
            }
        }
        /* idx > 0x11 cannot happen: status <= 0xFF and 0xD9+0x11 = 0xEA,
         * but statuses 0xEB..0xFF fall through here consuming nothing, and
         * sub_38702 then treats them as no-ops. Faithful. */
    } else {
        /* loc_395CC. Note the boundary: the parser skips the velocity byte
         * only when status > 0x80, but the SCANNER in audio_map_song_tracks
         * (loc_3837E) skips it when status >= 0x80. [ODDITY] the two
         * disagree for the single value status == 0x80 (note 0 with an
         * explicit velocity). No song in the game uses it - the lowest note
         * status seen in any of the four .KMS files is 0x98 - so the
         * disagreement is unreachable. This port follows the parser. */
        if (ev->status > 0x80) ev->d1 = *p++;
        ev->d2 = read_vlq(&p);
    }
    ev->len = (uint8_t)(p - start);
}

/* ------------------------------------------------------------------ */
/* state - dseg 0x81FC (tracks) and 0xA2B6 (voices)                    */
/* ------------------------------------------------------------------ */

#define NTRACK   0x18   /* 24 slots; 0..15 music, 16..22 sound effects */
#define NVOICE   0x10   /* the 0xA2B6 array is 16 entries              */
#define NCALL     4     /* see the [ODDITY] on the stacks below        */
#define NLOOP     4

/* dseg 0x81FC, stride 0x4C. Field comments give the original offsets.
 *
 * [ODDITY] The call stack lives at +0x05 with a stride of 4 and the loop
 * stack at +0x33 with a stride of 4, but +0x15/+0x16/+0x18 and +0x43 hold
 * other fields. So a 5th nested CALL (0xE6) in the original would write
 * over the track's active-voice count, its +0x16 byte and the low word of
 * its delay, and a 5th nested LOOP would write over the loop counters.
 * Both are latent corruption in the original. This port bounds them at 4
 * and drops the overflow. [DEVIATION] - defensive, not observable in any
 * shipped song (max nesting seen: 1 call, 1 loop). */
typedef struct {
    const uint8_t *ptr;              /* +0x00 stream position, NULL = idle */
    uint8_t  callsp;                 /* +0x04 */
    const uint8_t *callstack[NCALL]; /* +0x05 (stride 4)                   */
    uint8_t  nvoices;                /* +0x15 voices this track holds      */
    uint8_t  f16;                    /* +0x16 (0xE0), init 0x0F            */
    uint32_t delay;                  /* +0x18 ticks until the next event   */
    const uint8_t *instr;            /* +0x1E current instrument record    */
    uint8_t  defvel;                 /* +0x22 (0xE4), init 0x7F            */
    uint8_t  trackno;                /* +0x23                              */
    uint8_t  prio;                   /* +0x24 (0xE1)                       */
    uint8_t  sustain;                /* +0x25 controller 0x40              */
    uint16_t bend;                   /* +0x26 (0xE5)                       */
    uint8_t  volume;                 /* +0x28 (0xDE)                       */
    uint8_t  loopsp;                 /* +0x32 */
    const uint8_t *loopstack[NLOOP]; /* +0x33 (stride 4)                   */
    uint8_t  loopcnt[NLOOP];         /* +0x43                              */
    uint8_t  chan;                   /* +0x47 (0xE9), init 0xFF            */
    /* +0x48 is a far callback pointer, only ever set by the sound-effect
     * side (nopsub_37750). Not ported. */
    /* not in the original: the resolved per-song instrument table, which
     * the original reaches through the far pointer at +0x2E. */
    const uint8_t *const *instrtab;
    uint8_t  ninstr;
} mus_track;

/* dseg 0xA2B6, stride 0x2E. */
typedef struct {
    uint8_t  track;     /* +0x00 owning track, 0xFF = none        */
    uint8_t  state;     /* +0x01 0 free, 1 sounding, 2 releasing  */
    uint8_t  prio;      /* +0x02                                   */
    /* +0x03..+0x07 are untouched by seg028 and belong to the driver:
     * AD15.DRV keeps the note byte at +3, the packed (block<<10)|fnum
     * word at +4 and the fine-tuned copy actually written to the chip at
     * +6. See src/music_ad15.inc.c. */
    uint8_t  drvnote;   /* +0x03                                   */
    uint16_t pack;      /* +0x04                                   */
    uint16_t packtuned; /* +0x06                                   */
    uint32_t age;       /* +0x08                                   */
    uint32_t dur;       /* +0x0C ticks left                        */
    const uint8_t *instr;/*+0x10                                   */
    int16_t  level;     /* +0x14 software envelope level, signed   */
    uint8_t  stage;     /* +0x16 0 off 1 attack 2 decay 3 sus 4 rel*/
    uint16_t vibdelay;  /* +0x18                                   */
    uint16_t vibtime;   /* +0x1A                                   */
    int16_t  vibval;    /* +0x1C                                   */
    uint16_t arpdelay;  /* +0x1E                                   */
    uint16_t arptime;   /* +0x20                                   */
    uint8_t  arpoff;    /* +0x22                                   */
    uint16_t vibstep;   /* +0x24                                   */
    uint8_t  vibdir;    /* +0x26                                   */
    uint8_t  vibcnt;    /* +0x27                                   */
    uint8_t  arpcnt;    /* +0x28                                   */
    uint8_t  arpidx;    /* +0x29                                   */
    uint8_t  trk;       /* +0x2A owning track index (the original
                                 keeps a dseg pointer here)        */
    uint8_t  chan;      /* +0x2C driver channel                    */
} mus_voice;

/* Instrument record field offsets, all confirmed against ADSKIDMS.VCE.
 * Reached through sub_38DE6 / sub_39700 / loc_390C8. */
#define IN_LEN      0x00    /* word: 100 for AdLib, 93 for the rest    */
#define IN_TYPE     0x05    /* 5 = drum kit (dispatch by note number)  */
#define IN_CHANMASK 0x0C    /* word: 1<<voice for every allowed voice  */
#define IN_TRANSP   0x10    /* signed byte, semitones                  */
#define IN_LEVEL0   0x1C    /* word: initial envelope level            */
#define IN_ATKPEAK  0x1E    /* word                                    */
#define IN_ATKRATE  0x20    /* word                                    */
#define IN_DECRATE  0x22    /* word                                    */
#define IN_SUSLEVEL 0x24    /* word                                    */
#define IN_RELRATE  0x26    /* word                                    */
#define IN_VIBON    0x28    /* byte                                    */
#define IN_VIBRELOAD 0x29   /* byte                                    */
#define IN_VIBDELAY 0x2A    /* word                                    */
#define IN_VIBTIME  0x2C    /* word                                    */
#define IN_VIBDEPTH 0x2E    /* word                                    */
#define IN_VIBSTEP  0x30    /* word                                    */
#define IN_VIBFLAGS 0x34    /* byte: bit0/bit1 = allow down/up         */
#define IN_ARPON    0x35    /* byte                                    */
#define IN_ARPDELAY 0x36    /* word                                    */
#define IN_ARPTIME  0x38    /* word                                    */
#define IN_ARPRELOAD 0x3A   /* byte                                    */
#define IN_ARPTABLE 0x3B    /* 8 bytes, indexed by arpidx & 7          */
/* 0x44..0x63 of the AdLib record are the OPL2 patch itself; see the
 * driver section below. */

/* ------------------------------------------------------------------ */
/* module state                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  *kms;            /* whole .KMS file            */
    long      kmslen;
    const uint8_t *top;       /* the "titl"/"slct"/... chunk */
    const uint8_t *hdr;       /* HDR1                        */
    int       ninstr, ntrack;
    const uint8_t *instr[16];
    const uint8_t *trackstart[NTRACK];
} mus_song;

static struct {
    int  inited;
    int  rate;
    int  enabled;
    int  master_vol;          /* byte_45950 */
    mus_song song[MUSIC_SONG_COUNT];
    int  cur;                 /* -1 = nothing loaded */

    uint8_t *vce;             /* AD*.VCE bank */
    long     vcelen;
    /* audio_map_song_instruments' seven drum-kit resources */
    const uint8_t *basd, *snar, *tomm, *ride, *crsh, *chht, *ohht;

    mus_track track[NTRACK];
    mus_voice voice[NVOICE];
    int  nvoice;              /* byte_459D2 - what the driver reports */

    /* sub_3868A's accumulator and divisor */
    uint16_t accum;           /* word_44D48 */
    uint16_t divisor;         /* word_454BA */
    uint8_t  ntracks_playing; /* byte_44290 */
    int      playing;

    /* fractional 100 Hz timer, driven from the sample clock */
    double   tick_acc;

    /* verification hooks */
    FILE    *opl_log;
} M;

/* ================================================================== */
/* THE ADLIB DRIVER LAYER                                             */
/* ================================================================== */
/*
 * AD15.DRV is 3571 bytes of raw x86 that neither disassembly covers, and
 * every OPL register write in the original lives inside it. seg027/seg028
 * reach it through a table of 21 three-byte `jmp near` stubs at the start
 * of the blob; the sequencer calls into it at offsets
 *
 *   +0x00 init (returns the voice count in AL)   +0x21 program a voice
 *   +0x03 shutdown, part 2                       +0x24 set raw frequency
 *   +0x06 shutdown, part 1                       +0x27 per-tick update
 *   +0x09 note on                                +0x30 end-of-tick flush
 *   +0x0C begin release                          +0x39 MT-32 text (unused)
 *   +0x0F voice off                              +0x3F MT-32 sysex
 *   +0x12 channel volume                         +0x42 MT-32 patch bank
 *   +0x15 controller
 *   +0x18 reset all
 *   +0x1B pitch bend
 *   +0x1E free one voice
 *
 * See docs/FAITHFUL_RENDERER.md, Phase 8, for how far the reverse
 * engineering of the blob got and what is still guessed.
 */
#include "music_ad15.inc.c"

/* ================================================================== */
/* SEQUENCER                                                          */
/* ================================================================== */

static void seq_note_off_voice(mus_voice *v);

/* audio_driver_func1E, seg028.asm 2403..2589. Free every voice owned by a
 * track in [lo,hi] and clear those tracks' voice counts. */
static void seq_free_voices(int lo, int hi)
{
    int i;
    for (i = 0; i < M.nvoice && i < NVOICE; i++) {
        mus_voice *v = &M.voice[i];
        if (v->track > hi || v->track < lo) continue;
        drv_free_voice(i);           /* driver +0x1E */
        v->state = 0;
        v->instr = NULL;             /* dseg 0xA2C6 = voice+0x10 */
        v->track = 0xFF;
        v->prio  = 0;                /* dseg 0xA2B8 = voice+0x02 */
    }
    for (i = lo; i <= hi && i < NTRACK; i++) M.track[i].nvoices = 0;
}

/* sub_38178, seg027.asm 1229..1284: the state every voice is left in when
 * the driver is loaded. */
static void seq_reset_voices(void)
{
    int i;
    memset(M.voice, 0, sizeof M.voice);
    for (i = 0; i < NVOICE; i++) {
        M.voice[i].track = 0xFF;
        M.voice[i].trk   = 0xFF;
        M.voice[i].chan  = 0xFF;
    }
}

/* audio_init_chunk, seg027.asm 1867..1968. `first` is the offset within
 * HDR1 of the per-track table; pass NULL for `hdr` to just reset. */
static void seq_init_tracks(int lo, int hi, const mus_song *s,
                            uint8_t volume, uint8_t prio)
{
    int i;
    for (i = lo; i <= hi && i < NTRACK; i++) {
        mus_track *t = &M.track[i];
        /* audio_init_chunk only writes +0x2E (the instrument table) inside
         * the "we were given a chunk" branch, so a re-init with no chunk -
         * which is what the 0xDA STOP opcode does - keeps the old table. */
        const uint8_t *const *keep_tab = t->instrtab;
        uint8_t keep_n = t->ninstr;
        memset(t, 0, sizeof *t);
        t->instrtab = keep_tab;
        t->ninstr   = keep_n;
        t->defvel  = 0x7F;
        t->trackno = (uint8_t)i;
        t->f16     = 0x0F;
        t->prio    = prio;
        t->volume  = volume;
        t->chan    = 0xFF;
        t->ptr     = NULL;
        if (s && i < s->ntrack && s->trackstart[i]) {
            /* the original reads the same dword twice and stores it into
             * both the stack base (+5) and the current pointer (+0), each
             * time with +4 to skip the chunk's length dword. */
            t->callstack[0] = s->trackstart[i] + 4;
            t->ptr          = s->trackstart[i] + 4;
            t->instrtab     = s->instr;
            t->ninstr       = (uint8_t)s->ninstr;
        }
    }
}

/* sub_38AC4, seg028.asm 672..698 */
static const uint8_t *seq_instrument(const mus_track *t, int idx)
{
    if (!t->instrtab || idx < 0 || idx >= t->ninstr) return NULL;
    return t->instrtab[idx];
}

/* off_38E7E, seg028.asm 1250..1267: which drum resource a note plays when
 * the track's instrument has type 5. Index = note - 0x18, clamped. */
static const uint8_t *seq_drumkit(int note)
{
    int k = note - 0x18;
    if (k < 0 || k > 0x0F) return M.tomm;
    switch (k) {
    case 0:  return M.basd;   /* 24 kick            */
    case 2:  return M.snar;   /* 26 snare           */
    case 6:  return M.chht;   /* 30 closed hi-hat   */
    case 8:                   /* 32 open hi-hat     */
    case 10: return M.ohht;   /* 34 open hi-hat     */
    case 13: return M.ride;   /* 37 ride            */
    case 15: return M.crsh;   /* 39 crash           */
    default: return M.tomm;
    }
}

/* loc_390C8, seg028.asm 1435..1806 (the AdLib half, loc_391F0 onwards).
 * Returns a voice index or -1.
 *
 * The original's rules, in order:
 *   - a track may hold at most track->f16 voices at once; over that, the
 *     search runs the "steal from myself" variant at loc_39344,
 *   - only voices whose bit is set in the instrument's channel mask
 *     (record +0x0C, tested against 1<<voice) are eligible,
 *   - a completely free voice (state 0) wins immediately,
 *   - otherwise the OLDEST releasing voice (state 2) is stolen, else the
 *     oldest sounding voice (state 1) whose priority is <= the track's,
 *   - stealing a voice from another track moves the voice count between
 *     the two tracks.
 */
static int seq_alloc_voice(const uint8_t *instr, mus_track *t)
{
    int i;
    int best1 = -1, best2 = -1;
    uint32_t age1 = 0, age2 = 0;
    uint16_t mask;
    int selfsteal;

    if (!instr) return -1;
    mask = rd16(instr + IN_CHANMASK);
    if (mask == 0) return -1;             /* loc_390F7 */

    selfsteal = (t->nvoices >= t->f16);

    for (i = 0; i < M.nvoice && i < NVOICE; i++) {
        mus_voice *v = &M.voice[i];
        if (!(mask & (uint16_t)(1u << i))) continue;
        if (selfsteal) {
            /* loc_39200: at the track's voice limit, only its own voices
             * are candidates - and the ownership test comes BEFORE the
             * free test, so a free voice belonging to someone else is not
             * taken. */
            if (v->track != t->trackno) continue;
        }
        if (v->state == 0) { t->nvoices++; return i; }
        if (v->prio > t->prio) continue;   /* loc_39254/loc_3938E */
        if (v->state == 1 && v->age > age1) { age1 = v->age; best1 = i; }
        if (v->state == 2 && v->age > age2) { age2 = v->age; best2 = i; }
    }

    i = (best2 >= 0) ? best2 : best1;
    if (i < 0) return -1;
    {
        mus_voice *v = &M.voice[i];
        /* loc_39401/loc_39428: the voice's owner loses a voice and we gain
         * one, unless it was already ours. */
        if (v->trk != t->trackno) {
            if (v->trk < NTRACK && M.track[v->trk].nvoices)
                M.track[v->trk].nvoices--;
            t->nvoices++;
        }
        drv_begin_release(v->chan, v);     /* driver +0x0C */
        drv_voice_off(v->chan, v);         /* driver +0x0F */
    }
    return i;
}

/* sub_38DE6, seg028.asm 1073..1345 */
static int seq_note_on(const mus_event *ev, int ti)
{
    mus_track *t = &M.track[ti];
    const uint8_t *instr = t->instr;
    mus_voice *v;
    int vi, note;

    if (instr && instr[IN_TYPE] == 5) instr = seq_drumkit(ev->status);
    if (!instr) return -1;

    vi = seq_alloc_voice(instr, t);
    if (vi < 0) return -1;
    v = &M.voice[vi];

    if (v->instr != instr) {
        v->instr = instr;
        drv_program_voice(vi, v, t, instr);   /* driver +0x21 */
    }
    v->track  = (uint8_t)ti;
    v->trk    = (uint8_t)ti;
    v->state  = 1;
    v->stage  = 1;
    v->level  = (int16_t)rd16(instr + IN_LEVEL0);
    v->prio   = t->prio;
    v->age    = 0;
    v->dur    = ev->d2 - 1;
    v->vibdelay = rd16(instr + IN_VIBDELAY);
    v->vibtime  = rd16(instr + IN_VIBTIME);
    v->vibstep  = rd16(instr + IN_VIBSTEP);
    v->vibval   = 0;
    v->vibdir   = instr[IN_VIBFLAGS];
    v->vibcnt   = 0;
    v->arpdelay = rd16(instr + IN_ARPDELAY);
    v->arptime  = rd16(instr + IN_ARPTIME);
    v->arpoff   = 0;
    v->arpcnt   = 0;
    v->arpidx   = 0;
    v->chan     = (uint8_t)vi;             /* AdLib: channel == voice */

    if (ev->status == 0xFF)                /* the sound-effect path only */
        drv_set_frequency(v->chan, v, (uint16_t)ev->delta);

    note = (int8_t)ev->status + (int8_t)instr[IN_TRANSP];
    drv_note_on(v->chan, v, t, note, ev->d1, instr);   /* driver +0x09 */
    return vi;
}

/* sub_3968A, seg028.asm 2081..2140: age one voice and expire its duration */
static void seq_age_voice(mus_voice *v)
{
    v->age++;
    if (v->dur != 0) { v->dur--; return; }
    drv_begin_release(v->chan, v);          /* driver +0x0C */
    v->state = 2;
    /* `mov al,4Ch; mul byte ptr [bx]` - the OWNER byte at voice+0x00, not
     * the track pointer at voice+0x2A. */
    v->stage = (v->track < NTRACK && M.track[v->track].sustain) ? 3 : 4;
}

/* sub_3963C, seg028.asm 2035..2080: age the music voices. Called once per
 * SEQUENCER tick, so note durations are in sequencer ticks. */
static void seq_age_music_voices(void)
{
    int i;
    for (i = 0; i < M.nvoice && i < NVOICE; i++) {
        mus_voice *v = &M.voice[i];
        if (v->state == 0) continue;
        if (v->track >= 0x10) continue;
        seq_age_voice(v);
    }
}

static void seq_note_off_voice(mus_voice *v)
{
    v->level = 0;
    v->stage = 0;
    v->state = 0;
    if (v->track < NTRACK && M.track[v->track].nvoices)
        M.track[v->track].nvoices--;
    drv_voice_off(v->chan, v);              /* driver +0x0F */
}

/* sub_39700, seg028.asm 2141..2402. Runs once per 100 Hz TIMER tick over
 * every voice: software ADSR, vibrato and the 8-step arpeggio table. */
static void seq_voice_update(void)
{
    int i;
    for (i = 0; i < M.nvoice && i < NVOICE; i++) {
        mus_voice *v = &M.voice[i];
        const uint8_t *in;
        if (v->state == 0) continue;
        if (v->track > 0x0F) seq_age_voice(v);   /* sfx voices age here */
        in = v->instr;
        if (!in) continue;

        if (v->stage == 1) {                              /* attack   */
            v->level = (int16_t)(v->level + (int16_t)rd16(in + IN_ATKRATE));
            if (v->level >= (int16_t)rd16(in + IN_ATKPEAK)) {
                v->level = (int16_t)rd16(in + IN_ATKPEAK);
                v->stage = ((int16_t)rd16(in + IN_SUSLEVEL) <
                            (int16_t)rd16(in + IN_ATKPEAK)) ? 2 : 3;
            }
        }
        if (v->stage == 2) {                              /* decay    */
            v->level = (int16_t)(v->level - (int16_t)rd16(in + IN_DECRATE));
            if (v->level <= (int16_t)rd16(in + IN_SUSLEVEL)) {
                v->stage = 3;
                v->level = (int16_t)rd16(in + IN_SUSLEVEL);
            }
        }
        if (v->stage == 3 && rd16(in + IN_SUSLEVEL) == 0) v->stage = 4;
        if (v->stage == 4) {                              /* release  */
            v->level = (int16_t)(v->level - (int16_t)rd16(in + IN_RELRATE));
            if (v->level <= 0) { seq_note_off_voice(v); continue; }
        }

        if (in[IN_VIBON]) {                               /* vibrato  */
            if (v->vibdelay) v->vibdelay--;
            else if (v->vibtime) {
                if (v->vibtime != 0x7FFF) v->vibtime--;
                if (v->vibcnt) v->vibcnt--;
                else {
                    int16_t depth = (int16_t)rd16(in + IN_VIBDEPTH);
                    int16_t nv;
                    v->vibcnt = in[IN_VIBRELOAD];
                    if (v->vibdir == 2) {
                        v->vibval = (int16_t)(v->vibval - (int16_t)v->vibstep);
                        nv = (int16_t)(v->vibval < 0 ? -v->vibval : v->vibval);
                        if (depth <= nv) {
                            if (in[IN_VIBFLAGS] & 1) v->vibdir = 1;
                            else                     v->vibval = 0;
                        }
                    } else {
                        v->vibval = (int16_t)(v->vibval + (int16_t)v->vibstep);
                        nv = (int16_t)(v->vibval < 0 ? -v->vibval : v->vibval);
                        if (depth <= nv) {
                            if (in[IN_VIBFLAGS] & 2) v->vibdir = 2;
                            else                     v->vibval = 0;
                        }
                    }
                }
            }
        }

        if (in[IN_ARPON]) {                               /* arpeggio */
            if (v->arpdelay) v->arpdelay--;
            else if (v->arptime) {
                v->arptime--;
                if (v->arpcnt) v->arpcnt--;
                else {
                    v->arpcnt = in[IN_ARPRELOAD];
                    v->arpoff = in[IN_ARPTABLE + (v->arpidx & 7)];
                    v->arpidx++;
                }
            }
        }

        drv_voice_tick(v->chan, v,
                       v->trk < NTRACK ? &M.track[v->trk] : NULL, in);
    }
    drv_flush();                                          /* driver +0x30 */
}

/* audio_unk2, seg028.asm 871..959: 0xDE, channel volume */
static void seq_set_volume(int ti, uint8_t vol)
{
    int i;
    M.track[ti].volume = vol;
    for (i = 0; i < M.nvoice && i < NVOICE; i++)
        if (M.voice[i].track == ti) drv_set_volume(i, &M.voice[i], vol);
}

/* sub_38AEA, seg028.asm 699..817: 0xDF, controller */
static void seq_controller(int ti, uint8_t ctrl, uint8_t val)
{
    int i;
    mus_track *t = &M.track[ti];
    if (ctrl == 0x40) t->sustain = val;
    for (i = 0; i < M.nvoice && i < NVOICE; i++) {
        mus_voice *v = &M.voice[i];
        if (v->track == ti) drv_controller(i, v, ctrl, val);
        /* loc_38B9D: releasing the sustain pedal moves this track's
         * already-released voices into release. Note the original tests
         * only the voice state, never the envelope stage, and it runs
         * outside the ownership branch above. */
        if (ctrl == 0x40 && val == 0 && v->state == 2 && v->track == ti)
            v->stage = 4;
    }
}

/* sub_38BEA, seg028.asm 818..870: 0xE5, 14-bit pitch bend.
 *   if (w & 0x0100) w |= 0x0080;
 *   centred = (int8)(w & 0xFF) + ((w & 0xFF00) >> 1) - 0x2000;
 * i.e. the two 7-bit halves are packed into a 14-bit signed value. */
static void seq_pitch_bend(int ti, uint16_t w)
{
    int16_t centred;
    if (w & 0x0100) w |= 0x0080;
    centred = (int16_t)((int16_t)(int8_t)(w & 0xFF)
                        + (int16_t)((w & 0xFF00) >> 1) - 0x2000);
    M.track[ti].bend = (uint16_t)centred;
    drv_pitch_bend(M.track[ti].chan, &M.track[ti], centred);
}

/* sub_38702, seg028.asm 216..671. One track, one sequencer tick. */
static void seq_track_tick(int ti)
{
    mus_track *t = &M.track[ti];
    mus_event ev, peek;

    if (t->delay != 0) { t->delay--; return; }
    if (t->ptr == NULL) return;

    for (;;) {
        /* loc_38737 */
        if (t->delay != 0) { t->delay--; return; }
        if (t->ptr == NULL) { t->delay--; return; }  /* [ODDITY], see below */

        parse_event(&ev, t->ptr);
        t->ptr += ev.len;

        if (ev.status >= 0xD9) {
            switch (ev.status) {
            case 0xD9:                                   /* RETURN     */
                if (t->callsp != 0) {
                    t->ptr = t->callstack[t->callsp & (NCALL - 1)];
                    t->callsp--;
                } else {
                    t->ptr = NULL;
                    seq_free_voices(ti, ti);
                }
                break;
            case 0xDA:                                   /* STOP       */
                t->ptr = NULL;
                seq_free_voices(ti, ti);
                seq_init_tracks(ti, ti, NULL, M.master_vol, 0);
                break;
            case 0xDB:                                   /* RESTART    */
                t->callsp = 0;
                t->loopsp = 0;
                t->ptr = t->callstack[0];
                break;
            case 0xDC:                                   /* PATCH      */
                t->instr = seq_instrument(t, ev.d1);
                break;
            case 0xDD:                                   /* TEMPO      */
                if (ti < 0x10 && ev.d1)
                    M.divisor = (uint16_t)(0x7D00 / ev.d1);
                break;
            case 0xDE:  seq_set_volume(ti, ev.d1); break;/* VOLUME     */
            case 0xDF:  seq_controller(ti, ev.d1, (uint8_t)ev.d2); break;
            case 0xE0:  t->f16 = ev.d1; break;
            case 0xE1:  t->prio = ev.d1; break;
            case 0xE2:                                   /* LOOP start */
                if (t->loopsp < NLOOP) {
                    t->loopstack[t->loopsp] = t->ptr;
                    t->loopcnt[t->loopsp]   = (uint8_t)(ev.d1 - 1);
                    t->loopsp++;
                }
                break;
            case 0xE3:                                   /* LOOP end   */
                if (t->loopsp != 0) {
                    int d = t->loopsp - 1;
                    uint8_t c = t->loopcnt[d];
                    t->ptr = t->loopstack[d];
                    t->loopcnt[d]--;
                    if (c == 0) t->loopsp--;
                }
                break;
            case 0xE4:  t->defvel = ev.d1; break;
            case 0xE5:  seq_pitch_bend(ti, (uint16_t)ev.d2); break;
            case 0xE6: {                                 /* CALL       */
                char nm[5];
                const uint8_t *tgt;
                nm[0] = (char)( ev.d2        & 0xFF);
                nm[1] = (char)((ev.d2 >>  8) & 0xFF);
                nm[2] = (char)((ev.d2 >> 16) & 0xFF);
                nm[3] = (char)((ev.d2 >> 24) & 0xFF);
                nm[4] = 0;
                tgt = M.cur >= 0 ? res_find(M.song[M.cur].top, nm) : NULL;
                if (tgt) {
                    if (t->callsp + 1 < NCALL) {
                        t->callsp++;
                        t->callstack[t->callsp] = t->ptr;
                    }
                    t->ptr = tgt + 4;
                } else {
                    /* [DEVIATION] the original would jump through an
                     * unrelocated 4-char name and crash; we stop instead. */
                    t->ptr = NULL;
                }
                break;
            }
            case 0xE7: break;                            /* COMMENT    */
            case 0xE8: break;                            /* TEXT (MT32)*/
            case 0xE9:  t->chan = ev.d1; break;
            case 0xEA:  break;                           /* per-track byte */
            default:    break;                           /* 0xEB..0xFF */
            }
        } else {
            /* loc_38A44: statuses below 0x80 take the running velocity */
            if (ev.status < 0x80) ev.d1 = t->defvel;
            ev.status &= 0x7F;
            seq_note_on(&ev, ti);
        }

        /* loc_38A67: peek the next event's delta and sleep for it */
        if (t->ptr == NULL) continue;
        parse_event(&peek, t->ptr);
        t->delay = peek.delta;
    }
}
/* [ODDITY] loc_38742 - if a track's pointer is NULL and its delay is
 * already zero, the original still executes loc_38A98, which decrements
 * the 32-bit delay and so underflows it to 0xFFFFFFFF. The track is idle
 * either way, so the wrap is harmless; it is reproduced above. */

/* sub_3868A, seg028.asm 127..176: one 100 Hz timer tick's worth of music */
static void seq_timer_tick(void)
{
    int i;
    M.accum = (uint16_t)(M.accum + 0x80);
    while (M.accum >= M.divisor) {
        seq_age_music_voices();
        M.accum = (uint16_t)(M.accum - M.divisor);
        for (i = 0; i < M.ntracks_playing && i < NTRACK; i++)
            seq_track_tick(i);
    }
}

/* audiodriver_timer, seg028.asm 71..126. sub_386D6 (the sound-effect
 * tracks 0x10..0x16) is deliberately not run - see the header. */
static void seq_driver_timer(void)
{
    seq_voice_update();                    /* sub_39700 */
    if (M.playing && M.enabled) seq_timer_tick();
    else                        seq_age_music_voices();
}

/* ------------------------------------------------------------------ */
/* loading                                                            */
/* ------------------------------------------------------------------ */

static uint8_t *load_file(const char *dir, const char *name, long *len)
{
    char path[1024];
    FILE *f;
    uint8_t *buf;
    long n;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) {
        /* the original's file layer is case-insensitive because DOS was */
        size_t i;
        char lower[1024];
        snprintf(lower, sizeof lower, "%s/%s", dir, name);
        for (i = strlen(dir) + 1; i < strlen(lower); i++)
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += 32;
        f = fopen(lower, "rb");
    }
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)n + 16);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    memset(buf + n, 0, 16);
    fclose(f);
    *len = n;
    return buf;
}

/* init_audio_resources + audio_map_song_instruments + audio_map_song_tracks,
 * collapsed: resolve the HDR1 name tables into pointers. */
static int song_link(mus_song *s)
{
    int i;
    const uint8_t *hdr;
    const char *topname;
    char nm[5];

    if (!s->kms || s->kmslen < 16) return -1;
    if (rd16(s->kms + 4) < 1) return -1;
    memcpy(nm, s->kms + 6, 4); nm[4] = 0;
    topname = nm;
    s->top = res_find(s->kms, topname);
    if (!s->top) return -1;
    hdr = res_find(s->top, "hdr1");
    if (!hdr) return -1;
    s->hdr = hdr;

    s->ninstr = hdr[6];
    if (s->ninstr > 16) s->ninstr = 16;
    for (i = 0; i < s->ninstr; i++) {
        char in[5];
        memcpy(in, hdr + 7 + 4 * i, 4); in[4] = 0;
        s->instr[i] = M.vce ? res_find(M.vce, in) : NULL;
    }
    s->ntrack = hdr[7 + 4 * hdr[6]];
    if (s->ntrack > NTRACK) s->ntrack = NTRACK;
    for (i = 0; i < s->ntrack; i++) {
        char tn[5];
        memcpy(tn, hdr + 8 + 4 * hdr[6] + 5 * i, 4); tn[4] = 0;
        s->trackstart[i] = res_find(s->top, tn);
    }
    return 0;
}

static const char *song_file[MUSIC_SONG_COUNT] = {
    "SKIDTITL.KMS", "SKIDSLCT.KMS", "SKIDOVER.KMS", "SKIDVICT.KMS"
};

int music_native_init(const char *data_dir, int sample_rate)
{
    int i;
    if (M.inited) return 0;
    memset(&M, 0, sizeof M);
    M.rate       = sample_rate > 0 ? sample_rate : 44100;
    M.enabled    = 1;
    M.master_vol = 0x7F;
    M.cur        = -1;
    M.divisor    = 0x80;

    M.vce = load_file(data_dir, "ADSKIDMS.VCE", &M.vcelen);
    if (!M.vce) return -1;
    M.basd = res_find(M.vce, "BASD");
    M.snar = res_find(M.vce, "SNAR");
    M.tomm = res_find(M.vce, "TOMM");
    M.ride = res_find(M.vce, "RIDE");
    M.crsh = res_find(M.vce, "CRSH");
    M.chht = res_find(M.vce, "CHHT");
    M.ohht = res_find(M.vce, "OHHT");

    for (i = 0; i < MUSIC_SONG_COUNT; i++) {
        M.song[i].kms = load_file(data_dir, song_file[i], &M.song[i].kmslen);
        if (M.song[i].kms && song_link(&M.song[i]) != 0) {
            free(M.song[i].kms);
            M.song[i].kms = NULL;
        }
    }

    opl_reset(M.rate);
    M.nvoice = drv_init();
    if (M.nvoice > NVOICE) M.nvoice = NVOICE;
    seq_reset_voices();
    M.inited = 1;
    return 0;
}

void music_native_shutdown(void)
{
    int i;
    if (!M.inited) return;
    music_native_stop();
    drv_shutdown();
    for (i = 0; i < MUSIC_SONG_COUNT; i++) free(M.song[i].kms);
    free(M.vce);
    memset(&M, 0, sizeof M);
}

void music_native_stop(void)
{
    if (!M.inited) return;
    M.playing = 0;
    M.ntracks_playing = 0;
    seq_free_voices(0, NTRACK - 1);
    drv_reset_all();
    memset(M.track, 0, sizeof M.track);
    seq_reset_voices();
    M.cur = -1;
}

/* load_audio_finalize, seg027.asm 229..320 */
void music_native_play(music_song_t song)
{
    mus_song *s;
    if (!M.inited || !M.enabled) return;
    if (song < 0 || song >= MUSIC_SONG_COUNT) return;
    s = &M.song[song];
    if (!s->kms) return;

    music_native_stop();
    M.cur     = (int)song;
    M.accum   = 0;
    M.divisor = 0x80;
    M.tick_acc = 0.0;
    M.ntracks_playing = (uint8_t)s->ntrack;
    seq_init_tracks(0, s->ntrack - 1, s, (uint8_t)M.master_vol, 0x20);
    M.playing = 1;
}

int music_native_playing(void)
{
    int i;
    if (!M.inited || !M.playing) return 0;
    for (i = 0; i < M.ntracks_playing && i < NTRACK; i++)
        if (M.track[i].ptr) return 1;
    return 0;
}

void music_native_set_enabled(int on)
{
    if (!M.inited) { return; }
    M.enabled = on ? 1 : 0;
    if (!on) music_native_stop();
}

int music_native_enabled(void) { return M.inited ? M.enabled : 0; }

void music_native_set_volume(int v)
{
    if (v < 0) v = 0; if (v > 127) v = 127;
    M.master_vol = v;
}

/* ------------------------------------------------------------------ */
/* mixing                                                             */
/* ------------------------------------------------------------------ */

#define MIXCHUNK 256

void music_native_mix(int16_t *out, int frames, int channels)
{
    int16_t tmp[MIXCHUNK];
    double  per_tick;
    if (!M.inited || !M.enabled || !M.playing) return;
    per_tick = (double)M.rate / 100.0;      /* samples per 100 Hz tick */

    while (frames > 0) {
        int n = frames > MIXCHUNK ? MIXCHUNK : frames;
        int i;
        /* run every 100 Hz tick that falls inside this block first, then
         * render the block. Sub-tick timing of register writes is not
         * modelled; at 100 Hz that is at most a 10 ms quantisation, and
         * the original wrote its registers from the same 100 Hz ISR. */
        M.tick_acc += n;
        while (M.tick_acc >= per_tick) {
            M.tick_acc -= per_tick;
            seq_driver_timer();
        }
        opl_render(tmp, n);
        if (channels == 2) {
            for (i = 0; i < n; i++) {
                int a = out[2*i] + tmp[i];
                int b = out[2*i+1] + tmp[i];
                out[2*i]   = (int16_t)(a >  32767 ?  32767 : a < -32768 ? -32768 : a);
                out[2*i+1] = (int16_t)(b >  32767 ?  32767 : b < -32768 ? -32768 : b);
            }
            out += 2 * n;
        } else {
            for (i = 0; i < n; i++) {
                int a = out[i] + tmp[i];
                out[i] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
            }
            out += n;
        }
        frames -= n;
    }
}

/* ------------------------------------------------------------------ */
/* verification                                                       */
/* ------------------------------------------------------------------ */

static const char *opcode_name(uint8_t st)
{
    switch (st) {
    case 0xD9: return "RETURN";     case 0xDA: return "STOP";
    case 0xDB: return "RESTART";    case 0xDC: return "PATCH";
    case 0xDD: return "TEMPO";      case 0xDE: return "VOLUME";
    case 0xDF: return "CTRL";       case 0xE0: return "SETF16";
    case 0xE1: return "PRIO";       case 0xE2: return "LOOPSTART";
    case 0xE3: return "LOOPEND";    case 0xE4: return "DEFVEL";
    case 0xE5: return "BEND";       case 0xE6: return "CALL";
    case 0xE7: return "COMMENT";    case 0xE8: return "TEXT";
    case 0xE9: return "SETCHAN";    case 0xEA: return "SETVAR";
    default:   return "?";
    }
}

int music_native_dump_events(music_song_t song, FILE *out)
{
    mus_song *s;
    int ti, tempo = 0;
    if (!M.inited) return -1;
    if (song < 0 || song >= MUSIC_SONG_COUNT) return -1;
    s = &M.song[song];
    if (!s->kms) return -1;

    fprintf(out, "=== %s  (%ld bytes) ===\n", song_file[song], s->kmslen);
    fprintf(out, "container: %u chunk(s); top chunk length %u, %u sub-chunks\n",
            rd16(s->kms + 4), rd32(s->top), rd16(s->top + 4));
    fprintf(out, "hdr1: length %u  ninstr %d  ntracks %d\n",
            rd32(s->hdr), s->ninstr, s->ntrack);
    fprintf(out, "instruments:");
    for (ti = 0; ti < s->ninstr; ti++)
        fprintf(out, " %.4s%s", (const char *)(s->hdr + 7 + 4 * ti),
                s->instr[ti] ? "" : "(MISSING)");
    fprintf(out, "\n");

    for (ti = 0; ti < s->ntrack; ti++) {
        const uint8_t *p, *end;
        uint32_t tick = 0;
        int nev = 0, nnote = 0;
        int minnote = 999, maxnote = -1;
        uint32_t mindur = 0xFFFFFFFFu, maxdur = 0;
        uint64_t sumdelta = 0;
        if (!s->trackstart[ti]) { fprintf(out, "\ntrack %d: MISSING\n", ti); continue; }
        p   = s->trackstart[ti] + 4;
        end = s->trackstart[ti] + rd32(s->trackstart[ti]);
        fprintf(out, "\n--- track %d '%.4s'  chunk length %u ---\n", ti,
                (const char *)(s->hdr + 8 + 4 * s->ninstr + 5 * ti),
                rd32(s->trackstart[ti]));
        while (p < end) {
            mus_event ev;
            parse_event(&ev, p);
            p += ev.len;
            tick += ev.delta;
            sumdelta += ev.delta;
            nev++;
            if (ev.status < 0xD9) {
                int note = ev.status & 0x7F;
                nnote++;
                if (note < minnote) minnote = note;
                if (note > maxnote) maxnote = note;
                if (ev.d2 < mindur) mindur = ev.d2;
                if (ev.d2 > maxdur) maxdur = ev.d2;
                fprintf(out, "  t=%-6u +%-5u %02X note %3d vel %3d dur %u\n",
                        tick, ev.delta, ev.status, note,
                        ev.status > 0x80 ? ev.d1 : -1, ev.d2);
            } else {
                fprintf(out, "  t=%-6u +%-5u %02X %-9s", tick, ev.delta,
                        ev.status, opcode_name(ev.status));
                if (ev.status == 0xDD) { tempo = ev.d1;
                    fprintf(out, " %d BPM -> divisor %d, %.1f ticks/s",
                            ev.d1, 0x7D00 / (ev.d1 ? ev.d1 : 1), 0.4 * ev.d1); }
                else if (ev.status == 0xE7 || ev.status == 0xE8)
                    fprintf(out, " \"%.*s\"", (int)ev.textlen, (const char *)ev.text);
                else if (ev.status == 0xE6)
                    fprintf(out, " -> '%.4s'", (const char *)&ev.d2);
                else {
                    if (opcode_args[ev.status - 0xD9] != PARG_NONE)
                        fprintf(out, " d1=%d d2=%u", ev.d1, ev.d2);
                }
                fprintf(out, "\n");
            }
        }
        fprintf(out, "  [%d events, %d notes, total %u ticks", nev, nnote, tick);
        if (nnote) fprintf(out, ", notes %d..%d, durations %u..%u",
                           minnote, maxnote, mindur, maxdur);
        fprintf(out, "]\n");
        if (p != end)
            fprintf(out, "  !! MISALIGNED: stopped at +%ld, chunk ends at +%ld\n",
                    (long)(p - s->trackstart[ti]),
                    (long)(end - s->trackstart[ti]));
        else
            fprintf(out, "  stream ends exactly on the chunk boundary\n");
        (void)sumdelta;
    }
    if (tempo)
        fprintf(out, "\ntempo %d BPM = %.1f sequencer ticks/s (24 PPQN);"
                     " longest track is %.2f s of music\n",
                tempo, 0.4 * tempo, 0.0);
    return 0;
}

static void opl_log_hook(uint8_t reg, uint8_t val, void *ud)
{
    FILE *f = (FILE *)ud;
    fprintf(f, "    OPL %02X <- %02X\n", reg, val);
}

int music_native_dump_opl(music_song_t song, double seconds, FILE *out)
{
    int ticks, i;
    if (!M.inited) return -1;
    music_native_play(song);
    if (!M.playing) return -1;
    ticks = (int)(seconds * 100.0);
    opl_write_hook = opl_log_hook;
    opl_write_hook_ud = out;
    for (i = 0; i < ticks; i++) {
        fprintf(out, "tick %d (t=%.2fs)\n", i, i / 100.0);
        seq_driver_timer();
    }
    opl_write_hook = NULL;
    opl_write_hook_ud = NULL;
    music_native_stop();
    return 0;
}

static void wr32le(FILE *f, uint32_t v)
{ fputc(v & 255, f); fputc((v>>8)&255, f); fputc((v>>16)&255, f); fputc((v>>24)&255, f); }
static void wr16le(FILE *f, uint16_t v)
{ fputc(v & 255, f); fputc((v>>8)&255, f); }

int music_native_render_wav(music_song_t song, double seconds,
                            const char *wav_path)
{
    FILE *f;
    long total, done = 0;
    int16_t buf[1024];
    if (!M.inited) return -1;
    music_native_play(song);
    if (!M.playing) return -1;

    f = fopen(wav_path, "wb");
    if (!f) return -1;
    total = (long)(seconds * M.rate);
    fwrite("RIFF", 1, 4, f); wr32le(f, (uint32_t)(36 + total * 2));
    fwrite("WAVEfmt ", 1, 8, f); wr32le(f, 16);
    wr16le(f, 1); wr16le(f, 1);
    wr32le(f, (uint32_t)M.rate); wr32le(f, (uint32_t)(M.rate * 2));
    wr16le(f, 2); wr16le(f, 16);
    fwrite("data", 1, 4, f); wr32le(f, (uint32_t)(total * 2));

    while (done < total) {
        long n = total - done;
        if (n > 1024) n = 1024;
        memset(buf, 0, sizeof(int16_t) * (size_t)n);
        music_native_mix(buf, (int)n, 1);
        fwrite(buf, sizeof(int16_t), (size_t)n, f);
        done += n;
    }
    fclose(f);
    music_native_stop();
    return 0;
}

const char *music_native_opl_core(void) { return opl_core_name(); }
int         music_native_opl_is_real(void) { return opl_core_is_real(); }
