# Player physics

Episode II's player tuning, read out of `Sonic.exe` at **`0x00710520`**. Seven
rows of 108 bytes, one per playable mode.

```
python tools/physics.py Sonic.exe --json analysis/physics.json
```

## How it was found

Episode I's decompilation keeps its tuning in `g_gm_player_parameter[7]` as FX32
fixed-point integers — 12 fractional bits, so divide by 4096. Its Sonic row has a
jump impulse of `23130` and a gravity of `680`, which as floats are
**5.64697265625** and **0.166015625**.

Episode II stores the same table with the speeds converted to `float`. So the
search is for one distinctive bit pattern followed immediately by another. That
pair occurs 21 times in the executable and never by coincidence, and the table
base is referenced from six places in code.

**What confirms it is not the floats.** At offset 32 of every row sit four `u16`
counters: `time_air` **1800**, `time_damage` **180**, `pool_max` **96**,
`fall_wait_time` **24**. Those are Episode I's values, unchanged, in the struct
positions Episode I puts them — packed two-per-dword where Episode I used four
separate `int`s. No float search would have surfaced them, and nothing else in
`.rdata` would produce that run by accident.

Four of the seven jump impulses also match Episode I to the bit: 5.6470 (Sonic),
7.9841 (Super), 4.7058 and 6.6534 (the Mad Gear rows).

## The table

Units are **game pixels per frame at 60 Hz**.

| Row | Mode | Accel | Top | Decel | Jump | Gravity | Air accel | Air drag | Coyote |
|----:|------|------:|----:|------:|-----:|--------:|----------:|---------:|-------:|
| 0 | Sonic | 0.0354 | 9 | 0.125 | 5.6470 | 0.16602 | 0.0625 | 0.0625 | 24 |
| 1 | Super Sonic | 0.1062 | 15 | 0.5 | 7.9841 | 0.16602 | 0.1875 | 1.0 | 24 |
| 2 | Special Stage | 0.0354 | 9 | 0.125 | 5.6470 | 0.16602 | 0.1875 | 0.0625 | 24 |
| 3 | Pinball | 0.0354 | 9 | 0.125 | 5.6470 | 0.16602 | 0.0625 | 0.0625 | 24 |
| 4 | Pinball Super | 0.1062 | 15 | 0.5 | 7.9841 | 0.16602 | 0.1875 | 1.0 | 24 |
| 5 | Mad Gear | 0.0354 | 10 | 0.125 | 4.7058 | 0.16602 | 0.0625 | 0.125 | 120 |
| 6 | Mad Gear Super | 0.1062 | 11 | 0.125 | 6.6534 | 0.16602 | 0.1875 | 0.25 | 120 |

Gravity is identical in all seven rows. Terminal velocity is 15 throughout.

## What Episode II retuned

Same structure, different feel. Against Episode I's Sonic row:

| Field | Episode I | Episode II |
|-------|----------:|-----------:|
| Ground deceleration | 0.25 | **0.125** |
| Slope factor, running | 0.046875 | **0.0625** |
| Air drag | 0.5 | **0.0625** |
| Mad Gear top speed | 6.0 | **10.0** |
| Mad Gear coyote frames | 240 | **120** |

The air drag change is the big one: Episode II bleeds airborne speed at an eighth
of Episode I's rate, so a jump carries much further.

Episode II also has one field Episode I does not, at slot 13 between
`spd_slope_spin` and the pipe variant, reading 0.078125 for Sonic. Its purpose is
unknown.

## Field order

27 dwords per row, from Episode I's `GMS_PLY_PARAMETER`:

```
 0 spd_add            ground acceleration      14 slope factor, pipe
 1 spd_max            top speed                15 slope factor, pinball
 2 spd_dec            ground deceleration      16 spd_jump      jump impulse
 3 spd_spin           spin dash base           17 spd_fall      gravity
 4 spd_add_spin       spin dash per rev        18 spd_fall_max  terminal velocity
 5 spd_max_spin       spin dash cap            19 push_max
 6 spd_dec_spin       rolling friction         20 spd_jump_add  air acceleration
 7 spd_max_add_slope  downhill top-speed bonus 21 spd_jump_max  air speed cap
 8 u16 time_air, u16 time_damage               22 spd_jump_dec  air drag
 9 u16 pool_max, u16 fall_wait_time            23 pinball acceleration
10 spd_slope          slope factor, running    24 pinball top speed
11 spd_slope_max      slope speed cap          25 pinball deceleration
12 spd_slope_spin     slope factor, rolling    26 pinball downhill bonus
13 (no Episode I counterpart)
```

## Units, and the conversion this port uses

The constants are game pixels per frame. This port's world units come from the
tile models, where a cell is 20 units — measured, not assumed: of 836 tile meshes
across four zones, 259 are exactly 20 wide and 189 exactly 20 tall, with 40 and 10
as the next most common.

A collision cell spans **64 game pixels** (64 height columns, and 32 height units
at two pixels each — see [`FORMAT-COLLISION.md`](FORMAT-COLLISION.md)). So:

```
1 game pixel = 20 / 64 = 0.3125 world units
```

`PlayerPhysics.WorldPerPixel` is that factor. The table keeps the game's own
numbers and `Player` converts, rather than storing pre-scaled values that would
no longer be comparable against the binary.

Sanity check: top speed 9 px/frame at 60 Hz is 540 px/s, or 8.4 cells per second.
Jump apex is `v² / 2g` = 5.647² / (2 × 0.16602) ≈ 96 px, a little over one and a
half cells.

## The jump cut is not a clamp

The usual Mega Drive short hop clamps upward speed to a fixed value when the
button is released. Episode I — and this port follows it — does something else:

> if the button comes up while still rising faster than **4 px/frame**, set a
> flag; while that flag is set and the player is still rising, apply gravity a
> **second** time each frame.

So the cut is a doubled gravity for the remainder of the rise, not a ceiling on
it. Releasing at the very top of a jump does nothing at all, because the rise is
already below the 4 px/frame threshold.

## Slopes

`SlopeFactor` and `SlopeSpeedMax` are wired in. Episode I's form is

```
ground speed += slope_factor * sin(ground angle)
```

capped at `SlopeSpeedMax`, which is a **separate and higher limit** than running
top speed — 13 against 9. That is the only way a slope can carry you past top
speed, and it only works because Episode I's `ObjSpdUpSet` never pulls an
already-faster value back down to the limit. A plain clamp would undo the slope
term every frame; `Player.SpeedUp` reproduces the asymmetry.

A consequence of the recovered numbers worth knowing: **standing still on a 45
degree slope does not make you slide.** Deceleration is 0.125 per frame and the
slope contributes only `0.0625 * sin(45) = 0.044`, so friction wins. That is
Episode II's tuning, not a shortcut in this port.

## Not yet used

Recovered and sitting in the table unused: everything about spin dash and
rolling, `push_max`, and the pinball row.
