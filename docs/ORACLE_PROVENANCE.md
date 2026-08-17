# Oracle Provenance & Reference Verification

## 1. Executive Summary

In retro-engineering and behavioral source-porting, establishing the **provenance and veracity of the reference oracle** is the single most critical methodological requirement.

A common failure mode in emulator and port development is **circular validation**: comparing a newly translated C function against an earlier reverse-engineered C function, where both share identical transcription errors or undefined compiler assumptions.

This document establishes:
1. The exact pedigree of every reference binary and tool in this project.
2. The architectural distinction between unpatched original assembly (`repldumo.exe`) and translated C (`repldump.exe`).
3. Binary comparison between our legally owned Stunts 1.1 retail distribution and the Restunts reference assets.
4. The four-tier reference classification schema.

---

## 2. Classification of Reference Oracles

Every piece of reference code, tooling, data, and behavioral assertion in this project is explicitly assigned one of four classification tiers:

```
+-------------------------------------------------------------------------+
|                  TIER 1: PRIMARY ORACLE (Ground Truth)                  |
|  - Original Stunts 1.1 DOS Retail Binary (LOAD.EXE / EGA.CMN / MCGA.*)  |
|  - 100% Unpatched Disassembly Binary (repldumo.exe / restunto.exe)      |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|              TIER 2: VALIDATED DERIVED ORACLE (Proven Proxy)            |
|  - Ported C Tools Proven Frame-by-Frame Against Tier 1 (repldump.exe)   |
|  - Canonical State Exporter (STUNTS_CANONICAL_STATE_V1)                 |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                    TIER 3: SECONDARY REFERENCE                          |
|  - FM Towns 32-bit Binary with DWARF Symbols (4ddfmt.exp)               |
|  - Restunts C Decompilation (In-progress / unverified modules)          |
|  - Stunts Wiki Technical Papers & Modding Documentation                 |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                    TIER 4: HYPOTHESIS (Requires Proof)                  |
|  - Unverified assumptions regarding undefined C expressions             |
|  - Reconstructed algorithmic approximations                             |
+-------------------------------------------------------------------------+
```

### 2.1 Tier 1: Primary Oracle (The Mathematical Ground Truth)
* **Definition**: Execution behavior generated strictly by the unadulterated 1991 MS-DOS machine code.
* **Components**:
  1. **Canonical Retail Binary**: The user-owned `Original/Stunts_DOS_EN.zip` (`8c016289cf1e03525f0e6be93d73daf55f43bb5f546789f73fa3136660ef0886`).
  2. **`repldumo.exe`**: Built directly from `ASMORIG_OBJFILES` (`seg000.asm` – `seg039.asm`, `dseg.asm`). This target links **zero ported C code** and executes the byte-for-byte 16-bit real-mode assembly instructions of original Stunts.

### 2.2 Tier 2: Validated Derived Oracle
* **Definition**: Tooling written in C that has been mathematically verified against Tier 1 by differential testing with **0 divergent bytes**.
* **Components**:
  1. **`repldump.exe`**: When Restunts compiles `repldump.exe` (using ported C functions in `src/restunts/c/`), it is validated by diffing its `.BNI` output against `repldumo.exe`'s `.BIN` output across the entire replay suite.
  2. **`STUNTS_CANONICAL_STATE_V1` Extractor**: Extracts discrete physical variables from Tier 1 dumps without host-specific padding or compiler artifacts.

### 2.3 Tier 3: Secondary Reference
* **Definition**: High-value technical assets that guide engineering but cannot be treated as infallible proof of DOS 1.1 behavior without cross-verification.
* **Components**:
  1. **FM Towns 32-bit Binary (`4ddfmt.exp`, 1993)**: Contains original DSI variable and function names. While invaluable for naming and type information, it was compiled 2 years after DOS 1.1 with a 32-bit compiler and may contain minor engine modifications.
  2. **Restunts C Decompilation (`reference/restunts/src/restunts/c/`)**: Provides human-readable C translations of assembly routines.

### 2.4 Tier 4: Hypotheses
* **Definition**: Any assumption regarding compiler undefined behavior (e.g. division-by-zero register preservation, signed 16-bit integer overflow handling, uninitialized variable reads) that has not yet been proven via assembly disassembly and oracle test execution.

---

## 3. Binary & Resource Compatibility Audit

We conducted a byte-level SHA-256 comparison between our canonical distribution (`Original/Stunts_DOS_EN.zip`) and the Restunts 1.1 reference distribution:

