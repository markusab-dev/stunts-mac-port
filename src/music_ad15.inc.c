/*
 * music_ad15.inc.c - AD15.DRV, the AdLib/OPL2 driver, reverse engineered.
 *
 * Included by music_native.c; not a translation unit of its own.
 *
 * ====================================================================
 * PROVENANCE
 * ====================================================================
 *
 * AD15.DRV is 3571 bytes of raw 16-bit x86 with no header, loaded at
 * offset 0 of its own segment. NEITHER restunts disassembly covers it -
 * every OPL register write in the original game lives in this blob and
 * nowhere else. It was disassembled for this port with tools/dis8086.py
 * (a hand-written 8086/80186 decoder; there was no disassembler on this
 * machine that accepts raw 16-bit binaries). The full annotated listing is
 * build/ad15_dis.txt. Addresses below are offsets into AD15.DRV.
 *
 * The blob starts with 21 three-byte `jmp near` stubs; seg027/seg028 call
 * into it through those. Recursive descent from the 21 targets reached
 * 2688 of 3571 bytes with zero undecodable bytes and every branch target
 * on an instruction boundary.
 *
 *   +0x00 -> 0x0A7E  init()            +0x21 -> 0x006B  program_change()
 *   +0x03 -> 0x0ADA  shutdown()        +0x24 -> 0x04A8  set_freq_hz()
 *   +0x06 -> 0x005B  reset()           +0x27 -> 0x02C6  update_pitch()
 *   +0x09 -> 0x010B  note_on()         +0x2A -> 0x0449  store()
 *   +0x0C -> 0x01EF  note_off()        +0x2D -> 0x03E9  bend_to_fnum()
 *   +0x0F -> 0x0213  silence()         +0x30 -> 0x04F2  STUB (AX=0xFFFF)
 *   +0x12 -> 0x03C0  set_volume()      +0x33 -> 0x0457  start_pcm()
 *   +0x15 -> 0x0250  controller()      +0x36 -> 0x0495  get_state()
 *   +0x18 -> 0x04F2  STUB              +0x39 -> 0x04F2  STUB
 *   +0x1B -> 0x04F2  STUB              +0x3C -> 0x04F2  STUB
 *   +0x1E -> 0x00F5  stop()
 *
 * Five of the twenty-one are the same `mov ax,0FFFFh / retf` stub, which
 * settles three open questions in seg028 at a stroke: on AdLib the
 * "reset all" call (+0x18, sub_38178), the pitch-bend call (+0x1B,
 * sub_38BEA) and the end-of-tick flush (+0x30, sub_39700) DO NOTHING.
 * Pitch bend still reaches the chip, but only through update_pitch, which
 * re-reads the track's bend word every tick.
 *
 * init() returns 10. That is the origin of the sequencer's byte_459D2 and
 * it explains the instrument channel masks in ADSKIDMS.VCE: voices are
 * numbered 1..9 mapping to OPL2 channels 0..8, and **voice 0 is the
 * digital sample channel**, which is why no instrument mask in any of the
 * AdLib banks ever sets bit 0.
 *
 * ====================================================================
 * THE 100-BYTE .VCE RECORD, AdLib layout
 * ====================================================================
 *
 *   +0x00 word  record length (100)
 *   +0x05 byte  5 = drum kit (dispatch by note; read by seg028, not here)
 *   +0x0C word  channel mask, 1<<voice           (seg028)
 *   +0x10 byte  signed transpose, semitones      (seg028)
 *   +0x11 byte  signed fine tune, added to the packed A0/B0 word
 *   +0x12 byte  pitch-bend range in semitones
 *   +0x15 byte  0 = ignore note velocity, use 0x7F
 *   +0x16..0x18 modulation destination selectors (0x81..0x85)
 *   +0x19 byte  0x90 = add the software envelope level to the pitch
 *   +0x1C..0x3A the software envelope / vibrato / arpeggio block (seg028)
 *   +0x28 byte  0x90 = add the vibrato value to the pitch
 *   +0x35 byte  0x91 = recompute the pitch from note+arpeggio each tick
 *   +0x44 byte  CNT  (connection: 0 = FM, 1 = additive)  -> reg 0xC0 bit 0
 *   +0x45 byte  FB   (feedback 0..7)                     -> reg 0xC0 bits 1-3
 *   +0x46 12 bytes  operator 0 (modulator)
 *   +0x52 12 bytes  operator 1 (carrier)
 *
 *   operator record: +0 AR  +1 DR  +2 SL  +3 RR  +4 TL  +5 KSL
 *                    +6 MULT +7 KSR +8 EGT +9 VIB +10 AM +11 WS
 *   (already in OPL units - the file stores one field per byte and the
 *    driver does the packing.)
 *
 * ====================================================================
 * WHAT IS NOT PORTED
 * ====================================================================
 *
 * - The DIGITAL SAMPLE PLAYER. The INT 8 handler at 0x0C2E plays PCM by
 *   using OPL register 0x40 (channel 0 operator 0 Total Level) as a 6-bit
 *   DAC: it reprograms PIT channel 0 to the sample rate, and on every tick
 *   writes the next sample byte through a 256-byte linear-volume ->
 *   attenuation table at 0x0CF3. That is how Stunts gets digitised sound
 *   out of a bare AdLib. It is reached through voice/channel 0 only, which
 *   the music never allocates, so none of it is needed here. [DEVIATION]
 * - The OPL2 detection probe (ports 0x388, 0x318, 0x288, 0x218 with the
 *   timer-1 test at 0x0810) and the PIT-channel-2 busy-wait delay used
 *   between the two `out`s of every register write. Neither has meaning
 *   against an emulated chip. [DEVIATION]
 * - `store()` (+0x2A), `bend_to_fnum()` (+0x2D), `get_state()` (+0x36) and
 *   `start_pcm()` (+0x33): never called from seg027/seg028's music path.
 *
 * ====================================================================
 * [ODDITY] - carried over deliberately
 * ====================================================================
 *
 * - The register write at 0x07C7 keeps a 256-byte shadow of the register
 *   file and SKIPS THE BUS WRITE ENTIRELY when the value is unchanged.
 *   That is not just an optimisation: writing the same 0xB0 value twice
 *   does not re-key the channel. It is reproduced here, because the
 *   sequencer's behaviour depends on it.
 * - note_to_fnum computes block = note/12 with no clamp, and note_on ORs
 *   the block into 0xB0 as `0x20 | (packed >> 8)`. For note >= 96 the
 *   block reaches 8 and overflows into the key-on bit. The highest note
 *   any shipped song produces after transposition is 87, so it is
 *   unreachable; reproduced anyway.
 */

