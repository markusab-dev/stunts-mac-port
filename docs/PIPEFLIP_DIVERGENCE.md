# PIPEFLIP: the last physics deviation, narrowed

The one track that is not 16/16 against the DOS oracle. This is what is
actually known, measured rather than reasoned.

## It starts at frame 422, not 423

The summary in `tools/diff_oracle.py` reports 423 because it prints the first
divergence *per field* and the fields are ordered. Comparing whole CARSTATE
blocks, frame 421 is byte-identical and frame 422 is not.

## The car is in the air

`pos.y` over frames 419..424: 202309639, 170196999, 117702663, 64356359,
15597575, 7. It is falling, and it lands at frame 424 — after which the state
stops changing entirely (424, 425 and 426 are identical). So the whole
deviation happens during one fall and its landing.

## The shape of the error rules most things out

At frame 422:

| Field | Oracle | Ours | Difference |
|---|--:|--:|--:|
| pos.x | -2066546686 | -2066808830 | 4 whole units |
| pos.y | 64356359 | 64159751 | 3 whole units |
| pos.z | -1818427392 | -1819672576 | 19 whole units |
| rot.y (pitch) | 253 | 251 | 2 |
| pseudoGravity | -434 | -431 | 3 |

Positions are 16.16 fixed point, and **all three fractional halves are
identical** (0x0002, 0x0007, 0x0000). Only the whole-unit halves differ. That
is not accumulated rounding drift — it is one term, computed once, coming out
different.

Everything that *drives* the motion is identical for the entire 900 frames:
speed, speed2, rpm, gear, gearratio, steeringAngle, surfFront, surfRear,
surfAll. So this is not a physics-input difference.

## The first-order cause is the pitch

`pseudoGravity` is derived from the rotation, not independently
(`stateply.c:110`):

```c
vec_1C6 = { 0, 0, 0x82 };
mat_mul_vector(&vec_1C6, &mat_unk, &vec_FC);
arg_pState->car_pseudoGravity = -vec_FC.y;
```

so its 3-unit difference is a consequence of the 2-unit pitch difference, not
a second fault. Likewise the positions: a 2-unit pitch error over one airborne
step produces exactly this order of positional drift.

Rotation is written back in one place (`stateply.c:3319`):

```c
arg_pState->car_rotate.y = pState_minusRotate_x_1;
```

and in the airborne branch that value comes from `stateply.c:2359`:

```c
pState_minusRotate_x_1 = polarAngle(-var_F2, var_F4) - 0x100;
```

## The rounding direction is measurable, and it points one way

Diffing every 16-bit slot in CARSTATE for fields that are identical at frame
421 and differ at 422 gives eleven, and two of them are the tell:

| Offset | frame 420 | 421 | 422 oracle / ours |
|---|--:|--:|--:|
| +0x0A2 | 10 | -2 | **-12 / -13** |
| +0x096 | 43 | 28 | **12 / 13** |
| +0x04A | 193 | 201 | 235 / 233 |
| +0x08C | 16988 | 16991 | 16994 / 16993 |
| +0x09E | 16948 | 16951 | 16952 / 16951 |
| +0x0A4 | 16974 | 16977 | 16979 / 16978 |

`+0x0A2` has just crossed zero going down — 10, then -2, then -12 in the
oracle and **-13** in ours. -12 is what rounding *toward zero* gives; -13 is
what rounding *toward negative infinity* gives.

In C, `x / n` on a negative value rounds toward zero. An arithmetic right
shift, `x >> n`, rounds toward negative infinity. The 8086's `idiv` rounds
toward zero and `sar` rounds toward negative infinity.

**So we are shifting where the original divides**, on a value that has just
gone negative — which is why this appears on one track, at one frame, in one
fall, and nowhere in the other five tracks' 4500 frames.

`+0x096` moves the other way (ours 13 against the oracle's 12), which is
consistent: it is the negation of the same quantity, so one unit more negative
underneath shows up as one unit higher here.

## Where to look next


`var_F2` and `var_F4` are the velocity components fed to `polarAngle`. They
are **not** among the fields the oracle comparison covers, which is why the
divergence appears to arrive out of nowhere at 422 — the inputs had already
parted company and nothing was watching them.

Grep the airborne path in `src/sim_faithful/stateply.c` for `>>` applied to a
signed quantity that feeds `+0x0A2`, and check each against its line in
`seg001.asm`: if the assembly has `cwd` followed by `idiv`, the C must be `/`,
not `>>`. The two agree on positive values, which is why every other track
passes.

Do **not** widen `CAR_FIELDS` in `tools/diff_oracle.py` to chase this without
also updating `tools/verify.sh`: the bar counts the "identiska hela vägen"
lines and expects 16, so adding fields turns every track red for the wrong
reason.

## Ruled out: mat_mul_vector

The obvious suspect was `mat_mul_vector` (`src/render_faithful/math.c:137`),
which scales nine signed products with `>> 14`. It transforms the wheel
coordinates that `var_F2` and `var_F4` are summed from, so a rounding error
there would land exactly where this one does.

**It is correct.** restunts2's `vec_transform_asm_` (seg012.asm:8753), which
is what `mat_mul_vector` actually calls through, does:

```asm
imul    cx          ; dx:ax = signed 32-bit product
shl     ax, 1
rcl     dx, 1
shl     ax, 1
rcl     dx, 1       ; result taken from dx
```

Shifting the 32-bit product left by two and keeping the high word *is*
`product >> 14` with truncation toward negative infinity — the same thing C's
`>>` does on a signed value. No division, no rounding toward zero. The port
matches.

So the mismatch is somewhere else in the chain, and one of the two most
likely places is now eliminated rather than merely un-checked.

(Note for whoever continues: restunts1 has no `mat_mul_vector` label at all -
only restunts2 carries it, and only under the name `vec_transform`. That is
the second time restunts2 has been the tree with the answer, after the
symbolic dseg tables.)

Everything else is 16/16 and 100.00%, so whatever this is, it is narrow.
