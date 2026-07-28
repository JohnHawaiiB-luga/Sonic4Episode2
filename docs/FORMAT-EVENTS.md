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

Record layout:

| Offset | Type  | Meaning |
|--------|-------|---------|
| `0x00` | `u8`  | x position within the block |
| `0x01` | `u8`  | y position within the block |
| `0x02` | `u16` | object id |
| `0x04` | `u16` | flags |
| `0x06` | `s8`  | bounding box left |
| `0x07` | `s8`  | bounding box top |
| `0x08` | `u8`  | bounding box width |
| `0x09` | `u8`  | bounding box height |
| `0x0A` | `u16` | per-object parameter |

The `u16` at `+0x02` behaves like an object id: Zone 1 Act 1's main `.EV` holds
533 records drawing on 49 distinct values, with id 724 appearing 120 times. The
field at `+0x04` is dominated by `0` and `0x8000`.

Confidence: the 12-byte stride and the count are **VERIFIED**. The field split is
**corroborated but not proven** — it comes from Episode I's equivalent structure
and produces sane values on Episode II data (bounding boxes like 128×40, a record
at world origin that is plausibly the player start), but it has not been checked
against Episode II's own spawn code. `tools/stagemap.py` keeps the full 12 raw
bytes on every `Placement` so nothing is lost if the split turns out wrong.

## Block pitch and world coordinates

One block covers **256 × 256 pixels**, so a record's absolute position is:

```
world_x = block_x * 256 + record.x
world_y = block_y * 256 + record.y
```

This follows from the grid being the map at quarter resolution: four map cells
per block across 256 pixels makes each map cell 64 pixels square. Placement
positions computed this way land inside the stage bounds on every act checked.

## Confirmed from the engine's own reader

The record layout below is no longer an inference. `Sonic.exe:0x0053d541` is the
loop that streams these records and spawns from them:

```asm
movzx edx, word [esi + 2]      ; object id
movzx ecx, byte  [esi + 1]     ; local Y
add   ecx, dword [esp + 0x18]  ; + block origin Y
movzx eax, al                  ; local X, from [esi + 0]
add   eax, dword [esp + 0x14]  ; + block origin X
...
mov   ecx, 0x323               ; 803
cmp   dx, cx
jae   skip                     ; ids at or above 803 are ignored
mov   eax, dword [edx*4 + 0x7031c8]
```

So byte 0 is local X, byte 1 is local Y and the id is the `u16` at 2 — and the
dispatch table's 803 entries are the engine's own bound, not something counted
off a pointer run.

## `.DC` and `.RG` — same grid, different records

These share the block-grid header exactly and differ only in record size,
verified across the build:

| Extension | Stride | Layout |
|-----------|--------|--------|
| `.EV` | 12 bytes | placements, above |
| `.DC` |  4 bytes | `u8 x, u8 y, u16 id` |
| `.RG` |  2 bytes | `u8 x, u8 y` |

**`.RG` is rings, and the counts prove it.** Acts carry 192 to 489 records, boss
arenas carry exactly 12, and cutscenes carry none — 7,567 across the 34 acts in
the build, every one inside its stage bounds. Nothing else in a Sonic act is
numerous in that particular way, and a record carrying nothing but a position is
what you would expect when the type is implicit in the filename.

This also settles a question that looked harder than it was. The most-placed
`.EV` object ids — 715, 724 and 716, nearly 2,900 placements between them — are
**not** rings and no `.EV` id is. Rings never had an object id to find.
`tools/stagemap.py` reads all three block files through `read_blocks(data, stride)`.

Note that an all-empty file trivially satisfies *any* stride, so stride tests
must be run on files that actually contain records.

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

## Object ids — recovered

Every id resolves to its engine class, read from the symbolized Android build's
dispatch table (`tools/dispatch.py`, `analysis/object-names.json`). **679 of 714
ids named; 99.8% of all 13,588 placements across the 30 acts resolve.** The base
is anchored on two ids proven from placement statistics — id 443 is the start
marker, id 520 the goal — and exactly one alignment puts `GmGmkStartInit` on 443
and `GmGmkGoalPanelInit` on 520 at once. See `ObjectCatalog` and the beat-59
handoff. The stripped Windows build could only ever guess these from nearby
string immediates; the Android build names them outright.

### Recovered gimmick constants

Read from the named handlers, verified against Episode II's own bytes:

| Gimmick | Constant | Value | Source |
|---------|----------|-------|--------|
| **Dash panel** | boost speed | **13.5 px/frame** | direction table `0x0096C658`, read by `GmPlySeqInitDashPanel` (`0x005D2254`) |
| Dash panel | directions | right / left / up×2 / down×2, as `(±13.5, 0)`, `(0, ±13.5)` | same table, 8 entries |
| Dash panel | no-friction window | 12 frames | **still Episode I's — not recovered** |
| **Spring** | directions | 8 compass angles `0, 8192 … 57344` (A16) | table `0x00961D34`, indexed by `GmGmkSpringInit` (`0x00563CB0`) |
| Spring | vertical speed cap | 9.0 px/frame | `GmPlySeqInitSpringJump` (`0x005D1520`) clamp |
| Spring | base launch speed | 7.5 px/frame | **still Episode I's — passed as an argument, no named constant** |

The dash panel's 13.5 was long flagged "Episode I's, not recovered"; it is now
read from Episode II's own direction table, every entry of which has magnitude
13.500. Each gimmick's *direction* is selected in its object by the placement
record's flag byte (offset +4); the flag-to-variant bit mapping is the remaining
open piece for both.

## Usage

```sh
python tools/stagemap.py events G_ZONE1/MAP/ZONE11_MAP.AMB
```
