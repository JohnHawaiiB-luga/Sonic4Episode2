# Development status

15 September 2026

Active work is in the C++17 implementation. The C#/MonoGame code remains an
earlier prototype and format reference. The `phase1-prototype` branch preserves
the public 29 July version; `main` contains subsequent work.

## Implemented scope

- Native readers for stage/model data, textures, motion, selected effects and
  CRI audio containers, with ADX decoding.
- Direct3D 9 stage and character inspection, skeletal pose/matrix evaluation,
  material state and a limited connected player scene.
- Ordinary Sonic ground movement, collision, crouch/roll/spindash,
  low-speed wall/ceiling detachment, jumping, airborne release and landing.
- Ring collection, selected sound effects, ring particles and jump-dash effects.
- Homing/rebound helpers and ordered target selection. Real stage targets and
  contact behavior remain guarded pending integration.
- Managed renderer, texture/material, camera and restart improvements, plus
  corresponding tests and preview tools.

These features cover selected contexts. They do not establish a complete act,
all player states, full original rendering or whole-game equivalence.

## Verification

The September 15 publication checks passed: 52 native Win32 checks, 423 managed
tests and 51 Python tests, plus native and managed desktop builds. Two existing
CA2014 warnings remain in the managed stage assembler. Android builds were not
revalidated for this update.

Native CTest checks use synthetic inputs and cover formats, transforms, motion,
materials, collision and command-line tools. Managed tests cover the prototype
engine and readers. Python tests cover audio decoding and coverage tooling.
Build and test commands are in [README.md](README.md).

Additional local comparisons execute selected original PC routines and compare
typed native outputs. The accepted homing work includes 364 selector
case/precision pairs and 130 homing/rebound pairs per architecture. These results
apply to those helpers, not to complete scene behavior. Original inputs and raw
comparison records are private and are not required by the public component tests.

Windows x86/x64 work does not establish support for other operating systems.
Interactive rendering and full-game acceptance remain separate from unit tests.

## Next work

1. Reconstruct ordinary spring initialization, runtime dependencies and
   contact/launch behavior; connect real spring targets and homing.
2. Finish a complete first act, including death/retry, checkpoints, goal and results.
3. Complete Tails, remaining player states, objects, stages, bosses, menus/HUD,
   saves, rendering, animation, effects and audio.
4. Reconcile evidenced mobile/beta content differences and verify platform
   backends and packaging. Preserve final Steam behavior as the default.

The goal is an independently buildable game and engine with full control over
their implementation. Exact original source text and comments are not a recovery
claim. Optional renderer, shader and quality-of-life improvements follow
faithful reconstruction.
