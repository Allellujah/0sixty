#!/usr/bin/env bash
# Produces a single flashable image (bootloader + partition table + OTA data
# + app, at their real offsets) so the device can be reflashed with one
# esptool command and no PlatformIO install -- same pattern as the single
# merged "flasherProxy" image this device was originally flashed with (see
# README.md's "Restoring Bruce" section / project_cardputer_lora.md memory).
#
# Usage: tools/merge_bin.sh [build_dir]
#   build_dir defaults to .pio/build/cardputer (set PLATFORMIO_BUILD_DIR to
#   the same value you built with if you used a custom one, e.g. to work
#   around slow builds on a network-mounted project directory).
#
# Offsets/flash params below were captured verbatim from a real `pio run -t
# upload -v` on this exact board (m5stack-stamps3 / ESP32-S3, 8MB flash,
# dio/80m) -- don't guess these from the datasheet, PlatformIO's own default
# partition scheme picks them per-board and they're easy to get wrong.
set -euo pipefail

BUILD_DIR="${1:-.pio/build/cardputer}"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
# Version comes from include/config.h's FIRMWARE_VERSION -- the single
# source of truth (project rule, 2026-09-17: every .bin gets a version in
# its filename). Run this from the repo root so the relative path resolves.
VERSION=$(grep -oP 'FIRMWARE_VERSION = "\K[^"]+' include/config.h)
OUT="0sixty-v${VERSION}-merged.bin"

for f in "$BUILD_DIR/bootloader.bin" "$BUILD_DIR/partitions.bin" "$BUILD_DIR/firmware.bin" "$BOOT_APP0"; do
	[ -f "$f" ] || { echo "Missing $f -- run 'pio run -e cardputer' first" >&2; exit 1; }
done

"$HOME/.platformio/penv/bin/python" "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" \
	--chip esp32s3 merge_bin -o "$OUT" \
	--flash_mode dio --flash_freq 80m --flash_size 8MB \
	0x0000 "$BUILD_DIR/bootloader.bin" \
	0x8000 "$BUILD_DIR/partitions.bin" \
	0xe000 "$BOOT_APP0" \
	0x10000 "$BUILD_DIR/firmware.bin"

echo "Wrote $OUT -- flash with:"
echo "  esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 $OUT"
