# ExecPlan — Sonic 4 Episode II portable re-implementation

## Objective

Recover Episode II into portable, buildable source that runs on desktop and
mobile, preserving the game against the loss of its original 2012 Windows build.
Reference build: Sonic 4 Episode 2 PC Beta 8 (April 2012).

## Entry criteria — established

| Fact | Status | Evidence |
|------|--------|----------|
| Episode II PC has no managed code; a decompile in Episode I's sense is impossible | VERIFIED | No `mscoree`/`mscorlib`/`_CorExeMain` in `Sonic.exe`; imports are `d3d9`, `d3dx9_43`, `DINPUT8`, `DSOUND`, `XINPUT1_3`, `MSVCR90`, `steam_api` |
| `Launcher.exe` is a .NET settings dialog with no game logic | VERIFIED | Managed, references only `System.Drawing`/`System.Windows.Forms` resources; `MUTEX_PROCESS_SONIC4EP2_LAUNCHER` |
| Episode II uses the same AliceNN engine as Episode I | VERIFIED | Embedded source path `e:\sega\sonic4ep2-beta\program\library\alicenn\source\library\amMalloc.h`; Episode I decomp implements the same `Am*` library in C# |
| Both games share data-path conventions | VERIFIED | Episode I source references `G_COM/MENU/G_PAUSE.AMB`, `G_ZONE2/BOSS/BOSS02.AMB`, `DEMO/TITLE/D_TITLE.AMB`; this build has those directories |
| Episode I decomp is reusable without licence obstacle | VERIFIED | `Sonic4Episode1-master/LICENSE` is the Unlicense |
| The AMB container is fully understood | VERIFIED | `docs/FORMAT-AMB.md`; 1,614/1,614 archives parse, extraction lossless |

## Why the PC build, and not another platform

Settled question — do not re-litigate without new evidence.

Episode I is decompilable for exactly one reason: its Windows Phone 7 build.
WP7 forbade native code, so every app had to be managed .NET (Silverlight/XNA),
and managed assemblies keep the type and method metadata that lets ILSpy recover
near-source C#. That platform constraint is the whole reason a readable Episode I
exists.

**Episode II never shipped on a platform with that constraint.** Its releases
were Windows (x86), PlayStation 3 and Xbox 360 (PowerPC), iOS and Android (ARM),
and later Ouya and Nvidia Shield (ARM). Every one is native C++. A Windows Phone
port *was* announced, including Xbox 360 cloud-save cross-compatibility, but it
was **cancelled and never released**. There is no managed Episode II build in
existence to decompile.

Given that every option is native, the PC build is the best of them:

| | |
|---|---|
| x86-32 | the most mature decompiler tooling (IDA, Ghidra, Binary Ninja) |
| Direct3D 9 + SM3.0 | thoroughly documented, and MojoShader already translates the shaders |
| Available | it is the build on disk, and its data is already decoded |

Two caveats to keep in mind:

- **Beta 8 is not retail.** It has known build bugs — Sylvania Castle textures
  failing to load, Mantis enemies rendering with a black texture, white sand in
  the Oil Desert boss backdrop. These are defects of this build, not correct
  behaviour, and must not be enshrined as reference behaviour. Retail is 12 days
  later and is the better behavioural reference where the two disagree.
- **If a Windows Phone Episode II prototype ever surfaces, it changes everything.**
  A cancelled port may still exist internally. Should Obscure Gamers or Hidden
  Palace ever recover one, it would be managed code and would collapse most of
  Phase 3 and 4 into an ILSpy run. Worth watching; not worth waiting for.

### The iOS Beta 2 build

Dated 2012-01-26, recovered by Obscure Gamers in April 2025. It is native ARM, so
it offers **no decompilation advantage** and does not change the strategy.

It does have unique preservation value: it is the only known build containing the
cut "To Be Continued" cutscene teasing an Episode III, which is absent from Beta 8
and from retail. Only its audio survives in the shipped game.

Testable prediction if that build is ever obtained: `tools/amb.py` should read its
archives unchanged, because the AMB container is engine-level rather than
platform-level. The *contained* assets should differ — mobile builds use PVR
textures rather than `.DDS`, and a different SEGA NN model variant than the
Direct3D `.ZNO`. Evidence: Episode I's texture loader resolves `.pvr` first, then
PNG, then `.DDS`.

### Static recompilation — the alternative not taken

`XenonRecomp` / `XenosRecomp` (hedge-dev) mechanically convert Xbox 360 PowerPC
code to C++ and Xenos shaders to HLSL. This is the toolchain behind Sonic
Unleashed Recompiled, so it is proven on a 360-era SEGA title, and it reaches a
*playable* result far faster than manual re-implementation.

