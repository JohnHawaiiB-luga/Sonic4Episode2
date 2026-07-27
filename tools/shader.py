"""Direct3D 9 shader bytecode reader for Sonic the Hedgehog 4 Episode II.

`NNSTDSHADER/SHADER.AMB` holds 1,843 compiled shaders — 922 `.PSH` and 921
`.VSH`. The whole mobile plan rests on being able to translate them, so this
exists to answer one question with evidence rather than optimism: **is this
ordinary, well-formed Shader Model 3.0 bytecode?**

If it is, MojoShader consumes it directly — that is already how FNA runs XNA
shaders on OpenGL — and the mobile shader path is an off-the-shelf translation
step rather than re-authoring 1,843 shaders by hand.

Bytecode layout (all little-endian 32-bit tokens):

    version token   0xFFFE_MMmm vertex, 0xFFFF_MMmm pixel
    instructions    opcode in bits 0-15, length in bits 24-27
    comments        opcode 0xFFFE, token count in bits 16-30 — CTAB lives here
    end token       0x0000FFFF

A shader that walks cleanly from version token to end token, with every
instruction length landing inside the buffer and no unknown opcodes, is
translatable by any standard consumer.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amb  # noqa: E402

END_TOKEN = 0x0000FFFF
COMMENT_OPCODE = 0xFFFE

# Shader Model 1-3 opcode names, from the documented D3D9 instruction set.
OPCODES = {
    0x00: "nop", 0x01: "mov", 0x02: "add", 0x03: "sub", 0x04: "mad", 0x05: "mul",
    0x06: "rcp", 0x07: "rsq", 0x08: "dp3", 0x09: "dp4", 0x0A: "min", 0x0B: "max",
    0x0C: "slt", 0x0D: "sge", 0x0E: "exp", 0x0F: "log", 0x10: "lit", 0x11: "dst",
    0x12: "lrp", 0x13: "frc", 0x14: "m4x4", 0x15: "m4x3", 0x16: "m3x4",
    0x17: "m3x3", 0x18: "m3x2", 0x19: "call", 0x1A: "callnz", 0x1B: "loop",
    0x1C: "ret", 0x1D: "endloop", 0x1E: "label", 0x1F: "dcl", 0x20: "pow",
    0x21: "crs", 0x22: "sgn", 0x23: "abs", 0x24: "nrm", 0x25: "sincos",
    0x26: "rep", 0x27: "endrep", 0x28: "if", 0x29: "ifc", 0x2A: "else",
    0x2B: "endif", 0x2C: "break", 0x2D: "breakc", 0x2E: "mova", 0x2F: "defb",
    0x30: "defi", 0x40: "texcoord", 0x41: "texkill", 0x42: "tex", 0x43: "texbem",
    0x44: "texbeml", 0x45: "texreg2ar", 0x46: "texreg2gb", 0x47: "texm3x2pad",
    0x48: "texm3x2tex", 0x49: "texm3x3pad", 0x4A: "texm3x3tex",
    0x4C: "texm3x3spec", 0x4D: "texm3x3vspec", 0x4E: "expp", 0x4F: "logp",
    0x50: "cnd", 0x51: "def", 0x52: "texreg2rgb", 0x53: "texdp3tex",
    0x54: "texm3x2depth", 0x55: "texdp3", 0x56: "texm3x3", 0x57: "texdepth",
    0x58: "cmp", 0x59: "bem", 0x5A: "dp2add", 0x5B: "dsx", 0x5C: "dsy",
    0x5D: "texldd", 0x5E: "setp", 0x5F: "texldl", 0x60: "breakp",
}


class ShaderError(Exception):
    pass


class Shader:
    __slots__ = ("kind", "major", "minor", "opcodes", "tokens", "has_ctab", "comments")

    def __init__(self):
        self.kind = "?"
        self.major = self.minor = 0
        self.opcodes: Counter = Counter()
        self.tokens = 0
        self.has_ctab = False
        self.comments = 0

    @property
    def model(self) -> str:
        return f"{self.kind}_{self.major}_{self.minor}"

    def __repr__(self):
        return f"<Shader {self.model} {self.tokens} tokens, {sum(self.opcodes.values())} instructions>"


def parse(data: bytes) -> Shader:
    """Walk the token stream. Raises ShaderError on anything malformed."""
    if len(data) < 8 or len(data) % 4:
        raise ShaderError(f"length {len(data)} is not a whole number of tokens")

    sh = Shader()
    (version,) = struct.unpack_from("<I", data, 0)
    kind = version >> 16
    if kind == 0xFFFE:
        sh.kind = "vs"
    elif kind == 0xFFFF:
        sh.kind = "ps"
    else:
        raise ShaderError(f"bad version token {version:#010x}")
    sh.major, sh.minor = (version >> 8) & 0xFF, version & 0xFF

    offset = 4
    while offset + 4 <= len(data):
        (token,) = struct.unpack_from("<I", data, offset)
        sh.tokens += 1
        if token == END_TOKEN:
            return sh
        opcode = token & 0xFFFF
        if opcode == COMMENT_OPCODE:
            count = (token >> 16) & 0x7FFF
            body = offset + 4
            if body + count * 4 > len(data):
                raise ShaderError(f"comment at {offset} overruns the buffer")
            if count and data[body:body + 4] == b"CTAB":
                sh.has_ctab = True
            sh.comments += 1
            offset = body + count * 4
            continue
        if opcode not in OPCODES:
            raise ShaderError(f"unknown opcode {opcode:#x} at token offset {offset}")
        sh.opcodes[OPCODES[opcode]] += 1
        # Shader Model 2 and later carry the instruction length in bits 24-27.
        length = (token >> 24) & 0x0F
        offset += 4 + length * 4
    raise ShaderError("ran off the end without an end token")


def _iter_shaders(root: str):
    for path in amb._iter_amb_files(root):
        try:
            archive = amb.load(path)
        except Exception:
            continue
        for entry in archive:
            if entry.name.upper().endswith((".PSH", ".VSH")):
                yield path, entry.name, archive.read(entry)


def cmd_verify(args) -> int:
    ok = bad = 0
    models: Counter = Counter()
    opcodes: Counter = Counter()
    ctab = 0
    failures: list[str] = []
    instructions = 0

    for path, name, data in _iter_shaders(args.root):
        try:
            sh = parse(data)
            ok += 1
            models[sh.model] += 1
            opcodes.update(sh.opcodes)
            instructions += sum(sh.opcodes.values())
            ctab += sh.has_ctab
        except ShaderError as exc:
            bad += 1
            if len(failures) < 10:
                failures.append(f"{os.path.basename(path)}::{name}: {exc}")

    print(f"{ok} shaders parsed cleanly, {bad} failed")
    print(f"{ctab} carry a CTAB constant table")
    print(f"{instructions:,} instructions total")
    print("\nshader models:")
    for model, n in models.most_common():
        print(f"  {model:<8} {n}")
    print(f"\n{len(opcodes)} distinct opcodes, all from the documented SM1-3 set:")
    for op, n in opcodes.most_common(args.top):
        print(f"  {op:<12} {n}")
    for failure in failures:
        print(f"  ! {failure}", file=sys.stderr)
    return 1 if bad else 0


def cmd_show(args) -> int:
    for path, name, data in _iter_shaders(args.root):
        if args.name.upper() not in name.upper():
            continue
        sh = parse(data)
        print(f"{name}  {len(data)} bytes  {sh.model}  ctab={sh.has_ctab}")
        print(f"  {sh.tokens} tokens, {sum(sh.opcodes.values())} instructions, "
              f"{sh.comments} comment blocks")
        for op, n in sh.opcodes.most_common(args.top):
            print(f"    {op:<12} {n}")
        return 0
    print(f"no shader matching {args.name!r}", file=sys.stderr)
    return 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="D3D9 shader bytecode tool (Sonic 4 Episode II)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("verify", help="parse every shader under a directory tree")
    p.add_argument("root")
    p.add_argument("--top", type=int, default=20)
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("show", help="summarise one shader")
    p.add_argument("root")
    p.add_argument("name")
    p.add_argument("--top", type=int, default=12)
    p.set_defaults(func=cmd_show)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
