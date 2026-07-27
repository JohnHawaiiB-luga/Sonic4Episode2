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

The NZIF payload is six u32s:

    +0x00  version (2 in every observed file)
    +0x04  offset of the first data chunk (always 0x20, i.e. straight after)
    +0x08  total size of the data chunks
    +0x0C  offset of the NOF0 relocation chunk
    +0x10  size of the NOF0 chunk including its header
    +0x14  chunk count

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
    _ver, ofs_data, _size, ofs_nof0, _nof0_size, _n = struct.unpack_from("<6I", head.payload, 0)
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
    return 0


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

    p = sub.add_parser("verify", help="parse every NN container under a tree")
    p.add_argument("root")
    p.add_argument("--ext", default=".ZNO,.ZNM,.ZNV,.XNM", help="comma separated")
    p.set_defaults(func=cmd_verify)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
