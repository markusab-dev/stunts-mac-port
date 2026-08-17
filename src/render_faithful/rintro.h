#ifndef RESTUNTS_RINTRO_H
#define RESTUNTS_RINTRO_H

#include <stdint.h>
#include "../render/stunts_palette.h"

/*
 * rintro.h - the title sequence and the credits (Phase 10).
 *
 * Ported from reference/restunts/src/restunts/asm:
 *
 *   run_intro_looped       seg000  641.. 741   the orchestrator
 *   run_intro              seg000  742.. 827   the two 2D title stills
 *   load_intro_resources   seg000  828..1569   MISNAMED: this is the credits
 *   setup_intro            seg003 6268..6830   the 3D intro animation
 *   intro_op               seg003 6831..7182   its per-frame script
 *
 * See rintro.c and rintro3d.c for provenance, deviations and oddities.
 *
 * ------------------------------------------------------------------------
 * WIRING
 *
 * One call, in main_native.c, immediately before the main-menu loop - i.e.
 * after mainresptr/miscptr and the fonts are loaded and before
 * `while (!play_windowed) {`.  That is where seg000:1042D calls it:
 *
 *     #include "render_faithful/rintro.h"
 *     ...
 *     rintro_run_looped(win, ren, tex, &pal, data_dir);
 *
 * The name is rintro_run_looped and not run_intro_looped only because
 * externs.h:430 already declares the never-implemented DOS run_intro_looped
 * with no arguments, and the two signatures cannot coexist.
 *
 * The return value is the original's: 27 when the sequence was aborted with
 * Escape (seg000 loc_10474 then offers "quit to DOS"), 1 when any other key
 * skipped it, 0 when it ran to the end.  A window-close is reported as 27
 * AND re-posted as an SDL_QUIT event, so the caller's own loop still sees
 * it whether or not it looks at the return value.
 *
 * MUSIC: rintro_run_looped calls music_native_init() (idempotent) and
 * music_native_play(MUSIC_SONG_TITLE), which is seg000:648's
 * file_load_audiores("skidtitl", "skidms", "TITL").  The song is only
 * AUDIBLE if audio_native_init() has already opened the device - today
 * main_native.c does that further down, after the menu.  Moving that block
 * above this call is all it takes; nothing here breaks if it is not moved.
 *
 * SCREENSHOT HOOKS - each renders one picture and exits(0):
 *   STUNTS_INTRO_PROD_SHOT=<file.bmp>   the producer logo still
 *   STUNTS_INTRO_TITL_SHOT=<file.bmp>   the Stunts title still
 *   STUNTS_CREDITS_SHOT=<file.bmp>      the finished credits page
 *   STUNTS_CREDITS_ANIM=<dir>           every frame of the arrow animation
 *   STUNTS_INTRO3D_SHOTS=<dir>          the 3D animation, every
 *                                       STUNTS_INTRO3D_STEP'th frame
 */

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

int rintro_run_looped(struct SDL_Window* win, struct SDL_Renderer* ren,
                     struct SDL_Texture* tex, const stunts_palette_t* pal,
                     const char* data_dir);

/* ---- shared with rintro3d.c; not part of the caller's API ---------- */

/* rfb_pixels -> texture -> screen.  One presented frame. */
void rintro_present(void);
/* 100 Hz ticks since the previous call: the original's timer_get_delta_alt,
 * whose PIT ran at 100 Hz (word_4499C = 100 / framespersec). */
int16_t rintro_delta(void);
/* seg008 input_do_checking: 0, 27 for Escape, 1 for anything else. */
int16_t rintro_input(int16_t ticks);
/* seg008 input_repeat_check (3415..3450). */
int16_t rintro_repeat_check(int16_t ticks);
/* One 24-bit BMP of the current framebuffer. */
void rintro_write_bmp(const char* path);
/* seg003 setup_intro + intro_op.  Returns 0, or 1 when input aborted it. */
int16_t rintro_setup_intro(const char* data_dir);

#endif /* RESTUNTS_RINTRO_H */
