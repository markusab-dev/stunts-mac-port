# Handover: Phase 2 — opponents

Written at the end of a very long session, so the next one can start with the
scouting already done rather than repeating it.

## Where the port stands

Phase 1 of `~/.claude/plans/proud-watching-brooks.md` is **complete**. The
renderer has no stubs left: horizon panorama, in-game messages, turn arrows,
cracked windscreen, sinking, explosions. Main menu, track picker and car picker
work. Physics is verified 16/16 against the DOSBox oracle.

Everything below is what was learned about Phase 2 before stopping.

## The opponents are data, not AI

`OPP1.PRE` … `OPP6.PRE`, 27 resources each. The ones that matter:

| Resource | What it is |
|---|---|
| `enam` | the opponent's name, then their taunts |
| `path` | the racing line |
| `sped` | speed table — `120 75 50 40 30 20 60 40 20 90 115 20 30 70 80 50` |
| `edes` | description |
| `ed1a`…`ed4a`, `ev1a`…`ev3c` | dialogue lines |
| `winn`, `lose` | win / lose text |

`OPP<n>WIN.PVS` / `OPP<n>LOSE.PVS` hold the portraits — three 120x79 panels
each. **Note:** those are digitised photographs, dithered to 256 colours;
upscaling them was measured to make them worse, see the upscaling section of
FAITHFUL_RENDERER.md.

## What to port

| Function | Segment | Lines |
|---|---|---|
| `load_opponent_data` | seg004 | 266 |
| `opponent_op` | seg001 | 734 |
| `run_opponent_menu` | seg000 | 609 |

`do_opponent_op` (seg001, 7 lines) is just the thunk.

`load_opponent_data` reads the three resources with `locate_text_res(res,"nam")`
and `locate_shape_alt(res,"path"/"sped")`, then spends ~200 lines resolving the
abstract path against the actual track. It has large stack buffers - `var_B28`
is at -2856 - so give it room and read it in one sitting.

## Plumbing that already exists

* `state.opponentstate` exists and `update_player_state()` already takes it as
  an argument (src/sim_faithful/state.c:50).
* `simd_opponent` exists.
* `oppnentSped[16]` exists in src/sim_faithful/sfdata.c:81 and is **read** by
  ported, oracle-verified code: `sub_18D60` indexes it as
  `oppnentSped[si_oppSpedCode + ss_surfaceType]` (sfasm_port.c:1047). It is
  currently **all zeros** - nothing fills it. That is the `sped` resource above,
  and filling it is a small self-contained first step.
* `shape3d_load_car_shapes(playerid, opponentid)` already takes an opponent car
  id; main_native.c passes `0xFF` (none).
* Two sim stubs become meaningful only with an opponent, in
  src/sim_faithful/sfstubs.c: `car_car_coll_detect_maybe` (slot 6, already
  called 208 times per replay) and `car_car_speed_adjust_maybe` (slot 2).

## What the player loop does not do yet

`player_op` (state.c) only updates the player. The original drives the opponent
from a separate call in its main loop. There is no `opponent_op` equivalent
anywhere in this port yet.

## Verification, before claiming anything works

Cheap, under a minute, and it caught most of this session's mistakes:

```bash
# physics unchanged
for t in HILLTEST PIPEROLL T_HELL2 DEFAULT; do
  ./bin/stunts_native --data extracted/stunts/stunts --track $t --car coun --headless 600
done

# no unpainted pixels, every replay
for r in tests/replays/*.rpl; do
  ./bin/stunts_native --data extracted/stunts/stunts --replay "$r" --paint-check
done

# every track still starts
for f in extracted/stunts/stunts/*.TRK; do ...; done
```

Plus: render frames and *look*. `--replay <f> --shots <dir> --shot-from N
--shot-step K`, then `build/probe/sheet.py` for a contact sheet.

Test hooks that already exist: `STUNTS_MENU_SHOT`, `STUNTS_TRACK_SHOT`,
`STUNTS_CAR_SHOT`, `STUNTS_CRASH`, `STUNTS_ARROW`, `STUNTS_EXPLODE`.

## Habits worth keeping

1. **Read the asset before writing layout code.** Cockpit, menus, dialogs, car
   list, crack pattern, horizon - every one of them turned out to carry its own
   layout. Not one needed layout logic invented.
2. **Run every new 2D shape through `unflip_shape()`.** Three assets rendered as
   diagonal streaks because their header said "transposed" and the reader did
   not ask. A size check cannot detect it.
3. **Check that an edit actually landed** before rebuilding. A scripted
   search-and-replace silently failed to match twice this session; one of them
   was reported as done and only surfaced hours later.
4. **Kill stray `stunts_native` processes** before writing test images. Several
   instances writing the same file produced truncated BMPs that looked like a
   code bug.

## The one open question in the 2D layer

The horizon strips are blitted with a bitwise AND in the original
(`sprite_putimage_and`, a bare `and es:[di], al` with no lookup), but AND
renders a flat olive band while a plain copy renders the reference capture -
measured, 100% of 729600 pixels differ. A copy is what rskybox.c does.

Two explanations have been **eliminated**: the `!cg0`/`!eg0` palette map is
never applied to a `.PVS` (only to `.ESH`), and the AND blitter does no
`incnums[]` lookup the way `sprite_putimage_transparent` does. See the note at
the top of rskybox.c.
