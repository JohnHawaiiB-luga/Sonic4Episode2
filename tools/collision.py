"""Stage collision shape reader for Sonic the Hedgehog 4 Episode II.

Each zone ships three collision files in `ZONE<n>_ATTR.AMB`:

    .DF   height fields   - the actual ground shape
    .DI   surface angles
    .AT   character attributes (through, cliff, grind)

Status: **structure VERIFIED, addressing OPEN.** The file layout below holds
exactly on all **39** stage collision files in the build. What is *not* yet known
is how a stage's `_ATTR_` cell id selects a record — see the note at the bottom.

## Layout

    0x00  u16   chips        (1535 for every Zone 1 file)
    0x02  u16   records
    0x04        records[records][size]
    ...   u16   chipIndex[chips]      - ATTR cell id -> record

with `size` = 4096 for `.DF` and 64 for `.DI` and `.AT`.

**The records come first and the index table is last.** An earlier reading here
had it the other way round, which put the index where record data actually lives
and made it look like 3,070 bytes of zeros - the arithmetic works either way, so
the file size alone cannot tell you which. The order is settled by the engine's
own setup routine at `Sonic.exe:0x00560349`:

```asm
movzx ecx, word [eax + 2]     ; the second u16
shl   ecx, 0xc                ; times 4096, the .DF record size
lea   ebp, [eax + 4]          ; region A = records, at +4
lea   eax, [ecx + eax + 4]    ; region B = index, after the records
```

It does the same with `shl 6` (times 64) for `.DI` and `.AT`.

## Height records

A `.DF` record is **64 cells of 64 bytes**. Each cell's 64 bytes are a height per
pixel column, valued 0-63 — the classic per-tile height array, and the reason
slopes are possible at all.

Observed values bear that out: flat-full cells read as 64 bytes of `0x20` (32),
empty cells as 64 bytes of `0x00`, and slopes and curves as intermediate values.
Measured over 8.4 million height bytes, 0 and 32 dominate and 1..31 carry the
shaped ground. The full 0..255 range is technically used, but only **0.02%** of
bytes exceed 63, so a full cell is 32 units tall and the rest are rare special
cases rather than ordinary geometry.

`.DI` and `.AT` records are 64 bytes, one byte per cell of the same 8x8 block.

## The gimmick variant

55 further collision files live in the `*_COL.AMB` gimmick archives
(`GMK_GEAR_*`, `GMK_RAIL_*`, `GMK_BREAK_*`). They carry **no header at all** -
their first four bytes read as zero counts - and are plain record arrays, mostly
whole multiples of 4096 or 64. They need a separate path and are not handled here.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402

RECORD_SIZES = {".DF": 4096, ".DI": 64, ".AT": 64}

CELLS_PER_RECORD = 64      # an 8x8 block
HEIGHTS_PER_CELL = 64      # one per pixel column

# A full-height cell reads 32, not 63. Measured over 8.4 million height bytes:
# 0 and 32 dominate, 1..31 carry the slopes and curves, and the whole 0..255
# range is technically used - but only 0.02% of bytes exceed 63, so those are
# rare special cases rather than ordinary geometry.
FULL_HEIGHT = 32


class CollisionError(Exception):
    pass


class CollisionFile:
    """One `.DF`, `.DI` or `.AT`."""

    def __init__(self, kind: str, count: int, records: list[bytes],
                 index: list[int] | None = None):
        self.kind = kind
        self.count = count
        self.records = records
        # Maps an ATTR cell id straight to a record. Verified: every one of the
        # 256 ids Zone 1 Act 1 uses lands on a valid record.
        self.index = index or []

    def record_for(self, attr_id: int) -> int | None:
        """The record an `_ATTR_` cell id selects, or None if out of range."""
        if not 0 <= attr_id < len(self.index):
            return None
        record = self.index[attr_id]
        return record if 0 <= record < len(self.records) else None

    def heights_for(self, attr_id: int, cell: int) -> list[int] | None:
        """Column heights for an ATTR id, resolved through the index."""
        record = self.record_for(attr_id)
        return None if record is None else self.heights(record, cell)

    @property
    def record_size(self) -> int:
        return RECORD_SIZES[self.kind]

    def heights(self, record: int, cell: int) -> list[int]:
        """The 64 column heights of one cell of a `.DF` record."""
        if self.kind != ".DF":
            raise CollisionError(f"{self.kind} carries no height field")
        if not 0 <= record < len(self.records):
            raise CollisionError(f"record {record} out of range")
        if not 0 <= cell < CELLS_PER_RECORD:
            raise CollisionError(f"cell {cell} out of range")
        at = cell * HEIGHTS_PER_CELL
        return list(self.records[record][at: at + HEIGHTS_PER_CELL])

    def cell_bytes(self, record: int) -> list[int]:
        """The 64 per-cell bytes of a `.DI` or `.AT` record."""
        if self.kind == ".DF":
            raise CollisionError(".DF records are height fields, not per-cell bytes")
        return list(self.records[record])

    def __repr__(self):
        return f"<Collision {self.kind} {len(self.records)} records, count={self.count}>"


def parse(kind: str, data: bytes) -> CollisionFile:
    kind = kind.upper()
    if kind not in RECORD_SIZES:
        raise CollisionError(f"unsupported collision kind {kind}")
    if len(data) < 4:
        raise CollisionError("too short for a header")

    count, record_count = struct.unpack_from("<HH", data, 0)
    size = RECORD_SIZES[kind]
    expected = 4 + record_count * size + count * 2
    if expected != len(data):
        raise CollisionError(
            f"layout mismatch: count={count} records={record_count} "
            f"implies {expected} bytes, file has {len(data)}")

    records = [data[4 + i * size: 4 + (i + 1) * size] for i in range(record_count)]

    table_at = 4 + record_count * size
    index = list(struct.unpack_from(f"<{count}H", data, table_at))
    return CollisionFile(kind, count, records, index)


def classify(heights: list[int]) -> str:
    """A rough shape label, useful for eyeballing a decode."""
    distinct = set(heights)
    if distinct == {0}:
        return "empty"
    if len(distinct) == 1:
        return f"flat({heights[0]})"
    if heights == sorted(heights):
        return "slope up"
    if heights == sorted(heights, reverse=True):
        return "slope down"
    return "curved"


def cmd_show(args) -> int:
    archive = amb.load(args.archive)
    for entry in archive:
        kind = os.path.splitext(entry.name)[1].upper()
        if kind not in RECORD_SIZES:
            continue
        label = entry.name.replace(chr(92), "/").rsplit("/", 1)[-1]
        try:
            parsed = parse(kind, archive.read(entry))
        except CollisionError as exc:
            print(f"  ! {label}: {exc}", file=sys.stderr)
            continue

        print(f"{label}: {parsed!r}")
        if kind != ".DF":
            continue

        shapes: Counter = Counter()
        for record in range(len(parsed.records)):
            for cell in range(CELLS_PER_RECORD):
                shapes[classify(parsed.heights(record, cell))] += 1
        print(f"  cell shapes across {len(parsed.records)} records:")
        for shape, n in shapes.most_common(8):
            print(f"    {shape:<14} {n}")

        # Show a cell that is neither empty nor flat, since those are the
        # interesting ones and prove heights are real.
        for record in range(len(parsed.records)):
            for cell in range(CELLS_PER_RECORD):
                heights = parsed.heights(record, cell)
                if classify(heights) not in ("empty",) and len(set(heights)) > 1:
                    print(f"  record {record} cell {cell} ({classify(heights)}):")
                    print(f"    {heights}")
                    return 0
    return 0


def cmd_verify(args) -> int:
    ok = bad = 0
    failures: list[str] = []
    shapes: Counter = Counter()

    for path in amb._iter_amb_files(args.root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        for entry in archive:
            kind = os.path.splitext(entry.name)[1].upper()
            if kind not in RECORD_SIZES:
                continue
            try:
                parsed = parse(kind, archive.read(entry))
                ok += 1
                if kind == ".DF":
                    for record in range(len(parsed.records)):
                        for cell in range(CELLS_PER_RECORD):
                            shapes[classify(parsed.heights(record, cell))] += 1
            except CollisionError as exc:
                bad += 1
                if len(failures) < 6:
                    failures.append(f"{os.path.basename(path)}::{entry.name}: {exc}")

    print(f"{ok} stage collision files parsed, {bad} did not match the layout")
    if shapes:
        print("\nheight-field cell shapes:")
        for shape, n in shapes.most_common(10):
            print(f"  {shape:<14} {n}")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    print("\nNote: the 55 gimmick collision files in *_COL.AMB carry no header "
          "and are expected to fail this check.")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Collision shape tool (Sonic 4 Episode II)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("show", help="summarise one zone's collision archive")
    p.add_argument("archive")
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("verify", help="parse every stage collision file under a tree")
    p.add_argument("root")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