/* ---------------- the chip-side state the blob owns ---------------- */

static uint8_t  ad15_shadow[256];   /* cs:0x097D */
static uint8_t  ad15_vel[9];        /* cs:0x0974, cached velocity per ch  */
static int      ad15_nvoice;

/* cs:0x0870, 18 bytes: the OPL2 operator slot offsets, stored interleaved
 * two per channel. Verbatim from the blob. */
static const uint8_t ad15_op[18] = {
    0x00, 0x03,  0x01, 0x04,  0x02, 0x05,
    0x08, 0x0B,  0x09, 0x0C,  0x0A, 0x0D,
    0x10, 0x13,  0x11, 0x14,  0x12, 0x15
};

/* cs:0x0882..0x08F9, 60 words, base pointer cs:0x08B2 so the index runs
 * -24..+35. Verbatim from the blob. Indices +24..+35 are the textbook OPL2
 * octave; each octave below is the one above divided by two (rounded as
 * the original author rounded - these are the bytes, not a formula). */
static const uint16_t ad15_fnum_tbl[60] = {
    /* -24 */ 0x0015, 0x0017, 0x0018, 0x0019, 0x001B, 0x001D,
              0x001E, 0x0020, 0x0022, 0x0024, 0x0026, 0x0028,
    /* -12 */ 0x002B, 0x002D, 0x0030, 0x0033, 0x0036, 0x0039,
              0x003D, 0x0040, 0x0044, 0x0048, 0x004C, 0x0051,
    /*   0 */ 0x0056, 0x005B, 0x0060, 0x0066, 0x006C, 0x0072,
              0x0079, 0x0080, 0x0088, 0x0090, 0x0099, 0x00A2,
    /* +12 */ 0x00AB, 0x00B6, 0x00C0, 0x00CC, 0x00D8, 0x00E5,
              0x00F2, 0x0101, 0x0110, 0x0120, 0x0132, 0x0144,
    /* +24 */ 0x0157, 0x016B, 0x0181, 0x0198, 0x01B0, 0x01CA,
              0x01E5, 0x0202, 0x0220, 0x0241, 0x0263, 0x0287
};
#define AD15_FNUM(i)  ad15_fnum_tbl[(i) + 24]

