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


# Vertex format bits, deduced by correlating every observed flag word against its
# stride across 2,700 vertex lists. Each combination accounts for its stride
# exactly (0x10003 -> 32, 0x1001b -> 40, 0x10019 -> 28, 0x10001 -> 20).
VTX_POSITION = 0x00001  # 3 floats
VTX_NORMAL = 0x00002  # 3 floats
VTX_DIFFUSE = 0x00008  # 4 bytes
VTX_SPECULAR = 0x00010  # 4 bytes
VTX_TEXCOORD = 0x10000  # 2 floats

PRIM_TRIANGLE_STRIP = 0x4810  # the only mode in the entire build


class VertexList:
    """One `NZOB` vertex buffer.

        +0x00  u32  format flags
        +0x04  u32  unknown — points near the descriptor itself, OPEN
        +0x08  u32  stride in bytes
        +0x0C  u32  vertex count
        +0x10  u32  offset of the vertex buffer
    """

    SIZE = 0x14
    __slots__ = ("fmt", "unknown", "stride", "count", "ofs_buffer", "_data", "_base")

    def __init__(self, data: bytes, base: int, offset: int):
        (self.fmt, self.unknown, self.stride, self.count,
         self.ofs_buffer) = struct.unpack_from("<5I", data, offset)
        self._data, self._base = data, base

    @property
    def has_position(self) -> bool:
        return bool(self.fmt & VTX_POSITION)

    def attribute_offset(self, attribute: int) -> int | None:
        """Byte offset of an attribute within a vertex, or None if absent.

        Attributes are laid out in a fixed order and packed with no padding,
        which is why every observed flag combination accounts for its stride
        exactly.
        """
        if not self.fmt & attribute:
            return None
        offset = 0
        for bit, size in ((VTX_POSITION, 12), (VTX_NORMAL, 12),
                          (VTX_DIFFUSE, 4), (VTX_SPECULAR, 4), (VTX_TEXCOORD, 8)):
            if bit == attribute:
                return offset
            if self.fmt & bit:
                offset += size
        return None

    def _read(self, attribute: int, fmt: str, n: int) -> list[tuple]:
        at = self.attribute_offset(attribute)
        if at is None:
            return []
        start = self._base + self.ofs_buffer
        end = start + self.stride * self.count
        if start < 0 or end > len(self._data):
            raise NnError(f"vertex buffer {start:#x}..{end:#x} outside the file")
        return [
            struct.unpack_from(fmt, self._data, start + i * self.stride + at)
            for i in range(self.count)
        ]

    def positions(self) -> list[tuple[float, float, float]]:
        return self._read(VTX_POSITION, "<3f", 3)

    def normals(self) -> list[tuple[float, float, float]]:
        return self._read(VTX_NORMAL, "<3f", 3)

    def texcoords(self) -> list[tuple[float, float]]:
        return self._read(VTX_TEXCOORD, "<2f", 2)

    def __repr__(self):
        return f"<VertexList fmt={self.fmt:#x} stride={self.stride} count={self.count}>"


class PrimitiveList:
    """One `NZOB` index buffer.

        +0x00  u32  mode (always 0x4810, triangle strip)
        +0x04  u32  total index count
        +0x08  u32  strip count
        +0x0C  u32  offset of the per-strip index counts
        +0x10  u32  offset of the u16 index data
    """

    SIZE = 0x14
    __slots__ = ("mode", "total", "n_strips", "ofs_counts", "ofs_indices", "_data", "_base")

    def __init__(self, data: bytes, base: int, offset: int):
        (self.mode, self.total, self.n_strips, self.ofs_counts,
         self.ofs_indices) = struct.unpack_from("<5I", data, offset)
        self._data, self._base = data, base

    def strips(self) -> list[list[int]]:
        """Index data split into strips."""
        counts_at = self._base + self.ofs_counts
        if counts_at + self.n_strips * 4 > len(self._data):
            raise NnError("strip count table outside the file")
        counts = struct.unpack_from(f"<{self.n_strips}I", self._data, counts_at)

        at = self._base + self.ofs_indices
        out = []
        for n in counts:
            if at + n * 2 > len(self._data):
                raise NnError("index data outside the file")
            out.append(list(struct.unpack_from(f"<{n}H", self._data, at)))
            at += n * 2
        return out

    def triangles(self) -> list[tuple[int, int, int]]:
        """Expand every strip to triangles, flipping winding on odd steps."""
        tris = []
        for strip in self.strips():
            for i in range(len(strip) - 2):
                a, b, c = strip[i], strip[i + 1], strip[i + 2]
                if a == b or b == c or a == c:
                    continue  # degenerate, used to stitch strips together
                tris.append((a, b, c) if i % 2 == 0 else (a, c, b))
        return tris

    def __repr__(self):
        return f"<PrimitiveList mode={self.mode:#x} strips={self.n_strips} indices={self.total}>"


