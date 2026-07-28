# SEGA NN container format (`.ZNO` / `.ZNM` / `.ZNV`)

Every model, skeletal animation and vertex animation in Episode II is a SEGA NN
"BINCNK" file — a flat sequence of tagged chunks.

Status: **VERIFIED**. All **5,727 NN containers** parse cleanly with zero
failures and the chunk census cross-checks exactly against the file extensions.
Inside the object chunk, the header, node tree, vertex lists, primitive lists and
mesh sets are all decoded: **3,546 models yield 2,820,398 vertices and 2,513,705
triangles with zero failures**. Materials and their texture bindings are decoded, and so are motions: **1,481 of
1,481 parse, 296,072 channels carrying 3,184,997 key frames**. Morph animation
(`NZMA`) and the motion key *payloads* remain untouched.

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
with 35 matrix palettes**, i.e. a skinned skeleton. 839 models are skinned.

"Skinned" here means *geometry* driven by a node tree deeper than one level.
Excluding locators matters: seven camera rigs — `CAMERA_POS`,
`WM_CAMERA_PERSPECTIVE`, `WM_CAMERA_ORTHO` — have 2 or 3 nodes at depth 2 with no
vertices at all, so a test on node depth alone counts them as skinned and gives
846. This was found by the C# port disagreeing with the Python tools by exactly
seven.

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

## The node tree

Every model carries a node array — 846 of the 3,577 have more than one — and a
skinned model's vertices are authored against the pose it describes. Nothing can
be drawn from a multi-node model without walking it.

Each node is **144 bytes**, where Episode I's `NNS_NODE` is 112:

```
+0x04  i16   matrix index
+0x06  i16   parent, -1 at the root
+0x08  i16   first child
+0x0A  i16   next sibling
+0x0C  f32   translate x, y, z
+0x18  i32   rotate x, y, z      (A16: 65536 = one turn)
+0x24  f32   scale x, y, z
```

### Rotation is integers, which is why it looked like padding

`+0x18` through `+0x20` are **signed 32-bit integers in A16** — 65536 to a full
turn, the convention Episode I's `mtMathSin` uses. Read as floats the same bytes
come out as denormals and NaNs, and they had been skipped as padding on that
basis.

Sonic's skeleton settles it. Of 327 rotation words 129 are non-zero, they span
-32768 to 19180, and the values that recur are **16384 and -32768** — exactly a
quarter and a half turn, which is what a bind pose is made of. No float
interpretation produces round numbers like that.

### Walking it

`NodeTransforms.World` composes scale, then rotation Z then Y then X, then
translation, against the parent's matrix. Verified across the whole build:

- **846 of 846** multi-node models have a well-formed tree — one root, every link
  in range, no cycle reachable by walking parents.
- **846 of 846** produce finite world transforms.
- Sonic's 109 joints span **0 to 10.73 world units** in Y, feet at the origin,
  against a model bounding box of 11.6 — a standing skeleton, right way up.

Nodes are stored parents-before-children everywhere in this build, but that is an
observation rather than a guarantee, so the walk falls back to a node's local
transform when its parent has not been resolved yet.

### Which mesh belongs to which node

`NnMeshSet` carries the answer at `+0x10`, and it holds up everywhere:
**11,565 of 11,565 mesh sets across 3,546 models have a `NodeIndex` inside their
model's node array.** Just over half are 0, which is what you expect of the many
rigid single-node props.

`MatrixIndex` at `+0x14` is a second, different thing. It is `-1` on Sonic's
eighteen mesh sets, which is the marker for **palette skinning**: the vertices are
weighted across several matrices rather than riding one node.

### Why that stops short of drawing Sonic

His geometry is authored in a **centred model space** — raw positions span y -5.82
to 5.82 — while his posed skeleton stands from **0 to 10.73**. The two are not the
same space, and the five nodes his meshes bind to (104 to 108) all sit at the
origin, one exactly identity and two carrying a **-16384 rotation about X**, which
is a clean -90 degrees and the usual Y-up/Z-up axis conversion.

