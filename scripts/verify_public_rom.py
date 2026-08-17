#!/usr/bin/env python3
from pathlib import Path
import sys

sys.path.insert(0, "scripts")
from rom_manifest import ROM_SIZES, marker_for

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_public_rom.py <z64>")

p = Path(sys.argv[1])
data = p.read_bytes()

if b"DragonFS 2.0" not in data:
    raise SystemExit("FAIL: DragonFS signature not found in final ROM")

offsets = {}
for name, size in sorted(ROM_SIZES.items()):
    marker = marker_for(name, size)
    pos = data.find(marker)
    if pos < 0:
        raise SystemExit(f"FAIL: placeholder marker missing: {name}")
    if data.find(marker, pos + 1) >= 0:
        raise SystemExit(f"FAIL: duplicate placeholder marker: {name}")
    if pos < 0x101000:
        raise SystemExit(
            f"FAIL: {name} starts at 0x{pos:X}, inside N64 CRC-covered region"
        )
    offsets[name] = pos

print("Public ROM layout verified")
print(f"ROM size: {len(data):,} bytes")
print(f"First patch slot: 0x{min(offsets.values()):08X}")
print(f"Last patch slot:  0x{max(offsets.values()):08X}")
print("All 31 patch slots are outside the N64 header CRC-covered region.")
