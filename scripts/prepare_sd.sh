#!/usr/bin/env bash
set -euo pipefail

ROMDIR="${1:-}"
OUT="${2:-sdcard/cannonball}"

if [[ -z "$ROMDIR" || ! -d "$ROMDIR" ]]; then
    echo "Usage: $0 <uncompressed-outrun-rom-directory> [output-dir]" >&2
    exit 2
fi

python3 scripts/check_roms.py "$ROMDIR"

mkdir -p "$OUT"
cp -a "$ROMDIR"/. "$OUT"/

# Cannonball resources are useful for optional frontend/widescreen/custom data.
if [[ -d vendor/cannonball/res ]]; then
    mkdir -p "$OUT/res"
    cp -a vendor/cannonball/res/. "$OUT/res"/
fi

echo
echo "Prepared SD folder: $OUT"
echo "Copy the 'cannonball' directory to the root of your flashcart SD card."
