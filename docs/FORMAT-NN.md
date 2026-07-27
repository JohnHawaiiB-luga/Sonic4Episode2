# SEGA NN container format (`.ZNO` / `.ZNM` / `.ZNV`)

Every model, skeletal animation and vertex animation in Episode II is a SEGA NN
"BINCNK" file — a flat sequence of tagged chunks.

Status: **VERIFIED**. All **5,727 NN containers** parse cleanly with zero
failures and the chunk census cross-checks exactly against the file extensions.
Inside the object chunk, the header, node tree, vertex lists, primitive lists and
mesh sets are all decoded: **3,546 models yield 2,820,398 vertices and 2,513,705
triangles with zero failures**. Materials remain undecoded, and motions
(`NZMO`) and morph animation (`NZMA`) are untouched.

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

## Geometry

Status: **VERIFIED**. All **3,546 models with geometry extract with zero
failures** (the other 31 are locators), yielding **2,820,398 vertices and
2,513,705 triangles**.

### Pointer arrays

The material, vertex list and primitive list offsets in the object header each
point at an array of `{u32 fType; u32 offset}` pairs — one per item. The second
word is the offset of the actual descriptor.

### Vertex list descriptor

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | format flags |
| `0x04` | `u32` | unknown — resolves near the descriptor itself, **OPEN** |
| `0x08` | `u32` | stride in bytes |
| `0x0C` | `u32` | vertex count |
| `0x10` | `u32` | vertex buffer offset |

Format flags are a bitfield. Each combination accounts for its stride exactly,
which is how the bits were identified across 2,700 vertex lists:

| Bit | Attribute | Size |
|-----|-----------|------|
| `0x00001` | position | 12 |
| `0x00002` | normal | 12 |
| `0x00008` | diffuse colour | 4 |
| `0x00010` | specular colour | 4 |
| `0x10000` | texture coordinate | 8 |

`0x10003` → 32 bytes, `0x1001b` → 40, `0x10019` → 28, `0x10001` → 20. Bits
`0x40`/`0x100` appear on wider strides (56, 64) and are presumably blend weights
and indices for skinning; not yet confirmed.

Position, when present, is always at offset 0 within the vertex.

### Primitive list descriptor

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | mode |
| `0x04` | `u32` | total index count |
| `0x08` | `u32` | strip count |
| `0x0C` | `u32` | offset of per-strip index counts (`u32` each) |
| `0x10` | `u32` | offset of `u16` index data |

**Mode is `0x4810` on all 4,085 primitive lists in the build** — everything is a
triangle strip. Strips are concatenated in the index data, split by the per-strip
counts, and stitched with degenerate triangles that a reader should drop.

### Mesh sets — how it all binds together

Vertex and primitive lists are **not** positionally paired. The binding lives in
`NNS_MESHSET`, reached through the subobject list:

```
object -> subobject[n_subobj]  (20 bytes each)
            -> mesh set[n_meshset]  (40 bytes each)
                 -> iVtxList, iPrimList, iMaterial, iNode
```

Subobject:

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | type flags |
| `0x04` | `s32` | mesh set count |
| `0x08` | `u32` | mesh set array offset |
| `0x0C` | `s32` | texture count |
| `0x10` | `u32` | texture index list offset |

Mesh set:

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `float[3]` | bounding sphere centre |
| `0x0C` | `float` | bounding sphere radius |
| `0x10` | `s32` | node index |
| `0x14` | `s32` | matrix index |
| `0x18` | `s32` | material index |
| `0x1C` | `s32` | vertex list index |
| `0x20` | `s32` | primitive list index |
| `0x24` | `u32` | reserved |

**The mesh set is 40 bytes here where Episode I's is 48** — Episode I carries
three reserved words, the Direct3D build carries one. This was measured rather
than assumed: within every subobject, the gap between the mesh set array and the
texture index list immediately after it divides exactly by the mesh set count,
giving 40 on models with 1, 2 and 24 mesh sets alike.

Assuming positional pairing instead of reading mesh sets fails on roughly half
the corpus, and using Episode I's 48-byte stride fails on rather more — both with
plausible-looking indices rather than obvious errors. This is the sharpest
example so far of the oracle telling you the right *shape* and the wrong *size*.

### Node tree

144 bytes per node, and the tree is what places geometry: a mesh set names a node
index, and models carry authored world positions rather than being centred on the
origin.

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | type flags |
| `0x04` | `s16` | matrix palette index |
| `0x06` | `s16` | parent index, `-1` on the root |
| `0x08` | `s16` | first child, `-1` for a leaf |
| `0x0A` | `s16` | next sibling, `-1` if last |
| `0x0C` | `float[3]` | translation |
| `0x18` | `s32[3]` | rotation, fixed-point angles |
| `0x24` | `float[3]` | scaling |
| `0x30` | `float[16]` | inverse bind matrix |
| `0x70` | 32 bytes | unknown, zero in every observed model |

**144 bytes, where Episode I's `NNS_NODE` is 112** — the second size divergence,
after the mesh set. Verified by walking the tree on all **846 multi-node models**:
every parent, child and sibling index lands in range, each model has exactly one
root, and every scale is finite and non-zero. Strides of 136 and 152 fail on 846
and 845 models respectively, so 144 is not merely a permissive fit.

The stride was found by dumping the raw array and looking for the repeat, after a
brute-force sweep over plausible sizes found nothing that worked everywhere. Worth
remembering: eyeballing the bytes beat generate-and-test here.

### Vertex attributes

Attributes are packed in a fixed order with no padding — position, normal,
diffuse, specular, texture coordinate — which is why each flag combination
accounts for its stride exactly. An attribute's offset is the sum of the sizes of
the present attributes before it.

