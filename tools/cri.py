"""CRI middleware container reader for Sonic the Hedgehog 4 Episode II.

All of the game's audio is CRI ADX2: cue sheets in `SOUND/*.CSB` and the streamed
music in one 137 MB `SOUND/SONICDL_SNG01.CPK`. Both are built out of the same
primitive — the **@UTF table**, a big-endian typed table with a schema, a string
pool and optional per-row or shared-constant storage.

Getting @UTF gives you both formats, because a CSB is a table whose cells hold
further tables and a CPK uses tables for its header and file TOC.

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
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

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
TOC_MAGIC = b"TOC "


@dataclass(frozen=True)
class CpkEntry:
    path: str
    offset: int
    size: int
    extracted_size: int
    file_id: int


@dataclass(frozen=True)
class AudioStreamInfo:
    codec: str
    channels: int
    sample_rate: int
    sample_count: int
    loop_flag: int


@dataclass(frozen=True)
class AudioFileInfo:
    path: str
    streams: tuple[AudioStreamInfo, ...]


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


def cpk_entries(data: bytes) -> list[CpkEntry]:
    """Read the file records from a CPK's TOC."""
    if data[:4] != CPK_MAGIC:
        raise CriError(f"not a CPK archive (magic={data[:4]!r})")

    header = parse_container(data)
    if len(header.rows) != 1:
        raise CriError(f"CPK header has {len(header.rows)} rows, expected 1")
    values = header.rows[0]

    toc_offset = values.get("TocOffset")
    content_offset = values.get("ContentOffset")
    declared_files = values.get("Files")
    if not all(isinstance(value, int) for value in (
            toc_offset, content_offset, declared_files)):
        raise CriError("CPK header lacks numeric TocOffset, ContentOffset or Files")
    if toc_offset < 0 or toc_offset + 16 > len(data):
        raise CriError(f"CPK TOC offset {toc_offset} is outside the archive")
    if data[toc_offset: toc_offset + 4] != TOC_MAGIC:
        raise CriError(
            f"CPK TOC has wrong magic {data[toc_offset: toc_offset + 4]!r}")

    toc = parse(data[toc_offset + 16:])
    if len(toc.rows) != declared_files:
        raise CriError(
            f"CPK declares {declared_files} files but TOC has "
            f"{len(toc.rows)} rows")
    # VERIFIED on Episode II's archive: this base plus the first FileOffset is
    # ContentOffset, and the final file ends exactly at EtocOffset.
    archive_base = min(toc_offset, content_offset)
    entries: list[CpkEntry] = []
    paths: set[str] = set()
    for index, row in enumerate(toc.rows):
        directory = row.get("DirName")
        file_name = row.get("FileName")
        size = row.get("FileSize")
        extracted_size = row.get("ExtractSize")
        file_offset = row.get("FileOffset")
        file_id = row.get("ID")
        if not isinstance(directory, str) or not isinstance(file_name, str):
            raise CriError(f"CPK TOC row {index} lacks a file path")
        if not all(isinstance(value, int) for value in (
                size, extracted_size, file_offset, file_id)):
            raise CriError(f"CPK TOC row {index} lacks numeric file fields")
        if min(size, extracted_size, file_offset) < 0:
            raise CriError(f"CPK TOC row {index} has a negative file field")
        if size != extracted_size:
            raise CriError(
                f"CPK entry {file_name!r} is compressed "
                f"({size} stored, {extracted_size} extracted)")
        relative = _cpk_path(directory, file_name)
        if relative in paths:
            raise CriError(f"duplicate CPK output path {relative!r}")
        paths.add(relative)

        start = archive_base + file_offset
        end = start + size
        if start < 0 or end > len(data):
            raise CriError(
                f"CPK entry {relative!r} spans {start}:{end}, "
                f"outside {len(data)} bytes")
        entries.append(CpkEntry(
            relative,
            start,
            size,
            extracted_size,
            file_id,
        ))
    return entries