def read_relocations(data: bytes, f: NnFile, base: int = 0x20) -> list[int]:
    """Offsets the engine patches at load time, from the `NOF0` chunk.

    Layout: `u32 count`, `u32 reserved`, then `count` byte offsets relative to
    the data base.

    Recovered from the loader itself in `Sonic.exe` at `0x006c6c33`:

        mov ecx, dword [edi]            ; offset from the table
        shr ecx, 2                      ; /4, so it indexes u32s
        add dword [eax + ecx*4], eax    ; *(base + offset) += base

    So each listed word holds a base-relative offset that gets turned into an
    absolute pointer in place. **The file layout is the in-memory struct
    layout** — Episode II relocates rather than re-parsing, which is why every
    internal offset is relative to `OfsData`.

    Usefully, this doubles as a map of which words in the file are pointers.
    """
    chunk = f.find("NOF0")
    if chunk is None or chunk.size < 8:
        return []
    count, _reserved = struct.unpack_from("<II", chunk.payload, 0)
    if 8 + count * 4 > chunk.size:
        raise NnError(f"NOF0 declares {count} entries but the chunk is {chunk.size} bytes")
    offsets = struct.unpack_from(f"<{count}I", chunk.payload, 8)
    for off in offsets:
        if off % 4:
            raise NnError(f"relocation offset {off:#x} is not word aligned")
        if base + off + 4 > len(data):
            raise NnError(f"relocation offset {off:#x} outside the file")
    return list(offsets)


class TextureRef:
    """One entry of the `NZTL` texture list.

        +0x00  u32  type flags
        +0x04  u32  offset of the NUL-terminated filename
        +0x08  u16  minification filter
        +0x0A  u16  magnification filter
        +0x0C  u32  global index
        +0x10  u32  bank

    20 bytes — the same size as Episode I's `NNS_TEXFILE`, for once.
    """

    SIZE = 0x14
    __slots__ = ("ftype", "name", "min_filter", "mag_filter", "global_index", "bank")

    def __init__(self, data: bytes, base: int, offset: int):
        (self.ftype, ofs_name, self.min_filter, self.mag_filter,
         self.global_index, self.bank) = struct.unpack_from("<IIHHII", data, offset)
        self.name = ""
        if ofs_name:
            at = base + ofs_name
            end = data.find(b"\0", at)
            if 0 <= at < len(data):
                self.name = data[at: end if end != -1 else len(data)].decode("ascii", "replace")

    def __repr__(self):
        return f"<TextureRef {self.name!r}>"


def read_textures(data: bytes, f: NnFile, base: int = 0x20) -> list[TextureRef]:
    """Texture filenames a model references, from its `NZTL` chunk."""
    chunk = None
    for candidate in f.chunks:
        if candidate.name[2:] == "TL":
            chunk = candidate
            break
    if chunk is None:
        return []
    if chunk.size < 8:
        raise NnError("texture list chunk too short")
    (ofs_main,) = struct.unpack_from("<i", chunk.payload, 0)
    at = base + ofs_main
    if at + 8 > len(data):
        raise NnError(f"texture list root at {at:#x} outside the file")
    count, ofs_list = struct.unpack_from("<iI", data, at)
    if count < 0 or base + ofs_list + count * TextureRef.SIZE > len(data):
        raise NnError(f"texture list of {count} overruns the file")
    return [
        TextureRef(data, base, base + ofs_list + i * TextureRef.SIZE)
        for i in range(count)
    ]


