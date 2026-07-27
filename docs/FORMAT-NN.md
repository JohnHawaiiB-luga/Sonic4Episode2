# SEGA NN container format (`.ZNO` / `.ZNM` / `.ZNV`)

Every model, skeletal animation and vertex animation in Episode II is a SEGA NN
"BINCNK" file — a flat sequence of tagged chunks. This is the container only;
the geometry *inside* `NZOB` is not yet decoded.

Status: **VERIFIED**. All **5,727 NN containers** in the build parse cleanly with
zero failures, and the chunk census cross-checks exactly against the file
extensions.

## Chunks

```
struct chunk { char tag[4]; u32 size; u8 payload[size]; }
```

Chunks run back to back from offset 0 until `NEND`. A typical model:

| Tag | Role |
|-----|------|
| `NZIF` | file header |
| `NZTL` | texture list |
| `NZOB` | object — nodes, materials, vertex and index data |
| `NOF0` | relocation table: offsets inside the data chunks needing fixup |
| `NFN0` | original authored file name |
| `NEND` | terminator |

The second letter of a tag is the **platform code**: `Z` for Direct3D 9, `X` for
Xbox, `G` for GameCube, `I` for the OpenGL ES builds. This is what proves the
format is shared with Episode I, whose decompilation switches on `NIOB`, `NITL`
and `NEND` — the same chunks with a different platform letter.

## `NZIF` header

Six little-endian u32s. Field names come from Episode I's
`NNS_BINCNK_FILEHEADER`:

| Offset | Field | Notes |
|--------|-------|-------|
| `0x00` | `nChunk` | data chunk count — `2` for a model (`NZTL` + `NZOB`) |
| `0x04` | `OfsData` | offset of the first data chunk — always `0x20` |
| `0x08` | `SizeData` | total size of the data chunks |
| `0x0C` | `OfsNOF0` | offset of the `NOF0` chunk |
| `0x10` | `SizeNOF0` | size of `NOF0` including its 8-byte header |
| `0x14` | `Version` | |

Walking tag/size to `NEND` needs none of these, so `tools/nn.py` treats them as
informational and *validates* them instead — the declared data and `NOF0` offsets
are checked against where the chunks actually landed. All 5,727 files agree.

## Data chunk headers

Data chunks carry more than the plain tag/size pair:

| Offset | Field |
|--------|-------|
| `0x00` | tag |
| `0x04` | size |
| `0x08` | `OfsMainData` — where the chunk's root structure lives |
| `0x0C` | `Version` |

**`OfsMainData` is relative to `OfsData`, not to the chunk and not to the file.**
Every internal offset in the object data works the same way. Getting this base
wrong is the easiest way to produce a parse that looks plausible and is nonsense.

## `NZOB` object header

88 bytes at `OfsData + OfsMainData`. All `ofs_*` fields are relative to
`OfsData`; zero means the list is absent.

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `float[3]` | bounding sphere centre |
| `0x0C` | `float` | bounding sphere radius |
| `0x10` | `s32`, `u32` | material count, list offset |
| `0x18` | `s32`, `u32` | vertex list count, list offset |
| `0x20` | `s32`, `u32` | primitive list count, list offset |
| `0x28` | `s32` | node count |
| `0x2C` | `s32` | maximum node depth |
| `0x30` | `u32` | node list offset |
| `0x34` | `s32` | matrix palette count |
| `0x38` | `s32`, `u32` | subobject count, list offset |
| `0x40` | `s32` | texture count |
| `0x44` | `u32` | type flags |
| `0x48` | `s32` | version — `3` in every observed model |
| `0x4C` | `float[3]` | bounding box half-extents |

Status: **VERIFIED**. All **3,577 models** parse with sane values — no negative
counts, no list offsets past end of file, no implausible radii.

The layout validates itself arithmetically. `Z1_G_FL_A.ZNO`, the floor tile
placed 12,552 times in Zone 1 Act 1, reports a bounding box of `(10, 10, 0)` — a
flat quad with no depth — and a radius of `14.14`, which is √(10² + 10²) to two
decimal places. A misaligned read would not produce that relationship.

Model complexity spans what you would expect: the floor is 1 material, 1 vertex
list, 1 node; the Hopper badnik is 27 vertex lists across **62 nodes at depth 8
with 35 matrix palettes**, i.e. a skinned skeleton. 846 models are skinned.

Across all models: 10,138 nodes, 9,767 materials, 7,795 vertex lists and 11,564
primitive lists.

## Locator nulls

31 models carry nodes but no geometry — zero vertex lists, zero primitive lists,
zero radius, zero bounding box, and `ftype` `0x20` where real models set bit 0.

They are positional markers for cutscenes: `CAMERA_POS.ZNO`, `SONIC_POS.ZNO`,
`TAILS_POS.ZNO`, `TORNADO_POS.ZNO`, `TARGET_POS.ZNO`. Treat them as valid, not as
parse failures — a reader that rejects geometry-less objects will throw away the
cutscene camera rig.

## `NFN0` — original filenames

Two reserved u32s, then a NUL-terminated name, zero padded.

Worth extracting: the AMB string table stores names uppercased, while `NFN0`
preserves what the artist actually typed. `Z1_G_HASIRA_B.ZNO` in the archive is
`Z1_G_hasira_B.zno` here. For a preservation project that original casing is
signal, not noise.

## Census

| Payload | Count | Matches |
|---------|-------|---------|
| `NZOB` object | 3,577 | exactly the 3,577 `.ZNO` files |
| `NZMO` motion | 1,481 | the 1,431 `.ZNM` plus 50 `.XNM` |
| `NZMA` morph/vertex animation | 669 | exactly the 669 `.ZNV` files |
| `NZTL` texture list | 3,539 | models that reference textures |
| `NZNN` node names | 52 | |

Every file also carries exactly one `NZIF`, `NOF0`, `NFN0` and `NEND`.

## The `.XNM` oddity

50 files carry the Xbox motion extension but contain `NZMO` — Direct3D chunks.
They are Direct3D motions that kept an Xbox filename through the asset pipeline,
not console leftovers that need a separate decoder. All 50 live in the Special
Stage motion archives (`SS_SON_MTN`, `SS_TLS_MTN`).

## Still open

The lists the object header points at — materials, vertex lists, primitive lists,
nodes and subobjects. The header tells us how many there are and where they
start; their element layouts are not yet decoded, and that is what a stage viewer
actually needs.

Expect Episode II to diverge here. Episode I's material struct is
`NNS_MATERIAL_GLES11_DESC`, an OpenGL ES 1.1 fixed-function descriptor, whereas
Episode II is shader-driven Direct3D 9. The object header above is
platform-neutral and carried over unchanged; the material and vertex descriptors
almost certainly do not.

`NOF0` also has to be understood before trusting any pointer chain — it is the
relocation table listing which offsets the engine fixes up at load time.

Episode I's `NNS_OBJECT.Read`, reached from `amObjectSetup`
(`AppMain/Am/AmObject.cs:5`), remains the oracle for the traversal order.

## Usage

```sh
python tools/nn.py show   G_ZONE1/MAP/ZONE1_M.AMB Z1_G_FL_A
python tools/nn.py verify .
python tools/nn.py verify . --ext .ZNO
```
