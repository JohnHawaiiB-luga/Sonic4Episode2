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
