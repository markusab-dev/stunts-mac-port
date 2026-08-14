# Toolchain & Development Environment Guide

This document describes the required dependencies, installation procedures, and build tooling for the Stunts Native macOS Port project.

---

## 1. System Requirements

* **Host Platform**: macOS (Apple Silicon ARM64)
* **SDK / Compiler**: Xcode Command Line Tools (`clang`, `clang++`, `make`)
* **Scripting**: Python 3.10+ (standard library only for core scripts)
* **Package Manager**: Homebrew (`/opt/homebrew/bin/brew`)

---

## 2. Installed Development Tools

### 2.1 Emulators & Debugging Oracles
* **DOSBox Staging (v0.82.2)**: `/opt/homebrew/bin/dosbox-staging`
  - High-fidelity DOS emulation with integrated MT-32 synthesizer support.
  - Used for headless test oracle runs and baseline execution.
* **DOSBox-X (v2026.08.02)**: `/opt/homebrew/bin/dosbox-x`
  - Advanced emulator with built-in machine debugger, register views, and memory inspection.

### 2.2 Core macOS Toolchain
* **Clang C/C++ Compiler**: `/usr/bin/clang` (Apple LLVM compiler targeting ARM64 Mach-O binaries).
* **Python 3**: `/opt/homebrew/bin/python3` (Used for inventory, state dumping, and binary comparison).
* **Git Version Control**: Initialized locally in workspace.

---

## 3. Project Tooling Scripts

| Script | Path | Purpose |
| :--- | :--- | :--- |
| **Inventory & Hasher** | `tools/inventory.py` | Extracts original ZIP, validates SHA-256 hashes, generates JSON inventory. |
| **DOSBox Runner** | `tools/run_dos_stunts.sh` | Automated shell wrapper to mount and run games/tools in DOSBox. |
| **DOSBox Config** | `tools/dosbox_stunts.conf` | Tailored DOSBox configuration for accurate 20k cycles execution. |
| **State Comparator** | `tools/compare_states.py` | Frame-by-frame binary comparison tool for 1,120-byte `GAMESTATE` dumps. |

---

## 4. Quick Start Commands

### 4.1 Re-run Inventory & File Verification
```bash
python3 tools/inventory.py
```

### 4.2 Launch Original DOS Stunts in DOSBox
```bash
# Launch default game
bash tools/run_dos_stunts.sh

# Launch hardware setup utility
bash tools/run_dos_stunts.sh SETUP.EXE

# Launch replay dumper (when repldump is compiled)
bash tools/run_dos_stunts.sh REPLDUMP.EXE DEFAULT.RPL
```

### 4.3 Run State Verification Comparator
```bash
python3 tools/compare_states.py reference.bin target.bin
```
