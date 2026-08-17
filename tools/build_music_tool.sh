#!/usr/bin/env bash
# Build bin/music_tool - the device-free driver for src/music_native.c.
# Links no SDL and none of the renderer; music_native.c is self-contained.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"
mkdir -p bin

# Nuked-OPL3 is vendored in src/vendored/opl/ (LGPL-2.1, unmodified).
# src/music_opl_stub.c remains as the verification tone probe, NOT an OPL
# emulator. See src/vendored/opl/README before believing any .wav.
clang -O2 -g -Wall -Wextra -Wno-unused-parameter \
  src/music_native.c src/music_opl_nuked.c src/vendored/opl/opl3.c tools/music_tool.c \
  -lm -o bin/music_tool

echo "Built bin/music_tool"
