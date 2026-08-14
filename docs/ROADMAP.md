# Staged Project Roadmap

This roadmap defines the phased engineering plan for developing the native macOS Apple Silicon port of *Stunts*.

---

## Milestone Overview

```
[M0: Discovery & Architecture] ---> [M1: Math & Asset Core] ---> [M2: Headless Oracle Validation]
                                                                                |
[M5: Full Menus & Track Editor] <-- [M4: Interactive Gameplay] <-- [M3: Software 3D Renderer]
              |
              v
[M6: Native macOS Polish & .app Bundle]
```

---

## Phase 0: Discovery, Environment & Architecture *(Completed)*
* [x] Preserve original immutable copy (`Original/Stunts_DOS_EN.zip`) and compute SHA-256 hashes.
* [x] Produce complete machine-readable file inventory (`docs/ORIGINAL_INVENTORY.md` and `docs/original_inventory.json`).
* [x] Establish macOS DOSBox execution and debugging environment (`tools/dosbox_stunts.conf`, `tools/run_dos_stunts.sh`).
* [x] Research reverse-engineering ecosystem (Restunts, Restunts2, FM Towns debug symbols).
* [x] Formulate behavioral verification strategy and state dump comparator (`tools/compare_states.py`).
* [x] Establish architectural documentation and persistent agent instructions (`AGENTS.md`).

---

## Milestone 1: Portable Simulation Core & Asset Pipeline
* **Goal**: Build a self-contained, 64-bit portable C99 library (`libstunts_core`) containing fixed-point math and native asset decoders.
* **Deliverables**:
  1. **`sim_math`**: Port `math.c` to standard C99 fixed-width integer types (`int16_t`, `int32_t`, `uint16_t`). Add unit tests matching 100% of DOS `test.exe` trig results.
  2. **`dsi_unpack`**: Port multi-pass DSI decompressor (RLE + VLE/Huffman) into an in-memory stream reader.
  3. **`asset_parsers`**: Implement parsers for `.TRK` (tracks), `.HIG` (high scores), `.P3S` (3D shapes & `SIMD` physics blocks), `.PVS` (bitmaps), and `.RPL` (replays).
  4. **Verification**: CLI tool that extracts and parses every asset in the user's `Original/` folder with zero errors.

---

## Milestone 2: Headless Replay Simulator & Oracle Validation *(Critical Milestone)*
* **Goal**: Achieve bit-identical physics simulation against the original DOS reference oracle without rendering a single pixel to screen.
* **Deliverables**:
  1. Port `statecar.c` and `stateply.c` simulation update functions into `libstunts_sim`.
  2. Implement track setup and collision plane indexing (`plan` and `wall` spatial queries).
  3. Build `stunts_verify` command-line test runner.
  4. Execute `DEFAULT.RPL` through `stunts_verify` and compare against DOS `DEFAULT.BIN` using `tools/compare_states.py`.
  5. **Acceptance Criteria**: **Zero divergent frames across the entire duration of `DEFAULT.RPL`**.

---

## Milestone 3: Faithful 320x200 Software Renderer & Metal Presentation
* **Goal**: Render the 3D polygonal world and 2D dashboard onto a native macOS Metal window.
* **Deliverables**:
  1. Port 3D software rasterizer (`shape3d.c`) to draw polygons into a $320 \times 200$ 8-bit indexed framebuffer.
  2. Implement 2D UI and cockpit blitter (`shape2d.c`).
  3. Create Metal presentation pipeline: upload $320 \times 200$ texture, apply aspect-correct 4:3 integer scaling shader, present at display refresh rate.
  4. Enable replay playback with full synchronized visuals and multiple camera views (cockpit, chase, bumper, trackside).

---

## Milestone 4: Interactive Gameplay & Audio Engine
* **Goal**: Fully playable real-time driving experience with responsive controls and synthesized sound.
* **Deliverables**:
  1. Real-time game loop syncing discrete 20 Hz physics ticks with display V-sync.
  2. Modern input layer supporting Keyboard, Mouse, and Apple GameController framework (DualSense, Xbox, MFi gamepads).
  3. Audio engine integration:
     - Roland MT-32 synthesis via embedded `libmt32emu` for music and rich sound effects.
     - Sound Blaster OPL3 synthesizer fallback for authentic FM audio.
     - Dynamic engine pitch synthesis from RPM and tire skid audio.

---

## Milestone 5: Menus, Opponents, High Scores & Track Editor
* **Goal**: Complete feature parity with the full DOS game experience.
* **Deliverables**:
  1. Main menu, car selector, opponent selector, and track selector screens.
  2. Opponent AI execution during races with dynamic collision avoidance.
  3. High score record tracking and local persistence.
  4. Built-in interactive 3D track editor with live tile placement and terrain sculpting.
  5. Custom replay recording, playback, and `.RPL` exporting.

---

## Milestone 6: Native macOS Application & Release Packaging
* **Goal**: High-polish native macOS `.app` bundle ready for distribution.
* **Deliverables**:
  1. Self-contained `Stunts.app` bundle for Apple Silicon (ARM64).
  2. Native macOS menu bar options (Video filters, Aspect ratio toggles, Audio drivers, Keybindings).
  3. Sandboxed local storage for custom tracks, replays, and configuration.
  4. Comprehensive documentation and automated CI test suite.