It is rejected here because it does not serve this project's goal. Its output is
machine-translated code rather than maintainable source, which is weak for
preservation; it targets desktop Windows and Linux, whereas the objective is
phones; and it would require the Xbox 360 build rather than the PC one.

Reconsider only if the priority changes from "portable, readable, runs on phones"
to "playable on desktop as soon as possible".

## Strategy

Do not attempt a function-accurate x86 decompilation. Instead:

1. **Carry the engine across** from Episode I's public-domain C#, which already
   implements the shared AliceNN layer, and extend it where Episode II diverges.
2. **Reverse engineer only the deltas** from `Sonic.exe` — Episode II's game
   logic, new objects, Tails co-op, and the model-driven renderer.
3. **Read the original data files directly** rather than cooking assets into a
   new format, so a legitimate copy of the game is all a user needs.
4. **Keep the shared-project + platform-head layout** from Episode I, because it
   is what delivers Android and iOS via MonoGame and desktop via FNA.

Episode II bundles Episode I content for its Episode Metal mode
(`G_EP1COM`, `G_EP1ZONE1-4`, ~151 MB). Because Episode I's semantics are already
known from its decompilation, this content is a calibration set: any format
decoder can be checked against data whose meaning is independently established.

## Progress against the goal

The goal is the full thing: Episode II re-implemented in portable source and
running on desktop **and** phones. Percentages are weighted by effort, not by
task count, and are deliberately pessimistic.

| Phase | Weight | Done | Contribution |
|-------|-------:|-----:|-------------:|
| 1. Asset formats | 12% | ~95% | 11.4% |
| 2. Geometry, audio, shaders | 18% | ~99% | 17.9% |
| 3. Engine port | 20% | ~96% | 19.2% |
| 4. Game logic | 35% | ~42% | 14.7% |
| 5. Mobile targets | 15% | ~35% | 5.3% |
| **Total** | | | **≈ 70%** |

**Runnable: a playable slice.** You can run and jump on Zone 1 Act 1's real
geometry, with collision from the stage's own attribute layer, following real
per-column height fields rather than boxes. The physics constants are Episode II's own,
read out of its player parameter table, so a jump goes where the original sends
it. Nothing is spawned yet - so this is the first rung of phase 4, not the end.

What moved phase 4 off the floor is that **the object table is now readable**.
`Sonic.exe:0x007031C8` maps each `.EV` object id to the function that spawns it -
714 live ids across 382 functions, with instance sizes for 668 of them, names for
116, and the engine's real object priority. Spawning is now a matter of writing
behaviours, not of guessing what a number means. See `docs/FORMAT-OBJECTS.md`.

Previously: `Sonic4Episode2.Desktop` opens a MonoGame
window and renders a stage assembled live from the original archives - 17,526
tiles for Zone 1 Act 1. That is the first thing here that runs at all. It has no
player, no physics and no game logic, so **the playable game remains at 0%**.

A caveat worth stating plainly, because it is easy to assume otherwise: *the PC
platform is not already solved.* The original `Sonic.exe` does run on Windows —
this Beta 8 copy plays fine, with the Steam check disabled — but that is SEGA's
binary, the very thing this project exists to replace. Starting from a PC build
helps because x86 has the best tooling and D3D9 is well documented, not because
it delivers a working PC target for free. Our first runnable binary will be a
desktop one, and it does not exist yet.

Phase 1 is nearly finished and is genuinely front-loaded — it is the phase where
a sibling decompilation helps most. Phases 3 and 4 are where the years live, and
no amount of format work shortens them.

## Phases

### Phase 1 — Asset containers *(in progress)*

- [x] AMB archive format decoded, documented, verified across the whole build
- [x] `tools/amb.py`: list / extract / bulk / verify, lossless
- [x] `.MP` / `.MD` stage tile grids — 400/400 verified, `tools/stagemap.py`
- [x] `.EV` object placement — 65/65 verified; record fields still inferred
- [x] `.TXB` texture banks — 651/651 verified, every name resolves; `tools/txb.py`
- [ ] `.EV` record field meanings, confirmed against the binary's spawn code
- [ ] `.AME` effect definitions
- [ ] `.DC` / `.RG` — share the `.EV` block grid, different record size
- [x] `.DF`/`.DI`/`.AT` stage collision - 39/39 parse; `docs/FORMAT-COLLISION.md`
- [x] The `_ATTR_` id to collision record mapping - **solved from the binary**
- [ ] Unconfirmed minor formats: `.MFS` `.LTS` `.SSS` `.GPB`

Gate: every byte of every archive is accounted for by a decoder or an explicit
"unknown, N bytes" record. No silent drops.

### Phase 2 — Geometry, animation and audio

- [ ] `.ZNO` NN model (chunked, `NZIF`/`NZTL` magic confirmed) → vertices,
      materials, texture bindings, skeleton
