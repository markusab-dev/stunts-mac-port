/*
 * music_opl_nuked.c - src/music_opl.h implemented on Nuked-OPL3.
 *
 * The core is vendored unmodified in src/vendored/opl/ (opl3.c, opl3.h,
 * Copyright (C) 2013-2020 Nuke.YKT, LGPL-2.1). Nothing above music_opl.h
 * knows it is there: the sequencer and the ported AD15.DRV driver write OPL2
 * registers and this file turns them into samples.
 *
 * OPL2, not OPL3: the driver only ever writes the low register bank
 * (0x00..0xF5) and never sets the OPL3 "new" bit at 0x105, which leaves the
 * chip in its YM3812-compatible mode. That is exactly what Stunts' AdLib
 * driver did, so no masking is needed here.
 *
 * Mono: all four drivers Stunts shipped are mono. Nuked generates stereo, so
 * the two channels are averaged. OPL3_GenerateResampled does the 49716 Hz ->
 * host rate conversion internally, which is both simpler and better than
 * resampling afterwards.
 */
#include <string.h>

#include "music_opl.h"
#include "vendored/opl/opl3.h"

void (*opl_write_hook)(uint8_t reg, uint8_t val, void *ud) = 0;
void  *opl_write_hook_ud = 0;

static opl3_chip s_chip;
static int       s_rate;
static int       s_ready;

void opl_reset(int host_sample_rate)
{
	s_rate = host_sample_rate > 0 ? host_sample_rate : 44100;
	memset(&s_chip, 0, sizeof s_chip);
	OPL3_Reset(&s_chip, (uint32_t)s_rate);
	s_ready = 1;
}

void opl_write(uint8_t reg, uint8_t val)
{
	if (opl_write_hook) opl_write_hook(reg, val, opl_write_hook_ud);
	if (!s_ready) return;
	/* Buffered is what a real bus write behaves like: the chip latches the
	 * value and applies it on its own clock rather than instantly. */
	OPL3_WriteRegBuffered(&s_chip, (uint16_t)reg, val);
}

void opl_render(int16_t *buf, int frames)
{
	int i;
	if (!s_ready) { memset(buf, 0, (size_t)frames * sizeof(int16_t)); return; }
	for (i = 0; i < frames; i++) {
		int16_t st[2];
		OPL3_GenerateResampled(&s_chip, st);
		buf[i] = (int16_t)(((int32_t)st[0] + st[1]) / 2);
	}
}

const char *opl_core_name(void) { return "Nuked-OPL3 (YM3812 mode)"; }
int         opl_core_is_real(void) { return 1; }
