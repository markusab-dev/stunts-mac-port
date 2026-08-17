#!/bin/bash
# verify.sh - the standing verification bar for the port.
#
# Every check here exists because a bug got past the previous set. Run it
# before claiming anything works. It takes a few minutes and needs no mouse.
#
#   bash tools/verify.sh            everything
#   bash tools/verify.sh fysik      just the DOS oracle
#   bash tools/verify.sh skarmar    just the render hooks
#
# The seed matters: the game draws its opponent's start offsets from a random
# source, and the oracle capture was made with this one.
set -u
cd "$(dirname "$0")/.."

# Render offscreen. Every check here that draws would otherwise open a real
# window and steal focus, which is intolerable when someone is working at the
# machine - and it happened. Verified byte-identical output: the dummy driver
# produces the same BMP as a real window, so nothing is lost.
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}

DATA=${STUNTS_DATA:-extracted/stunts/stunts}
SEED="29,178,77,215,110,1"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0
WHICH=${1:-allt}

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32mok\033[0m    %s\n' "$*"; }
bad()  { printf '  \033[31mFEL\033[0m   %s\n' "$*"; FAIL=$((FAIL+1)); }

# Clear leftovers from a previous run of THIS tree. `pkill -9 -f intro_shot`
# used to be enough and is not: -f matches the whole command line, and every
# checkout on the machine runs the binary as ./bin/intro_shot, so a verify.sh
# in one worktree killed the binaries a verify.sh in another was in the middle
# of using. That produced a red "intro: titelbild" with a SIGKILLed process and
# nothing wrong with the code. Match on the process's working directory
# instead, which is the one thing that does tell the trees apart.
kill_ours() {
	local p
	for p in $(pgrep -f "bin/$1" 2>/dev/null); do
		[ "$(lsof -a -p "$p" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')" \
		  = "$PWD" ] && kill -9 "$p" 2>/dev/null
	done
	return 0
}
kill_ours stunts_native
kill_ours intro_shot

# ------------------------------------------------------------------ #
say "Bygger"
for s in build_native.sh build_dumper.sh build_intro.sh build_replaybar_shot.sh; do
	[ -f "tools/$s" ] || continue
	if out=$(bash "tools/$s" 2>&1); then ok "$s"
	else bad "$s"; echo "$out" | grep -i " error" | head -5; fi
done
[ -x bin/stunts_native ] || { bad "bin/stunts_native saknas"; exit 1; }

# ------------------------------------------------------------------ #
if [ "$WHICH" = allt ] || [ "$WHICH" = fysik ]; then
say "Fysik mot DOS-facit"
# All six must be 16/16 and 100.00%. PIPEFLIP was the last open deviation
# (frame 423, the uninitialised var_140someWhlData slot - see
# docs/PIPEFLIP_DIVERGENCE.md); it now matches like the rest.
for t in TUBETEST HILLTEST PIPEROLL T_HELL2 T_HELL4 PIPEFLIP; do
	[ -f "build/oracle_run/$t.BIN" ] || { printf '  --    %s (inget facit)\n' "$t"; continue; }
	STUNTS_KEVINSEED="$SEED" ./bin/dump_native_states \
		build/oracle_run "build/oracle_run/$t.RPL" "$TMP/$t.bin" >/dev/null 2>&1
	res=$(python3 tools/diff_oracle.py "build/oracle_run/$t.BIN" "$TMP/$t.bin" 2>&1 |
	      awk '/byte-över/{b=$NF} /identiska hela/{n++} END{printf "%d/16 %s", n, b}')
	want="16/16 100.00%"
	if [ "$res" = "$want" ]; then ok "$t  $res"; else bad "$t  $res (vantat $want)"; fi
done
fi

# ------------------------------------------------------------------ #
if [ "$WHICH" = allt ] || [ "$WHICH" = koming ]; then
say "Allt går att köra"
n=0; miss=""
for f in "$DATA"/*.TRK; do
	t=$(basename "$f" .TRK)
	./bin/stunts_native --data "$DATA" --track "$t" --car coun --headless 60 \
		>/dev/null 2>&1 && n=$((n+1)) || miss="$miss $t"
done
[ -z "$miss" ] && ok "banor $n/39" || bad "banor $n/39 -$miss"

n=0; miss=""
for f in tests/replays/*.rpl; do
	./bin/stunts_native --data "$DATA" --replay "$f" --headless 400 \
		>/dev/null 2>&1 && n=$((n+1)) || miss="$miss $(basename "$f")"
done
[ -z "$miss" ] && ok "inspelningar $n/12" || bad "inspelningar $n/12 -$miss"

CARS="ansx audi coun fgto jagu lanc lm02 p962 pc04 pmin vett"
n=0; miss=""
for c in $CARS; do
	./bin/stunts_native --data "$DATA" --track DEFAULT --car $c --headless 40 \
		>/dev/null 2>&1 && n=$((n+1)) || miss="$miss $c"
done
[ -z "$miss" ] && ok "bilar $n/11" || bad "bilar $n/11 -$miss"

# The interactive path. Both bugs the user hit in August 2026 lived here and
# neither could be reached by anything above: picking an opponent whose car
# matches the player's took a different branch of shape3d_load_car_shapes and
# aborted before the first frame. Every pairing, not just a sample.
n=0; miss=""
for c in $CARS; do
	./bin/stunts_native --data "$DATA" --track DEFAULT --car coun \
		--opponent 1 --opponent-car $c --headless 30 >/dev/null 2>&1 \
		&& n=$((n+1)) || miss="$miss coun/$c"
	./bin/stunts_native --data "$DATA" --track DEFAULT --car $c \
		--opponent 2 --opponent-car $c --headless 30 >/dev/null 2>&1 \
		&& n=$((n+1)) || miss="$miss $c/$c"
done
[ -z "$miss" ] && ok "motstandarbilar $n/22" || bad "motstandarbilar $n/22 -$miss"

n=0; miss=""
for o in 1 2 3 4 5 6; do
	./bin/stunts_native --data "$DATA" --track DEFAULT --car coun \
		--opponent $o --headless 40 >/dev/null 2>&1 && n=$((n+1)) || miss="$miss $o"
done
[ -z "$miss" ] && ok "motstandare $n/6" || bad "motstandare $n/6 -$miss"

# Phase 9: rewinding. update_gamestate stores a 1120-byte snapshot every
# word_45A00 frames; restore_gamestate has to put one back so exactly that
# the simulation carries on identically - the RNG seed rides inside the
# snapshot, which is what makes that possible at all.
n=0; miss=""
# Frame 0 is deliberately excluded: seg001:4296 resets the game there
# (init_game_state) instead of restoring, so "identical to the frame-0
# snapshot" is the wrong thing to ask for. 600 and 1200 are real
# snapshot boundaries mid-race.
for fr in 600 1200; do
	STUNTS_REWIND=$fr ./bin/stunts_native --data "$DATA" \
		--replay tests/replays/00_default_hell5_full.rpl --headless 1 \
		>/dev/null 2>&1 && n=$((n+1)) || miss="$miss ruta$fr"
done
[ -z "$miss" ] && ok "spolning $n/2 bit-identiska" || bad "spolning -$miss"

# Save/load a replay. Nothing covered this before: the physics oracle uses its
# own .RPL path and the screen checks only prove a strip was drawn. It matters
# because the format was wrong for a long time - a .RPL is 26 bytes of
# GAMEINFO, then the WHOLE 1802-byte track, then one input byte per frame, and
# this loader used to read inputs at offset 26 and feed the car 1802 bytes of
# track map as steering.
src=tests/replays/05_lm02_offroad_grass.rpl
if [ -f "$src" ]; then
	python3 - "$src" "$DATA" <<'PYEOF' && ok "inspelningsformat: huvud + bana + indata" \
	                              || bad "inspelningsformat stammer inte"
import sys, os
rpl, data = sys.argv[1], sys.argv[2]
b = open(rpl, 'rb').read()
frames = b[24] | (b[25] << 8)
name = b[13:22].split(b'\0')[0].decode('latin1').strip()
trk = os.path.join(data, name + '.TRK')
# A port-made replay is 26+frames; a DOS one is 26+1802+frames. Both must
# parse, and a DOS one must carry its track verbatim.
if len(b) == 26 + frames:
    sys.exit(0)                      # port-made, no track block
if len(b) != 26 + 1802 + frames:
    print(f"oväntad längd {len(b)}, väntade {26+1802+frames}"); sys.exit(1)
if not os.path.exists(trk):
    sys.exit(0)
if b[26:26+1802] != open(trk, 'rb').read():
    print("banblocket matchar inte " + trk); sys.exit(1)
sys.exit(0)
PYEOF
else
	bad "inspelningsformat (saknar $src)"
fi
fi

# ------------------------------------------------------------------ #
if [ "$WHICH" = allt ] || [ "$WHICH" = pixlar ]; then
say "Inga omålade pixlar"
n=0; miss=""
for f in tests/replays/*.rpl; do
	c=$(./bin/stunts_native --data "$DATA" --replay "$f" --paint-check 2>&1 |
	    grep -c "OMÅLAT")
	[ "$c" = 0 ] && n=$((n+1)) || miss="$miss $(basename "$f" .rpl):$c"
done
[ -z "$miss" ] && ok "inspelningar $n/12 helt målade" || bad "omålat -$miss"
fi

# ------------------------------------------------------------------ #
if [ "$WHICH" = allt ] || [ "$WHICH" = skarmar ]; then
say "Varje skärm ritar en bild"
# A hook that renders nothing is the failure this catches; looking at the
# pictures is still a separate, manual step.
shot() {  # shot <namn> <miljovariabel> [extra argument...]
	local name=$1 var=$2; shift 2
	rm -f "$TMP/s.bmp"
	env "$var=$TMP/s.bmp" ./bin/stunts_native --data "$DATA" "$@" >/dev/null 2>&1
	if [ -s "$TMP/s.bmp" ]; then ok "$name"; else bad "$name (ingen bild)"; fi
}
shot "huvudmeny"      STUNTS_MENU_SHOT
shot "banval"         STUNTS_TRACK_SHOT
shot "bilval"         STUNTS_CAR_SHOT
shot "motstandarval"  STUNTS_OPP_SHOT
shot "installningar"  STUNTS_OPTION_SHOT
shot "rekordlista"    STUNTS_HISCORE_SHOT
shot "resultatskarm"  STUNTS_ENDSCREEN_SHOT
# Phase 11. Each of these four is a screen that arrived as its own port and is
# now drawn by bin/stunts_native itself, which is the thing worth checking: the
# sibling harnesses in tools/ prove the code, these prove the wiring.
shot "inspelningslist" STUNTS_REPLAYBAR_SHOT
shot "banredigerare"   STUNTS_EDITOR_SHOT
shot "2D-karta"        STUNTS_MAP2D_SHOT
shot "styrspakskal"    STUNTS_JOYCAL_SHOT
# STUNTS_ICONS_SHOT writes a directory, not one file, so it cannot use shot().
rm -rf "$TMP/ic"; mkdir -p "$TMP/ic"
STUNTS_ICONS_SHOT="$TMP/ic" ./bin/stunts_native --data "$DATA" >/dev/null 2>&1
n=$(ls "$TMP/ic" 2>/dev/null | wc -l | tr -d " ")
# eleven pages plus the contact sheet
[ "$n" = 12 ] && ok "ikonpalett 11 sidor + kontaktkarta" \
              || bad "ikonpalett ($n filer, vantat 12)"
# These two are not paths - they set a value in the car's state (the crash
# bitmap flag, and the explosion's fixed-point scale), so they render through
# --shots like an ordinary frame.
effect() {  # effect <namn> <miljovariabel> <varde>
	local name=$1 var=$2 val=$3
	rm -rf "$TMP/e"; mkdir -p "$TMP/e"
	env "$var=$val" ./bin/stunts_native --data "$DATA" --track DEFAULT \
		--car coun --headless 12 --shots "$TMP/e" --shot-from 10 \
		--shot-step 99 >/dev/null 2>&1
	if [ -s "$TMP/e/f00010.bmp" ]; then ok "$name"; else bad "$name (ingen bild)"; fi
}
effect "sprucken ruta" STUNTS_CRASH   3
effect "explosion"     STUNTS_EXPLODE 512
for g in 0 1 2 3 4 5; do
	rm -f "$TMP/s.bmp"; mkdir -p "$TMP/g"
	STUNTS_GEAR=$g ./bin/stunts_native --data "$DATA" --track DEFAULT --car coun \
		--headless 12 --shots "$TMP/g" --shot-from 10 --shot-step 99 >/dev/null 2>&1
	[ -s "$TMP/g/f00010.bmp" ] || bad "vaxelgrind lage $g"
	rm -f "$TMP/g/f00010.bmp"
done
ok "vaxelgrind 6 lagen"

# Phase 7: the 3D track preview, on three landscapes so the horizon varies.
for t in DEFAULT HELL5 CTRACK04; do
	rm -f "$TMP/p.bmp"
	STUNTS_PREVIEW_SHOT="$TMP/p.bmp" ./bin/stunts_native --data "$DATA" \
		--track $t --car coun --headless 1 >/dev/null 2>&1
	[ -s "$TMP/p.bmp" ] || bad "banforhandsvisning $t"
done
ok "banforhandsvisning 3 banor"

# Phase 7: the car picker's showroom - eight turntable angles and the
# acceleration graph, which is a real simulation run, not a stored curve.
rm -rf "$TMP/sr"; mkdir -p "$TMP/sr"
out=$(STUNTS_SHOWROOM_SHOT="$TMP/sr" ./bin/stunts_native --data "$DATA" \
	--track DEFAULT --car coun --headless 1 2>&1 | grep -o "grafen gav [0-9]* punkter")
n=$(ls "$TMP/sr" 2>/dev/null | wc -l | tr -d " ")
# A black frame is the failure this catches: transformed_shape_op only queues
# polygons, and without get_a_poly_info the car renders as nothing at all.
ink=$(python3 - "$TMP/sr/turn0.bmp" <<'PYEOF'
import sys,struct
b=open(sys.argv[1],'rb').read()
off=struct.unpack_from('<I',b,10)[0]; w,h=struct.unpack_from('<ii',b,18)
row=((w*3+3)//4)*4
print(sum(1 for y in range(h) for x in range(w)
          if b[off+y*row+x*3:off+y*row+x*3+3]!=b'\x00\x00\x00'))
PYEOF
)
if [ "$n" = 8 ] && [ "${ink:-0}" -gt 500 ]; then ok "visningsrum 8 vinklar, $ink pixlar bil, $out"
else bad "visningsrum ($n rutor, ${ink:-0} pixlar)"; fi

# Phase 7: the graphics sub-screen, and proof its five detail levels actually
# change the picture - a settings screen that stores a number nothing reads
# would pass a "did it draw" check.
rm -f "$TMP/g.bmp"
STUNTS_GRAPHICS_SHOT="$TMP/g.bmp" ./bin/stunts_native --data "$DATA" >/dev/null 2>&1
[ -s "$TMP/g.bmp" ] && ok "grafikmeny" || bad "grafikmeny (ingen bild)"
same=0
for d in 0 1 2 3 4; do
	rm -rf "$TMP/d"; mkdir -p "$TMP/d"
	STUNTS_DETAIL=$d ./bin/stunts_native --data "$DATA" --track DEFAULT \
		--car coun --headless 32 --shots "$TMP/d" --shot-from 30 \
		--shot-step 99 >/dev/null 2>&1
	[ -s "$TMP/d/f00030.bmp" ] || { bad "detaljniva $d ritade inget"; continue; }
	cp "$TMP/d/f00030.bmp" "$TMP/det$d.bmp"
done
cmp -s "$TMP/det0.bmp" "$TMP/det4.bmp" && same=1
[ "$same" = 0 ] && ok "detaljnivaer 0 och 4 ger olika bild" \
                || bad "detaljnivan andrar ingenting"

if [ -x bin/intro_shot ]; then
	for pair in "brodrbund STUNTS_INTRO_PROD_SHOT" "titelbild STUNTS_INTRO_TITL_SHOT" \
	            "eftertexter STUNTS_CREDITS_SHOT"; do
		set -- $pair
		rm -f "$TMP/s.bmp"
		env "$2=$TMP/s.bmp" ./bin/intro_shot --data "$DATA" >/dev/null 2>&1
		[ -s "$TMP/s.bmp" ] && ok "intro: $1" || bad "intro: $1 (ingen bild)"
	done
	rm -rf "$TMP/3d"; mkdir -p "$TMP/3d"
	STUNTS_INTRO3D_SHOTS="$TMP/3d" STUNTS_INTRO3D_STEP=40 \
		./bin/intro_shot --data "$DATA" >/dev/null 2>&1
	c=$(ls "$TMP/3d" 2>/dev/null | wc -l | tr -d ' ')
	[ "$c" -gt 0 ] && ok "intro: 3D-animation ($c rutor)" || bad "intro: 3D-animation tom"
fi
fi

# ------------------------------------------------------------------ #
# Phase 11 brought three sub-bars of its own, each measuring pixels rather
# than file existence. They build their own binaries.
if [ "$WHICH" = allt ] || [ "$WHICH" = fas11 ]; then
say "Fas 11: egna kontroller"
if ./bin/replaybar_shot --smoke >"$TMP/rb.log" 2>&1
	then ok "inspelningslistens 18 matningar"
	else bad "replaybar_shot --smoke"; tail -5 "$TMP/rb.log"; fi
for c in check_map2d.sh check_editor.sh; do
	if bash "tools/$c" >"$TMP/$c.log" 2>&1
		then ok "$c"
		else bad "$c"; grep -a FEL "$TMP/$c.log" | head -5; fi
done
fi

# ------------------------------------------------------------------ #
say "Sammanfattning"
if [ "$FAIL" = 0 ]; then printf '  \033[32mallt gick igenom\033[0m\n'; exit 0
else printf '  \033[31m%d kontroller misslyckades\033[0m\n' "$FAIL"; exit 1; fi