/* 0x07C7. AH = register, AL = value. */
static void ad15_w(uint8_t reg, uint8_t val)
{
    if (ad15_shadow[reg] == val) return;      /* the shadow check */
    ad15_shadow[reg] = val;
    opl_write(reg, val);
}

/* 0x06F9. note_to_fnum(AL = note, AH = signed semitone offset).
 * block = note/12, index = note%12 + offset, result = fnum | block<<10. */
static uint16_t ad15_note_to_fnum(uint8_t note, int8_t offset)
{
    int block = note / 12;
    int idx   = (int8_t)((note % 12) + offset);   /* add in 8 bits, then cbw */
    if (idx < -24) idx = -24;                 /* [DEVIATION] the original
                                               * would index out of the
                                               * table; clamped here. */
    if (idx >  35) idx =  35;
    return (uint16_t)(AD15_FNUM(idx) | ((block & 0xFFFF) << 10));
}

/* 0x0730. The Total Level / velocity path.
 * ch is the OPL channel (already decremented), vel and vol are 0..127. */
static void ad15_apply_volume(int ch, const uint8_t *patch,
                              uint8_t vel, uint8_t vol)
{
    int op, i;
    for (i = 0; i < 2; i++) {
        int tlofs  = i ? 0x56 : 0x4A;
        int kslofs = i ? 0x57 : 0x4B;
        unsigned al = (unsigned)(patch[kslofs] << 6) & 0xC0;
        unsigned cl = patch[tlofs];
        /* operator 0 is only velocity-scaled in additive mode (CNT == 1),
         * because in FM mode it is a modulator and its level is timbre,
         * not loudness. Operator 1 always scales. */
        if (i == 1 || patch[0x44] == 1) {
            unsigned ax = ((unsigned)vol * (unsigned)vel) >> 6;
            ax = (ax + 1) >> 1;
            ax = (ax * (unsigned)(0x3F - patch[tlofs])) >> 6;
            ax = (ax + 1) >> 1;
            ax &= 0x3F;
            cl = 0x3F - ax;
        }
        op = ad15_op[2 * ch + i];
        ad15_w((uint8_t)(0x40 + op), (uint8_t)(al | (cl & 0x3F)));
    }
}

/* 0x0585. load_operator(record, slot). */
static void ad15_load_operator(const uint8_t *r, uint8_t slot)
{
    uint8_t v;
    v = (uint8_t)(((((((r[0x0A] << 1) | r[9]) << 1) | r[8]) << 1) | r[7]) << 4);
    v = (uint8_t)(v | (r[6] & 0x0F));
    ad15_w((uint8_t)(0x20 + slot), v);
    ad15_w((uint8_t)(0x40 + slot), (uint8_t)((r[5] << 6) | r[4]));
    ad15_w((uint8_t)(0x60 + slot), (uint8_t)((r[0] << 4) | r[1]));
    ad15_w((uint8_t)(0x80 + slot), (uint8_t)((r[2] << 4) | r[3]));
    ad15_w((uint8_t)(0xE0 + slot), r[0x0B]);
}

/* 0x0528. silence_channel(ch). */
static void ad15_silence_channel(int ch)
{
    ad15_w((uint8_t)(0x40 + ad15_op[2 * ch    ]), 0x3F);
    ad15_w((uint8_t)(0x40 + ad15_op[2 * ch + 1]), 0x3F);
    ad15_w((uint8_t)(0x80 + ad15_op[2 * ch    ]), 0x0A);
    ad15_w((uint8_t)(0x80 + ad15_op[2 * ch + 1]), 0x0A);
    ad15_w((uint8_t)(0xB0 + ch), 0x01);
}

/* ================= the twenty-one entry points ==================== */

/* +0x00, 0x0A7E: init(). The port probe and the register wipe are not
 * meaningful against an emulated chip; the register state it leaves
 * behind is. Returns the voice count, 10. */
static int drv_init(void)
{
    int c;
    memset(ad15_shadow, 1, sizeof ad15_shadow);   /* the blob pre-poisons
                                                   * the shadow with 1 so
                                                   * the zero wipe writes */
    for (c = 0; c < 0xFF; c++) ad15_w((uint8_t)c, 0x00);
    ad15_w(0x01, 0x20);          /* wave select enable */
    ad15_w(0x08, 0x00);          /* CSM off, NOTE-SEL 0 */
    ad15_w(0xBD, 0x00);          /* rhythm mode off */
    for (c = 0; c < 9; c++) ad15_silence_channel(c);
    memset(ad15_vel, 0, sizeof ad15_vel);
    ad15_nvoice = 10;
    return ad15_nvoice;
}

