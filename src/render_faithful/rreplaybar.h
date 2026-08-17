#ifndef RESTUNTS_RREPLAYBAR_H
#define RESTUNTS_RREPLAYBAR_H

#include <stdint.h>

/*
 * rreplaybar.h - the host contract for the in-race recording bar.
 *
 * loop_game (seg005 3391..5365) is the whole strip: the buttons, the scrub
 * control, the camera pan/zoom pad and the pause menu.  It is declared in
 * externs.h as
 *
 *     void loop_game(int16_t arg_0, int16_t arg_2, int16_t arg_4);
 *
 * and takes four modes in arg_0, exactly as the original:
 *
 *   0  load the 23 shapes out of SDGAME.PVS, then fall into mode 2 with
 *      arg_2 = 4 (the pause button latched).  Call once per race.
 *   1  redraw whatever changed.  arg_2 is the frame the solid marker sits
 *      on (state.game_frame), arg_4 the frame the hollow cursor sits on -
 *      the same value while playing, the scrub target while scrubbing.
 *   2  latch button arg_2 (0..6) and unlatch the rest.
 *   3  run one pass of the interactive loop: poll, hit-test, act.  This is
 *      the one that blocks while a scrub button is held.
 *
 * The original's host (ported_run_game_, seg005 74..889) calls them like
 * this, and this port's host must do the same:
 *
 *   after loading the race            loop_game(0, 0, 0);
 *   on entering replay mode           loop_game(0, 0, 0);
 *                                     loop_game(2, 4, 0);
 *                                     is_in_replay = 1;
 *   every frame while replaying       loop_game(3, 0, 0);
 *   every frame while merely paused   sprite_set_1_size(0, RFB_VIEW_W,
 *                                                      0, RFB_VIEW_H);
 *                                     loop_game(1, state.game_frame,
 *                                                  state.game_frame);
 *
 * and it must call replaybar_view_height() where the original computes
 * height_above_replaybar (seg005 498..520), so the 3D view shrinks to rows
 * 0..150 while the strip owns rows 151..199.
 *
 * [DEVIATION] show_dialog.  The original opens five of them from inside
 * loop_game - the pause menu "men", the display sub-menu "mdo", the
 * "overwrite the recording?" warning "con", the "file exists" warning "fex"
 * and the "save failed" box "ser".  This port has never had show_dialog;
 * every other screen stands in for it with SDL widgets of its own
 * (rendscreen.c, rwidgets.c, the graphics menu in main_native.c), and the
 * same choice is made here.  The five call sites go through
 * replaybar_hook_dialog instead, which the host fills in with its own
 * widget.  With no hook installed each one answers the way the original
 * answers a cancelled dialog, so the bar stays usable without them.
 */

/* The five show_dialog sites, by the original's text-resource key. */
struct replaybar_dialog {
	const char* resid;        /* "men" "mdo" "con" "fex" "ser"          */
	int16_t     nitems;       /* entries the original's text resource has */
	const int16_t* disabled;  /* nitems flags; non-zero = greyed out     */
	int16_t     kind;         /* the original's last show_dialog arg:
	                           * 2 = menu, 1 = message                   */
};

/* Returns the selected item, or 0 for "cancelled" - the original's own
 * convention (see the dispatch at seg005 loc_2440E, which subtracts 1). */
extern int16_t (*replaybar_hook_dialog)(const struct replaybar_dialog*);

/* seg005 loc_2410C: handle_ingame_kb_shortcuts.  Returns non-zero when it
 * swallowed the key, which ends loop_game's pass.  [ODDITY] externs.h types
 * it `void`, but seg005:24121 tests its return in al; hence the hook. */
extern int16_t (*replaybar_hook_kb_shortcut)(int16_t code);

/*
 * seg005 loc_2450A / loc_24630 - the load and save replay menu items.
 *
 * Both are ported in full, control flow included: the "fex" overwrite
 * question and its rename-and-try-again loop, the "ser" failure box, and the
 * track/car comparison that decides whether the car shapes have to be built
 * again for a recording made with a different car.  What is left to the host
 * is only what this port keeps on its side of the line everywhere else:
 *
 *   - the two file dialogs.  do_fileselect_dialog and do_savefile_dialog
 *     (seg008 1207..1984 and 2043..2191) go through show_dialog and the DOS
 *     mouse driver, neither of which is here;
 *   - the file layer.  fileio.c's DOS half is replaced by rfileio.c, so
 *     file_find and ported_file_load_replay_ / ported_file_write_replay_
 *     are the host's;
 *   - ported_free_player_cars_ / ported_setup_player_cars_, and the car
 *     placement at the end of init_game_state (seg001 4021..), which in
 *     this port lives in main_native.c's game_init.
 *
 * `name` throughout is a bare file name - no directory, no ".rpl" - which is
 * exactly what do_fileselect_dialog writes and what file_build_path is then
 * handed.  With a hook left NULL its call site behaves the way the original
 * behaves when the corresponding dialog is cancelled or the file cannot be
 * opened, so the bar stays usable.
 */

