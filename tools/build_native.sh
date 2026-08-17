#!/usr/bin/env bash
# Build the playable native port: original simulation + original renderer,
# both operating directly on the original GAMESTATE globals.
#
# Note: neither src/sim/ (the hand-written approximation) nor
# src/render_faithful/rbridge.c (its state-copy bridge) is linked here.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

RF="src/render_faithful"
SF="src/sim_faithful"

# -ftrivial-auto-var-init=zero: the vendored code inherits the original's
# habit of reading locals before writing them. DOS had predictable stack
# garbage; we do not, which made runs differ from each other.
clang -O2 -g ${ASAN:+-fsanitize=address -fno-omit-frame-pointer} \
  -DRFB_SCALE=${RFB_SCALE:-1} \
  -ftrivial-auto-var-init=zero \
  -DRESTUNTS_SDL \
  -I"${RF}" -include "${SF}/sfport.h" \
  -I/opt/homebrew/include/SDL2 \
  -Wno-return-type -Wno-parentheses -Wno-visibility \
  -Wno-incompatible-pointer-types -Wno-unused-label \
  "${SF}"/state.c "${SF}"/statecar.c "${SF}"/statecrs.c "${SF}"/stateply.c \
  "${SF}"/sfasm_port.c "${SF}"/sfopponent.c "${SF}"/sfstubs.c "${SF}"/sfdata.c "${SF}"/sfshapeinfo.c "${SF}"/sfdseg_head.c "${SF}"/sftrack_setup.c \
  "${RF}"/math.c "${RF}"/heapsort.c "${RF}"/shape3d.c "${RF}"/frame.c \
  "${RF}"/rskybox.c "${RF}"/ringame_text.c "${RF}"/rcrash.c "${RF}"/rexplode.c "${RF}"/rasm_port.c "${RF}"/rdraw_dispatch.c "${RF}"/rframe_helpers.c "${RF}"/rshape2d.c "${RF}"/rtrackprev.c "${RF}"/rcarmenu.c "${RF}"/rreplaybar.c \
  "${RF}"/rfont.c "${RF}"/rhighscore.c "${RF}"/rwidgets.c "${RF}"/rendscreen.c "${RF}"/rdata.c "${RF}"/rblit.c "${RF}"/rfileio.c "${RF}"/rstubs.c \
  "${RF}"/rintro.c "${RF}"/rintro3d.c "${RF}"/rpes.c \
  src/asset/stunts_asset_loader.c src/asset/stunts_dsi_unpack.c \
  src/render/stunts_palette.c \
  src/main_native.c src/audio_native.c src/music_native.c src/music_opl_nuked.c src/vendored/opl/opl3.c \
  -L/opt/homebrew/lib -lSDL2 \
  -o bin/stunts_native

if [ -n "${ASAN:-}" ]; then
  cat <<'NOTE'
NOTE: this is an AddressSanitizer build. Homebrew's libSDL2 is sdl2-compat,
a shim that dlopens libSDL3 at load time, and under ASan that lookup fails -
the process then abort()s in SDL's own dllinit with a "Failed loading SDL3
library" dialog, long before any of our code runs. That is NOT a memory bug.
Always run these builds as:
    DYLD_LIBRARY_PATH=/opt/homebrew/lib ./bin/stunts_native ...
NOTE
fi
echo "Built bin/stunts_native"
