# Object placement format (`.EV`)

`.EV` files hold the objects placed in a stage — rings, springs, enemies,
gimmicks, checkpoints. They sit alongside the tile grids in
`<zone><act>_MAP.AMB`.

Status: **VERIFIED structure, INFERRED field meanings.** All 65 `.EV` files in
the build parse without error under the layout below. The interpretation of the
individual bytes *within* a record is not yet proven and is marked as such.

## Spatial index

The file opens with a coarse block grid rather than a flat list, so the engine
can spawn and despawn objects by scroll position without scanning everything.

| Offset | Type  | Field |
|--------|-------|-------|
| `0x00` | `u16` | block_w |
| `0x02` | `u16` | block_h |
| `0x04` | `u32 × block_w*block_h` | absolute offset of each block's record list |

**`block_w` and `block_h` are the stage's tile dimensions divided by four and
rounded up.** Verified on every file: Zone 1 Act 1 is a 510 × 70 stage and its
`.EV` grid is 128 × 18 (`ceil(510/4)`, `ceil(70/4)`). The first offset in the
table always equals `4 + block_w * block_h * 4`, i.e. the data region begins
immediately after the table.

Blocks are indexed `y * block_w + x`. Several blocks may share one offset, which
is how empty regions are collapsed — the vast majority of blocks point at a
record list whose count is zero.

## Block records

Each block begins with a `u16` count followed by exactly `count` records of
**12 bytes**. Verified: every block in every `.EV` file satisfies
`block_size == 2 + count * 12`.

Within a record, the following is **INFERRED and unproven**:

| Offset | Type  | Likely meaning |
|--------|-------|----------------|
| `0x00` | `u8`  | x position within the block |
| `0x01` | `u8`  | y position within the block |
| `0x02` | `u16` | object id |
| `0x04` | `u16` | flags |
| `0x06` | 6 bytes | per-object parameters |

The `u16` at `+0x02` behaves like an object id: Zone 1 Act 1's main `.EV` has 533
records drawing on 49 distinct values, with id 724 appearing 120 times — the
frequency profile expected of rings. The field at `+0x04` is dominated by `0` and
`0x8000`, which reads like a flag word. Neither has been confirmed against the
binary's spawn code, so `tools/stagemap.py` retains the full 12 raw bytes on
every `Placement` alongside the decoded guesses.

## Three files per act

Each act ships three `.EV` variants, e.g. for Zone 1 Act 1:

| File | Placements | Distinct ids |
|------|-----------|--------------|
| `ZONE11.EV`  | 533 | 49 |
| `ZONE11A.EV` |  34 |  1 |
| `ZONE11C.EV` | 121 |  5 |

The base file carries the main object set. The `A` and `C` variants are far more
specialised — `A` uses a single object id throughout. What selects between them
is **OPEN**; the plausible candidates are difficulty, character or co-op mode,
given Episode II's Tails mechanics, but this has not been established.

## Related, still undecoded

`.DC` and `.RG` files in the same archive share this exact block-grid header —
same quarter-resolution dimensions, same `u32` offset table, table ending exactly
where the data begins. Their per-block records are **not** 12 bytes, so the
record layout differs and remains unknown. Episode I's decompilation is no help
here: its `readDCFile`, `readRGFile` and `readEVFile` are all unimplemented
stubs, so these must be recovered from the Episode II binary directly.

## Usage

```sh
python tools/stagemap.py events G_ZONE1/MAP/ZONE11_MAP.AMB
```