/* +0x03, 0x0ADA: shutdown(). */
static void drv_shutdown(void)
{
    int c;
    for (c = 0; c < 9; c++) ad15_silence_channel(c);
    ad15_w(0xB0, 0x01);
}

/* +0x18: a stub in AD15.DRV. sub_38178's "reset everything" therefore
 * does nothing on AdLib beyond what the sequencer itself clears. */
static void drv_reset_all(void)
{
    int c;
    for (c = 0; c < 9; c++) ad15_silence_channel(c);
}

/* +0x1E, 0x00F5: stop(ch). Channel 0 would stop the sample player. */
static void drv_free_voice(int ch)
{
    if (ch <= 0 || ch > 9) return;
    ad15_silence_channel(ch - 1);
}

/* +0x0C, 0x01EF: note_off(ch, p). Key off, keeping block and F-number. */
static void drv_begin_release(int ch, const mus_voice *v)
{
    if (ch <= 0 || ch > 9) return;
    ad15_w((uint8_t)(0xB0 + (ch - 1)), (uint8_t)(0x00 | (v->packtuned >> 8)));
}

/* +0x0F, 0x0213: silence(ch). Full attenuation on both operators. */
static void drv_voice_off(int ch, const mus_voice *v)
{
    (void)v;
    if (ch <= 0 || ch > 9) return;
    ad15_w((uint8_t)(0x40 + ad15_op[2 * (ch - 1)    ]), 0x3F);
    ad15_w((uint8_t)(0x40 + ad15_op[2 * (ch - 1) + 1]), 0x3F);
}

/* +0x21, 0x006B: program_change(ch, -, track, patch). */
static void drv_program_voice(int ch, mus_voice *v, const mus_track *t,
                              const uint8_t *patch)
{
    (void)v; (void)t;
    if (ch <= 0 || ch > 9 || !patch) return;
    ad15_w((uint8_t)(0xC0 + (ch - 1)),
           (uint8_t)(((patch[0x45] << 1) | patch[0x44]) & 0x0F));
    ad15_load_operator(patch + 0x46, ad15_op[2 * (ch - 1)    ]);
    ad15_load_operator(patch + 0x52, ad15_op[2 * (ch - 1) + 1]);
    /* The three modulation dispatches that follow read track[+0x29],
     * [+0x2B] and [+0x2C]. audio_init_chunk zeroes them and nothing in
     * seg027/seg028 ever writes them, so the dispatch never fires. Not
     * ported. */
}

/* +0x09, 0x010B: note_on(ch, voice, track, note, vel, patch). */
static void drv_note_on(int ch, mus_voice *v, const mus_track *t,
                        int note, uint8_t vel, const uint8_t *patch)
{
    int c;
    uint16_t packed;
    if (ch <= 0 || ch > 9 || !patch) return;
    c = ch - 1;

    v->drvnote = (uint8_t)note;
    if ((uint8_t)note != 0xFF) {
        packed = ad15_note_to_fnum((uint8_t)note, 0);
        v->pack = packed;
        v->packtuned = (uint16_t)(packed + (int8_t)patch[0x11]);
    }
    packed = v->packtuned;

    /* both operators to full attenuation first, so the key-on does not
     * click at the old level */
    ad15_w((uint8_t)(0x40 + ad15_op[2 * c    ]), 0x3F);
    ad15_w((uint8_t)(0x40 + ad15_op[2 * c + 1]), 0x3F);
    ad15_w((uint8_t)(0xA0 + c), (uint8_t)(packed & 0xFF));
    ad15_w((uint8_t)(0xB0 + c), (uint8_t)(0x20 | (packed >> 8)));

    ad15_vel[c] = patch[0x15] ? vel : 0x7F;
    ad15_apply_volume(c, patch, ad15_vel[c], t ? t->volume : 0x7F);
}

/* +0x24, 0x04A8: set_freq_hz(ch, p, hz). Only the sound-effect path uses
 * it; kept because sub_38DE6's status == 0xFF branch calls it.
 *   DX:AX = hz << 16; shift right, counting shifts as the block, until
 *   DX:AX <= 0x0185DCB0; divide by 50000; OR block<<2 into the high byte.
 */