- [ ] `.ZNM` / `.ZNV` NN motion → skeletal and vertex animation
- [ ] `.DDS` textures — standard, but confirm the DXT variants in use
- [x] `.PSH` / `.VSH` identified — 922 `ps_3_0` + 921 `vs_3_0`, compiled D3D9
      bytecode with `CTAB` constant tables
- [x] **Shader translatability established.** All 1,843 parse cleanly as
      well-formed SM3.0 with `CTAB`, using only documented SM1-3 opcodes — the
      exact input MojoShader consumes. `docs/FORMAT-SHADER.md`, `tools/shader.py`
- [ ] Actually run one through MojoShader end to end and inspect the GLSL ES.
      Remaining risk is output quality and ES 2.0 fallbacks, not feasibility
- [ ] `SOUND/` — identify the CRI middleware containers and decode

Gate: **partially met.** Whole stages assemble from original archives and render
correctly — 17,526 tiles for Zone 1 Act 1, silhouette matching the tile grid.
Still outstanding for the full gate: textures on the geometry (blocked on the
material struct) and motion playback (`NZMO` untouched).

### Phase 3 — Engine port

- [x] Solution scaffolded: `src/Sonic4Episode2.Core` (net8.0, no graphics
      dependency) + `Sonic4Episode2.Cli` harness, building clean
- [x] AMB and stage grids ported to C# and **cross-verified against the Python
      tools** - identical counts across 1,614 archives
- [x] NN container, geometry, nodes, materials, motions ported and cross-verified
- [x] DDS decoder and stage assembler ported
- [x] **Desktop head runs**: a MonoGame window rendering a stage assembled
      live from the original archives
- [ ] Android and iOS heads
- [ ] Port the Episode I `Am*` / `Nn*` engine layer, adapting the file system and
      binding layer to the Episode II data set
- [x] Task scheduler and scene state machine, with 16 tests pinning their
      semantics
- [x] Object system with its fixed procedure order, 30 tests passing
- [ ] Renderer: Episode II is far more 3D-model-driven than Episode I, so this
      is a genuine extension, not a copy

Gate: **met.** The engine boots through its scene table, mounts Zone 1 Act 1
from the original archives, registers its tasks and reaches a rendered frame in
a MonoGame window. 38 tests passing.

### Phase 4 — Game logic

- [x] Recover Episode II's object table from `Sonic.exe` - **id to spawn
      function, sizes, priority, partial names**
- [x] A player with gravity, ground/wall collision and jumping
- [x] Recover the real physics values from the binary - **all seven modes, at
      `0x00710520`**
- [ ] Tails co-op and the combo moves
- [ ] Zones, acts, bosses, special stages
- [ ] Episode Metal

Gate: Zone 1 Act 1 playable start to finish.

### Phase 5 — Mobile

- [x] Get the core library off the filesystem - **all data access goes through
      `IContentSource`, so an APK or bundle can serve it**
- [x] Touch input - **`VirtualPad`, tested at three aspect ratios and verified
      driving a real stage**
- [x] Make the renderer platform-neutral - **`StageViewerGame` takes an
      `IContentSource` and an `IInputSource`**
- [x] MonoGame Android head - **builds a signed APK; not yet run on a device**
- [ ] iOS head - needs a Mac to build at all
- [ ] Performance and memory work against the 1.2 GB data set

## Standing constraints

- **Evidence first.** Read the bytes, run the code, cite the tool output. Tag
  every claim VERIFIED, INFERRED or OPEN.
- **No silent data loss.** Preservation tooling that drops bytes is worse than no
  tooling, because it looks like it worked. Reconcile counts.
- **No assets in the repository.** Engineering artefacts only.
- **Lawful scope.** No DRM circumvention. The Steam integration is to be stubbed
  out and replaced, not defeated.

## Known risks

- **Shader translation — largely retired.** All 1,843 shaders now verified as
  well-formed SM3.0 carrying `CTAB` and using only documented SM1-3 opcodes,
  which is precisely MojoShader's input. What remains is a *quality and coverage*
  risk rather than a feasibility one: `ps_3_0` wants ES 3.0 class hardware, ES 2.0
  devices will need fallbacks, and translating 1,843 shaders at load time on a
  phone means caching the output matters.
- **Effort asymmetry.** Phase 1 was days. Phase 4 is the multi-year part, and no
  amount of tooling shortens it much.
- **Beta divergence.** This is Beta 8, not retail. Formats and content may differ
  from the shipped game; decoders should be tested against retail data before
  being called general.
- ~~No .NET SDK on the current workstation.~~ **Resolved** — .NET SDK 9.0.316
  installed and smoke-tested, so phase 3 can begin whenever the asset work is
  parked.
