"""Recover object id -> handler class name from the symbolized Android build.

The Windows build's spawn dispatch table (`Sonic.exe:0x007031C8`) is indexed
directly by object id but is stripped, so ids only ever had names guessed from
nearby string immediates. The Android developer build carries the same table
with every slot's function named by the linker, so the mapping can be read
rather than inferred.

Finding it: the table is an array of function pointers, so every live slot is a
relocation site. Collect `R_AARCH64_ABS64` (symbol-named) and
`R_AARCH64_RELATIVE` (addend-named) relocations, map each site to a function
name, then anchor the base.

**The base is anchored on two ids proven independently from placement data**
(`docs/FORMAT-EVENTS.md`, beat 52): id 443 is the act's start marker and id 520
its goal, each appearing exactly once per act at a consistent fraction of the
act's width. Exactly one base makes `GmGmkStartInit` land on 443 *and*
`GmGmkGoalPanelInit` land on 520 simultaneously. Two unrelated linker symbols
agreeing with two statistically-derived ids is what makes this a reading rather
than a guess.

Cross-check: every one of Zone 1 Act 1's 533 placements resolves to a name, and
the names describe Sylvania Castle — `WaterArea`, `BubbleManager`, `Sconce`,
and `GmEneEp2HariSenbo` (harisenbo, the pufferfish).

Usage:
    python tools/dispatch.py extract  <path/to/arm64-v8a/libfox.so>
    python tools/dispatch.py verify   <path/to/libfox.so> <game root>
"""
from __future__ import annotations

import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

# Anchors: object ids proven from placement statistics, not from any binary.
START_ID = 443
GOAL_ID = 520
START_SYM = "GmGmkStartInit"
GOAL_SYM = "GmGmkGoalPanelInit"

R_AARCH64_ABS64 = 0x101
R_AARCH64_RELATIVE = 0x403

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "analysis", "object-names.json")


class Elf:
    """Just enough ELF64 to read section headers, symbols and relocations."""

    def __init__(self, data: bytes):
        self.data = data
        if data[:4] != b"\x7fELF":
            raise ValueError("not an ELF file")
        self.e_shoff, = struct.unpack_from("<Q", data, 0x28)
        self.e_shentsize, self.e_shnum, self.e_shstrndx = struct.unpack_from(
            "<HHH", data, 0x3A)
        self._shstr = self.section_raw(self.e_shstrndx)[2]

    def section_raw(self, i: int):
        base = self.e_shoff + i * self.e_shentsize
        name_off, _stype = struct.unpack_from("<II", self.data, base)
        offset, size = struct.unpack_from("<QQ", self.data, base + 0x18)
        entsize, = struct.unpack_from("<Q", self.data, base + 0x38)
        return name_off, entsize, offset, size

    def section(self, want: str):
        for i in range(self.e_shnum):
            name_off, entsize, offset, size = self.section_raw(i)
            end = self.data.index(b"\0", self._shstr + name_off)
            if self.data[self._shstr + name_off:end].decode() == want:
                return offset, size, entsize
        return None


def strip_name(sym: str) -> str:
    """`GmGmkSpringInit` -> `Spring`; demangles the C++-mangled variants."""
    # Itanium mangling: _Z<len><name>... — recover the bare function name.
    m = re.match(r"^_Z(\d+)(.+)$", sym)
    if m:
        sym = m.group(2)[: int(m.group(1))]
    for prefix in ("GmGmk", "GmEne", "GmEp2", "GmPly", "Gm"):
        if sym.startswith(prefix):
            sym = sym[len(prefix):]
            break
    for suffix in ("Init",):
        if sym.endswith(suffix):
            sym = sym[: -len(suffix)]
    return sym or "?"


def read_symbols(elf: Elf) -> dict[int, str]:
    """Function address -> name, from .symtab if present else .dynsym."""
    out: dict[int, str] = {}
    for symtab, strtab in ((".symtab", ".strtab"), (".dynsym", ".dynstr")):
        st = elf.section(symtab)
        sr = elf.section(strtab)
        if not st or not sr:
            continue
        offset, size, entsize = st
        entsize = entsize or 24
        for at in range(offset, offset + size, entsize):
            name_off, = struct.unpack_from("<I", elf.data, at)
            info = elf.data[at + 4]
            value, = struct.unpack_from("<Q", elf.data, at + 8)
            if (info & 0xF) != 2 or not value:      # STT_FUNC only
                continue
            end = elf.data.index(b"\0", sr[0] + name_off)
            name = elf.data[sr[0] + name_off:end].decode("utf-8", "replace")
            if name:
                out.setdefault(value, name)
    return out


def relocation_sites(elf: Elf) -> dict[int, str]:
    """Address written -> name of the function whose address goes there."""
    rela = elf.section(".rela.dyn")
    if not rela:
        raise ValueError("no .rela.dyn")
    dynsym = elf.section(".dynsym")
    dynstr = elf.section(".dynstr")
    by_addr = read_symbols(elf)

    def dynsym_name(idx: int) -> str:
        base = dynsym[0] + idx * (dynsym[2] or 24)
        name_off, = struct.unpack_from("<I", elf.data, base)
        end = elf.data.index(b"\0", dynstr[0] + name_off)
        return elf.data[dynstr[0] + name_off:end].decode("utf-8", "replace")

    offset, size, entsize = rela
    entsize = entsize or 24
    sites: dict[int, str] = {}
    for at in range(offset, offset + size, entsize):
        r_offset, r_info, r_addend = struct.unpack_from("<QQq", elf.data, at)
        rtype = r_info & 0xFFFFFFFF
        if rtype == R_AARCH64_ABS64:
            name = dynsym_name(r_info >> 32)
            if name:
                sites[r_offset] = name
        elif rtype == R_AARCH64_RELATIVE:
            name = by_addr.get(r_addend)
            if name:
                sites.setdefault(r_offset, name)
    return sites


