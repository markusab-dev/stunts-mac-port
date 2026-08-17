#!/usr/bin/env bash
# Build the static-frame test harness for the ported restunts renderer.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

RF="src/render_faithful"

# NOTE: ${RF}/fileio.c is intentionally excluded — rfileio.c replaces it
# with an adapter over the project's verified asset loader.
# -ftrivial-auto-var-init=zero: see note in tools/build_native.sh.
clang -O1 -g \
  -ftrivial-auto-var-init=zero \
  -DRESTUNTS_SDL \
  -include "${RF}/rport.h" \
  -Wno-return-type -Wno-parentheses -Wno-visibility \
  "${RF}/math.c" \
  "${RF}/heapsort.c" \
  "${RF}/shape3d.c" \
  "${RF}/frame.c" \
  "${RF}/rasm_port.c" \
  "${RF}/rblit.c" \
  "${RF}/rfileio.c" \
  "${RF}/rbridge.c" \
  "${RF}/rstubs.c" \
  "${RF}/rdata.c" \
  "${RF}/rdraw_dispatch.c" \
  "${RF}/rframe_helpers.c" \
  src/sim/stunts_math.c \
  src/sim/stunts_sim.c \
  src/sim/stunts_canonical_state.c \
  src/asset/stunts_asset_loader.c \
  src/asset/stunts_dsi_unpack.c \
  src/render/stunts_palette.c \
  tools/render_faithful_frame.c \
  -o bin/render_faithful_frame

echo "Built bin/render_faithful_frame"