### 3.1 Physics & Geometry Resources (100% Identity)
| Resource | File in Our Archive | File in Reference | Our SHA-256 | Status |
| :--- | :--- | :--- | :--- | :---: |
| **Physics Planes & Collision** | `GAME.PRE` (15,434 B) | `GAME.PRE` (15,434 B) | `1ef98457007aa22a76f62b0ce80a1324707bb8a84c68832a8327c593631ca560` | **MATCH** |
| **3D Track Elements** | `GAME1.P3S` (17,556 B) | `GAME1.P3S` (17,556 B) | `e1dcf69b0d62dbe76e3d2319ef3e6484feaeefcbebfb0d39e3381a179379fb3f` | **MATCH** |
| **3D Scenery Objects** | `GAME2.P3S` (21,852 B) | `GAME2.P3S` (21,852 B) | `50e1ef382b6be00bf49cb150a04944ec7f0b2f767f4aa882c5f1c97a7e1f42ad` | **MATCH** |
| **Resource Directory** | `MAIN.RES` (1,504 B) | `MAIN.RES` (1,504 B) | `4d2ffcae594d0fb8cbafcc88ff05e04cb249e0c1f26a17b01d322ef8684bb5f2` | **MATCH** |
| **Default Track** | `DEFAULT.TRK` (1,802 B)| `DEFAULT.TRK` (1,802 B)| `a1f49635f8b9ec47c4bc47c87c9fe7079f2ea7da829c7886fa70f803c623efea` | **MATCH** |

### 3.2 Vehicle Physics Blocks (`SIMD` in `ST*.P3S` — 100% Identity)
All 11 vehicle 3D models and embedded `SIMD` physics parameter blocks match byte-for-byte:
* `STAUDI.P3S` (Audi Quattro Sport): `b93d3950a41d9263a233b2bf1d1f053ca438b4d896baaa3fcbbdd86bf3ec05c4` (MATCH)
* `STVETT.P3S` (Corvette ZR1): `1787d5598d1a1b18ca0ca4b4c7fc5fe7ecb3a24128ddb38da97e556e4313f898` (MATCH)
* `STP962.P3S` (Porsche 962 IMSA): `5ee4a4c51e9e09d17d09eccefe6aa7d620579e0a297e54cbe5fbfa0d9e262174` (MATCH)
* `STCOUN.P3S` (Countach 25th): `a4e76813359d9cffebae85ee4fe97a9f993d05ea07e6ce3fcf6ca8f44fffa109` (MATCH)
* `STFGTO.P3S` (Ferrari 288 GTO): `b4b84b8dc2287413d33dfdf38127fb962f31d044fba282b826b5397f395b2ce0` (MATCH)
* `STJAGU.P3S` (Jaguar XJR-9): `2ae47ff7130eb5a6d366a5965b0952d76ea2bc79555c4d0a9b8979b0075c3d40` (MATCH)
* `STLANC.P3S` (Lancia Delta HF): `b05423851b38f8fc686a9f4c3a7fcab2084c8a2b53a06733230a108a7fb785bc` (MATCH)
* `STLM02.P3S` (Lamborghini LM002): `0c6a58bf37d2f9ecff4c3b65287f3780fa2a061448b11116c27fc3cb0ebffb2e` (MATCH)
* `STPC04.P3S` (Porsche Carrera 4): `961726a978fcf70a6c0d0c3eb14917a2dc9944a1eb4044a2c9be066060c5c363` (MATCH)
* `STPMIN.P3S` (Porsche March-Indy): `3860bb4f10118eb3ebc20fa000a6dcda16eb250325ea7b218f4a7c06ebef1952` (MATCH)
* `STANSX.P3S` (Acura NSX): `b8cb9d5045b59746e01768822055ae3b40d65b16e88e89fe8ba051be7d25e07a` (MATCH)

### 3.3 Runtime Executable Components
* **`EGA.CMN`**: Common executable base in our distribution (`extracted/stunts/stunts/EGA.CMN`, 127,337 bytes) is identical to the base used to construct Restunts disassembly.
* **`MCGA.DIF` & `MCGA.COD`**: Overlay files in our distribution match the Restunts assets.

---

## 4. Avoiding Circular Validation in the Test Loop

To ensure true independent validation:
1. **Primary Ground Truth Dumps**: Reference `.BIN` state dumps are generated by running `repldumo.exe` (unpatched 100% original assembly) under DOSBox on the user's Mac.
2. **Canonical State Extraction**: The binary dump is parsed into architecture-independent `STUNTS_CANONICAL_STATE_V1` records.
3. **Native ARM64 Target**: Our native Apple Silicon simulation executes independently, loads data from `extracted/stunts/stunts/`, advances at 20 Hz, and emits its own `STUNTS_CANONICAL_STATE_V1` stream.
4. **Differential Comparison**: `tools/state-diff` verifies that the native ARM64 simulation produces identical values across every single simulation state variable on every frame.
