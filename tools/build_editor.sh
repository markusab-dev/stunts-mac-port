#!/usr/bin/env bash
# Build bin/editor_shot: the Phase 11 track editor (reditor.c, rdialog.c)
# with a standalone driver, so it can be built, run and screenshotted
# without src/main_native.c being involved at all.
#
# The object list is build_intro.sh's, minus the intro, plus reditor.c,
# rdialog.c and tools/editor_shot.c.  No SDL: nothing here opens a window.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

RF="src/render_faithful"
SF="src/sim_faithful"

mkdir -p bin

clang -O2 -g \
  -DRFB_SCALE=${RFB_SCALE:-1} \
  -ftrivial-auto-var-init=zero \
  -DRESTUNTS_SDL \
  -I"${RF}" -Isrc -include "${SF}/sfport.h" \
  -Wno-return-type -Wno-parentheses -Wno-visibility \
  -Wno-incompatible-pointer-types -Wno-unused-label \
  "${SF}"/state.c "${SF}"/statecar.c "${SF}"/statecrs.c "${SF}"/stateply.c \
  "${SF}"/sfasm_port.c "${SF}"/sfopponent.c "${SF}"/sfstubs.c "${SF}"/sfdata.c \
  "${SF}"/sfshapeinfo.c "${SF}"/sfdseg_head.c "${SF}"/sftrack_setup.c \
  "${RF}"/math.c "${RF}"/heapsort.c "${RF}"/shape3d.c "${RF}"/frame.c \
  "${RF}"/rskybox.c "${RF}"/ringame_text.c "${RF}"/rcrash.c "${RF}"/rexplode.c \
  "${RF}"/rasm_port.c "${RF}"/rdraw_dispatch.c "${RF}"/rframe_helpers.c \
  "${RF}"/rshape2d.c "${RF}"/rfont.c "${RF}"/rhighscore.c "${RF}"/rdata.c \
  "${RF}"/rblit.c "${RF}"/rfileio.c "${RF}"/rstubs.c "${RF}"/rwidgets.c \
  "${RF}"/rpes.c "${RF}"/rdialog.c "${RF}"/reditor.c \
  src/asset/stunts_asset_loader.c src/asset/stunts_dsi_unpack.c \
  src/render/stunts_palette.c \
  tools/editor_shot.c \
  -o bin/editor_shot

echo "Byggde bin/editor_shot"
