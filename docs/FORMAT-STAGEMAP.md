# Stage layout format (`.MP` / `.MD`)

Stage geometry for Sonic the Hedgehog 4 Episode II is stored as a stack of
parallel 2D grids inside `<zone><act>_MAP.AMB`. Every grid in a stage shares the
same dimensions and is addressed as `cells[y * width + x]`.

Status: **VERIFIED**. All 400 `.MP`/`.MD` grids across `G_ZONE1-4`, `G_ZONEF`,
`G_SS` and `G_EP1ZONE1` resolve to exactly 2 and 1 bytes per cell against their
own header dimensions, with no remainder. Rendered previews of Zone 1 Act 1 show
coherent platformer terrain, and the `_ATTR_B` layer reproduces the `_B` layer's
silhouette exactly in a different value space.

## Layout

Both files share a four-byte header, little-endian:

| Offset | Type  | Field  |
|--------|-------|--------|
| `0x00` | `u16` | width  |
| `0x02` | `u16` | height |
| `0x04` | ...   | `width * height` cells |

`.MP` cells are `u16`. `.MD` cells are `u8`. There is no padding, no palette and
no compression — the body length is exactly `width * height * depth`.

## `.MP` cell bitfield

A `.MP` cell is not a bare tile index — it packs a transform:

| Bits | Field |
|------|-------|
| 0–11  | tile id (12 bits) |
| 12–13 | rotation (0–3) |
| 14    | horizontal flip |
| 15    | vertical flip |

Verified across **512,070 non-zero cells** spanning every zone: the widest tile
id observed is 2779, comfortably inside 12 bits, and every high-nibble value that
occurs (1, 3, 4, 8, 12) decodes to a coherent transform. Transforms are rare —
99.8% of cells carry none — but they are definitely used, and adjacent cells
frequently appear as mirrored pairs, which is exactly what symmetric level
geometry should look like.

Corroborated afterwards by Episode I's `MP_BLOCK` struct, which applies the same
masks.

`tools/stagemap.py` exposes this as `Grid.tile(x, y) -> (id, rot, flip_h, flip_v)`.

## Layers

Zone 1 Act 1 (`ZONE11_MAP.AMB`, 510 × 70) is representative:

| Layer | Type | Occupancy | Distinct | Role |
|-------|------|-----------|----------|------|
| `_A.MP`      | u16 | 3.2%  | 60  | foreground tile ids |
| `_A.MD`      | u8  | 1.0%  | 28  | companion to `_A` |
| `_B.MP`      | u16 | 49.1% | 108 | main terrain tile ids |
| `_B.MD`      | u8  | 3.6%  | 28  | companion to `_B` |
| `_ATTR_A.MP` | u16 | 4.2%  | 213 | collision/attribute ids for `_A` |
| `_ATTR_B.MP` | u16 | 52.7% | 277 | collision/attribute ids for `_B` |
| `_N.MP/.MD`  |     | 2.3%  | 8   | near layer |
| `_M.MP/.MD`  |     | 4.1%  | 61  | parallax layer |
| `_M1`–`_M3`  |     | 0.1–1.5% |  | further parallax layers |

Cell value `0` means empty, which is why occupancy is low on every layer except
the main terrain.

`_ATTR_*` grids are strictly parallel to their `_A`/`_B` counterparts: the value
histogram of `ATTR_A.MP` matches `A.MP` count-for-count on its most common ids
(338, 200, 192, 48, 41, 35, 32 occurrences), i.e. one attribute cell per tile
cell rather than a separate coarser grid.

The relationship between a `.MP` layer and its `.MD` companion is **OPEN**. The
`.MD` grids are far sparser than their `.MP` partners and draw from a small id
space (28 distinct values), which is consistent with a per-cell variant, flip or
animation selector, but this has not been proven.

## Dimensions vary per act

Stage size is per-act, not fixed: Zone 1 Act 1 is 510 × 70 and Act 2 is 470 × 80.
Any consumer must read the header rather than assume a size.

## Companion files in the same archive

`ZONE11_MAP.AMB` also holds, alongside the 16 grids:

- `ZONE11.EV`, `ZONE11A.EV`, `ZONE11C.EV` — event / object placement scripts,
  three variants per act. Not yet decoded.
- `ZONE11.DC` — not yet decoded.
- `ZONE11.RG` — region data, not yet decoded.

Entry names in these archives carry a leading `.\.\` path prefix, which consumers
should strip.

## Usage

```sh
python tools/stagemap.py info   G_ZONE1/MAP/ZONE11_MAP.AMB
python tools/stagemap.py render G_ZONE1/MAP/ZONE11_MAP.AMB out/ --scale 2
```

`render` writes one PNG per layer using a stable colour hash per cell id, which
is the quickest way to sanity-check a decode by eye. It has no third-party
dependencies.
