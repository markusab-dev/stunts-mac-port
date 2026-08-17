#!/usr/bin/env bash
# check_map2d.sh - the standing checks for the editor's 2D track map
# (src/render_faithful/rtrackmap2d.c, seg009.asm draw_2DtrackMap).
#
# Four things, each of which a "did a file appear" test would have missed:
#
#  1. every shipped track at three scroll positions draws, and how much ink
#     each map carries;
#  2. a second pass over the warm cache changes no pixels - except the one
#     known case of the loc_2C0CA oddity documented in rtrackmap2d.c;
#  3. no map cell is left blank (a cache bug shows up as holes, and a
#     whole-frame ink count hides them);
#  4. the three window-edge branches - 0xFF, 0xFE and 0xFD, where a
#     multi-tile element's anchor has scrolled out of the window - draw the
#     right quarter of the neighbour's icon, checked against the same icon
#     drawn whole.
#
#   bash tools/check_map2d.sh [data_dir]
set -u
cd "$(dirname "$0")/.."
DATA=${1:-${STUNTS_DATA:-extracted/stunts/stunts}}
S=${RFB_SCALE:-1}
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export STUNTS_MAP2D_CACHECHECK=1
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0
ok()  { printf '  \033[32mok\033[0m    %s\n' "$*"; }
bad() { printf '  \033[31mFEL\033[0m   %s\n' "$*"; FAIL=$((FAIL+1)); }

bash tools/build_map2d_shot.sh >/dev/null 2>&1 || { bad "bygget"; exit 1; }

# ---------------------------------------------------------------- 1 + 2
printf '\n\033[1m2D-kartan: alla banor\033[0m\n'
n=0; broken=""; drifted=""; lowest=999999; lowat=""
for f in "$DATA"/*.TRK; do
	t=$(basename "$f" .TRK)
	for pos in "0 0" "9 9" "18 19"; do
		set -- $pos
		out=$(./bin/map2d_shot "$DATA" "$t" "$TMP/$t-$1-$2.bmp" "$1" "$2" 2>&1)
		ink=$(printf '%s' "$out" | sed -n 's/.*rad [0-9]*, \([0-9]*\) malade.*/\1/p')
		drift=$(printf '%s' "$out" | sed -n 's/.*andrade \([0-9]*\) pixlar.*/\1/p')
		[ -n "$ink" ] || { broken="$broken $t($1,$2)"; continue; }
		n=$((n+1))
		# TRY_IT 18/19 is the one known instance of the loc_2C0CA oddity.
		# Listed so a CHANGE shows up, not because it is right. 174 pixels
		# at RFB_SCALE 1, and the same picture at any other scale.
		want=0; [ "$t $1 $2" = "TRY_IT 18 19" ] && want=$((174 * S * S))
		[ "${drift:-0}" = "$want" ] || drifted="$drifted $t($1,$2):$drift"
		if [ "$ink" -lt "$lowest" ]; then lowest=$ink; lowat="$t $1 $2"; fi
	done
done
[ -z "$broken" ] && ok "kartor $n/117 ritade, minst $lowest malade pixlar ($lowat)" \
                 || bad "kartor $n/117 -$broken"
[ -z "$drifted" ] && ok "omritning over varm cache andrar inga pixlar" \
                  || bad "cache-drift -$drifted"

# ---------------------------------------------------------------- 3
python3 - "$TMP" <<'PY'
import glob, struct, sys
def read(p):
    b=open(p,'rb').read(); off=struct.unpack_from('<I',b,10)[0]
    w,h=struct.unpack_from('<ii',b,18); st=((w*3+3)//4)*4
    return [[b[off+(h-1-y)*st+x*3:off+(h-1-y)*st+x*3+3] for x in range(w)]
            for y in range(h)], w, h
worst, where = 10**9, None
for p in sorted(glob.glob(sys.argv[1] + '/*.bmp')):
    px, w, h = read(p)
    s = w // 320
    for r in range(11):
        rows = (15 if r == 10 else 16) * s          # the window ends at 0xB3
        for c in range(12):
            n = sum(1 for y in range(rows) for x in range(16*s)
                    if px[(r*16+4)*s+y][(c*16+8)*s+x] != b'\x00\x00\x00')
            if n < worst:
                worst, where = n, (p.split('/')[-1], r, c)
print('  \033[32mok\033[0m    minsta ruta %d malade pixlar %s' % (worst, where)
      if worst >= 32 else
      '  \033[31mFEL\033[0m   tom ruta: %d pixlar %s' % (worst, where))
sys.exit(0 if worst >= 32 else 1)
PY
[ $? = 0 ] || FAIL=$((FAIL+1))

# ---------------------------------------------------------------- 4
# HELL5 carries a four-tile element anchored at grid (row 9, col 2).
./bin/map2d_shot "$DATA" HELL5 "$TMP/e_whole.bmp" 2 9  >/dev/null 2>&1
./bin/map2d_shot "$DATA" HELL5 "$TMP/e_ff.bmp"    3 9  >/dev/null 2>&1
./bin/map2d_shot "$DATA" HELL5 "$TMP/e_fe.bmp"    2 10 >/dev/null 2>&1
./bin/map2d_shot "$DATA" HELL5 "$TMP/e_fd.bmp"    3 10 >/dev/null 2>&1
python3 - "$TMP" <<'PY'
import struct, sys
D = sys.argv[1]
def read(p):
    b=open(p,'rb').read(); off=struct.unpack_from('<I',b,10)[0]
    w,h=struct.unpack_from('<ii',b,18); st=((w*3+3)//4)*4
    return [[b[off+(h-1-y)*st+x*3:off+(h-1-y)*st+x*3+3] for x in range(w)]
            for y in range(h)], w
def cell(px, s, r, c):
    return [[px[(r*16+4)*s+y][(c*16+8)*s+x] for x in range(16*s)]
            for y in range(16*s)]
whole, w = read(D + '/e_whole.bmp'); s = w // 320
bad = 0
for name, f, ar, ac in (('0xFF, ankaret till vanster', 'e_ff', 0, 1),
                        ('0xFE, ankaret ovanfor',      'e_fe', 1, 0),
                        ('0xFD, ankaret snett upp',    'e_fd', 1, 1)):
    px, _ = read('%s/%s.bmp' % (D, f))
    want, got = cell(whole, s, ar, ac), cell(px, s, 0, 0)
    diff = sum(1 for y in range(16*s) for x in range(16*s) if want[y][x] != got[y][x])
    ink  = sum(1 for y in range(16*s) for x in range(16*s) if got[y][x] != b'\x00\x00\x00')
    if diff or ink < 64*s*s:
        print('  \033[31mFEL\033[0m   %s: %d pixlar skiljer, %d malade' % (name, diff, ink))
        bad += 1
    else:
        print('  \033[32mok\033[0m    %s: ratt kvart, %d malade pixlar' % (name, ink))
sys.exit(1 if bad else 0)
PY
[ $? = 0 ] || FAIL=$((FAIL+1))

printf '\n'
if [ "$FAIL" = 0 ]; then printf '  \033[32mallt gick igenom\033[0m\n'; exit 0
else printf '  \033[31m%d kontroller misslyckades\033[0m\n' "$FAIL"; exit 1; fi
