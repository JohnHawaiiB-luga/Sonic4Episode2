# The oracles — what we read, and what a `.so` actually is

This project is a **clean-room reimplementation**. We never copy code. We read
reference binaries to learn *what the game does*, then write our own
implementation and verify it against the game's own data. A binary read that way
is an **oracle**: it answers questions, it is not a source.

This document explains the three oracles we use, and — because it is the one that
needs explaining — what an Android `.so` file is and why this particular one
broke open blockers that had resisted months of work.

---

## The three oracles

| Oracle | What it is | What it is good for |
|---|---|---|
| **Beta 8 PC build** (`Sonic.exe` + 1.2 GB of data) | Windows, x86 32-bit, **stripped** | The **reference**. Every format claim and constant is verified against *these* bytes. It is also what we ship against. |
| **`libfox.so`** (Android) | ARM, **~24,000 named functions** | The **map**. Tells us which function to read, and what it is called. Code only — it ships with no game data. |
| **Episode I decompilation** (`Sonic4Episode1-master`) | C# from a managed .NET build, public domain | The **cross-check**. A different game on the same engine, so it confirms shapes and names without proving Episode II's values. |

The division of labour matters: `libfox.so` says *where to look*, the Beta 8 data
says *whether we are right*, and Episode I gives an independent second opinion.
No single one of them is trusted alone.

---

## What is a `.so` file?

**`.so` = "shared object".** It is Linux and Android's equivalent of a Windows
`.dll` — a compiled library of machine code and data that a program loads at
runtime rather than containing itself.

- On Windows, executables and libraries use the **PE** container format
  (`Sonic.exe`, `steam_api.dll`).
- On Linux and Android, they use **ELF** — *Executable and Linkable Format*.
  Our file literally begins with the four bytes `\x7f E L F`.

Android apps are Java/Kotlin, but performance-critical games are written in C++
and compiled with the **NDK** (Native Development Kit) into `.so` libraries. The
APK carries them under `lib/<abi>/`. Sonic 4 Episode II's entire engine is one
such library, named `libfox.so` — "fox" being the internal project name.

### Why there are two of them

Android devices have different CPUs, so an APK ships one build per **ABI**
(Application Binary Interface):

| File | Architecture | Size | Defined functions named |
|---|---|---|---|
| `arm64-v8a/libfox.so` | ELF64, AArch64 (64-bit ARM) | 11.7 MB | 23,890 |
| `armeabi-v7a/libfox.so` | ELF32, ARM (32-bit) | 12.5 MB | **29,129** |

These are **two compilations of the same source code**. That is useful twice
over:

1. **The 32-bit build names ~5,200 more functions than the 64-bit one** we have
   been using — different inlining decisions leave more symbols standing. If a
   function is not named in `arm64-v8a`, look in `armeabi-v7a` before concluding
   it is anonymous. *(We have not exploited this yet.)*
2. Pointer size differs (8 vs 4 bytes), so struct field offsets shift by known
   amounts between the two builds. Comparing them lets you **solve** for which
   fields are pointers and confirm a layout, rather than guessing.

---

## Why this `.so` is valuable — stated precisely

The headline is that it carries names for ~24,000 functions while `Sonic.exe` has
none. But the usual explanation ("it's a debug build") is not quite what the file
shows, and the distinction is worth recording.

An ELF binary can carry two symbol tables:

- **`.symtab`** — the *full* symbol table, including internal/static functions.
  This is what `strip` removes, and what a debug build keeps.
- **`.dynsym`** — the *dynamic* symbol table. A shared library **must** have this
  to export anything, so stripping never removes it.

**`libfox.so` has no `.symtab`.** By the conventional definition it *is*
stripped. What it has is an enormous `.dynsym`: 29,336 entries and 1.1 MB of name
strings, because the engine was compiled **without hiding its symbols** (no
`-fvisibility=hidden`), so nearly every C++ function was exported into the
dynamic table.

So the accurate statement is: *this build exports essentially its entire internal
API*. Whether it was literally a developer build is a provenance claim about
where the file came from, not something the bytes attest. Either way the practical
effect is the same and enormous — the names are real, including C++-mangled ones
carrying full parameter types.

---

## ELF anatomy, and what each part actually got us

A `.so` is divided into **sections**. These are the ones that mattered, with the
concrete result each produced:

