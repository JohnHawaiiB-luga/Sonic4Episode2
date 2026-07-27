#!/usr/bin/env python3
"""Recover the object catalogue from Sonic.exe.

Stage `.EV` records carry a numeric object id and nothing else, so the meaning of
those numbers has to come out of the executable. It comes from three places:

  * a spawn dispatch table indexed directly by object id, which turns an id into
    the function that builds it;
  * the argument each of those functions passes to the shared object constructor,
    which is the engine's own instance size and scheduler priority;
  * name strings referenced from the function, for the objects that load a named
    asset.

Nothing here needs the game to run, and nothing is guessed: every number below is
either read from the file or derived from an address that was.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import struct
import sys

# Addresses established by disassembly; see docs/FORMAT-OBJECTS.md.
DISPATCH_TABLE = 0x007031C8
DISPATCH_SLOTS = 803
OBJECT_CTOR = 0x004834C0

# A function body is scanned to the first `int3` run, but that byte triple also
# turns up inside immediates, so never believe a body shorter than this.
BODY_FLOOR = 512
BODY_CAP = 6000

# Task-manager, mutex and SDK strings live near object code and are not names.
NOT_A_NAME = re.compile(r"^(GM_|MUTEX_|NND_|AMB$|E\d{6,})|^[a-zA-Z0-9]{1,3}$")

# A name reachable from more handlers than this is a shared helper's, not an
# object's.
SHARED_LIMIT = 6

IDENTIFIER = re.compile(rb"[A-Za-z][A-Za-z0-9_]{2,31}\x00")
CALL_OR_JMP = re.compile(rb"[\xe8\xe9](....)", re.S)


class Image:
    """Just enough PE to turn a virtual address into a file offset."""

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
        for _, vaddr, vsize, raddr, rsize in self.sections:
            rel = va - self.base - vaddr
            if 0 <= rel < vsize and rel < rsize:
                return raddr + rel
        return None

    def u32(self, va: int) -> int | None:
        at = self.offset(va)
        return struct.unpack_from("<I", self.data, at)[0] if at is not None else None

    def is_code(self, va: int) -> bool:
        return self.text_lo <= va < self.text_hi

    def strings(self) -> dict[int, str]:
        """Every identifier-shaped C string in the data sections, by address."""
        found = {}
        for name, vaddr, _vsize, raddr, rsize in self.sections:
            if name not in (".rdata", ".data"):
                continue
            for m in IDENTIFIER.finditer(self.data[raddr:raddr + rsize]):
                found[self.base + vaddr + m.start()] = m.group()[:-1].decode()
        return found

    def body(self, fn: int) -> tuple[int, int] | None:
        at = self.offset(fn)
        if at is None:
            return None
        end = self.data.find(b"\xcc\xcc\xcc", at, at + BODY_CAP)
        return at, (max(end, at + BODY_FLOOR) if end > 0 else at + BODY_CAP)


def dispatch(image: Image) -> dict[int, list[int]]:
    """Object ids grouped by the spawn function they land on."""
    groups: dict[int, list[int]] = collections.defaultdict(list)
    for oid in range(DISPATCH_SLOTS):
        fn = image.u32(DISPATCH_TABLE + oid * 4)
        if fn and image.is_code(fn):
            groups[fn].append(oid)
    return dict(groups)


def constructor_args(image: Image, fn: int) -> tuple[int, int] | None:
    """The (size, priority) a handler passes to the shared object constructor."""
    span = image.body(fn)
    if span is None:
        return None
    start, end = span
    blob = image.data[start:end]
    for call in re.finditer(rb"\xe8(....)", blob, re.S):
        rel = struct.unpack("<i", call.group(1))[0]
        if fn + call.start() + 5 + rel != OBJECT_CTOR:
            continue
        before = blob[max(0, call.start() - 64):call.start()]
        pushed = [struct.unpack("<I", p.group(1))[0]
                  for p in re.finditer(rb"\x68(....)", before, re.S)]
        if len(pushed) >= 2:
            return pushed[-1], pushed[-2]
    return None


def referenced_strings(image: Image, fn: int, pool: dict[int, str], depth: int):
    """(call depth, offset, string) for every name a handler can reach."""
    out, seen, stack = [], set(), [(fn, 0)]
    while stack:
        at, level = stack.pop()
        if at in seen or not image.is_code(at):
            continue
        seen.add(at)
        span = image.body(at)
        if span is None:
            continue
        start, end = span
        for p in range(start, min(end, len(image.data)) - 4):
            name = pool.get(struct.unpack_from("<I", image.data, p)[0])
            if name:
                out.append((level, p - start, name))
        if level < depth:
            for call in CALL_OR_JMP.finditer(image.data[start:end]):
                rel = struct.unpack("<i", call.group(1))[0]
                stack.append((at + call.start() + 5 + rel, level + 1))
    return out


def build(image: Image, depth: int = 1) -> dict:
    pool = image.strings()
    groups = dispatch(image)

    reachable, seen_by = {}, collections.Counter()
    for fn in groups:
        hits = [h for h in referenced_strings(image, fn, pool, depth)
                if not NOT_A_NAME.match(h[2])]
        reachable[fn] = hits
        for name in {h[2] for h in hits}:
            seen_by[name] += 1
    shared = {n for n, c in seen_by.items() if c > SHARED_LIMIT}

    handlers = {}
    for fn, ids in sorted(groups.items()):
        hits = sorted((h for h in reachable[fn] if h[2] not in shared),
                      key=lambda h: (h[0], h[1]))
        size, priority = constructor_args(image, fn) or (None, None)
        handlers[f"{fn:#010x}"] = {
            "ids": ids,
            "size": size,
            "priority": priority,
            "name": hits[0][2] if hits else None,
            # A name lifted out of a callee may belong to the callee.
            "direct": bool(hits) and hits[0][0] == 0,
        }
    return handlers


def emit_csharp(handlers: dict, path: str) -> int:
    rows = sorted((oid, int(fn, 16), rec["size"] or 0, rec["name"], rec["direct"])
                  for fn, rec in handlers.items() for oid in rec["ids"])
    body = "\n".join(
        f'        new({oid}, 0x{fn:06X}, {size}, '
        f'{json.dumps(name) if name else "null"}, {str(bool(direct)).lower()}),'
        for oid, fn, size, name, direct in rows)
    src = open(path, encoding="utf-8").read()
    src = re.sub(r"(private static readonly Entry\[\] Table =\n    \{\n).*?(\n    \};)",
                 lambda m: m.group(1) + body + m.group(2), src, flags=re.S)
    src = re.sub(r"so \d+ of \d+ ids resolve",
                 f"so {sum(1 for r in rows if r[3])} of {len(rows)} ids resolve", src)
    open(path, "w", encoding="utf-8").write(src)
    return len(rows)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", nargs="?", default="Sonic.exe")
    ap.add_argument("--depth", type=int, default=1,
                    help="call levels to follow when looking for names; 2 finds "
                         "more but starts attributing one loader's name to "
                         "unrelated objects")
    ap.add_argument("--json", help="write the handler table here")
    ap.add_argument("--csharp", help="rewrite the Table block of ObjectCatalog.cs")
    args = ap.parse_args(argv)

    if not os.path.exists(args.exe):
        print(f"{args.exe}: not found", file=sys.stderr)
        return 1

    image = Image(open(args.exe, "rb").read())
    handlers = build(image, args.depth)

    ids = sum(len(r["ids"]) for r in handlers.values())
    named = sum(len(r["ids"]) for r in handlers.values() if r["name"])
    direct = sum(len(r["ids"]) for r in handlers.values() if r["direct"])
    sized = sum(len(r["ids"]) for r in handlers.values() if r["size"])
    print(f"{ids} object ids across {len(handlers)} spawn functions")
    print(f"  {named} named ({direct} read from the handler itself)")
    print(f"  {sized} with an instance size")

    if args.json:
        os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
        json.dump(handlers, open(args.json, "w"), indent=1)
        print(f"  wrote {args.json}")
    if args.csharp:
        print(f"  rewrote {args.csharp} with {emit_csharp(handlers, args.csharp)} rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