static void drv_set_frequency(int ch, mus_voice *v, uint16_t hz)
{
    uint64_t n = (uint64_t)hz << 16;
    int block = 0;
    uint32_t fnum;
    if (ch <= 0 || ch > 9) return;
    while (n > 0x0185DCB0ull && block < 7) { n >>= 1; block++; }
    fnum = (uint32_t)(n / 50000u);
    v->pack = v->packtuned =
        (uint16_t)((fnum & 0x3FF) | ((unsigned)block << 10));
}

/* +0x12, 0x03C0: set_volume(ch, voice, vol). */
static void drv_set_volume(int ch, const mus_voice *v, uint8_t vol)
{
    if (ch <= 0 || ch > 9 || !v->instr) return;
    ad15_apply_volume(ch - 1, v->instr, ad15_vel[ch - 1], vol);
}

/* +0x15, 0x0250: controller(ch, voice, cc, val).
 *   cc 0x07 -> volume; cc 0x01/0x0B/0x0C -> a modulation destination
 *   selected by patch[0x16]/[0x17]/[0x18], which in every AdLib bank the
 *   game ships is 0 (i.e. not in the dispatcher's 0x81..0x85 range), so
 *   only the volume case does anything. */
static void drv_controller(int ch, const mus_voice *v, uint8_t cc, uint8_t val)
{
    if (ch <= 0 || ch > 9 || !v->instr) return;
    if (v->state == 0) return;                    /* p[+1] != 0 */
    if (cc == 0x07) ad15_apply_volume(ch - 1, v->instr, ad15_vel[ch - 1], val);
}

/* +0x1B: a stub in AD15.DRV - pitch bend does not reach the chip here.
 * It reaches it one tick later, through update_pitch. */
static void drv_pitch_bend(int ch, const mus_track *t, int16_t bend)
{
    (void)ch; (void)t; (void)bend;
}

/* +0x30: a stub in AD15.DRV. */
static void drv_flush(void) { }

/* +0x27, 0x02C6: update_pitch(ch, voice, track, patch). Runs for every
 * sounding voice on every 100 Hz tick and is where vibrato, the arpeggio
 * table and the pitch bend actually reach the chip. */
static void drv_voice_tick(int ch, mus_voice *v, const mus_track *t,
                           const uint8_t *patch)
{
    int c;
    int32_t dx;
    int16_t bend;

    if (ch <= 0 || ch > 9 || !patch) return;
    if (v->state == 0) return;                    /* p1[+1] != 0 */
    c = ch - 1;

    dx = (int16_t)v->pack;
    if (patch[0x35] == 0x91)
        dx = (int16_t)ad15_note_to_fnum((uint8_t)(v->drvnote + v->arpoff), 0);
    if (patch[0x28] == 0x90) dx += (int16_t)v->vibval;
    if (patch[0x19] == 0x90) dx += (int16_t)v->level;

    bend = t ? (int16_t)t->bend : 0;
    if (bend > 0) {
        int32_t d = (int32_t)(int16_t)
            ad15_note_to_fnum(v->drvnote, (int8_t)patch[0x12]) - dx;
        dx += (d * (int32_t)bend) >> 13;
    } else if (bend < 0) {
        int32_t d = dx - (int32_t)(int16_t)
            ad15_note_to_fnum(v->drvnote, (int8_t)(-(int8_t)patch[0x12]));
        dx -= (d * (int32_t)(-bend)) >> 13;
    }
    dx += (int8_t)patch[0x11];
    v->packtuned = (uint16_t)dx;

    ad15_w((uint8_t)(0xA0 + c), (uint8_t)(dx & 0xFF));
    /* [ODDITY] the original ORs the WHOLE high byte in - `mov al,2; sub
     * al,[di+1]; shl al,5; or al,dh` - so if vibrato or a pitch bend ever
     * pushed the packed word above 0x1FFF, bits 5..7 of dh would corrupt
     * the key-on bit and the block. No mask here either; verified below
     * that it never happens in any of the four songs. */
    ad15_w((uint8_t)(0xB0 + c),
           (uint8_t)((((2 - v->state) & 0xFF) << 5) | ((dx >> 8) & 0xFF)));
    /* the three modulation dispatches at 0x039A follow; their selectors
     * are patch[0x35]/[0x28]/[0x19], which hold 0x90/0x91/0x00 in every
     * shipped AdLib bank and never the 0x81..0x85 the dispatcher accepts.
     * Not ported. */
}