class Node:
    """One entry of the node tree — a transform and its links.

        +0x00  u32       type flags
        +0x04  s16       matrix palette index
        +0x06  s16       parent index (-1 on the root)
        +0x08  s16       first child index (-1 for a leaf)
        +0x0A  s16       next sibling index (-1 if last)
        +0x0C  float[3]  translation
        +0x18  s32[3]    rotation, as fixed-point angles
        +0x24  float[3]  scaling
        +0x30  float[16] inverse bind matrix
        +0x70  32 bytes  unknown, zero in every observed model

    **144 bytes, where Episode I's `NNS_NODE` is 112.** Verified by walking the
    tree on all 846 multi-node models: every parent, child and sibling index
    lands in range, each model has exactly one root, and every scale is finite
    and non-zero. Strides of 136 and 152 fail on 846 and 845 models respectively,
    so the value is not merely permissive.
    """

    SIZE = 0x90
    __slots__ = ("ftype", "i_matrix", "i_parent", "i_child", "i_sibling",
                 "translation", "rotation", "scaling")

    def __init__(self, data: bytes, offset: int):
        (self.ftype, self.i_matrix, self.i_parent,
         self.i_child, self.i_sibling) = struct.unpack_from("<Ihhhh", data, offset)
        self.translation = struct.unpack_from("<3f", data, offset + 0x0C)
        self.rotation = struct.unpack_from("<3i", data, offset + 0x18)
        self.scaling = struct.unpack_from("<3f", data, offset + 0x24)

    @property
    def is_root(self) -> bool:
        return self.i_parent == -1

    def __repr__(self):
        return (f"<Node parent={self.i_parent} child={self.i_child} "
                f"sibling={self.i_sibling} t={self.translation}>")


def read_nodes(data: bytes, obj: NnObject, base: int = 0x20) -> list[Node]:
    nodes = []
    for i in range(obj.n_node):
        at = base + obj.ofs_node + i * Node.SIZE
        if at + Node.SIZE > len(data):
            raise NnError(f"node {i} outside the file")
        nodes.append(Node(data, at))
    return nodes


class MeshSet:
    """Binds a vertex list to a primitive list, a material and a node.

        +0x00  float[3]  bounding sphere centre
        +0x0C  float     bounding sphere radius
        +0x10  s32       node index
        +0x14  s32       matrix index
        +0x18  s32       material index
        +0x1C  s32       vertex list index
        +0x20  s32       primitive list index
        +0x24  u32       reserved

    This is what pairs geometry up. Vertex and primitive lists are *not*
    positionally matched — a model can have 27 of each and still pair them in a
    different order, so anything that assumes `vtx[i]` goes with `prim[i]` will
    produce out-of-range indices on roughly half the corpus.

    **40 bytes, where Episode I's `NNS_MESHSET` is 48.** Episode I carries three
    reserved words and the Direct3D build carries one. Measured, not assumed: in
    every subobject the gap between the mesh set array and the texture index list
    that follows it divides exactly by the mesh set count, giving 40 on models
    with 1, 2 and 24 mesh sets alike.
    """

    SIZE = 0x28
    __slots__ = ("center", "radius", "i_node", "i_matrix", "i_material",
                 "i_vtxlist", "i_primlist")

    def __init__(self, data: bytes, offset: int):
        cx, cy, cz, self.radius = struct.unpack_from("<4f", data, offset)
        self.center = (cx, cy, cz)
        (self.i_node, self.i_matrix, self.i_material,
         self.i_vtxlist, self.i_primlist) = struct.unpack_from("<5i", data, offset + 0x10)

    def __repr__(self):
        return (f"<MeshSet vtx={self.i_vtxlist} prim={self.i_primlist} "
                f"mat={self.i_material} node={self.i_node}>")


class SubObject:
    """A drawable group of mesh sets.

        +0x00  u32  type flags
        +0x04  s32  mesh set count
        +0x08  u32  mesh set list offset
        +0x0C  s32  texture count
        +0x10  u32  texture index list offset
    """

    SIZE = 0x14
    __slots__ = ("ftype", "n_meshset", "ofs_meshset", "n_tex", "ofs_tex")

    def __init__(self, data: bytes, offset: int):
        (self.ftype, self.n_meshset, self.ofs_meshset,
         self.n_tex, self.ofs_tex) = struct.unpack_from("<IiIiI", data, offset)


