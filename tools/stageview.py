"""Assemble a whole stage from its tile grid and per-tile models.

Ties together everything decoded so far: `.MP` grids give tile ids, tile ids
index the zone's tileset archive, and each `.ZNO` in it yields geometry.

Two facts drive the placement, both established by measurement:

* **A grid cell is 20 world units.** The dominant tile bounding box in
  `ZONE1_M.AMB` is exactly 20x20, with multi-cell pieces at 40 and 60.
* **Models carry a fixed authored origin unrelated to placement.** Tile id 32
  appears at cells (98,0) through (98,5) reporting the same bounding-sphere
  centre every time, so the models were laid out side by side in an authoring
  scene and the engine translates them onto the grid. Each model is therefore
  re-centred on its own bounding box before instancing.

Output is Wavefront OBJ plus an orthographic PNG preview, since projecting the
assembled geometry down the Z axis reproduces the platformer's own camera and is
the quickest way to see whether a stage came out right.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402
import dds  # noqa: E402
import nn  # noqa: E402
import stagemap  # noqa: E402

CELL = 20.0

# Layer -> depth, following the parallax ordering the layer names imply.
LAYER_DEPTH = {
    "_A": 128.0, "_B": -128.0, "_N": 256.0,
    "_M": -256.0, "_M1": -384.0, "_M2": -512.0, "_M3": -640.0,
}


class Mesh:
    __slots__ = ("positions", "uvs", "triangles", "tri_texture")

    def __init__(self):
        self.positions: list[tuple[float, float, float]] = []
        self.uvs: list[tuple[float, float]] = []
        self.triangles: list[tuple[int, int, int]] = []
        self.tri_texture: list[str] = []


def model_mesh(data: bytes) -> Mesh | None:
    """Geometry of one model, re-centred on its bounding box, with textures."""
    f = nn.parse(data)
    obj = nn.read_object(data, f)
    if obj is None or obj.is_locator:
        return None
    vlists = nn.read_vertex_lists(data, obj)
    plists = nn.read_primitive_lists(data, obj)
    meshsets = nn.read_meshsets(data, obj)
    try:
        materials = nn.read_materials(data, f, obj)
        textures = nn.read_textures(data, f)
    except nn.NnError:
        materials, textures = [], []

    cx, cy, cz = obj.center
    mesh = Mesh()
    for ms in meshsets:
        if not (0 <= ms.i_vtxlist < len(vlists) and 0 <= ms.i_primlist < len(plists)):
            continue
        # mesh set -> material -> texture map -> NZTL name
        name = ""
        if 0 <= ms.i_material < len(materials):
            index = materials[ms.i_material].texture_index
            if index is not None and index < len(textures):
                name = textures[index].name
        vl = vlists[ms.i_vtxlist]
        base = len(mesh.positions)
        for x, y, z in vl.positions():
            mesh.positions.append((x - cx, y - cy, z - cz))
        uvs = vl.texcoords()
        for i in range(vl.count):
            mesh.uvs.append(uvs[i] if i < len(uvs) else (0.0, 0.0))
        for a, b, c in plists[ms.i_primlist].triangles():
            mesh.triangles.append((base + a, base + b, base + c))
            mesh.tri_texture.append(name)
    return mesh if mesh.positions else None


def load_zone_textures(act_archive: str) -> dict:
    """Decode every DDS in the zone's texture archives, keyed by upper name."""
    directory = os.path.dirname(os.path.abspath(act_archive))
    out = {}
    for entry in sorted(os.listdir(directory)):
        if not entry.upper().endswith(("_T.AMB", "_TEX.AMB")):
            continue
        try:
            archive = amb.load(os.path.join(directory, entry))
        except Exception:
            continue
        for item in archive:
            if not item.name.upper().endswith(".DDS"):
                continue
            label = item.name.replace(chr(92), "/").rsplit("/", 1)[-1].upper()
            if label in out:
                continue
            try:
                out[label] = dds.parse(archive.read(item))
            except dds.DdsError:
                pass
    return out


