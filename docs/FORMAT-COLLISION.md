# Stage collision (`.DF` / `.DI` / `.AT`)

Each zone ships three collision files in `ZONE<n>_ATTR.AMB`:

    .DF   height fields   - the actual ground shape
    .DI   surface angles
    .AT   character attributes (through, cliff, grind)

Status: **structure VERIFIED, addressing OPEN.** The file layout below holds
exactly on all **39** stage collision files in the build. What is *not* yet known
is how a stage's `_ATTR_` cell id selects a record — see the note at the bottom.

## Layout

    0x00  u16   count      (1535 for every Zone 1 file)
    0x02  u16   records
    0x04  u8    reserved[count * 2]   - zero in every observed file
          ...   records[records][size]

with `size` = 4096 for `.DF` and 64 for `.DI` and `.AT`. The equation
`4 + count*2 + records*size == filesize` is satisfied by all 39 files, across
sizes from 4 KB to 327 KB, which is what makes the split trustworthy even though
the reserved block is empty.

## Height records

A `.DF` record is **64 cells of 64 bytes**. Each cell's 64 bytes are a height per
pixel column, valued 0-63 — the classic per-tile height array, and the reason
slopes are possible at all.

Observed values bear that out: flat-full cells read as 64 bytes of `0x20` (32),
empty cells as 64 bytes of `0x00`, and slopes and curves as intermediate values.
Measured over 8.4 million height bytes, 0 and 32 dominate and 1..31 carry the
shaped ground. The full 0..255 range is technically used, but only **0.02%** of
bytes exceed 63, so a full cell is 32 units tall and the rest are rare special
cases rather than ordinary geometry.

`.DI` and `.AT` records are 64 bytes, one byte per cell of the same 8x8 block.

## Surface angles, and what they prove about the height scale

A `.DI` byte is the cell's **surface angle, a full turn per 256**, measured in a
Y-up frame — so it runs opposite to the grid, whose Y grows downward. Flat ground
reads 0.

This was settled by checking `.DI` against `.DF`, since the two describe the same
surfaces in different ways: fit a least-squares gradient through a cell's height
field, convert to degrees, and compare. Across **23,474 shaped cells in all 13
attribute archives the median disagreement is 5.7 degrees**, with 75% inside 15.
Reproduce it with:

```
python tools/collision.py angles .
```

The fit only works at one scale, and that is the useful part. **A height unit is
two pixels.** A cell is 64 columns wide but heights only reach 32, and the
question of whether that means a squat cell or a coarse vertical scale is not
answerable from the height data alone. The angle fit answers it: at 1:1 the median
error is 16.9 degrees, at 2:1 it is 5.7. The remaining error is inherent — a cell
that curves has no single angle, and the game stores one anyway.

Sign and scale were found by sweeping every combination of scale, sign and 90
degree offset and keeping the best, rather than by assuming a convention.

## The gimmick variant

55 further collision files live in the `*_COL.AMB` gimmick archives
(`GMK_GEAR_*`, `GMK_RAIL_*`, `GMK_BREAK_*`). They carry **no header at all** -
their first four bytes read as zero counts - and are plain record arrays, mostly
whole multiples of 4096 or 64. They need a separate path and are not handled here.

## Resolved: how an `_ATTR_` id reaches a record

**How a stage's `_ATTR_` cell id selects a collision record is not known.**

The `count * 2` reserved block looked like an id-to-record index, and the
arithmetic supports one, but it is **entirely zero** in every file. Zone 1 Act 1
uses ATTR ids 481..1533 against only 79 `.DF` records, so the ids cannot index
the records directly either.

This is not recoverable from the data alone; it needs the routine in `Sonic.exe`
that loads a stage's collision. Until it is found, `CollisionMap` treats any
non-zero attribute as fully solid, which is why the playable slice has blocky
ground rather than slopes.

## Usage

```sh
python tools/collision.py show   G_ZONE1/MAP/ZONE1_ATTR.AMB
python tools/collision.py verify .
```
