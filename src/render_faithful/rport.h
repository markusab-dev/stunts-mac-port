/*
 * rport.h - Native-port compatibility prelude for the vendored restunts
 * renderer sources. Force-included before every translation unit via
 * `clang -include rport.h`.
 *
 * The vendored sources were written for Watcom C targeting 16-bit real-mode
 * DOS. This header neutralizes the DOS-specific keywords and supplies the
 * C99 environment the mechanically retyped sources expect.
 */
#ifndef RESTUNTS_RPORT_H
#define RESTUNTS_RPORT_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* DOS memory-model keywords have no meaning in a flat 64-bit address space. */
#define far
#define huge
#define near
#define cdecl

/* Old K&R-era cross-file calls that relied on implicit declarations. */
struct VECTOR;
void heapsort_by_order(int16_t n, int16_t* heap, int16_t* data);

/* Watcom runtime name for labs() */
#define _labs labs

/* Instruction-exact C translations of the former inline-asm functions
 * (rasm_port.c). */
uint16_t rasm_draw_line_related_impl(uint16_t arg_startX, uint16_t arg_startY,
                                     uint16_t arg_endX, uint16_t arg_endY,
                                     int16_t* arg_8, uint16_t var_4);
void rasm_preRender_default_impl_helper(int16_t* regsi, uint16_t var_A,
                                        uint16_t var_C, int16_t* var_18);

/* Native span blitters (rblit.c) with the exact signatures the renderer's
 * spritefunc/imagefunc pointers expect. */
void draw_filled_lines(int16_t* leftxs, int16_t* rightxs, uint16_t start_y,
                       uint16_t count, uint16_t color);
void draw_patterned_lines(int16_t* leftxs, int16_t* rightxs, uint16_t start_y,
                          uint16_t count, uint16_t color);
void draw_unknown_lines(int16_t* leftxs, int16_t* rightxs, uint16_t start_y,
                        uint16_t count, uint16_t color);
void preRender_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                    uint16_t color);
/* seg012.asm 17884-17929 - single clipped pixel (polyinfo primitive type 5). */
void putpixel_single_maybe(uint16_t arg_0x, uint16_t arg_2y, uint16_t arg_4col);

#endif /* RESTUNTS_RPORT_H */
