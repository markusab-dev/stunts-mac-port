#!/usr/bin/env bash
# Build repldumo.exe — the behavioural oracle — inside DOSBox.
#
# repldumo.exe is the restunts "original" target: it links ONLY the
# disassembled original 16-bit assembly (asmorig/), no ported C simulation.
# It replays a .RPL file and writes the 1120-byte GAMESTATE for every frame
# to <replay>.BIN, which is the ground truth our native port is compared to.
#
# Toolchain reality check:
#   * tasmx.exe and tlink.exe are genuine DOS programs and run fine in DOSBox.
#   * bcc.exe does NOT run: it needs 32RTM.EXE, which is missing from the
#     bundled toolchain and cannot legally be obtained.  So repldump.c can
#     never be compiled here.
#   * make.exe and wlink.exe are Win32 binaries and cannot run in DOSBox
#     either, hence the explicit batch files below instead of the makefiles.
#
# Because bcc is unavailable, the single C module of the oracle
# (repldump/repldump.c, function stuntsmain() plus its RESTUNTS_ORIGINAL
# helpers) has been hand-transcribed into TASM assembly as
# asmorig/repldrv.asm.  That module is assembled and linked exactly like the
# 43 original segments, and provides the STUNTSMAIN symbol that seg010.asm's
# CRT startup calls.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD="${ROOT}/build/oracle_build"
ASMDIR="${BUILD}/src/restunts/asmorig"
LINKDIR="${BUILD}/link"

if [[ ! -d "${BUILD}/src/restunts" ]]; then
  echo "Setting up build tree from reference/restunts..."
  mkdir -p "${BUILD}"
  cp -R "${ROOT}/reference/restunts/src" "${BUILD}/"
  cp -R "${ROOT}/reference/restunts/tools" "${BUILD}/"
  chmod -R u+w "${BUILD}"
fi

mkdir -p "${ASMDIR}/build" "${LINKDIR}"

# repldrv.asm is ours, not part of the reference tree.  The tracked master
# copy lives in tools/oracle/ (build/ is .gitignored); copy it into the build
# tree on every run so the two can never drift.
if [[ -f "${ROOT}/tools/oracle/repldrv.asm" ]]; then
  cp -f "${ROOT}/tools/oracle/repldrv.asm" "${ASMDIR}/repldrv.asm"
fi
if [[ ! -f "${ASMDIR}/repldrv.asm" ]]; then
  echo "ERROR: tools/oracle/repldrv.asm is missing." >&2
  echo "It provides stuntsmain(); without it the link fails with" >&2
  echo "  'Undefined symbol STUNTSMAIN in module SEG010.ASM'." >&2
  exit 1
fi

# The 43 original segments, plus our stuntsmain() driver.
ASM_FILES="segments seg000 seg001 seg002 seg003 seg004 seg005 seg006 seg007 \
seg008 seg009 seg010 seg011 seg012 seg013 seg014 seg015 seg016 seg017 seg018 \
seg019 seg020 seg021 seg022 seg023 seg024 seg025 seg026 seg027 seg028 seg029 \
seg030 seg031 seg032 seg033 seg034 seg035 seg036 seg037 seg038 seg039 seg041 \
dseg repldrv"

# CRT/helper objects taken from the bundled Borland library, in the order the
# original makefile's ALL_OBJ_FILES lists them.
LIB_OBJS="getvect labs memcpy fmemcpy h_ldiv f_lxmul f_scopy h_lrsh h_padd \
h_pina h_pada f_pcmp h_lursh h_psbp h_llsh"

# ---- Stage 1 batch: assemble ------------------------------------------------
# tasmx /m2 /s /zn <src>, <obj>   (flags taken verbatim from asmorig/makefile).
# Objects are written straight into the flat link/ directory; DOSBox creates
# them with uppercase names, which is what the linker response file expects.
{
  printf '@echo off\r\n'
  printf 'set PATH=%%PATH%%;s:\\tools\\bin\r\n'
  printf 'cd s:\\src\\restunts\\asmorig\r\n'
  printf 'echo. > s:\\ASMLOG.TXT\r\n'
  for f in ${ASM_FILES}; do
    printf 'tasmx /m2 /s /zn %s.asm, s:\\link\\%s.obj >> s:\\ASMLOG.TXT\r\n' "${f}" "${f}"
  done
  printf 'echo ASMDONE >> s:\\ASMLOG.TXT\r\n'
} > "${BUILD}/build1.bat"

