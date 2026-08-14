# Technical Unknowns & Open Investigation Items

This document tracks unresolved technical questions, ordered by priority and impact on project milestones.

---

## High Priority (Impacts Core Physics & Simulation Oracle)

### 1. 16-Bit Integer Overflow & Undefined Behavior in Ported Math
* **Context**: In 16-bit real-mode x86, integer overflow wraps modulo $2^{16}$ in hardware, and arithmetic shifts (`SAR`) have specific sign-extension behavior.
* **The Unknown**: When porting 16-bit assembly to 64-bit C (`int` is 32-bit, `long` is 64-bit on ARM64), implicit integer promotions can eliminate intentional 16-bit wrap-arounds or introduce C undefined behavior.
* **Resolution Path**: Audit all functions in `math.c` and `stateply.c` with explicit fixed-width casts (`(int16_t)((uint16_t)a + (uint16_t)b)`). Run unit tests against the DOS `test.exe` binary.

### 2. Division-by-Zero & Corner Cases in Vector Trigonometry
* **Context**: `polarAngle(z, y)` contains the note: `if (z == 0 && y == 0) return result; // orig code has undefined return value here as well!`.
* **The Unknown**: What exact register value was returned by the original 8086 assembly when $(0, 0)$ was passed?
* **Resolution Path**: Inspect the unpatched assembly `seg000.asm` / `math.asm` at `loc_13A8C` to verify register state before exit when $(z == y == 0)$.

### 3. Copy Protection Security Mechanism
* **Context**: Stunts 1.1 includes an off-disk copy protection system that prompts for words from the manual. Failing the quiz causes the car to explode after 3 seconds of driving.
* **The Unknown**: What exact memory byte or flag is set by the verification routine, and where is it checked in `RunGame`?
* **Resolution Path**: In Restunts disassembly, offset `0x2B3C` in unpacked `GAME.EXE` controls security bypass (`game_3F6autoLoadEvalFlag`). In native code, `security_check_passed` should default to `1`.

---

## Medium Priority (Impacts AI & Audio)

### 4. Opponent AI Waypoint Navigation Details
* **Context**: While player physics is substantially decompiled in `stateply.c`, opponent AI logic (`seg002.asm`, `OpponentOp`, `TrackOpponentOp`) still has unported assembly routines.
* **The Unknown**: The exact algorithm by which the AI calculates steering adjustments when pushed off its predetermined racing line (`td21_col_from_path`, `td22_row_from_path`).
* **Resolution Path**: Use Ghidra to complete decompilation of `seg002.asm` and cross-reference with FM Towns `4ddfmt.exp` AI symbols.

### 5. Audio Synthesizer Sequence Format (`.KMS` / `.VCE`)
* **Context**: Music sequences are stored in `.KMS` files, and engine synthesis tables are in `.VCE` files.
* **The Unknown**: The exact event opcode mapping used by the Voyetra sound driver for dynamic engine pitch modulation and skid frequency shifting.
* **Resolution Path**: Inspect `seg008.asm` / `seg009.asm` and compare with standard MIDI sequence parsers in `libmt32emu`.

---

## Low Priority (Visual Polish & Edge Cases)

### 6. Reproduction of Emergent Glitches (Overdrive & Rocket Bounce)
* **Context**: Competitive Stunts driving communities exploit specific physical anomalies (e.g. hitting specific track seams at high speed gives an instant speed boost > 255 mph).
* **The Unknown**: Do these glitches arise naturally from the integer collision solver equations in `stateply.c`, or do they depend on compiler-specific memory alignments?
* **Resolution Path**: Run the community's known glitch replays through `compare_states.py` against both DOSBox and the native simulation core.

### 7. Analog Controller Curve Mapping
* **Context**: DOS Stunts supported analog joysticks and mice via BIOS/driver polling with a non-linear steering rate dampening table (`steeringdots[62]`).
* **The Unknown**: The exact transformation curve between modern gamepad thumbsticks (range $[-1.0, +1.0]$) and the 62-entry discrete steering lock table.
* **Resolution Path**: Document and implement configurable deadzones and exponential response curves in the input layer.
