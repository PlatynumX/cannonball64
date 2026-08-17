#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

cd "$HOME/cannonball64"
OUT="${1:-$HOME/storage/downloads/cannonball64-self-contained.z64}"
TMP="$HOME/.cache/cannonball64-local-pack"
DFS_OFFSET=$((0x00400000))

echo "== Cannonball64 local ROM packer =="
echo "No Sega ROM data will be uploaded to GitHub."
pkg install -y clang git unzip python >/dev/null
rm -rf "$TMP"
mkdir -p "$TMP"

echo "[1/6] Finding validated OutRun Rev B ZIP..."
FOUND=""
for ZIP in "$HOME"/storage/downloads/*.zip; do
  [ -f "$ZIP" ] || continue
  TEST="$TMP/romtest"; rm -rf "$TEST"; mkdir -p "$TEST"
  if unzip -qq "$ZIP" -d "$TEST" 2>/dev/null; then
    if python3 scripts/check_roms.py "$TEST" >/dev/null 2>&1; then FOUND="$ZIP"; break; fi
  fi
done
if [ -z "$FOUND" ]; then echo "ERROR: no complete validated Rev B OutRun ZIP found in Android Downloads."; exit 1; fi
echo "Using: $FOUND"

echo "[2/6] Downloading latest successful public Cannonball64 artifact..."
RUN_ID="$(gh run list --workflow=build.yml --status success --limit 1 --json databaseId -q '.[0].databaseId')"
if [ -z "$RUN_ID" ] || [ "$RUN_ID" = "null" ]; then echo "ERROR: no successful GitHub build found."; exit 1; fi
mkdir -p "$TMP/artifact"
gh run download "$RUN_ID" -n cannonball64-r2-full-port -D "$TMP/artifact"
BASE="$(find "$TMP/artifact" -type f -name '*.z64' | head -n 1)"
if [ -z "$BASE" ]; then echo "ERROR: no .z64 found in artifact."; exit 1; fi
echo "Base ROM: $BASE"
echo "Base size: $(stat -c %s "$BASE") bytes"

echo "[3/6] Preparing local DragonFS payload..."
FSROOT="$TMP/filesystem"
mkdir -p "$FSROOT/cannonball"
unzip -qq "$FOUND" -d "$FSROOT/cannonball"
python3 scripts/check_roms.py "$FSROOT/cannonball"
git clone --depth 1 https://github.com/libretro/cannonball.git "$TMP/core" >/dev/null 2>&1
if [ -d "$TMP/core/res" ]; then cp -a "$TMP/core/res" "$FSROOT/cannonball/res"; fi

echo "[4/6] Building libdragon mkdfs natively in Termux..."
LIBD="$HOME/.cache/libdragon-host-src"
if [ ! -d "$LIBD/.git" ]; then
  rm -rf "$LIBD"
  git clone --depth 1 --branch trunk https://github.com/DragonMinded/libdragon.git "$LIBD" >/dev/null 2>&1
else
  git -C "$LIBD" fetch --depth 1 origin trunk >/dev/null 2>&1
  git -C "$LIBD" reset --hard FETCH_HEAD >/dev/null 2>&1
fi
clang -O2 -std=gnu17 -I"$LIBD/include" -I"$LIBD/src" "$LIBD/tools/mkdfs/mkdfs.c" -o "$TMP/mkdfs"
"$TMP/mkdfs" "$TMP/cannonball.dfs" "$FSROOT" >/dev/null
echo "DragonFS size: $(stat -c %s "$TMP/cannonball.dfs") bytes"

echo "[5/6] Appending payload locally at fixed 4 MiB ROM offset..."
python3 - "$BASE" "$TMP/cannonball.dfs" "$OUT" "$DFS_OFFSET" <<'PY'
from pathlib import Path
import sys
base = Path(sys.argv[1]).read_bytes()
dfs = Path(sys.argv[2]).read_bytes()
out = Path(sys.argv[3])
offset = int(sys.argv[4])
if len(base) > offset:
    raise SystemExit(f"ERROR: base ROM is {len(base)} bytes, larger than fixed {offset}-byte embed offset.")
packed = base + (b"\xFF" * (offset - len(base))) + dfs
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(packed)
print(f"Base:       {len(base):,} bytes")
print(f"DFS offset: 0x{offset:08X}")
print(f"DFS:        {len(dfs):,} bytes")
print(f"Final ROM:  {len(packed):,} bytes")
print(f"Written:    {out}")
PY

echo "[6/6] Done."
echo "SELF-CONTAINED ROM: $OUT"
echo "Do NOT commit or upload that generated .z64 to the public repository."
