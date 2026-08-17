#!/usr/bin/env bash
# Run the behavioural oracle: replay a .RPL through the ORIGINAL 16-bit game
# code under DOSBox and dump one 1120-byte GAMESTATE per frame.
#
#   usage: tools/run_oracle.sh <REPLAYNAME> [rundir]
#   e.g.:  tools/run_oracle.sh DEFAULT
#
# Produces <rundir>/<REPLAYNAME>.BIN:
#     offset 0 : uint16 N   (= gameconfig.game_recordedframes)
#     offset 2 : N * 1120 bytes, one GAMESTATE per simulated frame, in order
#
# Why the polling loop below: repldump.c ends with fatal_error("\nDone.\n"),
# and the game's fatal_error waits for a keypress (seg012.asm "flush_stdin"
# spins while no key is pending).  Unattended, repldumo.exe therefore parks
# there forever.  repldrv.asm writes three unbuffered progress markers to
# stdout; once "file closed OK" appears the DOS file handle has been closed and
# the dump is complete, so DOSBox can be killed safely.  We wait for that
# marker rather than for a wall-clock timeout.
set -euo pipefail

REPLAY="${1:?usage: run_oracle.sh <REPLAYNAME> [rundir]}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNDIR="${2:-${ROOT}/build/oracle_run}"
EXE="${ROOT}/build/oracle_build/link/REPLDUMO.EXE"

[[ -f "${EXE}" ]] || { echo "repldumo.exe not built - run tools/build_oracle.sh first" >&2; exit 1; }

if [[ ! -d "${RUNDIR}" ]]; then
  echo "Creating run directory from build/dos_playable (a writable copy of the game)..."
  mkdir -p "${RUNDIR}"
  cp -R "${ROOT}/build/dos_playable/"* "${RUNDIR}/"
  chmod -R u+w "${RUNDIR}"
fi
cp -f "${EXE}" "${RUNDIR}/REPLDUMO.EXE"

LOG="${RUNDIR}/ORACLE.LOG"
BIN="${RUNDIR}/${REPLAY}.BIN"
rm -f "${LOG}" "${BIN}"

printf '@echo off\r\nrepldumo %s > ORACLE.LOG\r\n' "${REPLAY}" > "${RUNDIR}/oracle.bat"

CONF=$(mktemp /tmp/oracle_run_XXXXXX.conf)
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
mount s "${RUNDIR}"
s:
call s:\\oracle.bat
exit
EOF

echo "Replaying ${REPLAY} through the original game code..."
dosbox-staging -conf "${CONF}" > /tmp/dosbox_oracle_run.log 2>&1 &
DPID=$!

DONE=0
for _ in $(seq 1 900); do
  sleep 1
  if grep -qa "file closed OK" "${LOG}" 2>/dev/null; then DONE=1; break; fi
  kill -0 ${DPID} 2>/dev/null || break
done
pkill -f dosbox-staging 2>/dev/null || true
sleep 1
rm -f "${CONF}"

echo "--- repldumo output ---"
tr -d '\r' < "${LOG}" 2>/dev/null || true
echo "-----------------------"

if [[ ${DONE} -ne 1 ]]; then
  echo "FAILED: repldumo.exe never reported 'file closed OK'; ${REPLAY}.BIN is NOT trustworthy." >&2
  exit 1
fi

SIZE=$(stat -f%z "${BIN}")
N=$(od -An -tu2 -N2 "${BIN}" | tr -d ' \n')
EXPECT=$((2 + 1120 * N))
echo "${REPLAY}.BIN: ${SIZE} bytes, header frame count ${N}, expected $((2 + 1120 * N))"
if [[ "${SIZE}" != "${EXPECT}" ]]; then
  echo "FAILED: size does not match 2 + 1120*N." >&2
  exit 1
fi
echo "OK"
