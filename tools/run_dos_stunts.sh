#!/usr/bin/env bash
# Runner script for executing original DOS Stunts or test tools under DOSBox on macOS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
GAME_DIR="${WORKSPACE_ROOT}/extracted/stunts/stunts"
CONF_FILE="${SCRIPT_DIR}/dosbox_stunts.conf"

if [[ ! -d "${GAME_DIR}" ]]; then
    echo "Error: Extracted game directory not found at ${GAME_DIR}"
    echo "Run 'python3 tools/inventory.py' first to extract original files."
    exit 1
fi

# Detect available emulator
EMULATOR=""
if command -v dosbox-staging &>/dev/null; then
    EMULATOR="dosbox-staging"
elif command -v dosbox-x &>/dev/null; then
    EMULATOR="dosbox-x"
elif command -v dosbox &>/dev/null; then
    EMULATOR="dosbox"
else
    echo "Error: No DOSBox emulator found. Install with 'brew install dosbox-staging' or 'brew install dosbox-x'."
    exit 1
fi

CMD_TO_RUN="${1:-STUNTS.COM}"
EXTRA_ARGS="${*:2}"

echo "============================================================"
echo " Launching DOS Stunts via ${EMULATOR}"
echo " Game Directory: ${GAME_DIR}"
echo " Command:        ${CMD_TO_RUN} ${EXTRA_ARGS}"
echo "============================================================"

# Create temporary autoexec commands
AUTOEXEC_CMDS=(
    "-c" "mount c \"${GAME_DIR}\""
    "-c" "c:"
    "-c" "${CMD_TO_RUN} ${EXTRA_ARGS}"
)

if [[ "${EMULATOR}" == "dosbox-staging" ]]; then
    "${EMULATOR}" -conf "${CONF_FILE}" "${AUTOEXEC_CMDS[@]}"
else
    "${EMULATOR}" -conf "${CONF_FILE}" "${AUTOEXEC_CMDS[@]}"
fi