# ---- Linker response file ---------------------------------------------------
# tlink joins object files with '+'.  Run from the flat link/ directory so the
# object names can be bare.  Order follows ALL_OBJ_FILES for TARGET=original:
# asmorig objects, then the stuntsmain() module (where repldump.obj used to
# go), then the CRT objects.
{
  first=1
  for f in ${ASM_FILES} ${LIB_OBJS}; do
    [[ $first -eq 1 ]] && first=0 || printf '+'
    printf '%s' "${f}"
  done
  printf '\r\nrepldumo.exe\r\n'
} > "${LINKDIR}/link.txt"

# ---- Stage 2 batch: link ----------------------------------------------------
{
  printf '@echo off\r\n'
  printf 'set PATH=%%PATH%%;s:\\tools\\bin\r\n'
  printf 'cd s:\\link\r\n'
  printf 'tlink /s /P- @link.txt > s:\\LINKLOG.TXT\r\n'
  printf 'echo LINKDONE >> s:\\LINKLOG.TXT\r\n'
} > "${BUILD}/build2.bat"

# ---- Copy the library objects into the flat link dir ------------------------
for o in ${LIB_OBJS}; do
  src=$(find "${BUILD}/tools/lib" -iname "${o}.obj" | head -1)
  [[ -n "${src}" ]] && cp -f "${src}" "${LINKDIR}/$(basename "${src}")"
done

# ---- Run both stages in one DOSBox session ----------------------------------
rm -f "${BUILD}/ASMLOG.TXT" "${BUILD}/LINKLOG.TXT" "${LINKDIR}/REPLDUMO.EXE"
CONF=$(mktemp /tmp/oracle_build.XXXX.conf)
cat > "${CONF}" <<EOF
[sdl]
output=texture
windowresolution=640x480
autolock=false
[dosbox]
machine=vga
memsize=32
[cpu]
core=auto
cputype=auto
cycles=max
[autoexec]
mount s "${BUILD}"
s:
call s:\\build1.bat
call s:\\build2.bat
exit
EOF

echo "Running DOS toolchain in DOSBox (this takes a couple of minutes)..."
dosbox-staging -conf "${CONF}" > /tmp/dosbox_oracle.log 2>&1 &
DPID=$!
for _ in $(seq 1 300); do sleep 1; kill -0 ${DPID} 2>/dev/null || break; done
pkill -f dosbox-staging 2>/dev/null || true
sleep 1
rm -f "${CONF}"

echo
echo "=== assemble ==="
if grep -qa "ASMDONE" "${BUILD}/ASMLOG.TXT" 2>/dev/null; then
  echo "  stage 1: reached the end"
else
  echo "  stage 1: DID NOT COMPLETE"
fi
grep -ac "Error messages:    None" "${BUILD}/ASMLOG.TXT" 2>/dev/null \
  | xargs echo "  modules assembled without errors:" \
  || echo "  (no ASMLOG.TXT)"
grep -a -A2 -i "^\*\*Error\|Error messages:    [1-9]" "${BUILD}/ASMLOG.TXT" 2>/dev/null || true

echo "=== link ==="
if grep -qa "LINKDONE" "${BUILD}/LINKLOG.TXT" 2>/dev/null; then
  echo "  stage 2: reached the end"
else
  echo "  stage 2: DID NOT COMPLETE"
fi
grep -a -i "error\|undefined" "${BUILD}/LINKLOG.TXT" 2>/dev/null || echo "  no linker errors"

echo "=== result ==="
if [[ -f "${LINKDIR}/REPLDUMO.EXE" ]]; then
  ls -l "${LINKDIR}/REPLDUMO.EXE"
  echo
  echo "To produce an oracle dump:  tools/run_oracle.sh DEFAULT"
  echo "  -> build/oracle_run/DEFAULT.BIN"
  echo "     (2-byte frame count N, then N * 1120-byte GAMESTATE records)"
  echo "  repldumo.exe ends in the game's fatal_error screen, which waits for a"
  echo "  keypress; run_oracle.sh watches for the 'file closed OK' marker on"
  echo "  stdout and stops DOSBox once the dump is closed on disk."
else
  echo "  repldumo.exe NOT produced"
  exit 1
fi