So multiplying his vertices by `world[NodeIndex]` would not pose him, it would
double-transform them. Posing a skinned model needs the matrix palette that
`n_mtxpal` counts, plus the blend indices and weights in the vertex format —
neither of which is decoded yet. **That is what stands between this project and a
Sonic on screen**, and it is a discrete piece of work rather than a loose end.

Rigid models — anything with one node, or with meshes bound to a node that carries
a real transform — can be posed with what is here today.

### Skinning weights, and a bug they exposed

The vertex format has more bits than the five that were decoded. Solving the
stride arithmetic across all 36 distinct formats in the build gives:

| Bits | Bytes | What |
|------|------:|------|
| `0x01000`, `0x02000`, `0x04000` | 4 each | **skinning weight**, one float |
| `0x00400` | 4 | **blend indices**, four packed bytes |
| `0x00040` + `0x00100` | 24 together | undecoded; always co-occur |
| `0x20000` | 16 | undecoded |

Read directly rather than inferred. Sonic's first vertex reads
`0.0122, 0.9878, 0.0000` — **summing to exactly 1** — followed by the dword
`00000100`, which is the four bytes `0, 1, 0, 0`. Across the build **all 572
skinned lists carry exactly three weights**, 177 of them also carry the index
dword, **96% of 112,831 sampled vertices sum to 1.000**, and **all 53,941 index
sets are valid with a largest byte of 15**.

That matches the shaders exactly: `v1` is a three-float weight, `v2` a `UBYTE4`
index. The fourth weight is implicit — three summing to one leaves nothing for it.

**They sit between the position and the normal**, which matters more than it
sounds. The component order in a vertex is not the order of the format bits, and
the layout table used to go straight from position to normal. On Sonic that put
the normal at offset 12 instead of 28 and the texture coordinates at 24 instead of
40 — **every skinned model's texture coordinates were being read out of its
normals.** It produced plausible-looking floats rather than an error, which is why
nothing caught it.

Fixed, and checked the way it should have been the first time: texture
coordinates now land in a sane range on **7,290 of 7,290** lists that carry them,
across 2.75 million vertices.

The normal's position is what pins the layout. Testing every three-float slot in
Sonic's 48-byte stride for unit length, `+28` comes out at exactly 1.0000 while
`+12` — where a naive reader looks — gives 0.9743. Close enough to look right,
which is the dangerous kind of wrong.

### What is still missing to draw a character

The weights say *how much* each of several matrices moves a vertex. What says
*which* matrices is the palette that `n_mtxpal` counts — 99 of them on Sonic's 109
nodes — and it is **not decoded**.

The object header counts palettes but carries no offset for them, so they live
inside a subobject. A subobject is 20 bytes and only three of its five dwords are
read: flags, mesh count, mesh offset. The remaining two are the obvious candidates.

**A lead that does not hold up, recorded so nobody spends the afternoon on it
twice.** On Sonic those two dwords read `5` and `0x1062C`, and `0x1062C` holds
`0, 1, 2, 3, 4` immediately before the subobject record — exactly what a count and
a palette of node indices should look like. Across the build it falls apart:

| Check | Result |
|-------|--------|
| Palette offset lands inside the file | 4,955/4,955 |
| Subobject counts sum to the header's `n_mtxpal` | **1,371/3,546** |
| Palette entries index a valid node | **5,378/10,080** |

Two of three are near chance. So either the palette is somewhere else, or those
dwords mean something else and Sonic's `0,1,2,3,4` is a coincidence of a
five-element array of small numbers.

### What the shaders say

The game's own vertex shaders settle the *shape* of the skinning, even though the
palette's storage is still open. Walking all 1,843 shaders for **relative
addressing on a constant register** — the unmistakable marker of palette skinning
— finds **126 vertex shaders that use it**.

One of them, `...RDMRC00000020.VSH` (`vs_3_0`), reads:

