# Object ids

A stage's `.EV` records say *where* something is and *which number* it is, and
nothing else. This is where the numbers get their meaning.

Unlike every other format in this project, none of it is in the data files. It
all comes out of `Sonic.exe`. The tool is `tools/objects.py`; run it against the
executable and it reproduces everything below.

```
python tools/objects.py Sonic.exe --json analysis/object-handlers.json
```

## The dispatch table

At **`0x007031C8`** sits an array of 803 function pointers indexed *directly* by
object id. Slot *n* is the function that spawns object *n*, or null when nothing
uses that id.

It was found structurally rather than by tracing code. Scanning the data sections
for long runs of pointers into `.text` turns up one table of 803 slots holding
714 code pointers, and the stage data agrees with it: of the 472 distinct object
ids used across every `.EV` file in the build, **469 land on a non-null slot**.
Ids that go unused in this build are mostly still populated, which is what you
would expect of a shipping table trimmed by level design rather than by the
linker.

## Variants share a spawn function

714 live ids collapse to **382 distinct functions**. Ids that share one are the
same object in different flavours, and the handler works out which by subtracting
its own base id. From the handler at `0x004A7580`:

```asm
movzx eax, word [edi + 2]      ; object id, straight out of the .EV record
mov   edx, 0x295               ; 661
cmp   dx, ax
ja    skip                     ; below 661 - not mine
mov   ecx, 0x29c               ; 668
cmp   ax, cx
ja    skip                     ; above 668 - not mine
sub   edx, 0x295
mov   dword [esi + 0x3c4], edx ; variant number, 0..7
```

So ids 661 through 668 are one object with eight settings. The largest family is
28 ids wide; 318 of the 382 functions serve exactly one id.

## What this confirms about `.EV`

The same routine reads the placement record, which independently confirms the
layout in [`FORMAT-EVENTS.md`](FORMAT-EVENTS.md) — previously supported only by
all 79 files parsing:

| Offset | Field | Evidence |
|--------|-------|----------|
| +2 | `u16` object id | `movzx eax, word [edi + 2]` |
| +4 | `u16` flags | `movzx eax, word [edi + 4]` |

The flags word is a bitfield, not a scalar. The same handler pulls two separate
2-bit fields out of it:

```asm
movzx eax, word [edi + 4]
shr   eax, 4
and   eax, 3                   ; bits 4-5
movzx ecx, word [edi + 4]
shr   ecx, 6
and   ecx, 3                   ; bits 6-7
```

## Instance size and priority

Every handler builds its object through one shared constructor at
**`0x004834C0`**, pushing the instance size and a scheduler priority:

```asm
push  0x1500                   ; priority
push  0x3dc                    ; 988 bytes
sub   esp, 8
fstp  dword [esp + 4]          ; spawn Y
fstp  dword [esp]              ; spawn X
call  0x4834c0
```

Reading those two pushes back out across all handlers gives a size for 668 of the
714 ids. They cluster into small objects around 956-980 bytes and stateful ones
around 2576-2656.

**Priority `0x1500` is the engine's real object priority** — 270 of the 294
handlers whose call could be read pass exactly that. The port used a guessed
`0x2000` before this; `GameEngine.PriorityObject` now takes the measured value.

## Names, and how far to trust them

An object references a name string only when it loads a named asset, so most ids
have no name to find. Following each handler and the functions it calls yields
**116 named ids across 45 distinct names** — `WaterSlider`, `CandleStick`,
`Propeller01`, `SandBranch03`, `MS_Homing`, `Spring`, `Switch`, `Spear`,
`Boss3_01` and so on. All are recognisably Episode II content, and all sit in the
zone you would expect.

**They are not equally trustworthy, and the catalogue says which is which.** Only
27 were read from the handler's own body; the other 89 came from a function the
handler calls, and a shared helper can hand its own string to an object it merely
assists. `ObjectCatalog.Entry.Direct` marks the difference.

Two things were tried and rejected:

- **Following two call levels** raised the count to 273, but it also attributed
  the task name `GM_LOAD_BBM` to five unrelated high-traffic objects. Depth is
  bought with precision here, so the search stops at one level.
- **Propagating a name across a whole family** looked like free coverage and
  gained nothing: because siblings share a function, they already share whatever
  name that function references.

The most-placed objects in the game — ids 715, 724 and 716, together nearly 2,900
placements — remain unnamed for exactly the reason above. They load no named
asset. Identifying those needs behaviour, not strings.

## A trap worth recording

Function bodies are scanned to the first run of `int3` padding. That byte value,
`0xCC`, also turns up inside ordinary immediates, so the naive scan truncated
most functions to nothing and found names for only 11% of ids. Refusing to
believe any body shorter than 512 bytes took it straight to 38% on the same
search. When a boundary heuristic silently agrees with you, check what it is
actually matching.

## Where an object's model lives

Gimmicks ship as **`EP2_GMK_<NAME>_MDL.AMB`**, with `_TEX` and `_MTN` archives
beside them under the same stem. There are **65** across the build, in each
zone's `GMK` directory and in `G_COM/GMK`.

The catalogue's names and those stems are obviously the same naming scheme, and
just as obviously not a mechanical transform:

| Object name | Archive stem | Rule |
|-------------|--------------|------|
| `Jetwall04` | `JETWALL` | strip trailing digits |
| `SandBranch03` | `SAND_BRANCH` | strip digits, split camel case |
| `MetalUnit03` | `METAL_UNIT` | as above |
| `Avalanche01` | `AVLNCH` | **abbreviated** |
| `CandleStick` | `SCONCE` | **renamed** |
| `SandTrank01` | `SAND_TANK` | **the game's own typo** |

`ObjectModels` does the mechanical part only — strip digits, try the name, the
`UPPER_SNAKE` form, and the underscore-free form — which resolves **8 of the 45
recovered names**.

**The abbreviated ones are deliberately left unresolved.** `AVLNCH` is very
probably `Avalanche`, and a table of very-probablies is how bad data gets into a
project that is careful everywhere else. They resolve when something confirms
them, not before.

## What is still open

- Ids with no name need identifying from behaviour — what they read, what they
  collide with, what sound they play.
- The parameter at record `+10` is not decoded; it is presumably the variant
  detail each handler reads after the id range check.
- The spawn context passed to a handler (`{record*, float x, float y}`) is only
  partly mapped.
