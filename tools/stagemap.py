"""Stage layout reader for Sonic the Hedgehog 4 Episode II.

Stage geometry lives in `<ZONE><act>_MAP.AMB` as a set of parallel grids of
identical dimensions:

    .MP   u16 width, u16 height, then width*height u16 cells
    .MD   u16 width, u16 height, then width*height u8  cells

Layer naming observed in the data set, per act:

    _A  / _B          tile layers (.MP tile id + .MD companion)
    _ATTR_A / _ATTR_B collision + attribute ids, parallel to the _A/_B grids
    _N               near layer
    _M, _M1.._M3     additional parallax layers

Status: VERIFIED. 400 of 400 .MP/.MD grids across every zone resolve to exactly
2 and 1 bytes per cell respectively against the header dimensions.

Renders previews as PNG with no third-party dependencies.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402


class StageMapError(Exception):
    pass


class Grid:
    def __init__(self, name: str, data: bytes):
        if len(data) < 4:
            raise StageMapError(f"{name}: too short to hold a header")
        self.name = name
        self.width, self.height = struct.unpack_from("<HH", data, 0)
        count = self.width * self.height
        body = len(data) - 4
        if count and body == count * 2:
            self.depth = 2
            self.cells = struct.unpack_from(f"<{count}H", data, 4)
        elif count and body == count:
            self.depth = 1
            self.cells = struct.unpack_from(f"<{count}B", data, 4)
        else:
            raise StageMapError(
                f"{name}: {self.width}x{self.height} does not divide {body} body bytes"
            )

    def __getitem__(self, xy):
        x, y = xy
        return self.cells[y * self.width + x]

    def tile(self, x: int, y: int) -> tuple[int, int, bool, bool]:
        """Decode a .MP cell into (tile_id, rotation, flip_h, flip_v).

        The u16 is a bitfield: id in bits 0-11, rotation in 12-13, horizontal
        flip in 14, vertical flip in 15. Verified across 512,070 non-zero cells
        — the widest id observed is 2779, comfortably inside 12 bits, and every
        high-nibble value seen (1, 3, 4, 8, 12) decodes to a sensible transform.
        Transforms are rare: 99.8% of cells carry none.
        """
        if self.depth != 2:
            raise StageMapError(f"{self.name}: tile() is only meaningful for .MP grids")
        v = self.cells[y * self.width + x]
        return v & 0x0FFF, (v >> 12) & 3, bool((v >> 14) & 1), bool((v >> 15) & 1)

    @property
    def occupancy(self) -> float:
        return sum(1 for c in self.cells if c) / len(self.cells)

    def __repr__(self):
        return (
            f"<Grid {self.name} {self.width}x{self.height} "
            f"u{self.depth * 8} occupancy={self.occupancy:.1%}>"
        )


def load_stage(path: str) -> dict[str, Grid]:
    """Read every .MP/.MD grid out of a stage archive, keyed by layer name."""
    archive = amb.load(path)
    grids: dict[str, Grid] = {}
    for entry in archive:
        ext = os.path.splitext(entry.name)[1].upper()
        if ext not in (".MP", ".MD"):
            continue
        # Names are stored with a leading ".\.\" path prefix.
        label = entry.name.replace("\\", "/").rsplit("/", 1)[-1]
        try:
            grids[label] = Grid(label, archive.read(entry))
        except StageMapError as exc:
            print(f"  ! {exc}", file=sys.stderr)
    return grids


def _png(path: str, width: int, height: int, rgb: bytearray) -> None:
    """Write a minimal 8-bit RGB PNG."""
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter type: none
        raw += rgb[y * stride : (y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    with open(path, "wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        fp.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        fp.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        fp.write(chunk(b"IEND", b""))


def _colour(value: int) -> tuple[int, int, int]:
    """Distinct, stable colour per non-zero cell id; empty cells stay dark."""
    if value == 0:
        return (16, 18, 24)
    h = (value * 2654435761) & 0xFFFFFFFF
    return (80 + (h & 0x7F), 80 + ((h >> 8) & 0x7F), 80 + ((h >> 16) & 0x7F))


def render(grid: Grid, path: str, scale: int = 1) -> None:
    w, h = grid.width, grid.height
    rgb = bytearray(w * scale * h * scale * 3)
    stride = w * scale * 3
    for y in range(h):
        for x in range(w):
            r, g, b = _colour(grid[x, y])
            for sy in range(scale):
                base = (y * scale + sy) * stride + x * scale * 3
                for sx in range(scale):
                    off = base + sx * 3
                    rgb[off] = r
                    rgb[off + 1] = g
                    rgb[off + 2] = b
    _png(path, w * scale, h * scale, rgb)


BLOCK_PITCH = 256  # pixels covered by one .EV/.DC/.RG block


class Placement:
    """One 12-byte record from an .EV block.

        +0x00 u8  x within the block      +0x06 s8  bounding box left
        +0x01 u8  y within the block      +0x07 s8  bounding box top
        +0x02 u16 object id               +0x08 u8  bounding box width
        +0x04 u16 flags                   +0x09 u8  bounding box height
                                          +0x0A u16 parameter
    """

    __slots__ = (
        "block_x", "block_y", "raw", "x", "y", "object_id", "flags",
        "left", "top", "width", "height", "param",
    )

    def __init__(self, block_x: int, block_y: int, raw: bytes):
        self.block_x, self.block_y, self.raw = block_x, block_y, raw
        self.x, self.y = raw[0], raw[1]
        self.object_id, self.flags = struct.unpack_from("<HH", raw, 2)
        self.left, self.top = struct.unpack_from("<bb", raw, 6)
        self.width, self.height = raw[8], raw[9]
        (self.param,) = struct.unpack_from("<H", raw, 10)

    @property
    def world(self) -> tuple[int, int]:
        """Absolute pixel position: block index scaled by the 256px pitch."""
        return self.block_x * BLOCK_PITCH + self.x, self.block_y * BLOCK_PITCH + self.y

    def __repr__(self):
        wx, wy = self.world
        return f"<Placement id={self.object_id} at ({wx},{wy}) flags=0x{self.flags:04X}>"


# Record stride per extension, verified against the whole build:
# .EV placements are 12 bytes, .DC 4 bytes, .RG 2 bytes.
BLOCK_STRIDE = {".EV": 12, ".DC": 4, ".RG": 2}


def read_blocks(data: bytes, stride: int) -> list[tuple[int, int, bytes]]:
    """Parse the shared .EV/.DC/.RG block structure.

    Layout: u16 block_w, u16 block_h (the map at quarter resolution, rounded
    up), then block_w*block_h u32 absolute offsets. Each block holds a u16
    record count followed by that many fixed-size records. Blocks may share an
    offset, which is how empty regions collapse.

    Returns (block_x, block_y, record_bytes) tuples.
    """
    if len(data) < 8:
        raise StageMapError("too short for a block-grid header")
    bw, bh = struct.unpack_from("<HH", data, 0)
    table_end = 4 + bw * bh * 4
    if table_end > len(data):
        raise StageMapError(f"offset table {bw}x{bh} overruns {len(data)} bytes")
    offsets = struct.unpack_from(f"<{bw * bh}I", data, 4)

    out = []
    for index, offset in enumerate(offsets):
        if offset + 2 > len(data):
            raise StageMapError(f"block {index} offset {offset} out of range")
        (count,) = struct.unpack_from("<H", data, offset)
        if offset + 2 + count * stride > len(data):
            raise StageMapError(f"block {index} declares {count} records but overruns")
        by, bx = divmod(index, bw)
        for i in range(count):
            base = offset + 2 + i * stride
            out.append((bx, by, data[base : base + stride]))
    return out


def read_events(data: bytes) -> list[Placement]:
    """Parse an .EV object placement file into Placement records."""
    return [Placement(bx, by, raw) for bx, by, raw in read_blocks(data, BLOCK_STRIDE[".EV"])]


def cmd_events(args) -> int:
    from collections import Counter

    archive = amb.load(args.archive)
    total = 0
    for entry in archive:
        if not entry.name.upper().endswith(".EV"):
            continue
        label = entry.name.replace("\\", "/").rsplit("/", 1)[-1]
        try:
            placements = read_events(archive.read(entry))
        except StageMapError as exc:
            print(f"  ! {label}: {exc}", file=sys.stderr)
            continue
        ids = Counter(p.object_id for p in placements)
        print(f"{label}: {len(placements)} placements, {len(ids)} distinct object ids")
        for oid, n in ids.most_common(args.top):
            print(f"    id {oid:<6} x{n}")
        total += len(placements)
    print(f"\n{total} placements total")
    return 0


def cmd_info(args) -> int:
    grids = load_stage(args.archive)
    if not grids:
        print("no .MP/.MD grids found", file=sys.stderr)
        return 1
    print(f"{args.archive}: {len(grids)} grids")
    for name, grid in grids.items():
        print(
            f"  {name:<22} {grid.width:>4}x{grid.height:<4} u{grid.depth*8:<2} "
            f"occupancy {grid.occupancy:>6.1%}  distinct {len(set(grid.cells)):>4}"
        )
    dims = {(g.width, g.height) for g in grids.values()}
    print(f"\nall layers share dimensions: {len(dims) == 1} {dims}")
    return 0


def cmd_render(args) -> int:
    grids = load_stage(args.archive)
    os.makedirs(args.dest, exist_ok=True)
    for name, grid in grids.items():
        out = os.path.join(args.dest, name + ".png")
        render(grid, out, args.scale)
        print(f"  {out}  ({grid.width * args.scale}x{grid.height * args.scale})")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Stage layout tool (Sonic 4 Episode II)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info", help="summarise the layers of a stage archive")
    p.add_argument("archive")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("events", help="list object placements from the .EV files")
    p.add_argument("archive")
    p.add_argument("--top", type=int, default=8)
    p.set_defaults(func=cmd_events)

    p = sub.add_parser("render", help="render each layer to a PNG")
    p.add_argument("archive")
    p.add_argument("dest")
    p.add_argument("--scale", type=int, default=1)
    p.set_defaults(func=cmd_render)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
