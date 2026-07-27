"""DDS texture decoder for Sonic the Hedgehog 4 Episode II.

The game's textures are ordinary DirectDraw Surfaces — 2,853 of them across the
build, overwhelmingly DXT1 with some DXT3 and DXT5 and a handful uncompressed.
No custom wrapper, no swizzling.

Decoding them here rather than leaning on a library keeps the toolchain
dependency-free, and gives us RGBA that can be written straight to PNG or fed to
a software rasteriser.

Block formats, all 4x4 pixels:

    DXT1  8 bytes   two RGB565 endpoints, then 2 bits per pixel
    DXT3  16 bytes  8 bytes of 4-bit explicit alpha, then a DXT1 block
    DXT5  16 bytes  two alpha endpoints + 3 bits per pixel, then a DXT1 block

DXT1 encodes one-bit alpha by endpoint ordering: when colour0 <= colour1 the
fourth palette entry is transparent black rather than an interpolated colour.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402

MAGIC = b"DDS "
DDPF_FOURCC = 0x4
DDPF_LUMINANCE = 0x20000


class DdsError(Exception):
    pass


class Texture:
    __slots__ = ("width", "height", "fourcc", "pixels")

    def __init__(self, width: int, height: int, fourcc: str, pixels: bytearray):
        self.width, self.height, self.fourcc, self.pixels = width, height, fourcc, pixels

    def rgb_at(self, u: float, v: float) -> tuple[int, int, int]:
        """Nearest-neighbour sample with wrapping, for software rasterising."""
        x = int(u * self.width) % self.width
        y = int(v * self.height) % self.height
        o = (y * self.width + x) * 4
        return self.pixels[o], self.pixels[o + 1], self.pixels[o + 2]

    def __repr__(self):
        return f"<Texture {self.width}x{self.height} {self.fourcc}>"


def _rgb565(value: int) -> tuple[int, int, int]:
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    # Replicate high bits into the low ones so 0x1F maps to 255, not 248.
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def _colour_block(data: bytes, offset: int, out: bytearray, ox: int, oy: int,
                  width: int, height: int, opaque: bool) -> None:
    c0, c1 = struct.unpack_from("<HH", data, offset)
    (bits,) = struct.unpack_from("<I", data, offset + 4)
    r0, g0, b0 = _rgb565(c0)
    r1, g1, b1 = _rgb565(c1)

    palette = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
    if c0 > c1 or opaque:
        palette.append(((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255))
        palette.append(((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255))
    else:
        palette.append(((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255))
        palette.append((0, 0, 0, 0))

    for py in range(4):
        y = oy + py
        if y >= height:
            break
        for px in range(4):
            x = ox + px
            if x >= width:
                continue
            r, g, b, a = palette[(bits >> (2 * (4 * py + px))) & 3]
            o = (y * width + x) * 4
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            out[o + 3] = a


def _dxt3_alpha(data: bytes, offset: int, out: bytearray, ox: int, oy: int,
                width: int, height: int) -> None:
    (alpha,) = struct.unpack_from("<Q", data, offset)
    for py in range(4):
        y = oy + py
        if y >= height:
            break
        for px in range(4):
            x = ox + px
            if x >= width:
                continue
            nibble = (alpha >> (4 * (4 * py + px))) & 0xF
            out[(y * width + x) * 4 + 3] = nibble * 17  # 0..15 -> 0..255


def _dxt5_alpha(data: bytes, offset: int, out: bytearray, ox: int, oy: int,
                width: int, height: int) -> None:
    a0, a1 = data[offset], data[offset + 1]
    bits = int.from_bytes(data[offset + 2: offset + 8], "little")
    table = [a0, a1]
    if a0 > a1:
        table += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        table += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    for py in range(4):
        y = oy + py
        if y >= height:
            break
        for px in range(4):
            x = ox + px
            if x >= width:
                continue
            out[(y * width + x) * 4 + 3] = table[(bits >> (3 * (4 * py + px))) & 7]


def parse(data: bytes) -> Texture:
    """Decode the top mip level of a DDS to RGBA8."""
    if len(data) < 128 or data[:4] != MAGIC:
        raise DdsError(f"not a DDS (magic={data[:4]!r})")
    height, width = struct.unpack_from("<II", data, 12)
    (pf_flags,) = struct.unpack_from("<I", data, 80)
    fourcc = data[84:88].decode("ascii", "replace")
    if not width or not height:
        raise DdsError(f"degenerate size {width}x{height}")

    out = bytearray(width * height * 4)
    body = 128

    if not (pf_flags & DDPF_FOURCC):
        # Uncompressed. Drive the unpack from the channel masks rather than
        # special-casing depths: the build ships B8G8R8(A8), L8 luminance and
        # X1R5G5B5, and mask-driven extraction covers all of them.
        bit_count, r_mask, g_mask, b_mask, a_mask = struct.unpack_from("<5I", data, 88)
        if bit_count not in (8, 16, 24, 32):
            raise DdsError(f"unsupported uncompressed depth {bit_count}")
        stride = bit_count // 8
        need = width * height * stride
        if body + need > len(data):
            raise DdsError("uncompressed payload truncated")

        def channel(value: int, mask: int) -> int:
            """Extract a masked channel and scale it up to 8 bits."""
            if not mask:
                return 0
            shift = (mask & -mask).bit_length() - 1
            span = mask >> shift
            raw = (value & mask) >> shift
            return raw * 255 // span if span else 0

        luminance = bool(pf_flags & DDPF_LUMINANCE)
        for i in range(width * height):
            at = body + i * stride
            value = int.from_bytes(data[at: at + stride], "little")
            if luminance:
                grey = channel(value, r_mask)
                r = g = b = grey
            else:
                r = channel(value, r_mask)
                g = channel(value, g_mask)
                b = channel(value, b_mask)
            a = channel(value, a_mask) if a_mask else 255
            out[i * 4: i * 4 + 4] = bytes((r, g, b, a))
        return Texture(width, height, f"RAW{bit_count}", out)

    block = {"DXT1": 8, "DXT3": 16, "DXT5": 16}.get(fourcc)
    if block is None:
        raise DdsError(f"unsupported compression {fourcc!r}")

    blocks_x = (width + 3) // 4
    blocks_y = (height + 3) // 4
    need = blocks_x * blocks_y * block
    if body + need > len(data):
        raise DdsError(f"{fourcc} payload truncated: need {need}, have {len(data) - body}")

    for by in range(blocks_y):
        for bx in range(blocks_x):
            at = body + (by * blocks_x + bx) * block
            ox, oy = bx * 4, by * 4
            if fourcc == "DXT1":
                _colour_block(data, at, out, ox, oy, width, height, opaque=False)
            elif fourcc == "DXT3":
                _colour_block(data, at + 8, out, ox, oy, width, height, opaque=True)
                _dxt3_alpha(data, at, out, ox, oy, width, height)
            else:
                _colour_block(data, at + 8, out, ox, oy, width, height, opaque=True)
                _dxt5_alpha(data, at, out, ox, oy, width, height)
    return Texture(width, height, fourcc, out)


def write_png(tex: Texture, path: str) -> None:
    raw = bytearray()
    stride = tex.width * 4
    for y in range(tex.height):
        raw.append(0)
        raw += tex.pixels[y * stride: (y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        fp.write(chunk(b"IHDR", struct.pack(">IIBBBBB", tex.width, tex.height, 8, 6, 0, 0, 0)))
        fp.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        fp.write(chunk(b"IEND", b""))


def cmd_export(args) -> int:
    archive = amb.load(args.archive)
    os.makedirs(args.dest, exist_ok=True)
    written = 0
    for entry in archive:
        if not entry.name.upper().endswith(".DDS"):
            continue
        label = entry.name.replace(chr(92), "/").rsplit("/", 1)[-1]
        if args.name and args.name.upper() not in label.upper():
            continue
        try:
            tex = parse(archive.read(entry))
        except DdsError as exc:
            print(f"  ! {label}: {exc}", file=sys.stderr)
            continue
        out = os.path.join(args.dest, os.path.splitext(label)[0] + ".png")
        write_png(tex, out)
        print(f"  {out}  {tex.width}x{tex.height} {tex.fourcc}")
        written += 1
        if args.limit and written >= args.limit:
            break
    return 0


def cmd_verify(args) -> int:
    from collections import Counter
    ok = bad = 0
    formats: Counter = Counter()
    failures: list[str] = []
    for path in amb._iter_amb_files(args.root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        for entry in archive:
            if not entry.name.upper().endswith(".DDS"):
                continue
            try:
                tex = parse(archive.read(entry))
                # A fully transparent result is legitimate: the build ships
                # 8x8 NULL.DDS placeholders that really are blank.
                ok += 1
                formats[tex.fourcc] += 1
            except DdsError as exc:
                bad += 1
                if len(failures) < 10:
                    failures.append(f"{os.path.basename(path)}::{entry.name}: {exc}")
    print(f"{ok} textures decoded, {bad} failed")
    for fmt, n in formats.most_common():
        print(f"  {fmt:<6} {n}")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    return 1 if bad else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="DDS texture tool (Sonic 4 Episode II)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("export", help="decode textures from an archive to PNG")
    p.add_argument("archive")
    p.add_argument("dest")
    p.add_argument("--name", help="only entries containing this substring")
    p.add_argument("--limit", type=int, default=0)
    p.set_defaults(func=cmd_export)

    p = sub.add_parser("verify", help="decode every texture under a directory tree")
    p.add_argument("root")
    p.set_defaults(func=cmd_verify)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
