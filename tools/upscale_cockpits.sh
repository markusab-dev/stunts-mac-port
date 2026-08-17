#!/usr/bin/env bash
# Build the high-resolution cockpit artwork for every car.
#
# For each car: export the shapes from its own STDA/STDB archives, upscale them
# 4x with Real-ESRGAN (via the model set Upscayl ships), then blend the result
# back against the original so that the printed digits, tick marks and needles
# come from the untouched art while the surfaces come from the model. See
# tools/blend_cockpit.py for why that split exists.
#
#   usage: tools/upscale_cockpits.sh [car ...]      (default: all eleven)
#
# Output: assets/cockpit_hd/<car>/<tag>.bmp, which the game loads on its own.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BIN=/Applications/Upscayl.app/Contents/Resources/bin/upscayl-bin
MODELS=/Applications/Upscayl.app/Contents/Resources/models
MODEL=${MODEL:-ultrasharp-4x}
DATA=extracted/stunts/stunts
GAME=bin/stunts_native

if [ ! -x "$BIN" ]; then
	echo "hittar inte $BIN - installera med: brew install --cask upscayl" >&2
	exit 1
fi
[ -x "$GAME" ] || { echo "bygg forst: bash tools/build_native.sh" >&2; exit 1; }

CARS=("$@")
if [ ${#CARS[@]} -eq 0 ]; then
	CARS=(ansx audi coun fgto jagu lanc lm02 p962 pc04 pmin vett)
fi

WORK=build/hd_work
mkdir -p "$WORK" assets/cockpit_hd

for car in "${CARS[@]}"; do
	echo "=== $car ==="
	raw="$WORK/$car/raw"; png="$WORK/$car/png"; up="$WORK/$car/up"; ai="$WORK/$car/ai"
	rm -rf "$WORK/$car"; mkdir -p "$raw" "$png" "$up" "$ai"

	# 1. the game writes out its own artwork, already unflipped
	"$GAME" --data "$DATA" --track DEFAULT --car "$car" --headless 2 \
	        --export-cockpit "$raw" >/dev/null 2>&1
	n=$(ls "$raw"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
	if [ "$n" = "0" ]; then echo "  inga former exporterade, hoppar over"; continue; fi

	# 2. 4x through the model
	for b in "$raw"/*.bmp; do
		t=$(basename "$b" .bmp)
		sips -s format png "$b" --out "$png/$t.png" >/dev/null 2>&1
		"$BIN" -i "$png/$t.png" -o "$up/$t.png" -s 4 -m "$MODELS" -n "$MODEL" >/dev/null 2>&1
		sips -s format bmp "$up/$t.png" --out "$ai/$t.bmp" >/dev/null 2>&1
	done

	# 3. keep the model's surfaces, keep the original's lettering
	python3 tools/blend_cockpit.py "$raw" "$ai" "assets/cockpit_hd/$car" | tail -2
done

echo
echo "klart. Spelet laddar assets/cockpit_hd/<bil>/ automatiskt;"
echo "--no-hd stanger av det, --import-cockpit <mapp> valjer en annan uppsattning."
