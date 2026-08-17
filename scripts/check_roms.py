#!/usr/bin/env python3
from pathlib import Path
import sys, zlib

# Cannonball Rev B / MAME "outrun" parent set.
# epr-10381a.132 and epr-10381b.132 carry the same expected CRC; current
# MAME names it "b", while some Cannonball builds also accept the older name.
REQUIRED = {
    "epr-10380b.133": 0x1f6cadad,
    "epr-10382b.118": 0xc4c3fa1a,
    "epr-10383b.117": 0x10a2014a,
    "epr-10327a.76":  0xe28a5baf,
    "epr-10329a.58":  0xda131c81,
    "epr-10328a.75":  0xd5ec5e5d,
    "epr-10330a.57":  0xba9ec82a,

    "opr-10268.99":   0x95344b04,
    "opr-10232.102":  0x776ba1eb,
    "opr-10267.100":  0xa85bb823,
    "opr-10231.103":  0x8908bcbf,
    "opr-10266.101":  0x9f6f1a74,
    "opr-10230.104":  0x686f5e50,

    "mpr-10371.9":    0x7cc86208,
    "mpr-10373.10":   0xb0d26ac9,
    "mpr-10375.11":   0x59b60bd7,
    "mpr-10377.12":   0x17a1b04a,
    "mpr-10372.13":   0xb557078c,
    "mpr-10374.14":   0x8051e517,
    "mpr-10376.15":   0xf3b8f318,
    "mpr-10378.16":   0xa1062984,

    "opr-10186.47":   0x22794426,
    "opr-10185.11":   0x22794426,

    "epr-10187.88":   0xa10abaa9,

    "opr-10193.66":   0xbcd10dde,
    "opr-10192.67":   0x770f1270,
    "opr-10191.68":   0x20a284ab,
    "opr-10190.69":   0x7cab70e2,
    "opr-10189.70":   0x01366b54,
    "opr-10188.71":   0xbad30ad9,
}

MASTER_ALT = {
    "epr-10381b.132": 0xbe8c412b,
    "epr-10381a.132": 0xbe8c412b,
}

def crc32(path: Path) -> int:
    crc = 0
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xffffffff

def main():
    if len(sys.argv) != 2:
        print("Usage: check_roms.py <directory-containing-uncompressed-ROMs>")
        return 2

    base = Path(sys.argv[1])
    if not base.is_dir():
        print(f"Not a directory: {base}")
        return 2

    failures = 0

    for name, expected in REQUIRED.items():
        p = base / name
        if not p.exists():
            print(f"MISSING  {name}")
            failures += 1
            continue
        got = crc32(p)
        if got != expected:
            print(f"BAD CRC  {name}: got {got:08x}, expected {expected:08x}")
            failures += 1
        else:
            print(f"OK       {name}  {got:08x}")

    alt_ok = False
    for name, expected in MASTER_ALT.items():
        p = base / name
        if p.exists():
            got = crc32(p)
            if got == expected:
                print(f"OK       {name}  {got:08x}")
                alt_ok = True
                break
            else:
                print(f"BAD CRC  {name}: got {got:08x}, expected {expected:08x}")
    if not alt_ok:
        print("MISSING  epr-10381b.132 (or compatible epr-10381a.132)")
        failures += 1

    if failures:
        print(f"\nFAIL: {failures} required ROM item(s) missing or incorrect.")
        return 1

    print("\nPASS: complete Cannonball OutRun Revision B ROM set.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
