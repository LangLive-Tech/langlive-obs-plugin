#!/usr/bin/env bash
# Builds the macOS installer: release/<name>-macos-universal-v<version>.pkg
#
# Usage:
#   ./scripts/macos/package-macos-installer.sh            # Release build
#   ./scripts/macos/package-macos-installer.sh RelWithDebInfo
#
# Needs a configured build_macos (cmake --preset macos). The package is
# unsigned unless MACOS_CODESIGN_IDENTITY / MACOS_INSTALLER_IDENTITY are set
# (CI release builds: scripts/ci/macos-signing.sh, then
# scripts/ci/notarize-macos.sh).

set -euo pipefail

CONFIG="${1:-Release}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT/build_macos"
STAGE="$BUILD_DIR/package-stage"
OUTPUT_DIR="$ROOT/release"

if [ ! -d "$BUILD_DIR" ]; then
	echo "error: $BUILD_DIR not found. Run 'cmake --preset macos' first." >&2
	exit 1
fi

cmake --build "$BUILD_DIR" --config "$CONFIG"

rm -rf "$STAGE"
cmake --install "$BUILD_DIR" --config "$CONFIG" --prefix "$STAGE"

PKG="$(find "$STAGE" -maxdepth 1 -name '*.pkg' -print -quit)"
if [ -z "$PKG" ]; then
	echo "error: no .pkg was produced in $STAGE" >&2
	exit 1
fi

mkdir -p "$OUTPUT_DIR"
mv -f "$PKG" "$OUTPUT_DIR/"
rm -rf "$STAGE"
echo "Installer written to $OUTPUT_DIR/$(basename "$PKG")"
