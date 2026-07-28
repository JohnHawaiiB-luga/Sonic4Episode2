#!/usr/bin/env python3
"""Draw a stage's collision as a PNG.

This is the clearest picture of what the reverse engineering actually recovered.
The tile grid says what a level *looks* like; this says what it *is* — every
solid surface, at the per-pixel resolution the game itself collides against,
assembled from three separate files that each had to be decoded before any of it
could be drawn:

  `_ATTR_B.MP`  which cell has which attribute id
  `.DF`         64 column heights per cell, two pixels per unit
  `.DI`         one surface angle per cell, a full turn per 256

Rings from `.RG` are overlaid, and the player spawn is marked, so the result is a
readable map of an act rather than a debug dump.

Writes a PNG with no dependencies — same minimal encoder the other tools use.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb          # noqa: E402
import collision    # noqa: E402
import stagemap     # noqa: E402

CELL = 16           # pixels drawn per collision cell
HEIGHTS = 64        # columns stored per cell
FULL = 32           # height units in a full cell

SKY = (24, 26, 38)
SOLID = (232, 236, 244)
SHADE = (120, 132, 158)
STEEP = (250, 170, 90)          # surfaces the angle data calls sloped
RING = (255, 202, 40)
SPAWN = (90, 200, 255)


def write_png(path: str, width: int, height: int, pixels: bytearray) -> None:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * width * 3:(y + 1) * width * 3]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def load(act_path: str):
    """The attribute grid, the shape files and the rings for one act."""
    archive = amb.load(act_path)
    grid = rings = None
    for entry in archive:
        name = entry.name.upper()
        if name.endswith("_ATTR_B.MP"):
            grid = stagemap.Grid(entry.name, archive.read(entry))
        elif name.endswith(".RG"):
            rings = archive.read(entry)

    directory = os.path.dirname(act_path)
    shapes = angles = None
    for candidate in os.listdir(directory):
        if not candidate.upper().endswith("_ATTR.AMB"):
            continue
        attr = amb.load(os.path.join(directory, candidate))
        for entry in attr:
            kind = os.path.splitext(entry.name)[1].upper()
            if kind == ".DF":
                shapes = collision.parse(kind, attr.read(entry))
            elif kind == ".DI":
                angles = collision.parse(kind, attr.read(entry))
        if shapes:
            break
    return grid, shapes, angles, rings


def ring_positions(data: bytes):
    if not data:
        return []
    out = []
    bw, bh = struct.unpack_from("<HH", data, 0)
    for index in range(bw * bh):
        offset = struct.unpack_from("<i", data, 4 + index * 4)[0]
        count = struct.unpack_from("<H", data, offset)[0]
        by, bx = divmod(index, bw)
        for i in range(count):
            at = offset + 2 + i * 2
            out.append((bx * 256 + data[at], by * 256 + data[at + 1]))
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("archive", help="the act's <ZONE><act>_MAP.AMB")
    ap.add_argument("dest")
    ap.add_argument("--region", help="x,y,w,h in cells; default is everything")
    ap.add_argument("--cell", type=int, default=CELL)
    args = ap.parse_args(argv)

    grid, shapes, angles, rings = load(args.archive)
    if grid is None:
        print("no _ATTR_B.MP in that archive", file=sys.stderr)
        return 1

    x0, y0, w, h = 0, 0, grid.width, grid.height
    if args.region:
        x0, y0, w, h = (int(v) for v in args.region.split(","))
    w = min(w, grid.width - x0)
    h = min(h, grid.height - y0)

    scale = args.cell
    width, height = w * scale, h * scale
    pixels = bytearray(SKY * (width * height))

    def put(px: int, py: int, colour):
        if 0 <= px < width and 0 <= py < height:
            at = (py * width + px) * 3
            pixels[at:at + 3] = bytes(colour)

    def attribute_at(cx: int, cy: int) -> int:
        if not (0 <= cx < grid.width and 0 <= cy < grid.height):
            return 0
        return grid[cx, cy] & 0x0FFF

    solid_cells = shaped = 0
    for cy in range(y0, y0 + h):
        for cx in range(x0, x0 + w):
            attribute = attribute_at(cx, cy)
            if attribute == 0:
                continue
            solid_cells += 1
            # Only a cell with open space above it has a surface worth drawing a
            # highlight on; without this, stacked solid rock reads as stripes.
            exposed = attribute_at(cx, cy - 1) == 0

            cell = (cy & 7) * 8 + (cx & 7)
            heights = shapes.heights_for(attribute, cell) if shapes else None
            angle = 0
            if angles:
                record = angles.record_for(attribute)
                if record is not None:
                    angle = angles.cell_bytes(record)[cell]
            sloped = angle not in (0,) and abs(((-angle * 360 / 256) + 180) % 360 - 180) > 8

            bx = (cx - x0) * scale
            by = (cy - y0) * scale
            if heights is None or max(heights) == 0:
                for py in range(scale):
                    for px in range(scale):
                        put(bx + px, by + py,
                            SOLID if (exposed and py < 2) else SHADE)
                continue

            shaped += 1
            face = STEEP if sloped else SOLID
            for px in range(scale):
                column = heights[min(px * HEIGHTS // scale, HEIGHTS - 1)]
                filled = min(int(column * scale / FULL), scale)
                top = scale - filled
                for py in range(top, scale):
                    highlight = py < top + 2 and (exposed or filled < scale)
                    put(bx + px, by + py, face if highlight else SHADE)

    for rx, ry in ring_positions(rings):
        px = int(rx / 64 * scale) - x0 * scale
        py = int(ry / 64 * scale) - y0 * scale
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if dx * dx + dy * dy <= 4:
                    put(px + dx, py + dy, RING)

    write_png(args.dest, width, height, pixels)
    print(f"{args.dest}: {width}x{height}, {solid_cells} solid cells "
          f"({shaped} with height fields), {len(ring_positions(rings))} rings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