| Section | Size | What it is | What it got us |
|---|---:|---|---|
| `.text` | 5.96 MB | Executable code | Everything we disassemble — `nnCalcMatrixPaletteNode`, `GmGmkItemInit`, … |
| `.rodata` | 848 KB | Read-only constants | The **dash-panel velocity table** (`0x0096C658`, all magnitudes 13.500) and the **spring angle table** (`0x00961D34`) |
| `.dynsym` | 704 KB | Symbol table: name → address | The ~24,000 names. This is the whole reason the file is useful |
| `.dynstr` | 1.15 MB | The name strings themselves | — |
| `.rela.dyn` | 992 KB | Relocations | **Found the 803-slot object dispatch table** (see below) |
| `.rela.plt` | 251 KB | Relocations for calls | **Solved the item-effect mapping** (see below) |
| `.plt` / `.got` | 167 / 110 KB | Indirect-call machinery | Explained why the item effects appeared to have no callers |

### Relocations — how we found the object dispatch table

A shared library can be loaded at any address in memory, so every pointer stored
*inside* it has to be patched once its real base address is known. The list of
patches lives in `.rela.dyn`, and each entry says "at address X, write the address
of Y".

That has a powerful consequence for reverse engineering: **a table of function
pointers is a table of relocation entries.** In beat 59 we collected every
relocation whose target was a named `GmGmk*Init` function, and the write addresses
came out as a dense, evenly-spaced array — the dispatch table itself. Anchoring it
on two object ids already proven from placement statistics (443 = start, 520 =
goal) named **679 of 714 object ids**, covering 99.8% of every placement in all 30
acts.

### PLT and GOT — why the item effects looked uncallable

When code calls an *exported* function, the call does not usually jump straight to
it. It jumps to a small stub in the **PLT** (Procedure Linkage Table), which reads
the real address from the **GOT** (Global Offset Table). This indirection is what
lets a symbol be relocated or replaced at load time.

In beat 62 this looked like a dead end: scanning all of `.text` for a direct
branch to `GmPlayerItemRing10Set` and friends found **zero callers**, which made
no sense for functions that obviously run. Beat 63 resolved it by following the
real chain — `.dynsym` index → `.rela.plt` GOT slot → PLT stub → the code calling
*that* stub. Every one of the five item effects converged on a single dispatcher,
whose jump table gave the complete config-to-effect mapping.

**The lesson worth keeping:** in a shared library, "no direct callers" usually
means "called through the PLT", not "never called".

### Embedded shaders

`.rodata` also holds **45 complete GLSL ES shader programs** (17 vertex, 28
fragment), compiled at runtime via `glShaderSource`/`glCompileShader`. There is no
MojoShader anywhere in the binary, so SEGA hand-ported the shaders for mobile
rather than translating the Direct3D bytecode at runtime. Against the PC build's
1,843 Shader Model 3.0 shaders these are a consolidated subset, but they are an
official reference for what a correct translation should look like.

---

## The retail Android APK — a fourth oracle, and a new container format

Separate from the symbolized dev `.so`, we now hold the **retail Android build**:
`com.sega.sonic4ep2thd` version 1.4 — the Tegra HD release. Two copies exist that
are the *same build* differing only in signing (`CERT.RSA`/`CERT.SF`/`MANIFEST.MF`
and a 16-byte tail on the data pack), so only one is needed.

What it contains:

| Entry | Size | Note |
|---|---:|---|
| `assets/res.ogg` | **556 MB** | **Not audio.** The entire game data pack — see below |
| `lib/armeabi-v7a/libfox.so` | 4.2 MB | The **retail, stripped** engine |
| `lib/armeabi/libImmEmulatorJ.so` | 0.13 MB | Immersion haptics |
| `assets/dl.ini` | 18 B | Version code and expected pack byte-size |

Two things worth noting immediately. The retail engine is **4.2 MB against our dev
build's 12.5 MB** for the same architecture — that difference is almost entirely
the symbol table, which is a good measure of how unusual the dev build is. And
retail ships **32-bit ARM only**; there is no arm64 slice.

### `res.ogg` is an `LPK` archive, not an Ogg file

The `.ogg` extension is a packaging trick: Android's build tooling skips
compression for known media extensions, so naming a pre-packed archive `.ogg`
makes it get **stored** rather than deflated. The file is stored uncompressed and
its real magic is `LPK\0`.

