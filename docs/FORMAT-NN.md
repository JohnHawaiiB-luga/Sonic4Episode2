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

Six little-endian u32s:

| Offset | Field |
|--------|-------|
| `0x00` | version — `2` in every observed file |
| `0x04` | offset of the first data chunk — always `0x20` |
| `0x08` | total size of the data chunks |
| `0x0C` | offset of the `NOF0` chunk |
| `0x10` | size of the `NOF0` chunk including its 8-byte header |
| `0x14` | chunk count |

Walking tag/size to `NEND` needs none of these, so `tools/nn.py` treats them as
informational and *validates* them instead — the declared data and `NOF0` offsets
are checked against where the chunks actually landed. All 5,727 files agree.

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

The contents of `NZOB`. That is where vertices, indices, materials and the node
tree live, and it is the next thing needed for a stage viewer.

`NOF0` matters for this: NN stores internal pointers as offsets that the engine
fixes up at load time against the data chunk base. Any reader of `NZOB` has to
apply the same relocation rather than assuming absolute values.

Episode I's `amObjectSetup` (`AppMain/Am/AmObject.cs:5`) is the oracle — it walks
this identical structure for its own platform variant and hands the object chunk
to `NNS_OBJECT.Read`.

## Usage

```sh
python tools/nn.py show   G_ZONE1/MAP/ZONE1_M.AMB Z1_G_FL_A
python tools/nn.py verify .
python tools/nn.py verify . --ext .ZNO
```
