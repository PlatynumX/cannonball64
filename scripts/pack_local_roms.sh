#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

cd "$HOME/cannonball64"

OUT="${1:-$HOME/storage/downloads/cannonball64-self-contained.z64}"
TMP="$HOME/.cache/cannonball64-local-pack"
DFS_OFFSET=$((0x00400000))

echo "== Cannonball64 no-pkg local packer =="
echo "No Sega ROM data is uploaded to GitHub."

for tool in python3 unzip gh git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required existing tool is missing: $tool"
        exit 1
    fi
done

rm -rf "$TMP"
mkdir -p "$TMP"

echo
echo "[1/6] Finding and validating OutRun Rev B..."
FOUND=""
for ZIP in "$HOME"/storage/downloads/*.zip; do
    [ -f "$ZIP" ] || continue

    TEST="$TMP/romtest"
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
    echo "ERROR: no complete validated OutRun Rev B ZIP found in Downloads."
    exit 1
fi

echo "Using: $FOUND"

echo
echo "[2/6] Downloading latest successful public Cannonball64 build..."
RUN_ID="$(gh run list \
    --workflow=build.yml \
    --status success \
    --limit 1 \
    --json databaseId \
    -q '.[0].databaseId')"

if [ -z "$RUN_ID" ] || [ "$RUN_ID" = "null" ]; then
    echo "ERROR: no successful GitHub build found."
    exit 1
fi

mkdir -p "$TMP/artifact"
gh run download "$RUN_ID" \
    -n cannonball64-r2-full-port \
    -D "$TMP/artifact"

BASE="$(find "$TMP/artifact" -type f -name '*.z64' | head -n 1)"
if [ -z "$BASE" ]; then
    echo "ERROR: downloaded artifact contains no .z64."
    exit 1
fi

BASE_SIZE="$(stat -c %s "$BASE")"
echo "Run:       $RUN_ID"
echo "Base ROM:  $BASE"
echo "Base size: $BASE_SIZE bytes"

if [ "$BASE_SIZE" -gt "$DFS_OFFSET" ]; then
    echo "ERROR: base ROM is larger than fixed 4 MiB embed offset."
    exit 1
fi

echo
echo "[3/6] Preparing local embedded filesystem..."
FSROOT="$TMP/filesystem"
mkdir -p "$FSROOT/cannonball"
unzip -qq "$FOUND" -d "$FSROOT/cannonball"
python3 scripts/check_roms.py "$FSROOT/cannonball"

# Runtime resources are open-source Cannonball data. Fetch locally.
git clone --depth 1 \
    https://github.com/libretro/cannonball.git \
    "$TMP/core" >/dev/null 2>&1

if [ -d "$TMP/core/res" ]; then
    cp -a "$TMP/core/res" "$FSROOT/cannonball/res"
fi

echo
echo "[4/6] Building DragonFS with Python..."
python3 scripts/mkdfs_py.py \
    "$TMP/cannonball.dfs" \
    "$FSROOT"

DFS_SIZE="$(stat -c %s "$TMP/cannonball.dfs")"

echo
echo "[5/6] Creating self-contained N64 ROM..."
python3 - "$BASE" "$TMP/cannonball.dfs" "$OUT" "$DFS_OFFSET" <<'PYEOF'
from pathlib import Path
import sys

base = Path(sys.argv[1]).read_bytes()
dfs = Path(sys.argv[2]).read_bytes()
out = Path(sys.argv[3])
offset = int(sys.argv[4])

if len(base) > offset:
    raise SystemExit(
        f"base ROM is {len(base)} bytes, larger than embed offset {offset}"
    )

# Place DragonFS at the exact file offset expected by the N64 frontend.
packed = base + (b"\xFF" * (offset - len(base))) + dfs
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(packed)

print(f"Base ROM:   {len(base):,} bytes")
print(f"DFS offset: 0x{offset:08X}")
print(f"DragonFS:   {len(dfs):,} bytes")
print(f"Final ROM:  {len(packed):,} bytes")
PYEOF

echo
echo "[6/6] SUCCESS"
echo
echo "Self-contained ROM:"
echo "  $OUT"
echo
echo "This file contains your local OutRun ROM data."
echo "Do not commit it to the public GitHub repository."
