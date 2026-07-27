# Engine and binary survey — `Sonic.exe` (Beta 8)

What the executable is made of, and what that implies for re-implementation.

Binary: 4,302,848 bytes, SHA-256
`8ce7cc1d8fd4b35c4345e5b44a6f86a059445391685eaeb0eb93da05aa886224`,
timestamped 2012-04-17.

## Identity

| | |
|---|---|
| Engine | **AliceNN** (Dimps' application framework) over **SEGA NN Library for DirectX G2.0** (`nn Ver 1.01.06b`, built 2012-03-04) |
| Toolchain | MSVC 15.00.30729 — Visual C++ 2008 SP1, x86 |
| Graphics | Direct3D 9 + `d3dx9_43` (DirectX SDK June 2010) |
| Audio | **CRI ADX2** (CRI Audio 3.57.06, CRI File System 2.24.04), statically linked, DirectSound 8 output |
| Input | DirectInput 8 **and** XInput 1.3 |
| XML | TinyXML, compiled into AliceNN |
| Window title | `SONIC THE HEDGEHOG 4 Episode II (Beta8)` |

The source tree leaked through `assert()` `__FILE__` strings:
`e:\sega\sonic4ep2-beta\program\library\alicenn\source\library\...`. This is the
same AliceNN framework Episode I uses, which is the basis of the whole approach —
see the note on clean-room practice below.

## Scale

`.text` is 3,038,535 bytes containing roughly **8,000 functions** (rizin
recursive descent reports 8,007; an independent count of call targets preceded by
padding gives 7,770 — the two agree closely).

Bucketing `.text` by whether code references library-region or game-region
strings puts the boundary sharply at about `0x00660000`:

| Region | Size | Share |
|--------|------|-------|
| Game code | ~2.51 MB, ~6,600 functions | ~83% |
| Middleware (CRI, NN, TinyXML, CRT) | ~0.53 MB | ~17% |

Only the game portion needs reverse engineering. The middleware is replaced
wholesale rather than reversed.

## What makes this hard

- **No PDB.** The debug directory is stripped (`RVA=0, Size=0`) and the file
  contains no `RSDS`/`NB10` record anywhere.
- **No game-code RTTI.** 118 type descriptors exist, but 99 are `Cri*`, 8 are
  `TiXml*`, 5 are `std::`, and the only application class is `CAmDxInclude`
  (which only has RTTI because it derives from D3DX's `ID3DXInclude`). The game
  and NN translation units were built `/GR-`. **The class hierarchy cannot be
  recovered from RTTI** — it needs vtable-from-constructor analysis against the
  3,837 padded code pointers in `.rdata`.
- **`/LTCG` on 663 of 757 C++ objects.** Expect cross-module inlining, COMDAT
  folding merging identical functions to one address, and dissolved translation
  unit boundaries.
- **~82% of functions are FPO-optimised** (only 1,475 classic `push ebp` frames),
  so stack recovery needs a decompiler, not pattern matching.

## What makes it tractable

- **1,243 identifier strings survive** as task names and debug labels, and they
  name the module families directly: `gmPauseMenu::{Load,Build,Flush,Execute}`,
  `ssMapBuild`, `ss::gr::{CPostEffect,CReflect,CShadow}::{Build,Release,Update}`,
  `er::script::CService*`, `aoStorage::{Save,Load}`, `dmRankSys::Main`, `gsInit`,
  `objCamera`, plus task ids `GM_LOAD`, `GM_MAP_MAIN`, `GM_EVT_MGR`, `SY_EVT_SYS`,
  `IZ_FADE_SYS`.
- These are the **same prefixes Episode I uses** (`Am`, `Ao`, `Dm`, `Gm`, `Gs`,
  `Nn`, `Obj`, `er`), so Episode I's decompilation tells us what each family does.
- **930 asset path strings** give the full data layout, and `#AMB` is confirmed as
  the universal container.

## Shaders — the mobile question, answered

`NNSTDSHADER/SHADER.AMB` holds **1,843 shaders: 922 `ps_3_0` and 921 `vs_3_0`**,
all compiled Direct3D 9 bytecode carrying `CTAB` constant tables (verified by
decoding the version token of every one).

This is better news than compiled bytecode usually is. Shader Model 3.0 D3D9
bytecode with a constant table is exactly the input **MojoShader** consumes, and
MojoShader is already the shader path FNA uses to run XNA content on OpenGL. So
the mobile shader story is a translation step off the shelf rather than
re-authoring 1,843 shaders by hand. This should still be proven early with a
spike, because it is the assumption the whole mobile target rests on.

## Content inventory

Recovered from menu strings and asset paths:

- **Zones**: 4 numbered plus a final zone. Zone 1 has acts `ZONE11/12/13_MAP` plus
  `ZONE1BOSS_MAP`; zones 2–4 add a tileset-variant letter (`ZONE21A`, `ZONE32B`,
  `ZONE43B`, ...). `G_ZONEF` holds `ZONEF1_MAP` and `ZONEFBOSS_MAP`.
- **Stage id space**: `O_STGID001`–`O_STGID025`.
- **Special stages**: 7 (`SS01`–`SS07`).
- **Episode Metal is present**: `G_EP1COM`, `G_EP1ZONE1-4`, menu entries
  `EpisodeMetal1..4`.
- **Players**: `SON` (Sonic), `TLS` (Tails, split into `TLSBODY_*`/`TLSTAIL_*`),
  `MSN` (Metal Sonic), `SSON` (Super Sonic).
- **Gimmicks**: roughly 200 named, including the Episode II co-op set —
  `Coop01..05`, `Double01..06`, `TlsProp`, `TlsScrew`, `Transform`.
- **Bosses**: per-zone cue sets plus `FinalBoss01..21`.

## Things that must be replaced, not ported

- **Steam is a hard dependency.** `SteamAPI_Init() failed` and
  "You should pay for it before you play it." are fatal paths. The re-implementation
  stubs this interface out; it is not defeated, bypassed or circumvented.
- **CRI ADX2** is statically linked middleware and is replaced by an
  independent decoder for the `.CSB`/`.CPK` audio data.
- **Direct3D 9 fixed pipeline** is replaced by the portable renderer.

## Clean-room practice

Episode I's decompilation is public domain by its author's licence, but it is
still machine-derived from SEGA's code. It is therefore used here as a
**behavioural oracle** — read it to learn what a format means and how a system
behaves, then write our own implementation — rather than as a source tree to copy
from. Every format decoded in this project is independently verified against the
Episode II data before it is documented as fact. The AMB and stage-map decoders
were each confirmed this way.
