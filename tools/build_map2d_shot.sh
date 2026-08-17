#!/usr/bin/env bash
# Build bin/map2d_shot, the Phase 11 harness for the editor's 2D track map.
# Same flags and the same object set as tools/build_dumper.sh, plus rpes.c
# (SDTEDIT is a .PES) and the new src/render_faithful/rtrackmap2d.c.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

RF="src/render_faithful"
SF="src/sim_faithful"

mkdir -p bin
clang -O1 -g \
  -DRFB_SCALE=${RFB_SCALE:-1} \
  -ftrivial-auto-var-init=zero \
  -DRESTUNTS_SDL \
  -I"${RF}" -include "${SF}/sfport.h" \
  -Wno-return-type -Wno-parentheses -Wno-visibility \
  -Wno-incompatible-pointer-types -Wno-unused-label \
  "${SF}"/state.c "${SF}"/statecar.c "${SF}"/statecrs.c "${SF}"/stateply.c \
  "${SF}"/sfasm_port.c "${SF}"/sfopponent.c "${SF}"/sfstubs.c "${SF}"/sfdata.c \
  "${SF}"/sfshapeinfo.c "${SF}"/sfdseg_head.c "${SF}"/sftrack_setup.c \
  "${RF}"/math.c "${RF}"/heapsort.c "${RF}"/shape3d.c "${RF}"/frame.c \
  "${RF}"/rskybox.c "${RF}"/ringame_text.c "${RF}"/rcrash.c "${RF}"/rexplode.c \
  "${RF}"/rshape2d.c "${RF}"/rasm_port.c "${RF}"/rdraw_dispatch.c \
  "${RF}"/rframe_helpers.c "${RF}"/rfont.c "${RF}"/rhighscore.c "${RF}"/rdata.c \
  "${RF}"/rblit.c "${RF}"/rfileio.c "${RF}"/rstubs.c "${RF}"/rpes.c \
  "${RF}"/rtrackmap2d.c "${RF}"/reditoricons.c \
  src/asset/stunts_asset_loader.c src/asset/stunts_dsi_unpack.c \
  src/render/stunts_palette.c \
  tools/map2d_shot.c \
  -o bin/map2d_shot

echo "Built bin/map2d_shot"
