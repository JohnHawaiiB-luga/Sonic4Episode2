"""AMB archive reader for Sonic the Hedgehog 4 Episode II (AliceNN engine).

Format recovered from the Episode I decompilation's amFs implementation
(Sonic4Episode1/AppMain/Am/AmFs.cs, readAmbHeader) and verified against the
Episode II Beta 8 data set.

Layout, little-endian:

    0x00  char[4]  '#AMB'
    0x04  u32      version/flags (unused by the reader)
    0x08  u32      reserved
    0x0C  u32      reserved
    0x10  s32      file_num
    0x14  s32      entry_table_offset
    0x18  u32      reserved
    0x1C  s32      string_table_offset (0 => entries are unnamed)

    entry table:  file_num * 0x10 bytes -> { s32 offset, s32 length, u64 pad }
    string table: file_num * 0x20 bytes -> NUL-terminated name, zero padded

Entries whose name ends in .amb are themselves AMB archives, nested by value.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass, field

MAGIC = b"#AMB"
ENTRY_SIZE = 0x10
NAME_SIZE = 0x20


class AmbError(Exception):
    pass


@dataclass
class AmbEntry:
    index: int
    name: str
    offset: int
    length: int

    @property
    def is_archive(self) -> bool:
        return self.name.lower().endswith(".amb")


@dataclass
class AmbArchive:
    entries: list[AmbEntry] = field(default_factory=list)
    _data: bytes = b""

    def __len__(self) -> int:
        return len(self.entries)

    def __iter__(self):
        return iter(self.entries)

    def read(self, entry: AmbEntry) -> bytes:
        return self._data[entry.offset : entry.offset + entry.length]

    def find(self, name: str) -> AmbEntry | None:
        lowered = name.lower()
        for entry in self.entries:
            if entry.name.lower() == lowered:
                return entry
        return None

    def open_nested(self, entry: AmbEntry) -> "AmbArchive":
        return parse(self.read(entry))


def _cstring(data: bytes) -> str:
    end = data.find(b"\0")
    if end != -1:
        data = data[:end]
    return data.decode("ascii", errors="replace")


def parse(data: bytes) -> AmbArchive:
    """Parse an AMB archive from an in-memory buffer."""
    if len(data) < 0x20 or data[:4] != MAGIC:
        raise AmbError(f"not an AMB archive (magic={data[:4]!r})")

    file_num, entry_table = struct.unpack_from("<ii", data, 0x10)
    (string_table,) = struct.unpack_from("<i", data, 0x1C)

    if file_num < 0 or entry_table < 0:
        raise AmbError(f"corrupt header (file_num={file_num}, entries@{entry_table})")
    if entry_table + file_num * ENTRY_SIZE > len(data):
        raise AmbError("entry table overruns buffer")

    archive = AmbArchive(_data=data)
    for i in range(file_num):
        offset, length = struct.unpack_from("<ii", data, entry_table + i * ENTRY_SIZE)
        name = ""
        if string_table:
            name = _cstring(data[string_table + i * NAME_SIZE :][:NAME_SIZE])
        # Map archives carry a string table whose name slots are mostly blank;
        # fall back to the index so every entry stays addressable and distinct.
        archive.entries.append(AmbEntry(i, name or str(i), offset, length))
    return archive


def load(path: str) -> AmbArchive:
    with open(path, "rb") as fp:
        return parse(fp.read())


def validate(archive: AmbArchive, size: int) -> list[str]:
    """Return a list of structural problems; empty means self-consistent."""
    problems = []
    for entry in archive:
        if entry.offset < 0 or entry.length < 0:
            problems.append(f"[{entry.index}] {entry.name}: negative offset/length")
        elif entry.offset + entry.length > size:
            problems.append(
                f"[{entry.index}] {entry.name}: extends past end of file "
                f"({entry.offset}+{entry.length} > {size})"
            )
    return problems


def _safe_join(root: str, name: str) -> str:
    """Join guarding against absolute paths and traversal in archive names."""
    cleaned = name.replace("\\", "/").lstrip("/")
    parts = [p for p in cleaned.split("/") if p not in ("", ".", "..")]
    if not parts:
        parts = ["unnamed"]
    return os.path.join(root, *parts)


def extract(archive: AmbArchive, dest: str, recurse: bool = True) -> int:
    """Extract every entry to dest. Nested archives become directories."""
    count = 0
    used: set[str] = set()
    os.makedirs(dest, exist_ok=True)
    for entry in archive:
        payload = archive.read(entry)
        if recurse and entry.is_archive and payload[:4] == MAGIC:
            try:
                count += extract(parse(payload), _safe_join(dest, entry.name), recurse)
                continue
            except AmbError:
                pass  # fall through and write the raw bytes
        out = _safe_join(dest, entry.name)
        # Two entries may still share a name; never let one silently replace another.
        if out.lower() in used:
            stem, ext = os.path.splitext(out)
            out = f"{stem}.{entry.index}{ext}"
        used.add(out.lower())
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as fp:
            fp.write(payload)
        count += 1
    return count


def _iter_amb_files(root: str):
    if os.path.isfile(root):
        yield root
        return
    for base, _, files in os.walk(root):
        for name in files:
            if name.lower().endswith(".amb"):
                yield os.path.join(base, name)


def cmd_list(args) -> int:
    archive = load(args.archive)
    size = os.path.getsize(args.archive)
    print(f"{args.archive}: {len(archive)} entries, {size} bytes")
    for entry in archive:
        tag = " [archive]" if entry.is_archive else ""
        print(f"  {entry.index:4d}  {entry.offset:10d}  {entry.length:10d}  {entry.name}{tag}")
    problems = validate(archive, size)
    for problem in problems:
        print(f"  ! {problem}", file=sys.stderr)
    return 1 if problems else 0


def cmd_extract(args) -> int:
    archive = load(args.archive)
    written = extract(archive, args.dest, recurse=not args.flat)
    print(f"extracted {written} files to {args.dest}")
    return 0


def cmd_verify(args) -> int:
    ok = bad = 0
    ext_counts: dict[str, int] = {}
    for path in _iter_amb_files(args.root):
        try:
            data = open(path, "rb").read()
            archive = parse(data)
            problems = validate(archive, len(data))
            if problems:
                bad += 1
                print(f"FAIL {path}: {problems[0]}")
                continue
            ok += 1
            for entry in archive:
                ext = os.path.splitext(entry.name)[1].upper() or "(none)"
                ext_counts[ext] = ext_counts.get(ext, 0) + 1
        except (AmbError, OSError) as exc:
            bad += 1
            print(f"FAIL {path}: {exc}")

    print(f"\n{ok} archives parsed cleanly, {bad} failed")
    if ext_counts:
        print("\ncontained file types:")
        for ext, n in sorted(ext_counts.items(), key=lambda kv: -kv[1]):
            print(f"  {ext:>10}  {n}")
    return 1 if bad else 0


def cmd_bulk(args) -> int:
    """Unpack every archive under a tree, mirroring the source layout."""
    root = os.path.abspath(args.root)
    archives = files = failed = 0
    for path in _iter_amb_files(root):
        rel = os.path.relpath(path, root)
        dest = os.path.join(args.dest, os.path.splitext(rel)[0])
        try:
            files += extract(load(path), dest)
            archives += 1
        except (AmbError, OSError) as exc:
            failed += 1
            print(f"FAIL {rel}: {exc}", file=sys.stderr)
        if archives % 100 == 0 and archives:
            print(f"  ... {archives} archives, {files} files")
    print(f"\nunpacked {archives} archives into {files} files under {args.dest}")
    if failed:
        print(f"{failed} archives failed", file=sys.stderr)
    return 1 if failed else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="AMB archive tool (Sonic 4 Episode II)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list", help="list the contents of one archive")
    p.add_argument("archive")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("extract", help="extract one archive")
    p.add_argument("archive")
    p.add_argument("dest")
    p.add_argument("--flat", action="store_true", help="do not recurse into nested archives")
    p.set_defaults(func=cmd_extract)

    p = sub.add_parser("bulk", help="unpack every archive under a directory tree")
    p.add_argument("root")
    p.add_argument("dest")
    p.set_defaults(func=cmd_bulk)

    p = sub.add_parser("verify", help="parse every archive under a directory tree")
    p.add_argument("root")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