def extract_cpk(data: bytes, output: str | os.PathLike) -> list[CpkEntry]:
    """Extract every stored file from a CPK into an output directory."""
    entries = cpk_entries(data)
    root = Path(output)
    root.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        target = root.joinpath(*PurePosixPath(entry.path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data[entry.offset: entry.offset + entry.size])
    return entries


def identify_aax(data: bytes) -> tuple[AudioStreamInfo, ...]:
    """Identify every encoded stream held by an AAX table."""
    table = parse(data)
    if table.name != "AAX":
        raise CriError(f"expected AAX table, found {table.name!r}")

    streams: list[AudioStreamInfo] = []
    for index, row in enumerate(table.rows):
        payload = row.get("data")
        loop_flag = row.get("lpflg")
        if not isinstance(payload, bytes) or not isinstance(loop_flag, int):
            raise CriError(f"AAX row {index} lacks data or lpflg")
        streams.append(_identify_adx(payload, loop_flag, index))
    if not streams:
        raise CriError("AAX table has no streams")
    return tuple(streams)


def identify_cpk(data: bytes) -> list[AudioFileInfo]:
    """Identify every AAX file stored in a CPK."""
    files: list[AudioFileInfo] = []
    for entry in cpk_entries(data):
        payload = data[entry.offset: entry.offset + entry.size]
        files.append(AudioFileInfo(entry.path, identify_aax(payload)))
    return files


# VERIFIED: all 94 Episode II streams use these ADX signature, parameter and
# marker fields; channels and sample rate agree with the CSB metadata 94/94.
def _identify_adx(
        data: bytes, loop_flag: int, row_index: int) -> AudioStreamInfo:
    if len(data) < 24:
        raise CriError(f"AAX row {row_index} audio header is truncated")
    if data[:2] != b"\x80\x00":
        raise CriError(
            f"AAX row {row_index} has unsupported audio magic {data[:4]!r}")

    header_size = struct.unpack_from(">H", data, 2)[0] + 4
    if header_size < 6 or header_size > len(data):
        raise CriError(
            f"AAX row {row_index} ADX header size {header_size} is invalid")
    if data[header_size - 6: header_size] != b"(c)CRI":
        raise CriError(f"AAX row {row_index} lacks the ADX copyright marker")
    if data[4:7] != bytes((3, 18, 4)):
        raise CriError(
            f"AAX row {row_index} uses unsupported ADX parameters "
            f"{tuple(data[4:7])}")

    channels = data[7]
    sample_rate = struct.unpack_from(">I", data, 8)[0]
    sample_count = struct.unpack_from(">I", data, 12)[0]
    if channels == 0 or sample_rate == 0:
        raise CriError(
            f"AAX row {row_index} has invalid ADX channels/sample rate")
    return AudioStreamInfo(
        "ADX",
        channels,
        sample_rate,
        sample_count,
        loop_flag,
    )


def _cpk_path(directory: str, file_name: str) -> str:
    raw = "/".join(part for part in (directory, file_name) if part)
    path = PurePosixPath(raw.replace("\\", "/"))
    if (not file_name or path.is_absolute() or
            any(part in ("", ".", "..") or ":" in part for part in path.parts)):
        raise CriError(f"unsafe CPK output path {raw!r}")
    return path.as_posix()


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


def cmd_extract(args) -> int:
    data = Path(args.file).read_bytes()
    entries = extract_cpk(data, args.output)
    print(
        f"{len(entries)} files extracted from "
        f"{os.path.basename(args.file)} to {args.output}")
    return 0


def cmd_identify(args) -> int:
    files = identify_cpk(Path(args.file).read_bytes())
    for file in files:
        combinations = {
            (stream.codec, stream.sample_rate, stream.channels)
            for stream in file.streams
        }
        if len(combinations) == 1:
            codec, sample_rate, channels = combinations.pop()
            description = (
                f"{codec}, {sample_rate} Hz, {_count(channels, 'channel')}, "
                f"{_count(len(file.streams), 'stream')}")
        else:
            description = ", ".join(
                f"{stream.codec} {stream.sample_rate} Hz "
                f"{_count(stream.channels, 'channel')}"
                for stream in file.streams
            )
        print(f"{file.path}: {description}")

    codec_files: dict[str, set[str]] = {}
    codec_streams: dict[str, int] = {}
    format_files: dict[tuple[int, int], set[str]] = {}
    format_streams: dict[tuple[int, int], int] = {}
    total_streams = 0
    for file in files:
        for stream in file.streams:
            codec_files.setdefault(stream.codec, set()).add(file.path)
            codec_streams[stream.codec] = codec_streams.get(stream.codec, 0) + 1
            key = (stream.sample_rate, stream.channels)
            format_files.setdefault(key, set()).add(file.path)
            format_streams[key] = format_streams.get(key, 0) + 1
            total_streams += 1

    print()
    print(f"{_count(len(files), 'file')}, {_count(total_streams, 'stream')}")
    for codec in sorted(codec_streams):
        print(
            f"{codec}: {_count(len(codec_files[codec]), 'file')}, "
            f"{_count(codec_streams[codec], 'stream')}")
    for sample_rate, channels in sorted(format_streams):
        key = (sample_rate, channels)
        print(
            f"{sample_rate} Hz, {_count(channels, 'channel')}: "
            f"{_count(len(format_files[key]), 'file')}, "
            f"{_count(format_streams[key], 'stream')}")
    return 0


def _count(value: int, noun: str) -> str:
    return f"{value} {noun if value == 1 else noun + 's'}"


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

    p = sub.add_parser("extract", help="extract every stored file from a CPK")
    p.add_argument("file")
    p.add_argument("output")
    p.set_defaults(func=cmd_extract)

    p = sub.add_parser("identify", help="report codecs and stream formats in a CPK")
    p.add_argument("file")
    p.set_defaults(func=cmd_identify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
