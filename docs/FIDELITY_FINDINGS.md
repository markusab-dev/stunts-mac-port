# Stunts Fidelity Findings & Architectural Insights

## 1. Executive Summary

Phase 2 constructed the **Stunts Fidelity & Verification Laboratory** to establish a mathematically deterministic test environment on Apple Silicon ARM64.

Before developing native gameplay or rendering, we investigated the original MS-DOS simulation data model, decompressed resource archives, verified binary compatibility against the user-owned Stunts 1.1 distribution, and built a differential verification engine capable of detecting the exact frame and variable of any simulation divergence.

---

## 2. Key Forensic & Architectural Findings

### 2.1 Oracle Provenance & Source Hierarchy
* **Primary Oracle (`repldumo.exe`)**: Links only `ASMORIG_OBJFILES` (`seg000.asm` – `seg039.asm`, `dseg.asm`). It executes 100% unpatched 16-bit real-mode x86 assembly.
* **Validated Derived Oracle (`repldump.exe`)**: Links ported C modules (`stateply.c`, `statecar.c`, `math.c`, `fileio.c`). When verified against `repldumo.exe`, it provides human-readable C verification.
* **Asset Compatibility**: All 11 car geometry/physics blocks (`ST*.P3S`), track files (`DEFAULT.TRK`), and physics collision archives (`GAME.PRE`) in our legally owned distribution (`Original/Stunts_DOS_EN.zip`) match the Restunts reference assets 100% byte-for-byte (SHA-256 verified).

### 2.2 DSI Resource Packaging Format
Distinctive Software Inc. (DSI) used a unified binary archive structure across `.RES`, `.PRE`, `.P3S`, and `.PVS` files:
1. **Header (4 bytes)**: Uncompressed payload length ($L$).
2. **Sub-Resource Count (2 bytes)**: Number of resource tags ($N$) at offset 4.
3. **Tag Directory ($4N$ bytes)**: Array of 4-character ASCII strings (e.g. `"simd"`, `"plan"`, `"wall"`, `"car0"`).
4. **Offset Table ($4N$ bytes)**: 32-bit little-endian relative offsets from data start.
5. **Data Payload**: Starts at offset $6 + 8N$.

```
+------------------+------------------+------------------+--------------------+---------------------+
| 4B Decomp Size   | 2B Num Res (N)   | 4N Tag Array     | 4N Offset Array    | Data Payload (6+8N) |
+------------------+------------------+------------------+--------------------+---------------------+
```

* **Compression**: Handled by single-pass or multi-pass run-length encoding (RLE) and variable-length encoding (VLE/Huffman).
* **Physics Planes (`plan`)**: Decompresses from `GAME.PRE` into exactly **536 collision planes** (34 bytes each, 18,224 bytes total).
* **Vehicle Dynamics (`simd`)**: Embedded in `CAR<ID>.RES` (320 bytes containing 104-byte torque curve, gear ratios, aero drag, steering lock tables, and 4-wheel offsets).

### 2.3 Mathematical Model & Fixed-Point Units
* **Discrete Simulation Rate**: Fixed 20 Hz ($50\text{ ms}$ per tick).
* **Angular Grid**: 1024 discrete units $= 360^\circ$ ($0.35156^\circ$ per unit, $90^\circ = 256$).
* **Trigonometry**: 14-bit fixed point with 257-entry `sintab` ($1.0 = 16384 = 2^{14}$). Quadrant folding enables exact full-circle sine and cosine without floating point.
* **Inverse Tangent**: 258-entry `atantable` indexed by $((z \ll 16) / y) \gg 8$, folded across octants using 3-bit flag arithmetic.
* **World Space**: 1 tile $= 1024 \times 1024$ world units. Track grid is $30 \times 30$ tiles ($30720 \times 30720$ units). Elevation $+Y$ is vertical.

### 2.4 Discrete Input Bitfield Format
Replay files (`.RPL`) record player inputs as a compact 1-byte bitfield per 50 ms tick:
* **Bit 0**: Accelerate (1 = pressed, 0 = released)
* **Bit 1**: Brake (1 = pressed, 0 = released)
* **Bits 2..3**: Steering Code (`00` = Center, `01` = Turn Right, `10` = Turn Left)
* **Bit 4**: Gear Shift Down (1 = request downshift)
* **Bit 5**: Gear Shift Up (1 = request upshift)
* **Bits 6..7**: Reserved / Unused

---

## 3. Tooling & Verification Infrastructure Delivered

| Component | Path | Description |
| :--- | :--- | :--- |
| **Common Types & Schema** | `src/common/stunts_types.h` | C99 fixed-width integer types and `STUNTS_CANONICAL_STATE_V1` definition |
| **Integer Math Library** | `src/sim/stunts_math.h`, `.c` | 1024-degree integer trig, 3x3 matrices, wrapping helpers |
| **DSI Decompressor** | `src/asset/stunts_dsi_unpack.h`, `.c` | Portable multi-pass RLE/VLE unpacker |
| **Asset Loader** | `src/asset/stunts_asset_loader.h`, `.c` | `.TRK`, `.RPL`, `SIMD`, `plan` loader reading from original files |
| **Simulation Core** | `src/sim/stunts_sim.h`, `.c` | Discrete 20 Hz vehicle simulation tick |
| **Canonical Exporter** | `src/sim/stunts_canonical_state.h`, `.c` | Serializer for JSON Lines (`.jsonl`) and compact binary (`.cs1`) |
| **Native Verification Target** | `src/main_verify.c` (`bin/stunts_sim_verify`)| Headless CLI simulation runner |
| **Semantic State Diff Tool** | `tools/state_diff.py` (`tools/state-diff`) | Frame-by-frame differential tester with context diagnostic reporting |
| **Unified Regression Suite** | `tools/test-fidelity` | Automated single-command test suite |

---

## 4. Verification & Regression Results

Running `./tools/test-fidelity` on Apple Silicon macOS:
1. **Compiler-Semantic Unit Tests (Debug `-O0`)**: 38/38 Passed (0 failures).
2. **Compiler-Semantic Unit Tests (Release `-O3`)**: 38/38 Passed (0 failures).
3. **Asset Pipeline Tests**: 11/11 Passed (0 failures).
4. **State Diff Self-Check**: 100% bit-exact across all frames (0 divergent variables).
5. **State Diff Mutation Diagnostic Test**: Accurately detected injected fault on Frame 42 in variable `'speed_actual'` with 3-frame trajectory lead-up context.
