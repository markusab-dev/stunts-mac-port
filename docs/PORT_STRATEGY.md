# Native Port Strategy & Architectural Evaluation

## 1. Architectural Strategy Comparison

We evaluated three potential architectural strategies for bringing *Stunts* to native Apple Silicon macOS:

| Strategy | Architecture | Fidelity | Unknowns | Effort | Testing Rigor | macOS Portability | Replay Compat | Maintainability |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **A — Continue Restunts** | 16-bit DOS hybrid with assembly patching and Borland/Watcom toolchain | **100% (DOS)** | Very Low | High | High (via DOSBox) | **Very Poor** (Requires 16-bit x86 emulator) | 100% | Low (Obsolete 16-bit toolchains) |
| **B — Faithful Hybrid Source Port** *(Recommended)* | Native C/C++ core reusing verified physics/math from Restunts; native Metal/SDL presentation | **100% (Bit-Identical)** | Low | **Moderate** | **Extreme** (Automated frame diffing) | **Native ARM64 / macOS `.app`** | **100%** | **High** (Clean modern modular C) |
| **C — Clean Re-implementation** | Rewrite engine from scratch using black-box observations and modern physics | Low (<85%) | Extreme | High | Low (Subjective only) | High | **0% (Guaranteed Desync)** | High |

---

## 2. Recommendation & Justification

### Recommendation: Strategy B — Faithful Hybrid Source Port

### Why Strategy A Falls Short for macOS
Strategy A preserves 16-bit segmented real-mode constraints (`far`/`near` pointers, segment registers `DS`/`ES`, 640 KB memory limits, INT 21h calls). This architecture cannot compile to a 64-bit ARM64 Mach-O binary without running inside an emulated DOS layer, defeating the goal of a true native macOS application.

### Why Strategy C Fails Fidelity Requirements
Modern vehicle physics engines (Bullet, PhysX, Box2D, Unity/Godot physics) use continuous floating-point rigid-body dynamics, penalty springs, and iterative impulse solvers. Stunts physics, by contrast, is a unique, highly specialized integer simulation developed by Kevin Pickell in 1990:
- Discrete 1024-degree integer trigonometry.
- Fixed 20 Hz step integration with speed-dependent aerodynamic and grip tables.
- Specific collision clamping rules against `plan` resources.
- Iconic emergent glitches (overdrive speed accumulation, corner hopping, rocket bounces).

Any black-box re-implementation will immediately break replay compatibility and fail to reproduce authentic vehicle handling.

### Why Strategy B Is Optimal
Strategy B takes the **exact mathematical formulas, lookup tables, and simulation loops** verified by the Restunts project, translates them to clean, portable, fixed-width C99 types (`int16_t`, `int32_t`, `uint16_t`, `uint32_t`), and connects them to a modern macOS platform wrapper. It achieves **100% behavioral identity** while running natively on Apple Silicon.

---

## 3. Native Port Modular Architecture

The native application is architected into five cleanly separated modules:

```
+--------------------------------------------------------------------------------+
|                                Stunts.app (macOS)                              |
+--------------------------------------------------------------------------------+
|  [Presentation Layer]                                                          |
|  - Metal 3.0 / SDL3 Windowing & Framebuffer Presentation                       |
|  - Native macOS Menu Bar, Fullscreen, Retina & Integer Scaling                 |
|  - GameController Framework (DualSense, Xbox, Keyboard, Mouse)                 |
+--------------------------------------------------------------------------------+
|  [Audio Engine]                                                                |
|  - Embedded Roland MT-32 Synth (libmt32emu) + OPL3 FM Synthesizer              |
|  - CoreAudio / Low-Latency PCM Output                                          |
+--------------------------------------------------------------------------------+
|  [Renderer Subsystem]                                                          |
|  - Faithful 320x200 8-bit Software Rasterizer (100% authentic visual pipeline) |
|  - Optional Hardware-Accelerated 3D Renderer (Wide Aspect, Higher FPS)         |
+--------------------------------------------------------------------------------+
|  [Core Simulation Library (stunts-sim)] -- 100% Portable C99                   |
|  - Fixed-Point Vector/Matrix Math (1024-degree LUTs)                           |
|  - Discrete 20 Hz Physics Pipeline & Collision Planes (plan/wall)              |
|  - Vehicle Drivetrain & Surface Grip Engine                                    |
|  - Opponent AI Decision Tree & Path Navigation                                 |
|  - Deterministic PRNG (kevinrandom) & Replay Player                            |
+--------------------------------------------------------------------------------+
|  [Asset & Storage Pipeline]                                                    |
|  - In-Memory DSI Unpacker (Reads directly from user's Original/STUNTS.zip)     |
|  - Track (.TRK), Highscore (.HIG), Replay (.RPL), 3D Shape (.P3S) Parsers      |
+--------------------------------------------------------------------------------+
```

---

## 4. Subsystem Breakdown

### 4.1 `libstunts_sim` (Simulation Core)
* **Language**: Pure C99 with standard `<stdint.h>`. Zero dependencies on OS, graphics, or audio APIs.
* **Responsibilities**:
  - `sim_math`: Fixed-point trigonometry (`sin_fast`, `cos_fast`, `polarAngle`), 3x3 matrices, vector math.
  - `sim_state`: Vehicle state integration, engine RPM, torque curves, aerodynamic drag, tire grip.
  - `sim_collision`: 3D plane intersection (`plan`), barrier collision (`wall`), and terrain height queries.
  - `sim_replay`: Deterministic replay stream consumption and state generation.
* **Verification Interface**: Exposes a clean C API to run headless simulations and output binary `GAMESTATE` frames.

### 4.2 `libstunts_asset` (Asset Pipeline)
* **Zero Asset Redistribution**: Loads directly from the user's `Original/` archive or folder at runtime.
* **Responsibilities**:
  - DSI decompression (RLE and VLE/Huffman passes).
  - Parsing `.P3S` 3D shapes, `SIMD` physics tables, `.TRK` layouts, `.HIG` records, and `.PVS` bitmaps.

### 4.3 `libstunts_render` (Rendering Engine)
* **Primary Renderer**: Faithful software polygon rasterizer replicating DSI's 320x200 MCGA Mode 13h output buffer.
* **Scaler**: Metal shader presenting the 320x200 buffer with crisp integer scaling, correct 4:3 CRT aspect correction, and optional scanline filtering.

### 4.4 `libstunts_audio` (Sound Engine)
* **Music & Effects**: Leverages embedded `libmt32emu` to synthesize Roland MT-32 scores (`.KMS`) and Sound Blaster OPL3 FM audio without requiring physical vintage synthesizer hardware.

### 4.5 `stunts_app` & `stunts_cli`
* **`stunts_app`**: Native Cocoa/Metal executable bundled into `Stunts.app`.
* **`stunts_cli`**: Command-line verification tool for automated regression testing against DOS oracle dumps.
