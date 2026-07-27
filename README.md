# Sonic 4: Episode II

Pulling Sonic the Hedgehog 4: Episode II apart and rebuilding it as portable
source, so it outlives its 2012 Windows build and runs on things that fit in your
pocket.

# Status

Early, but the foundations are real. Every number below is verified against the
*entire* Beta 8 data set, not one lucky file.

 - AMB archives — **1614/1614** parse, extraction is lossless
 - Stage tile grids — **400/400** grids resolve exactly
 - Object placement — **65/65** `.EV` files
 - Texture banks — **651/651**, every texture name resolving to a real DDS
 - Tile ids to 3D models — **13/13** act maps index cleanly into their tileset
 - Nothing playable yet. No engine, no game code, don't get excited.

Tools are Python with zero dependencies. `stagemap.py` renders layers to PNG,
which is the fastest way to find out whether a decode is real or whether you have
been staring at noise for an hour.

# Why this isn't a decompilation

Because it can't be, and it's worth being upfront about that.

Episode I is decompilable for exactly one reason: its Windows Phone 7 build. WP7
banned native code, so everything shipped as managed .NET, and managed assemblies
keep the metadata that lets ILSpy hand you back near-original C#.

Episode II never shipped on a platform with that restriction. Windows, PS3, 360,
iOS, Android, Ouya, Shield — all native C++. A Windows Phone port was announced
and then quietly cancelled, which is the single most annoying fact in this repo.
`Sonic.exe` is native x86 out of Visual C++ 2008 with no PDB, no RTTI on game
classes, and `/LTCG` smearing 663 translation units together. There is no button
that turns that back into source.

So this is a **re-implementation**, guided by reverse engineering. Different
method, same destination.

# The trick that makes it possible

Both episodes run SEGA's AliceNN engine. Episode II's own binary admits it — an
assert left the path `e:\sega\sonic4ep2-beta\program\library\alicenn\...` sitting
in `.rdata`. And Episode I's decompilation contains that same engine already
recovered into readable C#.

Better still, the data conventions are identical. Episode I's source references
`G_COM/MENU/G_PAUSE.AMB` and `G_ZONE2/BOSS/BOSS02.AMB` — directories this build
ships verbatim.

So Episode I gets used as a **behavioural oracle**: read it to learn what a
format or subsystem *means*, then write our own implementation and verify it
against Episode II's actual bytes. Every format here was confirmed that way
before being written down. Where the two disagree, Episode II's data wins.

# Credit where it's absolutely due

This project stands entirely on work other people did first, and it would not
exist otherwise.

 - **WamWooWam**, for [Sonic 4 Episode 1 Deluxe](https://github.com/WanKerr/Sonic4Episode1)
   — the clean, well-split Episode I decompilation that serves as the oracle for
   basically everything here. Released into the public domain, which is a
   generosity worth calling out.
 - **TGEnigma**, for the [original Episode I decompile](https://github.com/TGEnigma/Sonic4Ep1-WindowsPhone-Decompilation)
   that the above was built in response to.
 - **Hidden Palace** and **Obscure Gamers**, for preserving the prototypes.

Reading someone else's decompilation of the sibling game is a wildly unfair
advantage and I'm not going to pretend otherwise.

# What's actually in here

| | |
|---|---|
| `tools/amb.py` | AMB archive reader — list, extract, bulk unpack, verify |
| `tools/stagemap.py` | stage grids, object placement, tileset resolution, PNG previews |
| `tools/txb.py` | texture banks |
| `docs/` | format specifications, all marked VERIFIED / INFERRED / OPEN |
| `plans/EXECPLAN.md` | the roadmap and why the PC build was chosen |

```sh
python tools/amb.py verify .
python tools/stagemap.py render  G_ZONE1/MAP/ZONE11_MAP.AMB out/ --scale 2
python tools/stagemap.py tileset G_ZONE1/MAP/ZONE11_MAP.AMB
python tools/txb.py    list      G_ZONE1/MAP/ZONE1_T.AMB
```

# Issues

## The stages are 3D and I was hoping they wouldn't be

Tile ids don't point at sprites, they index a per-zone archive of ZNO models —
297 of them for Zone 1 alone. So a "stage viewer" means parsing SEGA NN geometry,
not blitting tiles. Verified on all 13 act maps, and the histogram is exactly
what you'd want: `Z1_G_FL_A.ZNO` (floor) placed 12,552 times, `Z1_G_HASIRA_B.ZNO`
(柱, pillar) 2,458, walls after that.

## The renderer won't come for free

Episode I's graphics layer is a fixed-function OpenGL ES 1.x shim. Episode II is
shader-driven: 3,577 models and 1,843 Shader Model 3.0 shaders. The oracle runs
out right about here. The shaders themselves should translate through MojoShader,
which is what FNA already does — but that needs proving, not assuming.

## Object ids are still anonymous

Roughly 298 object names live in the binary as immediates pushed inside each
object's own code rather than in a lookup table, so mapping id 724 to a name
needs disassembly rather than a clever grep.

# No assets here, ever

Tools and documentation only. Nothing in this repository will run without your
own legally acquired copy of the game, and the data stays where it is.