Header, as far as it is read (**INFERRED**, not yet verified against extracted
files):

| Offset | Type | Value seen | Reading |
|---|---|---|---|
| `0x00` | `char[4]` | `LPK\0` | magic |
| `0x04` | `u16` | `1` | version |
| `0x06` | `u16` | `2281` | **file count** |
| `0x08` | `u32` | `0x00010077` | unknown |
| `0x0C` | `u32` | `0x20` | start of the offset table |
| `0x10` | `u32` | `0x23C4` | end of offset table / next section |
| `0x14` | `u32` | `0x23D0` | a further section |
| `0x20` | `u32[2281]` | ascending, `0x80`-aligned | a per-file table — **not plain byte offsets, see below** |

**The arithmetic self-validates:** `0x20 + 2281 × 4 = 0x23C4`, which is exactly the
value stored at `+0x10`. So the count and the table extent agree independently,
which is strong evidence the first three fields are read correctly.

**Correction (checked against the extracted file).** The `0x20` table was first
read here as plain byte offsets. That is **wrong**: the values ascend but the last
is `0xFFF84E80` ≈ 4.29 GB, far past the 583 MB file, so the high bits must carry
flags or the values are scaled — unresolved. The region at `0x23D0` looks more
promising as the real per-file record array: it reads as repeating 32-bit groups
in which two values are frequently *equal* (e.g. `0x3A57, 0x3A57`), the classic
shape of a (compressed size, uncompressed size) pair where equality means stored.
And the payload at the first candidate data offset (`0x80080`) is high-entropy, so
entries are compressed or encrypted rather than raw. **`LPK` remains OPEN**; the
count and header extent are the only parts confirmed.

Decoding `LPK` is the gateway to what the retail mobile build uniquely answers:
which texture format SEGA actually shipped for mobile (our open ETC2/ASTC
question), the real touch-control layout, and a second, *release-accurate* copy of
the level data to check Beta 8 against.

## The iOS build — the shader Rosetta stone

The iOS release (`com.sega.sonic4ep2` 1.0, iOS 4.0 target, May 2012) is the most
directly useful oracle for **rendering**, for one reason: where the PC build ships
`NNSTDSHADER/SHADER.AMB` containing **1,873 compiled Direct3D 9 bytecode blobs**,
iOS ships the *same* directory as **GLSL source** —
`nnstdshader.vert` (36 KB) and `nnstdshader.frag` (32 KB).

It is also the only build whose data we can read with no new work:
**400 of 400 sampled `.AMB` archives parse with our existing decoder, 0 failures**,
confirming the AMB spec is platform-universal. Models use the same NN structures
under a different platform letter — `.LNO`/`.LNM`/`.LNV` where PC uses
`.ZNO`/`.ZNM`/`.ZNV`. Textures are `.PVR` (PowerVR), audio is CRI `.ACB`/`.awb`
rather than the PC's `.CSB`/`.CPK`, and each zone carries a small 2,228-byte
`STENV/STAGE_ENV_ZONE*.GPB` — environment settings, undecoded.

### The standard shader interface (recovered, names only)

The library is one über-shader configured by ~390 `#define` symbols per stage,
which is where the PC's 1,873 compiled permutations come from — the same source
compiled under different flag combinations. Naming convention is `nngl` +
`a`=attribute, `u`=uniform, `v`=varying, `tex`=sampler.

| Role | Interface |
|---|---|
| Vertex inputs | `nnglaPosition`, `nnglaNormal`, `nnglaColor0`, `nnglaTexCoord0..3` |
| **Skinning** | `nnglaWeight`, `nnglaMtxIdx`, `nngluPositionMatrices` |
| Transforms | `nngluModelViewProjectionMatrix`, `nngluProjectionMatrix`, `nngluNormalMatrix`, `nngluTextureMatrix` |
| Material | `nngluFrontMaterialDiffuse`, `nngluFrontMaterialSpecularShininess`, `nngluFrontMaterialEmissionAlphaRef` |
| Lighting | `nngluParallelLightDirection/Diffuse/Ambient/Specular`, `nngluSceneAmbientTangent`, count via `NNGLD_OPT_NUM_PARALLEL_LIGHT` |
| Texture stages | `nngltexBase`, `nngltexDecal`, `nngltexDecal2`, `nngltexDecal3`, `nngltexModulate`, `nngltexAdd`, `nngltexOpacity`, `nngltexNormal`, `nngltexUserSampler2D1/2` |
| Stage blend levels | `nngluTexBaseDecal123Alpha`, `nngluTexShininessDualParaboloidAddLevel` |

