#!/usr/bin/env bash
# Build bin/dump_native_states, the harness that replays a .RPL through the
# vendored simulation and writes one 1120-byte GAMESTATE per frame, so
# tools/diff_oracle.py can compare it against the real game's dump.
#
# Same flags as tools/build_native.sh minus SDL: this must be the *same* code
# the playable build runs, or the comparison proves nothing.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

RF="src/render_faithful"
SF="src/sim_faithful"

clang -O1 -g \
  -ftrivial-auto-var-init=zero \
  -I"${RF}" -include "${SF}/sfport.h" \
  -Wno-return-type -Wno-parentheses -Wno-visibility \
  -Wno-incompatible-pointer-types -Wno-unused-label \
  "${SF}"/state.c "${SF}"/statecar.c "${SF}"/statecrs.c "${SF}"/stateply.c \
  "${SF}"/sfasm_port.c "${SF}"/sfopponent.c "${SF}"/sfstubs.c "${SF}"/sfdata.c "${SF}"/sfshapeinfo.c "${SF}"/sfdseg_head.c "${SF}"/sftrack_setup.c \
  "${RF}"/math.c "${RF}"/heapsort.c "${RF}"/shape3d.c "${RF}"/frame.c \
  "${RF}"/rskybox.c "${RF}"/ringame_text.c "${RF}"/rcrash.c "${RF}"/rexplode.c "${RF}"/rshape2d.c \
  "${RF}"/rasm_port.c "${RF}"/rdraw_dispatch.c "${RF}"/rframe_helpers.c \
  "${RF}"/rfont.c "${RF}"/rhighscore.c "${RF}"/rdata.c "${RF}"/rblit.c "${RF}"/rfileio.c "${RF}"/rstubs.c \
  src/asset/stunts_asset_loader.c src/asset/stunts_dsi_unpack.c \
  src/render/stunts_palette.c \
  tools/dump_native_states.c \
  -o bin/dump_native_states

echo "Built bin/dump_native_states"