def read_meshsets(data: bytes, obj: NnObject, base: int = 0x20) -> list[MeshSet]:
    """Every mesh set across every subobject, in draw order."""
    out: list[MeshSet] = []
    for i in range(obj.n_subobj):
        at = base + obj.ofs_subobj + i * SubObject.SIZE
        if at + SubObject.SIZE > len(data):
            raise NnError(f"subobject {i} outside the file")
        sub = SubObject(data, at)
        if not sub.ofs_meshset:
            continue
        for j in range(sub.n_meshset):
            m = base + sub.ofs_meshset + j * MeshSet.SIZE
            if m + MeshSet.SIZE > len(data):
                raise NnError(f"mesh set {j} of subobject {i} outside the file")
            out.append(MeshSet(data, m))
    return out


def _pointer_array(data: bytes, base: int, ofs_list: int, count: int) -> list[int]:
    """Read `count` {u32 fType, u32 offset} pairs, returning the offsets."""
    out = []
    for i in range(count):
        at = base + ofs_list + i * 8
        if at + 8 > len(data):
            raise NnError(f"pointer array entry {i} outside the file")
        _ftype, offset = struct.unpack_from("<2I", data, at)
        out.append(offset)
    return out


def read_vertex_lists(data: bytes, obj: NnObject, base: int = 0x20) -> list[VertexList]:
    lists = []
    for offset in _pointer_array(data, base, obj.ofs_vtxlist, obj.n_vtxlist):
        at = base + offset
        if at + VertexList.SIZE > len(data):
            raise NnError(f"vertex descriptor at {at:#x} outside the file")
        lists.append(VertexList(data, base, at))
    return lists


def read_primitive_lists(data: bytes, obj: NnObject, base: int = 0x20) -> list[PrimitiveList]:
    lists = []
    for offset in _pointer_array(data, base, obj.ofs_primlist, obj.n_primlist):
        at = base + offset
        if at + PrimitiveList.SIZE > len(data):
            raise NnError(f"primitive descriptor at {at:#x} outside the file")
        lists.append(PrimitiveList(data, base, at))
    return lists


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
        try:
            textures = read_textures(data, f)
        except NnError:
            textures = []
        if textures:
            print(f"    textures: {', '.join(t.name for t in textures if t.name)}")
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


def cmd_export(args) -> int:
    """Write a model's geometry to Wavefront OBJ."""
    archive = amb.load(args.archive)
    matches = [e for e in archive if args.name.upper() in e.name.upper()
               and e.name.upper().endswith(".ZNO")]
    if not matches:
        print(f"no .ZNO matching {args.name!r}", file=sys.stderr)
        return 1

    os.makedirs(args.dest, exist_ok=True)
    for entry in matches[: args.limit]:
        data = archive.read(entry)
        f = parse(data)
        obj = read_object(data, f)
        if obj is None or obj.is_locator:
            print(f"  skipping {entry.name} (no geometry)")
            continue
        vlists = read_vertex_lists(data, obj)
        plists = read_primitive_lists(data, obj)
        meshsets = read_meshsets(data, obj)

        label = entry.name.replace(chr(92), "/").rsplit("/", 1)[-1]
        out = os.path.join(args.dest, os.path.splitext(label)[0] + ".obj")
        verts = tris = 0
        with open(out, "w", encoding="ascii") as fp:
            fp.write(f"# {label} - extracted from Sonic 4 Episode II\n")
            fp.write(f"# {len(meshsets)} mesh sets, {obj.n_vtxlist} vertex lists, "
                     f"{obj.n_primlist} primitive lists\n")
            for i, mesh in enumerate(meshsets):
                if not (0 <= mesh.i_vtxlist < len(vlists)):
                    continue
                if not (0 <= mesh.i_primlist < len(plists)):
                    continue
                vl = vlists[mesh.i_vtxlist]
                origin = verts
                for x, y, z in vl.positions():
                    fp.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
                    verts += 1
                uvs = vl.texcoords()
                for u, v in uvs:
                    fp.write(f"vt {u:.6f} {1.0 - v:.6f}\n")  # OBJ's V axis points up
                normals = vl.normals()
                for nx, ny, nz in normals:
                    fp.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")

                fp.write(f"g mesh{i}_mat{mesh.i_material}\n")
                for tri in plists[mesh.i_primlist].triangles():
                    parts = []
                    for idx in tri:
                        n = origin + idx + 1
                        if uvs and normals:
                            parts.append(f"{n}/{n}/{n}")
                        elif uvs:
                            parts.append(f"{n}/{n}")
                        elif normals:
                            parts.append(f"{n}//{n}")
                        else:
                            parts.append(str(n))
                    fp.write("f " + " ".join(parts) + "\n")
                    tris += 1
        print(f"  {out}  {verts} vertices, {tris} triangles, {len(meshsets)} mesh sets")
    return 0


