# Persistent Instructions for AI Agents & Engineers (AGENTS.md)

This project is a high-precision reverse-engineering and native source port of the 1990 MS-DOS racing game **Stunts / 4D Sports Driving** for Apple Silicon macOS.

All future AI agents, pair programmers, and contributors working on this codebase **MUST** adhere strictly to the rules and principles defined below.

---

## 1. Non-Negotiable Core Rules

### Rule 1: Behavioral Fidelity Takes Precedence Over Everything
* The original DOS game is the **mathematical and behavioral ground truth**.
* The port is successful only if it behaves **identically** to the original binary under identical inputs.
* Subjective similarity ("it feels close enough") is **NOT** an acceptable standard of correctness.

### Rule 2: DO NOT Substitute Original Physics With Generic Physics Engines
* **NEVER** introduce Bullet Physics, PhysX, Box2D, Havok, Unity Physics, Godot Physics, or any generic modern rigid-body simulator into this project.
* The original vehicle physics equations, discrete integer trigonometry tables (`sintab`), speed-dependent aerodynamic curves, 20 Hz discrete time steps, and surface collision plane solvers (`plan`/`wall`) must be preserved in code.

### Rule 3: Original Files Under `Original/` Are Strictly Immutable
* **NEVER** edit, overwrite, rename, or delete files inside `Original/` or `extracted/`.
* The user's legally owned original distribution is the read-only reference material.
* Do not commit proprietary game assets to source control.

### Rule 4: No Unverified Assumptions
* Clearly distinguish between:
  1. **[FACT]**: Verified directly against the original binary/data.
  2. **[DERIVED]**: Sourced from Restunts disassembly or technical documentation.
  3. **[HYPOTHESIS]**: Plausible assumptions requiring verification.
* Never treat comments, variable names, or unverified claims in community code as infallible ground truth.

### Rule 5: Behavioral Oracle Testing Is Mandatory
* For all physics, math, and simulation changes, validation **MUST** be performed by running deterministic test replays through the native test harness and diffing the resulting `GAMESTATE` frame dumps against DOSBox oracle runs using `tools/compare_states.py`.
* A pull request or milestone is only complete when there are **0 divergent frames** across the test replay suite.

---

## 2. Directory Layout Reference

* `Original/`: Read-only source ZIP (`Stunts_DOS_EN.zip`).
* `extracted/`: Extracted canonical DOS game files (read-only reference).
* `reference/`: Reference reverse-engineering repositories (`reference/restunts`, `reference/restunts2`).
* `docs/`: Technical documentation, architecture, file formats, inventory, and roadmaps.
* `tools/`: Diagnostic scripts, DOSBox runners, inventory tools, state comparators.
* `src/`: Native port source code (to be developed in subsequent milestones).

---

## 3. Standard Verification Commands

```bash
# Verify file inventory & hashes
python3 tools/inventory.py

# Run original DOS game in DOSBox
bash tools/run_dos_stunts.sh

# Run frame-by-frame state comparator
python3 tools/compare_states.py reference.bin target.bin
```