```
mul   r2, c75, v2            ; scale the index by a constant
mova  a0, r2                 ; into the address register
mul   r1, v1, c[a0.x + 3]    ; weight times a matrix row
mad   r1, c[a0.x + 3], v1, r1
...
dp4   r0, v0, r1             ; against the position
```

So:

- **`v0` is the position, `v1` the blend weights, `v2` the blend indices.**
- A bone is **four constant registers**, indexed `c[a0.x + 0..3]`.
- The index is scaled by `c75` before use, which is how a bone number becomes a
  register offset.
- Highest constant register across the skinning shaders runs to **c142**, so the
  palette is large — consistent with `n_mtxpal` being 99 on Sonic.

**The open question is now narrow and specific.** The vertex carries weights and,
by the stride arithmetic, nothing else — 48 bytes on Sonic is exactly position 12,
weights 16, normal 12, texture coordinates 8. Yet the shader reads indices from a
separate input register. Either the indices are packed into those same 16 bytes
alongside the weights, or the declaration feeds `v2` from somewhere the stride
does not account for.

The next step is the **D3D9 vertex declaration** the engine builds per vertex
list. That names each element's offset, type and usage outright, and settles both
questions at once.

## Still open

- **The render-state block's contents.** The material's texture binding is
  solved, but the packed `u16` pairs at `+0x0C` (sampler or blend state) are not
  interpreted.
- **Vertex colours** are not extracted, and bits `0x40`/`0x100` on the wider
  strides (56, 64) are unidentified — presumably blend weights and indices.
- **Unknown word at `+0x04`** of the vertex list descriptor, and the 32 unknown
  bytes at `+0x70` of a node.
- **Motion key frame payloads.** Motion headers and channel descriptors parse,
  but the key data each channel points at is not yet interpreted. `NZMA` morph
  animation is untouched.

Episode I's `NNS_OBJECT.Read`, reached from `amObjectSetup`
(`AppMain/Am/AmObject.cs:5`), remains the oracle for traversal order — with the
caveat that its struct *sizes* cannot be trusted.

## `NOF0` - the relocation table

Status: **VERIFIED**. All **3,577 models parse, 0 failures, 134,372 relocation
entries**, every one word-aligned and inside the file.

| Offset | Field |
|--------|-------|
| `0x00` | entry count |
| `0x04` | reserved |
| `0x08` | `count` byte offsets, relative to the data base |

Recovered from the loader in `Sonic.exe` at `0x006c6c33`:

```asm
mov ecx, dword [edi]            ; offset from the table
shr ecx, 2                      ; /4, so it indexes u32s
add dword [eax + ecx*4], eax    ; *(base + offset) += base
```

Each listed word holds a base-relative offset that the engine turns into an
absolute pointer **in place**.

Two consequences worth internalising:

- **The file layout is the in-memory struct layout.** Episode II relocates rather
  than re-parsing, which is exactly why every internal offset is relative to
  `OfsData` and why the structs can be read straight out of the file.
- **`NOF0` is a map of which words are pointers.** That is a general-purpose tool
  for attacking any struct in this format - including materials, where measuring
  sizes had failed three times.

The same function confirms two earlier findings from code rather than data: the
chunk dispatch compares against `NZOB` (`0x424F5A4E`), `NEND` and `NZTL` exactly
as walked here, and the texture-list loop at `0x006c6cce` steps by `0x14`,
**independently confirming the 20-byte texture entry**. It also uppercases texture
names in place, which is why Episode I's decompilation calls `.ToUpper()`.

## Materials - decoded, and the texture binding closed

Materials are the one **variable-size** structure in this format, so they cannot
be walked by stride. Which optional fields are present is read from `NOF0`, which
lists exactly the words that are pointers - that is what made them tractable
after three size-based attempts failed.

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | flags (`0x1102`, `0x0000` observed) |
| `0x04` | `u32` | reserved, zero |
| `0x08` | `u32` | -> colour block |
| `0x0C` | `u32` | -> render-state block |
| `0x18` | `u32` | -> **texture map block**, present only on some materials |

