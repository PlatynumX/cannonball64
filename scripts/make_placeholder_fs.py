#!/usr/bin/env python3
from pathlib import Path
import shutil
from rom_manifest import ROM_SIZES, marker_for

root = Path("filesystem")
game = root / "cannonball"

if root.exists():
    shutil.rmtree(root)

game.mkdir(parents=True)

# Keep every byte changed by the local injector outside the N64 CRC1/CRC2
# region. This file sorts before the cannonball directory, so mkdfs allocates
# it first.
guard_size = 2 * 1024 * 1024
guard_prefix = b"CB64 PUBLIC PLACEHOLDER CRC GUARD\n"
guard = root / "000_crc_guard.bin"
guard.write_bytes(guard_prefix + b"\xA5" * (guard_size - len(guard_prefix)))

for name, size in sorted(ROM_SIZES.items()):
    marker = marker_for(name, size)
    filler = bytes([(sum(name.encode("ascii")) & 0x7F) | 0x80])
    (game / name).write_bytes(marker + filler * (size - len(marker)))

print(f"Created {len(ROM_SIZES)} public placeholder ROM slots")
print(f"Placeholder game bytes: {sum(ROM_SIZES.values()):,}")
print(f"CRC guard bytes: {guard.stat().st_size:,}")