def assemble(act_archive: str, layers: list[str], tileset: str | None = None,
             region: tuple[int, int, int, int] | None = None):
    """Instance every tile of the chosen layers, returning combined geometry."""
    tileset = tileset or stagemap.find_tileset(act_archive)
    if not tileset:
        raise SystemExit("could not locate the tileset archive; pass --tileset")
    models = amb.load(tileset)
    grids = stagemap.load_stage(act_archive)

    cache: dict[int, Mesh | None] = {}
    positions: list[tuple[float, float, float]] = []
    uvs: list[tuple[float, float]] = []
    triangles: list[tuple[int, int, int]] = []
    tri_tile: list[int] = []
    tri_texture: list[str] = []
    placed = skipped = 0
    rx, ry, rw, rh = region if region else (0, 0, 1 << 30, 1 << 30)

    for label, grid in grids.items():
        stem = os.path.splitext(label)[0]
        suffix = stem[stem.rfind("_"):] if "_" in stem else ""
        if grid.depth != 2 or "_ATTR_" in label.upper():
            continue
        if layers and suffix not in layers:
            continue
        depth = LAYER_DEPTH.get(suffix, 0.0)

        for y in range(grid.height):
            if not (ry <= y < ry + rh):
                continue
            for x in range(grid.width):
                if not (rx <= x < rx + rw):
                    continue
                raw = grid[x, y]
                if not raw:
                    continue
                tid = raw & 0x0FFF
                if tid not in cache:
                    cache[tid] = None
                    if tid < len(models):
                        try:
                            cache[tid] = model_mesh(models.read(models.entries[tid]))
                        except nn.NnError:
                            cache[tid] = None
                mesh = cache[tid]
                if mesh is None:
                    skipped += 1
                    continue
                # Grid Y grows downward, world Y grows upward.
                ox, oy = x * CELL, -y * CELL
                base = len(positions)
                for px, py, pz in mesh.positions:
                    positions.append((px + ox, py + oy, pz + depth))
                uvs.extend(mesh.uvs)
                for ti, (a, b, c) in enumerate(mesh.triangles):
                    triangles.append((base + a, base + b, base + c))
                    tri_tile.append(tid)
                    tri_texture.append(mesh.tri_texture[ti] if ti < len(mesh.tri_texture) else "")
                placed += 1
    return positions, uvs, triangles, tri_tile, tri_texture, placed, skipped, tileset