def find_base(sites: dict[int, str]) -> int:
    """The one base where the start and goal symbols land on their proven ids."""
    starts = [w for w, n in sites.items() if n == START_SYM]
    goals = [w for w, n in sites.items() if n == GOAL_SYM]
    bases = {g - GOAL_ID * 8 for g in goals} & {s - START_ID * 8 for s in starts}
    if len(bases) != 1:
        raise ValueError(
            f"expected exactly one base reconciling id {START_ID}/{GOAL_ID}, "
            f"got {sorted(hex(b) for b in bases)}")
    return bases.pop()


def extract(so_path: str) -> dict[int, str]:
    elf = Elf(open(so_path, "rb").read())
    sites = relocation_sites(elf)
    base = find_base(sites)
    table: dict[int, str] = {}
    for w, name in sites.items():
        if w < base:
            continue
        slot, rem = divmod(w - base, 8)
        # The engine's own table is 803 slots wide in the Windows build.
        if rem == 0 and slot < 803:
            table[slot] = name
    return dict(sorted(table.items()))


def cmd_extract(argv) -> int:
    so = argv[0]
    table = extract(so)
    names = {i: strip_name(s) for i, s in table.items()}
    payload = {
        "source": "arm64-v8a/libfox.so dispatch table",
        "anchored_on": {str(START_ID): START_SYM, str(GOAL_ID): GOAL_SYM},
        "ids": {str(i): {"class": table[i], "name": names[i]} for i in table},
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=1, sort_keys=False)
        fh.write("\n")
    distinct = len(set(names.values()))
    print(f"{len(table)} ids named, {distinct} distinct classes -> "
          f"{os.path.relpath(OUT, HERE)}")
    for probe in (START_ID, GOAL_ID, 719):
        print(f"  id {probe}: {table.get(probe, '(empty)')}")
    return 0


def cmd_verify(argv) -> int:
    """Resolve every placement in every act and report coverage."""
    so, root = argv[0], argv[1]
    table = extract(so)
    sys.path.insert(0, HERE)
    import amb
    import stagemap

    total = named = 0
    acts = 0
    missing: Counter = Counter()
    for path in amb._iter_amb_files(root):
        if os.path.basename(path).upper().endswith("_MAP.AMB"):
            try:
                archive = amb.load(path)
            except Exception:
                continue
            for entry in archive:
                stem = entry.name.upper()
                if not stem.endswith(".EV") or not stem[:-3][-1:].isdigit():
                    continue
                try:
                    places = stagemap.read_events(archive.read(entry))
                except Exception:
                    continue
                acts += 1
                for p in places:
                    total += 1
                    if p.object_id in table:
                        named += 1
                    else:
                        missing[p.object_id] += 1
    pct = 100.0 * named / total if total else 0.0
    print(f"{acts} placement files, {total} placements, "
          f"{named} named ({pct:.1f}%)")
    if missing:
        print("unnamed ids:", dict(missing.most_common(10)))
    return 0 if named == total else 1


CATALOG = os.path.join(HERE, "..", "src", "Sonic4Episode2.Core", "Assets",
                       "ObjectCatalog.cs")


def cmd_csharp(argv) -> int:
    """Rewrite ObjectCatalog's table, adding the recovered class name."""
    so = argv[0]
    path = argv[1] if len(argv) > 1 else CATALOG
    table = extract(so)
    names = {i: strip_name(s) for i, s in table.items()}

    src = open(path, encoding="utf-8").read()
    rows = re.findall(r"^\s*new\((\d+), 0x([0-9A-Fa-f]+), (\d+), "
                      r"(null|\"[^\"]*\"), (true|false)\),\s*$",
                      src, flags=re.M)
    if not rows:
        print("could not parse the existing table")
        return 1

    body = []
    for oid, fn, size, asset, direct in rows:
        i = int(oid)
        cls = json.dumps(names[i]) if i in names else "null"
        body.append(f"        new({i}, 0x{int(fn, 16):06X}, {size}, "
                    f"{cls}, {asset}, {direct}),")
    src = re.sub(r"(private static readonly Entry\[\] Table =\n    \{\n).*?(\n    \};)",
                 lambda m: m.group(1) + "\n".join(body) + m.group(2),
                 src, flags=re.S)
    open(path, "w", encoding="utf-8").write(src)
    covered = sum(1 for oid, *_ in rows if int(oid) in names)
    print(f"rewrote {os.path.relpath(path, HERE)}: {len(rows)} rows, "
          f"{covered} with a recovered class name")
    return 0


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print(__doc__)
        return 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "extract":
        return cmd_extract(rest)
    if cmd == "verify":
        return cmd_verify(rest)
    if cmd == "csharp":
        return cmd_csharp(rest)
    print(f"unknown command '{cmd}'")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
