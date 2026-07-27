#!/usr/bin/env python3
"""Recover the player parameter table from Sonic.exe.

Episode I keeps its player tuning in `g_gm_player_parameter[7]`, an array of
FX32 fixed-point integers. Episode II keeps the same field order but stores the
speeds as floats, and arranges them as **3 characters of 11 modes** rather than
Episode I's flat 7 — so the values are directly readable once you know the shape.

The shape comes from the engine, not from counting: `imul ecx, ecx, 0x4a4` at
`0x0046AEA1` scales a character id by 1188 bytes, which is exactly 11 rows of
108, and the fourth such block is unrelated data.

Finding it is a matter of searching for one number. Episode I's jump impulse is
23130 in FX32, which is 5.64697265625 as a float, and it sits immediately before
the gravity value 680/4096 = 0.166015625. That pair occurs 21 times in the
executable and nowhere by accident.

The table is confirmed by four integers that Episode II did *not* change and that
no float search would have found: at offset 32 sit `time_air` 1800 and
`time_damage` 180, and at 36 `pool_max` 96 and `fall_wait_time` 24 — Episode I's
values exactly, packed as `u16` pairs where Episode I used four `int`s.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys

ROW_STRIDE = 108           # 27 dwords
MODE_COUNT = 11            # rows per character
CHARACTER_STRIDE = 0x4A4   # 1188 = 11 * 108, read straight off `imul ecx, ecx, 0x4a4`
CHARACTER_COUNT = 3        # the fourth block is unrelated data

# Field order carried over from Episode I's GMS_PLY_PARAMETER. Slots 8 and 9 hold
# four u16 counters rather than a float, and slot 13 has no Episode I counterpart.
FLOAT_FIELDS = {
    0: "GroundAcceleration", 1: "TopSpeed", 2: "GroundDeceleration",
    3: "SpinDashBase", 4: "SpinDashCharge", 5: "SpinDashMax", 6: "RollFriction",
    7: "DownhillTopSpeedBonus",
    10: "SlopeFactor", 11: "SlopeSpeedMax", 12: "SlopeFactorRolling",
    13: "SlopeFactorRollingAlt", 14: "SlopeFactorPipe", 15: "SlopeFactorPinball",
    16: "JumpImpulse", 17: "Gravity", 18: "TerminalVelocity", 19: "PushMax",
    20: "AirAcceleration", 21: "AirSpeedMax", 22: "AirDrag",
    23: "PinballAcceleration", 24: "PinballTopSpeed", 25: "PinballDeceleration",
    26: "PinballDownhillBonus",
}
INT_FIELDS = ["BreathFrames", "InvincibleFrames", "PoolMax", "CoyoteFrames"]

# Modes 0-6 keep Episode I's char_id enumeration, which they match in order and
# in value. 7 and 8 are heavily slowed variants with no Episode I counterpart;
# 9 and 10 carry ordinary values and are not identified.
MODES = ["Normal", "Super", "SpecialStage", "Pinball", "PinballSuper",
         "MadGear", "MadGearSuper", "Slowed1", "Slowed2", "Spare1", "Spare2"]

# Episode I's Sonic row, as floats. The anchor.
JUMP = 23130 / 4096.0
GRAVITY = 680 / 4096.0


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

    def va(self, offset: int) -> int | None:
        for _n, vaddr, _vs, raddr, rsize in self.sections:
            if raddr <= offset < raddr + rsize:
                return self.base + vaddr + (offset - raddr)
        return None


def find_table(image: Image) -> int | None:
    """File offset of character 0 mode 0, located from the jump/gravity pair."""
    anchor = struct.pack("<f", JUMP) + struct.pack("<f", GRAVITY)
    hits = [m.start() for m in re.finditer(re.escape(anchor), image.data)]
    if not hits:
        return None
    # The pair sits at field 16, so row 0 starts 64 bytes earlier. Rows repeat at
    # ROW_STRIDE, so walk back to the lowest one that still parses as a row.
    start = min(hits) - 64
    while plausible(image, start - ROW_STRIDE):
        start -= ROW_STRIDE
    return start


def plausible(image: Image, at: int) -> bool:
    if at < 0 or at + ROW_STRIDE > len(image.data):
        return False
    f = [struct.unpack_from("<f", image.data, at + k * 4)[0] for k in range(27)]
    counters = struct.unpack_from("<HHHH", image.data, at + 32)
    return (0.001 < f[0] < 1 and 1 <= f[1] <= 40 and 0 < f[2] < 5
            and 0.01 < f[17] < 2 and 1 < f[16] < 25 and 5 <= f[18] <= 40
            and counters[0] <= 7200 and counters[1] <= 1200 and counters[3] <= 600)


def read_rows(image: Image, start: int) -> list[dict]:
    """Every row, character-major, so index is `character * MODE_COUNT + mode`."""
    rows = []
    for character in range(CHARACTER_COUNT):
        for mode in range(MODE_COUNT):
            at = start + character * CHARACTER_STRIDE + mode * ROW_STRIDE
            row = {"character": character, "mode": MODES[mode]}
            for slot, name in FLOAT_FIELDS.items():
                row[name] = struct.unpack_from("<f", image.data, at + slot * 4)[0]
            for name, value in zip(INT_FIELDS,
                                   struct.unpack_from("<HHHH", image.data, at + 32)):
                row[name] = value
            rows.append(row)
    return rows


def emit_csharp(rows: list[dict], address: int, path: str) -> None:
    order = [FLOAT_FIELDS[k] for k in sorted(FLOAT_FIELDS)] + INT_FIELDS

    def literal(row):
        parts = []
        for name in order:
            v = row[name]
            parts.append(f"{v}" if isinstance(v, int) else f"{v!r}f")
        return ", ".join(parts)

    props = "\n".join(
        f"    /// <summary>{name}, in game pixels per frame.</summary>\n"
        f"    public float {name} {{ get; init; }}"
        if FLOAT_FIELDS.get(k) == name else ""
        for k, name in [(k, FLOAT_FIELDS[k]) for k in sorted(FLOAT_FIELDS)])

    rows_src = "\n".join(
        f"        // character {r['character']}, {r['mode']}\n        new({literal(r)}),"
        for r in rows)

    src = f'''namespace Sonic4Episode2.Core.Assets;

/// <summary>
/// Episode II's own player tuning, read out of <c>Sonic.exe:{address:#010X}</c>.
/// </summary>
/// <remarks>
/// <b>{CHARACTER_COUNT} characters of {MODE_COUNT} modes</b>, each row {ROW_STRIDE}
/// bytes, in the same field order Episode I uses for
/// <c>g_gm_player_parameter</c>. The character stride is {CHARACTER_STRIDE} bytes,
/// read straight off the engine's own indexing — <c>imul ecx, ecx, 0x4a4</c> at
/// <c>0x0046AEA1</c>, on a character id loaded from the player work struct.
/// Episode I stores these as FX32 fixed-point integers; Episode II stores the
/// speeds as plain floats, so they can be read directly.
/// <para>
/// Characters 0 and 1 are physically identical bar one slope field, and both have
/// a Super mode. Character 2 has no Super — its mode 1 repeats its normal values —
/// which is what you would expect of Metal Sonic. Modes 0 to 6 match Episode I's
/// character enumeration in order and in value; modes 7 and 8 are heavily slowed
/// variants with no Episode I counterpart, and 9 and 10 are not identified.
/// </para>
/// <para>
/// The table was found by searching for Episode I's jump impulse — 23130 in FX32
/// is {JUMP} as a float — immediately followed by its gravity,
/// {GRAVITY}. What confirms it is four integers Episode II did not
/// change and that no float search would have surfaced: <c>BreathFrames</c> 1800,
/// <c>InvincibleFrames</c> 180, <c>PoolMax</c> 96 and <c>CoyoteFrames</c> 24, packed
/// as <c>u16</c> pairs where Episode I used four <c>int</c>s. Four of the mode
/// jump impulses match Episode I to the bit; the rest Episode II retuned.
/// </para>
/// <para>
/// <b>Units are game pixels per frame at 60 Hz.</b> A collision cell spans 64 of
/// those pixels and {'{'}StageAssembler.CellSize{'}'} world units, so
/// <see cref="WorldPerPixel"/> converts. Regenerate with <c>tools/physics.py</c>.
/// </para>
/// </remarks>
/// <param name="GroundAcceleration">Added to ground speed per frame while steering.</param>
/// <param name="TopSpeed">Ground speed cap.</param>
/// <param name="GroundDeceleration">Bled off per frame with no input, and when braking.</param>
/// <param name="JumpImpulse">Upward speed applied on the jump frame.</param>
/// <param name="Gravity">Added to downward speed per airborne frame.</param>
/// <param name="TerminalVelocity">Downward speed cap.</param>
/// <param name="AirAcceleration">Horizontal acceleration while airborne.</param>
/// <param name="AirSpeedMax">Horizontal speed cap while airborne.</param>
/// <param name="AirDrag">Horizontal speed bled off per airborne frame with no input.</param>
/// <param name="CoyoteFrames">Frames after leaving ground before gravity applies.</param>
public readonly record struct PlayerPhysics(
{",\n".join(f"    float {n}" if n not in INT_FIELDS else f"    int {n}" for n in order)})
{{
    /// <summary>Game pixels spanned by one collision cell.</summary>
    /// <remarks>
    /// From the collision format: a cell holds 64 height columns, and its 32
    /// height units are two pixels each.
    /// </remarks>
    public const float PixelsPerCell = 64f;

    /// <summary>Frames per second the constants above assume.</summary>
    public const int FrameRate = 60;

    /// <summary>World units per game pixel, for converting the values above.</summary>
    public static float WorldPerPixel => StageAssembler.CellSize / PixelsPerCell;

    /// <summary>Characters in the table.</summary>
    public const int CharacterCount = {CHARACTER_COUNT};

    /// <summary>Modes per character.</summary>
    public const int ModeCount = {MODE_COUNT};

    /// <summary>Every row, character-major.</summary>
    public static readonly PlayerPhysics[] All =
    {{
{rows_src}
    }};

    /// <summary>One row.</summary>
    public static PlayerPhysics For(int character, int mode) =>
        All[character * ModeCount + mode];

    /// <summary>Ordinary Sonic — the row the stage scene uses.</summary>
    public static PlayerPhysics Sonic => For(0, 0);
}}
'''
    open(path, "w", encoding="utf-8").write(src)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", nargs="?", default="Sonic.exe")
    ap.add_argument("--json")
    ap.add_argument("--csharp")
    args = ap.parse_args(argv)

    if not os.path.exists(args.exe):
        print(f"{args.exe}: not found", file=sys.stderr)
        return 1
    image = Image(open(args.exe, "rb").read())
    start = find_table(image)
    if start is None:
        print("player parameter table not found", file=sys.stderr)
        return 1

    address = image.va(start)
    rows = read_rows(image, start)
    print(f"player parameter table at {address:#010x}: "
          f"{CHARACTER_COUNT} characters x {MODE_COUNT} modes, "
          f"rows of {ROW_STRIDE} bytes, character stride {CHARACTER_STRIDE}")
    for row in rows:
        print(f"  char {row['character']} {row['mode']:13s} "
              f"accel {row['GroundAcceleration']:.5f}  "
              f"top {row['TopSpeed']:6.3f}  jump {row['JumpImpulse']:.4f}  "
              f"gravity {row['Gravity']:.5f}  coyote {row['CoyoteFrames']}")

    if args.json:
        os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
        json.dump({"address": address, "rows": rows}, open(args.json, "w"), indent=1)
        print(f"  wrote {args.json}")
    if args.csharp:
        emit_csharp(rows, address, args.csharp)
        print(f"  wrote {args.csharp}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
