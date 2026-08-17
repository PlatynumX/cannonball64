#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

cd "$HOME/cannonball64"

OUT="${1:-$HOME/storage/downloads/cannonball64-self-contained.z64}"
TMP="$HOME/.cache/cannonball64-r5-pack"

for x in gh unzip python3; do
    command -v "$x" >/dev/null 2>&1 || {
        echo "ERROR: required existing tool missing: $x"
        exit 1
    }
done

rm -rf "$TMP"
mkdir -p "$TMP"

echo "== Cannonball64 r5 local ROM injector =="
echo "This never uploads Sega ROM data."

echo
echo "[1/4] Finding your validated OutRun Rev B archive..."
FOUND=""
for ZIP in "$HOME"/storage/downloads/*.zip; do
    [ -f "$ZIP" ] || continue
    TEST="$TMP/test"
    rm -rf "$TEST"
    mkdir -p "$TEST"
    if unzip -qq "$ZIP" -d "$TEST" 2>/dev/null; then
        if python3 scripts/check_roms.py "$TEST" >/dev/null 2>&1; then
            FOUND="$ZIP"
            break
        fi
    fi
done

if [ -z "$FOUND" ]; then
    echo "ERROR: complete validated Rev B set not found in Android Downloads."
    exit 1
fi
echo "Using: $FOUND"

ROMDIR="$TMP/roms"
mkdir -p "$ROMDIR"
unzip -qq "$FOUND" -d "$ROMDIR"
python3 scripts/check_roms.py "$ROMDIR"

echo
echo "[2/4] Downloading latest successful public Cannonball64 build..."
RUN_ID="$(gh run list \
    --workflow=build.yml \
    --status success \
    --limit 1 \
    --json databaseId \
    -q '.[0].databaseId')"

[ -n "$RUN_ID" ] && [ "$RUN_ID" != "null" ] || {
    echo "ERROR: no successful build found."
    exit 1
}

mkdir -p "$TMP/artifact"
gh run download "$RUN_ID" \
    -n cannonball64-r2-full-port \
    -D "$TMP/artifact"

BASE="$(find "$TMP/artifact" -type f -name '*.z64' | head -n1)"
[ -n "$BASE" ] || {
    echo "ERROR: artifact has no .z64"
    exit 1
}

echo "Run: $RUN_ID"
echo "Base: $BASE"

echo
echo "[3/4] Replacing the public placeholder slots locally..."
python3 - "$BASE" "$ROMDIR" "$OUT" <<'PYEOF'
from pathlib import Path
import sys

sys.path.insert(0, "scripts")
from rom_manifest import ROM_SIZES, marker_for

base_path = Path(sys.argv[1])
romdir = Path(sys.argv[2])
out = Path(sys.argv[3])

image = bytearray(base_path.read_bytes())
patched = []

for name, expected_size in sorted(ROM_SIZES.items()):
    source = romdir / name
    if not source.is_file():
        raise SystemExit(f"missing validated ROM after extraction: {name}")

    payload = source.read_bytes()
    if len(payload) != expected_size:
        raise SystemExit(
            f"wrong size for {name}: {len(payload)} != {expected_size}"
        )

    marker = marker_for(name, expected_size)
    pos = image.find(marker)

    if pos < 0:
        raise SystemExit(
            f"placeholder for {name} not found. "
            "The latest successful GitHub artifact is probably an older build. "
            "Wait for the r5 workflow to succeed, then rerun this script."
        )

    if image.find(marker, pos + 1) >= 0:
        raise SystemExit(f"duplicate placeholder for {name}")

    # CRC1/CRC2 cover 0x1000..0x101000. CI guarantees every replacement
    # slot is beyond this range, so local injection cannot invalidate them.
    if pos < 0x101000:
        raise SystemExit(
            f"unsafe layout: {name} is at 0x{pos:X}, inside CRC region"
        )

    image[pos:pos + expected_size] = payload
    patched.append((name, pos, expected_size))

for name, expected_size in ROM_SIZES.items():
    if marker_for(name, expected_size) in image:
        raise SystemExit(f"placeholder remained after patch: {name}")

out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(image)

print(f"Patched {len(patched)} ROM files")
print(f"Final size: {len(image):,} bytes")
print(f"First patched offset: 0x{min(x[1] for x in patched):08X}")
print(f"Last patched offset:  0x{max(x[1] for x in patched):08X}")
print(f"Written: {out}")
PYEOF

echo
echo "[4/4] SUCCESS"
echo "Self-contained ROM:"
echo "  $OUT"
echo
echo "No ROM bytes were sent to GitHub."
