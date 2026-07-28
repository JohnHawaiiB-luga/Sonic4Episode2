# Player physics

Episode II's player tuning, read out of `Sonic.exe` at **`0x00710520`** —
**3 characters of 11 modes**, each row 108 bytes.

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

## The shape

The layout is the engine's own, not something counted off the data.
`Sonic.exe:0x0046AEA1` scales a character id straight into the table:

```asm
movzx ecx, byte [esi + 0x34f0]   ; character id, from the player work struct
imul  ecx, ecx, 0x4a4            ; 1188 bytes per character
fld   dword [ecx + 0x710520]     ; that character's first field
```

1188 is exactly 11 rows of 108, and the fourth such block is unrelated data — so
**3 characters of 11 modes**.

Characters 0 and 1 are physically identical bar one slope field, and both have a
Super mode. Character 2 has none: its Super row simply repeats its normal values,
which is what you would expect of Metal Sonic.

## The modes

Modes 0 to 6 match Episode I's `char_id` enumeration in order *and* in value, so
the names are inherited from it. Modes 7 and 8 have no Episode I counterpart, and
9 and 10 are unidentified — they carry ordinary values.

Units are **game pixels per frame at 60 Hz**. Character 0:

| Mode | Name | Accel | Top | Decel | Jump | Gravity | Air accel | Air drag | Coyote |
|-----:|------|------:|----:|------:|-----:|--------:|----------:|---------:|-------:|
| 0 | Normal | 0.0354 | 9 | 0.125 | 5.6470 | 0.16602 | 0.0625 | 0.0625 | 24 |
| 1 | Super | 0.1062 | 15 | 0.5 | 7.9841 | 0.16602 | 0.1875 | 1.0 | 24 |
| 2 | Special Stage | 0.0354 | 9 | 0.125 | 5.6470 | 0.16602 | 0.1875 | 0.0625 | 24 |
| 3 | Pinball | 0.0354 | 9 | 0.125 | 5.6470 | 0.16602 | 0.0625 | 0.0625 | 24 |
| 4 | Pinball Super | 0.1062 | 15 | 0.5 | 7.9841 | 0.16602 | 0.1875 | 1.0 | 24 |
| 5 | Mad Gear | 0.0354 | 10 | 0.125 | 4.7058 | 0.16602 | 0.0625 | 0.125 | 120 |
| 6 | Mad Gear Super | 0.1062 | 11 | 0.125 | 6.6534 | 0.16602 | 0.1875 | 0.25 | 120 |
| 7 | Slowed 1 | 0.00177 | 0.225 | 0.0125 | 2.8235 | 0.16602 | 0.003125 | 0.00625 | 24 |
| 8 | Slowed 2 | 0.00531 | 0.375 | 0.05 | 3.9921 | 0.16602 | 0.009375 | 0.1 | 24 |

Modes 7 and 8 are a fortieth and a twenty-fourth of normal top speed, with
**gravity untouched** — so they are slowed movement rather than slow motion.
Terminal velocity is 15 and gravity 0.16602 in every row of the table.

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

## Rolling

Rolling is wired and runs entirely on Episode II's own numbers. It gives up
steering completely and coasts on `RollFriction` **0.03125** against a running
`GroundDeceleration` of **0.125** — four times lighter, which is the whole reason
to do it — and it takes the stronger `SlopeFactorRolling` of **0.15625** against a
running **0.0625**, so a curled Sonic outruns a running one downhill.

Episode I halves the rolling friction while the stick is held into the direction
of travel and doubles it otherwise, so steering into a roll extends it and
steering against one kills it. Those are *shifts of Episode II's own value*, not
magnitudes taken from Episode I.

One number here is **not recovered**: the speed below which a roll ends and above
which one can start. Episode I calls it `GMD_PL_STOP_SPD` and sets it to 0.5
px/frame, which is what `Player.RollThreshold` uses — but 0.5 occurs 168 times in
Episode II's constant pool, so unlike the parameter table it could not be
confirmed. It is flagged as such in the code.

## Spin dash

Fully recovered and implemented. The charge values are in the per-character table
— base **3.0**, **2.0** per revolution, capped at **10.0** — and the launch
constants are globals, stored as doubles and read off the launch expression at
`0x00513005`:

```asm
fld   dword [esi + 0x3518]   ; charge
fmul  qword [0x743ea0]       ; 0.5
fadd  qword [0x744030]       ; 8.0
```

So **`launch = 8.0 + charge * 0.5`**. One charge gives **9.5 px/frame** and a full
one **13.0**.

Episode I's is `11.75 + charge * 0.125`, spanning 12.125 to 13.0. **Episode II
kept the ceiling and dropped the floor**, so charging went from nearly pointless
to worth two thirds of the move's speed. That is a real difference in how the
move plays, and it is exactly why beat 28 refused to port Episode I's formula:
`11.75` appears nowhere in Episode II, and guessing would have felt wrong in a way
players notice.

While winding up, the charge bleeds **proportionally** — `charge -= charge *
0.03125` per frame, through the same decrease-toward-zero helper the ground
friction uses at `0x005A8800`. Hesitating costs speed.

The player work struct holds the live charge at `+0x3518`; the live copy of every
table field sits at `+0x3578 + field * 4`, which is how these were found — the
copy routine at `0x0046AE84` compares `[esi+0x3578]` against table field 0 and
`[esi+0x357c]` against field 1.

## Springs

The first object with behaviour. A spring is a trigger box that launches the
player upward and re-arms when the player leaves it.

**The impulse is Episode I's, not recovered.** Episode I launches at
`7.5 + 1.5 * intensity` px/frame (`GMD_GMK_SPRING_SPD` 30720 FX32). Episode II's
spring handler at `0x004F7570` was read and its reachable constants are timing
values — no 7.5 anywhere — so its launch speed arrives some way not yet traced.
`Springs.ImpulsePixels` carries the Episode I base, flagged like `RollThreshold`.

One real bug fell out of building it: the jump-release cut used to arm on *any*
rise, so a spring launch with the button up read as a released jump and got double
gravity. Springs would have felt weak for an invisible reason. The cut is now
scoped to rises that came from the jump button, and there is a test asserting a
bounce cannot be cut.

## Not yet used

Recovered and sitting in the table unused: `push_max` and the pinball row.
