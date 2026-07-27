# Sonic 4: Episode II — preservation and portable re-implementation

A long-running effort to recover Sonic the Hedgehog 4 Episode II into portable,
buildable source, in the spirit of the Episode I decompilation, so the game can
be preserved and run on current hardware — phones included.

## What this is, and what it is not

The Episode I project is a genuine *decompilation*. Its source was the Windows
Phone 7 build, which Microsoft required to be managed .NET/XNA; managed
assemblies carry full type and method metadata, so tools like ILSpy recover
near-original C# automatically.

**Episode II has no managed build.** `Sonic.exe` in this Beta 8 tree is native
x86, compiled with Visual Studio 2008, linked against Direct3D 9, D3DX9_43,
DirectInput8, DirectSound, XInput 1.3 and the Steam API, with no CLR references
of any kind. `Launcher.exe` is a .NET WinForms settings dialog and contains no
game logic. There is no push-button path from this binary to buildable source,
and any claim otherwise is false.

So this is a **guided re-implementation**, not a decompilation. What makes it
tractable rather than hopeless is that Episode II runs on the same engine as
Episode I.

## The Rosetta stone

Episode II's own binary embeds the source path
`e:\sega\sonic4ep2-beta\program\library\alicenn\source\library\amMalloc.h`.
"AliceNN" is SEGA's in-house framework wrapping the NN graphics library, and the
Episode I decompilation contains that same framework already recovered into
readable C# — 31 files under `AppMain/Am/` (`AmFs`, `AmBind`, `AmModel`,
`AmMotion`, `AmTexture`, `AmSprite`, `AmTask`, `AmCri`, ...) plus `AppMain/Nn/`.

The two games also share their data conventions exactly. Episode I's source
refers to `G_COM/MENU/G_PAUSE.AMB`, `G_ZONE2/BOSS/BOSS02.AMB`,
`DEMO/TITLE/D_TITLE.AMB` — all of which are directory-for-directory how this
Episode II build is laid out on disk.

That means a large fraction of the engine does not have to be puzzled out of x86
at all. Episode I tells us what each subsystem *does*; the binary tells us where
Episode II differs. The reverse engineering effort then concentrates on what is
genuinely new: Tails co-op and the combo moves, the new zones and bosses, and a
renderer far more 3D-model-driven than Episode I's.

**Episode I is used as a behavioural oracle, not as a source to copy.** Its
decompilation is public domain by its author's licence, but it is still machine-
derived from SEGA's code. So the practice here is to read it to understand a
format or a system, then write our own implementation, and independently verify
every decoded format against Episode II's own data before documenting it as
fact. Both formats decoded so far were confirmed that way.

## Status

Early. One phase is complete and verified.

- **Done — AMB archive format.** Fully decoded and documented in
  [`docs/FORMAT-AMB.md`](docs/FORMAT-AMB.md). `tools/amb.py` reads, lists,
  verifies and unpacks it, including nested archives. Verified against **all
  1,614 archives in this build with zero parse failures**, and extraction is
  lossless (validated by reconciling written-file counts against files on disk).
- **Done — stage layout.** `.MP` and `.MD` decoded and documented in
  [`docs/FORMAT-STAGEMAP.md`](docs/FORMAT-STAGEMAP.md), verified across all 400
  grids in the build. `tools/stagemap.py` reads them and renders each layer to a
  PNG; the Zone 1 Act 1 render is recognisable platformer terrain.
- **Done — binary survey.** [`docs/ENGINE.md`](docs/ENGINE.md): ~8,000 functions
  of which ~6,600 are game code, no PDB, no game-code RTTI, and all 1,843 shaders
  are Shader Model 3.0 bytecode — which matters, because that is a format
  MojoShader already translates for mobile.
- **Next** — the remaining formats inside the archives: NN models and motions
  (`.ZNO`/`.ZNM`/`.ZNV`), texture banks (`.TXB`), effects (`.AME`), and the event
  scripts (`.EV`) that place objects in a stage.
- **Not started** — the engine and the game logic.

See [`plans/EXECPLAN.md`](plans/EXECPLAN.md) for the phased plan and
[`docs/RESUME-HERE.md`](docs/RESUME-HERE.md) for current state.

## Tools

`tools/amb.py` needs only Python 3.10+ and no third-party packages. Run it from
the game root:

```sh
python Sonic4Episode2/tools/amb.py list   G_COM/MENU/G_PAUSE.AMB
python Sonic4Episode2/tools/amb.py verify .
python Sonic4Episode2/tools/amb.py bulk   . ../unpacked
```

A full unpack of the 1.2 GB data set produces roughly the same volume again, so
budget disk accordingly.

## Target platforms

Episode I builds as an MSBuild *shared project* consumed by thin per-platform
head projects (FNA, XNA, UWP, WebAssembly, WP7). Episode II will follow the same
shape, since it is what makes "runs on phones" achievable: the shared engine and
game code stay platform-neutral, and MonoGame covers Android and iOS while FNA
covers desktop.

## Scope

This repository contains engineering work only — tools, documentation and source.
It contains no game assets. Running anything built here requires your own copy of
the game, and the data files stay where they are.