### The texture-role roster (recovered)

Studying how the library is parameterised — not copying it — gives the full set of
texture roles the engine supports, which is **24**, well beyond the nine sampler
uniforms visible from the declarations alone. Each is a compile-time option named
`NNGLD_OPT_TEX_<ROLE>`, aliased inside the shader to a short `DT_*` symbol:

| Role group | Options |
|---|---|
| Surface | `BASE`, `DECAL`, `DECAL2`, `DECAL3` |
| Combine | `MODULATE`, `ADD`, `OPACITY` |
| Shading | `NORMAL`, `SPECULAR`, `SHININESS` |
| Reflection | `ENVMASK`, `DUALPARABOLOID` |
| Spare | `USER1`…`USER8`, `SAMPLER2D2` |

There is a matching per-role texture-coordinate selector (`tc_bn`, `tc_dc1`,
`tc_dc2`, `tc_dc3`, `tc_ad`, `tc_md`, `tc_em`, `tc_nr` …), each choosing which of
the eight interpolated coordinate sets that stage samples with. So a role is not
merely "which texture" but "which texture, sampled by which coordinate set".

**An honest limit, and it decides the next step.** Every one of these options is
`#define`d in the file to `(-1)` — the *disabled* default. The real value is
supplied by the engine when it compiles a permutation, so **the source names the
roles but does not assign their numbers.** Our material stage flags carry a low
16-bit field (2 = base on 8,808 stages, 4 = environment on 1,255, with 1 and 8
also present) which looks like a bitmask over this roster, but the mapping is
**not confirmed** and cannot be closed from this file.

That is precisely the question a frame capture answers — observe which texture
binds to which sampler for a known material — which is why `apitrace` is staged.

**This independently confirms the matrix palette (beat 58).** `nngluPositionMatrices`
indexed by `nnglaMtxIdx` and weighted by `nnglaWeight` is precisely the model we
recovered from `nnCalcMatrixPaletteNode` and implemented in `MatrixPalette.Build`
— arrived at from a completely different direction, on a different platform.

**It also states the fidelity gap exactly.** Our renderer does base texture plus
alpha/additive blend. The real material model adds per-vertex lighting from
parallel lights and scene ambient, material diffuse/specular/emission, up to seven
further texture stages, and texture matrices. That list *is* the remaining
rendering work, now enumerated rather than guessed.

Clean-room note: we read this to learn the material model and interface. The
shader source is SEGA's; nothing is copied. Our implementation is written against
these recovered facts, exactly as the rest of the project is written against
recovered symbol names and struct offsets.


### Capture plus CTAB — how the light parameters get recovered

The gameplay capture (beat 67) shows the vertex constant space is **small: 16
registers**, and that `c13` sits invariant at `(0, 1, 0, 0)` across all 1,035
draws in the slice. That is *consistent with* a fixed light direction and is
**not claimed as one** — a clean axis vector is equally an up-vector or a matrix
row, and values alone cannot tell them apart.

The PC shaders settle it. Their `CTAB` constant tables carry **names**:
`ambient`, `diffuse`, `specular`, `emission`, `shininess`, and the fog pair
`density` / `start`. So each shader states which register holds which term, in
its own metadata. Crossing that against the captured register values names every
constant the engine feeds — no inference from shape.

That is the route to retiring the light direction invented in beat 64, and it is
better than reading the iOS GLSL for the purpose, because the iOS source's role
options are all compile-time `(-1)` defaults while `CTAB` is concrete per shader.


### The PC shader constant vocabulary — recovered from CTAB

Every PC shader carries a `CTAB` constant table naming its uniforms and the
registers they occupy. Parsing all **1,843** gives the engine's full constant
vocabulary. Offsets inside the table are relative to the structure start, i.e.
just past the `CTAB` fourcc — getting that wrong yields names that all read
`CTAB`, which is the tell.

