# Faithful Renderer Port (src/render_faithful/)

## What this is

A native ARM64 port of the **original** Stunts 1.1 rendering pipeline, vendored
from the reverse-engineered `reference/restunts` sources — not a
reimplementation. The previous renderer attempts in `src/render/` re-invented
the pipeline from descriptions and produced structurally wrong images; this
port uses the original algorithms directly.

## Provenance

| File | Origin | Modification |
| :--- | :--- | :--- |
| `frame.c` | restunts `c/frame.c` (C port of seg003 `update_frame`) | mechanical 16-bit type mapping only |
| `shape3d.c` | restunts `c/shape3d.c` (seg006/seg012 3D core) | type mapping; 3 inline-asm bodies routed to `rasm_port.c` |
| `math.c`, `heapsort.c` | restunts C ports | type mapping only |
| `externs.h`, headers | restunts | type mapping only |
| `rasm_port.c` | **new** — instruction-exact C translation of the two remaining Watcom inline-asm functions: `draw_line_related_impl` (edge clipper/descriptor builder, seg012 loc_2EB62) and `preRender_default_impl_helper` (span-array edge merge, loc_31B5C) | translated with 16-bit register/carry semantics, original labels preserved |
| `rdraw_dispatch.c` | **new** — C translation of `get_a_poly_info` (seg006, final draw dispatcher) | as above |
| `rframe_helpers.c` | **new** — C translations of `skybox_op`, `skybox_op_helper2`, `transformed_shape_add_for_sort` (seg003), `build_track_object`, `subst_hillroad_track` (seg004) | as above |
| `rdata.c` | **new** — data tables transcribed from `asm/dseg.asm` (trkObjectList, scene shapes, lookahead cone tables, material lists, ...) | values transcribed 1:1; see tools/extract_dseg_tables.py |
| `rblit.c` | **new** — 320x200 8-bit framebuffer + span blitters (`draw_filled_lines` etc., which lived in the MCGA video driver binary) | semantics derived from the C call sites; dither pattern bit order is [HYPOTHESIS] until pixel-verified |
| `rfileio.c` | **new** — adapter: original file/memory API → project's verified DSI asset loader | replaces segment-arithmetic fileio.c/memmgr.c |
| `rbridge.c` | **new** — copies `stunts_sim_context_t` into the original `GAMESTATE`/track-map globals each frame | isolates renderer from the sim implementation |
| `rstubs.c` | **new** — no-op stubs for HUD text/crack/sinking/explosion overlays + real `sprite_set_1_size` | 2D overlays to be ported later |

## Port rules

1. Watcom 16-bit type discipline: `int`→`int16_t`, `unsigned`→`uint16_t`,
   `long`→`int32_t` (mechanical pass: `tools/apply_16bit_types.sh`). Plain
   `char` stays signed: restunts builds with Borland C (`bcc`), whose char is
   signed, matching Apple arm64's default — no flag needed.
2. Inline-asm translations model registers as 16-bit locals, preserve label
   names and goto structure, and emulate carries explicitly, so each C block
   can be audited line-by-line against the asm.
3. No behavior "fixes": oddities in the original (e.g. `proj & 0xFFFF == 0`
   precedence) are preserved as-is.

## Known deliberate deviations (to resolve)

- `slope_round()` replaces the seg012 interpolation tables
  (`off_2F44A`, cx < 0x32) with the arithmetic the fallback path computes:
  `round((dx<<16)/cx)`. [HYPOTHESIS] that table contents equal this formula;
  verify by pixel comparison, else extract tables from the video driver.
- Dither pattern bit order in `draw_patterned_lines` is a best guess pending
  pixel comparison.
- Skybox mountain bitmap panorama is stubbed (polygon sky/ground fills are
  ported); 2D bitmap path lands with the shape2d port.
- HUD overlays (time text, cracks, sinking, explosions) stubbed.

## Verification plan

1. Static frame: `tools/render_faithful_frame.c` renders frame N of a replay
   → BMP (320x200, SDMAIN.PVS palette).
2. Reference: original game in DOSBox-X on the same track/car/camera,
   screenshots compared pixel-wise (`tools/compare_frame_320.py`).
3. End state: pixel-identical 3D viewport (modulo stubbed overlays), then
   overlays, then the interactive app.

## Important project-status discovery (2026-08-14)

