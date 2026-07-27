"""CRI middleware container reader for Sonic the Hedgehog 4 Episode II.

All of the game's audio is CRI ADX2: cue sheets in `SOUND/*.CSB` and the streamed
music in one 137 MB `SOUND/SONICDL_SNG01.CPK`. Both are built out of the same
primitive — the **@UTF table**, a big-endian typed table with a schema, a string
pool and optional per-row or shared-constant storage.

Getting @UTF gives you both formats, because a CSB is a table whose cells hold
further tables and a CPK is a table whose cells hold file data.

Layout, big-endian throughout, with every offset **relative to 0x08**:

    0x00  char[4]  '@UTF'
    0x04  u32      table size, from 0x08 onward
    0x08  u32      rows offset
    0x0C  u32      string pool offset
    0x10  u32      data pool offset
    0x14  u32      table name, into the string pool
    0x18  u16      column count
    0x1A  u16      row width in bytes
    0x1C  u32      row count

Then one descriptor per column: a flags byte, usually a u32 name offset, and for
constant columns the value inline. The low nibble of the flags is the type; the
high nibble is where the value lives — zero, a shared constant in the schema, or
per row.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

MAGIC = b"@UTF"

# Storage class, from the flag byte's high nibble. These are the values CRI
# actually uses - not a dense 1/2/3 enumeration, which is the obvious wrong
# guess and yields empty column names because the name offset then misaligns.
STORAGE_ZERO = 0x10
STORAGE_CONSTANT = 0x30
STORAGE_PER_ROW = 0x50

# Type codes, from the low nibble. The value is (struct format, size).
TYPES = {
    0x00: ("B", 1), 0x01: ("b", 1),
    0x02: ("H", 2), 0x03: ("h", 2),
    0x04: ("I", 4), 0x05: ("i", 4),
    0x06: ("Q", 8), 0x07: ("q", 8),
    0x08: ("f", 4), 0x09: ("d", 8),
    0x0A: ("I", 4),   # string: offset into the pool
    0x0B: ("II", 8),  # data: offset and length into the data pool
}


class CriError(Exception):
    pass


class Column:
    __slots__ = ("name", "type_code", "storage", "constant")

    def __init__(self, name: str, type_code: int, storage: int, constant=None):
        self.name = name
        self.type_code = type_code
        self.storage = storage
        self.constant = constant

    @property
    def is_string(self) -> bool:
        return self.type_code == 0x0A

    @property
    def is_data(self) -> bool:
        return self.type_code == 0x0B

    def __repr__(self):
        return f"<Column {self.name} type={self.type_code:#x} storage={self.storage:#x}>"


class UtfTable:
    """A parsed @UTF table: named columns and a list of row dictionaries."""

    def __init__(self, name: str, columns: list[Column], rows: list[dict]):
        self.name = name
        self.columns = columns
        self.rows = rows

    def __len__(self) -> int:
        return len(self.rows)

    def column_names(self) -> list[str]:
        return [c.name for c in self.columns]

    def values(self, column: str) -> list:
        return [row.get(column) for row in self.rows]

    def __repr__(self):
        return f"<UtfTable {self.name} {len(self.rows)} rows x {len(self.columns)} cols>"


def _cstring(data: bytes, at: int) -> str:
    end = data.find(b"\0", at)
    if end == -1:
        end = len(data)
    return data[at:end].decode("utf-8", "replace")


def parse(data: bytes) -> UtfTable:
    """Parse one @UTF table from the start of a buffer."""
    if len(data) < 0x20 or data[:4] != MAGIC:
        raise CriError(f"not a @UTF table (magic={data[:4]!r})")

    base = 8  # every offset in the header is relative to here
    (table_size, rows_offset, string_offset,
     data_offset, name_offset) = struct.unpack_from(">5I", data, 4)
    column_count, row_width = struct.unpack_from(">2H", data, 0x18)
    (row_count,) = struct.unpack_from(">I", data, 0x1C)

    if base + table_size > len(data):
        raise CriError(f"table claims {table_size} bytes, buffer has {len(data) - base}")

    strings = base + string_offset
    pool = base + data_offset
    table_name = _cstring(data, strings + name_offset)

    columns: list[Column] = []
    at = 0x20
    for i in range(column_count):
        if at >= len(data):
            raise CriError(f"column {i} descriptor past end of buffer")
        flags = data[at]
        at += 1

        storage = flags & 0xF0
        type_code = flags & 0x0F
        if type_code not in TYPES:
            raise CriError(f"column {i}: unknown type {type_code:#x}")

        # The name offset is always present, whatever the storage class.
        (name_at,) = struct.unpack_from(">I", data, at)
        at += 4
        name = _cstring(data, strings + name_at)

        constant = None
        if storage == STORAGE_CONSTANT:
            fmt, size = TYPES[type_code]
            constant = struct.unpack_from(">" + fmt, data, at)
            constant = constant[0] if len(constant) == 1 else constant
            at += size

        columns.append(Column(name, type_code, storage, constant))

    rows: list[dict] = []
    for r in range(row_count):
        row_at = base + rows_offset + r * row_width
        cursor = row_at
        row: dict = {}
        for column in columns:
            if column.storage == STORAGE_ZERO:
                row[column.name] = 0
                continue
            if column.storage == STORAGE_CONSTANT:
                value = column.constant
            else:
                fmt, size = TYPES[column.type_code]
                if cursor + size > len(data):
                    raise CriError(f"row {r} runs past end of buffer")
                unpacked = struct.unpack_from(">" + fmt, data, cursor)
                value = unpacked[0] if len(unpacked) == 1 else unpacked
                cursor += size

            if column.is_string and isinstance(value, int):
                value = _cstring(data, strings + value)
            elif column.is_data and isinstance(value, tuple):
                offset, length = value
                value = data[pool + offset: pool + offset + length]
            row[column.name] = value
        rows.append(row)

    return UtfTable(table_name, columns, rows)


CPK_MAGIC = b"CPK "


def parse_container(data: bytes) -> UtfTable:
    """Parse a `.CSB` or a `.CPK`.

    A CSB is a bare @UTF table. A CPK wraps one behind a 16-byte header - magic,
    flags and the table size - so the same parser handles both once the wrapper
    is stepped over.
    """
    if data[:4] == CPK_MAGIC:
        if len(data) < 0x10:
            raise CriError("CPK header truncated")
        return parse(data[0x10:])
    return parse(data)


def nested_tables(table: UtfTable) -> dict[str, UtfTable]:
    """Parse any cell whose bytes are themselves a @UTF table.

    A CSB is exactly this: a `TBLCSB` table whose rows name sub-tables — INFO,
    CUE, SYNTH, SOUND_ELEMENT and so on — and whose `utf` column holds each one
    as raw bytes.
    """
    out: dict[str, UtfTable] = {}
    for row in table.rows:
        name = None
        for key in ("name", "Name"):
            if isinstance(row.get(key), str):
                name = row[key]
                break
        for value in row.values():
            if isinstance(value, (bytes, bytearray)) and value[:4] == MAGIC:
                try:
                    out[name or f"table{len(out)}"] = parse(bytes(value))
                except CriError:
                    pass
    return out


def cue_names(path: str) -> list[str]:
    """Cue names from a `.CSB`, which is what the game triggers sounds by."""
    table = parse_container(open(path, "rb").read())
    for sub_name, sub in nested_tables(table).items():
        if sub_name.upper() != "CUE":
            continue
        for key in ("name", "Name", "CueName"):
            if key in sub.column_names():
                return [v for v in sub.values(key) if isinstance(v, str)]
    return []


def cmd_show(args) -> int:
    data = open(args.file, "rb").read()
    table = parse_container(data)
    print(f"{os.path.basename(args.file)}: {table!r}")
    print(f"  columns: {', '.join(table.column_names())}")

    children = nested_tables(table)
    for name, child in children.items():
        print(f"\n  {name}: {child!r}")
        print(f"    columns: {', '.join(child.column_names())}")
        for row in child.rows[: args.rows]:
            shown = {
                k: (f"<{len(v)} bytes>" if isinstance(v, (bytes, bytearray)) else v)
                for k, v in list(row.items())[:6]
            }
            print(f"    {shown}")
    return 0


def cmd_cues(args) -> int:
    total = 0
    for path in sorted(_iter_files(args.root, ".CSB")):
        names = cue_names(path)
        total += len(names)
        print(f"{os.path.basename(path)}: {len(names)} cues")
        for name in names[: args.limit]:
            print(f"    {name}")
    print(f"\n{total} cues across every cue sheet")
    return 0


def cmd_verify(args) -> int:
    ok = bad = 0
    failures: list[str] = []
    for path in sorted(_iter_files(args.root, ".CSB", ".CPK")):
        try:
            table = parse_container(open(path, "rb").read())
            children = nested_tables(table)
            print(f"{os.path.basename(path):<24} {table.name:<10} "
                  f"{len(table.rows)} rows, {len(children)} nested tables")
            ok += 1
        except CriError as exc:
            bad += 1
            failures.append(f"{os.path.basename(path)}: {exc}")
    print(f"\n{ok} CRI containers parsed, {bad} failed")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    return 1 if bad else 0


def _iter_files(root: str, *extensions: str):
    if os.path.isfile(root):
        yield root
        return
    for base, _, files in os.walk(root):
        for name in files:
            if name.upper().endswith(extensions):
                yield os.path.join(base, name)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="CRI container tool (Sonic 4 Episode II)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("show", help="dump a container's tables")
    p.add_argument("file")
    p.add_argument("--rows", type=int, default=3)
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("cues", help="list cue names from every cue sheet")
    p.add_argument("root")
    p.add_argument("--limit", type=int, default=8)
    p.set_defaults(func=cmd_cues)

    p = sub.add_parser("verify", help="parse every CRI container under a tree")
    p.add_argument("root")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
