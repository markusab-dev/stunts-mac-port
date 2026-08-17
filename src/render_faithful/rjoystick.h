#ifndef RESTUNTS_RJOYSTICK_H
#define RESTUNTS_RJOYSTICK_H

#include <stdint.h>
#include "../render/stunts_palette.h"

/*
 * rjoystick.h - the joystick: the input path, and seg008's calibration
 * screen.  See rjoystick.c for provenance, deviations and oddities.
 *
 * Ported from reference/restunts/src/restunts/asm and (for the routines
 * restunts1 only has as `extrn`) reference/restunts2/src/asm:
 *
 *   get_joy_flags      restunts2 seg012 3850..4041   the gameport read
 *   sub_307B4          restunts2 seg012 4043..4050   start calibrating
 *   sub_307D2          restunts2 seg012 4052..4066   nibble -> 3x3 cell
 *   sub_307E3          restunts2 seg012 4068..4078   the analog wheel
 *   do_joy_restext     restunts1 seg008 4708..4993   the calibration screen
 *   replay_unk         restunts1 seg005 3301..3390   wheel -> steer bits
 *   loc_2279A..227C8   restunts1 seg005 1319..1352   how driving reads it
 *
 * ------------------------------------------------------------------------
 * WIRING - four host calls, all in src/main_native.c.
 *
 * 1. Once, next to the other one-time setup (after SDL_Init, before the
 *    menu loop).  Safe with no device attached; returns 0 then.
 *
 *        #include "render_faithful/rjoystick.h"
 *        ...
 *        rjoy_init();
 *
 * 2. In the race loop, immediately after the keyboard has built its `input`
 *    byte and BEFORE the byte is written into td16_rpl_buffer - i.e. at
 *    src/main_native.c:2656, right after the two `if (k[SDL_SCANCODE_...])`
 *    lines and before `if (input != 0 && state.game_inputmode == 0)`:
 *
 *        input = rjoy_or_input(input);
 *
 *    It must be called at the point where elapsed_time2 == state.game_frame
 *    (which is where main_native.c samples input), because the steering
 *    servo reads the car's current steering angle.  With no device the byte
 *    comes back untouched.
 *
 * 3. The option menu's "Driving input device" button (src/main_native.c:1270,
 *    `case 0:` of run_option_menu's dispatch, off_1314A/seg000:5136) is where
 *    the original calls do_joy_restext.  Replace the `note = res_text(...)`
 *    stub with:
 *
 *        if (rjoy_calibrate(win, ren, tex, pal, dlgfont) != 0)
 *                note = res_text(mainresptr, "joy");
 *        else    note = res_text(mainresptr, "key");
 *
 *    Returns 1 when the joystick was left enabled, 0 when it was not.
 *
 * 4. On the way out, next to SDL_Quit():  rjoy_shutdown();
 *
 * TEST HOOKS - all of them are exercised by tools/build_joy.sh's bin/joy_shot,
 * and none of them needs a joystick attached:
 *   STUNTS_JOYCAL_SHOT=<file.bmp>    draw one frame of the screen and return
 *                                    0 without waiting for input
 *   STUNTS_JOYCAL_CELL=0..8            ... with that one of the nine lit
 *   STUNTS_JOYCAL_JOX=1                ... the "jox" refusal instead
 *   STUNTS_JOYFAKE=<x>,<y>,<buttons> a stick that is not there: x and y on
 *                                    the 0..1023 scale, buttons 0x10/0x20
 *   STUNTS_JOYFAKE=sweep             a stick that waves round all nine
 *                                    squares and then presses a button
 *   STUNTS_JOYFAKE=sweepfail           ... too fast, so the screen refuses
 */

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

/* Open the first attached device.  Idempotent.  Returns 1 when a joystick
 * or game controller is present, 0 when there is none - which is not an
 * error and leaves every other entry point here a no-op. */
int rjoy_init(void);

/* 1 when a device is open. */
int rjoy_present(void);

/* Close the device and the SDL subsystem this file opened. */
void rjoy_shutdown(void);

/* dseg 0x3FE00 byte_3FE00 bit 0: "the joystick is the driving input
 * device".  do_key_restext / do_mou_restext clear it, do_joy_restext sets
 * it.  get_joy_flags returns 0 while it is clear, exactly as the original -
 * so an attached-but-unselected joystick steers nothing. */
extern char byte_3FE00;
void rjoy_set_enabled(int on);

/* restunts2 seg012 3850..4041.  The flag byte the whole game speaks:
 *     bit0 0x01 up      bit1 0x02 down    bit2 0x04 right
 *     bit3 0x08 left    bit4 0x10 button1 bit5 0x20 button2
 * Returns 0 when byte_3FE00 bit 0 is clear or no device is open. */
int16_t get_joy_flags(void);

/* restunts2 seg012 4052..4066 - sub_307D2, the sixteen-entry table that turns
 * the four direction bits into one of the calibration screen's nine cells:
 *     0 centre  1 N  2 NE  3 E  4 SE  5 S  6 SW  7 W  8 NW */
int16_t sub_307D2(int16_t flags);

/* restunts2 seg012 4043..4050 - sub_307B4: arm the joystick and throw both
 * axes' learned extents away, so the next few hundred get_joy_flags calls
 * re-learn them.  This is what the calibration screen calls on entry. */
void sub_307B4(void);

/* OR the stick into the 20 Hz driving byte main_native.c records:
 *     bit0 accelerate   bit1 brake        bits2-3 steer (01 right, 10 left)
 *     bit4 shift up     bit5 shift down
 * (statecar.c: `arg_carInputByte & 3` is 1 accelerate / 2 brake,
 *  `(arg_carInputByte >> 2) & 3` is 1 right / 2 left, 0x10 is
 *  car_current_gear++ and 0x20 is car_current_gear--.) */
char rjoy_or_input(char input);

/* seg008 do_joy_restext (4708..4993) - the nine-square calibration screen.
 * Returns byte_3FE00's bit 0 as the routine leaves it: 1 the joystick is
 * on, 0 it is off (the dialog was cancelled, or a square was missed and the
 * "jox" refusal was shown). */
int16_t rjoy_calibrate(struct SDL_Window* win, struct SDL_Renderer* ren,
                       struct SDL_Texture* tex, const stunts_palette_t* pal,
                       void far* dlgfont);

/* One 24-bit BMP of the framebuffer - what STUNTS_JOYCAL_SHOT writes. */
void rjoy_write_bmp(const char* path);

#endif /* RESTUNTS_RJOYSTICK_H */
