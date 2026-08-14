# Stunts Reverse-Engineering Research & Ecosystem Survey

## 1. Executive Summary

The *Stunts* / *4D Sports Driving* reverse-engineering landscape is one of the most mature retro-gaming disassembly efforts in existence. Over more than 15 years, the community centered around [Stunts Wiki](https://wiki.stunts.hu/) and the **Restunts** project has systematically disassembled, analyzed, and partially decompiled the DOS version of the game.

This document synthesizes findings from:
1. The canonical **Restunts** repository (`4d-stunts/restunts`)
2. The modern **Restunts2** refurbishment project (`dstien/restunts2`)
3. The **Stunts Wiki** technical documentation and forum archives
4. Analysis of the FM Towns 32-bit executable (`4ddfmt.exp`)
5. Direct binary inspection of our original reference files

---

## 2. Classification of Knowledge

To preserve scientific rigor, all technical findings in this project are strictly categorized into three tiers:

| Tier | Classification | Description |
| :---: | :--- | :--- |
| **[FACT]** | **Verified Fact** | Verified by direct binary inspection, byte-level hashing, or code execution against our canonical `Original/Stunts_DOS_EN.zip` files. |
| **[DERIVED]** | **Derived Fact** | Sourced from Restunts disassembly, IDA databases, or Stunts Wiki technical papers; highly reliable but requires ongoing validation against our reference binary. |
| **[HYPOTHESIS]** | **Hypothesis** | Plausible model or assumption regarding undocumented behavior, undefined C expressions, or incomplete disassembly requiring experimental proof. |

---

## 3. History & Structure of Key Projects

### 3.1 The Restunts Project (`4d-stunts/restunts`)
* **Genesis**: Initiated in 2009 by `clvn` (Kevin) with core contributions from `AlbertoMarnetto`, `duplode`, `dstien`, and `LowLevelMahn`.
* **Methodology**: 
  1. Combined `LOAD.EXE` and `MCGA.COD`/`MCGA.DIF`/`EGA.CMN` into a monolithic `GAME.EXE` via `execombiner`.
  2. Disassembled the entire 16-bit real-mode binary in IDA Pro 6.1 (`game_mod3.idb`), splitting it into 40 segmented assembly files (`seg000.asm` – `seg039.asm`) plus data segments (`dseg.asm`, `dsegu.asm`).
  3. Developed an export pipeline (`src/idc/anders.idc`) allowing individual functions in ASM to be substituted with C implementations in `src/restunts/c/`.
  4. Built hybrid DOS executables (`restunts.exe` containing ported C + unported ASM, and `restunto.exe` containing 100% original ASM) using Borland C++ 5.2 / TASM / TLINK / WLINK under DOSBox.
  5. Created `repldump.exe`, a batch-mode headless tool that runs any `.RPL` replay file and dumps the full 1,120-byte `GAMESTATE` structure for every simulation frame.

### 3.2 The Restunts2 Project (`dstien/restunts2`)
* **Modernization**: Transitioned from proprietary IDA Pro/Borland tools to free and contemporary toolchains:
  - Exported IDA analysis database into **Ghidra** with a collaborative multi-user server.
  - Python Ghidra exporter (`ghidra/restunts-export.py`) targeting Open Watcom 2 Assembler syntax.
  - Clean GNU Makefile supporting 16-bit DOS builds, 32-bit protected-mode DOS builds, and preliminary 64-bit Linux unit test builds (`make linux64`).
  - Began enforcing fixed-width integer types (`int16_t`, `int32_t`, etc.) and automated testing.

### 3.3 FM Towns 32-Bit Flat-Model Reference (`4ddfmt.exp`)
* **[FACT]** In 1993, Electronic Arts released *4D Sports Driving* for the Japanese Fujitsu FM Towns platform (`4ddfmt.exp`, dated 1993-02-16).
* **[DERIVED]** The FM Towns executable is a 32-bit flat-memory protected-mode i386 executable shipped with **full unstripped DWARF/Watcom debug symbols**.
* **Significance**: Provides definitive symbol names, struct layouts, and variable types directly from DSI/EA source code, eliminating guesswork regarding variable meanings.

---

## 4. Current State of C Translation (What is Ported vs. Unported)

### 4.1 Fully or Substantially Ported Subsystems in C
* **[DERIVED] Fixed-Point & Vector Mathematics (`math.c` - 27.5 KB)**
  - 1024-degree integer trigonometry (`sin_fast`, `cos_fast`, `polarAngle`, `polarRadius2D/3D`).
  - 3x3 fixed-point matrix transformations (`mat_rot_x/y/z`, `mat_multiply`, `mat_invert`).
  - 2D screen/world rectangle operations (`rect_union`, `rect_intersect`, `rect_is_inside`).
  - Plane and normal inner product math (`plane_origin_op`, `plane_rotate_op`).
* **[DERIVED] Vehicle Physics & Drivetrain (`statecar.c` - 25.5 KB, `stateply.c` - 95.2 KB)**
  - Engine RPM and transmission gear ratios (`update_rpm_from_speed`, `update_car_speed`).
  - Aerodynamic drag calculations via `aerorestable` lookup tables.
  - Four-wheel terrain/surface grip integration (`car_sumSurfAllWheels`, `surface_grip[4]`).
  - Lateral tire slip, skid angle, and drift state machines.
  - Airborne pseudo-gravity integration and jump flight mechanics.
  - Collision responses against track planes (`plan`) and vertical barriers (`wall`).
* **[DERIVED] Resource Decompression & File I/O (`fileio.c` - 23.1 KB)**
  - Multi-pass DSI decompressor (RLE byte run decoding, RLE sequence decoding, Variable-Length Huffman/VLE tree unpacking).
  - Track loader (`file_load_track`), replay reader (`file_load_replay`), 3D shape loader (`file_load_3dres`).
* **[DERIVED] Memory Management (`memmgr.c` - 14.7 KB)**
  - DSI resource handle table and dynamic paragraph allocation (`mmgr_alloc_resbytes`).
* **[DERIVED] 3D Software Rasterization Pipeline (`shape3d.c` - 127.3 KB)**
  - 3D vertex rotation, camera transformation, frustum clipping, perspective division, and polygon rasterizer.

### 4.2 Partially Ported or Unported Subsystems
* **[DERIVED] Opponent AI Behavioral Logic (`state.c`, `seg002.asm`)**
  - Opponent decision-making and waypoint steering (`OpponentOp`, `TrackOpponentOp`) are partially analyzed but contain assembly segments.
* **[DERIVED] Audio Engine & Sound Synthesis (`seg008.asm`, `seg009.asm`)**
  - FM synth / Roland MT-32 driver communication is still largely in 16-bit real-mode assembly.
* **[DERIVED] Menu System & UI Event Loops (`restunts.c`, `seg001.asm`)**
  - Menu navigation, dialog event handling, copy protection quiz, and track editor UI are deeply intertwined with DOS VGA mode 13h / 07h interrupts.

---

## 5. Verification Tools & Behavioral State Dumps

* **[FACT]** `repldump` exists as a headless test runner.
* **Format of Dump**:
  - Starts with a 2-byte unsigned short: `total_recorded_frames`.
  - Followed by `total_recorded_frames` records of `struct GAMESTATE` (exactly **1,120 bytes per frame**).
* **Deterministic Oracle**:
  Running `repldump.exe` under DOSBox with an unpatched binary (`repldumo.exe`) yields the exact ground-truth frame-by-frame physics trajectory. Any native implementation can be fed the identical input replay and validated byte-for-byte.
