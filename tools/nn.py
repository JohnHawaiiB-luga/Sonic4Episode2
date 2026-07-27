"""SEGA NN (Ninja Next) container reader for Sonic the Hedgehog 4 Episode II.

Models (`.ZNO`), motions (`.ZNM`) and vertex animations (`.ZNV`) are all BINCNK
files: a flat sequence of 8-byte-headed chunks terminated by `NEND`.

    struct chunk { char tag[4]; u32 size; u8 payload[size]; }

Chunk tags carry a platform letter in position 1 — `Z` for Direct3D 9, `X` for
Xbox, `G` for GameCube, `I` for the OpenGL ES builds. Episode I's decompilation
switches on `NIOB`/`NITL`/`NEND`; Episode II ships the same chunks as `NZOB` and
`NZTL`, which is what confirms the two games share this format.

A typical model:

    NZIF   file header
    NZTL   texture list
    NZOB   object — nodes, materials, vertex and index data
    NOF0   relocation table: offsets inside the data chunks needing fixup
    NFN0   originating file name
    NEND   terminator

The NZIF payload is six u32s (field names from Episode I's
`NNS_BINCNK_FILEHEADER`):

    +0x00  nChunk    data chunk count (2 for a model: NZTL and NZOB)
    +0x04  OfsData   offset of the first data chunk (always 0x20)
    +0x08  SizeData  total size of the data chunks
    +0x0C  OfsNOF0   offset of the NOF0 relocation chunk
    +0x10  SizeNOF0  size of the NOF0 chunk including its header
    +0x14  Version

Data chunks carry a longer header than the plain tag/size pair: after the size
come `OfsMainData` and `Version`. `OfsMainData` is relative to `OfsData`, not to
the chunk, and points at the chunk's root structure.

Walking tag/size to `NEND` does not need any of those, so the reader treats them
as informational and validates them instead of relying on them.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402

HEADER_TAGS = {b"NZIF", b"NXIF", b"NGIF", b"NIIF"}
END_TAG = b"NEND"


class NnError(Exception):
    pass


class Chunk:
    __slots__ = ("tag", "offset", "size", "payload")

    def __init__(self, tag: bytes, offset: int, size: int, payload: bytes):
        self.tag, self.offset, self.size, self.payload = tag, offset, size, payload

    @property
    def name(self) -> str:
        return self.tag.decode("ascii", "replace")

    def __repr__(self):
        return f"<Chunk {self.name} @{self.offset:#x} {self.size}B>"


class NnFile:
    def __init__(self, chunks: list[Chunk]):
        self.chunks = chunks

    def __iter__(self):
        return iter(self.chunks)

    def find(self, tag: str) -> Chunk | None:
        upper = tag.upper().encode("ascii")
        for chunk in self.chunks:
            if chunk.tag == upper:
                return chunk
        return None

    def kind(self) -> str | None:
        """Primary payload tag, platform letter normalised to '?'.

        A model carries both NZOB and NZTL; the object is what identifies it, so
        the substantive chunks win over the texture list.
        """
        found = None
        for chunk in self.chunks:
            if chunk.tag in HEADER_TAGS or chunk.tag in (END_TAG, b"NOF0", b"NFN0"):
                continue
            normalised = "N?" + chunk.name[2:]
            if normalised in ("N?OB", "N?MO", "N?MA"):
                return normalised
            found = found or normalised
        return found

    @property
    def filename(self) -> str | None:
        """Original authored filename, preserving its case.

        NFN0's payload is two reserved u32s followed by a NUL-terminated name.
        Worth having: the AMB string table uppercases everything, while this
        keeps what the artist actually typed (`Z1_G_hasira_B.zno`).
        """
        chunk = self.find("NFN0")
        if not chunk or chunk.size < 9:
            return None
        raw = chunk.payload[8:]
        end = raw.find(b"\0")
        return raw[: end if end != -1 else len(raw)].decode("ascii", "replace") or None


class NnObject:
    """The `NZOB` object header — 88 bytes describing a model's contents.

    Every `ofs_*` is relative to the file's data base (the `OfsData` field of the
    file header, `0x20` in practice), not to the chunk or the file. Zero means
    the list is absent.

        +0x00  float[3]  bounding sphere centre
        +0x0C  float     bounding sphere radius
        +0x10  s32/u32   material count, offset
        +0x18  s32/u32   vertex list count, offset
        +0x20  s32/u32   primitive list count, offset
        +0x28  s32       node count
        +0x2C  s32       maximum node depth
        +0x30  u32       node list offset
        +0x34  s32       matrix palette count
        +0x38  s32/u32   subobject count, offset
        +0x40  s32       texture count
        +0x44  u32       type flags
        +0x48  s32       version
        +0x4C  float[3]  bounding box half-extents
    """

    SIZE = 0x58

    __slots__ = (
        "center", "radius", "n_material", "ofs_material", "n_vtxlist", "ofs_vtxlist",
        "n_primlist", "ofs_primlist", "n_node", "max_node_depth", "ofs_node",
        "n_mtxpal", "n_subobj", "ofs_subobj", "n_tex", "ftype", "version", "bbox",
    )

    def __init__(self, data: bytes, offset: int):
        cx, cy, cz, self.radius = struct.unpack_from("<4f", data, offset)
        self.center = (cx, cy, cz)
        p = offset + 0x10
        self.n_material, self.ofs_material = struct.unpack_from("<iI", data, p)
        self.n_vtxlist, self.ofs_vtxlist = struct.unpack_from("<iI", data, p + 8)
        self.n_primlist, self.ofs_primlist = struct.unpack_from("<iI", data, p + 16)
        self.n_node, self.max_node_depth, self.ofs_node = struct.unpack_from("<iiI", data, p + 24)
        self.n_mtxpal, self.n_subobj, self.ofs_subobj = struct.unpack_from("<iiI", data, p + 36)
        self.n_tex, self.ftype, self.version = struct.unpack_from("<iIi", data, p + 48)
        self.bbox = struct.unpack_from("<3f", data, p + 60)

    @property
    def is_skinned(self) -> bool:
        """A node tree deeper than one level means a real skeleton."""
        return self.n_node > 1 and self.max_node_depth > 1

    @property
    def is_locator(self) -> bool:
        """A null object: nodes but no geometry, used as a positional marker.

        Cutscenes anchor their camera and actors to these — `CAMERA_POS.ZNO`,
        `SONIC_POS.ZNO`, `TAILS_POS.ZNO`. They carry a zero radius and bounding
        box, and `ftype` bit 0 (set on every model that owns vertex data) is
        clear.
        """
        return self.n_vtxlist == 0 and self.n_primlist == 0 and self.n_node > 0

    def __repr__(self):
        return (
            f"<NnObject nodes={self.n_node} materials={self.n_material} "
            f"vtxlists={self.n_vtxlist} primlists={self.n_primlist} tex={self.n_tex}>"
        )


def read_object(data: bytes, f: NnFile) -> NnObject | None:
    """Locate and parse the NZOB object header of a parsed NN file."""
    chunk = None
    for candidate in f.chunks:
        if candidate.name[2:] == "OB":
            chunk = candidate
            break
    if chunk is None:
        return None
    if f.chunks[0].size < 24:
        raise NnError("file header too short to locate the data base")
    _n, ofs_data, _sz, _nof0, _snof0, _ver = struct.unpack_from("<6I", f.chunks[0].payload, 0)
    if chunk.size < 8:
        raise NnError("object chunk too short for its data header")
    (ofs_main,) = struct.unpack_from("<i", chunk.payload, 0)
    at = ofs_data + ofs_main
    if at < 0 or at + NnObject.SIZE > len(data):
        raise NnError(f"object header at {at:#x} lies outside the file")
    return NnObject(data, at)


def parse(data: bytes) -> NnFile:
    """Walk the chunk list. Raises NnError on anything structurally wrong."""
    if len(data) < 8:
        raise NnError("too short to hold a chunk header")
    if data[:4] not in HEADER_TAGS:
        raise NnError(f"not an NN container (tag={data[:4]!r})")

    chunks: list[Chunk] = []
    offset = 0
    while offset + 8 <= len(data):
        tag = data[offset : offset + 4]
        (size,) = struct.unpack_from("<I", data, offset + 4)
        if not all(32 <= c < 127 for c in tag):
            raise NnError(f"non-printable tag {tag!r} at {offset:#x}")
        end = offset + 8 + size
        if end > len(data):
            raise NnError(f"chunk {tag!r} at {offset:#x} overruns ({size} bytes)")
        chunks.append(Chunk(tag, offset, size, data[offset + 8 : end]))
        if tag == END_TAG:
            return NnFile(chunks)
        offset = end
    raise NnError("ran off the end without finding NEND")


def check_header(data: bytes, f: NnFile) -> list[str]:
    """Cross-check the NZIF fields against where the chunks actually landed."""
    problems = []
    head = f.chunks[0]
    if head.size < 24:
        return [f"{head.name} payload is only {head.size} bytes"]
    _n, ofs_data, _size, ofs_nof0, _nof0_size, _ver = struct.unpack_from("<6I", head.payload, 0)
    if len(f.chunks) > 1 and ofs_data != f.chunks[1].offset:
        problems.append(f"header says data at {ofs_data:#x}, first data chunk is at {f.chunks[1].offset:#x}")
    nof0 = f.find("NOF0")
    if nof0 and ofs_nof0 != nof0.offset:
        problems.append(f"header says NOF0 at {ofs_nof0:#x}, found at {nof0.offset:#x}")
    return problems


def cmd_show(args) -> int:
    archive = amb.load(args.archive)
    matches = [e for e in archive if args.name.upper() in e.name.upper()]
    if not matches:
        print(f"no entry matching {args.name!r}", file=sys.stderr)
        return 1
    for entry in matches[: args.limit]:
        data = archive.read(entry)
        try:
            f = parse(data)
        except NnError as exc:
            print(f"{entry.name}: {exc}", file=sys.stderr)
            continue
        label = entry.name.replace(chr(92), "/").rsplit("/", 1)[-1]
        print(f"{label}  {len(data)} bytes  kind={f.kind()}  origin={f.filename}")
        for chunk in f:
            print(f"    {chunk.name}  @{chunk.offset:#07x}  {chunk.size} bytes")
        for problem in check_header(data, f):
            print(f"    ! {problem}")
        try:
            obj = read_object(data, f)
        except NnError as exc:
            print(f"    ! object: {exc}")
            obj = None
        if obj:
            cx, cy, cz = obj.center
            bx, by, bz = obj.bbox
            print(f"    object  centre ({cx:.2f}, {cy:.2f}, {cz:.2f})  radius {obj.radius:.2f}"
                  f"  bbox ({bx:.2f}, {by:.2f}, {bz:.2f})")
            print(f"            {obj.n_material} materials, {obj.n_vtxlist} vertex lists, "
                  f"{obj.n_primlist} primitive lists, {obj.n_tex} textures")
            print(f"            {obj.n_node} nodes (depth {obj.max_node_depth}), "
                  f"{obj.n_mtxpal} matrix palettes, {obj.n_subobj} subobjects"
                  f"{'  [skinned]' if obj.is_skinned else ''}")
    return 0


def cmd_objects(args) -> int:
    """Parse the object header of every model under a tree and sanity-check it."""
    ok = bad = skinned = locators = 0
    failures: list[str] = []
    totals = Counter()
    for path in amb._iter_amb_files(args.root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        for entry in archive:
            if not entry.name.upper().endswith(".ZNO"):
                continue
            data = archive.read(entry)
            try:
                obj = read_object(data, parse(data))
                if obj is None:
                    raise NnError("no object chunk")
                # A model with no geometry, a negative count or an impossible
                # radius means the layout is wrong, not that the model is odd.
                if min(obj.n_material, obj.n_vtxlist, obj.n_primlist, obj.n_node,
                       obj.n_tex, obj.n_subobj, obj.n_mtxpal) < 0:
                    raise NnError("negative count")
                if obj.n_node == 0:
                    raise NnError("model with no nodes")
                if obj.is_locator and obj.radius != 0.0:
                    raise NnError("geometry-less object with a non-zero radius")
                if not (0.0 <= obj.radius < 1e6):
                    raise NnError(f"implausible radius {obj.radius}")
                for ofs in (obj.ofs_material, obj.ofs_vtxlist, obj.ofs_primlist,
                            obj.ofs_node, obj.ofs_subobj):
                    if ofs and ofs >= len(data):
                        raise NnError(f"list offset {ofs:#x} past end of file")
                ok += 1
                skinned += obj.is_skinned
                locators += obj.is_locator
                totals["nodes"] += obj.n_node
                totals["materials"] += obj.n_material
                totals["vertex lists"] += obj.n_vtxlist
                totals["primitive lists"] += obj.n_primlist
            except NnError as exc:
                bad += 1
                if len(failures) < 10:
                    failures.append(f"{os.path.basename(path)}::{entry.name}: {exc}")

    print(f"{ok} object headers parsed and sane, {bad} failed")
    if ok:
        print(f"{skinned} are skinned (node tree deeper than one level), "
              f"{locators} are geometry-less locators")
        print("\ntotals across every model:")
        for key, n in totals.most_common():
            print(f"  {key:<17} {n}")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    return 1 if bad else 0


def cmd_verify(args) -> int:
    ok = bad = 0
    kinds: Counter = Counter()
    tags: Counter = Counter()
    failures: list[str] = []
    exts = tuple(e.upper() for e in args.ext.split(","))

    for path in amb._iter_amb_files(args.root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        for entry in archive:
            if not entry.name.upper().endswith(exts):
                continue
            try:
                f = parse(archive.read(entry))
                problems = check_header(archive.read(entry), f)
                if problems:
                    raise NnError(problems[0])
                ok += 1
                kinds[f.kind()] += 1
                for chunk in f:
                    tags[chunk.name] += 1
            except NnError as exc:
                bad += 1
                if len(failures) < 10:
                    failures.append(f"{os.path.basename(path)}::{entry.name}: {exc}")

    print(f"{ok} NN containers parsed cleanly, {bad} failed")
    if kinds:
        print("\npayload kinds:")
        for kind, n in kinds.most_common():
            print(f"  {kind:<8} {n}")
        print("\nchunk tags seen:")
        for tag, n in tags.most_common():
            print(f"  {tag:<8} {n}")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    return 1 if bad else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="SEGA NN container tool (Sonic 4 Episode II)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("show", help="dump the chunk list of a model or motion")
    p.add_argument("archive")
    p.add_argument("name", help="substring of the entry name")
    p.add_argument("--limit", type=int, default=3)
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("objects", help="parse and sanity-check every model's object header")
    p.add_argument("root")
    p.set_defaults(func=cmd_objects)

    p = sub.add_parser("verify", help="parse every NN container under a tree")
    p.add_argument("root")
    p.add_argument("--ext", default=".ZNO,.ZNM,.ZNV,.XNM", help="comma separated")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