| Vertex-stage uniform | Shaders | Size |
|---|---:|---|
| `u_Fog` | 921 | float4 x5 |
| `u_FrontMaterial` | 921 | **float4 x2** |
| `u_TextureMatrix` | 830 | float4 |
| `u_ModelViewProjectionMatrix` / `u_ModelViewMatrix` / `u_NormalMatrix` | 595 / 595 / 504 | float4 x4 / x3 / x3 |
| `u_ModelViewBoneMatrix` | 326 | float4 x16 |
| `u_ModelViewBoneNormalMatrix` | 262 | float4 x12 |
| `u_LightSource` | 19 | float4 x16 |

| Pixel-stage uniform / sampler | Shaders |
|---|---:|
| `s_texBase`, `u_TexBaseAlpha` | 789 / 788 |
| `u_LightSource`, `u_FrontLightModelProduct` | **676** / 676 |
| `u_FrontMaterial` | 443 |
| `s_texSpecular` | 282 |
| `s_texDecal`, `u_TexDecalAlpha` | 260 / 205 |
| `s_texNormal` | 215 |
| `u_TexShadowColor`, `s_texShadow` | 199 / 144 |
| `s_texEnvMask` | 167 |
| `s_texModulate` | 52 |
| `s_texUserSampler2D1` / `2` | 37 / 36 |
| `s_texDualParaboloid`, `u_DualParaboloidMatrix` | 31 / 31 |

Three things this settles.

**Lighting is per-pixel.** `u_LightSource` appears in **676 pixel shaders against
19 vertex shaders**. Beat 64 set MonoGame's `PreferPerPixelLighting = false` on an
assumption; that was wrong and is corrected.

**`u_FrontMaterial` is two float4 registers** — independent confirmation of the
material colour block decoded in beat 64, arrived at from shader metadata rather
than from the model data.

**The PC has shadows the mobile builds do not.** `s_texShadow` and
`u_TexShadowColor` appear in 144 and 199 shaders, and nothing equivalent exists in
the iOS interface. Worth remembering when using iOS as the shader reference: it is
a *reduced* port, not the same feature set.

## The shading maths, disassembled out of the bytecode

The CTAB names told us which register holds what. Disassembling the instruction
stream tells us what the shaders *do* with them. **All 1,843 shaders decode
cleanly** — every instruction's operand tokens consumed exactly, none left over
and none missing — across 23 distinct pixel opcodes. That completeness is the
evidence the operand decoding (register-type split across bits 28-30 and 11-12,
write masks, swizzles) is right.

### `s_texEnvMask` is NOT the environment map

Worth stating first because the name misleads, and the whole multi-texture design
was nearly built on it. `s_texEnvMask` is a plain **UV-mapped mask** — 167 of 167
of its fetches use an interpolated coordinate. The actual environment lookup is
`s_texDualParaboloid`, whose fetches are 31 of 31 **computed**. The two are
distinguishable structurally: an environment lookup is a `texld` whose coordinate
operand is a temp register rather than an interpolant.

The 167 mask shaders partition exactly: **28** pair it with a dual-paraboloid
environment, **137** use it with no environment lookup at all (it drives a decal
lerp), and **2** pair it with a sphere map.

### The environment combine is ADDITIVE

```
envTerm.rgb = envTexel.rgb * envMask.r * u_TexDualParaboloidLevel.x
shaded.rgb  = saturate(baseTexel.rgb * lighting.rgb + envTerm.rgb)
out.rgb     = lerp(u_Fog.rgb, shaded.rgb, fogFactor)
out.a       = baseTexel.a * u_TexBaseAlpha.x * vertexColour.a
```

Unanimous across all 31, and established two independent ways. **Structurally**,
every environment chain ends in a single `mad` with the environment as the addend.
**Numerically**, measuring `d(out)/d(envTexel)` with the base texel at 0.30 and
again at 0.60 gives an identical gain — the signature of an addition, since a
multiply would have doubled it. The measured gain of exactly `0.04000` equals
`mask (0.5) × level (0.2) × fog (0.4)`, which confirms the whole scale chain at
once. **No lerp and no fresnel anywhere**: no dot product, no `pow`, no `1-x` term
feeds the environment contribution in any of the 31. Alpha is untouched in 31/31.