The colour block is a count followed by RGBA floats. The render-state block is a
leading integer then packed `u16` pairs, and reads like sampler or blend state.

### The texture map block - this is the binding

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | type - `0x60000002` on the large majority |
| `0x04` | `u32` | **index into the model's `NZTL` texture list** |

Status: **VERIFIED**. **9,431 of 9,431** materials carrying this block name a
texture index that lies inside their own model's texture list, with **none out of
range**. A further 336 materials have no block at all and are untextured.

`Z1_G_HASIRA_B.ZNO` is the case that proves it. Three materials, two textures:
material 0 has no block, material 1 points at index 0
(`Z1_1_block_06_dif.dds`), material 2 at index 1 (`Z1_1_block_21_dif.dds`). That
model is exactly the one that defeated the earlier subobject-texture-list
approach, because its 3 materials against 2 textures showed the selector had to
live in the material. It does, at `+0x18` -> `+0x04`.

### The full chain

```
mesh set --i_material--> material --+0x18--> texture map --index--> NZTL --> .DDS
```

Every link is now verified. `tools/nn.py export` writes an `.mtl` beside the
`.obj` with `map_Kd` pointing at the right texture, so a viewer picks it up
automatically.

The optional `+0x18` pointer also explains the size correlation noticed earlier:
material pointers with `fType` `0x30000000` are consistently exactly 4 bytes
larger than `0x10000000` ones, because that flag is what carries the extra
texture-map field.


## Motions (`NZMO`)

Status: **VERIFIED**. All **1,481 motions parse, 0 failed**, carrying **296,072
channels and 3,184,997 key frames** between them.

### Motion header

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | type flags; low 5 bits are the channel kind |
| `0x04` | `float` | start frame |
| `0x08` | `float` | end frame |
| `0x0C` | `s32` | submotion count |
| `0x10` | `u32` | submotion array offset |
| `0x14` | `float` | frame rate |
| `0x18` | `u32[2]` | reserved |

32 bytes, matching Episode I's `NNS_MOTION`.

**Start frames may be negative.** Five Sonic transition animations begin at -5 or
-10 for blend pre-roll — `SON_CHANGE_01`, `SON_END_1_01`, `SON_CHANGE_L_01`,
`SON_TO_CHANGE_01`, `SON_TO_CHANGE_L_01`. A validator that assumes frames start
at zero will flag those five as corrupt when they are perfectly correct.

### Submotion

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `u32` | type flags |
| `0x04` | `u32` | interpolation type |
| `0x08` | `s32` | target id — the node this channel drives |
| `0x0C` | `float` | start frame |
| `0x10` | `float` | end frame |
| `0x14` | `float` | first key frame |
| `0x18` | `float` | last key frame |
| `0x1C` | `s32` | key frame count |
| `0x20` | `s32` | key size in bytes |
| `0x24` | `u32` | key data offset |

40 bytes.

### Census

| Frame rate | Motions |
|-----------|---------|
| 60 | 1,410 |
| 29.97 | 69 |
| 30 | 2 |

Every motion in the build is channel kind **1** (node animation) — no camera or
light motions ship as `.ZNM`. The 29.97 entries are NTSC-rate, presumably
authored against video.

### Still open

The **key frame payloads**. Each channel says how many keys it has, how large
each is and where they live, but the key contents are not yet interpreted. That
is what actually animates a skeleton, and it is the next piece of animation work.

## Materials - post-mortem on three failed approaches

## Materials - post-mortem on three failed approaches

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

**What worked instead.** Going at the binary with rizin, which produced `NOF0`,
then using the relocation table as a pointer map rather than guessing at sizes.
See the section above - the descriptor's leading fields are now known. Finishing
the job still needs the draw-path code, since the texture selector cannot be
confirmed from data alone.

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

