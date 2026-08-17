# PIPEFLIP: the last physics deviation - found and closed

PIPEFLIP is now 16/16 and 100.00%, like the other five oracle tracks, and
every frame is byte-identical to the DOS build outside `opponentstate`.

The cause was **an uninitialised stack local that the original relies on
keeping its value between frames**. Nothing to do with rounding.

## What it was

`update_player_state` has a four-word stack local, `var_140someWhlData`
(`seg001.asm:862`, `[bp-140h]`). Across the whole 2700-line routine it is

* **written in exactly one place**, `loc_14FAC` (`seg001.asm:1099`), and
* **read in three**, `loc_15882`, `loc_159AD` and `loc_15A30`
  (`seg001.asm:1977, 2091, 2250`) - the three wheel-touches-surface paths.

The write is inside the wheel-integration loop, and that loop only reaches
`loc_14FAC` when `var_pSpeed2Scaled != 0`, i.e. **only while the car is
moving**. The reads are unconditional.

So once a car has stopped, the original keeps reading the slot back exactly
as the last moving frame left it - `update_player_state` is re-entered on the
same stack frame every time, so the memory is still there. It is uninitialised
in the C sense and perfectly deterministic in practice.

The port declared it as an ordinary C local. Ours read zero.

## Why it only showed on this one track, at this one frame

The value only matters when a **stopped** car's wheel is pushed out of a
surface, which needs all three of:

| condition | PIPEFLIP |
|---|---|
| `car_speed2 == 0` (so the slot is stale) | from frame 420 - the car has crashed |
| a wheel below the plane (`nextPosAndNormalIP < 0`) | wheel 3, first time at frame 423 |
| that wheel taking the `loc_15A30` push-out path | frame 423 and after |

Frames 421 and 422 have wheel 3 in contact but only falling (the gravity step
at `loc_15642`), which never reads the slot. Frame 423 is the first push-out,
and it is where the two runs part company. The other five tracks never stop
the car on a surface, so their 4500 frames never read a stale slot.

## The measurement that pinned it

Diffing whole CARSTATE blocks (rather than field by field) shows frame 422
0-based - 423 in the dump's 1-based numbering - as the first differing frame,
with **eleven bytes** out: `pos1.x/y/z`, `rot.x` (heading), `rot.z` (roll),
`field_48`, and five `car_whlWorldCrds` words. `rot.y` (pitch) is identical.

Instrumenting our own port to dump `vecl_1C0` per wheel and per pass gives the
rest. After the collision pass wheel 3 should be at

    (494567, 987, 1087631)   but we produce   (494551, 975, 1087555)

and the other three wheels agree. (The target is fixed by the oracle's own
dump: `car_whlWorldCrds1[i]` is that long `>> 6`, and `car_posWorld1` is the
four longs summed and `>> 2`.)

That wheel's new position is `prev + vec_C + vec_planerotopresult`, and since
`vec_C` is `(0, -100, 0)` here, the x and z error must be entirely in
`vec_planerotopresult` - the surface push. Ours is `(-265, -175, 46)`, and it
has to be about `(-249, -166, 122)`: **the same length, a different
direction**, roughly 14 degrees apart. Rounding cannot do that.

`plane_rotate_op` builds that push as `planeRotation * rotY(-si) * (0,0,EE)`,
with

```c
si = polarAngle(-var_32.x, var_32.z) + pState_f36Mminf40sar2;
```

Sweeping `si` and `EE` over every value that reproduces the required x and z
puts the answer at `si` ~ 191 with `EE` ~ 320. We compute `EE = 320` and
`si = 232`, from `polarAngle(214, 32) = 232` plus
`pState_f36Mminf40sar2 = var_140someWhlData[3] = 0`.

    232 + 983 = 1215,  1215 & 0x3FF = 191

and 983 is exactly `car_36MwhlAngle` on frames 414..420 - the value the slot
was last written with, on frame 420, the last frame with `speed2 != 0`. The
arithmetic closes on the nose.

## The fix

`src/sim_faithful/stateply.c`, in `update_player_state`:

```c
static int16_t var_140persist[2][4];
int16_t* var_140someWhlData = var_140persist[arg_MplayerFlag != 0];
```

`static` is the C spelling of "the same stack slot every call", which is what
the original gets for free. It is indexed by the caller because the player's
and the opponent's calls arrive through differently sized frames (`player_op`
and `opponent_op`), so in the original they land on different slots; the five
oracle tracks have no opponent, so only slot 0 is exercised by the bar.

## Things this ruled out along the way

The earlier version of this document argued for a `>>`-versus-`/` rounding
mismatch. That was wrong, and so were two of its premises:

* **The positions are not 16.16 fixed point.** They are plain 32-bit world
  units; `pos1.y` runs 3597, 3087, 2597, 1796, 982, 238, 0. The claim that
  "all three fractional halves are identical" came from reading the low word
  of an integer.
* **The field offsets were one slot off.** The value quoted as `+0x0A2`
  going 10, -2, -12/-13 is `car_whlWorldCrds2[3].y` at `+0x0A0`, and
  `car_whlWorldCrds2` is written at `loc_16309` from the *final* rotation
  matrix - downstream of the position and rotation, so a consequence rather
  than a cause. Likewise the "pitch" that differs by 2 is `car_rotate.x`,
  the heading; the pitch `car_rotate.y` is identical at frame 422.

A scan of every C statement in `src/sim_faithful/` and `src/render_faithful/`
against the assembly carried in its own comment block found **no** place where
the C shifts and the original divides, or vice versa. Checked by hand against
the disassembly and found faithful: `mat_mul_vector`, `mat_multiply`,
`mat_invert`, `polarAngle` (`int_atan2`, restunts2 seg012.asm:33),
`polarRadius2D`/`3D` (`int_hypot`, seg012.asm:3081), `multiply_and_scale`,
`vec_normalInnerProduct` and `plane_origin_op` (seg001.asm:9065, 9133 - both
use `__aFldiv`, which truncates toward zero like C), and `carState_rc_op`.

## Instrumenting the oracle, if it is ever needed again

Getting inside a DOS frame is possible and worth writing down.
`build/oracle_build/src/restunts/asmorig/seg001.asm` can be patched to copy
stack locals into `state.game_longs` (288 bytes, which `diff_oracle.py`
already reports separately), and `tools/build_oracle.sh` picks the edit up
without re-copying the reference tree.

One trap: `game_longs` is **read back** by `sub_19BA0`, the crash-debris
updater, which `update_gamestate` calls *after* `update_player_state` returns
(`seg001.asm:4488`). Parking probe values there mid-frame makes the debris
immortal and the run never finishes. Park at the end of `update_gamestate`
and put the real values back at its start.

In the end the port's own instrumentation was enough, because the oracle dump
already pins the wheel longs to within 64 through `car_whlWorldCrds1` and
their sum exactly through `car_posWorld1`.