`tools/nn.py` extracts positions, normals and texture coordinates, and the OBJ
exporter writes all three. Texture coordinates need their V axis flipped for OBJ.

### Texture list (`NZTL`)

The chunk's root is `{s32 count; u32 list_offset}`, then `count` entries of
**20 bytes** — the one struct so far that matches Episode I's size exactly:

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | type flags |
| `0x04` | `u32` | offset of the NUL-terminated filename |
| `0x08` | `u16` | minification filter |
| `0x0A` | `u16` | magnification filter |
| `0x0C` | `u32` | global index |
| `0x10` | `u32` | bank |

Status: **VERIFIED**. Across the build, 3,577 models carry **9,815 texture
references and 9,665 of them (98.5%) resolve** to a `.DDS` that actually exists.
Names keep their authored casing, e.g. `ene_hopper_dif.dds`, `Z1_1_block_06_dif.dds`.

The 150 unresolved are effect and cutscene textures — `EMERALD_ADD.DDS`,
`SONIC_FOOT.DDS`, `EG_TOON_*` — which live in archives loaded separately from the
model, or are referenced but not shipped in this beta. Nothing suggests a parsing
problem.

Suffixes follow the texture bank convention: `_dif` diffuse, `_spe` specular,
`_env` environment map.

### Binding a mesh to a texture — partially solved

A subobject carries its own texture index list (`n_tex`, `ofs_tex`), an array of
`s32` indices into the model's `NZTL`. The Hopper's two subobjects both list
`[0, 1, 2]`, i.e. all three of its textures.

That is *not* the whole answer. `Z1_G_HASIRA_B.ZNO` has **3 materials and only 2
textures**, and its mesh sets reference materials 1, 2 and 0 — so the final
selector has to live in the material, which remains undecoded. For the common
case of a model with a single texture the ambiguity does not arise, and that
covers most stage tiles.

### Cross-check

The extraction agrees with the header independently. `ENE_HOPPER.ZNO` declares a
bounding box centred on `(0.00, 4.36, 2.84)` with half-extents
`(3.88, 9.04, 9.48)`. Its 3,211 extracted vertices span x `[-3.88, 3.88]`,
y `[-4.68, 13.39]`, z `[-6.64, 12.31]` — centre `(0.00, 4.36, 2.84)`,
half-extents `(3.88, 9.04, 9.48)`. Those two figures come from different regions
of the file and match to two decimal places.

## Still open

- **Materials**, and with them the exact mesh-to-texture binding. See the
  post-mortem below — three data-driven approaches have failed and the next
  attempt should go at the binary. Located but **variable in size**, unlike
  everything else here.
  The material pointer's `fType` differs per material (`0x10000000`,
  `0x30000000`) and the gaps between consecutive descriptors vary — 196 and 200
  bytes within a single model — so the layout is flag-driven, with optional
  blocks present or absent. One block is clearly an RGBA colour: a material in
  `Z1_G_HASIRA_B.ZNO` reads `(0.255, 0.494, 0.541, 1.000)`. The field needed most
  is whichever selects the texture bank slot.
- **Vertex colours** are not extracted, and bits `0x40`/`0x100` on the wider
  strides (56, 64) are unidentified — presumably blend weights and indices.
- **Unknown word at `+0x04`** of the vertex list descriptor, and the 32 unknown
  bytes at `+0x70` of a node.
- **`NOF0`.** The relocation table. Not needed so far, because every offset
  reached in practice is already correct relative to `OfsData`, but it should be
  understood before trusting a pointer chain in general.

Episode I's `NNS_OBJECT.Read`, reached from `amObjectSetup`
(`AppMain/Am/AmObject.cs:5`), remains the oracle for traversal order — with the
caveat that its struct *sizes* cannot be trusted.

## Materials — post-mortem on three failed approaches

Recorded so nobody repeats them. Every other structure in this format yielded to
measurement; materials have not.

**1. Measure the stride, as with mesh sets and nodes.** Failed: there is no
stride. Descriptor gaps vary *within a single model* — 196 and 200 bytes in
`Z1_G_HASIRA_B.ZNO`.

**2. Use the subobject texture list to sidestep materials entirely.** Partially
useful but insufficient. A subobject does list `s32` indices into `NZTL`, but
`Z1_G_HASIRA_B.ZNO` has 3 materials against 2 textures with mesh sets referencing
materials 1, 2 and 0 — so the selector genuinely lives in the material.

**3. Correlate a flag word against size.** The pointer's `fType` looked
promising: `0x30000000` descriptors are consistently **exactly 4 bytes larger**
than `0x10000000` ones (120/124, 196/200, 260/264, 184/188), so that flag adds one
field. But the material's own leading `u32` does not determine the remaining
size — across 1,982 sampled materials, only 88 map to a unique size.

**The flaw underneath all three:** gap-to-next-descriptor is being treated as the
material's size, and it is not. Materials are individually referenced blobs and
other structures interleave between them, so the gap is an upper bound at best.
Every size-based inference built on it is unsound.

**What to do instead.** Go at the binary with rizin. Find the routine that reads a
material — reachable from the object-loading path — and read the field layout out
of the code. The data cannot settle this on its own, which is a different
situation from every other struct here.

## Usage

```sh
python tools/nn.py export   G_ZONE1/MAP/ZONE1_M.AMB Z1_G_FL_A out/
python tools/nn.py geometry .
```

`export` writes Wavefront OBJ, which any 3D viewer opens.

## Usage

```sh
python tools/nn.py show   G_ZONE1/MAP/ZONE1_M.AMB Z1_G_FL_A
python tools/nn.py verify .
python tools/nn.py verify . --ext .ZNO
```
