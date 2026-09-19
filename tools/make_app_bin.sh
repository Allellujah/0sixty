#!/usr/bin/env bash
# Produces the plain app-only .bin (for Launcher's SD/WebUI sideload method,
# as opposed to merge_bin.sh's single merged image for a direct esptool
# flash) with the version baked into its filename -- project rule,
# 2026-09-17: every compiled .bin gets a version in its name.
#
# Usage: tools/make_app_bin.sh [build_dir]
#   build_dir defaults to .pio/build/cardputer (set PLATFORMIO_BUILD_DIR to
#   the same value you built with if you used a custom one).
set -euo pipefail

BUILD_DIR="${1:-.pio/build/cardputer}"
VERSION=$(grep -oP 'FIRMWARE_VERSION = "\K[^"]+' include/config.h)
OUT="0sixty-v${VERSION}-app.bin"

[ -f "$BUILD_DIR/firmware.bin" ] || { echo "Missing $BUILD_DIR/firmware.bin -- run 'pio run -e cardputer' first" >&2; exit 1; }

cp "$BUILD_DIR/firmware.bin" "$OUT"
echo "Wrote $OUT"
