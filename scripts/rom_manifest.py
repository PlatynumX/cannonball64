#!/usr/bin/env python3

# Filename/size metadata for the OutRun Rev B set.
# No commercial ROM data is contained here.
ROM_SIZES = {
    "epr-10187.88":   32768,
    "epr-10327a.76":  65536,
    "epr-10328a.75":  65536,
    "epr-10329a.58":  65536,
    "epr-10330a.57":  65536,
    "epr-10380b.133": 65536,
    "epr-10381b.132": 65536,
    "epr-10382b.118": 65536,
    "epr-10383b.117": 65536,

    "mpr-10371.9":    131072,
    "mpr-10372.13":   131072,
    "mpr-10373.10":   131072,
    "mpr-10374.14":   131072,
    "mpr-10375.11":   131072,
    "mpr-10376.15":   131072,
    "mpr-10377.12":   131072,
    "mpr-10378.16":   131072,

    "opr-10185.11":   32768,
    "opr-10186.47":   32768,

    "opr-10188.71":   32768,
    "opr-10189.70":   32768,
    "opr-10190.69":   32768,
    "opr-10191.68":   32768,
    "opr-10192.67":   32768,
    "opr-10193.66":   32768,

    "opr-10230.104":  32768,
    "opr-10231.103":  32768,
    "opr-10232.102":  32768,
    "opr-10266.101":  32768,
    "opr-10267.100":  32768,
    "opr-10268.99":   32768,
}

MARKER_SIZE = 128
MARKER_PREFIX = "CB64_ROM_PLACEHOLDER::"

def marker_for(name: str, size: int) -> bytes:
    raw = f"{MARKER_PREFIX}{name}::SIZE={size}::".encode("ascii")
    if len(raw) > MARKER_SIZE:
        raise ValueError(name)
    return raw + (b"\x5A" * (MARKER_SIZE - len(raw)))
