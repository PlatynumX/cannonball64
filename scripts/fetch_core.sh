#!/usr/bin/env bash
set -euo pipefail

REF="${CANNONBALL_REF:-master}"
DEST="vendor/cannonball"

rm -rf "$DEST"
mkdir -p vendor

echo "Fetching libretro/cannonball ref: $REF"
git clone --depth 1 --branch "$REF" https://github.com/libretro/cannonball.git "$DEST"

echo "Core commit:"
git -C "$DEST" rev-parse HEAD

# Some libretro-common copies may be a submodule depending on upstream revision.
git -C "$DEST" submodule update --init --recursive --depth 1 || true

if [[ ! -f "$DEST/Makefile.common" ]]; then
    echo "ERROR: Makefile.common missing after checkout" >&2
    exit 1
fi

echo "Applying Cannonball64 RDP core patch"
python3 scripts/patch_core_rdp.py "$DEST"
