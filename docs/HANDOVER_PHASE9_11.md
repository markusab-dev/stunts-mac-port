# Handover: what is left, with the scouting done

Written so the next session starts where this one ended rather than
re-deriving it. Everything below was read out of the disassembly and checked
against this tree; nothing here is guessed.

## Where the port stands

`bash tools/verify.sh` is the bar. It currently passes everything:

| Check | Result |
|---|---|
| DOS oracle, TUBETEST / HILLTEST / PIPEROLL / T_HELL2 / T_HELL4 | 16/16 fields, 100.00% bytes |
| DOS oracle, PIPEFLIP | 10/16, 98.30% — the one known open deviation |
| All shipped tracks start | 39/39 |
| All replays run | 12/12 |
| All cars | 11/11 |
| Opponent car pairings | 22/22 |
| Rewind restores state byte-identically | 3/3 |
| Unpainted pixels | 0 across all 12 replays |
| Screens that render | 25 |

Phases 1–8 and 10 are done. Phase 7's visible gaps are closed.

## Phase 9 — what is done and what is not

**Done, and verified:** the rewind *mechanism*.

* `update_gamestate` (sfasm_port.c) already writes the snapshots. Every
  `word_45A00` frames — that is `30 * framespersec`, so 600 at 20fps — it
  calls `get_kevinrandom_seed(state.kevinseed)` and copies the whole
  1120-byte state into slot `game_frame / word_45A00` of the twenty-slot
  ring at `cvxptr`.
* `restore_gamestate` and `init_kevinrandom` are now ported
  (sfasm_port.c, from seg001 4286..4394 and seg002 175..205).
* **Why it works at all:** the RNG seed is six bytes carried *inside* the
  saved state. Put a snapshot back and everything after it reproduces
  exactly. Without that a rewind would diverge the moment anything drew a
  random number. Measured: rewinding to frames 0, 600 and 1200 restores all
  1120 bytes identically.
* `STUNTS_REWIND=<frame>` drives that test; it is in `tools/verify.sh`.

**Also done:** `P` pauses mid-race and Backspace rewinds one snapshot
interval, wired into the race loop in main_native.c. The two controls that
matter are usable without the drawn strip.

**Also done:** `replaybar_view_height()` and the frame readout
(`rreplaybar.c`). seg005:498..520 spells out when the strip exists at all:

    if (dashb_toggle && !followOpponentFlag
        && game_replay_mode == 2 && replaybar_enabled)
             height_above_replaybar = 0x97;   /* 151 */
    else     height_above_replaybar = 0xC8;   /* 200 */

so it is a *playback* control - never present while driving, and gone in the
opponent's camera. 49 rows.

**Not done:** the strip's buttons. `loop_game` (seg005 3391..5365, 1975 lines) is
the on-screen VCR strip — pause, the scrub control, save/load replay, and the
camera pan and zoom. `ported_run_game_` (seg005 74..889, 816 lines) is the
host around it.

What it needs that this tree does not have:

| Missing | Source | Lines |
|---|---|--:|
| *(nothing — every dependency is now ported)* | | |

`init_game_state` (seg001 3885..4021) is **done**: `init_game_state_vars`
already held the body, so it needed the tail (seg001:4008..4021, which clears
everything the scoreboard reads) and the argument dispatch, where -3 takes the
early exit that only refreshes the frame-rate tables.

`check_input` (seg008 3109..3147) is **done** — `src/render_faithful/rreplaybar.c`,
which is where the rest of the bar belongs. Porting it turned up three more
symbols that externs.h declared and nothing defined: `mouse_butstate`,
`get_kb_or_joy_flags` and `kbormouse`, now backed by SDL in the same file.
Phase 9 turned up six of these in all: `mouse_butstate`,
`get_kb_or_joy_flags`, `kbormouse`, `word_4499C`, `replaybar_enabled` and
`height_above_replaybar` (Phase 6 had found `video_flag1_is1`, Phase 10
`waitflag` and `slow_video_mgmt`). `word_4499C` was the expensive one: it was
defined in `audio_native.c`, which `build_dumper.sh` does not link, so the
oracle tool stopped building and **all six physics tracks reported 0/16** -
an undefined constant masquerading as total physics failure. It now lives in
`sfasm_port.c`, which both binaries link. **Grep for the symbol before assuming a routine is missing
its data — it may simply never have been defined.**

Everything else it calls is already ported: `restore_gamestate`,
`update_gamestate`, `update_crash_state`, `format_frame_as_string`,
`audio_carstate`, `sprite_1_unk`, `sprite_1_unk4`, `mouse_multi_hittest`,
`intro_draw_text`, `font_op2_alt`, `locate_text_res`, `shape2d_op_unk`.

`show_dialog` is called five times; this port stands in for it with its own
SDL widgets everywhere else, and the same choice fits here.

## Phase 11 — the track editor

Not started. `load_tracks_menu_shapes` is misnamed: it *is* the editor.

| Part | Source | Lines |
|---|---|--:|
| The editor | seg009 54..2911 | 2857 |
| The 2D track map | `draw_2DtrackMap`, seg009 3179 | 820 |
| Icon rendering | `preRender_icons`, seg009 2954 | 222 |
| Helpers | seg009 4007 | 478 |

It leans on seg008 harder than anything else: eight `show_dialog`, six
`draw_button` (ported, `rwidgets.c`), six `mouse_track_op`. **This is the
point at which porting `show_dialog` (850 lines) faithfully may be cheaper
than building an eighth bespoke dialog** — take that decision at the start of
Phase 11, with the eight call sites in front of you.

## Joystick

`do_joy_restext` (seg008 4708..4993, 285 lines) calibrates a joystick. This
port reads no joystick at all — no `SDL_INIT_JOYSTICK`, no `SDL_Joystick`
anywhere — so the calibration screen needs the input path built first. Two
pieces of work, not one.

## PIPEFLIP frame 423

The last physics deviation, 10/16 and 98.30%. Narrowed but open. Everything
else is 16/16 and 100.00%.

## Habits that earned their place

1. **Read the data before writing layout code.** Cockpit, menus, dialogs, car
   list, crack pattern, horizon, high-score file, gear gate, track preview
   camera — every one carried its own layout. Not one needed layout logic
   invented.
2. **Run every new 2D shape through `unflip_shape()`.** Several assets render
   as diagonal streaks otherwise, and a size check cannot detect it.
3. **Check an edit landed before rebuilding.** Scripted search-and-replace has
   silently failed to match here more than once.
4. **Render offscreen.** `tools/verify.sh` exports `SDL_VIDEODRIVER=dummy`.
   Test runs used to open real windows and steal focus while the user was
   working. Output is byte-identical.
5. **Never leave the tree built with `ASAN=1`.** The Homebrew libSDL2 is a
   shim over SDL3 and aborts in its own `dllinit` under ASAN, before `main`
   — so an ASAN run reports nothing and *looks* clean. It is not.
6. **Assert on content, not on "a file appeared".** The turntable bug produced
   a perfectly valid all-black BMP; the graphics menu could have stored a
   number nothing reads. Both checks now measure pixels.
7. **Make fatal messages say which way they failed.** "locate_shape: not
   found" cost hours; "not in the registry" pointed straight at memory
   corruption.
8. **Take no agent report at face value.** Every number in the table above was
   re-run before being repeated.
