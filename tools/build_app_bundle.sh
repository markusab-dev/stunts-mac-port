#!/usr/bin/env bash
# Package the playable port as a double-clickable macOS app.
#
# This builds the SAME code as tools/build_native.sh - the vendored original
# simulation plus the vendored original renderer. It used to build src/sim/ and
# src/render/, the old hand-written approximation, which is not what we ship.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

APP_NAME="Stunts"
BUNDLE_DIR="build/${APP_NAME}.app"
MACOS_DIR="${BUNDLE_DIR}/Contents/MacOS"
RESOURCES_DIR="${BUNDLE_DIR}/Contents/Resources"

echo "Bygger ${BUNDLE_DIR}..."
rm -rf "${BUNDLE_DIR}"
mkdir -p "${MACOS_DIR}" "${RESOURCES_DIR}/data"

echo "  [1/3] kompilerar"
bash tools/build_native.sh >/dev/null
cp bin/stunts_native "${MACOS_DIR}/${APP_NAME}"

echo "  [2/3] Info.plist"
cat > "${BUNDLE_DIR}/Contents/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>Stunts</string>
    <key>CFBundleIdentifier</key>
    <string>com.antigravity.stunts</string>
    <key>CFBundleName</key>
    <string>Stunts</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.1.0-native</string>
    <key>CFBundleVersion</key>
    <string>1.1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

# main_native.c falls back to Contents/Resources/data when the relative default
# does not resolve, which is what happens when Finder launches with cwd = "/".
echo "  [3/3] kopierar speldata"
cp -R extracted/stunts/stunts/* "${RESOURCES_DIR}/data/"

echo "Klart. Starta med: open ${BUNDLE_DIR}"
