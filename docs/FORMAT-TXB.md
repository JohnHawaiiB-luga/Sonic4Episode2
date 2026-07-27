# Texture bank format (`.TXB`)

A texture bank is the index that maps a stage's texture slots to the `.DDS` files
stored alongside it in the same AMB archive. Tile ids in the stage grids resolve
through this table, so it is the link between level geometry and what the level
actually looks like.

Status: **VERIFIED**. All **651 texture banks** in the build parse, and every
texture name in every bank resolves to a `.DDS` entry present in the same
archive. Two independent structural checks hold on all of them: the declared
entry count matches the sibling DDS count, and the string table begins at exactly
`entry_table_offset + count * 20`.

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
| `0x08` | `u16` | unknown, `1` in every observed file |
| `0x0A` | `u16` | unknown, `1` in every observed file |
| `0x0C` | 8 bytes | zero on disk — more runtime slots |

The two `u16` fields at `+0x08` are **OPEN**. They are invariant across the whole
data set, so nothing can be inferred about them from the data alone; they will
have to come from the binary's texture-loading code.

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