The claimed simulation verification ("40,146/40,146 primary oracle frames vs
DOSBox ground truth, 0 divergences") **does not hold**. `tools/test-fidelity`
only compares the hand-written native sim (`src/sim/stunts_sim.c`, ~550 lines)
against itself compiled at -O0 vs -O3; the "VERIFIED" subsystem labels are
hardcoded strings; the replays are synthetic. No DOSBox comparison exists in
the pipeline. The sim is a plausible approximation, not a verified port.
Plan: vendor the restunts original C simulation (stateply.c/statecar.c) behind
the same GAMESTATE bridge and verify against real repldumo.exe dumps
(see Task #8).


## Bridge findings (2026-08-15) — verified against the original

Rendering the first real frames surfaced four convention mismatches between
`src/sim` and the original renderer. All were resolved in favour of the
original code (the ground truth), and are now handled in `rbridge.c`/`src/sim`:

1. **Position scale.** `CARSTATE.car_posWorld1` is in 1/64 tile-units:
   `frame.c` reads `car_posWorld1.lx >> 6`, and `restunts.c` initialises it as
   `trackcenterpos2[col] * 64`. The sim keeps tile-scale (1024/tile) values, so
   the bridge shifts left by 6.
2. **Track row convention.** `frame.c` indexes
   `td14_elem_map_main[east + trackrows[south]]` with `trackrows[i] = 30*(29-i)`
   and `south = 29 - (z>>10)`; the two flips cancel, so the .TRK row equals
   `z >> 10`. The sim applied a single flip, placing the car 19 rows away from
   the start line. Fixed in `stunts_sim.c` (start position and ground query).
3. **Start-line element code.** [FACT] `trkObjectList` in `dseg.asm` maps
   element codes 0x01..0x03 to shape `fini`; the previous 0x27..0x2A guess maps
   to `ramp`/`rban`. Fixed in `stunts_sim.c`.
4. **SIMD block.** The original copies the raw `simd` resource straight into
   the global (`fmemcpy(&simd_player, locate_shape_alt(carresptr, "simd"), …)`).
   The bridge now does the same. The sim's own field-by-field parse is off by
   two bytes: it yields `wheel_coords = (16,-960,0)…` where the raw block
   yields the symmetric `(-960,0,1984), (960,0,1984), (960,0,-1984),
   (-960,0,-1984)` — another symptom of the unverified sim (Task #8).

### Known remaining gaps (renderer-visible)

* **Lane offset at the start.** `restunts.c` offsets the start position from the
  tile centre by `multiply_and_scale(sin_fast(track_angle+0x200), 210)` and
  `…+0x100, 36)` — i.e. back from the line and over into the lane. The sim
  starts exactly on the tile centre, so our car sits on the centre line while
  the DOS car sits in its lane. Needs `track_angle` from `track_setup`.
* **Cockpit viewport.** DOS cockpit uses the windshield rect and
  `set_projection(0x23, dash_y/6, 0x140, dash_y)` with the dashboard height,
  not the full 200 rows we currently pass. Full-screen chase view is the
  apples-to-apples comparison until the dashboard is ported.
* **Skybox panorama.** The hills/trees bitmap band is still stubbed
  (`rstub_skybox_bitmap_blit`); sky and ground polygon fills are correct.
* **Sim motion.** Holding the throttle moves the car roughly 6x too fast and
  in the wrong heading direction — sim-side, tracked by Task #8.


## First verified pixel comparison (2026-08-15)

Reference: original Stunts in DOSBox-staging, DEFAULT track, COUN, cockpit
view with the dashboard toggled off (`D`), which makes the DOS projection
identical to ours (`set_projection(0x23, 200/6, 0x140, 200)`).
Files: `tests/dos_reference/dos_nodash_320x200.png` vs
`tests/render_native/faithful_coun_f0_cockpit.bmp`
(`tools/compare_reference.py`).

**Result: 36,092 / 64,000 pixels identical (56.4%).**

Colour reproduction is exact. The macOS screenshot carries the monitor's ICC
profile ("PHL 346E2C"), which shifts saturated colours; after converting the
capture to sRGB the palettes agree: asphalt `(73,73,73)` in both, sky
`(92,255,255)` vs `(93,255,255)` (one unit of profile-conversion rounding).
This validates the SDMAIN.PVS palette load, the `material_color_list` mapping
and the span blitters.

Remaining differences are all accounted for by known stubs/gaps:
* HUD text overlay ("Fasten your Seatbelt!") — stubbed (`rstubs.c`).
* Skybox hills/trees bitmap panorama — stubbed
  (`rstub_skybox_bitmap_blit`); sky/ground polygon fills are correct.
* Start-line banner: DOS draws a white banner bitmap where we draw the
  checkered polygon band — same 2D-bitmap gap.
* Residual sub-tile camera offset (the lane placement now matches after
  porting the `210 back / 36 lateral` start offset from `restunts.c`;
  `track_angle` is still derived from the start element rather than from a
  ported `track_setup`).


## Camera-height investigation (2026-08-15)

A user comparison spotted that our cockpit view sits lower than DOS: the
bridge deck behind the start banner is visible below the banner in ours, but
hidden in DOS. Two candidate causes were tested against the DOSBox capture.

**Start offset — confirmed correct.** Sweeping the start Z offset shows a
sharp peak exactly at the value the ported `restunts.c` formula produces
(net -210): -410 -> 45.7%, -310 -> 48.2%, **-210 -> 56.4%**, -110 -> 46.8%,
-10 -> 45.3%. No change needed.

**`car_height` — confirmed correct data.** Offset 208 of the raw `simd`
block gives sensible per-car values: COUN 16, PMIN 12, P962 13, VETT 18,
AUDI 23, LM02 37. The field is right.

**Cause: the car's resting height is missing.** Raising the eye point by
~8 world units hides the bridge deck exactly as in DOS and raises the score
to 59.9%. The `road` shape is flat at Y=0 (all 28 vertices), so the road
surface is not the source. [DERIVED] The original physics settles the car on
its wheels, leaving the body origin a ride height above the road, while
`stunts_sim.c` pins `pos_world.ly` to the terrain height (0) and never
models suspension. Supporting measurement: the wheel shapes `car1`/`car2`
span Y 0..17 and 2..18 (radius ~8) in shape space.

No fudge factor was added: `car_height` stays at the value from the data and
the start offset stays at the ported formula. The residual vertical offset is
a simulation gap that the Task #8 physics port resolves at source.
`STUNTS_CAR_HEIGHT`, `STUNTS_START_DX` and `STUNTS_START_DZ` remain as
no-op-by-default validation hooks for future sweeps.


## Simulation port — step A complete (2026-08-15)

`src/sim_faithful/` now holds the vendored restunts simulation
(`state.c`, `statecar.c`, `statecrs.c`, `stateply.c`, 4,835 lines) under the
same mechanical 16-bit type mapping as the renderer
(`tools/apply_16bit_types_sim.sh`). **All four files compile clean.**
The sources contain zero inline assembly, and they write into the very same
`struct GAMESTATE state` the renderer already reads — so once complete, the
`rbridge.c` state copy disappears entirely.

One change beyond the type mapping: `statecar.c`'s `update_car_speed` began
with `return ported_update_car_speed_(...)`, routing the original build to the
16-bit asm and skipping the 804-line C body below it. The bypass is removed so
the C body runs.

### Step B scope — measured, not estimated

Linking revealed twelve helpers the simulation calls that exist **only** as
16-bit assembly. Sizes from the disassembly:

| Function | asm lines | Role |
| :--- | ---: | :--- |
| `update_grip` | 517 | **core** — tyre grip per frame |
| `bto_auxiliary1` | 426 | track-object geometry helper |
| `car_car_coll_detect_maybe` | 404 | car-to-car collision detect |
| `sub_18D60` | 383 | called from the start/reset path |
| `detect_penalty` | 280 | off-track time penalties |
| `carState_rc_op` | 193 | per-wheel contact response |
| `car_car_speed_adjust_maybe` | 173 | car-to-car impact speed |
| `state_op_unk` | 170 | post-crash/roll handling |
| `upd_statef20_from_steer_input` | 130 | **core** — steering input |
| `audio_unk3` | 24 | sound only — genuinely stubbable |
| `nextPosAndNormalIP`, `show_penalty_counter` | ? | not found as asm procs; to locate |

**~2,700 lines of assembly**, of which `update_grip` and
`upd_statef20_from_steer_input` are on the per-frame physics path and cannot
be stubbed. `track_setup` (seg004.asm, 1,764 lines) is additionally required
to build the track tables the physics reads (`trackdata3`, `td01`, `td02`,
`td10`).

`sfstubs.c` currently supplies neutral stand-ins for the linkable subset so
the port compiles; they are explicitly **not** ports, and `sfstub_hits[]`
counts calls so tests can detect reliance on them.


## Simulation port — first run of the original physics (2026-08-15)

`bin/test_faithful_sim` (built from `tools/test_faithful_sim.c`) sets up the
original globals the way `restunts.c` does and drives `player_op()` directly.
**The vendored physics runs 40 ticks on DEFAULT/COUN without crashing.**

Two setup facts recovered on the way, both from `restunts.c`:
* `SIMD.aerorestable` is a **pointer**, not table data. `setup_aero_trackdata`
  (restunts.c:716) points it at the trakdata aero slice and fills that slice
  with `(aero_resistance * i * i) >> 9` for i in 0..0x3F. Copying the raw
  `simd` resource alone leaves it dangling — that was the first crash.
* `steerWhlRespTable_ptr` selects between `steerWhlRespTable_10fps` and
  `_20fps` by `framespersec` (restunts.c:457). Both tables are transcribed in
  `sfdata.c`; `grassDecelDivTab` = {0xFF,0x100,0xC0,0x80,0x40} likewise.

### The car does not move yet — and the stub counters say exactly why

Car data loads correctly (5 gears, ratios 33339/23194/16516/12086/8287, torque
curve, idle torque 51), the throttle bit is decoded (`(input & 3) == 1`),
`car_is_accelerating` goes to 1 and all four wheels report ground contact —
but speed stays 0 and RPM stays at idle.

`sfstub_hits[]` over 40 ticks:

| Stub | Calls | Note |
| :--- | ---: | :--- |
| `carState_rc_op` | 160 | 4 per tick — the per-wheel contact response |
| `detect_penalty` | 40 | 1 per tick |
| `sub_18D60` | 40 | 1 per tick |
| `bto_auxiliary1` | 40 | 1 per tick |
| `audio_unk3` | 40 | sound only, harmless |

`carState_rc_op` returning a constant 0 means every wheel reports no reaction
force, so `update_player_state` never converts engine torque into motion.
It is called four times per tick, once per wheel, which matches its role.

**Next blocking work: `carState_rc_op` (193 asm lines) and very likely
`bto_auxiliary1` (426 lines), which resolves the track object under a point.**
`update_grip` and `upd_statef20_from_steer_input` are already translated
(`sfasm_port.c`) and compile clean.


## Original physics driving (2026-08-15)

`carState_rc_op` (193 asm lines) is translated in `sfasm_port.c` and its stub
removed. It was **not** the reason the car stayed still — the agent verified
the function converges sensibly and falsified that hypothesis.

The real cause was in the harness: `update_car_speed` computes drive as
`(car_gearratioshr8 * torque) >> 4`, and `car_gearratioshr8` (gear ratio >> 8)
was left at 0 by the improvised setup, so torque was multiplied by zero. The
harness now transcribes `init_carstate_from_simd` (restunts.c) in full rather
than improvising the initial CARSTATE.

**Result — DEFAULT / COUN, throttle held, 40 ticks (2 s):**

| tick | Z | speed | rpm |
| ---: | ---: | ---: | ---: |
| 1 | 5632 | 156 | 800 |
| 10 | 5644 | 1560 | 800 |
| 20 | 5678 | 3120 | 1587 |
| 30 | 5735 | 4680 | 2380 |
| 40 | 5815 | 6315 | 3212 |

Speed is in 256ths of mph, so tick 40 is **24.7 mph after two seconds** in
first gear — a plausible standing start. RPM stays at idle until the wheel
speed couples, then rises with it. For contrast, the hand-written
`src/sim/stunts_sim.c` reached 20697 (80 mph) in the same two seconds and ran
off the map.

Remaining stubs, all 1 call/tick and none blocking motion: `detect_penalty`,
`sub_18D60`, `bto_auxiliary1`, `audio_unk3`.

Still outstanding before this can claim fidelity: the four remaining stubs,
`track_setup` (so lap/checkpoint/penalty tables are real), and — the actual
point — verification against `repldumo.exe` GAMESTATE dumps from DOSBox.
Nothing here has been compared to the oracle yet.


## Step 1 complete: integrated native port (2026-08-15)

`bin/stunts_native` (built by `tools/build_native.sh`, source
`src/main_native.c`) runs the vendored original simulation and the vendored
original renderer together.

**The translation layer is gone.** `player_op()` writes
`struct GAMESTATE state` and `update_frame()` reads it — the same globals, as
in the original. `src/render_faithful/rbridge.c` and the hand-written
`src/sim/` are no longer linked, and with them go all four convention
mismatches documented above (position scale, track row order, start-line
element code, SIMD parse): there is nothing left to mismatch.

Verified headless (`--headless N`, which also dumps
`tests/render_native/native_drive.bmp`): DEFAULT/COUN, throttle held, the car
accelerates from the start line to 6315 (24.7 mph) over 40 ticks while the
renderer produces a correct chase-camera scene — car on the road, start gate
behind, lane markings and barrier post in place.

Interactive: arrow keys / WASD steer and brake, Shift and Space shift gears,
`C` cycles the camera, Esc quits. 20 Hz simulation, 60 fps presentation.

Remaining stubs are unchanged and still 1 call/tick: `detect_penalty`,
`sub_18D60`, `bto_auxiliary1`, `audio_unk3`.

**Not yet verified against the oracle.** The trajectory looks plausible; that
is not evidence. Step 2 is building the DOSBox `repldumo.exe` comparison.


## The oracle exists (2026-08-15)

`bin`-less but real: `tools/build_oracle.sh` + `tools/run_oracle.sh` produce
`build/oracle_run/DEFAULT.BIN` — a frame-by-frame dump of `struct GAMESTATE`
from the **original 16-bit game code**, which is the ground truth this project
has been missing since the start.

**How, given `bcc` is unusable.** The bundled Borland compiler needs
`32RTM.EXE`, which is absent and not legally obtainable, so `repldump.c` can
never be compiled here. But a probe link of the 43 original assembly segments
reported exactly one undefined symbol — `STUNTSMAIN`. The oracle therefore
reduced to writing that single function. `tools/oracle/repldrv.asm` (719 lines,
tracked; `build_oracle.sh` copies it into the gitignored build tree) is a hand
transcription of `repldump.c`'s `stuntsmain()` plus its `RESTUNTS_ORIGINAL`
helpers (`fopen`/`fclose`/`fwrite` via INT 21h, `init_row_tables`,
`init_trackdata`) into TASM. Every calling convention was verified against a
concrete site in the disassembly rather than assumed — notably `seg000.asm`'s
`ported_stuntsmain_`, the pre-rename original of this very function.

`repldumo.exe` ends in the game's `fatal_error` screen, whose `flush_stdin`
actually *waits for a keypress*; `run_oracle.sh` watches for a stdout marker
written after `fclose` and stops DOSBox once the dump is closed on disk.

**Verification of the dump (DEFAULT.RPL, the recording shipped with the game):**

| Check | Result |
| :--- | :--- |
| Header frame count | 10646, matches DEFAULT.RPL |
| File size | 11,923,522 = 2 + 1120x10646 exactly |
| Distinct records | 10646 / 10646 |
| `game_frame` | 1..10646, +1 per record |
| `game_travDist` | monotonically non-decreasing |
| Determinism | two DOSBox sessions, two builds: byte-identical |
| Input-dependence | a 20-frame replay yields records byte-identical to the first 20 |
| Trajectory | 1.7 mph standing start, gear 0 (airborne) mid-lap with revs decoupled from ground speed, finishes near the starting X — a completed lap |

**Honest limits.** There is no third-party known-good dump to diff against,
because the reference `repldumo.exe` cannot be built here; confidence rests on
the transcription matching the disassembly, the size arithmetic, determinism,
input-dependence and physical plausibility. Two regions carry no signal on this
replay and must not be counted as agreement by a diff harness:
`game_longs1/2/3` (0x000-0x120) are zero in every frame, and `opponentstate`
(0x222-0x2F2) is written once and never changes because DEFAULT.RPL has no
opponent. Note also `playerstate` begins at **0x152 = 338**, not 340 as the
older tooling assumed (`structs.inc`: `game_frame` at 0x140, `game_jumpCount`
at 0x150, `CARSTATE` = 0xD0; 0x152 + 2*208 = 0x2F2 closes exactly).


## First real oracle measurement (2026-08-15)

`tools/dump_native_states.c` runs the vendored simulation over a replay and
writes the same 1120-byte records as the oracle; `tools/diff_oracle.py` diffs
them. The replay carries everything the run needs — GAMEINFO, the element map,
the terrain map, then one input byte per frame — so the harness loads the track
from the replay exactly as `file_load_replay()` does. Note DEFAULT.RPL is
**PMIN on HELL5**, not COUN on DEFAULT.

**Run 1 — from our own start position: useless, and it found a bug.**
Our start-line search looks for element codes 0x01..0x03 ("fini"). HELL5
contains none of them, so it fell back to tile (15,15) and every frame差ered.
The start-line rule derived from DEFAULT.TRK does not generalise; the real
answer lives in `track_setup` (seg004.asm, unported), which computes
`startcol2`/`startrow2`.

**Run 2 — seeded from the oracle's own frame 1**, which isolates the per-frame
physics from the unported init path:

| Measure | Result |
| :--- | :--- |
| `playerstate` byte-identical | **frames 1-12** |
| First `playerstate` divergence | frame 13, in `field_48`, `car_trackdata3_index`, `car_whlWorldCrds2[]` |
| Position / rotation / speed identical | **through frame 62** |
| First positional divergence | frame 63 (`pos.y` 546 vs 512, `rot.y` 3 vs 0, `rot.z` 5 vs 0) |

**Reading.** The vehicle dynamics reproduce the original *bit-exactly for 62
frames* (3.1 s) — drivetrain, grip, steering and integration all agree. What
diverges first is not physics but **track bookkeeping**:
`car_trackdata3_index` is the track-segment index, maintained via `sub_18D60`
(stubbed) against the `trackdata3`/`td01`/`td02` tables that `track_setup`
(unported) would have built; ours are filled with the -1 sentinel. At frame 63
the oracle picks up pitch, roll and elevation while we stay flat — the car is
meeting track geometry our stubbed `bto_auxiliary1` cannot report.

So the measurement names the next work precisely, and it is not the physics
core: **`track_setup` (1764 lines), `bto_auxiliary1` (426), `sub_18D60` (383)**.

Caveat on any headline percentage: a naive byte comparison scores ~72% even
with a completely wrong starting position, because much of the record is zero.
`diff_oracle.py` therefore excludes `game_longs1/2/3` and `opponentstate` and
reports first-divergence per field instead of leaning on a single number.


## Two real defects found while measuring (2026-08-15)

Translating `bto_auxiliary1` (426 asm lines) and `sub_18D60` (383) changed the
oracle numbers not at all — and that turned out to be the *correct* outcome,
while the attempt surfaced two genuine bugs that mattered more.

**1. `bto_auxiliary1` is correct but inert on this replay.** It is called 892
times and returns 0 every time. Instrumentation shows the tiles it resolves
have `ss_physicalModel` 0x18/0x19/0x1A/0x00 (bank entrance, banked road, banked
corner), while its table dispatch only selects for physicalModel in
{0x0B, 0x12, 0x20-0x23, 0x47-0x4A} — none of which occur on HELL5. Its
multi-tile filler branch fires 773 times and resolves correctly, so the code is
genuinely exercised; it simply cannot influence this recording.

**2. `sub_18D60` is data-blocked, not translation-blocked.** It walks
`td17_trk_elem_ordered`/`trackdata18`/`td21`/`td22` — all filled at run time by
`track_setup()` — and dereferences `trkObjectList[].ss_trkObjInfoPtr`, which
still holds raw unrelocated dseg offsets because the `shapeinfos` table is out
of scope for the port. It segfaults on its first call. The translation is
committed with an explicit `[BLOCKED]` guard at entry that returns the same
neutral 0 the stub did; **delete the guard as soon as track_setup and
shapeinfos exist**, and re-measure (task #9).

**3. The port was non-deterministic — now fixed.** Three runs of the same
binary produced three different dumps, first differing around record 883-909,
which made any comparison past ~880 frames meaningless. Two causes:

* *Heap overread in the asset decompressor.* `dsi_decomp_vle` advanced its
  read pointer with no bounds check; on the final symbol the bit-buffer refill
  runs once more than needed and reads one byte past the payload (caught by
  AddressSanitizer at `stunts_dsi_unpack.c:225`, decompressing GAME.PRE). In
  DOS that byte was harmless heap; here it is garbage. All eight reads now go
  through a `VLE_RD()` guard that yields 0 past the end — the consumed byte
  sequence is unchanged.
* *Uninitialised locals.* The vendored code inherits the original's habit of
  reading stack locals before writing them. DOS had predictable stack garbage;
  we do not. Building with `-ftrivial-auto-var-init=zero` makes three runs
  byte-identical. The flag is now in `tools/build_native.sh`,
  `build_faithful_frame.sh` and `build_faithful_app.sh`.

  This is a scaffold, not fidelity: zero is not necessarily what DOS held.
  Where an uninitialised read actually affects behaviour, the fix is to find
  it and reproduce the original's value. Determinism just makes that findable.

**Oracle numbers after all of the above (unchanged, as expected):**
`playerstate` byte-identical for 12 consecutive steps; first positional
divergence at oracle record 63. Both remain gated on `car_trackdata3_index`,
i.e. on `track_setup`.


## Extracting track_setup's output instead of porting it (2026-08-15)

Before spending 1764 lines translating `track_setup`, we tested whether it is
really the remaining gap — by taking its *output* from the oracle. `repldrv.asm`
now writes `TRAKDATA.BIN` immediately after `track_setup()` returns: the 8 dseg
bytes at `startcol2` (which also cover `hillFlag` and `startrow2`), then
`track_angle` as a word, then the raw 0x6BF3-byte trakdata chunk from
`td01_track_file_cpy`. `tools/dump_native_states.c` loads it and repoints
td01/td02/trackdata3/td08/td09/td10/td14/td15/td17/td18/td19/td21/td22/td23
into the blob using `init_trackdata()`'s carve-up.

(Two DOS gotchas cost a round each: the string literals live in the CODE
segment while `dos_fopen` reads its arguments through DS — the existing code
copies them to stack scratch first, and so must ours; and `track_angle` sits
far from the other scalars in dseg, so it needs its own write. The values are
`startcol2=29, startrow2=25, hillFlag=0, track_angle=512`.)

**Result — cold start, no seeding from the oracle:**

| | before | with real trakdata |
| :--- | :--- | :--- |
| start position | wrong tile (15,15 fallback) | **x exact: 1931008** |
| pos/rot/speed identical | 62 frames *(seeded)* | **63 frames (cold start)** |
| run length before crash | full | 160 frames |

The car now starts where the original starts, derived from the original's own
formula and data, and tracks the oracle's position, rotation and speed exactly
for 63 frames without being handed any state.

**Two blockers remain, both now precisely identified:**

* **Frame 64** — `pos1`, `rotate`, `trackdata3_index`, `field_48`, `rc2` and the
  wheel world coordinates all diverge together. Same divergence point as
  before, so the real trakdata did not move it: the cause is elsewhere.
* **Frame ~160 — segfault in `sub_18D60`.** With td17/td18 non-NULL its
  `[BLOCKED]` guard correctly steps aside and the function finally executes,
  then faults on `trkObjectList[].ss_trkObjInfoPtr`, which still holds raw
  unrelocated dseg offsets because the `shapeinfos` table (dseg 0x8021,
  120 x 14 bytes) is not part of the port. **`shapeinfos` is the next blocker,
  and it is data, not code.**

Also note frame 1 already differs in 20 bytes — `field_48`, `whlWorldCrds2[]`
and neighbours — even though every physical quantity matches. That is
`init_carstate_from_simd` versus the original's fuller init path, not physics.


## Frame 64: located, not yet solved (2026-08-15)

What the oracle does at frame 64 is now unambiguous — the car drives onto a
banked surface and starts climbing:

```
frame   Y     rot.x rot.y rot.z
 63    512     512    0     0
 64    546     511    3     5
 66   1005     509   27    14
 70   1994     512   22    18
```

Our car stays at Y=512 with rot (512,0,0).

Instrumenting `build_track_object` (temporary `dbg_*` globals in
`rframe_helpers.c`, printed by `dump_native_states.c` under `STUNTS_PROBE=1`)
shows the tile lookup is **correct**: tile (29,3), `ss_physicalModel = 0x18`
(bank road entrance A), and the ported dispatch does reach
`code_bto_bankEntranceA`. It sets `terrainHeight = 2` and `planindex = 142`,
constant across frames 59-71.

Collision plane 142, read straight out of GAME.PRE, is:

    angle_yz=0  angle_xy=0  origin=(-120,0,517)  normal=(0,8192,0)

`normal=(0,8192,0)` is straight up — a **flat** plane. So the plane our code
selects cannot tilt the car, which matches the symptom exactly. Either the
original selects a different plane at this point, or the bank surface is
applied from somewhere other than `planindex`.

**Next concrete step:** `planindex` is a global and therefore absent from the
GAMESTATE dump, so we cannot yet compare it against the original. Adding it
(and `terrainHeight`) to `TRAKDATA.BIN`-style per-frame oracle output would
settle in one run whether our plane selection or the plane application is
wrong. That is the same extract-rather-than-port trick that already paid off
twice.

Note this is *not* blocked on `track_setup` — the real trakdata is loaded and
did not move frame 64 — nor on `bto_auxiliary1`, which correctly returns 0 for
physicalModel 0x18.


## Track heightmap was read one byte early (2026-08-15)

Found from user play-testing — "the track didn't look like the standard track,
and the car didn't meet the hill correctly; the camera went haywire".

`stunts_load_track()` read the 1802-byte .TRK as two **900**-byte maps. It is
two **0x385 (901)**-byte maps: that is the layout `init_trackdata()` carves out
(td14, +0x385, td15), and it is why `file_load_replay()` can read a replay
straight into td13 and have the maps land in place. 900 of each 901 are the
30x30 grid; the last byte is padding.

Consequence: element codes were right but every terrain height was shifted one
tile, so road pieces and terrain no longer lined up. DEFAULT.TRK read from
offset 900 puts hills at (11,4),(11,5),(11,6)... plus a spurious height at
(0,0); from 901 they sit at (11,3),(11,4),(11,5)... The car therefore met a
slope where the road said flat, and the chase camera — which clamps to
`terrainHeight` — followed it.

Fixed in `src/asset/stunts_asset_loader.c`. This affected the playable build
and every renderer test that loaded a .TRK; it did not affect the oracle
comparison runs, which take their maps from inside the replay file (already at
the correct 0x385 stride).


## Self-testing loop, and two real bugs it found (2026-08-15)

The user asked why they had to play-test at all. They did not: both sides can
be driven without a human.

* Our port: `bin/dump_native_states <data> <rpl> <out> [seed seedframe] [trakdata]`
  replays a .RPL and writes one 1120-byte GAMESTATE per frame.
* The original: it cannot be driven interactively, but it *replays* — and a
  .RPL can be **synthesised**. The format is `26-byte GAMEINFO + 0x385 element
  map + 0x385 terrain map + one input byte per frame`; the maps come straight
  out of a .TRK, which has the same 901+901 layout.

`build/oracle_run/HILLTEST.RPL` is exactly the user's drive: COUN on DEFAULT,
throttle held, 900 frames. `tools/run_oracle.sh HILLTEST` produces the
original's per-frame state for that same drive, and it is then a plain diff.
Any "what should happen here?" question is now one build away.

### Bug 1 — the .TRK heightmap (see previous section)

Play-testing found it; the oracle runs could not, because they take their maps
from inside the replay, where the stride was already right.

### Bug 2 — `sar16` was not an arithmetic shift

`sfasm_port.c`'s helper read

    return (int16_t)(v < 0 ? ~((~(uint16_t)v) >> n) : ((uint16_t)v >> n));

which is wrong in C: a `uint16_t` is **promoted to int** before `~`, so the
complement is taken over 32 bits and the shift drags the wrong bits down.
`sar16(-14247, 1)` returned 25644 instead of -7124, and a second shift turned
that into **12822** — the exact five-figure suspension force seen in
`car_rc2[]` when the car reached the hill. Found by watchpoint
(`watchpoint set expression -w write -- &state.playerstate.car_rc2[0]`), which
put the write inside `carState_rc_op` even though every path there clamps to
+-0x180.

Now shifts the unsigned value and fills the vacated high bits by hand; ten unit
cases pass. `sar32` was fine — `uint32_t` is not promoted.

This helper is used throughout the translated physics, so it was corrupting
values well beyond this one symptom.

### Where the hill stands

With the real trakdata loaded (correct start position) and both fixes in:

| | frames |
| :--- | ---: |
| position + all three rotations + speed identical to the oracle | **247 consecutive** |

247 frames is 12.35 s of bit-exact agreement — the flat-road physics is right.
The car still fails at the hill entry (oracle frame 249: Y=1363, pitch 49;
ours: Y=0, pitch 38 — our car sinks to ground level instead of climbing the
ramp), so at least one more defect sits in the hill-entry ground query.
`sub_18D60` remains blocked by default (`STUNTS_ALLOW_18D60=1` to enable, which
still segfaults on the absent `shapeinfos`).


## The hill: cause identified (2026-08-15)

Prompted by "what have others done?", two things paid off immediately.

**1. `reference/restunts/docs/format.html` documents the .TRK format** and
independently confirms the heightmap fix: bytes 0-899 are items, **byte 900 is
the skybox** (0=desert, 1=tropical, 2=alpine, 3=city, 4=county — useful later,
we currently hardcode it), and bytes 901-1800 are terrain, *"ordered from left
to right and from bottom to top"* — which is exactly why the element map is
indexed through `trackrows[]` and the terrain map through `terrainrows[]`.
Terrain codes 0x00-0x12 are shapes, not heights: flat at sea level (0x00),
flat elevated (0x06), and slopes/diagonals between them (0x07-0x12). That
matches `hillHeightConsts = {0, 450}` — only two levels exist.

**2. `reference/restunts/src/restunts/status.html`** shows `build_track_object`
at **2208 lines with 82 callers and no PORTED marker** — restunts never ported
it either. Our version is a one-pass machine translation of ~2700 asm lines, so
it was the prime suspect.

**It is not the culprit.** The oracle now also writes `GLOBALS.BIN` — a
per-frame snapshot of `terrainHeight`, `planindex`, `wallindex` and
`current_surf_type` — and `dump_native_states` writes the same record. Over the
HILLTEST drive the two agree **exactly for 248 frames**, including picking
slope plane 12 on the hill and terrainHeight 452 on the plateau. That 2700-line
translation is essentially right.

**The actual first divergence is frame 247, in `field_48`,
`car_trackdata3_index`, `car_vec_unk3/4/5` and the wheel coordinates — which is
precisely the set of fields `sub_18D60` writes.** That is the function still
disabled by the `[BLOCKED]` guard, because it dereferences
`trkObjectList[].ss_trkObjInfoPtr`, which holds raw unrelocated dseg offsets:
the `shapeinfos` table (dseg 0x8021, 120 x 14 bytes) is not part of the port.

So the hill failure traces to a function we knowingly stubbed, and unblocking
it is a **data** problem — extract `shapeinfos`, relocate the pointers — the
same trick that already worked for the dseg tables, the trakdata chunk and
these globals. `STUNTS_ALLOW_18D60=1` enables the translated function once the
data exists.

## The documentation pass, and what it unlocked (2026-08-15)

Reading every reference document before touching code (the user's instruction)
paid for itself immediately.

### What the docs actually say

* `docs/stunts.txt` [8.4]/[8.5] and `docs/format.html` **contradict each other**
  on map ordering. stunts.txt says items run bottom-to-top and terrain
  top-to-bottom; format.html says the opposite. Our conventions
  (`trackrows[i] = 30*(29-i)` for elements, `terrainrows[i] = 30*i` for terrain)
  are the ones the oracle agrees with, so the code stands and format.html is
  wrong on this point. Both agree the skybox byte is at 900 and terrain starts
  at 901 — the off-by-one the user's play-test found.
* Terrain codes are 0x00–0x12 only, with **two elevation levels**: 0x06 is flat
  elevated, 0x07–0x0A are the four slope orientations, 0x0B–0x12 diagonal.
* `docs/game.res.txt` documents `plan` (536 × 34-byte PLANE: angleYZ, angleXY,
  origin, normal, 3×rotation-matrix vector) and `wall` (angleXZ, x, z). It also
  states plainly that **where each surface starts and ends is not in GAME.RES**
  — it is in the executable. That is exactly what `build_track_object` and
  `shapeinfos` encode.
* `docs/flow.txt` gives the real call order and names `Trakdata1_2_17_21_22`
  as the routine that "returns something based on the position".
* `src/restunts/status.html`: 173 of 617 functions PORTED, 154 IGNORE. So
  restunts itself ported ~28%; the parts we needed were never among them.

### The unlock: restunts2 stores the missing table symbolically

`shapeinfos` (120 × TRKOBJINFO) is what `sub_18D60` reaches through
`trkObjectList[].ss_trkObjInfoPtr`. restunts1 stores it as 1680 anonymous `db`
bytes whose pointer fields are bare dseg offsets — unrelocatable. **restunts2's
Ghidra export stores the same bytes as `TRKOBJINFO <...>` records whose
si_cameraDataOffset is a symbol** (`shapedata174`, `shapedata42_2`, …).

`tools/extract_shapeinfos.py` parses the structured form, cross-checks all
120 × 12 non-pointer bytes against restunts1's raw image (identical), resolves
the 30 surface blobs plus the six entries that reuse si_opp1/si_opp2 as a
pointer (all → `shapedata84`/`_2` + 42), and emits `src/sim_faithful/
sfshapeinfo.c`. Its index map agrees with rdata.c's independent decode on all
215 trkObjectList entries.

Two deviations, both documented in the generated file and measured:
* 48 trkObjectList entries had dseg offset 0 (blank tile, ghost cars). DOS reads
  real bytes there; we point them at a zeroed block. `shapeinfo_null_hits`
  counts every hit — **0 over the whole 900-frame replay**, so it is unobservable.
* `struct TRKOBJINFO` is `#pragma pack(1)`, so Mach-O cannot relocate its
  pointer field. Pointers are filled in by a constructor, as rdata.c already
  does for `ss_shapePtr`.

### detect_penalty was the real blocker, and it is not about penalties

With shapeinfos in place `sub_18D60` runs, but nothing improved. The reason:
`state.c` only advances `state.field_2F2` — the car's progress along the track —
through `detect_penalty`'s out-parameter, and that was still a stub returning 0.
The car therefore never moved forward through the element list, and every
downstream lookup got the same stale index.

`detect_penalty` (restunts2 seg001.asm 5267–5538, 223 instructions, no calls)
is a depth-first walk of the track graph with an explicit stack. It also
maintains `game_startcol/startcol2/startrow/startrow2`. Everything it touches
already existed in the port, so it translated directly (`sfasm_port.c`).
One deviation: it clears its `visited[]` array only below `track_pieces_counter`,
a dseg word `track_setup()` fills and we do not have; we clear all 0x385, which
is a superset and cannot be observed.

`init_game_state` also mirrors the start tile into GAMESTATE
(`restunts.c:537-540`); the harness did not, so those four fields differed from
frame 1. Fixed.

### Measured result

| Field | before | after |
| :--- | ---: | ---: |
| `field_2F2` (track progress) | never advanced | identical through frame **247** |
| `car_trackdata3_index` | diverged frame 106 | identical through frame **251** |
| `field_48` | diverged frame 107 | identical through frame **247** |
| `game_startcol` | differed from frame 1 | **identical all 900 frames** |
| `car_vec_unk3/4/5` (surface points) | diverged frame 107 | identical through frame **250** |
| position / rotation | diverged frame 247 | diverged frame **248** |

Everything `sub_18D60` writes now matches the original right up to the hill.

### Where it still breaks, precisely

Frame 248, the moment the front wheels reach the ascent tile:

    hjul 0: tile(7,11) terrain= 7 elem   4->182 model=0x01 terrainHeight=2
    hjul 2: tile(7,10) terrain= 0 elem   4->  4 model=0x01 terrainHeight=2

    car_whlWorldCrds1.y   original [8, 8, 2, 2]     ours [-342, -342, 2, 2]

The tile lookup, the terrain code, and the `subst_hillroad_track(7, 4) -> 0xB6`
substitution are all correct — 0xB6 is element 182, whose `ss_physicalModel`
is 1 in both restunts2's dseg and our rdata.c. The front wheels should rise
from 2 to 8; ours are driven to -342, and `stateply.c:2156` then clamps the
averaged body height to 0, so the car sits on the floor instead of climbing.

`build_track_object`'s terrain switch handles codes 0–6 and lets 7–0x12 fall
through as flat, which is faithful — the slope is supposed to come from the
substituted element's physical model. But model 1 is `code_bto_road`, which
only sets `current_surf_type` and touches neither `planindex` nor
`terrainHeight`. So on our side a hill-road tile yields a flat surface.

**Next concrete step:** the oracle's `GLOBALS.BIN` records only the *last*
`build_track_object` call of each frame, and the simulation makes six per frame
(one per wheel plus two more). That is not enough to tell which plane the
original hands the front wheels on the ascent tile. Extending `repldrv.asm` to
dump `planindex`/`terrainHeight`/`current_surf_type` **per call** rather than
per frame would answer it directly. `tools/build_dumper.sh` and the
`STUNTS_BTO=<frame>` probe in `dump_native_states.c` already print our side in
that form.

## The hill bug, found and fixed (2026-08-15, later the same day)

Three hypotheses went in (wrong quartet variant / wrong boundary timing / slope
terrain treated as flat). All three died in one afternoon of measurements, in
the best possible way:

* **Test 1** (pure arithmetic, no emulator): the oracle's own wheel positions
  at frames 248–250, evaluated against all 536 planes from GAME.PRE, land
  exactly on **plane 12** for the front wheels and plane 0 for the rears —
  8.6→8, 24.8→24, 41→41 up the whole slope.
* **Test 2** (per-call instrumentation): our `build_track_object` picks
  **exactly the same planes**. Selection was never the problem.
* A probe on `plane_origin_op`'s inputs then showed the real fault in one
  line of output: `c(7680,0)` — **`elem_zCenter` was 0** instead of 11776.

Root cause: the original's `init_row_tables` (restunts.c:237) fills **eight**
tables; our two harness copies of that loop filled **six**. `terrainpos` and
`terraincenterpos` are zero-initialised in dseg and only get their values at
runtime, so `elem_zCenter = terraincenterpos[row]` stayed 0 forever. Planes
0–3 short-circuit in `plane_origin_op` before touching the element centre,
which is why every flat surface behaved perfectly and everything sloped
(hills, ramps, banked corners — every plane index ≥ 4) misbehaved, including
the bank-entrance camera chaos documented earlier.

Second, smaller find, exposed the moment the hill worked: `wallptr` was never
loaded (the original does `locate_shape_alt(gameresptr, "wall")`,
restunts.c:799). The hilltop's walled edge dereferenced NULL at frame ~304.
Both harnesses now load the `wall` resource beside `plan`.

**Result on HILLTEST.RPL (the user's own 45-second drive, 900 frames):**

| Field group | Agreement with the original |
| :--- | :--- |
| position (x,y,z) | **identical, all 900 frames** |
| rotation (heading, pitch, roll) | **identical, all 900 frames** |
| speed, rpm, gear, gear ratio | **identical, all 900 frames** |
| surfaces per wheel | **identical, all 900 frames** |
| playerstate as a whole (0x152–0x221) | **0 differing bytes over 900 frames** |
| track progress / start fields | identical |
| remaining diffs | opponent scratch, audio state, UI timers, random seed — init-value differences outside the car |

The playable app inherits the fix (same two loops, same wall load) and now
crests the hill onto the 452-high plateau in a headless run. The penalty/path
functions remain guarded off there until `track_setup` is ported, which
affects penalties and AI targeting but no car physics.

Verification commands:

    bash tools/build_dumper.sh
    ./bin/dump_native_states build/oracle_run build/oracle_run/HILLTEST.RPL out.bin "" 1 build/oracle_run/TRAKDATA.BIN
    python3 tools/diff_oracle.py build/oracle_run/HILLTEST.BIN out.bin

## Phantom ramps: the 22-byte stride bug (2026-08-15, evening)

The user's play-test after the hill fix reported ramp shapes rendered all over
the track that the car ignores. Their screenshots plus one correction from
them (the beige-posts-and-black-mesh look is the *elevated road*, a real
element) narrowed it fast; a probe logging every shape queued for drawing
found the smoking gun on the plateau tiles: shape **24 ("ramp")** queued
beneath every road tile on raised ground, where the original queues shape
**43 ("high"**, the elevated ground block).

Root cause, `frame.c`:

    currenttransshape->shapeptr = &game3dshapes[0x3B2 / sizeof(struct SHAPE3D)];

`0x3B2` is a DOS **byte** offset into `game3dshapes`, where `SHAPE3D` is 22
bytes: 946/22 = 43. Our struct holds native 8-byte pointers, so the same
expression divides by a larger size and lands on 24 — the ramp. Every element
sitting on elevated terrain therefore got a phantom ramp drawn under it.

Five such hardcoded divisions existed, all now `/ 22` with comments:

| site | DOS index | drew instead | visible as |
| :--- | ---: | ---: | :--- |
| frame.c:775 ground block under elevated elements | 43 "high" | 24 "ramp" | phantom jumps everywhere on raised ground |
| frame.c:947 player car verts (steering) | 126 | 72 | front-wheel steering visuals |
| frame.c:1001 opponent car verts | 127 | 72 | same, opponent |
| frame.c:1032/1053 shape 0x98A | 111 | 64 | (site to be identified visually) |

Verified after the fix: the drawing queue shows 43 where 24 was; the crest
mesh band and plateau phantoms are gone from rendered frames; and the
900-frame physics identity against the oracle is unchanged.

Lesson recorded: any dseg byte-offset arithmetic ported into C must divide by
the DOS struct size, never `sizeof` of the modernised struct. `grep` for
`sizeof(struct` over vendored code when a "wrong object drawn/read" symptom
appears.

## Loops ("driving upside down"), root-caused (2026-08-16)

User report: the car glitches exactly when inverted in a loop and never makes
it around, at what should be sufficient speed.

Method: synthetic replays on modified DEFAULT maps, oracle vs port.

* **TUBETEST** (pipe on the start straight): pipe passage **bit-identical**.
  (A straight car rides the pipe floor and never inverts — wrong element for
  the symptom.) Bonus find: both cars later bounce off the plateau's slalom
  blocks, at slightly different positions — logged as a follow-up lead.
* **LOOPTEST** (loop, filler-tile side first): the ORIGINAL crashes too — the
  2-field loop is directional, entry must be on the ID tile. Our port matched
  the crash bit-for-bit, wreck dynamics included.
* **LOOPTST2** (loop, ID side first): the original goes around (apex y≈590,
  inverted at top). **Our port is bit-identical on every physics field for
  all 900 frames** — full inversion included.
* **LOOPTST2 without trakdata** (= exactly the playable app's configuration,
  where track_setup's tables do not exist and sub_18D60/detect_penalty are
  guarded to neutral): the car climbs to y≈257 — where the wall passes
  vertical — loses the surface and falls. Reproduces the user's symptom
  precisely.

Conclusion: the upside-down physics is exact; the app fails loops because
`sub_18D60`'s path data (car_vec_unk3/4/5 from td17/td18/td21/td22) feeds the
special-surface handling, and the app has no track_setup to build those
tables. The fix is task #10: port track_setup, byte-verify its output against
the oracle's TRAKDATA.BIN, then drop the scaffold guards.

Also noted: oracle harness runs fail if a synthetic track is not a closed
circuit (track_setup's path walk); build tests by modifying DEFAULT instead.

## track_setup ported: the port now builds its own track (2026-08-16)

`track_setup` (restunts2 seg004.asm 4008-5764, 1484 instructions) is translated
in `src/sim_faithful/sftrack_setup.c`. It was the last big unported piece and
the reason loops failed in the playable build.

It walks the track as a *path* rather than row by row: from the start tile it
follows each element's exit point to the next tile, pushing branch points onto
a 64-entry scratch stack so alternative routes are explored afterwards. Out of
it come the thirteen tables the rest of the game reads — the successor/branch
lists, the element and tile per path index, the connectivity nibbles, the
opponent's checkpoints and racing line, plus startcol2/startrow2/hillFlag/
track_angle.

Data it needed, none of which existed before: the four terrain-connectivity
tables, `byte_3E71E`/`byte_3E724`, and the `track_pieces_counter`/`byte_45635`/
`byte_45D90`/`byte_45E16`/`byte_4616E` scalars. `trackdata6`/`trackdata7` were
declared but never defined. `sfdata_init_trackdata()` now carves the one
0x6BF3-byte trakdata block exactly as `init_trackdata()` (restunts.c:262) does,
so every slice exists.

**Verification — two complementary runs against the oracle's TRAKDATA.BIN**
(the real game's own tables for DEFAULT, dumped straight after its track_setup
returned), via `STUNTS_TS_VERIFY`:

| mode | result |
| :--- | :--- |
| `=1` zero-filled buffer | **0 differing bytes** across all thirteen tables |
| `=2` poisoned buffer | every table's first difference sits exactly at the index where the original stopped writing (128 path pieces, 8 checkpoints, 42 target points) |

Together those show both that every value we write is right and that we write
neither more nor less than the original. DEFAULT's 128-piece path includes a
loop, two pipes, a half-pipe, two pipe entrances, a hill road and a slalom, so
the special elements are covered.

Consequences:

* The two scaffold guards are **gone**. `sub_18D60` and `detect_penalty` now
  run unconditionally, as in the original.
* `dump_native_states` no longer needs the oracle's trakdata at all: running
  HILLTEST with nothing borrowed from the original still gives **identical
  position, rotation, speed, gear and per-wheel surfaces for all 900 frames**.
* The playable app derives its own start tile, angle and hill flag; the old
  "search for element 0x01..0x03" guess (which did not generalise - see the
  HELL5 note above) is deleted.
* Remaining stubs are only opponent-related or audio: car-to-car collision and
  speed adjustment, `state_op_unk`, `audio_unk3`.

An invalid track (for instance a hand-built one whose 2-field loop has its ID
and filler tiles the wrong way round) makes track_setup return a nonzero error
code and leave the tables short — the original behaves identically, which the
LOOPTST2 comparison confirmed byte for byte.

## Upside-down driving, tested autonomously (2026-08-16)

The user reported the car still misbehaves when inverted in a pipe. Synthetic
replays were built and driven through both the original and the port.

**Building a usable test took several iterations, each of which taught
something worth recording:**

* A synthetic track must remain a *closed circuit*: `track_setup` walks the
  path and fails with `no_path` (7) if it never returns to piece 0. Overwriting
  too much of DEFAULT's column 7 breaks the return leg.
* The terrain connectivity check rejects a partially flattened map
  (`terr_mism`, 11) - flatten the whole terrain or none of it.
* A 2-field element is directional: the filler tile must be the one the walk
  reaches first. LOOPTEST (filler south of the ID) is valid, LOOPTST2 is not -
  and the original rejects the invalid one exactly as we do.
* `z tile == file row`, while `trkRow == 29 - file row`. Getting this backwards
  put two loops where the car could not reach them.
* Steering can be synthesised: input byte bits are accel 0x01, brake 0x02,
  right 0x04, left 0x08. Sweeping the frame at which steering starts found a
  run that genuinely inverts the car.

**Results.** Four tracks, 900 frames each, our own track tables, nothing
borrowed from the original:

| test | what happens | car state vs original |
| :--- | :--- | :--- |
| HILLTEST | hill, plateau, jump | identical |
| LOOPRUN | ballistic arc off the plateau, apex y=590 | identical |
| FLATLOOP | flat run into a loop at 138 mph | identical |
| PIPEFLIP | pipe, steered up the wall, **rolls to 265°** | identical to frame 422 |

So inverted physics is exact. Two findings came out of it:

**1. A real divergence at PIPEFLIP frame 423** - the moment the car, rolled
about -152 degrees, touches down. `car_whlWorldCrds2[1].y` is 12 in the
original and 13 here (and [3].y is -12 vs -13); position and rotation then
differ by 3-19 units. Speed, rpm, gear and per-wheel surfaces stay identical
throughout, so this is a rounding difference in the wheel-transform geometry,
not the engine. `multiply_and_scale` matches restunts' C exactly, so the
culprit is further in - `mat_rot_zxy` / `vec_transform` are the next suspects.
This is the only genuine physics-side defect found in any test so far.

**2. tools/diff_oracle.py was hiding a gap.** It excluded
`game_longs1/2/3` as "dead - zero in every oracle frame". That is false
whenever the car crashes: the oracle has 102233 non-zero bytes there on
HILLTEST, and our runs have zero. The region holds 24 crash-debris particle
positions written by `state_op_unk` (seg001.asm 9206-9367, 138 instructions),
which calls `get_kevinrandom` and is **still a stub here**. The simulation
never reads it back - it is a visual effect - but the exclusion meant earlier
"identical for 900 frames" claims quietly skipped it. The script now counts
and prints both separated regions instead of dropping them.

Also confirmed: our loops fail the synthetic loop tracks in exactly the same
way the original does, frame for frame. Loops are not a port defect.

## All 39 shipped tracks, and the gear-change defect (2026-08-16)

The user's bar is "every track drivable", so all 39 `.TRK` files that ship with
the game were run through the port.

* **`track_setup` returns 0 on all 39** — 12 to 458 path pieces each.
* **No crashes**: each track driven 600 frames headless, all exit cleanly.

Six tracks richest in inversion elements (loops, pipes, half-pipes,
corkscrews) were then compared frame by frame against the DOS original,
driving on our own tables with nothing borrowed:

| track | car state vs original |
| :--- | :--- |
| HELL2 (46 special tiles) | **identical, 900 frames** |
| HELL (26) | **identical, 900 frames** |
| HELL4 (26) | diverges frame 361 |
| HEIZ! (27) | diverges frame 681 |
| HEIZ3 (22) | diverges frame 689 |
| MONSTER (21) | oracle run incomplete |

### The defect: speed is not lost on a gear change

All three divergences have the same signature, and it is not a collision
(`game_impactSpeed` stays 0 in both) nor the rev limiter (probed: `max_rpm`
8250, `braking_eff` 256, and it does not fire at those frames):

    HELL4 frame 360 -> 361, gear ratio changes 12086 -> 8287 in BOTH
      original: speed 37663 -> 35675   (-1988, -5.3%)
      ours:     speed 37663 -> 37662   (-1, ordinary drag)

`car_currpm` then follows each side's own speed consistently
(rpm = f(speed, gearratio) holds in both), so the engine model is fine — the
original simply loses about 5% of its speed *through the shift* and we do not.
Everything else at that frame (height, per-wheel surfaces, roll, pitch, gear,
jump count) is identical. HEIZ!/HEIZ3 show the same thing (-2598, -1956).

Next step is the gear-change completion path in `statecar.c`
(`loc_17B93`/`loc_17BDA`, where `car_changing_gear` clears and `car_gearratio`
is reloaded) compared against seg001's original — something there reduces
speed that the vendored C does not do.

Note this is a *different* defect from the inverted-touchdown wheel rounding
found in PIPEFLIP; both remain open.

### Reusable test assets

`build/oracle_run/` now holds synthetic tracks built for this: `LOOPRUN`
(valid loop, long runway), `FLATLOOP` (flat, loop at 138 mph), `PIPEFLIP`
(pipe with synthesised steering that rolls the car to 265 degrees), plus
`T_*.RPL` generated from shipped tracks. Replays are generated straight from a
`.TRK` — 26-byte GAMEINFO with the track name at offset 13, then the two
0x385 maps, then one input byte per frame.

## The gear-change bug: a signed/unsigned divide (2026-08-16)

The defect that broke the fast tracks turned out to be one line in
`update_car_speed` (statecar.c). The original:

    push mass as dword ; push 25 as dword ; push cwd(delta)
    call __aFlmul          ; SIGNED long multiply
    call __aFuldiv         ; UNSIGNED long divide   <-- !
    sar  ax, 1             ; only the LOW word reaches the shift

The vendored C had `(((int32_t)var_deltaSpeed * 0x19) / car_mass) >> 1`, i.e. a
*signed* divide. The two agree while `var_deltaSpeed` is positive, which is why
this never showed up at moderate speed. At high speed drag exceeds gravity, the
delta goes negative, and the original reinterprets the negative product as a
huge unsigned number - producing exactly the speed loss the car takes through a
gear change. The disassembly even remarks there is "no clear reason" to use the
unsigned routine here; it is a quirk of the original, and reproducing it is the
whole point.

The same block's `mul cx ; shr ax,4` is unsigned and drops DX, so the product is
truncated to 16 bits before shifting; that is now reproduced too.

Also tried and reverted: making `mat_mul_vector` truncate its product to the
high word after a 2-bit left shift, as `vec_transform_asm_` does. It changes no
observed result (the operands never reach the overflow range), so the simpler
`>> 14` from restunts' own C was kept.

### Result

| test | before | after |
| :--- | ---: | ---: |
| HILLTEST (hill, plateau, jump) | 16/16 | **16/16** |
| LOOPRUN, FLATLOOP (loops) | 16/16 | **16/16** |
| HELL2, HELL (loop/pipe/cork tracks) | 16/16 | **16/16** |
| HEIZ! | 2/16 | **16/16** |
| HEIZ3 | 3/16 | **16/16** |
| HELL4 | 5/16 | 14/16 |
| PIPEFLIP (inverted landing) | 10/16 | 10/16 |
| **PIPEROLL (986 degrees of roll in a pipe)** | - | **16/16** |

`PIPEROLL` is the case the user asked for: steering synthesised so the car
rolls right round the inside of a pipe, 986 degrees in total - nearly three
revolutions, well past inverted. Every physics field matches the original for
all 900 frames, and the accumulated rotation agrees to the decimal.

All 39 shipped tracks still build their path tables and drive 600 frames
without crashing.

### Still open

Two small residual differences, both rounding-scale and both at ground contact:

* HELL4 frame 374: the car starts to turn and x differs by 6 units out of
  21043 (0.03%); it persists but does not grow.
* PIPEFLIP frame 423: landing while rolled about -152 degrees, roll differs by
  3 units and two wheel coordinates by 1.

Neither is the gear bug (that is fixed and verified); both look like a further
rounding mismatch in the wheel/rotation geometry. `mat_mul_vector`,
`multiply_and_scale` and every `__aFuldiv` site have been checked and match.

## Experiment: rendering above 320x200 (2026-08-16)

The user asked how hard it would be to *try* 2x/4x. Answer, measured rather
than estimated: making it build and run took five small edits; making it look
right is a different matter.

`src/render_faithful/rfbsize.h` now holds one knob, `RFB_SCALE` (default 1,
passed by `tools/build_native.sh` via `RFB_SCALE=n`). At 1 every constant is
byte-identical to before — verified: the oracle diffs still give 16/16 on
HILLTEST, PIPEROLL and HELL2, and the rendered 1x frame is byte-identical to
the pre-change output. So the verified path is untouched.

What had to follow the resolution:

* `RFB_W`/`RFB_H` and the framebuffer in `rblit.c`
* the clip rectangle in `main_native.c` (was `{0, 0x140, 0, 0xC8}`)
* the `sprite_set_1_size(0, 0x140, ...)` calls in `frame.c`
* `var_798[480+480]`, the per-scanline edge buffer in `shape3d.c`
* the SDL window, which now divides by RFB_SCALE so the window stays the same
  physical size while the picture inside gets finer

One trap worth recording: `set_projection(i1, i2, i3, i4)` takes the horizontal
and vertical half-FOV in **degrees** as i1/i2 and the pixel dimensions as
i3/i4. Scaling i2 with the resolution doubles the vertical field of view and
squashes the world into the top of the screen. Only i3/i4 scale.

**Results**

* **2x (640x400): works.** Geometry, car, scenery and road all correct and
  visibly sharper - the edges are recomputed, not magnified. One artifact
  remains: ground either side of the road fills white instead of green, so
  some large background polygon is not covering the full height.
* **4x (1280x800): first attempt broken** - long horizontal streaks off every
  polygon edge. The user's description cracked it: *"it renders perfectly at
  the car and the nearest road, but outside that it drags or smears."*

That is a stride bug, not a precision limit. The scanline edge table is two
arrays back to back - left x per row, then right x per row - and the original
separates them by a fixed 480 words (`[di+3C0h]`). Enlarging the buffer was not
enough: **27 sites in `rasm_port.c` and 2 in `shape3d.c` still wrote the right
edges at offset 480**, which at 4x lands in the middle of the *left* array. Old
left-edge values therefore survived into the next polygon, so spans ran on
past where they should stop - exactly the "smearing" described. All 29 now use
`RFB_SPANROWS`.

**4x renders cleanly after that**, and 2x improved as well (its ground fill was
the same bug in milder form).

Remaining at 2x/4x: the ground beside the road fills white. Traced to
`rframe_helpers.c`, where the skybox/horizon path still has ~13 hardcoded
`0x140`/`0xC8` screen constants (`var_rect.right`, `rect_skybox.right`,
`var_rect.bottom`, the panorama x arithmetic). Fixing those is the next step if
this is ever picked up - but it is cosmetic, so it was **not** pursued (see the
gameplay-before-cosmetics note).

1x remains verified after all of this: the rendered frame is byte-identical to
the pre-experiment output and HILLTEST/PIPEROLL still diff 16/16.

## The 4x crash the user hit, and what the white ground actually is

**Crash (fixed).** `stack buffer overflow` in `preRender_default_impl`. My
`RFB_SPANROWS` was `max(height, 480)` - at 4x that is exactly 800, the screen
height, with *no* headroom. The original's 480 for a 200-row screen is a **2.4x
margin**, and it needs one: a polygon is walked into the edge table *before* it
is clipped, so it can span far more rows than the screen has. Now
`RFB_SPANROWS = 480 * RFB_SCALE`, which keeps the original ratio at every
scale and is exactly 480 at 1x.

**White ground (understood, not fixed).** Established by experiment, not
guesswork: clearing the framebuffer to a marker colour before each frame shows
those pixels keep the marker at 2x/4x and are overdrawn at 1x - so they are
**never drawn at all**, not drawn in the wrong colour.

Ruled out along the way:
* Not the skybox fill. `skybox_op_helper2` (which paints
  `skybox_sky_color`/`skybox_grd_color`) is **never called** in this build at
  any scale, so the ground we see at 1x is real terrain geometry, not a
  background wash. Scaling the ~15 screen constants in that path
  (`0x140`/`0xC8` -> `RFB_VIEW_W`/`RFB_VIEW_H`) is still correct and was kept,
  but it changes nothing visible.
* Not a leftover screen constant: the renderer was swept for 320/200/160/0x140
  /0xC8; what remains at `rframe_helpers.c:2059+` is `var_posElemCrds` /
  `var_nextPosElemCrds`, i.e. **world** units in `build_track_object`, and must
  not scale.

So the remaining fault is a large near-camera polygon being dropped somewhere
in the 3D pipeline at higher resolution - the ground plane in front of the car.
Its distinguishing feature is size: distant, small polygons all render fine.
The likely candidate is 16-bit overflow in projected screen coordinates
(`projectiondata9` scales with half-width, so at 4x it is 4x larger and
`projectiondata9 * radius / z` can wrap), but that has **not** been confirmed
and the polygon-count instrumentation to prove it did not produce output.

1x is untouched throughout: the rendered frame is byte-identical to the
pre-experiment output and HILLTEST/PIPEROLL/HELL2 still diff 16/16.

## The on-screen clock (2026-08-16)

First piece of the 2D layer. `src/render_faithful/rfont.c` replaces four stubs:
`intro_draw_text`, `font_set_fontdef`, `font_set_fontdef2` and
`format_frame_as_string`, and adds `font_draw_text` / `font_op2` /
`font_set_colour` from seg012.

**The font format**, decoded from FONTLED.FNT and verified glyph by glyph
(digits and colon all present, which is exactly what a clock needs):

    +0x0E  word    line height          8
    +0x10  word    advance width        6
    +0x14  byte    0 = fixed width, else a width byte precedes each glyph
    +0x16  word[]  glyph offsets indexed by character; 0 = absent
    glyph: height rows of ceil(width/8) bytes, 1 bit per pixel, MSB leftmost

**One deliberate deviation**, the only one in the port: `font_draw_text` is
behaviour-exact rather than instruction-exact. The original points DS at the
font resource and uses the file's own first bytes as scratch pen state, and the
two disassemblies label those offsets inconsistently. Unlike the simulation,
HUD text has no oracle - it is either legible in the right place or it is not -
so the data format is reproduced exactly and the blit is written plainly. This
is documented at the top of rfont.c so nobody mistakes it for a faithful port.

**Two real bugs found on the way**, both pre-existing:

* `resID_byte1` was declared `char` - a *single byte* - but the original is a
  text buffer at that dseg address, and `format_frame_as_string` writes "M:SS"
  into it. Every timer update was overrunning it into the next global. Now
  `char[32]`.
* `file_load_resource()` routes through `stunts_asset_load_archive`, which
  expects one of the game's resource archives. A `.FNT` is a plain file, so the
  font silently failed to load. It is now read raw.

**Verified:** the clock reads 0:10 at frame 200 and 1:01 at frame 1220 with
`framespersec` 20, matching the arithmetic exactly; the DOS reference capture
confirms the same font and drop-shadow style. Physics unchanged - HILLTEST,
PIPEROLL and HELL2 still diff 16/16.

Still stubbed in the 2D layer: `draw_ingame_text` (393 lines - the "Fasten your
Seatbelt!" and similar messages), `init_crak`, `do_sinking`,
`shape_op_explosion`, and the skybox mountain blitter.

## The cockpit (2026-08-16)

`src/render_faithful/rshape2d.c` adds the 2D bitmap layer and with it the
dashboard, roof strip, gearbox surround and steering wheel. Ported from
seg012 `shape2d_op_unk3` / `shape2d_op_unk` and seg005 `setup_car_shapes`
modes 0, 1 and part of 2.

**Where the cockpit lives.** Two archives per car, named from the four-character
car id: `STDA<id>.PVS` holds `dash`, `roof`, and the nine shapes
`whl1 whl2 whl3 ins2 gbox ins1 ins3 inm1 inm3` (that packed name list is the
literal `setup_car_shapes` passes to `locate_many_resources`, and the order is
the index order); `STDB<id>.PVS` holds the gear knob, `gnob gnab dot<sp> dota
dot1 dot2`.

**The shapes are raw, not compressed.** A 16-byte SHAPE2D header followed by
width*height bytes of 8-bit pixels, because `file_load_resource(3, ...)`
already ran them through `parse_shape2d` at load time. Verified rather than
assumed: across all eleven `STDA*.PVS` files, every shape's slot in the archive
is exactly `width*height+16` bytes. Each header also carries its own screen
position, so the cockpit needs no layout logic - for the Countach, `roof` is
320x9 at (0,0) and `dash` is 320x70 at (0,130).

**Those two numbers size the 3D view.** `roofbmpheight` is roof's height and
`dashbmp_y` is dash's y, and the windshield is the rows between them
(restunts.c:1032). The vertical field of view narrows to match, via
`set_projection(0x23, dashbmp_y / 6, 0x140, dashbmp_y)` - so turning the dash
on genuinely shows less of the world, exactly as the original does, rather than
pasting a panel over a full-height view.

**The steering wheel** has three frames - hard left, centred, hard right -
picked from `car_steeringAngle >> 3` with the shift done in sign-magnitude
(seg005 loc_2319D): below -10 gives whl1, above +10 gives whl3, otherwise whl2.

**Two things this fixes on the way:**

* `sprite_set_1_size` only wrote half its clip rectangle. The original writes
  each edge to both of its aliases - the span blitters read `left2`/`widthsum`,
  the SHAPE2D blitters read `left`/`right` - and ours set only the first pair,
  so `sprite_left`/`sprite_right` were stale at 0.
* `dashbmp_y` was declared in externs.h but never defined anywhere.

**[DEVIATION - withdrawn same day]** `gbox` was briefly drawn from mode 1 on the
assumption that it was the gearbox surround and that showing it beat showing
nothing. It is not: it is the *source* art for the gear gate, which mode 2
composites with the knob inside an offscreen sprite window before blitting the
result. Blitting it straight to its screen position painted an opaque orange
gate over the right-hand gauges. `tests/dos_reference/dos_default_coun_cockpit_driving.png`
settles it - that corner shows the dash bitmap's own gauges, switch panel and
dark strip, with no gate. Removing the call restored exactly that, and changed
3336 pixels, every one of them inside gbox's own rectangle. Whatever belongs
there for a manual gearbox needs mode 2's compositing; nothing is the right
answer until then.

**[ODDITY]** Mode 1 draws `roof` through `shape2d_op_unk`, which is an RLE
blitter (positive byte = run, negative = literal, 0 = end), while `dash` goes
through the raw blitter. But roof shapes on disk are raw like everything else:
decoding one as RLE terminates after 37 of its 2880 pixels. Since
`parse_shape2d` has already expanded everything by then, the RLE path looks
unreachable with these assets, so roof is drawn raw. Noted in the source in
case a car's roof ever renders as garbage.

**Verified:** all 39 shipped tracks run 300 frames with the cockpit enabled and
none crash; all 11 cars load and render their own dashboard, including the
open-wheel Indy car, which correctly has no roof strip and so keeps a
full-height windshield. Physics is byte-identical with the cockpit on and off
across seven tracks - the panel is drawn after `update_frame` and touches
nothing the simulation reads. The wheel animation is confirmed by driving a
right turn and diffing the wheel rectangle: 1995 of 5888 pixels change.

**Still missing from mode 2:** the speedometer and tachometer needles
(`ins1`/`ins3`/`inm1`/`inm3`), the digital readout (`dig0`..`dig9` in STDB, used
by cars whose `simd.spdcenter.y2` is 0), and the gear knob. All three need the
offscreen sprite windows `sprite_make_wnd` builds.

## The tilted horizon (2026-08-16)

Reported as "the graphics glitch badly at the horizon in a banked corner".
Reproduced by replaying HELL5 through the ported engine and dumping frames
1730-1850, where the car is on a banked corner (`physicalModel` 0x1A, roll 76 of
1024): the sky was a wedge in one corner and the rest of the background above
the terrain was never painted.

**Cause: a data table read 64 bytes too high.** `skybox_op` handles the case
where the camera is rolled and the horizon crosses the screen at an angle. It
takes the two horizon endpoints, measures the horizon's direction with
`polarAngle`, and extends that line into two covering quads - one filled with
the sky colour, one with the ground colour - by pushing four points out along
directions read from a table at `ds:098Ch` (seg003.asm 4402/4425,
`mov ax, [bx+98Ch]`).

The port had reconstructed that table on the assumption that dseg starts at
linear 3B7B0h. It starts at **3B770h** - both disassemblies open their dseg with
`word_3B770` at offset 0 - so every entry was read 40h too high, landing inside
the string constants `aOpp`/`aPen`/`aRpl`. The real bytes at 3C100h, identical
in restunts and restunts2:

    80 00  80 01  80 02  80 03      = 0080h 0180h 0280h 0380h
                                    = 128, 384, 640, 896

which in the 1024-step circle are the four 45-degree diagonals - exactly what
extending a line into a pair of half-plane quads needs. The string bytes gave
112, 368, 110, 114 instead. The first two are close enough to 128 and 384 that
the sky quad still roughly covered its half, which is why there was a wedge of
sky rather than nothing; the ground quad got two directions four steps apart
and collapsed to a slice of nothing, leaving the rest of the background
unpainted.

**Why it hid for so long.** The level-horizon fast path (`arg_8 == 0`) never
reads this table - it just fills two horizontal bands. Every straight-line test
and every oracle replay comparison ran on state, not pixels, so the defect was
invisible until someone drove through a banked corner and looked.

**Verified:** the sky now fills its half and the horizon line runs straight
across at the camera roll angle through the whole corner. Straight-line
rendering is byte-identical before and after, as it must be - `cmp` on the
rendered frame of DEFAULT/COUN at frame 110 reports no difference.

**New tooling this needed:** `--replay <file>` plays a recorded `.RPL` through
the native build, taking the track and car from the recording's own GAMEINFO
header, and `--shots <dir> --shot-from N --shot-step K` dumps a numbered BMP
sequence. Driving a real recording is the only way to reach the parts of a
track that need steering; the previous headless mode only held the throttle.

**Found while doing this, not yet fixed:** replays 03 (VETT on FAST) and 08
(JAGU on HELL2) segfault around frame 1000. Confirmed pre-existing - they crash
identically with the old skybox table - so this is a separate defect.

## dseg:0000 is an address, not a null pointer (2026-08-16)

Found while running all twelve recorded replays after the horizon fix: two of
them - VETT on FAST, JAGU on HELL2 - segfaulted around frame 1000. Confirmed
pre-existing (they crashed identically with the old skybox table), so this is a
separate defect that the previous straight-line 300-frame track sweep never
reached.

`lldb` put it in `sub_18D60` at `ldw(p = 0x0, 0)`. Instrumenting both sites that
compute `p` showed `elem=0x00`, `nullblock=1`: the blank tile, resolving to
`shapeinfo_null`, the zeroed stand-in for tiles with no shapeinfo of their own.

**The mistake is a category error, not an arithmetic one.** `si_cameraDataOffset`
is a *near dseg offset*, and 0 is a perfectly good one - it means dseg:0000, the
start of the data segment. The original reads twelve bytes from there, gets
whatever lives at the front of dseg, and carries on; nothing faults because
nothing can. The port turned that offset into a C null pointer, and the same
read faulted.

`shapeinfo_null` was already zero-filled deliberately, so that reads into it
"stay defined and deterministic rather than faulting" - but the *pointer stored
inside it* was still null, and that is what got dereferenced.

**Fix:** `src/sim_faithful/sfdseg_head.c` reproduces dseg:0000 as data. The
first 944 bytes are the real contents, transcribed from restunts2's dseg.asm by
`tools/extract_dseg_head.py`; the rest is zero padding, sized so that no
reachable offset can fault (`sub_18D60` reads at most `var_10*6+12` bytes in and
`var_10` is 8-bit, so 4096 covers it). The constructor in sfshapeinfo.c now
points every `shapeinfo_null` entry's `si_cameraDataOffset` at it.

**Verified:** all twelve replays now run to completion - 10646 frames on HELL5
down to 2000 on the shortest - and all 39 shipped tracks still pass the 300-frame
sweep. Straight-line rendering remains byte-identical. No behaviour changed on
the runs that already worked, and that holds by construction rather than by
testing: any run that completed before never took this path, because taking it
meant dereferencing a pointer at or near address zero.

## The other half of the horizon bug: uninitialised video flags (2026-08-16)

Asked whether banked corners were now clean, the honest answer was that only
one corner had been looked at. Eyeballing more frames is weak evidence, so the
question got a test instead.

**The invariant.** The renderer has no clear-screen step: the background fill
is expected to cover every pixel of the windshield that geometry does not.
`--paint-check` fills the framebuffer with an unused palette index before each
`update_frame` and counts how much of it survives inside the windshield rect.
Anything left over is a pixel the frame never painted - which in DOS would show
the previous frame's contents.

**It failed immediately**, and not in the code just fixed: 7437 of HELL5's
10646 frames left pixels unpainted, starting at frame 150 with a roll of only
3/1024. That is a *third* path through `skybox_op` - when the horizon is tilted
but nearly horizontal (`var_5A`), it is drawn as up to 32 vertical strips of
sky-and-ground rather than as two quads.

**Cause: `video_flag3_isFFFF` was 0.** The name is the analysis' own: it holds
-1, an all-ones mask. The strip loop ANDs it into each strip's right edge
(seg003 loc_1C80A). With 0 in it every strip came out zero-width, hit the
`if (var_rect.left == ax) continue` guard, and the whole loop drew nothing.

The port declared the flag but never assigned it. The original sets it, along
with four others, in the `// Video` block of its start-up (restunts.c:1256).
Of those, three exist in the ported subset and are now set exactly as the
original does: `video_flag2_is1 = 1`, `video_flag3_isFFFF = -1`,
`video_flag5_is0 = 0`. The second matters too - `rect_union` and
`rectlist_add_rect` both `fatal_error` on any value but 1.

**Verified:**

* `--paint-check` reports **0 unpainted frames** across all twelve replays
  (37146 frames total, every camera attitude a real drive produces) and across
  all 39 shipped tracks at 400 frames each.
* Straight-line rendering is still byte-identical - the level-horizon fast path
  reads none of this.
* Physics output is unchanged on the seven-track comparison.

**The lesson worth keeping.** Both horizon defects were in code the state
oracle called correct, because the oracle compares numbers and never looks at
pixels. A cheap whole-frame invariant found in one run what frame-by-frame
inspection would have taken days to notice - and it found a bug in a code path
nobody had thought to suspect.

## The scaling bug was the same bug (2026-08-16)

The user asked whether the "unpainted" defect was not exactly what had been
wrong with resolution scaling all along. It was - and the paint-check harness
answered it in one run.

Built at `RFB_SCALE=2` and run over HELL5 with the dashboard off,
`--paint-check` reported **1567 of 2000 frames** with unpainted pixels, all at
roll 0 - the *level*-horizon path, which reads none of the data fixed earlier
that day. So: the same class of defect, a different cause.

**Cause: `sprite_clear_1_color` had 320 and 200 hardcoded.** It is the native
replacement for the video driver's solid-fill routine, and every sky and ground
band in the renderer goes through it. It clamped the fill to 320x200 and
indexed with a stride of 320. At scale 2 the framebuffer is 640x400, so the
fill covered a corner of the screen and wrote to the wrong rows - leaving a
band of stale pixels below the horizon. That band is the "white ground" that
had been parked as an unexplained scaling defect.

It is the *framebuffer's* size that belongs here, not the original's screen
size, so the two constants are now `RFB_VIEW_W` / `RFB_VIEW_H`.

**Verified after the fix:**

* `--paint-check` reports **0 unpainted frames** at scale 2 and at scale 4,
  across all twelve replays.
* Rendered frames at 2x (640x400) and 4x (1280x800) show clean sky, terrain,
  road and buildings, with the horizon correctly tilted through a banked
  corner. No white ground anywhere.
* Scale 1 is unaffected: byte-identical rendering, 0 unpainted frames over
  HELL5's 10646, physics unchanged.

**What is still not scaled:** the cockpit. The dash, roof, wheel and gearbox
are fixed-size bitmaps drawn at their own 320x200 coordinates, so at scale 2
the panel appears at quarter size in a corner and the bottom of the screen is
never painted. That is a bounded, well-understood piece of work - replicate the
SHAPE2D blits by `RFB_SCALE`, or letterbox the cockpit - not the open-ended
mystery scaling used to be. Run scaled builds with `--nodash` until it is done.

**Method note.** Three of the four defects found today were in code that the
state oracle called correct, and the fourth had been parked for weeks as
unexplained. What found them was not closer reading but one cheap whole-frame
invariant - "no pixel of the windshield may go unpainted" - applied over real
recorded drives. It is worth asking, for each remaining unknown, whether an
invariant like that exists.

### Cockpit scaling (2026-08-16)

The remaining scaling gap closed the same day. `shape2d_op_unk3` and
`font_draw_text` now scale their destination rectangle by `RFB_SCALE` and
expand each source pixel into an RFB_SCALE x RFB_SCALE block; `draw_cockpit`
opens the clip window to `VIEW_W`/`VIEW_H` rather than a literal 320x200. The
art stays 320-wide, so the panel is chunky next to the smooth 3D - that is
inherent to upscaling the original bitmaps, not a defect.

At scale 1 every one of these reduces to the copy it was: verified
byte-identical rendering. At scale 4 all twelve replays report 0 unpainted
frames and all 39 tracks run.

The SDL window is now resizable; it is created at the original's 1.2 pixel
aspect (`* 6 / 5`), which is why a 1280x800 framebuffer opens in a 1280x960
window - DOS mode 13h pixels were taller than they were wide.

## Live gauges (2026-08-16)

`setup_car_shapes` mode 2 now draws the speedometer and tachometer needles.

**There is no trigonometry in this.** Each car's SIMD block ships lookup tables
of needle *tip positions* - `spdpoints` (up to 104 points) and `revpoints` (up
to 128) - and the needle is one straight line from the dial centre to the
indexed tip (seg005 loc_23456/loc_23485):

    si = car_speed / 0x280;      clamped to spdnumpoints - 1
    di = car_currpm >> 7;        clamped to revnumpoints - 1
    line(spdcenter, spdpoints[si], meter_needle_color)
    line(revcenter, revpoints[di], meter_needle_color)

`meter_needle_color` is `dialog_fnt_colour`, assigned beside the skybox colours
in seg003 loc_1D88E - the same white the clock uses.

`spdcenter.y == -1` marks a car with a digital speed readout instead of a sweep
needle (loc_232B6); the Porsche 962 and the Indy car are both like that, and
their speedo needle is correctly skipped. `spdcenter.y == 0` means no gauges at
all.

**[DEVIATION - behaviour-exact]** The original composites the dial face and its
needles inside an offscreen sprite window sized from `ins2`, then blits the
finished window to `ins2`'s screen position. That exists to avoid tearing on
real VGA, where the CRT is scanning the framebuffer while you draw into it.
Painting straight to our framebuffer in the same order gives the same result -
the dial face is redrawn first, which is what erases the previous frame's
needles, and the whole frame is presented at once - so `sprite_make_wnd` and
the rest of the window machinery are not needed. The needle coordinates are in
the window's own space and are offset by `ins2`'s screen position here.

**Verified:** driving DEFAULT and sampling, speed 156 gives index 0 and tip
(7,34) - the "0" mark at the lower left of the dial - rising through (5,15),
(16,7) to (26,2), straight up at the "120" mark, at speed 26794. The sweep
direction and the dial's printed numbers agree. All 39 tracks and six cars run;
`--paint-check` still reports 0 unpainted frames over HELL5's 10646.

### The gear gate: unblocked by a header nibble

The gear display works, and finding out why it did not is the useful part.

**Some shapes are stored transposed.** `file_unflip_shape2d` (restunts
shape2d.c:244) reads a nibble out of the header and un-transposes on that:

    if ((s2d_unk6 & 0xF0) == 0) flip = s2d_unk5 >> 4;
        1 = plain transpose, dst[i + j*w] = src[j + i*h]
        2 = transpose with the rows interlaced into two halves
        3 = a third form the original also refuses to handle

This is exactly what the earlier "every shape is width*height+16 bytes, so they
must all be raw" check could not see. That check ruled out *compression*; a
transposed image occupies precisely the same number of bytes. The conclusion was
right for most shapes and wrong for a few, and the few were the ones that
looked broken.

Which shapes carry it varies by car:

    Countach   gbox                       (and dot, dota, gnab, gnob in STDB)
    Porsche962 gbox, ins2
    Corvette   gbox, ins2  - flip type 2

So this was never only a gearbox problem: the Porsche's and the Corvette's
*instrument clusters* were being decoded wrong too. The Corvette's famous
digital dash - "FUEL ONLY", the segment speed readout, the bar tachometer - now
renders correctly, and it was the only test of flip type 2.

`unflip_shape` in rshape2d.c does the transform as setup_car_shapes(0) looks
each shape up, and clears the nibble afterwards so a cached archive cannot be
transposed twice.

**The gate is a transient overlay.** It sits on top of the right-hand gauges,
and the original saves those pixels into an offscreen window before drawing it,
blitting them back when the shift ends (loc_23057, and sprite_clear_shape_alt
at loc_234BE). So the resting dashboard shows gauges, and the gate appears only
while `car_changing_gear` or `car_fpsmul2` is set - which is what the DOS
reference capture shows, taken between shifts. It appears for automatic cars
too; the gearbox shifts itself, and you watch it happen. Since mode 1 repaints
the panel every frame here, restoring the background needs no saved copy:
simply not drawing the gate has the same effect.

**Verified:** driving DEFAULT, the fuel gauge is visible, the gate with its
chrome knob appears across the shift with the knob at the right H position -
the Countach reads (22,42), (34,12), (34,42) for gears 1, 2 and 3 - and the
gauge returns after. All eleven cars and all 39 tracks run; 0 unpainted frames
over HELL5; physics unchanged.

## Smoothing the cockpit art (2026-08-16)

The 3D view genuinely renders at `RFB_SCALE` resolution - real geometry, real
edges - while the cockpit is 320-wide art from 1990 enlarged by pixel
replication. Side by side, the panel looks blocky against a smooth world.

`rshape2d.c` now optionally runs **Scale2x** (EPX) over the cockpit shapes at
load time, once, after unflipping. It is on by default whenever
`RFB_SCALE > 1`; `--sharp` disables it, `--smooth` forces it.

**Why Scale2x rather than something cleverer.** Every output pixel is a *copy*
of an input pixel, chosen from the four orthogonal neighbours - the algorithm
never invents a colour. The result therefore still lives entirely inside the
game's 256-colour palette, so the framebuffer, the blitters and the SDL
conversion path all stay exactly as they are. A blending scaler, or an AI
upscaler, produces colours with no palette index and would force the whole
compositing path to truecolour first. That path is the real work; the choice of
upscaler is a swappable detail once it exists.

Scale2x also happens to suit this particular art: the panels are full of thin
white numerals, one-pixel needles and dithered shading, which is precisely
where AI upscalers smear or hallucinate. Rounding the staircase on the gauge
bezels and the wheel rim - which is what actually reads as "blocky" - is what
Scale2x does well.

Applied once for 2x and twice for 4x. Edges clamp.

**Verified:** 0 unpainted frames at scale 4 with smoothing on; all eleven cars
and all 39 tracks run. Scale 1 cannot be affected - `make_hires` returns early
on `RFB_SCALE < 2`, confirmed by rendering with `--smooth` at scale 1 and
getting a byte-identical frame.

## Truecolour cockpit, and the seam for an external upscaler (2026-08-16)

Scale2x alone was not enough - it rounds staircase corners but cannot soften
them, because every output pixel must be a copy of an input pixel to stay
inside the 256-colour palette. Getting further meant lifting that constraint.

**The 3D view is untouched.** It still renders palette-indexed exactly as the
original does. `present()` converts that frame to RGB, and the cockpit is then
composited over the result in truecolour. So the faithful path keeps its
faithful output, and only the enlarged 2D artwork gains colours the game's
palette never had. `rs_rgba`/`rs_pal` in rshape2d.c select the target; the
needles, gear knob and clock all route through one `put()` so they follow.

Truecolour is armed unconditionally, not just when smoothing is on - the
cockpit is drawn after the conversion, so it has to write there either way.
Without a blended copy every pixel is simply `pal[index]`, which is why scale 1
renders byte-identically, verified.

After Scale2x, the artwork now gets one 1-6-1 separable blend pass per
doubling. On a Scale2x result that is exactly an anti-alias: inside a flat area
every neighbour is the same colour, so averaging changes nothing, and only
colour boundaries move. The centre weight is deliberately high because the
panels are full of one-pixel white numerals and needles that a flatter kernel
turns to mush.

**The seam.** Nothing here can run an image model, so the genuinely intelligent
upscale - the one that recognises a gauge as a circular bezel with a needle
over it and redraws it - has to happen outside. Two hooks make that a normal
workflow rather than a rewrite:

    --export-cockpit <dir>   every cockpit shape as a 24-bit BMP
    --import-cockpit <dir>   <tag>.bmp back, at exactly RFB_SCALE times the
                             original size, used instead of anything computed

Whatever produced the replacement is irrelevant to the engine - a
super-resolution model, xBRZ, or a hand redraw all land in the same slot. The
import checks dimensions and refuses anything else, so a wrong-sized file fails
loudly instead of rendering skewed. Round-tripped end to end through macOS's
own `sips` as a stand-in upscaler: 17 shapes out, 17 back, rendered correctly.

**Verified:** 0 unpainted frames at scale 4, all eleven cars, all 39 tracks;
scale 1 byte-identical.

### The AI upscaling experiment, and what it measured (2026-08-16)

Upscayl was installed from Homebrew (it bundles Real-ESRGAN models and a CLI),
all 17 Countach cockpit shapes were exported, run through two models, and
imported back. Topaz was not tried: it is commercial and needs a purchased
licence.

The result is worth recording, because it settles by measurement something that
had only been asserted:

| | numerals on the speedometer |
|---|---|
| Scale2x + blend | `140 160 180 200`, `mph`, diamond markers - all correct |
| digital-art-4x  | `180` renders as `IOO`, `mph` is an unreadable blob |
| ultrasharp-4x   | `140`→`IUO`, `180`→`IBO`, `200`→`ZOO`, plus edge noise |

Both models produce visibly smoother *curves* than anything algorithmic - the
gauge bezels are genuinely rounder. They also **rewrite the text**, and on a
dashboard the text is the information. A speedometer that reads `IOO` where the
art says `180` is worse than a blocky one, and no amount of sharpening fixes it,
because the model is not sharpening: it is inventing plausible glyph-shaped
pixels from a 3-pixel-tall source.

Scale2x cannot do that. Every output pixel is a copy of an input pixel, so the
numerals are exactly the numerals; the blend pass then only moves colour
boundaries. That is why it stays the default.

The AI sets remain on disk (`build/cockpit_coun_ai`, `build/cockpit_coun_us`)
and can be selected at any time with `--import-cockpit`, which is the point of
having the seam: the question is now a five-second experiment rather than an
argument.

A model trained specifically on pixel art, or one run at 2x rather than 4x,
might do better; so might upscaling only the large shapes and leaving the
lettered ones alone. The harness makes any of those cheap to try.

## Upscaling: what the measurements decided (2026-08-16)

Three approaches were built and compared on the cockpit artwork, and the
outcome decided the default.

* **Scale2x + a blend pass** - smoother than plain replication, cannot alter a
  glyph because every output pixel is a copy of an input pixel.
* **Real-ESRGAN** (Upscayl, `ultrasharp-4x` and `digital-art-4x`) - genuinely
  rounder curves, and it rewrites the numerals: `180` came back as `IOO` / `IBO`,
  `200` as `ZOO`.
* **A per-pixel blend of the two** (`tools/blend_cockpit.py`) - detail mask from
  the *original* marks the thin high-contrast strokes, those come from Scale2x,
  the surfaces come from the model. Correct numerals and smooth surfaces both.

The blend is the best of the three and it is still only a marginal gain, which
is the honest summary. Extending the experiment to the menu artwork settled it:
on the opponent portraits - digitised photographs, dithered down to 256 colours
- the model reads the dither pattern as texture and **amplifies it**, so the
result is worse than plain replication. On the small buttons it is a wash.

**Decision: the resolution increase is for the 3D world, which is genuinely
re-rendered; the 2D artwork stays the original's own pixels, made bigger.**
`rshape2d_smooth` therefore defaults to 0. `--smooth` turns Scale2x back on and
`--import-cockpit <dir>` loads any external set, so none of the work is lost -
`tools/upscale_cockpits.sh` still builds a full AI set for every car if wanted.

What would actually sharpen this material is not an upscaler at all: the digits
and menu text come from three bitmap fonts (`FONTDEF.FNT`, `FONTLED.FNT`,
`FONTN.FNT`), roughly 96 glyphs. Drawing those as outlines once gives exactly
correct, genuinely crisp text at any resolution - and it is a bounded job rather
than an open-ended one.

## The main menu (2026-08-16)

`run_menu` (seg000, 163 lines) is ported. Like the cockpit, it turns out to be
almost entirely data:

* the screen is one 320x200 shape, `scrn` in `SDMSEL.PVS` - the road with the
  signposts;
* the five clickable areas are four parallel word tables in dseg,
  `menu_buttons_x1/x2/y1/y2`, and they land exactly on the signs:

      0  105..208 119..197   the car, "Let's Drive"  -> start the race
      1   66..107  77..120   the round blue "Car" sign
      2    5.. 67 114..170   the red "Opponent" stop sign
      3  190..253  76..122   the orange "Track" sign
      4  255..312 116..166   the green "Option" sign

`mouse_multi_hittest` is a linear walk of those tables; that is `menu_hittest`
here. **[DEVIATION]** the event loop and the window-to-320x200 coordinate
mapping are SDL rather than the DOS mouse driver, and the attract-mode idle
timer is not implemented. The artwork, the rectangles and the meaning of each
index are the original's.

`STUNTS_MENU_SHOT=<file>` renders the screen once and exits, which is how it
gets checked without a mouse.

**Still command-line only:** the car, opponent, track and option screens.
`run_car_menu` is 1300 lines of seg000, `run_opponent_menu` 609,
`run_tracks_menu` 479 plus 2899 for `load_tracks_menu_shapes` (the track
previews) - about 5700 lines in total. Clicking those signs currently prints a
note; `--track` and `--car` still choose.

## The track picker (2026-08-16)

Clicking the orange "Track" sign now opens a list of every `.TRK` in the data
directory - 42 of them here - and the chosen one is what "Let's Drive" races.
The command line still works; the menu just makes it unnecessary.

**The layout came out of a string, again.** `MAIN.RES` is the language file,
and the dialog templates live in it under the prefix `textresprefix` selects -
`'e'` for English, so `"loa"` resolves to `"eloa"`:

    eloa: "Load @]Path:]@                 ]@]@]@]@]@]@]@]@]"
    elsu: "(Scroll Up)"        elsd: "(Scroll Down)"

`]` ends a line and `@` marks a field. So the dialog is a title, a path line,
and **eight** file rows - which is where the eight rows come from rather than
from a guess, and why the labels read exactly "Load" and "Path:".

**[DEVIATION]** `show_dialog` (872 lines of seg008) parses that template and
renders it through the DOS mouse driver and its own widget system;
`do_fileselect_dialog` adds 777 more for the file handling, and `draw_button`
282. Those are not ported. What is reproduced exactly is the data - the
template's own strings, its eight-row shape, the game's own `FONTDEF.FNT`, and
a file list read from the same directory the original scans. The box is a
filled rectangle because that is what `show_dialog` clears it with
(`sprite_clear_1_color`). The interaction is SDL.

Not ported: the 3D track preview (`draw_track_preview` 600 lines plus
`load_tracks_menu_shapes` 2899), the high-score panel, and saving.

### Two bugs found while building it

* **`font_draw_text` was still writing to the palette-indexed buffer.** The
  RFB_SCALE and truecolour changes made for the cockpit never actually landed
  in `rfont.c` - a scripted edit had silently not matched - so the dialog drew
  its box and highlight and no text at all. It now scales like the cockpit
  bitmaps and follows whichever target is active.
* **`write_bmp` wrote 192000 separate three-byte `fwrite` calls**, and produced
  short files. The proximate cause was several stray `stunts_native` processes
  writing the same path at once, but the fix is worth keeping either way: the
  image is built in memory and written in one call.

`STUNTS_MENU_SHOT` and `STUNTS_TRACK_SHOT` render either screen once and exit,
which is how both get checked without a mouse.

## The car picker (2026-08-16)

Clicking the blue "Car" sign lists all eleven cars by their real names, over
the game's own showroom backdrop. Together with the track picker, the port no
longer needs the command line to choose a race.

**Data again.** Every car ships a `CAR<id>.RES` holding, besides the SIMD
physics block the simulation already reads:

    gnam  "Lamborghini Countach"
    gsna  "Coun|Lamborghini Countach"
    edes  "25th Aniversary]Lamborghini Countach]]Mid Engine, RW Drive]..."

and `SDCSEL.PVS` holds `stop`, the 320x103 showroom - blue curtains, red
carpet, a turntable - which is what the original spins the car on. The names in
the list are `gnam`, read from each car's own file; nothing is transcribed.

**[DEVIATION]** `run_car_menu` is 1300 lines of seg000 and renders the car's 3D
model rotating on that turntable, with paint and transmission toggles. Not
ported. The backdrop and the names are the game's; the list and the interaction
are not. `edes` is loaded but not yet shown.

### A third transposed shape

`stop` carries the same flip flag as `gbox` and the Corvette's `ins2`, and drew
as diagonal red and blue streaks until `unflip_shape` was applied to it. That
routine is now exported from rshape2d.c rather than private to the cockpit
loader, and the menus call it on every shape they draw - it is a no-op when the
header says the shape is already row-major.

Worth noting as a pattern: three separate times now, artwork that looked
corrupt has been a shape whose header said "transposed" and whose reader did
not ask. Any new 2D asset should go through `unflip_shape` on principle.

**Verified:** 39 tracks, all eleven cars, 0 unpainted frames; both pickers
render correctly at 320x200 and 1280x800. `STUNTS_CAR_SHOT` renders the car
screen once and exits, as `STUNTS_MENU_SHOT` and `STUNTS_TRACK_SHOT` do for the
others.

**Still not ported:** `run_opponent_menu` (609 lines), `run_option_menu` (239),
the 3D track preview (`draw_track_preview` 600 + `load_tracks_menu_shapes`
2899), the car's rotating model, high scores and saving.

## The horizon panorama (2026-08-16)

The world no longer ends in a flat colour band. `src/render_faithful/rskybox.c`
ports `load_skybox` (seg003, 113 lines) and the blit behind the five
`sprite_putimage_and_alt` calls in `skybox_op_helper2`.

**The landscape is one byte in the track file.** It sits at offset 0x384 of the
element map - the byte between the two 901-byte halves of a `.TRK`, which is
exactly the one `stunts_load_track()` had been discarding as padding.
`load_skybox(td14_elem_map_main[0x384])` (restunts.c:801) turns it into a name
from a table of nine-byte entries at dseg `aDesert`:

    0 desert   1 tropical   2 alpine   3 city   4 country

All five are shipped, and all five are used: of the 39 tracks, 14 pick desert,
13 tropical, 1 alpine, 7 city and 4 country. Bit 3 means "no scenery" and
refreshes only the colours.

Each archive holds four strips named in one packed string, exactly as the
cockpit shapes are - `"scensce2sce3sce4"` - and they tile into a band 0x400
pixels around that wraps as the camera turns. `skybox_ptr1..4` are their
heights, and each strip is placed at `horizon - height` so it stands on the
horizon rather than hanging from it. `word_454CE`, the tallest of the four, is
the same global `skybox_op` reads to know how far scenery can reach - it had
been 0 until now.

Loading the landscape also assigns `skybox_sky_color`, `skybox_grd_color`,
`skybox_wat_color` and `meter_needle_color`, so the four assignments
main_native.c had been making by hand are gone.

### [ODDITY - unresolved] The blit is an AND that cannot be one

seg003 calls `sprite_putimage_and_alt` five times and never ORs anything back,
and seg012 holds the only implementation, whose inner loop really is
`and es:[di], al`. But the strips are not masks: each is a finished picture of
a treeline or a skyline over its own sky.

Measured on DEFAULT, sky colour index 116: `dest & src` differs from a plain
copy on **100% of 729600 blitted pixels**, and renders a flat olive band where
the reference capture shows vegetation. A plain copy renders the reference's
picture. A copy is therefore what is implemented.

The likely explanation, untested: these archives carry `!cg0` / `!eg0`
colour-mapping tables which the original applies at load time through
`file_load_shape2d_palmap_apply` (restunts shape2d.c:499) and this port's
archive loader does not. Remapped values could plausibly be chosen so that
`sky & mapped` lands on the intended colours. Worth resolving before trusting
any other AND blit.

**Verified:** all 39 tracks run, 0 unpainted frames over HELL5's 10646, physics
byte-identical. Three tracks with different landscape bytes render three
different horizons - a city skyline on ALLJUMPS, a tropical treeline on
DEFAULT.

### A note on method

The `.pvs` versus `.res` mistake in the loader, and a scripted edit that
silently did not match, both cost a build cycle here. The second is the one the
plan already warns about: **check that an edit landed before rebuilding**. It
was caught this time by grepping for the new call before building.

## In-game messages (2026-08-16)

`src/render_faithful/ringame_text.c` ports `draw_ingame_text` (seg003, 428
lines) together with `font_op2_alt` and `locate_text_res` from seg008.

Every message is a text resource in GAME.PRE, found through the same language
prefix the dialogs use - `textresprefix` is `'e'`, so `"pre"` resolves to
`"epre"`. The layout is data as well: each block loads its string and jumps to
one shared tail (loc_1D1D5) that centres it and draws it, so all a block
chooses is the string and the y.

    edm1 170  edm2 182   "Professional Driver" / "on Closed Circuit", attract mode
    epre  90             "Fasten your Seatbelt!"
    ese1  93  ese2 105   the security-system warning
    ewww  93             "Wrong Way"
    eopp 116             "Opponent Near"

`font_op2_alt` is exactly `(0x140 - font_op2(str)) / 2`.

`passed_security` now defaults to 1: it is the flag the copy-protection
question sets, and this port asks nothing, so the warning must not fire.

**[DEVIATION - partial]** The turn arrows (bitmaps from SDGAME2.PVS needing
`sprite_putimage_transparent`, 192 lines) and the penalty counter (which
formats a number rather than printing a fixed string) are not ported. Both are
marked in the source. Every plain message works.

### Two bugs this uncovered, one of them mine

* **The lap clock had stopped drawing.** Arming `rs_rgba` globally for the
  truecolour cockpit sent *everything* that goes through `font_draw_text` into
  the RGB frame - including the clock and these messages, both of which are
  drawn from inside `update_frame`, before `present()` rebuilds that frame from
  the indexed buffer and overwrites them. It had been broken since the
  truecolour change and nothing caught it: `--paint-check` cannot see it,
  because the pixels underneath *are* painted. `rs_rgba` is now armed only
  around the cockpit and the menus, which are the only things drawn after the
  conversion.
* **The messages were being drawn in the wrong font.** `fontdefptr` fell back
  to FONTLED, the seven-segment face the clock uses, which contains digits and
  a colon and no letters at all - so the text was drawn and silently vanished.
  It is now FONTDEF.FNT, as the original loads.

The pre-start state is wired only in the interactive path: `game_inputmode`
starts at 0 there, which is what shows the message, and the first input flips
it to 1. Headless runs still start at 1 so every recorded physics baseline
stays exactly as it was - verified byte-identical.

**Verified:** all 39 tracks, 0 unpainted frames over HELL5's 10646, physics
unchanged, clock reading 0:10 at frame 200 again.

## Crash effects (2026-08-16)

`src/render_faithful/rcrash.c` ports `init_crak` (seg003, 227 lines) and
`do_sinking` (65). Both stubs are gone.

**The cracks are lines, not a bitmap.** GAME.PRE carries two resources:

    crak   four-word records - x0, y0, x1, y1 - one per crack segment. They
           all fan out from the same impact point, (131, 84):
           (131,84)-(159,66), (131,84)-(185,91), (131,84)-(142,107), ...
    cinf   [0] = how many stages, then a running total per stage.
           Here: 4 stages, at 6, 18, 42 and 77 segments.

So the windscreen shatters in four steps, and the step is
`framecount / (framespersec / 7)` clamped to the last. Each segment is drawn
*three* times - black one pixel above, black one below, and
`dialog_fnt_colour` between them - which is what gives the cracks depth
instead of leaving them flat white lines.

Only y is scaled, by `height / 200`: the cracks were authored for a 200-tall
screen and the windshield is shorter once the dashboard shows. x is scaled by
RFB_SCALE here instead, since the original leaves it alone at a fixed 320
width.

`do_sinking` fills a band of `skybox_wat_color` rising from the bottom of the
windshield, `(height * frames) / (framespersec * 4)` tall - four seconds from
the surface to gone.

`STUNTS_CRASH=1` and `=2` force the two states so both can be checked without
arranging a real collision; both were verified that way.

**Verified:** all 39 tracks, all eleven cars, 0 unpainted frames over HELL5's
10646, physics byte-identical.

**Phase 1 remaining:** `shape_op_explosion` (seg012, 442 lines) is still a stub.
It also needs `sprite_putimage_transparent` (192), which the turn arrows in
draw_ingame_text want as well - so those two land together.

## Explosions and the turn arrows - Phase 1 complete (2026-08-16)

`src/render_faithful/rexplode.c` ports `shape_op_explosion`,
`sprite_putimage_transparent` and `load_sdgame2_shapes`. The last stub in the
renderer is gone.

SDGAME2.PVS holds five shapes under one packed name string,
`"ex01ex02ex03leftrigh"`: three explosion frames and the two turn arrows. With
the transparent blit in place, the arrows in `draw_ingame_text` are wired up
too, so all three turn hints work - left, right and "Wrong Way".

**`shape_op_explosion` is a scaled transparent blit.** Its first argument is a
16.8 fixed-point scale and the shape's `s2d_unk1`/`s2d_unk2` are an anchor -
the centre of the blast - so the draw position is
`x - (anchor_x * scale >> 8)`, keeping the anchor pinned to the projected 3D
point while the picture grows around it. frame.c derives the scale as
`(bounding_box_pixels << 8) / shape_width`, which is also why
`sdgame2_widths[]` has to be filled at load time (seg003 loc_1D908).

**The two callers disagree on units**, which cost a round of debugging: the
arrows pass positions in the original's 320x200 space, while the explosion is
placed from a screen rectangle frame.c has already computed and whose scale
already encodes the framebuffer size. The shared blit therefore takes a
destination rectangle in framebuffer units and each caller converts.

### [ODDITY] The transparent colour is 255, not 0

The original's loop is `lodsb; jz skip; stosb` - index **0**. These shapes use
255: every one has 255 in its corner, ex01 is 255 across 285 of its 608 pixels,
and drawing it puts a solid white box around the fireball. Skipping 255 gives a
clean fireball and leaves the arrows correct; both were checked.

That is now the **third** asset whose stored values do not match what the
original's code expects, after the skybox AND-blit and the gbox transpose. The
common suspect is the same each time: these archives carry `!cg0` / `!eg0`
tables which the original applies at load time through
`file_load_shape2d_palmap_apply`, and this port does not. A remap sending 255
to 0 would reconcile blitter and data exactly. **Doing that once is almost
certainly better than special-casing a fourth time** - it is the highest-value
loose end left in the 2D layer.

**Verified:** all 39 tracks, 0 unpainted frames over HELL5's 10646, physics
byte-identical. Explosion checked at three scales and both arrows rendered via
the `STUNTS_EXPLODE` / `STUNTS_ARROW` test hooks.

**Phase 1 is complete.** The renderer has no stubs left.

## Correcting the record: two of the three "oddities" (2026-08-16)

Chasing the colour-map theory that three separate notes leaned on produced a
useful result: **it is wrong**, and one of the three oddities was never an
oddity at all.

**The palette map is never applied to a .PVS.** `file_load_shape2d()` runs
`palmap` only on the `.ESH` path, the EGA one. A `.PVS` is decompressed,
unflipped and returned (restunts shape2d.c). So `!cg0` / `!eg0` cannot explain
anything in the VGA assets, and the notes claiming they might have been
corrected in rskybox.c and rexplode.c.

**The explosion's transparent colour of 255 is faithful, not a deviation.**
`sprite_putimage_transparent` sets `mov ah, 0FFh` immediately above its inner
loop and compares every source byte against it. The earlier note called it an
oddity on the strength of a `lodsb; jz` seen in `sprite_putimage`, the *opaque*
blitter, which does not apply here. The shapes carry 255 in their corners; the
code looks for 255; they agree.

That loop also indexes a 256-byte table `cs:incnums[]` before comparing, so the
original remaps each pixel as it blits. The table is `extrn` in seg012.inc and
defined nowhere restunts disassembled. Drawing the raw value gives the correct
picture, which says it is the identity across everything these shapes use.

**The skybox AND blit is still open, but the easy explanations are gone.**
`sprite_putimage_and` does no lookup at all - it is a bare `and es:[di], al` -
and the palette map is not applied to its data either. What remains is
measured: AND renders a flat olive band, a copy renders the reference capture.

Worth stating plainly: two of the three "the data does not match the code"
notes were **my own mistakes in reading the disassembly**, not defects in the
port or the data. The remaining one is a real, narrow, measured discrepancy.

## Phase 2: the opponent (2026-08-17)

`src/sim_faithful/sfopponent.c` ports four routines. Three of them exist
**only** in restunts2's Ghidra export - restunts1's tree carries just
`extrn load_opponent_data:proc` and no C body anywhere - so every line
number below refers to `reference/restunts2/src/asm/`.

| Ported | Source | asm lines |
|---|---|---|
| `load_opponent_data` | seg004.asm 5772..6023 | 252 |
| `opponent_op` | seg001.asm 11..725 | 715 |
| `car_car_coll_detect_maybe` | seg001.asm 8102..8499 | 398 |
| `car_car_speed_adjust_maybe` | seg001.asm 6690..6855 | 166 |

Both car-car stubs in `sfstubs.c` are gone. The name map from restunts2's
symbols to the ones this port already had:

    int_atan2(z,y)      -> polarAngle          int_hypot   -> polarRadius2D
    int_hypot_3d        -> polarRadius3D       vec_transform -> mat_mul_vector2
    int_sin / int_cos   -> sin_fast / cos_fast gettlistpoint -> sub_18D60

`gettlistpoint == sub_18D60` is not a guess: same near proc in seg001, same
four arguments, and the port's `sub_18D60` already writes `oppnentSped[]`
through its fourth argument (sfasm_port.c:1047) - which is exactly the byte
`opponent_op` reads back out of `state.field_3F9`.

### What the data turned out to be

All six `OPP<n>.PRE` were dumped before a line was written. 27 resources
each; the three that matter to the simulation are `nam`, `path` and `sped`.

**`sped` is exactly 16 bytes in all six.** It is a 4x4 matrix, read as
`oppnentSped[si_oppSpedCode + ss_surfaceType]`:

    OPP1 BR  120  75  50  40 |  30  20  60  40 |  20  90 115  20 |  30  70  80  50
    OPP2 OP  150 120 100  40 |  30  20  80  60 |  40 120 115  30 |  30 100 120  50
    OPP3 JS  120 120 100  40 |  30  20  80  60 |  40 110 115  20 |  30 100 120  50
    OPP4 CC  150 120  80  20 |  10   5  50  40 |  30 120 115  20 |  30  70 120  50
    OPP5 HW  170 140 100  40 |  30  20  70  60 |  40 140 115  20 |  30 120 120  50
    OPP6 SV  200 150 100  50 |  30  20 100  80 |  40 175 115  60 |  50 150 140  50

That is the difficulty ladder, and it is the whole of the "AI": the target
speed is `state.field_3F9 << 8`, and `state.field_3F9` is one of those
sixteen bytes.

**[ODDITY, unresolved] `path` is loaded and never read.** The routine calls
`locate_shape_alt(res,"path")` into `var_414:var_412` and no instruction in
the remaining 200 lines touches either half - checked by grepping the proc
body for both names, which shows the store and nothing else. `offset aPath`
appears exactly once in the whole disassembly. The resource is 186 bytes in
every one of the six files. **The racing line is not in the file** - it is
computed, see below. The 186 bytes are presumably a leftover from an earlier
design; the port loads them and ignores them, as the original does.

**[ODDITY, resolved as faithful] the routing cost reads past the end of
`sped`.** The per-tile cost is `var_A[td17_trk_elem_ordered[si]] + 1`, where
`var_A` is the `sped` pointer and the index is a track element id up to 214.
It therefore reads into whatever follows `sped` in the archive - `winn`, then
`lose`. That is what the instructions say and it is reproduced. It is safe
in every file: the smallest tail after `sped` is 255 bytes (OPP1), the
largest 274 (OPP6), and no element id reaches 215.

### The racing line is a shortest-path search, not a stored path

`load_opponent_data` is a depth-first search over the track graph
`track_setup()` built:

* `td01_track_file_cpy[si]` is the primary successor; `0` means the finish
  and `-1` a dead end.
* `td02_penalty_related[si]` is the fork, `-1` when there is none. Every
  fork is pushed on a 256-deep branch stack together with the walk length
  and the 32-bit running cost, and popped on backtrack.
* A tile already on the current walk aborts it (loop detection).
* The cheapest walk that reaches the finish is copied into `trackdata3` as
  a word per tile, terminated by `0, 0, 1`.

The four stack arrays are read off the indexed writes rather than guessed:
`[bx+0xf4e0]` is -2848 (902 words, the tile stack), `[bx+0xfbf2]` -1038 and
`[bx+0xfdf6]` -522 (256 words each), `[bx+0xf0d2]` -3886 (256 dwords).
**[DEVIATION]** they are `static` in the port instead of 3.8 KB of
uninitialised stack; no path reads a slot it has not written, so this is
behaviour-neutral.

Measured, opponent 3 on all 39 shipped tracks: every one produces a line,
from EXERCISE's 13 tiles / cost 1696 to HELL2's 440 / 53353.

### `opponent_op` is a servo

Per frame it walks a waypoint cursor along `trackdata3` (advancing when
within 200 units of the waypoint, or aimed more than 0x100 off it), steers
towards it rate-limited to 8 units per frame at 20 Hz, accelerates or brakes
towards `field_3F9 << 8`, and then runs the *same*
`update_car_speed` / `update_grip` / `update_player_state` the player runs
with the two SIMD/CARSTATE pairs swapped. The three-way position test in the
middle is the "Opponent Near" logic: it writes `state.field_45E` = 1 or 2,
which `ringame_text.c` was already reading and which had never been set.

Two argument orders cost a careful re-read and are worth recording:
`mat_rot_zxy` is called with pushes `(1, rot.vx, rot.vy, rot.vz)`, so the
first cdecl argument is `rot.z` - the same shape as state.c:132 for the
player, not the shape shape3d.c uses. And `multiply_and_scale` in the finish
test takes the cosine as its **first** argument, the distance as its second.

### `car_car_coll_detect_maybe` is not about cars

Despite the name it is a general oriented-box-against-oriented-box test, and
`stateply.c` calls it four times: once car-against-car and three times
against the fixed boxes `unk_3BD5A` / `unk_3BD62` / `unk_3BD6A`. **That is
why the stub was being hit 208 times in a single-car replay** - it was a real
hole in the player's physics, not dormant opponent code.

The two corner-sign tables come from dseg.asm 559/566:
`word_3BE04 = {1,0,0,1}` negates the x half-extent and
`word_3BE0C = {0,0,1,1}` the z half-extent. The `[bx+0x69c]` in the first
loop is `word_3BE0C` addressed by its raw dseg offset - `word_3BE04` sits
eight bytes below it, which is exactly the relationship the other three
loops spell out by name.

### Verification (the numbers)

**Against the DOS oracle**, `tools/build_dumper.sh` + `tools/diff_oracle.py`
over all sixteen `.BIN` dumps in `build/oracle_run`, comparing the stubbed
build against this one. Only one line moves, and it moves the right way:

    replay      before          after
    TUBETEST     2/16 fält 71.91%   16/16 fält 78.46%
    (all fifteen others byte-for-byte unchanged)

The two known deviations from Phase 5 of the plan are still there and still
the same size: T_HELL4 14/16, PIPEFLIP 10/16.

`tools/build_dumper.sh` had rotted - it was missing `sfdseg_head.c` and the
five renderer files added in Phase 1, so `bin/dump_native_states` had not
linked for some time. It is fixed and now builds the same tree as
`build_native.sh`.

**Player physics, 39 tracks x 600 headless frames**, diffed line by line
against the pre-change build: **38 of 39 identical**, CTRACK08 the only
change, and that is the TUBETEST fix showing up in a second place (the car
now stops against the box at (22,3) instead of climbing to Y=2684 and
sticking there).

**Paint check:** 0 unpainted frames across all twelve replays (10646 + 11 x
2000-3000), with and without an opponent.

**All 39 tracks x all 6 opponents x 120 frames: 0 of 234 failed.**

Opponent 6 on DEFAULT completes a full lap in ~2900 frames (`varv=1`) and is
then stopped by `update_crash_state(3, 1)` at the line, which is the
original's finish path.

### Wiring

`--opponent <1..6>` and `--opponent-car <id>` on `bin/stunts_native`; **0 is
the default**, so every recorded baseline runs exactly as before. `game_init`
now mirrors restunts.c `setup_player_cars` for the opponent (its own SIMD,
its own aero table), places it with `0x300` where the player uses `0x100` -
36 units to the other side of the tile centre, which is what puts the two
cars side by side on the grid - and seeds `car_vec_unk3` with the same
`sub_18D60` call `init_game_state` makes. `shape3d_load_car_shapes` gets the
real opponent id instead of `0xFF`, and frame.c's opponent-drawing path (line
491, already written) lights up: measured, the two renders differ in a
car-shaped region of the road from frame 20 onwards.

`unload_resource` was only in the unlinked `fileio.c`; it is now in
`rfileio.c` where the rest of the archive handling lives.

### Not done

`run_opponent_menu` (seg000, 609 lines) is **not ported** - the red stop sign
in the main menu still prints "den menyn ar inte portad an". Choosing an
opponent is CLI-only for now. The win/lose portraits in `OPP<n>WIN.PVS` /
`OPP<n>LOSE.PVS` and the taunt lines (`ed1a`..`ev3c`, `winn`, `lose`) are
therefore also unused; `winn`/`lose` turn out to be plain panel-index
sequences (`1 1 1 1 2 3 ...`, 10-36 bytes) rather than bitmaps, so the
sequencing is data too when that lands.

## Phase 3: times, records and saving (2026-08-17)

Five procedures out of seg000, plus one from seg008, plus the replay wiring.
Line numbers are `reference/restunts/src/restunts/asm/`; restunts2 was checked
for each and adds nothing here - unlike the opponent code, all six have full
bodies in restunts1.

| Ported | Source | asm lines | Lands in |
|---|---|---|---|
| `run_opponent_menu` | seg000 4276..4885 | 609 | `main_native.c` |
| `run_option_menu` | seg000 4886..5125 | 239 | `main_native.c` |
| `highscore_write_a` | seg000 2214..2333 | 119 | `rhighscore.c` |
| `highscore_text_unk` | seg000 2334..2571 | 237 | `rhighscore.c` |
| `print_highscore_entry` | seg000 2572..2722 | 150 | `rhighscore.c` |
| `enter_hiscore` | seg000 2723..2907 | 185 | `rhighscore.c` |
| `highscore_write_b` | seg000 2908..2974 | 66 | `rhighscore.c` |
| `hiscore_draw_text` | seg008 3264..3345 | 81 | `rhighscore.c` |
| `draw_button` (bevel + label) | seg008 3615..3900 | 282 | `main_native.c` |

`enter_hiscore` was not on the list but is the glue: without it the two write
routines have nothing to write. It is 185 lines and it is what turns a finish
time into a record.

### The .HIG file is seven fixed records, and the layout falls out of a stack frame

A `.HIG` is exactly 364 bytes - `7 * 52`, and `highscore_write_a` writes
`0x16C`. The record layout is not guessed: it is the blank template
`highscore_write_a` builds at `[bp-56 .. bp-4]` (loc_1160A), read back by
`print_highscore_entry`'s copy into `var_4A .. var_18`:

    +0   char[17]  driver's name
    +17  char[24]  car name, the car's own `gnam`
    +41  byte      1 = wrap the opponent field in parentheses
    +42  char[8]   opponent, "<initials>/<car short name>"
    +50  uint16    time in frames at 20 Hz, 0xFFFF = empty

Decoding the shipped `DEFAULT.HIG` with it gives clean text and sensible
times, which is the check that it is right:

    JTK        Porsche/March INDY  -         1443 frames = 1:12.15
    katsauto   Porsche/March INDY  -         1449          1:12.45
    JTK        Porsche/March INDY  SV/INDY   1497          1:14.85
    ...                                      1539 1539 1565 1579

All seven render, sorted, with the game's own captions.

**[ODDITY - faithful, reproduced]** the blank template `strcpy`s **twenty**
dots into the **seventeen**-byte name field. The overrun is then partly
overwritten by the 23-dot car field, and the net effect is that an empty
record's name reads as one 40-character run of dots. That is what the
instructions do and the port does it too; it is visible the moment a track
with no `.HIG` is played, and it is what the original looked like.

**The sort is a permutation, not a move.** `word_46170` is a seven-entry order
table. `highscore_write_a` sets it to the identity; `enter_hiscore` finds the
first slot whose stored time is greater than the new one, shifts the table
down from there, and points that position at **physical slot 6** - the new
record always lives in the last of the seven. `highscore_write_b` then writes
the file *through* that permutation, so the file on disk is always sorted even
though memory is not. `byte_449CE` remembers which displayed row is the new
one, and `highscore_text_unk` draws that row in `dialogarg2` (= 4) instead of
0.

### The captions and the columns are data

`MAIN.RES` under the `'e'` prefix:

    ehs1 "FASTEST TIMES for"   heading, + " '<track>'", centred, y=5
    ehs2 "NAME"  ehs3 "CAR"  ehs5 "OPP."  ehs4 "TIME"    y=15

and the four column x positions are literals in `highscore_text_unk`:
**16, 120, 224, 272**. Rows are at `y = 25 + 10*i`. The captions go through
`hiscore_draw_text`, which draws the string at all four diagonal neighbours in
colour 0 and then at (x, y) in `dialog_fnt_colour` - a four-way outline, not a
drop shadow like `intro_draw_text`.

The rows are drawn in colour **0**, which only makes sense on a light
background - and it is: `end_hiscore` (seg000:13ECD) puts the table on one
full-width bevelled panel, `draw_button(NULL, 0, 0, 0x140, 0x64, 15, 8, 7, 0)`.
The port draws that panel first, which is why the rows are legible.

### The opponent menu

`SDOSEL.PVS` holds `scrn` (the clipboard-and-desk backdrop), `clip` (the frame
over the photo) and seven portraits under one packed name string
`"opp0opp1opp2opp3opp4opp5opp6"` - index 0 being the stopwatch, "race against
the clock". `MISC.PRE` holds the five button labels, again with the language
prefix: `ebla` "Last", `ebnx` "Next", `ebcl` "Clock", `ebca` "Car", `ebdo`
"Done", and `erac` "Race against]the Clock.]". Each `OPP<n>.PRE` carries its
own `edes`, the profile text - "Squealin']Bernie Rubber]]Age: 23 ...".

The five hit rectangles are the dseg tables `opponentmenu_buttons_*`
(dseg.asm 2529..2548): x1 20/76/132/188/244, x2 76/132/188/244/300, y 177..197
for all five - and `draw_button` is called at x+1, y+1 with w=0x36 h=0x12,
which lands exactly inside them. The profile is drawn at x=12, y=33+n*height
in **FONTN**, the narrow face, split on `']'`; blank lines still advance y
(loc_12CD2). "Car" is dead while racing the clock (loc_12D8D), and on "Done"
an opponent with no car of its own inherits the player's (loc_12E6A).

That last branch also explains a byte in every shipped `.RPL`: with no
opponent the original writes `0xFF` into `game_opponentcarid[0]`, which is
exactly what `DEFAULT.RPL`'s header carries (`ff 4d 49 4e`). The port now
writes the same sentinel.

### The option menu

`misc gstu` "Stunts" and `gver` "Version 1.1 (Feb 12 1991)" centred at y=6 and
y=16 through `intro_draw_text`, over a background cleared to `word_407FA` = 9.
**Those two are the only strings in MISC.PRE with no language prefix** - the
original looks them up with `locate_shape_alt`, not `locate_text_res`, and
using the wrong one silently draws nothing. That cost one build cycle here.

The menu itself is the `emop` template,
`"Options]}[Driving input device][Music on/off][Sound effects on/off][Load
replay][Set graphic levels][Exit to Dos]"` - `'}'` ends the title and each
`'['` starts a button. `show_dialog` returns the index and the jump table at
`off_1314A` maps it: -1 and 7 leave, 3 is the replay loader, the rest are
sub-screens. The port implements **Load replay**, **Set graphic level**
(`detail_level`), **Exit to Dos** and **Done**; the three input/audio entries
print their own text resource, since there is no audio yet.

### Replays

`file_load_replay` / `file_write_replay` were said to be ported already. They
are - in `src/render_faithful/fileio.c`, which **is not in the link**: its DOS
file layer is replaced by `rfileio.c`. So `file_write_replay` is transcribed
into `main_native.c` against the same globals it uses there
(`td13_rpl_header`, `gameconfig.game_recordedframes`), and `td11_highscores`,
`td13_rpl_header` and `td16_rpl_buffer` are now carved out of the trakdata
block at their real offsets `0x1A1E`, `0x1C7A` and `0x239E` - three of the
slices `sfdata_init_trackdata()` had left as comments.

Two new flags: **`--record <file>`** writes a headless run out as a `.RPL`,
and **`--play <file>`** watches one in the window (`--replay` still runs
headless, which is what every baseline in `tests/` uses). Interactive driving
now records into `td16_rpl_buffer` as it goes, and the results screen can save
it.

`gameconfig.game_trackname` had never been filled in. It is what
`file_build_path` turns into `"<track>.HIG"` and what a `.RPL` header carries,
so nothing that reads it could have worked before.

### [DEVIATION] - stated plainly, because there is no oracle for any of it

* The event loops, the sprite windows and the DOS mouse driver are SDL, as in
  the other menus. What is reproduced exactly is the data: every string, every
  rectangle, every colour index and all of the high-score arithmetic.
* `show_dialog` (872 lines), `do_fileselect_dialog` (777) and `call_read_line`
  are not ported. The name entry is `SDL_TEXTINPUT`; the file picker is the
  existing track dialog, generalised to take an extension - which is what
  `do_fileselect_dialog` takes as an argument anyway (seg000:130E2 passes
  `".rpl"`).
* `draw_button`'s bevel is drawn with four fills instead of thirty
  `preRender_line` calls. Same pixels: face 7, three-pixel top/left in 15,
  three-pixel bottom/right in 8, label centred and split on `']'` at 8 pixels
  a line.
* `end_hiscore` (seg000 5126..7089, 1963 lines) is **not** ported. The results
  screen shows the four numbers it shows - elapsed time, penalty time, top
  speed (`gState_topSpeed >> 8`, as seg000:5699 does it) and jumps - with the
  original's own labels from MISC.PRE, and offers its high-score entry, its
  replay save and its table. Its evaluation graph and its own layout are not
  reproduced.
* `run_car_menu` behind the opponent menu's "Car" button is this port's
  `run_car_dialog`, as it already was for the player.
* `.HIG` and `.RPL` are written next to the data directory with `/`, not
  through `file_build_path`'s backslash join off a dseg offset.

### Verification (the numbers)

* **Physics unchanged.** `--track DEFAULT --car coun --headless 600` is
  **byte-identical to `build/probe/phys9.txt`**, all 30 sampled lines. (The
  four names in the brief - HILLTEST, PIPEROLL, T_HELL2 - are `.RPL`s in
  `build/oracle_run`, not tracks; phys9.txt is the DEFAULT run.)
* **The DOS oracle does not move.** `tools/build_dumper.sh` +
  `tools/diff_oracle.py`: TUBETEST **16/16**, HILLTEST **16/16**, PIPEROLL
  **16/16**, T_HELL2 **16/16**. The two known Phase-5 deviations are still
  exactly the same size: T_HELL4 **14/16**, PIPEFLIP **10/16**.
* **Paint check:** 0 unpainted frames over HELL5's 10646.
* **All 39 shipped tracks start:** 39 run, 0 failures. Three of them also run
  with opponent 3 over 300 frames.
* **Record/replay round trip:** a 600-frame HELL2/lanc run written with
  `--record` is 626 bytes (26-byte header + 600 inputs), its header decodes to
  `lanc / COUN / "HELL2   " / 20 fps / 600 frames`, and replaying it reproduces
  the recording run's physics trace **line for line**.
* **The record path, end to end**, against the shipped `DEFAULT.HIG`
  (`STUNTS_HISCORE_TEST="<frames>:<name>"` runs load -> rank -> insert ->
  name -> write -> reload and prints the table):
  * 1200 frames -> rank 0, table shifts down, 1:16.95 falls off the end;
  * 1490 -> rank 2, inserted between 1:12.45 and 1:14.85;
  * 2000 -> rank -1, correctly rejected, table untouched;
  * a track with no `.HIG` -> a blank 364-byte table is created, and it shows
    the 40-dot name the original's overrun produces.
* **Rendered and looked at**, at 320x200 and at 1280x800: the opponent menu
  with and without an opponent, the option menu, the fastest-times table and
  the results screen. New one-shot hooks, in the style of `STUNTS_MENU_SHOT`:
  `STUNTS_OPP_SHOT`, `STUNTS_OPTION_SHOT`, `STUNTS_HISCORE_SHOT`,
  `STUNTS_RESULT_SHOT` (+ `STUNTS_RESULT_SHOT_TIME`), `STUNTS_HISCORE_TEST`.

### Not done

* The win/lose portraits `OPP<n>WIN.PVS` / `OPP<n>LOSE.PVS` and the taunt
  lines (`ed1a`..`ev3c`) are still unused - they belong to `end_hiscore`'s
  post-race sequence, which is not ported.
* `show_graphic_levels_menu`, `do_key/joy/mou/mof/sonsof/dos_restext`.
* The 3D track preview (`draw_track_preview` 600 + `load_tracks_menu_shapes`
  2899) and the car's rotating model, both still outstanding from Phase 1.
* The `esav` "Save @]Path:]@ ]Name:]@]" dialog: the results screen saves the
  recording as `<track>.RPL` rather than asking for a name.

## Phase 4: sound (2026-08-17)

The user's choice was "ja, men modernt": port the original's *decision*
logic faithfully, synthesise the output with modern means. That split is
exactly where this lands.

| Ported | Source | asm lines | Lands in |
|---|---|---|---|
| `audio_carstate` | restunts2 seg001 7192..7662 | 471 | `src/audio_native.c` |
| `audio_unk3` | restunts2 seg001 7664..7687 | 24 | same |
| `sub_18D06` | restunts2 seg001 7689..7725 | 37 | same |
| `audio_op_unk` / `audio_function2` | restunts1 seg007 455..540 | 86 | same |
| `audio_op_unk2` | restunts1 seg007 (the pitch/volume math) | 87 | same |
| `audio_op_unk3` / `_unk4` | restunts1 seg007 | 52 | same |
| `audio_op_unk5` / `_unk6` / `_unk7` | restunts1 seg007 1046..1156 | 89 | same |
| `audio_function2_wrap` | restunts1 seg007 | 30 | same |
| `frame_callback`'s ring drain | restunts1 seg005 1099..1136 | 38 | same |

`audio_carstate` has **no body in restunts1** - it is `extrn` there, and
seg005 only calls it - so the main routine comes from restunts2's Ghidra
export. The nine seg007 helpers are the other way round: only restunts1
disassembled them. Neither tree alone is enough.

### What the sound data turned out to be

`PCENG1.VCE` is 1049 bytes, `GEENG.SFX` 2074. They are **not samples**.
The layout is: dword total size, word count, `count` four-character names,
`count` dwords of entry offsets, then the entries - and a `<XX>ENG1.VCE`
entry is 93 bytes (PC/MT) or 100 (AdLib/Tandy) of synthesis parameters for
that driver's chip. Names, in file order:

    PCENG1  ENGI STAR SKID STOP CRAS BLOW CRA2 SCRA BUMP SKI2   (10)
    MTENG1  ENGI STAR SKID STOP CRAS BLOW SCRA BUMP SKI2         (9)
    ADENG1  STOP STAR ENGI BLOW SKID SCRA BUMP CRAS              (8)
    TDENG1  STAR SKID STOP CRAS BLOW ENGI BUMP SCRA              (8)
    GEENG.SFX  BLOW BUMP CRAS SCRA SKI2 SKID STAR STOP MTIN      (9)

Read for *what sounds exist*, that list is the whole sound design: an
engine loop, a starter, two skid loops, a stop, a crash, a blow-out, a
scrape and a bump. Playing the parameters would need an OPL emulator,
which was ruled out, so nothing here reads past the two bytes below.

**Two bytes of the ENGI entry are the entire rpm-to-pitch law.**
`audio_op_unk2` does `les bx,[di+8]` to reach the voice and then

    freq = rpm / vce[0x0E] + (vce[0x0F] << 4)

Measured from the shipped files: **PC 6/0, AdLib 11/0, MT-32 60/0**. The
port reads `PCENG1.VCE` at start-up and uses its 6 - a value whose result
is plausibly Hz (idle 800 rpm -> 133 Hz, redline 8400 -> 1400 Hz).

### How the original decides what sounds

The simulation was already raising the flags; nothing was reading them.
`CARSTATE.field_CF` - `externs.h:88` calls it "is initialized?", which is
wrong - is the sound request byte, rebuilt every frame:

| bit | set at | means |
|---|---|---|
| 0x01 | `state.c:33`, cleared 39 | engine running |
| 0x02 | `sfasm_port.c:530` | sliding, a wheel on surface type 1 |
| 0x04 | `sfasm_port.c:533` | sliding, no wheel on surface type 1 |
| 0x10 | `stateply.c:902` | scraping along a wall (re-set every frame of contact) |
| 0x20 | `stateply.c:1838` | a wheel's `car_rc1` over 0xFA - suspension bottom-out |

`audio_carstate` turns the *level* bits 0/1/2 into start/stop *edges*
through one latch byte per car (`byte_42D26` / `byte_42D2A`), and pushes
one 0x22-byte record per frame into a 40-entry ring at `unk_44F4C`. The
record holds each car's listener-relative position this frame and last
frame (`car_posWorld1`/`car_posWorld2`, both `>> 6`) plus each car's rpm.
A 100 Hz timer callback drains one record every `word_4499C` = 100/fps = 5
ticks - `seg001.asm:3846..3852` is where that constant is computed - and
hands it to `audio_op_unk2`, which is where the real decisions are:

* **audible radius 6000 units** (`0x1770`, about 5.9 tiles). Beyond it the
  volume is set to 0 and the routine returns without touching the pitch.
* **linear volume falloff**, `vol = 127 - 127*d/6000`, in integer maths.
* **radial velocity** `vrad = (100/elapsed) * (d_prev - d_now)`. The divide
  happens first, on the constant, so at 20 fps (`elapsed` = 5) the scale is
  exactly 20 - i.e. units per second.
* **Doppler**, `freq' = freq * 6000 / (6000 - vrad)`. The same 6000 is both
  the audible radius and the speed of sound.

The listener is chosen by `cameramode`, the same four cases `frame.c`
uses for the eye point, except that modes 0 and 2 put the listener *in the
car* rather than at the camera. In cockpit view the player's own relative
vector is therefore identically zero: full volume, no Doppler.

`audio_unk3` (bits 0x10/0x20) and `audio_function2_wrap` (the impact, from
`update_crash_state`) were `sfstubs.c` no-ops and are now live.

### [ODDITY] - four, all reproduced

* **Six dead bytes per ring record.** `audio_carstate` writes only offsets
  +6 .. +0x21 of each 0x22-byte slot. Bytes 0..5 are never written and
  never read.
* **Approaching is quieter.** `cmp var_16,0 / jle / shr ax,4 / sub` takes
  1/16 off the volume when `vrad > 0`. An approaching car is slightly
  quieter than a receding one at the same distance. No explanation found.
* **Skid A to skid B takes two frames.** The latch check at
  `LAB_1471_44f8` stops the running loop and returns without starting the
  new one; the start happens on the next frame.
* **An out-of-range `cameramode` reads uninitialised stack.** The switch
  falls through to `LAB_1471_4372` with the listener vectors never
  assigned. `main_native.c` masks the camera key with `& 3`, so it cannot
  happen; the port zeroes them, matching the build's
  `-ftrivial-auto-var-init=zero`.

### [DEVIATION] - stated plainly

* **There is no oracle for any of this.** The DOS dumps compare GAMESTATE,
  and not one audio global lives in GAMESTATE. The decision half is
  transcribed instruction by instruction and asserted on that basis; the
  playback half is new code and is only *behaviour*-exact, in the sense
  that it is driven by the ported numbers. Said explicitly because every
  other claim in this document is backed by a byte comparison.
* **"The PC driver's frequency word is Hz"** is a calibration choice. The
  divisor 6 is read out of the shipped file; treating the quotient as Hz
  is not something the disassembly states.
* **The 100 Hz timer is not a timer.** `audio_native_frame()` runs
  `word_4499C` ticks of `frame_callback`'s drain loop per simulation
  frame, which produces exactly the same one-record-per-frame drain with
  `elapsed == word_4499C` that the real timer produces at 20 fps.
* **Mono, no panning.** Every driver the original shipped - PC speaker,
  AdLib/OPL2, Tandy, MT-32 - is mono, and the ported decision layer
  produces a distance and a pitch but never a stereo position.
* **The synth is invented.** Engine = two saws (f and 2.01f) through a
  one-pole lowpass that opens with revs; skid = white noise through a
  state-variable bandpass at 2600 Hz (variant A) or 1450 Hz (B); crash,
  bump and scrape = filtered noise with exponential envelopes. Which .VCE
  slot each of `audio_op_unk3`..`_unk7` reaches is **unresolved** - the
  fixed slot order is built by `init_audio_resources`, which no tree
  disassembled, and the name order differs between the four driver files -
  so the mapping from slot to timbre here is semantic, not from the data.
* **Music is not implemented.** `SKID*.KMS` needs the same OPL2/MT-32
  emulation the voices do. The option menu's "Music on/off" still only
  prints its text resource.

### Wiring

* `--nosound`, `--dump-audio <file.wav>`, `--audio-trace`.
* **Headless is silent by default**: `audio_native_init` gets mode 0 unless
  `--dump-audio` is given, so the audio layer allocates nothing, installs
  no hooks and runs no code. Every existing baseline is untouched by
  construction, not by luck.
* `sfstubs.c`'s `audio_unk3` / `audio_function2_wrap` now forward through
  two function pointers that `audio_native.c` installs. That keeps
  `bin/dump_native_states` - which links `sfstubs.c` but not SDL - building
  and behaving exactly as before.
* The option menu's **"Sound effects on/off"** (button 2) toggles it.
* `word_4499C` was declared in `externs.h:239` and **defined nowhere**; it
  is defined here.

### Verification (the numbers)

Sound cannot be checked by listening in this loop, so it is checked
numerically. `--audio-trace` prints rpm, chosen frequency, volume, the
loop flags and the one-shot counters once per frame; `--dump-audio`
renders the same synth deterministically to a .wav.

* **Physics unchanged.** `--track DEFAULT --car coun --headless 600` is
  byte-identical to `build/probe/phys9.txt`, all 30 sampled lines - and
  identical again **with the audio layer switched on** (`--dump-audio`),
  which is the check that the decision layer writes no simulation state.
* **The DOS oracle does not move.** `tools/build_dumper.sh` +
  `tools/diff_oracle.py`: TUBETEST **16/16**, HILLTEST **16/16**,
  PIPEROLL **16/16**, T_HELL2 **16/16**; the two known Phase-5 deviations
  still exactly T_HELL4 **14/16** and PIPEFLIP **10/16**.
* **Paint check:** 0 unpainted frames over HELL5's 10646.
* **All 39 shipped tracks start:** 39 run, 0 failures.
* **rpm -> pitch, 600 frames of DEFAULT in cockpit view: `hz == rpm/6`
  in 600 of 600 frames.** rpm 800..8420 -> 133..1403 Hz.
* **Gear changes move the pitch.** Six one-frame rpm drops over 800 in
  that run, each with the matching pitch drop:

      frame  103  rpm 7564->5459   hz 1260-> 909
      frame  176  rpm 7604->5543   hz 1267-> 923
      frame  311  rpm 8129->5415   hz 1354-> 902
      frame  321  rpm 4993->4007   hz  832-> 667
      frame  327  rpm 4007->1789   hz  667-> 298
      frame  335  rpm 1789-> 800   hz  298-> 133

* **The rendered waveform really is at that frequency.** A DFT over the
  .wav windows for those frames: expected 133 Hz -> measured 133.0;
  expected 535 -> 527; expected 1274 -> 1282. (The two later windows span
  several frames of changing revs, which is the whole of the error.)
* **Distance falloff and Doppler, DEFAULT with opponent 6 over 900
  frames**, traced on the opponent's channel:

      dist    96   vol 125      (127 - 127*96/6000   = 125)
      dist   882   vol 109      (127 - 18            = 109)
      dist  2134   vol  82      (127 - 45            =  82)
      dist  3459   vol  54      (127 - 73            =  54)
      dist  4951   vol  23      (127 - 104           =  23)
      dist  6521   vol   0      past the 6000 cutoff, pitch frozen

  and at frame 361 the opponent is receding at 31 units/frame, i.e.
  `vrad = -626`; the traced pitch is 1178 Hz against a base of 1300, and
  `1300 * 6000 / (6000 + 626) = 1177`. The Doppler term is exact.
* **Every sound path fires on real recordings.** All twelve replays in
  `tests/replays/`, counting frames with each loop or one-shot active:

      02_lanc_slalom_corners   skidA  115  skidB 1365
      06_audi_wall_collision   skidA   21  skidB  949   crash 1
      09_pc04_carrera_test     skidA    7  skidB  928   bump 6  scrape 116
      10_ansx_acura_slalom     skidA   29  skidB  827   bump 3
      05_lm02_offroad_grass    skidA   21  skidB    3   bump 6  crash 1
      00_default_hell5_full    skidB    2  bump 12  scrape 53  crash 1

  Note `05_lm02_offroad_grass` is the one replay where skid **A** outnumbers
  skid B - independent confirmation that bit 1 ("a wheel on surface type
  1") is the off-road variant.
* **The windowed path opens a real device.** `--play` with `--audio-trace`
  runs at 20 Hz with the SDL audio device open and no error from
  `SDL_OpenAudioDevice`. Whether it *sounds* right has not been judged -
  nobody has listened to it.

### Not done

* Music (`.KMS`), and therefore the "Music on/off" entry.
* `init_audio_resources` / `audio_load_driver` / the four `*15.DRV` files:
  no driver is loaded, so the slot-to-name mapping behind
  `audio_op_unk3`..`_unk7` stays unresolved (see [DEVIATION] above).
* The `STAR` (starter) and `STOP` voices. `audio_carstate` never reaches
  them; they belong to the ignition sequence in `seg005`.
* Stereo. The decision layer has the geometry for it, but the original
  does not.

## Phase 8: music (2026-08-17)

The four `.KMS` songs now play. What follows is what was transcribed, what
was reverse engineered, and — separately — what was actually *measured*.

New files, none of them touching anything that existed:

    src/music_native.h        the API (init / play / stop / enabled / mix)
    src/music_native.c        the sequencer, seg027+seg028+seg029
    src/music_ad15.inc.c      AD15.DRV, the AdLib driver, reverse engineered
    src/music_opl.h           the OPL2 register interface
    src/music_opl_stub.c      a tone probe. NOT an OPL emulator. See below.
    src/vendored/opl/README   what belongs there and why it is empty
    tools/dis8086.py          a 16-bit x86 disassembler, written for this
    tools/music_tool.c        device-free driver: dump events / dump OPL / wav
    tools/build_music_tool.sh
    tools/verify_music.py     an independent Python model, cross-checked
    tools/analyse_music_wav.py  FFT of the rendered audio
    build/ad15_dis.txt        the annotated AD15.DRV disassembly

`src/main_native.c` was **not** touched — another agent held it. Wiring is
five lines: `music_native_init(data_dir, rate)` next to the audio init,
`music_native_play(...)` where a screen starts, and `music_native_mix(buf,
frames, channels)` inside the SDL audio callback after the effects are
mixed. It adds into the buffer and clips; it never blocks or allocates.

### The format, established from the bytes and not from a guess

A chunk container is `dword length; word n; n*4 names; n*4 offsets; data`,
and it nests. `SKIDTITL.KMS` is a one-chunk container `"titl"` holding
`HDR1` and six track streams `t0s0`..`t5s0`. `HDR1` is
`dword length; byte; byte linked; byte ninstr; ninstr*4 names; byte
ntracks; ntracks*5`, which for SKIDTITL is `4+3+20+1+30 = 58` and the file
says 58. Every one of the 25 track streams across the four songs decodes
and **ends exactly on its chunk boundary** — that is the check that would
catch a single mis-sized opcode anywhere in the parser.

Events are `variable-length delta; status; operands`. Status below 0xD9 is
a note (`status & 0x7F`), with an explicit velocity byte only when status
is above 0x80, then a variable-length duration. 0xD9..0xEA are the
eighteen control opcodes, two independent tables in the original
(`off_395A8` parses, `off_383A0` skips) that agree on every length.

**Tempo is BPM at 24 PPQN.** `0xDD b` sets `word_454BA = 32000/b`; each
100 Hz timer tick adds 128 to an accumulator and spends it in units of
that divisor, so ticks/second `= 100*128*b/32000 = 0.4*b`. At b = 120 that
is 48 ticks/s. The scores corroborate that reading: every one of
SKIDTITL's 1171 deltas and durations is a multiple of 6 ticks - a
sixteenth note - and 6 and 12 stay the two commonest values in the other
three. Their 1- and 2-tick deltas belong to the modulation tracks, not to
the notes: SKIDSLCT's `t6s0` is 718 events of which only 184 are notes,
the rest 350 `0xDF` controller-1 (modulation wheel) steps and 179 `0xE5`
pitch bends, and SKIDOVER's `t5s0` is the same shape. The 100 Hz comes
from `timer_setup_interrupt` (restunts2 seg012.asm:3216) programming PIT
divisor 0x2E9C = 11932; 1193182/11932 = 100.0 Hz. The comment there
saying "11977" is wrong.

The four songs, decoded:

| song | tracks | events | notes | longest track | track names |
| :--- | ---: | ---: | ---: | ---: | :--- |
| SKIDTITL | 6 | 603 | 568 | 1254 ticks | Kick/Snare/Toms, Hats, Bass, Lead (Synth), Harm (Synth), Strat |
| SKIDSLCT | 7 | 2107 | 1542 | 3090 | |
| SKIDOVER | 6 | 1030 | 439 | 1940 | |
| SKIDVICT | 6 | 1013 | 608 | 2040 | |

Three of the four loop with `0xE2 LOOPSTART 255` at the top of every track
and `0xE3 LOOPEND` then `0xD9 RETURN` at the bottom — 255 repeats of a
1152-tick (24 s) loop before a track could ever reach its RETURN, i.e.
endless in practice. SKIDOVER does it differently: every one of its six
tracks ends `0xDB RESTART` then `0xD9 RETURN`, and RESTART clears both
stacks and jumps to the track's start, so it loops unconditionally and
forever. Both paths are implemented and both were exercised in the 120 s
cross-check below.

### AD15.DRV

3571 bytes of raw x86 that neither disassembly covers, and the only place
in the whole game where an OPL register is written. There was no
disassembler on this machine that takes a raw 16-bit binary (Apple's
`objdump` is llvm-objdump and rejects `-b binary -m i8086`; no capstone),
so `tools/dis8086.py` was written for the job. Recursive descent from the
21 jump-table entries reached 2688 of 3571 bytes with **zero undecodable
bytes and every branch target on an instruction boundary**; the same tool
gives the same clean result on MT15/PC15/TD15.

The twenty-one entry points resolve, and **five of them are the same
`mov ax,0FFFFh / retf` stub**, which settles three loose ends in seg028 at
a stroke: on AdLib the "reset all" call (+0x18), the pitch-bend call
(+0x1B) and the end-of-tick flush (+0x30) do nothing at all. Pitch bend
still reaches the chip, but only one tick later, through `update_pitch`.

`init()` returns **10**, and that single number explains the instrument
channel masks. Voices are numbered 1..9 onto OPL2 channels 0..8, and
**voice 0 is the digital sample channel** — which is why no mask in any
AdLib bank ever sets bit 0. The mask at record +0x0C is tested against
`1 << voice` (dseg 0x4E9E is just the sixteen powers of two): BASS on
voice 1, melodic patches on 2..7, cymbals on 8, kick and snare on 9.

The 100-byte `.VCE` record's AdLib half:

    +0x11 signed fine tune (added to the packed A0/B0 word)
    +0x12 pitch-bend range in semitones
    +0x15 0 = ignore velocity, use 0x7F
    +0x44 CNT, +0x45 FB      -> reg 0xC0 = FB<<1 | CNT
    +0x46 operator 0 (modulator), 12 bytes
    +0x52 operator 1 (carrier),   12 bytes
      operator: +0 AR +1 DR +2 SL +3 RR +4 TL +5 KSL
                +6 MULT +7 KSR +8 EGT +9 VIB +10 AM +11 WS
      -> reg 0x20 = AM<<7|VIB<<6|EGT<<5|KSR<<4|MULT,  0x40 = KSL<<6|TL,
         0x60 = AR<<4|DR,  0x80 = SL<<4|RR,  0xE0 = WS

Decoding all 25 records of `ADSKIDMS.VCE` with that layout gives **zero
out-of-range bit fields**, and the values read as what the names say: the
six percussion patches all use FB 6/7 with MULT 15 on the modulator, and
HRN1/HRN2/HRN9 are byte-identical to each other.

Pitch: `note_to_fnum` is `block = note/12`, `index = note%12 + offset`,
`fnum = table[index]`, result `fnum | block<<10`. The table is 60 words
based at 0x08B2 so the index runs −24..+35; entries +24..+35 are the
textbook OPL2 octave (0x157, 0x16B, …, 0x287) and each octave below is
half the one above. `note_on` then writes `0xA0+ch = packed & 0xFF` and
`0xB0+ch = 0x20 | packed>>8`. Velocity and channel volume are combined at
0x0730 as `TL = 0x3F − round(round(vol*vel/128)*(0x3F−TL_base)/128)`,
applied to the carrier always and to the modulator only when CNT = 1.

Three things carried over deliberately, marked `[ODDITY]` in the source:

* The register writer keeps a 256-byte shadow and **skips the bus write
  when the value is unchanged**. That is not an optimisation — writing the
  same `0xB0` twice does not re-key the channel, and the sequencer's
  behaviour depends on it. Reproduced.
* `block = note/12` has no clamp; note ≥ 96 overflows into the key-on bit.
  The highest note any shipped song produces after transposition is 87.
* The parser skips a velocity byte when status > 0x80, the scanner in
  `audio_map_song_tracks` when status ≥ 0x80. They disagree at exactly
  status 0x80. No song uses it; the lowest note status in the four files
  is 0x98.

### The OPL core is missing, and that is the honest headline

Phase 8 was specified as "vendor Nuked-OPL3 or ymfm, do not write your
own". Neither could be fetched — there is no local copy (dosbox-x and
dosbox-staging are installed as binaries only) and this session was not
permitted to download one. What is compiled in is `src/music_opl_stub.c`,
which keeps the register file and emits **one sine per keyed channel at
`f = fnum * 49716 / 2^(20-block)`**, ignoring FM, envelopes, feedback,
waveform select, MULT and rhythm mode entirely. Its purpose is to make the
note stream measurable, not to make a sound. `music_native_opl_is_real()`
returns 0 and anything claiming the music sounds right must check it.
`src/vendored/opl/README` says exactly what to drop in and what must not
change when it is.

### What was measured

* **Two independent implementations agree exactly.** `tools/verify_music.py`
  re-implements the container, the event parser, the tempo accumulator,
  the drum-kit dispatch and `note_to_fnum` in Python, from the same
  disassembly but written separately. Over the first **120 seconds of each
  of the four songs** it predicts every note-on's 100 Hz tick and packed
  `(block<<10)|fnum`, and the C port's OPL key-on writes are the same
  sequence, in the same order:

      SKIDTITL  2846 note-ons   identical
      SKIDSLCT  3926            identical
      SKIDOVER  1756            identical
      SKIDVICT  2790            identical

  That is 11 318 events, and it covers each song's loop wraparound.
* **Every track stream ends on its chunk boundary** — 25 of 25 across the
  four songs, and the parser consumed exactly the declared byte count.
* **The audio is at the pitch the registers ask for.** 30 s of each song
  rendered to .wav, then FFT over every 0.15 s window in which no
  channel's pitch state changes (the music is dense; longer windows do not
  exist). Of 388 expected fundamentals above 60 Hz, **345 land within 2 Hz
  of an interpolated spectral peak, median error 0.32 Hz**. All 43 that do
  not have another simultaneous tone within 27 Hz — the width of the Hann
  main lobe at this window length — so the two peaks merge and the
  measurement, not the port, is what fails. Verified: every one of the 43.
* **6114 register writes in 20 s of SKIDTITL**, of which 458 are key-ons
  and 1347 are `0xA0` writes — the 889 extra ones are the per-tick
  vibrato/pitch updates in `update_pitch`, so that path is exercised.
* **Clean under ASan+UBSan**: 160 s of rendering across the four songs,
  plus 24 play/stop cycles at 8000/28000/48000 Hz with enable/disable and
  shutdown/re-init. No diagnostics.
* **Nothing else regressed.** Phase 8 added files and appended to this
  document; it changed no line of any existing source, `src/audio_native.c`
  and `src/main_native.c` included. The standing suite was run anyway,
  against a build made at 09:20 on 2026-08-17:
  oracle TUBETEST/HILLTEST/PIPEROLL/T_HELL2 **16/16**, T_HELL4 **14/16**,
  PIPEFLIP **10/16** — exactly the known state; paint check **0 unpainted
  frames of 10646**; **all 39 shipped tracks start**, 0 failures. (Another
  agent was editing `src/main_native.c` in parallel; those numbers describe
  the tree as it stood when they were taken, and nothing in Phase 8 is
  linked into that binary yet.)

What is emphatically *not* claimed: that it sounds like Stunts. Nobody has
listened, and with the stub in place there is nothing worth listening to.

### Not done

* **The OPL2 core.** Above. This is the one thing standing between a
  correct note stream and actual music.
* **Wiring into `src/main_native.c`** — the file was off limits this
  session. Nothing calls `music_native_*` yet.
* **The digital sample player.** AD15.DRV's INT 8 handler at 0x0C2E plays
  PCM by using OPL register 0x40 as a 6-bit DAC, driven off PIT channel 0
  at the sample rate, through a 256-byte volume table at 0x0CF3. That is
  how the game gets digitised sound out of a bare AdLib. It lives on
  voice 0, which the music never allocates, so none of it is ported —
  but it is the answer to how `.SLB` samples would ever be played.
* **MT-32, Tandy and PC-speaker.** Only the AdLib path exists. Every
  `byte_40634` branch in seg027/seg028 is dropped.
* **The sound-effect side of the sequencer**: tracks 0x10..0x16,
  `sub_386D6`, `sub_39050`, `sub_38CF8` and the `.SFX` container. Those
  belong with `audio_native.c`, and joining the two is a later decision.
* **The per-track callback at track+0x48** (`nopsub_37750`) — only the
  sound-effect side ever sets it.

### [ODDITY] still open

* `sub_38702` loc_38742: when a track's pointer is NULL *and* its delay is
  already zero, the original still falls into `loc_38A98` and decrements
  the 32-bit delay, wrapping it to 0xFFFFFFFF. Harmless (the track is idle
  either way) and reproduced.
* The call stack at track+0x05 has a stride of 4 but `+0x15`, `+0x16` and
  `+0x18` hold live fields, so a fifth nested `0xE6 CALL` in the original
  overwrites the track's voice count and the low word of its delay. Same
  for a fifth nested `0xE2` and the loop counters at `+0x43`. Bounded at 4
  here — a `[DEVIATION]`, unreachable in the shipped songs (max nesting
  observed: one).
* `0xE6 CALL` in the original jumps through a far pointer that
  `audio_map_song_tracks` wrote over the chunk name; if the name did not
  resolve it jumps through the raw ASCII. This port stops the track
  instead. `[DEVIATION]`.
* `ENTRY_17` of AD15.DRV (`start_pcm`) uses the same argument both as the
  repeat count and, plus 0x32, as the PCM data offset. Could not be
  decided from the driver alone whether that is intentional. Not ported,
  so it does not matter yet.
* `0x0B41: mov ax, 0xB001` in AD15.DRV is dead — overwritten four bytes
  later with no call between. Looks like a removed `call 0x07C7`.
* seg028's `byte_3930E` is a mis-disassembly in restunts1: the bytes
  `83 7E F0 FF / 75 03 / E9 ..` are `cmp word [bp-10h],0FFFFh; jnz
  loc_39317; jmp loc_390F7`, not the `db 131,126,240 / push [di+3]` IDA
  printed. Read correctly here.

## Phase 5: the five defects (2026-08-17)

Phase 5 was a list of things that were **wrong**, not missing. All five were
looked at; four are closed, one is half closed. The headline is that the DOS
oracle went from 78.46 % to **100.00 %** byte agreement on four of the six
test recordings and 16/16 on five of six - T_HELL4, previously a "known
deviation", is now exact.

| Defect | Verdict |
|---|---|
| 1. `preRender_sphere` was a deliberate `fatal_error` | ported, 650 asm lines |
| 2. the helicopters never moved | ported, 573 asm lines; **oracle 78 % -> 100 %** |
| 3. `state_op_unk`, the last simulation stub | ported, 170 + 121 asm lines |
| 4. two physics deviations | T_HELL4 **root-caused and fixed**; PIPEFLIP still open |
| 5. the skybox AND-blit | **resolved** - the routine's name was a mislabel |

### 1. `preRender_sphere`: the comment was false, and grep is why

`rdraw_dispatch.c:90` called `fatal_error("preRender_sphere: not ported (no
source in reference)")`, and the comment above it said the routine "has no
proc body in any asm/ or asmorig/ file". Both statements were wrong. The code
is right there:

| Ported | Source | asm lines |
|---|---|--:|
| `preRender_sphere` | restunts1 seg012.asm 10164..10367 | 204 |
| `preRender_sphere_helper` | restunts1 seg020.asm 53..88 | 36 |
| `preRender_sphere_helper2` | restunts1 seg015.asm 50..440 | 391 |
| `putpixel_single_maybe` | restunts1 seg012.asm 17884..17929 | 46 |
| `off_3F3C8` / `byte_3F418` / `word_3F3C6` | dseg.asm 16433..17252 | 780 bytes |

**How the false claim survived is worth writing down.** Seven of the asm
files - seg001, seg004, seg009, seg010, seg012, seg023, seg028 - contain
bytes that make GNU grep classify them as binary. A plain
`grep preRender_sphere .../asm/*.asm` therefore prints only the seg006 call
site and the `extrn` in seg012.inc, and **silently skips the one file that
holds the body**. `grep -a` finds it immediately. seg012.asm:287 has the
matching `public`; the `extrn` in the .inc is the importing side. Anything
concluded from a grep over that reference tree without `-a` should be
re-checked.

What the routine does: the vertical diameter is `arg_4 * 13/16` and the
horizontal radius is 5/4 of the vertical one, which is the 320x200 pixel
aspect. Under a vertical radius of `word_3F3C6` (= 40, and dseg gives that
table exactly 40 entries) the ellipse is scan-converted straight out of
`off_3F3C8`'s half-width tables; at or above it, `preRender_sphere_helper2`
builds a 32-gon out of normalised sums `P1 + k/4 P2` - the same construction
as `preRender_wheel_helper3`, at twice the angular resolution - and hands it
to `preRender_default_alt`.

The 780-byte table was transcribed by parsing dseg.asm; the offset formula
`byte_3F418 + r*(r-1)/2` was checked against all 40 `dw offset byte_XXXXX`
operands and holds for every one.

**One bug in the translation, caught by looking at the picture.** `mov
[si+40h], ax` in helper2's mirror loop is a *byte* displacement, i.e. 32
int16 on, not 16. Written as `sip[16]` the 32-gon came out as a self-crossing
mess that filled the whole screen; a size check would never have caught it.
`sip[32]` gives a clean circle.

**Verification.** A sweep of diameters 0..104 at (160,100) plus the four
clip cases:

* diameter 0 draws nothing, 1 draws one pixel, 2..96 use the table path;
* the table path holds `w/h` between 1.27 and 1.38 (5/4 plus quantisation);
* the two paths meet cleanly: d=96 (table) gives w=99 h=78 px=6138, d=97
  (32-gon) gives w=97 h=79 px=5947 - no step at the switchover;
* off-screen left/right/top/bottom clip to the right half-shapes, a centre
  200 px outside the window draws nothing, and a negative diameter returns
  without drawing (the `or dx,dx / jg` is a signed test);
* the rendered picture is six round ellipses, not a smear.

**How reachable was the crash? Measured, and the honest answer is "not
yet".** Walking every loaded `SHAPE3D`'s primitive stream (stride
`2 + numpaints + primidxcounttab[type]`, 0 malformed records over 123 shapes)
for all 116 scenery shapes and all 11 cars: the shipped data holds **exactly
one** primitive of file type 11 (-> primtype 2), in `car0` of **PMIN**
(`game3dshapes[124]`), and **no** primitive of file type 1 (-> primtype 5,
the single pixel). Slot 124 is written only by `run_car_menu`
(seg000.asm:3029) - the rotating model on the car-selection turntable - and
`putpixel_single_maybe`'s other two call sites are `run_car_menu`
(seg000:3699) and `intro_op` (seg003:7003, 7110). None of those three screens
is ported yet, so over all 12 replays in `tests/replays`, every camera mode
and detail level, the measured call count is **0**. The crash was latent, not
live. It becomes live the moment Phase 7's turntable or Phase 10's intro
lands, which is why it is fixed now.

### 2. The helicopters: the port was bypassing the frame driver

`main_native.c` and `tools/dump_native_states.c` each called `player_op()` and
`opponent_op()` from their own loops. The original never does that. Both are
called from `update_gamestate` (seg001.asm 4395..4603), which in one fixed
order also:

1. reads the recorded input byte itself, out of `td16_rpl_buffer` at the
   **current** `game_frame`, and only then increments `game_frame` - so
   `player_op` runs with the frame counter already advanced, which the port's
   loops did not do;
2. snapshots the whole 1120-byte GAMESTATE into a 20-slot "cvx" ring every
   `word_45A00` (= 30*fps) frames - the rewind target `loop_game` uses;
3. runs `move_helicopters`, then `sub_19BA0`, then `audio_carstate`.

| Ported | Source | asm lines |
|---|---|--:|
| `update_gamestate` | restunts1 seg001.asm 4395..4603 | 209 |
| `move_helicopters` (`sub_2298C`) | restunts1 seg005.asm 1552..1923 | 372 |
| `sub_19BA0` | restunts1 seg001.asm 9386..9507 | 122 |
| `init_game_state` (the missing preamble) | restunts1 seg001.asm 3885..4021 | 137 |
| `get_kevinrandom` / `_seed` | restunts1 seg002.asm 206..267 | 62 |

`state.game_vec1[i]` is the helicopter that carries **camera mode 1** for car
i (`frame.c:194`), and `state.field_3F7[i]` is the nearest trackside camera
(`frame.c:214`). Neither was ever written, so the helicopter camera sat at the
world origin and the trackside view always used camera 0.

`move_helicopters` chases the car's look-ahead point `car_vec_unk3` - or the
car's own position when it is crashed, off the racing line, or outside the
`field_48` window - at up to 0x78 units per frame (0xF0 at 10 fps), never
closer than a radius of 0x1C2, while the height chases `(car_y >> 6) + 0x10E`
at up to 30 units per frame. Every `framespersec/2` frames it re-points
`field_3F7[i]` at the nearest of the `byte_4616E` cameras in `trackdata9`.

Also missing: `init_game_state`'s preamble set `state.field_3F4 = 1`,
`state.game_frames_per_sec = 1` and all four helicopter vectors (the
helicopter starts 0x1000 behind the start tile, 0x200 to its right and 0x3C0
above the hill height). The port only had the *car* placement half of that
routine, which is why three more GAMESTATE bytes differed from frame 1.

`sub_19BA0` is the crash-debris animator (see defect 3).

**Call-order change, judged by the oracle, before and after.**

    track      before                          after (this change only)
    TUBETEST   16/16   78.46 %                 16/16   81.98 %
    HILLTEST   16/16   78.32 %                 16/16   81.98 %
    PIPEROLL   16/16   95.37 %                 16/16   99.04 %
    T_HELL2    16/16   95.42 %                 16/16   99.04 %
    T_HELL4    14/16   78.00 %                 14/16   81.46 %
    PIPEFLIP   10/16   79.59 %                 10/16   83.25 %

Every field that was 16/16 stayed 16/16, and the 26 bytes that differed in
frame 1 of every single recording dropped to 6 - the six of
`state.kevinseed`, and nothing else. The ones that disappeared were
`game_vec1[0]`, `game_vec1[1]`, `game_vec3`, `game_vec4`,
`game_frames_per_sec` and `field_3F4`. Over the whole 900-frame runs the four helicopter
vectors are now **byte-identical to the DOS dump on 900 of 900 frames on all
six recordings**, and so is `field_3F7`. (Immediately after this change
T_HELL4's helicopters first differed at frame 374 - the car's own deviation,
fixed under defect 4 below; PIPEFLIP's still match 900/900 because its
divergence is smaller than the `>> 6` the helicopter code quantises to.)

Physics against `build/probe/phys9.txt` is **byte-identical, all 30 sampled
lines**, before and after - the reordering changes when `game_frame` is
incremented relative to `player_op`, and the oracle says the new order is the
right one.

### 3. `state_op_unk`: the crash debris

`sfstubs.c`'s slot `[3]` was a no-op. It is the crash-debris spawner
(seg001.asm 9215..9385), and its partner `sub_19BA0` (9386..9507) - which
`update_gamestate` runs every frame while `state.field_42A` is set - flies
and lands the pieces. `state_op_unk` fills up to 18 of the 24 splinter slots
(8 on the wheel-spray path), each with a random spin from
`get_kevinrandom`, a launch heading `n/var_12` of the way round a 0x400
circle (or a 0xC0 arc centred on the car's heading), and a launch speed of
`random*6/4 + arg_4 + 0x180`.

Two callers: `update_crash_state` (statecrs.c:136) with `arg_0` 0 or 1 - the
full explosion - and `stateply.c:3143` with `arg_0 = wheel+2` - the spray off
one wheel.

`get_kevinrandom` (seg002.asm 232..267) is a six-byte lagged-Fibonacci
generator that folds downwards and then increments as a big-endian counter;
it did not exist in the port at all (`g_kevinrandom_seed` was declared in
`externs.h` and defined nowhere).

**[DEVIATION] the RNG's starting state cannot be derived from the dumps.**
Every oracle recording shows the same six bytes in `state.kevinseed` at frame
1 - `29,178,77,215,110,1` - because the DOS harness had already drawn from
the generator in the menus. The port boots from the dseg zeros. Nothing in
the simulation reads `state.kevinseed` (only `restore_gamestate` does, for a
rewind), so it costs six bytes of the frame-1 comparison and nothing else -
**except** the debris, which is drawn from the same stream.
`tools/dump_native_states.c` therefore takes `STUNTS_KEVINSEED=a,b,c,d,e,f`
to adopt the oracle's starting state for a comparison run. That knob is a
measurement aid, not a fix; the playable build does not use it.

With it, the crash debris - **102233 non-zero bytes** of `game_longs` in the
DOS dump for TUBETEST, 90240 for T_HELL4, 83977 for PIPEFLIP - is
**identical on every frame of all six recordings**. Without it, `game_longs`
first differs at the frame of the first crash, and the difference is the
random numbers, not the code. That is the check that `state_op_unk`,
`sub_19BA0` and `get_kevinrandom` are all right.

Visually: 4085 frames into `00_default_hell5_full.rpl` the car explodes and
blue body panels arc through the air and fall. Before this change nothing
spawned at all.

### 4. The two physics deviations: one fixed at the root, one still open

#### T_HELL4 frame 374 - FIXED. `vector_op_unk` was a wrong reconstruction.

The method was to narrow, not to guess. At frame 373 the whole GAMESTATE
matched; at 374 **only** `posWorld1`, `field_48` and the wheel world
coordinates differed, and all four wheels differed by exactly the same
`(-6, 0, +1)` - a rigid translation, with the rotation, all five `rc` arrays,
the speeds and the gear identical. Seeding the run from the oracle's own
frame 373 reproduced the divergence exactly, so it was a single frame's
arithmetic, not accumulated drift.

Tracing `update_player_state` at that frame showed the car was scraping a
wall (`field_CF` = 0x11 in *both* dumps, and `car_36MwhlAngle` = -130 in
both, which pins `var_EE` = 65 and `vec_FC.x` = 768 in both). Our per-frame
translation was `(1362, 0, 2840)`; the oracle's was `(980, 0, 2917)`. An
exhaustive sweep of every `(var_F2, var_EE)` the wall path could possibly
produce showed that no value reproduces the oracle - so the difference had to
be upstream, in the one call that feeds `var_F2`.

That call is `vector_op_unk`, and **restunts1 never disassembled it**. The
vendored `c/math.c` carries a hand reconstruction, and the reconstruction is
wrong in two ways. restunts2's Ghidra export does have the body
(seg012.asm 9592..9640):

    mov ax,[bp+i] ; mov [di+4],ax ; sub ax,[bx+4] ; mov [bp+var_4],ax
    mov ax,[si+4] ; sub ax,[bx+4] ; or ax,ax ; jge LAB_4620
    shr word ptr [bp+var_4],1        <-- LOGICAL, on 16 bits
    shr ax,1                         <-- LOGICAL, on 16 bits
    LAB_4620: mov [bp+var_2],ax
    mov ax,[si] ; sub ax,[bx] ; imul [bp+var_4] ; idiv [bp+var_2] ; add ax,[bx]

The reconstruction declares `var_4`/`var_2` as `long` and halves them with
`>>` on a signed value - **arithmetic**. The original uses `shr` on 16-bit
words - **logical**. The halving only runs when `vec1->z - vec2->z` is
negative, and a logical shift of a negative word lands just under 0x8000: both
operands come out near 32768, their ratio collapses to ~1, and the
interpolation returns (very nearly) `vec1` instead of a point between `vec1`
and `vec2`. That is a real behaviour of the shipped game, not a rounding
error.

At T_HELL4 frame 374, with `vec_1C = (-129,0,-11)`, `vec_C = (-82,0,9)`,
`i = 0`:

    reconstruction: var_4=-5, var_2=-10  ->  (-47 * -5)/-10 + -82 = -105
    original:       var_4=0xFFF7 shr 1 = 32763
                    var_2=0xFFEC shr 1 = 32758
                    (-47 * 32763)/32758 = -47, -47 + -82        = -129

An exhaustive search over every `vec_FC` the rest of the wall path could
accept had already said the oracle needs **exactly -129**. Fixed in
`src/render_faithful/math.c`, instruction by instruction. T_HELL4 went from
14/16 / 78.00 % to **16/16 / 100.00 %**, and every other track is unchanged
or better.

`vector_op_unk` is also the renderer's near-plane clipper (shape3d.c:1199,
1365), so this changes clipped polygons too. Checked: paint check still 0
unpainted pixels over HELL5's 10646 frames, all 39 tracks still start, and
cockpit / chase / helicopter / trackside frames still render the road,
buildings, fence, "Wrong Way" banner and turn arrow correctly.

#### PIPEFLIP frame 423 - still open, but narrowed

Not fixed, and not fudged. What is established:

* frames 1..422 are byte-identical (with the RNG seed, 100 % of the compared
  region);
* frame 423 is a suspension bottom-out - `field_CF` = 0x20 in both dumps -
  and **not** a wall scrape, so the `vector_op_unk` fix does not touch it;
* everything that describes the car's *dynamics* at 423 matches exactly:
  `car_rc1`..`car_rc5`, both speeds, `car_speeddiff`, `car_pseudoGravity`,
  `car_steeringAngle`, `car_surfaceWhl`, the gear, `car_vec_unk3/4/5`;
* what differs is small and geometric: `car_rotate` by `(+2, 0, -3)`,
  `posWorld1` by `(4, 3, 19)` in 64x units, and four of the twenty-four wheel
  coordinate components by exactly 1;
* the rotation is *derived* from the wheel positions
  (`stateply.c:2270`, `polarAngle` over the diagonals of `vec_1DE`), so the
  rotation difference is downstream of a wheel-position difference of about
  one unit, not a separate bug.

Ruled out by reading the asm against the port, instruction by instruction:

* `mat_mul_vector` (restunts1 seg012.asm 8981..9070) - the `imul` /
  `shl ax,1 / rcl dx,1` x2 / take `dx` sequence is exactly `>> 14` on the
  32-bit product with a 16-bit store, and the accumulation into
  `[di+VECTOR.vx]` wraps at 16 bits in both;
* `vec_normalInnerProduct` (restunts2 seg001.asm 9133..9200) - the divide is
  `__aFldiv`, which truncates toward zero like C;
* `plane_origin_op` (restunts2 seg001.asm 9065..9132) - PLANE stride 0x22,
  field offsets and the `index < 4` early return all match;
* `vector_op_unk` - the fix above changes nothing here, measured.

Still 10/16 and 98.30 % (up from 10/16 and 79.59 %). The remaining work is to
find which routine in the wheel-contact path rounds by one unit; the two
tools that cracked T_HELL4 - seeding the dumper from the oracle's previous
frame, and sweeping a routine's output for the value the oracle demands - are
the way in.

### 5. The skybox AND-blit: resolved, and it was a mislabel

The note at the top of `rskybox.c` had been narrowed twice and was still
open: the horizon is drawn by a routine called `sprite_putimage_and_alt`,
`dest & src` differs from a plain copy on 100 % of 729600 blitted pixels and
paints a flat olive band, and a plain copy reproduces the reference capture.

The proc bodies settle it. `sprite_putimage_and_alt` (seg012.asm
11762..11787) is 25 lines that load `ds:si` and two coordinates and then
`jmp short loc_33BF5` - and **loc_33BF5 is at seg012.asm:11813, inside
`sprite_putimage` (11788..11941), the plain copy**. It is not a variant of
`sprite_putimage_and` at all; it is a third entry point into the ordinary
copy blitter whose only job is to take the destination x/y as arguments
instead of reading them out of the SHAPE2D header. (`nopsub_33B98`,
11734..11761, is the second such entry, taking coordinates relative to the
shape's own position.) The genuine AND blitter is `sprite_putimage_and`
(11274..11465, `lodsb / and es:[di],al`), and `sprite_putimage_and_alt2`
(11246..11273) jumps into *that* one, at `loc_338C9`.

Every call site agrees: the 19 calls to `sprite_putimage_and_alt` - seg000
x4, seg003 x7, seg005 x6, seg009 x8, i.e. the cockpit, the horizon, the
in-race overlays and the track editor's icons - all draw finished pictures,
while the five genuine AND calls (`sprite_putimage_and` in seg008 x1 and
seg009 x4, `sprite_putimage_and_alt2` in seg005 x2) draw masks.

So the port's plain copy is correct, and it is correct for the *right*
reason. The comment in `rskybox.c` has been rewritten to say so.

### Verification (the numbers)

`bash tools/build_dumper.sh` + `python3 tools/diff_oracle.py`, all 900-frame
runs. "fält" is the 16 tracked playerstate fields, "byte" the byte agreement
over the compared region:

| track | before Phase 5 | after Phase 5 | after, with `STUNTS_KEVINSEED` |
|---|---|---|---|
| TUBETEST | 16/16 78.46 % | 16/16 89.11 % | **16/16 100.00 %** |
| HILLTEST | 16/16 78.32 % | 16/16 89.11 % | **16/16 100.00 %** |
| PIPEROLL | 16/16 95.37 % | 16/16 99.04 % | **16/16 100.00 %** |
| T_HELL2 | 16/16 95.42 % | 16/16 99.04 % | **16/16 100.00 %** |
| T_HELL4 | **14/16** 78.00 % | **16/16** 90.16 % | **16/16 100.00 %** |
| PIPEFLIP | 10/16 79.59 % | 10/16 88.92 % | 10/16 98.30 % |

* Without the seed the frame-1 difference is exactly the six bytes of
  `state.kevinseed` on every track (was 26 bytes).
* `game_longs` - the crash debris, 102233 / 90240 / 83977 non-zero bytes in
  the DOS dumps - is **identical on every frame of all six** with the seed.
  Without it, it first differs at the first crash, from the random numbers.
* **Physics:** `--track DEFAULT --car coun --headless 600` is byte-identical
  to `build/probe/phys9.txt`, all 30 sampled lines.
* **Paint check:** 0 of 10646 frames of `00_default_hell5_full.rpl` had
  unpainted pixels.
* **All 39 shipped tracks start:** 39 run, 0 failures.
* **The 4x build** (`RFB_SCALE=4 bash tools/build_native.sh`) compiles and
  runs headless.
* **Sphere sweep:** diameters 0..104 plus five clip cases, table path and
  32-gon path continuous at the d=96/97 switchover, picture inspected.
* **Reachability of the sphere:** exactly 1 sphere primitive in all shipped
  3D data (PMIN `car0`), 0 pixel primitives, 0 calls over 12 replays.
* **Helicopters:** `game_vec1`/`game_vec3`/`game_vec4`, `field_3F7` and
  `game_longs` each identical to the DOS dump on 900/900 frames, all six
  recordings (with `STUNTS_KEVINSEED` for `game_longs`).
* **Pictures looked at:** helicopter camera over 4 frames of DEFAULT,
  trackside camera over 4 frames, the crash at HELL5 frame 4085 over 6
  frames, and 4 frames of ordinary cockpit driving after the
  `vector_op_unk` change.

### New wiring

* `sim_hook_audio_frame` (sfasm_port.c, installed by `main_native.c`) stands
  in for `update_gamestate`'s two `call audio_carstate` sites, so
  `bin/dump_native_states` - which links the simulation but not SDL - builds
  and behaves exactly as before. Same pattern as `audio_hook_unk3`.
  A side effect that is *more* faithful: `audio_carstate` no longer runs
  while `game_inputmode` is 0, i.e. before the driver touches a key, which
  is what the original does.
* `cvxptr` (20 x 1120 bytes) and `word_45A00` (= 30*fps) are now allocated
  and set, in both harnesses, where `init_game_state` sets them.
* `--camera 0..3` on `bin/stunts_native`, so the helicopter and trackside
  views can be screenshotted without a keyboard.
* `STUNTS_KEVINSEED=a,b,c,d,e,f` on `bin/dump_native_states` (see the
  [DEVIATION] under defect 3).
* `byte_4393C` and `byte_449DA` were declared in `externs.h` and defined
  nowhere; they are defined in `rdata.c` now.

### [ODDITY] - four, all reproduced

* **`preRender_sphere` leaves stale left-edge values.** The
  horizontal-clip skip at seg012.asm loc_331AD is taken *after* the row
  pair's left x has already been stored. Harmless in practice - the
  half-width table is monotonic, so skipped rows are always the outermost
  ones and fall outside the range finally drawn - but the original never
  clears them.
* **A negative sphere diameter draws nothing.** `or dx,dx / jg` is a signed
  test, so a diameter that rounds to zero draws one pixel and a negative one
  returns having drawn nothing at all.
* **`state_op_unk`'s heading mask is applied to the high byte only.**
  seg001.asm:9345 is `and ah,3`, which masks to 10 bits and to the 0x400
  circle at once; a negative `var_6` keeps its low byte.
* **`state_op_unk` divides by `var_12` without checking it.** `var_12` is
  the number of free splinter slots, and can be 0 - but only when every slot
  is taken, and then the loop containing the divide is unreachable. The
  original relies on exactly that; so does the port.

### [DEVIATION] - stated plainly

* **`putpixel_single_maybe` writes `rfb_pixels` directly.** The original
  stores through `es:[sprite_lineofs[y] + x]` with `es` from
  `sprite_bitmapptr`. This port has one framebuffer and never fills either
  field - the same deviation `draw_filled_lines` already carries - so the
  address arithmetic becomes `rfb_pixels[y*RFB_W + x]`. The clip test itself
  is the original's, and note it uses `sprite_left`/`sprite_right` where
  `preRender_sphere` uses `sprite_left2`/`sprite_widthsum-1`.
* **`preRender_sphere`'s edge buffers are `RFB_SPANROWS`, not 480.** So they
  still fit at `RFB_SCALE > 1`. The table path can never write past index 77
  anyway, since `word_3F3C6` caps the radius at 40.
* **The RNG start state is not reproducible** - see defect 3.
* **`update_gamestate`'s attract-mode tail is ported but never exercised.**
  The `game_replay_mode == 1` branch that noses the car back to the start
  tile needs `byte_4393C`, which only `loop_game` (Phase 9) sets. It is
  transcribed and compiles; nothing has run it.

### Not done

* PIPEFLIP frame 423, above.
* `update_gamestate`'s checkpoint ring is written but nothing reads it -
  `restore_gamestate` and the rewind belong to Phase 9.

## Two bugs from the interactive path (2026-08-17)

Both were reported by the user from actually playing, and neither could be
reached by the headless test bar as it stood. Worth recording as a pattern:
the bar drives the *simulation* well and the *interface* not at all.

### The opponent's car crashed the loader when it matched the player's

`shape3d_load_car_shapes` (shape3d.c:3317) has two branches. If the opponent
drives a different car it loads `ST<id>.P3S` as usual; if it drives the *same*
car it does not re-read the file, it duplicates the archive in memory:

```c
var_6 = mmgr_get_chunk_size_bytes(carresptr);
car2resptr = mmgr_alloc_resbytes("car2", var_6);
for (i = 0; i < var_6; i++) car2resptr[i] = carresptr[i];
```

That is correct DOS: a resource handle *is* the bytes, and `locate_shape`
parses them. Our shim had drifted from that. `file_load_resource` returns a
pointer registered in a table that also holds the parsed archive, and
`locate_shape_nofatal` looked the pointer up and used the parse. A chunk from
`mmgr_alloc_resbytes` has no parse attached, so the search returned NULL,
`locate_shape_fatal("car0")` called `fatal_error`, and the process aborted
before the first frame.

The fix keeps the game code untouched and repairs the shim: the archive header
parse moved out of `stunts_asset_load_archive` into
`stunts_asset_adopt_archive(data, len, take_ownership)`, and
`locate_shape_nofatal` builds a non-owning view over a plain chunk the first
time one is searched. Any other faithful code that copies an archive now works
for the same reason the original does.

Measured: all 11 cars as the opponent, and all 11 same-car pairs, 22/22 —
against 1/11 failing before, which is why it looked so specific.

### The gear knob was drawn 12 px right and 11 px down

The `_alt` suffix on the seg012 blitters marks "position given by the caller",
but it does **not** mean they all treat that position the same way. Two of them
subtract the shape's own anchor first and two do not:

| Entry point | seg012 | `x - s2d_unk1`? | used by |
|---|--:|:--|---|
| `sprite_putimage_or_alt` | 12477 | **yes** | gear knob art |
| `sprite_putimage_and_alt2` | 11246 | **yes** | gear knob mask |
| `sprite_putimage_and_alt` | 11762 | no | horizon, cockpit, editor icons |
| `sprite_putimage_transparent` | 12343 | no | explosions, arrows |

The knob is 24x23 with anchor (12,11) — its centre. Missing the subtraction put
it half out of the bottom of the gate. The data settles the intent completely:
the Countach's six knob points are (22,24) (22,42) (34,12) (34,42) (46,12)
(46,42), and printing `gbox`'s own pixels shows its three slots centred on
columns 22, 34 and 46, with the slot ends at rows 12 and 42. The knob centres
are the slot ends, exactly.

Two further deviations in the same call site, from the same reading of
loc_230DE:

* The **mask pass was missing**. The original draws `gnab` with AND and then
  `gnob` with OR — `gnab` is 0xFF outside the knob's circle and 0 inside,
  `gnob` the reverse, which is an ordinary masked sprite done in two passes.
  Only the OR was implemented.
* The composition is **clipped to the gate**, because the original composes
  into an offscreen sprite of `gbox`'s size and blits that window to the
  screen. `blit_window_set/reset` in rshape2d.c stands in for
  `sprite_set_1_from_argptr` without allocating a second framebuffer.

Adding the AND pass then exposed a third bug, in our own compositor rather than
the port. `put()` wrote the cockpit to `rs_rgba` *instead of* `rfb_pixels` when
truecolour output was armed, so the AND read an index buffer that had never
received the gate and cleared the knob's surroundings to black. `put()` now
writes both. Nothing downstream reads the index buffer in a way that notices:
`present()` rebuilds `frame_rgba` from it at the top of each frame and only
then draws the cockpit over the result, `--paint-check` refills it before every
frame, and the menus clear `frame_rgba` themselves and never read it.

New tooling from this: `tools/dump_shape2d.c` prints every sub-resource's
SHAPE2D header (size, anchor, screen position, transpose flag) for any archive,
and `STUNTS_GEAR=<0..5>` holds the gate open on one gear so it can be rendered
without arranging a real shift.

Verification after the change, all re-run rather than assumed: DOS oracle
TUBETEST, HILLTEST, PIPEROLL, T_HELL2 and T_HELL4 all 16/16 fields and 100.00%
byte agreement, PIPEFLIP unchanged at 98.30%; 0 unpainted pixels across all 12
replays; 39/39 tracks; 12/12 replays; 11/11 cars.

## Phase 6: the results screen (2026-08-17)

The last gap *inside* the game loop. Until now the player crossed the line and
got four numbers from a native placeholder. `end_hiscore` is now ported in
full, together with the four seg008/seg012 widgets it shares with the menus,
the dialogs and the track editor.

| Ported | Source | asm lines | Lands in |
|---|---|--:|---|
| `end_hiscore` | seg000.asm 5126..7089 | 1964 | `rendscreen.c` |
| `draw_button` | seg008.asm 3615..3897 | 283 | `rwidgets.c` |
| `draw_lines_unk` | seg008.asm 3453..3614 | 162 | `rwidgets.c` |
| `sprite_1_unk` | seg012.asm 10875..10979 | 105 | `rwidgets.c` |
| `sprite_1_unk4` | seg013.asm 50..152 | 103 | `rwidgets.c` |
| `sprite_shape_to_1_alt` | seg012.asm 11997..12064 | 68 | `rwidgets.c` |
| `shape2d_op_unk5` + the shared body | seg012.asm 12093..12204 | 112 | `rwidgets.c` |
| `print_int_as_string_maybe` | seg008.asm 4097..4191 | 95 | `rfont.c` |
| `get_super_random` | seg008.asm 4465..4502 | 38 | `rendscreen.c` |
| `mouse_multi_hittest` (the rectangle test) | seg008.asm 3049..3108 | 60 | `rendscreen.c` |
| `mouse_timer_sprite_unk` (the blink) | seg008.asm 4201..4271 | 71 | `rendscreen.c` |

restunts2 carries the same procedure as `end_hiscore_asm_` (seg000.asm
5005..6927, 1923 lines) and was checked against the transcription at every
point where restunts1's listing is ambiguous - in particular the three
`smart` / `nosmart` blocks, which restunts1 prints as assembler directives
around a bare `and ax, 1`. restunts2 shows them as plain `and ax, 0x1` /
`and ax, 0x3` and agrees with restunts1 everywhere else. It adds nothing new.

### What the screen actually is

Two bevelled panels, drawn by `draw_button` with a null label:

    draw_button(NULL, 0, 0,    0x140, 0x64, 15, 8, 7, 0)   rows   0..100
    draw_button(NULL, 0, 0x65, 0x140, 0x63, 15, 8, 7, 0)   rows 101..200

(The second one is `h = 0x63`, i.e. it reaches the bottom of the screen, not
`y=101..164` as the survey had it.)

The **lower** panel holds the statistics, centred with `font_op2_alt`, from
`y = 0x6B` in steps of 10, every line assembled in the 0xAC74 scratch area:

    eelt "Elapsed time: "  + (total - penalty), or + ednf "DNF"
                           + econ " (cont)" when byte_43966 bit 1 is set
    eppt "Penalty time: "  + penalty                (only when non-zero)
    eowt "Opponent Winning time: " / eolt "Opponent time: " + field_144,
                           or eolt + ednf           (only with an opponent)
    eavs "Average speed: " + travDist/(pEndFrame+elapsed_time1) >> 8 + emph
    eimp "Impact speed: "  + impactSpeed >> 8 + emph (only when non-zero)
    etop "Top speed: "     + topSpeed    >> 8 + emph
    ejum "Jumps: "         + jumpCount              (only when non-zero)

The **upper** panel holds one of two pages, toggled by the leftmost button:
the *evaluation* - the opponent's portrait and its taunt - or the fastest-times
table, which is Phase 3's `highscore_text_unk()` unchanged.

Four buttons at `y = 0xAF`, `w = 0x46`, `h = 0x15`, from the dseg tables
`word_3BCEC` (x1 = 4, 84, 164, 244) and `word_3BCF6` (x2 = 75, 155, 235, 315)
with `hiscore_buttons_y1/_y2` = 174/197:

    0  ebev "View]Eval" / ebhi "View]High"
    1  ebrp "View]Replay"
    2  ebra "Race" (with an opponent) / ebdr "Drive"
    3  ebmm "Main]Menu"

Button 0 exists only when there is **both** an opponent and a usable table;
otherwise the other three shift left by `var_9C` = 0xFFDC (-36), which is what
centres them, and the hit test is run against `&x1[1]`, three entries, with 1
added to the result (loc_14343). `end_hiscore` returns `selectedmenu - 1`, so
0 = view the replay, 1 = race again, 2 = the main menu - which is exactly how
seg000:104C0 reads it.

### The verdict is decided by the *opponent's* time line

`var_18` is set inside the statistics block, not before it:

* `field_144 == 0` (the opponent did not finish) and we did -> **0, we won**;
* the opponent's time is lower than ours, or we did not finish -> **1, the
  opponent won**, and the line reads "Opponent Winning time";
* neither -> **2**, which is what a quit with nobody home looks like.

`var_18 == 0` starts SKIDVICT, anything else SKIDOVER - so racing the clock
always ends on the game-over song, which is the original's own behaviour and
looks odd until you read the compare (`cmp var_18, 0 / jnz`).

And "win"/"lose" are from the **opponent's** point of view. `var_18 == 1` -
the opponent beat us - is the branch that loads `OPP<n>WIN.PVS` and the `ev*`
lines, i.e. the opponent gloating.

`var_16` decides whether the evaluation page exists at all: it is
`game_opponenttype`, cleared when `var_18 == 2` and `pEndFrame != oEndFrame`.

### The portraits and the taunts are data, and here is the format

`winn` and `lose` are resources in **OPP\<n\>.PRE**, not in the .PVS - a
NUL-terminated run of 1-based frame numbers. Each byte plus `'0'` patches the
fourth character of the name `"op01"` and `locate_shape_fatal` pulls that frame
out of `OPP<n>WIN.PVS` / `OPP<n>LOSE.PVS`. Decoded from the shipped files:

    OPP1 winn 1 1 1 1 2 3 3 3 3
         lose 1 1 1 1 2 3
    OPP2 winn 1 1 1 1 2 3 4 5 6 6 6 6
         lose 1 1 1 1 2 3 4 5 6 6 6 6
    OPP3 winn 1 1 1 1 2 3 4 5 6 7 7 7 7
         lose 1 1 1 1 2 3 4 4 4
    OPP4 winn 1 1 1 1 2 3 4 5 6 7 8 8 8 8
         lose 1 1 1 1 2 3 4 5 6 5 6 5 4 3 2 2 2 2
    OPP5 winn 1 1 1 1 2 3 4 5 6 7 8 8 8 8
         lose 1 1 1 1 2 3 4 5 6 7 7 7 7
    OPP6 winn 1 1 1 1 2 3 4 5 6 7 8 8 8 8
         lose 1 1 1 1 2 3 4 5 6 7 8 7 6 5 4 3 2 2 2 2

One step every 0x1E ticks. The frames are not all the same size - 120x79,
120x87, 128x79, 112x76, 104x74 - which is why the picture is placed from the
header rather than from a constant: `x = 0x138 - width * video_flag1_is1`,
`y = (0x63 - height) >> 1`, with a `draw_lines_unk` frame three pixels out.

A taunt line id is three characters: `'v'` (0x76) or `'d'` (0x64), a sequence
digit `'1'..'3'`, and `'a' +` a random 0..3 - so `ev1a`..`ev3c` and
`ed1a`..`ed3c`, which is what the archives carry. The single-line case
(`var_18 == 2`) uses the literal `"d4a"`. The three lines are word-wrapped
*as one paragraph* into `portrait_x - 16` pixels of FONTN starting at (8,8),
so they read as one speech.

The middle line's index comes from `get_kevinrandom() + gState_frame` masked to
1 (win) or 3 (lose); the outer two come from `get_super_random() % 3` with an
anti-repeat step through `word_3BCDE` = {2, 0, 1}. **The `end_hiscore_random`
that loc_13801 computes is overwritten a few instructions later in every path
that reaches it** - it is transcribed anyway, because the anti-repeat step
reads and rewrites `word_40D3C`, and that value does survive to the next race.

### The run only counts if the track has not been edited

loc_138FF reloads `<track>.TRK` and compares 0x385 bytes against
`td14_elem_map_main`. Anything else - a mismatch, or a file that will not
load - sets `var_6E` to 0xFF, and 0xFF means no table at all: the top panel
says `ehna` "High score table is unavailable!" at y=0x32 and button 0
disappears. That is also what a `.HIG` that can be neither read nor created
produces.

`enter_hiscore`'s third argument is `var_18`, and it goes **straight into byte
41 of the record** - the byte `print_highscore_entry` brackets on. So a time
set while the opponent won is stored parenthesised. Phase 3's placeholder
passed `opponent != 0` there, which is not the same thing; it is fixed.

### `video_flag1_is1` was declared and never defined

`externs.h:221` declared it, nothing defined it, and nothing used it - so the
link never noticed. It is a multiplier on shape widths (`imul video_flag1_is1`
in seg000 x4, seg005 x3, seg009 x4) that seg031.asm:236 sets to 1 when the
MCGA mode comes up. `end_hiscore` is the first ported caller: with it left at
the dseg zero the portrait would be placed at x = 312 and drawn off the edge.
Defined in `rdata.c`, set in `game_init` beside the other three video flags.

### [DEVIATION] - stated plainly, because a UI screen has no oracle

Behaviour-exact, not instruction-exact, in these places and nowhere else. The
layout, every colour index, every string, every rectangle and all of the
arithmetic are the original's.

* **No sprite windows.** The original renders into an offscreen
  `sprite_make_wnd(0x140, 0xC8, 0x0F)`, shows it with `sprite_blit_to_video`,
  and repaints only the strip that changed. This port redraws the whole
  picture every frame into the presented buffer, as every other ported menu
  does. The `video_flag5_is0` double-buffered portrait path (loc_13E0F,
  loc_142B0) is consequently dead - as it is in the original whenever that
  flag is clear, which `game_init` makes it.
* **No DOS mouse and no event loop.** `mouse_timer_sprite_unk`'s blink is
  kept (word_407CE = 5 / word_407D0 = 14, half the 60-tick cycle each) and its
  selection outline is `sprite_1_unk4` as in the original; `mouse_multi_hittest`
  is reproduced against the same rectangles. The loop itself is SDL, in
  `main_native.c`.
* **No `show_dialog`.** The name entry (`enter_hiscore` -> `show_dialog(3,…)`
  -> `call_read_line`) is `SDL_TEXTINPUT`, as Phase 3 already had it. The
  `ihd` disk dialog on a `.TRK` that will not load is skipped: a missing track
  file means no high score, which is the branch the dialog leads to when it is
  cancelled.
* **`get_super_random` loses one of its four terms.** The original is
  `rand() + get_kevinrandom() + timer_get_counter() + gState_frame`, made
  positive. There is no PIT counter here; the other three are the original's
  and the result is only ever taken modulo 3 or 4.
* **`draw_button` and `draw_lines_unk` write to the framebuffer**, in 320x200
  coordinates scaled by RFB_SCALE, instead of through
  `es:[sprite_lineofs[y] + x]`. Same deviation `draw_filled_lines`,
  `putpixel_single_maybe` and `rshape2d.c`'s blitters already carry. The
  twelve `preRender_line` calls, their order and their endpoints are exact.
* **`shape2d_op_unk5` clips to the framebuffer**; the original does not clip
  at all.
* `end_hiscore`'s return value is produced and printed but the host does not
  act on 0 (view replay) or 1 (race again) yet - those need `run_game` to be
  callable a second time, which is Phase 9's `loop_game` host.

### [ODDITY] - four, all faithful and all reproduced

* **The taunt's resource id relies on a NUL left behind by the previous
  line.** loc_13B42 writes three characters into `var_12` and never
  terminates them. It works because `var_12` was last written by
  `print_int_as_string_maybe(…, 0, 3)` for the "Top speed" line, which always
  runs and always produces exactly three characters plus a NUL at [3]. The
  port terminates the id explicitly; the behaviour is identical because that
  line is unconditional.
* **`sprite_1_unk4`'s rectangle is a pixel short.** `var_2 = x2 - x1 + 1` but
  `var_4 = y2 - y1` with no `+1`, so the two vertical runs stop one row above
  the bottom edge - the horizontal runs cover the corners anyway.
* **Seven statistic lines do not fit.** The last one lands at y = 167 and the
  button row starts at 175; FONTDEF is 8 pixels tall and `hiscore_draw_text`
  adds a shadow row, so with penalty *and* an opponent *and* an impact speed
  *and* jumps the "Jumps" line loses its bottom rows under the buttons. Both
  numbers are literals in the disassembly.
* **`draw_button`'s label loop runs to `strlen` inclusive**, so a label ending
  in `']'` emits one empty line and an empty label emits one too.

### Verification (the numbers)

Everything below was re-run against this tree, not carried over.

* **DOS oracle unchanged.** `tools/build_dumper.sh` + `tools/diff_oracle.py`,
  900-frame runs with `STUNTS_KEVINSEED="29,178,77,215,110,1"`:
  TUBETEST **16/16 100.00 %**, HILLTEST **16/16 100.00 %**,
  PIPEROLL **16/16 100.00 %**, T_HELL2 **16/16 100.00 %**,
  T_HELL4 **16/16 100.00 %**, PIPEFLIP **10/16 98.30 %** - the one known
  open deviation, exactly the size it was.
* **Physics:** `--track DEFAULT --car coun --headless 600` reproduces all 30
  sampled lines of `build/probe/phys9.txt`, line for line.
* **Paint check:** 0 of 10646 frames of `00_default_hell5_full.rpl` had
  unpainted pixels.
* **All 39 shipped tracks start:** 39/39.
* **The 4x build** (`RFB_SCALE=4`) compiles and renders the screen correctly
  at 1280x800.
* **Every portrait frame in the shipped data was checked**, not sampled: 74
  frames over the twelve `OPP<n>WIN/LOSE.PVS`, every index the `winn`/`lose`
  scripts reference. All 74 exist, and **all 74 have a zero flip nibble**, so
  `unflip_shape()` - which every one of them is still run through - is a
  no-op for them. Running the RLE decoder's termination test over the same 74
  says **0 of 74** would take the compressed path, i.e. the raw path is what
  runs, which is the same conclusion `rshape2d.c` records for the cockpit
  "roof".
* **The button state machine, driven for real.** `STUNTS_ENDSCREEN_KEYS`
  pushes scripted keys as SDL events, one per frame, through all three phases,
  so the actual event loop handles them. Thirteen sequences, each checked
  against the disassembly:

      four buttons (opponent + table)      three buttons (racing the clock)
      right enter            -> 1          right enter             -> 1
      right right enter      -> 2          right right enter       -> 2
      left left enter        -> 2          left enter              -> 2
      left enter right enter -> 1          left left enter         -> 1
      right right right enter q -> 2       right right right enter -> 0
      space                  -> 0          space                   -> 0
      esc                    -> 2

  The wrap-around asymmetry is the original's: in the four-button form `left`
  from 0 goes to 3 and `right` from 3 goes to 0, in the three-button form the
  same two land on 3 and 1 (loc_1447A / loc_144A4).
* **The record path end to end, through the real screen.** A 1500-frame
  finish against `DEFAULT.HIG` runs the "Continue" wait, then the name entry,
  then lands on the table with the new row at rank 3 (`1:15.00`, "Lamborghini
  Countach", "OP/L002") and 1:16.95 pushed off the end. Repeated with the
  opponent winning, the same row is stored **parenthesised** - "(CC/NSX )" -
  which is the `var_18`-into-byte-41 rule above. `DEFAULT.HIG` was restored
  byte-identical afterwards.
* **Pictures looked at**, at 320x200 and again at 960x600 / 1280x800: a win
  against opponent 1, a loss against opponent 4, both cars DNF against
  opponent 2, the "nobody finished" `d4a` line against opponent 1, opponent 5
  at 4x, opponent 6, racing the clock (three buttons, table page), the "High
  score table is unavailable" page, the "Continue" page, and **the real
  post-race screen after driving `01_coun_accel_topspeed.rpl` to its end** -
  DNF, 72 Mph average, 128 Mph impact and top, 1 jump, all from the
  simulation's own `gState_*` copy rather than from test values. The portrait
  frame was inspected pixel by pixel at both corners: two rings of
  `dialog_fnt_colour` and one of 0 at the top left, two of `word_407D2` = 8
  and one of 0 at the bottom right - which is the third-ring colour a
  four-fill bevel cannot produce, and the reason `draw_lines_unk` was ported
  rather than approximated.

### New wiring

* `src/render_faithful/rwidgets.c` / `.h` and `rendscreen.c` / `.h`, both in
  `tools/build_native.sh`.
* `hiscore_draw_text` is no longer static in `rhighscore.c`; it is seg008's
  routine and the statistics block draws every line with it.
* `video_flag1_is1` defined in `rdata.c`, set in `game_init`.
* `main_native.c`'s `menu_button` (the four-fill `draw_button` stand-in) is
  superseded by the real `draw_button` for this screen; the Phase 3 menus
  still use it and can be moved over when they are next touched.
* Test hooks, in the style of `STUNTS_MENU_SHOT`:
  `STUNTS_ENDSCREEN_SHOT` (+ `_TIME`, `_OPP`, `_PEN`, `_TOP`, `_JUMP`,
  `_IMP`, `_DIST`, `_PAGE` 0/1/2, `_FRAME`, `_HNA`) renders one synthetic
  finish and exits; `STUNTS_ENDSCREEN_KEYS` drives the real loops with
  scripted keys. `STUNTS_RESULT_SHOT` / `_TIME` still work. With `--play` the
  synthetic hook stands aside, so `STUNTS_ENDSCREEN_SHOT` then captures the
  screen after a real race.

### Not done

* The host does not act on "View Replay" or "Race again" - see the last
  deviation above.
* `show_dialog`, `call_read_line` and `do_fileselect_dialog` are still not
  ported; the `esav` "Save @]Path:]@ ]Name:]@]" dialog is still an `S` key
  that writes `<track>.RPL`.
* `sprite_shape_to_1_alt` is ported and compiles but `end_hiscore` does not
  call it - it is here because Phases 7 and 11 want it.
* PIPEFLIP frame 423, unchanged and still open.

## Phase 10: the intro and the credits (2026-08-17)

Everything that runs before the main menu: the two title stills, the 3D logo
animation, and the credits page. 1838 lines of assembler across four
procedures, plus five small helpers and one container format that had no port.

| Ported | Source | asm lines |
|---|---|--:|
| `run_intro_looped` | seg000.asm 641..741 | 100 |
| `run_intro` | seg000.asm 742..827 | 85 |
| `load_intro_resources` (the credits) | seg000.asm 828..1569 | 741 |
| `setup_intro` | seg003.asm 6268..6830 | 562 |
| `intro_op` | seg003.asm 6831..7182 | 351 |
| `init_plantrak` | seg001.asm 8529..8682 | 154 |
| `input_repeat_check` | seg008.asm 3415..3450 | 36 |
| `sprite_blit_to_video` | seg008.asm 3992..4066 | 74 |
| `sprite_1_unk2` | seg012.asm 10825..10873 | 49 |
| `setup_aero_trackdata` | restunts.c 715..737 | - |
| the `.PES` container (six routines) | restunts shape2d.c 345..592 | - |

New files: `src/render_faithful/rintro.c` (the stills and the credits),
`rintro3d.c` (the 3D animation), `rpes.c` (the container), their two headers,
and `tools/intro_shot.c` + `tools/build_intro.sh`, a standalone driver so the
whole sequence can be built, run and screenshotted without `main_native.c`
being involved.

### `load_intro_resources` is the credits screen, and its name is the only lie

The survey had already flagged the name. What the routine actually does is
draw 23 lines of text out of CRED.RES, then play a three-part animation out of
SDCRED.PES: an arrow slides in from x = 330 to x = 40 at two pixels a tick,
turns into eight frames of itself curling upwards, and settles as the
Distinctive Software logo with its wordmark. It loads no resource anyone else
uses.

The layout is data, as it always is here. `arrw`'s own `s2d_pos` (40, 170) is
where the slide stops and which row it runs along; `arow`'s `s2d_pos_y` (132)
is the top of the window the eight frames play in. The 23 text lines are two
columns at x = 20 and x = 172, with six colour pairs out of
`word_407D4`..`word_407EA` in dseg (15/8 for a name, 11/3, 12/4, 13/5, 9/1,
10/2 for the five headings), and every string goes through `copy_string` into
the same 32-byte scratch buffer before `intro_draw_text` draws it twice - a
shadow at (x+1, y+1) and the text at (x, y).

### SDCRED.PES: a different container, and what is inside it

Every other 2D archive in the game is a `.PVS`, which after DSI decompression
holds 8-bit pixels. SDCRED.PES is the game's only `.PES`, and its shapes are
still planar: `s2d_width` counts BYTES, and `s2d_unk3`..`s2d_unk6` carry in
their low nibbles the colour each of up to four 1-bit planes contributes, with
`s2d_unk4`'s high nibble as the background. `file_load_shape2d` (restunts
shape2d.c:583) runs `file_unflip_shape2d_pes` and then
`file_load_shape2d_esh`, which expands 1bpp x N planes to 8bpp eight pixels
per source byte and remaps the result through `palmap[]`.

Read out of the file rather than assumed:

    name  w  h  unk3 unk4 unk5 unk6  planes  expands to
    !cg0 16  4   01   02   04   08     4      128x4
    arow 10 65   01   06   78   00     3       80x65  at (16,132)
    arrw 25 29   61   08   00   00     2      200x29  at (40,170)
    arw1 26 31   61   08   00   00     2      208x31  at (32,168)
    ...
    type 11 28   0F   00   10   00     1       88x28  at (96,168)

`palmap` is initialised from an `!MGA` resource when the archive has one, and
dseg.asm:21699 seeds it with the identity 0..15. SDCRED.PES carries `!cg0`,
the CGA map, and no `!MGA` - and checked over the whole shipped data set, no
file contains those four bytes at all. So in VGA mode the identity stands and
the expansion is what reaches the screen.

**A trap the house rule walks straight into.** The expanded shape inherits the
source header, including `s2d_unk5`'s high nibble - the "this is transposed"
mask, which for `arow` is 7. The original never runs `file_unflip_shape2d`
over a `.PES` result, so the stale mask is harmless there; this port's rule is
to put *every* 2D shape through `unflip_shape()`, and a stale 7 would have made
that routine transpose an already-correct picture into diagonal streaks.
`rpes.c` clears the nibble, which makes `unflip_shape()` the no-op it should
be. The low nibble - plane 2's colour, already consumed - is left alone.

### The 3D intro simulates a car; it does not animate one

`setup_intro` loads three objects out of TITLE.P3S (`logo`, `log2`, `brav`),
scatters a hundred single-pixel stars from `get_kevinrandom`, and then calls
`init_plantrak`, which builds a five-entry racing line **out of nothing** -
track elements 7, 6, 8, 9, 7 at columns 1,0,0,1,1 and rows 28,28,29,29,28, an
18-entry `trackdata3` path, and a Countach placed at
`(0x17700, 0, (trackpos[28] + 0x12E) << 6)` - and hands it to the ordinary
opponent AI. The car in the intro is `opponent_op` driving, frame by frame,
at 20 Hz. Everything the camera then does is scripted around wherever the car
happens to be.

Three camera phases, cut on the frame counter:

* **0-6 s** the camera IS the car: its position 0x14 higher, its heading as
  the yaw, pitch 0. `arg_A` = 0 so the car itself is not drawn, and `log2` is
  the object in view.
* **6-11 s** the camera jumps to the fixed point (0x400, 0x5A, 0x400) and
  looks at the car, which is now drawn: `polarAngle` for the yaw,
  `polarRadius2D` then `polarAngle` for the pitch.
* **11-23 s** the camera climbs 0x14 a frame, pulls back 5 a frame, eases its
  x back to 0x400 ten at a time, and the look-at target walks one unit a frame
  towards (0x400, ·, 0x400). `arg_C` flips to 1, so `logo` replaces `log2`.

The projection is `set_projection(0x28, 0x28, 0x140, 0xC8)` - 40 degrees each
way, much wider than the 0x23 the in-car view uses.

**The bug in this port's first version, and how it was found.** From six
seconds on the screen was black apart from a few stars. A trace of the camera
showed the pitch sitting at 285 (100 degrees) where the geometry demanded
about -10. seg003:6739 pushes the radius FIRST and the height difference
SECOND, so the height difference is `polarAngle`'s FIRST argument - the same
order `frame.c:246` already uses for the chase camera. Written the other way
round, the camera pitched a hundred degrees away from the logo. Nothing but
looking at the picture would have caught it: the routine ran, returned, and
drew a technically valid frame of empty space.

**The second bug, same lesson.** The credits' slide loop is
`cmp [bp+var_2], si / jle loc_10DA0` - draw while the arrow is still RIGHT of
its resting place. Written the other way round the slide terminated in a
single frame and the finished page still looked perfect; only the
frame-by-frame screenshots showed the arrow never moving.

### Almost half of both 3D routines is unreachable, and that is fine

`slow_video_mgmt_copy != 0` guards two ping-pong 100-entry POINT2D buffers,
three global RECTANGLEs, `rect_union` / `rect_intersect` and a dirty-rectangle
blit, so that a slow machine copies only the changed strip out of the
offscreen window. That flag is dseg's `slow_video_mgmt`, which only the
graphics-detail menu sets, and the mechanism exists to avoid copying an
offscreen window this port does not have. The taken branch is ported
instruction by instruction; the other is described in `rintro3d.c` and
skipped. Same for `video_flag5_is0`, which selects `setup_mcgawnd1/2`.

### [DEVIATION] - stated plainly

There is no oracle for a title screen. This is behaviour-exact, not
instruction-exact, in these places and nowhere else:

* **No sprite windows.** The original draws into an offscreen
  `sprite_make_wnd(0x140, 0xC8, 0x0F)` and copies rectangles of it to the
  screen. Where it clears a window, draws unclipped into it and copies a
  sub-rectangle out, this port sets that sub-rectangle as the clip window and
  draws clipped - the same pixels reach the screen. `sprite_copy_wnd_to_1`,
  `sprite_copy_2_to_1_2`, `sprite_putimage(wnd)` and `sprite_clear_shape` are
  therefore all no-ops here. Worth recording while we are at it:
  **`sprite_clear_shape` (seg012:13503) does not clear anything** - it is a
  `rep movsb` FROM sprite1's bitmap INTO the shape, i.e. "grab the screen back
  into the window".
* **`sprite_blit_to_video`'s dissolve degenerates to a present.** With a mode
  other than 0xFFFE the original runs four passes of `sprite_1_unk3`
  (seg012:10985), a sparse copy that reveals every twelfth row and one pixel
  in four along it, so the picture fades in. Both ends of that copy are the
  same bytes here, so the four passes are the identity. The input polling and
  the return value are kept.
* **The timer is SDL_GetTicks at the original's 100 Hz.** `word_4499C` is
  `100 / framespersec` (restunts.c:464), which pins the tick; the two stills
  are `input_repeat_check(0x190)` = four seconds each, the credits page is
  `input_repeat_check(0x1F4)` = five, and the 3D animation runs to
  `0x17 * framespersec` = 460 frames = 23 seconds. Measured end to end: 31.5 s.
* **`input_do_checking` is an SDL event poll**, returning 27 for Escape and 1
  for anything else. There is no DOS mouse, so
  `mouse_draw_opaque_check` / `_transparent_check` are not called.
* **The intro needs a cut-down `game_init`.** The original reaches
  `run_intro_looped` with `init_trackdata()` done and DEFAULT.TRK already read
  into `td14_elem_map_main` (all 1802 bytes, which fills `td15` too because the
  two are contiguous) but with **no** `track_setup` - the five-tile line is
  written by hand. `intro3d_init` reproduces exactly that, plus the four video
  flags, the collision planes and the material tables that `game_init` sets and
  that nothing has set yet at intro time. It is idempotent, it is redone on
  every pass (only `init_polyinfo`, which allocates, is guarded), and
  `game_init` redoes all of it for a race. `video_flag1_is1` in particular is
  needed by the CREDITS, not the 3D part: seg000:875 multiplies the arrow's
  width by it.
* **`td04_aerotable_pl` / `td05_aerotable_op`** are not carved out of the
  trakdata block in this port, so `setup_aero_trackdata` writes into a static
  array - exactly as `game_init` already does for a race.
* **The `.PES` expansion is per-shape on first lookup**, cached, instead of
  whole-archive at load time. Same pixels; the consequence is that the shapes
  must be fetched with `pes_locate_shape()` rather than `locate_shape_alt()`,
  because the archive handle still points at the planar bytes.

### [ODDITY] - four, all reproduced

* **`waitflag` is not how long a still is shown.** seg000:757 reads `prod`'s
  `s2d_pos_y` and sets `waitflag` to 0xA0 or 0xB4 - and nothing in the intro
  ever reads it. Its only reader in the whole game is `show_waiting`
  (seg008:4073), which passes it to `show_dialog` as the "please wait" box's
  width. Both stills are shown for 400 ticks. The assignment is reproduced
  because the value survives into the next dialog the game opens.
* **`prod` is looked up twice**, through two copies of the same string literal
  (`aProd` / `aProd_0`), the first only to read one header field.
* **The "gsta" credit line calls `copy_string` with the literal 0AC74h** where
  every other line names `resID_byte1`. Those are the same address:
  `resID_byte1` sits at dseg offset 0xAC74, computed by summing every data
  definition ahead of it in dseg.asm. A disassembly artefact, not a bug -
  "Stan Chow" really is drawn.
* **The eight-frame wait loop swallows keypresses.** loc_10E66 reassigns
  `var_46` every time round and never leaves on it; only loc_10E83, after the
  five ticks are up, looks at it. A press in the middle of a frame is lost.

### Escape means two different things

`run_intro_looped` returns whatever the step that ended it returned.
`run_intro` returns a KEY CODE, so Escape during the stills comes back as 27
and seg000:10474 then offers "quit to DOS"; `setup_intro` and the credits
return a flag, so Escape during those comes back as 1 and goes straight to the
menu. The port keeps that, and additionally re-posts an SDL_QUIT so a
window-close is still seen by the caller's own loop.

### Verification (the numbers)

Nothing here touches the simulation, and the DOS oracle says so. All re-run
after the final build, `STUNTS_KEVINSEED=29,178,77,215,110,1`, 900-frame runs:

| track | fields | byte agreement |
|---|---|---|
| TUBETEST | 16/16 | 100.00 % |
| HILLTEST | 16/16 | 100.00 % |
| PIPEROLL | 16/16 | 100.00 % |
| T_HELL2 | 16/16 | 100.00 % |
| T_HELL4 | 16/16 | 100.00 % |
| PIPEFLIP | 10/16 | 98.30 % (the known deviation, frame 423) |

* **Paint check:** 0 of 10646 frames of `00_default_hell5_full.rpl` had
  unpainted pixels.
* **All 39 shipped tracks start:** 39 ok, 0 failures.
* **Physics:** `--track DEFAULT --car coun --headless 600` is byte-identical
  to `build/probe/phys9.txt`, all 30 sampled lines, 0 differing.
* **Both builds compile and run:** `tools/build_native.sh` and
  `RFB_SCALE=4 tools/build_intro.sh`.
* **Re-entrancy:** `bin/intro_shot --loops 2` runs the whole sequence twice,
  both passes returning 0, no crash and no double-expansion - the `.PES`
  archive is left planar and re-expanded from a private copy each time.
* **Pictures actually looked at**, at 320x200 and at 1280x800: the Broderbund
  producer logo, the Stunts title screen, twelve frames of the 3D animation
  (the in-car opening, the Countach circling the DSI plate, the final climb
  out), six frames of the arrow slide, all eight arrow frames and the finished
  credits page.

### Wiring

One call in `main_native.c`, where seg000:1042D makes it - after the fonts and
`mainresptr`/`miscptr` are loaded and immediately before the main-menu loop:

    #include "render_faithful/rintro.h"
    ...
    rintro_run_looped(win, ren, tex, &pal, data_dir);

The name is `rintro_run_looped` and not `run_intro_looped` because
`externs.h:430` already declares the never-implemented DOS `run_intro_looped`
with no arguments.

**The title song needs one more thing.** `rintro_run_looped` calls
`music_native_init()` (idempotent) and `music_native_play(MUSIC_SONG_TITLE)`,
which is seg000:648's `file_load_audiores("skidtitl", "skidms", "TITL")`. It
is only AUDIBLE once `audio_native_init()` has opened the device, and
`main_native.c` does that further down, after the menu - so the menus are
silent today for the same reason. Moving that block above the intro call is
all it takes; nothing breaks if it is not moved.

### Screenshot hooks

Each renders one picture and exits, as every ported screen does:
`STUNTS_INTRO_PROD_SHOT`, `STUNTS_INTRO_TITL_SHOT`, `STUNTS_CREDITS_SHOT`
(a file), `STUNTS_CREDITS_ANIM` and `STUNTS_INTRO3D_SHOTS`
(+ `STUNTS_INTRO3D_STEP`, a directory), plus `STUNTS_INTRO3D_TRACE=1` for the
camera/car trace that found the `polarAngle` bug.

## The heap overflow behind the opponent-car crash (2026-08-17)

The registry fix recorded above was real but only half the story. The crash
came back intermittently — 5 runs in 30, moving around as the heap layout
changed, which is the signature of memory corruption rather than a logic
error. The self-describing `fatal_error` added while chasing it named the
fault in one line: the handle was **not in the registry at all**, which no
amount of resource-loading logic can explain.

`shape3d_load_car_shapes` duplicates the player's archive when the opponent
drives the same car. The original copies bytes:

```asm
loc_20216:
    mov     bx, word ptr [bp+var_A]     ; a 32-bit counter
    les     di, carresptr
    mov     al, es:[bx+di]              ; a BYTE
    les     di, car2resptr
    mov     es:[bx+di], al              ; a BYTE
```

`carresptr` and `car2resptr` are declared `int16_t far*` in this port, so the
transcribed `for (i = 0; i < var_6; i++) car2resptr[i] = carresptr[i];`
copied var_6 **words** into a var_6-**byte** allocation: an overflow of
exactly the buffer's own size, every single time that branch ran. What it
trampled was usually rfileio's own `chunk_entry` nodes, which live in the same
heap — hence a lookup that failed for a pointer that had just been registered.
The loop counter `i` is also `int16_t` here against a 32-bit counter in the
original, which would have broken on any archive over 32767 bytes.

Fixed with byte pointers and a `uint32_t` counter. 0 failures in 60 runs,
against 5 in 30 before.

Three lessons, all of which cost time here:

1. **A type on a resource handle is a claim about its element size.** Every
   other handle in this port is `void far*` or `char far*`; this one was
   `int16_t far*` and the difference was invisible until it indexed something.
2. **Make the fatal message say which way it failed.** "not found" sent me
   looking at archive parsing. "not in the registry" pointed straight at
   memory corruption. It is four extra lines and it ended the hunt.
3. **AddressSanitizer is unavailable in this tree** — the Homebrew
   `libSDL2-2.0.0.dylib` is a shim over SDL3 and aborts in its own `dllinit`
   under ASAN, before `main`. An ASAN build therefore reports nothing and
   looks like a clean run. It is not; do not read silence from `ASAN=1` as a
   result.

### Two process changes

`tools/verify.sh` is new: the whole bar in one command, including the
opponent-car matrix that neither the physics oracle nor the replay checks
could reach. Both bugs the user hit lived in exactly that gap.

It also exports `SDL_VIDEODRIVER=dummy`. Every render check used to open a
real window and take focus, which is unacceptable while someone is working at
the machine — and it happened, repeatedly, during this session. Verified
byte-identical: the dummy driver produces the same BMP as a real window.

### The intro is opt-in

Phase 10's attract sequence measures 38 seconds a pass. The original showed it
on every launch and returned to it whenever the menu sat idle; here it needs
`--intro`, because 38 seconds before the menu on every launch is not what a
port gets played for. `--nointro` is kept for scripts.

## Phase 7, part one: the track preview and the showroom (2026-08-17)

Two of the four items, and the two that were actually 3D. Both were pure
transcription — every routine they call was already ported, which is what the
plan predicted and the only prediction in the plan that turned out exactly
right.

### draw_track_preview (seg003 4760..5360, 595 lines)

The whole track from a fixed camera, in one still. It is not a second
renderer: it walks the same 30x30 element and terrain grids `update_frame`
walks and hands every tile to the same `transformed_shape_op`.

The camera is six constants in dseg, not state:

    from = (15360, 20200, -2800)     word_3C108 / _3C10A / _3C10C
    to   = (15360,  2800, 10960)     word_3C10E / _3C110 / _3C112
    horizon vector = (0, -10100, 16760)                 unk_3C114

One pitch angle is derived from those — `polarAngle(height difference, ground
distance)`, the same argument order the chase camera and the intro use — and
everything else follows: the horizon row comes from projecting the horizon
vector, sky above it, ground below, and two of the four panorama strips
across it. Every world position is `(world - camera) >> 1`, so the whole
track fits without touching the projection.

The byte offsets pin the DOS `TRACKOBJECT` layout: the index is multiplied by
14, which only works if the three pointers are *near*, giving `+0` info,
`+2` rotY, `+4` shape, `+6` loShape, `+8` overlay, `+9` surface, `+0Ah`
zbias, `+0Bh` multiTile. Every offset the routine uses lands on a named
field, so the port is written through the names.

`game3dshapes` entries 43, 91, 92 and 93 are the flat bases drawn under a
piece standing on a hill, chosen by `ss_multiTileFlag` — the asm names them
as `+3B2h`, `+7D2h`, `+7E8h` and `+7FEh`, and SHAPE3D is 22 bytes.

### The car picker's showroom (seg000, parts of run_car_menu)

Not the whole 1300-line routine — this port's picker works and is kept. Only
the two pieces that were missing:

**The turntable.** Three dseg constants, which restunts2 carries
symbolically and restunts1 only as raw bytes:

    carmenu_carpos   VECTOR <0x0000, 0xFCB8, 0x0B40>  = (0, -840, 2880)
    carmenu_cliprect RECTANGLE <0x0000, 0x0140, 0x0000, 0x005F>

The shape is `game3dshapes+0AA8h` = entry 124, which is the same slot
`shape3d_load_car_shapes` fills with the player car's `car0` — the picker
spins the exact shape you will drive. `ts_unk` is 0x7530 rather than the
world's 0x400, which pins the car at full detail. There is no camera state:
the pitch is `polarAngle(carpos.y, carpos.z)`, derived from where the car is
placed.

**The acceleration graph.** It is a *simulation*, not a stored curve: the
game sets `framespersec` to 20, resets the car, forces first gear and calls
the ordinary `update_car_speed` up to 800 times, plotting a pixel per step
until the curve leaves the top of its 38x64 box. So the graph is exactly as
truthful as the physics, which here is byte-identical to DOS on five tracks.
The Countach takes 52 steps.

### The one real bug, and it cost a black screen

`transformed_shape_op` only **queues** polygons; `get_a_poly_info` is what
rasterises them. The track preview calls it inside its own loop, so that
worked first time. The turntable did not, and rendered a perfectly empty
black frame with a correctly loaded 191-vertex, 150-primitive shape sitting
in the queue. The original calls it once per frame at seg000:3985, after the
last shape and before `sprite_copy_wnd_to_1`.

Worth stating plainly because it is the sort of thing that reads as a
data-loading problem: a shape that is present, valid and drawn into nothing.
The diagnostic that settled it was printing the shape's own counts from
inside the draw call — 191/150/7 proved the data was fine and moved the
search to the pipeline.

Both are in `tools/verify.sh` now, and the showroom check asserts a *pixel
count*, not just that a file appeared, because the failure it guards against
produces a perfectly valid all-black BMP.

Still open in Phase 7: the option sub-screens (seg008 4708..5422, 730 lines,
of which joystick calibration is 285 and the graphics-level picker 175).

## Phase 7, part two: the option sub-screens (2026-08-17)

seg008 4708..5422 is eight routines, not one screen, and seven of them turn
out to be the same four lines: push the input state, look a string up in
MAIN.RES, hand it to `show_dialog`, flip a flag, pop. `do_key_restext` sets
`byte_3FE00 = 0` and `byte_3B8F2 = 0`; `do_mou_restext` sets
`byte_3B8F2 = 1`; the rest are music, sound, pause and quit. This port
already has all of those working through its own chrome.

The one with real content is `show_graphic_levels_menu` (5247..5422, 175
lines), and it is now ported.

Its dialog is built by string surgery: the "mrl" template holds nine `[`
markers, and the routine walks the string writing a `*` after whichever one
is currently selected. What the nine choices *do* is the part that matters
(loc_2A098 and the four labels below it):

    0..4  detail_level = choice
    5     slow_video_mgmt = 0
    6     slow_video_mgmt = 1
    7     framespersec2 = 0x0A
    8     framespersec2 = 0x14
    9     leave

and on the way out, only if `framespersec2` actually changed, it puts up
"mrs".

`detail_level` was previously a two-way toggle here, which was a stand-in.
It is real: frame.c reads it in four places including
`detail_threshold_by_level[]`. Measured on one DEFAULT frame, level 0 and
level 4 differ by **3534 pixels** and the palette drops from 70 colours to
50, so the five settings genuinely change what is drawn. `slow_video_mgmt`
selects an offscreen-window path this port does not have; it is shown and
stored so the setting round-trips, and the screen says so rather than
pretending.

`STUNTS_DETAIL=<0..4>` sets the level from the command line, and
`tools/verify.sh` asserts that levels 0 and 4 produce **different files** —
a settings screen that stores a number nothing reads would sail through a
"did it draw" check.

### Not done: joystick calibration

`do_joy_restext` (4708..4993, 285 lines) calibrates a joystick. This port
reads no joystick at all - there is no `SDL_INIT_JOYSTICK`, no
`SDL_Joystick` anywhere - so porting the calibration alone would produce a
screen that carefully measures a device nothing then steers with. It needs
the input path first, and that is a separate piece of work from Phase 7's
"menus' visible gaps". Recorded here rather than quietly counted as done.

**Phase 7 status: the three visible gaps are closed** - track preview, car
turntable, acceleration graph and the graphics sub-screen. Joystick support
(calibration plus the input path) is the one item left, and it is listed
with Phase 9 and Phase 11 as remaining work.

## Phase 9: rewinding, pause, and the bar's foundations (2026-08-17)

**Every dependency `loop_game` needs is now ported.** What is left of Phase 9
is the drawn strip's buttons; the two controls that matter already work.

* `restore_gamestate` (seg001 4286..4394) and `init_kevinrandom`
  (seg002 175..205). The mechanism is twenty 1120-byte snapshots at `cvxptr`
  — the same 1120 bytes the DOS oracle dumps — written by `update_gamestate`
  every `word_45A00` = `30 * framespersec` frames. **The RNG seed rides inside
  the snapshot**, which is the whole reason a rewind reproduces exactly rather
  than diverging at the first random number. Measured: restoring at frames 600
  and 1200 gives all 1120 bytes back identically.
* `init_game_state` (seg001 3885..4021) — `init_game_state_vars` already held
  the body; this added the tail that clears the scoreboard fields, and the
  argument dispatch where -3 only refreshes the frame-rate tables.
* `check_input` (seg008 3109..3147) — a debounce, not a read: it spins until
  nothing is held, so the keypress that opened a screen cannot also act on it.
* `replaybar_view_height` (seg005 498..520) — the strip exists only when the
  dashboard is on, the camera is not the opponent's, `game_replay_mode == 2`
  and `replaybar_enabled`. It is a *playback* control, never present while
  driving.
* `P` pauses and Backspace rewinds one interval, in the race loop.

### Six declared-but-undefined symbols, and the one that cost the most

Phase 9 was blocked six times by a symbol `externs.h` declared and no file
defined: `mouse_butstate`, `get_kb_or_joy_flags`, `kbormouse`, `word_4499C`,
`replaybar_enabled`, `height_above_replaybar`. Phase 6 had hit
`video_flag1_is1` and Phase 10 `waitflag` and `slow_video_mgmt`.

`word_4499C` is worth the warning. It *was* defined — in `audio_native.c`,
which `tools/build_dumper.sh` does not link. Referencing it from
`init_game_state` stopped the oracle tool building, and the bar then reported
**0/16 on all six physics tracks at once**. An undefined constant, presenting
as total physics failure. It now lives in `sfasm_port.c`, which both binaries
link.

**Grep for the symbol before assuming a routine is missing its data**, and
when a whole category of checks fails at once, suspect the build before the
code.

### A test that was wrong, not the code

Completing `init_game_state` broke the rewind check at frame 0 — correctly.
seg001:4296 *resets* the game there rather than restoring, so "identical to
the frame-0 snapshot" was never the right thing to assert. The check now uses
frames 600 and 1200, real snapshot boundaries, with the reason written beside
it.