Two things scale it and nothing else: **`s_texEnvMask` red channel only** (all 167
read `.x`; halving the mask exactly halves the gain) and
**`u_TexDualParaboloidLevel.x`** (doubling it exactly doubles the gain, 31/31).
The mask is optional — 3 shaders drop it and use the level alone.

### The environment UV is a reflection, computed in the pixel shader

The vertex shader supplies only raw vectors: eye-space position with the fog
factor packed in `.w`, and the eye-space normal via `u_NormalMatrix`. The
projection is entirely pixel-side.

```
I   = normalize(eyePosition)
R   = I - 2*dot(I,N)*N
R'  = u_DualParaboloidMatrix * R
den = |R'.z| + 1
s   = ( R'.x/den + 1) * 0.5
t   = (-R'.y/den + 1) * 0.5
u   = (R'.z >= 0 ? -0.5 : +0.5) * s      -- this sign is the one unconfirmed step
uv  = (u, t)
```

Verified numerically on all 31, both hemisphere branches exercised in each. The
disassembly falls into **9 different instruction schedulings** with different
write masks and swizzles, and that they all collapse to one formula is itself
independent evidence the decoding is correct.

A second path exists: **12 shaders are sphere-map / matcap**, feeding reflection
coordinates to `s_texBase` itself — there is no separate diffuse map, the base
slot *is* the reflection, so it modulates lighting like any base map. The UV is
the classic OpenGL `SPHERE_MAP` with V flipped, verified 12/12:

```
m = sqrt(R.x² + R.y² + (R.z+1)²);  s = R.x/m*0.5 + 0.5;  t = -R.y/m*0.5 + 0.5
```

And the **decal path**, verified 137/137 by differential test:

```
blend = envMask.r * decalTexel.a * u_TexDecalAlpha.x
rgb   = lerp(baseTexel.rgb, decalTexel.rgb, blend)
```

### Read indices per shader, never hardcode them

The mask's UVs come from `u_TextureMatrix[k]` applied affinely, in 167/167 — but
**`k` varies per shader**: 3 (75x), 4 (59x), 2 (32x), 1 (1x), while the base map
uses element 0 (574x) or 1 (202x). The same caveat applies to sampler registers:
`s_texBase` sits at s0 in 582 shaders and s1 in 207, and `s_texNormal` is pinned
at s0 in all 215 that declare it. **The register a texture lands in is a property
of the shader permutation, not of the texture.** Any code that assumes a fixed
slot per role is wrong.

### Still unknown, stated plainly

- **Which half of the paraboloid atlas is the front hemisphere.** The `±0.5`
  selection rests on the documented D3D9 `cmp` rule, which both the interpreter
  and the reference formula assume, so that one sign is not independently
  confirmed by the numerical agreement. Everything else in the projection is.
- **The runtime value of `u_DualParaboloidMatrix`.** Its shape is known — a 3x3
  rotating view space into the environment map's frame — but the CPU-side value
  was never observed.
- **The physical layout of the dual-paraboloid texture.** A single atlas with the
  hemispheres side by side is INFERRED from the `u = ±0.5·s` packing and wrap
  addressing, not confirmed by opening one.
- **`s_texUserSampler2D1` / `2`** (37 and 36 shaders, many with computed
  coordinates) — not investigated. Possibly further projection or environment
  paths.

**It also killed a candidate, correctly.** The gameplay capture showed `c13`
invariant at `(0, 1, 0, 0)`, which looked like a fixed light direction. CTAB places
`c13` inside `u_Fog` (c10..c14). Had that been claimed rather than flagged, a fog
parameter would now be hard-coded as the engine's light direction.

## Working with it

`rizin` is the tool on this machine (no IDA or Ghidra):

```sh
rizin -q -c "is" <path>/arm64-v8a/libfox.so        # list symbols
rizin -q -e scr.color=0 -c "s 0x0060fb94; pd 184" <path>/libfox.so   # disassemble
```

`tools/dispatch.py` also contains a small, dependency-free ELF reader (sections,
symbols, relocations) written for the dispatch-table recovery — reuse it rather
than writing another one.

---

## Handling

The `.so` files and any symbol dump are **gitignored** (`/analysis/libfox-symbols.txt`,
`/analysis/*.so`). They are oracles, never sources: we read them, write our own
code, and verify the result against Episode II's own data. Nothing derived from
them is committed except recovered *facts* — names, offsets, constants — expressed
in our own words and our own code.