def cmd_textures(args) -> int:
    """Check every model's texture references resolve to a real DDS.

    A model's `NZTL` names the textures it wants. Those names should exist as
    `.DDS` somewhere in the game — usually in the zone's `_T` archive rather than
    beside the model — so this walks the whole build, collects every DDS name,
    and then checks each model's references against that set.
    """
    dds: set[str] = set()
    for path in amb._iter_amb_files(args.root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        for entry in archive:
            if entry.name.upper().endswith(".DDS"):
                dds.add(entry.name.replace(chr(92), "/").rsplit("/", 1)[-1].upper())
    print(f"{len(dds)} distinct DDS filenames across the build")

    models = refs = resolved = 0
    missing: Counter = Counter()
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
                textures = read_textures(data, parse(data))
            except NnError as exc:
                missing[f"<parse failed: {exc}>"] += 1
                continue
            models += 1
            for tex in textures:
                if not tex.name:
                    continue
                refs += 1
                if tex.name.upper() in dds:
                    resolved += 1
                else:
                    missing[tex.name.upper()] += 1

    pct = (resolved / refs * 100) if refs else 0.0
    print(f"{models} models carry {refs} texture references")
    print(f"{resolved} resolve to a real DDS ({pct:.1f}%), {refs - resolved} do not")
    for name, n in missing.most_common(10):
        print(f"  ! {name} x{n}")
    return 0


def cmd_geometry(args) -> int:
    """Extract geometry from every model under a tree and check it holds up."""
    ok = bad = skipped = 0
    verts = tris = 0
    failures: list[str] = []
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
                if obj is None or obj.is_locator:
                    skipped += 1
                    continue
                vlists = read_vertex_lists(data, obj)
                plists = read_primitive_lists(data, obj)
                meshsets = read_meshsets(data, obj)
                nodes = read_nodes(data, obj)
                if not meshsets:
                    raise NnError("no mesh sets")
                roots = sum(1 for n in nodes if n.is_root)
                if roots != 1:
                    raise NnError(f"{roots} root nodes, expected exactly one")
                for j, node in enumerate(nodes):
                    for link in (node.i_parent, node.i_child, node.i_sibling):
                        if link < -1 or link >= len(nodes):
                            raise NnError(f"node {j} link {link} out of range")
                for mesh in meshsets:
                    if not (0 <= mesh.i_vtxlist < len(vlists)):
                        raise NnError(f"vertex list index {mesh.i_vtxlist} out of range")
                    if not (0 <= mesh.i_primlist < len(plists)):
                        raise NnError(f"primitive list index {mesh.i_primlist} out of range")
                    pl = plists[mesh.i_primlist]
                    if pl.mode != PRIM_TRIANGLE_STRIP:
                        raise NnError(f"unexpected primitive mode {pl.mode:#x}")
                    limit = vlists[mesh.i_vtxlist].count
                    for tri in pl.triangles():
                        # An index past its own vertex list means the mesh set
                        # binding is being read wrongly.
                        if max(tri) >= limit:
                            raise NnError(f"index {max(tri)} >= {limit} vertices")
                        tris += 1
                for vl in vlists:
                    verts += len(vl.positions())
                ok += 1
            except (NnError, struct.error) as exc:
                bad += 1
                if len(failures) < 10:
                    failures.append(f"{os.path.basename(path)}::{entry.name}: {exc}")

    print(f"{ok} models yielded geometry, {bad} failed, {skipped} locators skipped")
    print(f"{verts:,} vertices and {tris:,} triangles extracted")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    return 1 if bad else 0


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

    p = sub.add_parser("export", help="write a model's geometry to Wavefront OBJ")
    p.add_argument("archive")
    p.add_argument("name", help="substring of the entry name")
    p.add_argument("dest")
    p.add_argument("--limit", type=int, default=5)
    p.set_defaults(func=cmd_export)

    p = sub.add_parser("textures", help="check model texture references resolve to real DDS")
    p.add_argument("root")
    p.set_defaults(func=cmd_textures)

    p = sub.add_parser("geometry", help="extract geometry from every model under a tree")
    p.add_argument("root")
    p.set_defaults(func=cmd_geometry)

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
