# AMB archive format

`.AMB` is the container format used by the AliceNN engine for all bulk game data
in Sonic the Hedgehog 4 Episode II. Every model, texture, animation, shader and
effect in the game is stored inside one.

Status: **VERIFIED**. The layout below was recovered from the Episode I
decompilation's `readAmbHeader` (`Sonic4Episode1/AppMain/Am/AmFs.cs:153-221`)
and then confirmed by parsing all 1,614 `.AMB` files in the Episode II Beta 8
data set with zero failures (`tools/amb.py verify .`).

## Header

All integers are little-endian.

| Offset | Type   | Field                | Notes                                     |
|--------|--------|----------------------|-------------------------------------------|
| `0x00` | `char[4]` | magic             | `#AMB` (`23 41 4D 42`)                    |
| `0x04` | `u32`  | version/flags        | not read by the engine                    |
| `0x08` | `u32`  | reserved             |                                           |
| `0x0C` | `u32`  | reserved             |                                           |
| `0x10` | `s32`  | `file_num`           | entry count                               |
| `0x14` | `s32`  | `entry_table_offset` |                                           |
| `0x18` | `u32`  | reserved             |                                           |
| `0x1C` | `s32`  | `string_table_offset`| `0` means entries are unnamed             |

## Entry table

`file_num` records of `0x10` bytes at `entry_table_offset`:

| Offset | Type  | Field    |
|--------|-------|----------|
| `0x00` | `s32` | offset   | absolute, from the start of the archive |
| `0x04` | `s32` | length   | in bytes                                |
| `0x08` | `u64` | padding  | zero in every observed file             |

## String table

`file_num` records of `0x20` bytes at `string_table_offset`. Each is a
NUL-terminated ASCII name, zero-padded to `0x20`. When `string_table_offset` is
`0` the engine substitutes the decimal entry index as the name.

A string table can also be *present but blank* for most of its slots. The stage
geometry archives do this heavily — `G_ZONE1/MAP/ZONE1_M_M.AMB` names only 7 of
its 154 entries and `ZONE1_M_V.AMB` only 8 of 237 — with the remainder addressed
positionally by the map loader. 3,794 entries across the data set are blank this
way. `tools/amb.py` falls back to the entry index for these, matching the
engine's no-string-table behaviour; without that fallback they all collapse onto
a single output filename and extraction silently loses ~25% of the stage data.

## Nesting

Entries whose name ends in `.amb` are complete AMB archives stored by value, and
are parsed recursively. `G_COM/MENU/G_PAUSE.AMB` is the canonical example: two
entries, one `.AMA` and one nested `.AMB` holding the actual menu assets. 540
nested archives exist in the data set.

Note that the engine treats offsets in a nested archive as relative to that
archive's own buffer, so a nested archive must be sliced out before parsing —
`tools/amb.py` does this in `AmbArchive.open_nested`.

## Legacy variant

`readAmbHeader` has a second branch for buffers that do not begin with `#AMB`,
where the first `s32` is the entry count and names are length-prefixed strings.
This is the shape produced by the Windows Phone 7 content cooker for Episode I.
**No file in the Episode II data set uses it**, so `tools/amb.py` rejects
non-`#AMB` buffers rather than implementing a path it cannot test.

## Contained file types

Counted across all 1,614 archives:

| Extension | Count | Identification |
|-----------|-------|----------------|
| `.ZNO`  | 3,577 | SEGA NN model, Direct3D 9 variant (`NZIF` chunk magic) |
| `.DDS`  | 2,853 | DirectDraw Surface texture |
| `.ZNM`  | 1,431 | SEGA NN motion (skeletal animation) |
| `.AME`  |   925 | AliceNN effect definition |
| `.PSH`  |   922 | compiled Direct3D 9 pixel shader |
| `.VSH`  |   921 | compiled Direct3D 9 vertex shader |
| `.ZNV`  |   669 | SEGA NN motion, vertex/morph variant |
| `.TXB`  |   651 | texture bank / atlas descriptor |
| `.AMB`  |   540 | nested archive |
| `.MP`   |   336 | map / stage layout |
| `.MD`   |   258 | map data |
| `.AMA`  |   256 | AliceNN sprite animation |
| `.EV`   |    79 | event script |
| `.XNM`  |    50 | SEGA NN motion, Xbox variant (leftover console assets) |
| `.DF` `.DI` `.DC` `.RG` | 159 | collision / region data, unconfirmed |
| `.MFS`  |    36 | unconfirmed |
| `.LTS`  |    28 | lighting set, unconfirmed |
| `.AT`   |    13 | unconfirmed |
| `.SSS`  |     7 | unconfirmed |
| `.GPB`  |     6 | unconfirmed |
| unnamed | 3,794 | blank string-table slots, mostly stage geometry |

The `.ZNO`/`.ZNM`/`.ZNV` prefix letter encodes the target platform in SEGA's NN
library: `X` for Xbox, `G` for GameCube, `Z` for Direct3D 9. The presence of 50
`.XNM` files is a build artifact of the shared console/PC asset pipeline.

## Usage

```sh
python tools/amb.py list    G_COM/MENU/G_PAUSE.AMB
python tools/amb.py extract G_ZONE1/ENE/EP2_ENE_HOPPER_MDL.AMB out/
python tools/amb.py verify  .
```
