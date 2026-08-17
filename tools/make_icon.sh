#!/usr/bin/env bash
# Build build/Stunts.icns from the game's own start menu.
#
# The port renders its own menu (STUNTS_MENU_SHOT), which is then scaled with
# NEAREST - the art is 320x200 pixel art and smoothing it looks wrong - and
# cropped to a centred square around the car. So the icon is the real screen,
# not a drawing of it.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
export SDL_VIDEODRIVER=dummy
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
bash tools/build_native.sh >/dev/null
STUNTS_MENU_SHOT="$TMP/menu.bmp" ./bin/stunts_native \
	--data "${STUNTS_DATA:-extracted/stunts/stunts}" >/dev/null 2>&1
rm -rf build/Stunts.iconset; mkdir -p build/Stunts.iconset
python3 - "$TMP/menu.bmp" <<'PYEOF'
from PIL import Image
import sys
im = Image.open(sys.argv[1])
big = im.resize((im.width * 8, im.height * 8), Image.NEAREST)
w, h = big.size
sq = big.crop(((w - h) // 2, 0, (w - h) // 2 + h, h)).resize((1024, 1024), Image.NEAREST)
for sz in (16, 32, 64, 128, 256, 512, 1024):
    f = Image.NEAREST if sz >= 128 else Image.LANCZOS
    sq.resize((sz, sz), f).save(f"build/Stunts.iconset/icon_{sz}x{sz}.png")
    if sz <= 512:
        f2 = Image.NEAREST if sz * 2 >= 128 else Image.LANCZOS
        sq.resize((sz * 2, sz * 2), f2).save(f"build/Stunts.iconset/icon_{sz}x{sz}@2x.png")
PYEOF
iconutil -c icns build/Stunts.iconset -o build/Stunts.icns
echo "build/Stunts.icns klar"
