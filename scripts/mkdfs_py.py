#!/usr/bin/env python3
"""
Small, dependency-free DragonFS 2.0 image builder.

Implements the filesystem format used by libdragon's mkdfs:
- 256-byte sectors
- big-endian directory metadata
- sorted directory traversal
- DFS 2.0 hash lookup table

This is intended for Cannonball64's local end-user ROM packing step.
"""
from __future__ import annotations

import argparse
import os
import stat
import struct
from dataclasses import dataclass
from pathlib import Path

SECTOR_SIZE = 256
MAX_FILENAME_LEN = 243

ROOT_FLAGS = 0xFFFFFFFF
ROOT_NEXT_ENTRY = 0xDEADBEEF
ROOT_PATH = b"DragonFS 2.0"

FLAGS_FILE = 0x0
FLAGS_DIR = 0x1

DFS_LOOKUP_PRIME = 31


@dataclass
class LookupFile:
    path: str
    path_hash: int
    data_ofs: int
    data_len: int


class Builder:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.buf = bytearray()
        self.files: list[LookupFile] = []

    @staticmethod
    def round_sector(n: int) -> int:
        return ((n + SECTOR_SIZE - 1) // SECTOR_SIZE) * SECTOR_SIZE

    def alloc(self, size: int) -> int:
        rsize = self.round_sector(size)
        if rsize == 0:
            raise ValueError("DragonFS does not support empty allocations")
        ofs = len(self.buf)
        self.buf.extend(b"\0" * rsize)
        return ofs

    def new_sector(self) -> int:
        return self.alloc(SECTOR_SIZE)

    def new_blob(self, size: int) -> int:
        if size <= 0:
            raise ValueError("DragonFS does not support empty files")
        return self.alloc(size)

    @staticmethod
    def hash_path(s: str) -> int:
        h = 0
        for b in s.encode("utf-8"):
            h = ((h * DFS_LOOKUP_PRIME) + b) & 0xFFFFFFFF
        return h

    def put_u32(self, ofs: int, value: int) -> None:
        self.buf[ofs:ofs + 4] = struct.pack(">I", value & 0xFFFFFFFF)

    def write_dirent(self, ofs: int, *, next_entry: int = 0,
                     flags: int = 0, name: str = "", file_pointer: int = 0) -> None:
        raw_name = name.encode("utf-8")
        if len(raw_name) > MAX_FILENAME_LEN:
            raise ValueError(f"filename too long for DragonFS: {name!r}")

        self.put_u32(ofs + 0, next_entry)
        self.put_u32(ofs + 4, flags)

        self.buf[ofs + 8:ofs + 8 + 244] = b"\0" * 244
        self.buf[ofs + 8:ofs + 8 + len(raw_name)] = raw_name

        self.put_u32(ofs + 252, file_pointer)

    def add_file_data(self, path: Path) -> tuple[int, int]:
        data = path.read_bytes()
        if not data:
            raise ValueError(f"DragonFS cannot store empty file: {path}")

        if len(data) > 0x0FFFFFFF:
            raise ValueError(f"file too large for DragonFS: {path}")

        ofs = self.new_blob(len(data))
        self.buf[ofs:ofs + len(data)] = data
        return ofs, len(data)

    def relative_lookup_path(self, path: Path) -> str:
        return path.resolve().relative_to(self.root).as_posix()

    def add_directory(self, path: Path) -> int:
        entries = sorted(path.iterdir(), key=lambda p: str(p))

        first_entry = 0
        cur_entry = 0

        for item in entries:
            st = item.stat()

            if stat.S_ISREG(st.st_mode):
                entry_ofs = self.new_sector()
                data_ofs, data_len = self.add_file_data(item)

                self.write_dirent(
                    entry_ofs,
                    next_entry=0,
                    flags=((FLAGS_FILE << 28) | (data_len & 0x0FFFFFFF)),
                    name=item.name,
                    file_pointer=data_ofs,
                )

                rel = self.relative_lookup_path(item)
                self.files.append(
                    LookupFile(
                        path=rel,
                        path_hash=self.hash_path(rel),
                        data_ofs=data_ofs,
                        data_len=data_len,
                    )
                )

            elif stat.S_ISDIR(st.st_mode):
                entry_ofs = self.new_sector()
                child_first = self.add_directory(item)

                # Match mkdfs: empty directories are skipped. The already
                # allocated entry sector simply remains unused.
                if child_first == 0:
                    continue

                self.write_dirent(
                    entry_ofs,
                    next_entry=0,
                    flags=(FLAGS_DIR << 28),
                    name=item.name,
                    file_pointer=child_first,
                )
            else:
                continue

            if cur_entry:
                self.put_u32(cur_entry + 0, entry_ofs)

            cur_entry = entry_ofs
            if not first_entry:
                first_entry = cur_entry

        return first_entry

    def write_lookup(self) -> None:
        # C mkdfs sorts by the hash before writing the lookup.
        self.files.sort(key=lambda f: f.path_hash)

        num_files = len(self.files)
        lookup_size = 8 + (16 * num_files)
        lookup_ofs = self.new_blob(lookup_size)

        # Root sector's next_entry holds actual lookup structure size;
        # file_pointer holds its filesystem-relative offset.
        self.put_u32(0, lookup_size)
        self.put_u32(252, lookup_ofs)

        path_size = 0
        for f in self.files:
            n = len(f.path.encode("utf-8")) + 1
            path_size += n
            if n & 1:
                path_size += 1

        path_ofs = self.new_blob(path_size)

        self.put_u32(lookup_ofs + 0, num_files)
        self.put_u32(lookup_ofs + 4, path_ofs)

        cursor = 0
        path_cursor = 0

        for i, f in enumerate(self.files):
            encoded = f.path.encode("utf-8")
            strlen_nul = len(encoded) + 1

            ent = lookup_ofs + 8 + (i * 16)
            self.put_u32(ent + 0, f.path_hash)
            self.put_u32(ent + 4, (strlen_nul << 20) | path_cursor)
            self.put_u32(ent + 8, f.data_ofs)
            self.put_u32(ent + 12, f.data_len)

            self.buf[path_ofs + path_cursor:
                     path_ofs + path_cursor + len(encoded)] = encoded
            self.buf[path_ofs + path_cursor + len(encoded)] = 0

            path_cursor += strlen_nul
            if strlen_nul & 1:
                path_cursor += 1

    def build(self) -> bytes:
        # Root identification sector.
        root = self.new_sector()
        assert root == 0

        self.write_dirent(
            0,
            next_entry=ROOT_NEXT_ENTRY,
            flags=ROOT_FLAGS,
            name=ROOT_PATH.decode("ascii"),
            file_pointer=0,
        )

        first = self.add_directory(self.root)
        if not first:
            raise ValueError(f"filesystem root is empty: {self.root}")

        # mkdfs relies on the first root directory entry living at sector 1.
        if first != SECTOR_SIZE:
            raise AssertionError(
                f"unexpected first root entry offset {first}; expected {SECTOR_SIZE}"
            )

        self.write_lookup()
        return bytes(self.buf)


def validate_image(data: bytes) -> None:
    if len(data) < 512 or len(data) % SECTOR_SIZE:
        raise ValueError("invalid DragonFS image size")

    next_entry, flags = struct.unpack(">II", data[:8])
    path = data[8:252].split(b"\0", 1)[0]
    lookup_ofs = struct.unpack(">I", data[252:256])[0]

    if flags != ROOT_FLAGS:
        raise ValueError("bad DragonFS root flags")
    if path != ROOT_PATH:
        raise ValueError("bad DragonFS root signature")
    if lookup_ofs <= 0 or lookup_ofs >= len(data):
        raise ValueError("bad DragonFS lookup offset")

    num_files, path_ofs = struct.unpack(
        ">II", data[lookup_ofs:lookup_ofs + 8]
    )

    if num_files <= 0:
        raise ValueError("DragonFS contains no files")
    if path_ofs <= lookup_ofs or path_ofs >= len(data):
        raise ValueError("bad DragonFS path table")

    expected_lookup_size = 8 + 16 * num_files
    if next_entry != expected_lookup_size:
        raise ValueError("DragonFS lookup size mismatch")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("output")
    ap.add_argument("directory")
    ns = ap.parse_args()

    root = Path(ns.directory)
    if not root.is_dir():
        raise SystemExit(f"not a directory: {root}")

    builder = Builder(root)
    data = builder.build()
    validate_image(data)

    out = Path(ns.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)

    print(f"DragonFS: {out}")
    print(f"Files:    {len(builder.files)}")
    print(f"Bytes:    {len(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
