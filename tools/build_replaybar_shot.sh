#!/usr/bin/env bash
# Build bin/replaybar_shot - the viewer for Phase 9's recording bar.
#
# Same flags and (almost) the same file list as tools/build_native.sh; the
# only substitution is tools/replaybar_shot.c for src/main_native.c, so the
# strip is drawn by exactly the code the game runs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

RF="src/render_faithful"
SF="src/sim_faithful"

clang -O1 -g \
  -DRFB_SCALE=${RFB_SCALE:-1} \
  -ftrivial-auto-var-init=zero \
  -DRESTUNTS_SDL \
  -I"${RF}" -include "${SF}/sfport.h" \
  -I/opt/homebrew/include/SDL2 \
  -Wno-return-type -Wno-parentheses -Wno-visibility \
  -Wno-incompatible-pointer-types -Wno-unused-label \
  "${SF}"/state.c "${SF}"/statecar.c "${SF}"/statecrs.c "${SF}"/stateply.c \
  "${SF}"/sfasm_port.c "${SF}"/sfopponent.c "${SF}"/sfstubs.c "${SF}"/sfdata.c \
  "${SF}"/sfshapeinfo.c "${SF}"/sfdseg_head.c "${SF}"/sftrack_setup.c \
  "${RF}"/math.c "${RF}"/heapsort.c "${RF}"/shape3d.c "${RF}"/frame.c \
  "${RF}"/rskybox.c "${RF}"/ringame_text.c "${RF}"/rcrash.c "${RF}"/rexplode.c \
  "${RF}"/rasm_port.c "${RF}"/rdraw_dispatch.c "${RF}"/rframe_helpers.c \
  "${RF}"/rshape2d.c "${RF}"/rtrackprev.c "${RF}"/rcarmenu.c "${RF}"/rreplaybar.c \
  "${RF}"/rfont.c "${RF}"/rhighscore.c "${RF}"/rwidgets.c "${RF}"/rendscreen.c \
  "${RF}"/rdata.c "${RF}"/rblit.c "${RF}"/rfileio.c "${RF}"/rstubs.c \
  "${RF}"/rintro.c "${RF}"/rintro3d.c "${RF}"/rpes.c \
  src/asset/stunts_asset_loader.c src/asset/stunts_dsi_unpack.c \
  src/render/stunts_palette.c \
  src/audio_native.c src/music_native.c src/music_opl_nuked.c \
  src/vendored/opl/opl3.c \
  tools/replaybar_shot.c \
  -L/opt/homebrew/lib -lSDL2 \
  -o bin/replaybar_shot

echo "Built bin/replaybar_shot"
