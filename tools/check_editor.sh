#!/usr/bin/env bash
# check_editor.sh - the Phase 11 verification bar for the track editor.
#
# Three things, each with a number rather than "a file appeared":
#   1. every shipped .TRK gets track_setup's "Track ok." verdict, and the
#      editor's own validator (sub_2C81C) leaves it alone;
#   2. every one of the eleven palette pages renders ink;
#   3. the 1802-byte load/save round trip is byte-identical, the multi-tile
#      continuations land, and all fifteen verdict texts resolve.
#
#   bash tools/check_editor.sh
set -u
cd "$(dirname "$0")/.."
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
DATA=${STUNTS_DATA:-extracted/stunts/stunts}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0

ok()  { printf '  \033[32mok\033[0m    %s\n' "$*"; }
bad() { printf '  \033[31mFEL\033[0m   %s\n' "$*"; FAIL=$((FAIL+1)); }

bash tools/build_editor.sh >/dev/null 2>&1 || { bad "bygget"; exit 1; }

printf '\n\033[1mAlla banor godkanns av validatorn\033[0m\n'
n=0; miss=""
for f in "$DATA"/*.TRK; do
	t=$(basename "$f" .TRK)
	r=$(./bin/editor_shot --data "$DATA" --track "$t" --check 2>&1 | grep track_setup)
	case "$r" in
	*"(eok)"*) n=$((n+1)) ;;
	*)         miss="$miss $t" ;;
	esac
done
tot=$(ls "$DATA"/*.TRK | wc -l | tr -d ' ')
[ -z "$miss" ] && ok "banor $n/$tot \"Track ok.\"" || bad "banor $n/$tot -$miss"

printf '\n\033[1mVarje palettsida ritar\033[0m\n'
n=0; miss=""
for p in 0 1 2 3 4 5 6 7 8 9 10; do
	rm -f "$TMP/p.bmp"
	STUNTS_EDITOR_SHOT="$TMP/p.bmp" ./bin/editor_shot --data "$DATA" \
		--track DEFAULT --page $p >/dev/null 2>&1
	ink=$(python3 - "$TMP/p.bmp" <<'PYEOF'
import sys, struct
b = open(sys.argv[1], 'rb').read()
off = struct.unpack_from('<I', b, 10)[0]
w, h = struct.unpack_from('<ii', b, 18)
row = ((w * 3 + 3) // 4) * 4
# count non-black pixels inside the palette box only: x 220..316, y 36..132
print(sum(1 for y in range(36, 132) for x in range(220, 316)
          if b[off + (h - 1 - y) * row + x * 3: off + (h - 1 - y) * row + x * 3 + 3]
             != b'\x00\x00\x00'))
PYEOF
)
	if [ "${ink:-0}" -gt 4000 ]; then n=$((n+1)); else miss="$miss sida$p:${ink:-0}"; fi
done
[ -z "$miss" ] && ok "palettsidor $n/11 ritade" || bad "palettsidor -$miss"

printf '\n\033[1mModellen\033[0m\n'
if ./bin/editor_shot --data "$DATA" --track DEFAULT --selftest > "$TMP/st.txt" 2>&1; then
	grep -c '^ok ' "$TMP/st.txt" | { read c; ok "sjalvtest, $c kontroller"; }
else
	bad "sjalvtest"; cat "$TMP/st.txt"
fi
grep -q '\[91 255 / 254 253\]' "$TMP/st.txt" \
	&& ok "2x2-element ger 91/FF/FE/FD" || bad "fortsattningsbyte"
grep -q 'utfall 14 eat "No Objects' "$TMP/st.txt" \
	&& ok "alla 15 utfallstexter loses upp" || bad "utfallstabellen"

printf '\n\033[1mDialogen\033[0m\n'
d=$(./bin/editor_shot --data "$DATA" --dialog mss 2>&1)
echo "$d" | grep -q '6 knappar' && ok "mss: 6 knappar staplade" || bad "mss"
d=$(./bin/editor_shot --data "$DATA" --dialog chl 2>&1)
echo "$d" | grep -q '2 knappar' && ok "chl: 2 knappar" || bad "chl"
echo "$d" | grep -q 'knapp 0: x 56..142 y 103..113' \
	&& echo "$d" | grep -q 'knapp 1: x 142..' \
	&& ok "chl: knapparna delar rad (\"[Save First[Load\")" \
	|| bad "chl-layouten"

printf '\n\033[1mSammanfattning\033[0m\n'
if [ "$FAIL" = 0 ]; then printf '  \033[32mallt gick igenom\033[0m\n'; exit 0
else printf '  \033[31m%d kontroller misslyckades\033[0m\n' "$FAIL"; exit 1; fi
