"""Texture bank (`.TXB`) reader for Sonic the Hedgehog 4 Episode II.

A TXB is the index that maps a stage's texture slots to the `.DDS` files sitting
beside it in the same AMB archive. Unlike AMB, **TXB is big-endian** — a legacy
of the SEGA NN library's console origins.

    0x00  char[4]  '#TXB'
    0x04  u32      header size / version (0x10 in every observed file)
    0x08  u32      reserved
    0x0C  u32      reserved
    0x10  u32      entry count
    0x14  u32      entry table offset

    entry table: count * 20 bytes
        +0x00  u32  runtime slot, always 0 on disk
        +0x04  u32  absolute offset of the NUL-terminated texture name
        +0x08  u16  unknown (1 in every observed file)
        +0x0A  u16  unknown (1 in every observed file)
        +0x0C  8 bytes, zero on disk

The string table begins immediately after the entry table.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402

MAGIC = b"#TXB"
ENTRY_SIZE = 20


class TxbError(Exception):
    pass


class TextureRef:
    __slots__ = ("index", "name", "flag_a", "flag_b")

    def __init__(self, index: int, name: str, flag_a: int, flag_b: int):
        self.index, self.name, self.flag_a, self.flag_b = index, name, flag_a, flag_b

    def __repr__(self):
        return f"<TextureRef {self.index} {self.name!r}>"


def parse(data: bytes) -> list[TextureRef]:
    if len(data) < 0x18 or data[:4] != MAGIC:
        raise TxbError(f"not a TXB (magic={data[:4]!r})")
    count, table = struct.unpack_from(">II", data, 0x10)
    if table + count * ENTRY_SIZE > len(data):
        raise TxbError(f"entry table ({count} x {ENTRY_SIZE} @ {table}) overruns {len(data)}")

    refs = []
    for i in range(count):
        base = table + i * ENTRY_SIZE
        _slot, name_off = struct.unpack_from(">II", data, base)
        flag_a, flag_b = struct.unpack_from(">HH", data, base + 8)
        if not 0 <= name_off < len(data):
            raise TxbError(f"entry {i} name offset {name_off} out of range")
        end = data.find(b"\0", name_off)
        if end == -1:
            end = len(data)
        refs.append(TextureRef(i, data[name_off:end].decode("ascii", "replace"), flag_a, flag_b))
    return refs


def basename(name: str) -> str:
    return name.replace(chr(92), "/").rsplit("/", 1)[-1]


def cmd_list(args) -> int:
    archive = amb.load(args.archive)
    banks = [e for e in archive if e.name.upper().endswith(".TXB")]
    if not banks:
        print("no .TXB in this archive", file=sys.stderr)
        return 1
    dds = {basename(e.name).upper() for e in archive if e.name.upper().endswith(".DDS")}
    for entry in banks:
        refs = parse(archive.read(entry))
        print(f"{basename(entry.name)}: {len(refs)} texture slots")
        missing = 0
        for ref in refs:
            here = basename(ref.name).upper() in dds
            missing += not here
            mark = "" if here else "   <- not in this archive"
            if args.verbose or not here:
                print(f"  {ref.index:4d}  {ref.name}{mark}")
        print(f"  {len(refs) - missing}/{len(refs)} resolve to a DDS in the same archive")
    return 0


def cmd_verify(args) -> int:
    total = ok = 0
    problems: list[str] = []
    for path in amb._iter_amb_files(args.root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        banks = [e for e in archive if e.name.upper().endswith(".TXB")]
        if not banks:
            continue
        dds = {basename(e.name).upper() for e in archive if e.name.upper().endswith(".DDS")}
        for entry in banks:
            total += 1
            data = archive.read(entry)
            try:
                refs = parse(data)
            except TxbError as exc:
                problems.append(f"{os.path.basename(path)}: {exc}")
                continue
            count, table = struct.unpack_from(">II", data, 0x10)
            # The string table must start exactly where the entry table ends.
            first_name = struct.unpack_from(">I", data, table + 4)[0] if count else None
            contiguous = count == 0 or first_name == table + count * ENTRY_SIZE
            names = {basename(r.name).upper() for r in refs}
            unresolved = names - dds if dds else set()
            if contiguous and not unresolved:
                ok += 1
            else:
                problems.append(
                    f"{os.path.basename(path)}::{basename(entry.name)} "
                    f"contiguous={contiguous} unresolved={len(unresolved)}/{len(names)}"
                )
    print(f"{ok}/{total} texture banks parse and resolve against sibling DDS entries")
    for p in problems[:12]:
        print(f"  ! {p}")
    if len(problems) > 12:
        print(f"  ... and {len(problems) - 12} more")
    return 1 if problems else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Texture bank tool (Sonic 4 Episode II)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list", help="list the texture slots of an archive's bank")
    p.add_argument("archive")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("verify", help="parse every bank under a directory tree")
    p.add_argument("root")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
