#!/usr/bin/env python3
"""Recover the asset manifest from Sonic.exe.

The engine loads archives by number, not by name. A global table pairs each
numeric asset id with the archive path it loads, as 20-byte records:

    +0x00  ptr    path string
    +0x04  ptr    buffer, filled at load time
    +0x08  u32    reserved, always zero
    +0x0C  ptr    loader function
    +0x10  u32    asset id

This is the load-time counterpart to the spawn dispatch table that
`tools/objects.py` recovers: that says which *code* an object id runs, this says
which *archive* an asset id loads. Together they are how a placement becomes a
thing on screen — the spawn function loads its assets by these ids.

Reproduces `analysis/asset-manifest.json`.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import struct
import sys

RECORD_SIZE = 20
MAX_ID = 20000          # ids run into the low thousands; this rejects noise


class Image:
    def __init__(self, data: bytes):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        opt = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            at = pe + 24 + opt + i * 40
            name = data[at:at + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, at + 8)
            self.sections.append((name, vaddr, vsize, raddr, rsize))
        text = next(s for s in self.sections if s[0] == ".text")
        self.text_lo = self.base + text[1]
        self.text_hi = self.text_lo + text[2]

    def offset(self, va: int) -> int | None:
        for _n, vaddr, vsize, raddr, rsize in self.sections:
            rel = va - self.base - vaddr
            if 0 <= rel < vsize and rel < rsize:
                return raddr + rel
        return None

    def is_code(self, va: int) -> bool:
        return self.text_lo <= va < self.text_hi

    def cstring(self, va: int) -> str | None:
        at = self.offset(va)
        if at is None:
            return None
        end = self.data.find(b"\0", at, at + 128)
        if end <= at:
            return None
        text = self.data[at:end].decode("ascii", "replace")
        return text if re.fullmatch(r"[A-Za-z0-9_./]+\.[A-Za-z0-9]+", text) else None


def manifest(image: Image) -> dict[int, str]:
    found = {}
    for name, vaddr, _vsize, raddr, rsize in image.sections:
        if name not in (".rdata", ".data"):
            continue
        for off in range(raddr, raddr + rsize - RECORD_SIZE, 4):
            path_ptr, _buf, reserved, loader, asset_id = \
                struct.unpack_from("<IIIII", image.data, off)
            if reserved != 0 or not image.is_code(loader):
                continue
            if not (0 < asset_id < MAX_ID):
                continue
            path = image.cstring(path_ptr)
            if path and ".AMB" in path.upper():
                found[asset_id] = path
    return found


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", nargs="?", default="Sonic.exe")
    ap.add_argument("--json")
    args = ap.parse_args(argv)

    if not os.path.exists(args.exe):
        print(f"{args.exe}: not found", file=sys.stderr)
        return 1

    image = Image(open(args.exe, "rb").read())
    assets = manifest(image)
    print(f"{len(assets)} asset ids mapped to archive paths")

    kinds = collections.Counter(
        path.rsplit("_", 1)[-1].split(".")[0].upper() for path in assets.values())
    print("  by kind:", ", ".join(f"{k}:{c}" for k, c in kinds.most_common(8)))
    dirs = collections.Counter(path.split("/")[0] for path in assets.values())
    print("  by top directory:", ", ".join(f"{k}:{c}" for k, c in dirs.most_common(6)))

    if args.json:
        os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
        json.dump({str(k): v for k, v in sorted(assets.items())},
                  open(args.json, "w"), indent=1)
        print(f"  wrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
