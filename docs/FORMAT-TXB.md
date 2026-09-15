# Texture bank format (`.TXB`)

A texture bank lists texture names and stored filter values. Most references in
the prepared PC corpus resolve to `.DDS` payloads in the same AMB archive; one bank
uses payloads present in a separate texture archive.

Status: **VERIFIED structure and raw values; PC filter conversion OPEN**. A scan
of the prepared final PC reference on 2026-09-10 rehashed all 1,619 AMB/AMA inputs
and found **1,009 texture banks with 6,286 entries**, including nested containers.
Every bank parses and its string table begins at
`entry_table_offset + count * 20`. In 1,008 banks every texture reference resolves
to a sibling DDS. The remaining bank's two texture payloads are present in its
separate texture archive; the executable's cross-archive lookup remains OPEN.
Content recognition matters: one DDS entry and its matching TXB reference both
have a trailing dot after `.DDS`, so an extension-only census incorrectly calls
that payload missing. Preserve the stored name when resolving archive entries.

## Endianness

**TXB is big-endian**, unlike the little-endian AMB container that holds it. This
is a legacy of the SEGA NN library's GameCube and Xbox origins; the PC build
inherited the on-disk layout unchanged. Reading it as little-endian yields
nonsense values like `0x33000000` for the entry count, which is the giveaway.

## Header

| Offset | Type  | Field |
|--------|-------|-------|
| `0x00` | `char[4]` | `#TXB` |
| `0x04` | `u32` | header size / version — `0x10` in every observed file |
| `0x08` | `u32` | reserved |
| `0x0C` | `u32` | reserved |
| `0x10` | `u32` | entry count |
| `0x14` | `u32` | entry table offset — `0x18` in every observed file |

## Entry table

`count` records of **20 bytes** at `entry_table_offset`:

| Offset | Type  | Field |
|--------|-------|-------|
| `0x00` | `u32` | runtime slot — zero on disk, filled with the loaded texture handle |
| `0x04` | `u32` | absolute offset of the texture's NUL-terminated name |
| `0x08` | `u16` | raw filter field A; community tools interpret it as minification filtering |
| `0x0A` | `u16` | raw filter field B; community tools interpret it as magnification filtering |
| `0x0C` | 8 bytes | zero on disk — more runtime slots |

The raw pairs vary across the final PC corpus:

| Field A / field B | Entries |
|-------------------|---------|
| `1 / 1` | 2,774 |
| `5 / 1` | 3,469 |
| `0 / 0` | 1 |
| `4 / 1` | 42 |

Sonic4_Tools reads these fields as compact minification and magnification filter
values, with minimum-filter ordinals `0..5` and magnification ordinals `0..1`.
These are not literal graphics API constants. See its pinned
[TXB reader](https://github.com/OSA413/Sonic4_Tools/blob/adcf7c055a2f230016a8ddee78a5db6b686fd846/src/txb/txb-rs-lib/src/txb.rs),
[minimum-filter mapping](https://github.com/OSA413/Sonic4_Tools/blob/adcf7c055a2f230016a8ddee78a5db6b686fd846/src/txb/txb-rs-lib/src/gl_texture_min_filter.rs)
and [magnification mapping](https://github.com/OSA413/Sonic4_Tools/blob/adcf7c055a2f230016a8ddee78a5db6b686fd846/src/txb/txb-rs-lib/src/gl_texture_mag_filter.rs).
This independently documented interpretation is a useful hypothesis for the PC
conversion path. The original executable's mapping to Direct3D sampler states
still needs static and runtime verification. `tools/txb.py` preserves the values
as `flag_a` and `flag_b`; it does not apply a guessed renderer conversion.

Roughly half of each entry is zero on disk. That is consistent with the structure
being the engine's in-memory texture descriptor written out verbatim, with
pointer fields left null — the same pattern the AMB reader shows.

## String table

Begins immediately after the entry table and holds NUL-terminated ASCII names.
Names are plain filenames matching sibling entries in the archive, e.g.
`Z1_1_BLOCK_03_DIF.DDS`.

## Naming conventions

Texture names encode zone, act and usage:

`Z1_1_BLOCK_03_DIF.DDS` → zone 1, act 1, `BLOCK_03`, diffuse map.

Observed suffixes:

| Suffix | Meaning |
|--------|---------|
| `_DIF` | diffuse / albedo |
| `_DCL` | decal layer |
| `_ADD` | additive blend layer |
| `_N_DIF` | night variant of the diffuse map |

Common stems include `BLOCK_nn` (terrain tiles), `LEAF`, `IVY`, `TREE`, `PLANT`,
`LOG`, `ROADLEAF` (Sylvania Castle foliage), `WATER`, `SHADOW`, `GROUND`,
`BREAK` (destructible), and `OBJ_nn`.

The `_N_DIF` night variants are worth noting: the engine ships separate lit and
unlit texture sets for the same geometry rather than doing it with lighting.

## Usage

```sh
python tools/txb.py list   G_ZONE1/MAP/ZONE1_T.AMB -v
python tools/txb.py verify .
```