/* do_fileselect_dialog / do_savefile_dialog.  Non-zero when a name was
 * given; `nmax` counts the terminator. */
extern int16_t (*replaybar_hook_ask_loadname)(char* name, int16_t nmax);
extern int16_t (*replaybar_hook_ask_savename)(char* name, int16_t nmax);

/* file_build_path + file_find (seg005:2687): is there a <name>.rpl already? */
extern int16_t (*replaybar_hook_replay_exists)(const char* name);

/* ported_file_load_replay_ (seg005:1925) and ported_file_write_replay_
 * (seg005:1967).  Both answer in the original's own sense: 0 when the file
 * was read or written, non-zero on failure - loc_2458B zeroes
 * game_recordedframes after a failed read, loc_24712 puts up "ser" after a
 * failed write.
 *
 * A .RPL is 26 bytes of GAMEINFO, then the 1802-byte track - the two
 * 901-byte maps - then one input byte per recorded frame.  That layout is
 * not a guess: the original reads the whole file into td13_rpl_header, and
 * trakdata puts td14_elem_map_main, td15_terr_map_main and td16_rpl_buffer
 * immediately after it (sfdata.c:210), so a single read lands the header,
 * the track and the inputs where the game wants them.  The write is
 * `game_recordedframes + 724h` bytes, and 0x724 is 26 + 1802. */
extern int16_t (*replaybar_hook_load_replay)(const char* name);
extern int16_t (*replaybar_hook_save_replay)(const char* name);

/* seg005 loc_2460D: ported_free_player_cars_ + ported_setup_player_cars_,
 * run only when the comparison above found the recording needs other cars. */
extern void (*replaybar_hook_rebuild_cars)(void);

/* [DEVIATION] seg001 4021.. - the second half of init_game_state, which puts
 * the two cars on the start tile.  sim_faithful's init_game_state has only
 * the first half (sfasm_port.c:2101); the placement is game_init's.  Called
 * where the original's own init_game_state(-1) would have done it. */
extern void (*replaybar_hook_place_cars)(void);

/* seg005 loc_2480A - the "mdo" menu's graphics entry. */
extern void (*replaybar_hook_graphics_menu)(void);

/* loop_game(3, ...) spins inside itself while a scrub button is held, so it
 * needs a way to put the frame on the screen.  The original writes straight
 * into the visible page and needs nothing. */
extern void (*replaybar_hook_present)(void);

/* seg005 498..520.  Sets and returns height_above_replaybar: 0x97 when the
 * strip is up, 0xC8 when it is not. */
int16_t replaybar_view_height(void);

/* [DEVIATION] "the strip on the current page has been wiped, lay it down
 * again".  The original never needs this - nothing but loop_game writes the
 * strip's rows, so its per-page byte_449D8 flag is enough.  A host that
 * redraws the whole framebuffer every frame, as this port's does, must call
 * this immediately before each loop_game(3, ...) or the strip is drawn once
 * and then skipped for ever.  See the note above the definition. */
void replaybar_invalidate(void);

/*
 * STUNTS_REPLAYBAR_SHOT=<path>: draw the strip once and write it out as a
 * 24-bit BMP, so it can be looked at without a mouse.  Returns 1 if it drew
 * and wrote something, 0 if the variable is unset.  Pass NULL for `path` to
 * read the variable; pass a path to force it.
 *
 * Knobs, all optional:
 *   STUNTS_REPLAYBAR_CAMERA  cameramode 0..3      (which strip panel)
 *   STUNTS_REPLAYBAR_LATCH   0..6                 (which button is lit)
 *   STUNTS_REPLAYBAR_FOCUS   0..8, -1 for none    (which one is outlined)
 *   STUNTS_REPLAYBAR_FRAME   the solid marker's frame
 *   STUNTS_REPLAYBAR_TARGET  the hollow cursor's frame
 *   STUNTS_REPLAYBAR_TOTAL   game_recordedframes
 */
int replaybar_shot(const char* path);

#endif /* RESTUNTS_RREPLAYBAR_H */
