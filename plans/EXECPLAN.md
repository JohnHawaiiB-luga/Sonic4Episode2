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
- [ ] Unconfirmed minor formats: `.DF` `.DI` `.MFS` `.LTS` `.AT` `.SSS` `.GPB`

Gate: every byte of every archive is accounted for by a decoder or an explicit
"unknown, N bytes" record. No silent drops.

### Phase 2 — Geometry, animation and audio

- [ ] `.ZNO` NN model (chunked, `NZIF`/`NZTL` magic confirmed) → vertices,
      materials, texture bindings, skeleton
- [ ] `.ZNM` / `.ZNV` NN motion → skeletal and vertex animation
- [ ] `.DDS` textures — standard, but confirm the DXT variants in use
- [x] `.PSH` / `.VSH` identified — 922 `ps_3_0` + 921 `vs_3_0`, compiled D3D9
      bytecode with `CTAB` constant tables
- [ ] Spike: run one through **MojoShader** and confirm usable GLSL ES output.
      This is FNA's existing shader path, so translation should be off the shelf
      rather than a re-authoring effort — but the mobile target depends on it, so
      prove it early rather than assuming it
- [ ] `SOUND/` — identify the CRI middleware containers and decode

Gate: a viewer that renders one Episode II model with its texture and plays one
motion, sourced from the original archives.

### Phase 3 — Engine port

- [ ] Stand up the shared project and platform heads (desktop first)
- [ ] Port the Episode I `Am*` / `Nn*` engine layer, adapting the file system and
      binding layer to the Episode II data set
- [ ] Task scheduler, state machine, object system
- [ ] Renderer: Episode II is far more 3D-model-driven than Episode I, so this
      is a genuine extension, not a copy

Gate: engine boots, mounts the real data, reaches a rendered frame.

### Phase 4 — Game logic

- [ ] Recover Episode II's object and stage tables from `Sonic.exe`
- [ ] Player physics, then Tails co-op and the combo moves
- [ ] Zones, acts, bosses, special stages
- [ ] Episode Metal

Gate: Zone 1 Act 1 playable start to finish.

### Phase 5 — Mobile

- [ ] MonoGame Android and iOS heads
- [ ] Touch input, since the original PC build assumes pad or keyboard
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

- **Shader translation — downgraded.** 1,843 shaders are compiled D3D9 bytecode,
  which sounded like a large re-authoring job until the version tokens were
  decoded: they are all Shader Model 3.0 with `CTAB` constant tables, the exact
  input MojoShader consumes. That turns it into an off-the-shelf translation
  step. Still the single most important thing to prove with an early spike,
  because the mobile target rests on it.
- **Effort asymmetry.** Phase 1 was days. Phase 4 is the multi-year part, and no
  amount of tooling shortens it much.
- **Beta divergence.** This is Beta 8, not retail. Formats and content may differ
  from the shipped game; decoders should be tested against retail data before
  being called general.
- **No .NET SDK on the current workstation.** Python tooling runs; C# work needs
  an SDK installed first.