def _png(path: str, width: int, height: int, rgb: bytearray) -> None:
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride: (y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        fp.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        fp.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        fp.write(chunk(b"IEND", b""))


def _tile_colour(tid: int) -> tuple[int, int, int]:
    h = (tid * 2654435761) & 0xFFFFFFFF
    return (70 + (h & 0x7F), 70 + ((h >> 8) & 0x7F), 70 + ((h >> 16) & 0x7F))


def render_ortho(positions, uvs, triangles, tri_tile, tri_texture, textures,
                 path: str, width: int = 1600) -> None:
    """Project down Z and rasterise, sampling each triangle's texture.

    Perspective correction is unnecessary here: the projection is orthographic,
    so barycentric interpolation of the texture coordinates is exact.
    """
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    span_x, span_y = max(maxx - minx, 1e-6), max(maxy - miny, 1e-6)
    height = max(1, int(width * span_y / span_x))
    scale = width / span_x

    rgb = bytearray(width * height * 3)
    for i in range(0, len(rgb), 3):
        rgb[i], rgb[i + 1], rgb[i + 2] = 16, 18, 24
    depth = [1e30] * (width * height)

    def project(p):
        return ((p[0] - minx) * scale, (maxy - p[1]) * scale, p[2])

    have_uv = len(uvs) == len(positions)
    for ti, tri in enumerate(triangles):
        pts = [project(positions[i]) for i in tri]
        z = sum(p[2] for p in pts) / 3.0
        lo_x = max(0, int(min(p[0] for p in pts)))
        hi_x = min(width - 1, int(max(p[0] for p in pts)) + 1)
        lo_y = max(0, int(min(p[1] for p in pts)))
        hi_y = min(height - 1, int(max(p[1] for p in pts)) + 1)
        if lo_x > hi_x or lo_y > hi_y:
            continue
        (x0, y0, _), (x1, y1, _), (x2, y2, _) = pts
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-12:
            continue

        tex = textures.get(tri_texture[ti].upper()) if ti < len(tri_texture) else None
        fallback = _tile_colour(tri_tile[ti] if ti < len(tri_tile) else 0)
        t0 = t1 = t2 = None
        if tex is not None and have_uv:
            t0, t1, t2 = uvs[tri[0]], uvs[tri[1]], uvs[tri[2]]

        for py in range(lo_y, hi_y + 1):
            for px in range(lo_x, hi_x + 1):
                cx, cy = px + 0.5, py + 0.5
                w0 = ((x1 - x0) * (cy - y0) - (cx - x0) * (y1 - y0)) / area
                w1 = ((cx - x0) * (y2 - y0) - (x2 - x0) * (cy - y0)) / area
                if w0 < 0 or w1 < 0 or w0 + w1 > 1:
                    continue
                idx = py * width + px
                if z >= depth[idx]:
                    continue
                depth[idx] = z
                if t0 is not None:
                    w2 = 1.0 - w0 - w1
                    u = t0[0] * w2 + t1[0] * w1 + t2[0] * w0
                    v = t0[1] * w2 + t1[1] * w1 + t2[1] * w0
                    cr, cg, cb = tex.rgb_at(u, v)
                else:
                    cr, cg, cb = fallback
                o = idx * 3
                rgb[o] = cr
                rgb[o + 1] = cg
                rgb[o + 2] = cb
    _png(path, width, height, rgb)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Assemble a stage from its tiles")
    ap.add_argument("archive", help="the act's <ZONE><act>_MAP.AMB")
    ap.add_argument("dest")
    ap.add_argument("--tileset")
    ap.add_argument("--layers", default="_B",
                    help="comma separated layer suffixes, e.g. _B,_A (default _B)")
    ap.add_argument("--width", type=int, default=1600)
    ap.add_argument("--no-obj", action="store_true")
    ap.add_argument("--no-textures", action="store_true")
    ap.add_argument("--region", help="x,y,w,h in grid cells")
    args = ap.parse_args(argv)

    region = None
    if args.region:
        parts = [int(v) for v in args.region.split(",")]
        if len(parts) != 4:
            raise SystemExit("--region wants x,y,w,h")
        region = tuple(parts)

    layers = [s for s in args.layers.split(",") if s] if args.layers != "all" else []
    (positions, uvs, triangles, tri_tile, tri_texture,
     placed, skipped, tileset) = assemble(args.archive, layers, args.tileset, region)
    print(f"{os.path.basename(args.archive)} -> {os.path.basename(tileset)}")
    print(f"{placed} tiles instanced, {skipped} skipped")
    print(f"{len(positions):,} vertices, {len(triangles):,} triangles")
    if not positions:
        print("nothing to render", file=sys.stderr)
        return 1

    os.makedirs(args.dest, exist_ok=True)
    stem = os.path.splitext(os.path.basename(args.archive))[0]
    if not args.no_obj:
        obj_path = os.path.join(args.dest, stem + ".obj")
        with open(obj_path, "w", encoding="ascii") as fp:
            fp.write(f"# {stem} assembled from {os.path.basename(tileset)}\n")
            for x, y, z in positions:
                fp.write(f"v {x:.3f} {y:.3f} {z:.3f}\n")
            for a, b, c in triangles:
                fp.write(f"f {a+1} {b+1} {c+1}\n")
        print(f"  {obj_path}")

    png_path = os.path.join(args.dest, stem + ".png")
    textures = {} if args.no_textures else load_zone_textures(args.archive)
    if textures:
        named = sum(1 for t in tri_texture if t)
        hit = sum(1 for t in tri_texture if t and t.upper() in textures)
        print(f"{len(textures)} zone textures loaded; "
              f"{hit}/{named} textured triangles resolved")
    render_ortho(positions, uvs, triangles, tri_tile, tri_texture, textures,
                 png_path, args.width)
    print(f"  {png_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
