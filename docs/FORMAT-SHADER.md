# Shaders (`.PSH` / `.VSH`)

`NNSTDSHADER/SHADER.AMB` holds every shader the game uses: **922 pixel and 921
vertex**, all compiled Direct3D 9 bytecode.

This file exists to answer one question with evidence, because the entire mobile
plan depends on it: **can these be translated, or would they have to be
re-authored by hand?**

Answer: they can be translated.

## The verification

Status: **VERIFIED**. All **1,843 shaders parse cleanly with zero failures**,
walking the token stream from version token to end token.

| | |
|---|---|
| Shader models | 922 `ps_3_0`, 921 `vs_3_0` — nothing else |
| Constant tables | **1,843 of 1,843** carry a `CTAB` |
| Instructions | 98,672 |
| Distinct opcodes | 26, every one from the documented SM1–3 set |

No unknown opcodes, no malformed instruction lengths, no shader that runs off the
end of its buffer. `rep`/`endrep` appear 373 times each — exactly balanced, which
is a free self-check on the instruction-length walk, since an off-by-one in token
stepping would desynchronise the pairing.

Most-used opcodes are entirely ordinary: `mad` 17,648, `dcl` 17,249, `mul`
14,219, `dp3` 11,709, `dp4` 7,875, `mov` 6,728, `add` 6,056, `tex` 3,138.

## Why this matters

Compiled bytecode initially sounded like the worst case for portability. It is
close to the best case:

- **Shader Model 3.0 with a `CTAB` is exactly MojoShader's input.** MojoShader is
  already how FNA runs XNA shaders on OpenGL, so this is an off-the-shelf path
  rather than a bespoke one.
- **Only documented opcodes appear.** Nothing vendor-specific, nothing undocumented
  that a translator would choke on.
- **The constant table is present everywhere**, so uniform names and register
  bindings survive translation instead of having to be reconstructed.

## What this does *not* prove

Being precise, since the mobile target leans on this:

- It proves the bytecode is **well-formed and uses standard instructions**. It does
  not prove that MojoShader's GLSL ES output is correct or fast for these
  particular shaders.
- `ps_3_0` requires OpenGL ES 3.0 class hardware for a faithful translation.
  Anything relying on ES 2.0 will need fallbacks.
- 1,843 shaders is a lot to compile at load time on a phone. Caching translated
  output will matter.

The remaining risk is a *performance and coverage* problem, not a "this cannot be
done" problem. That is a large downgrade from where the plan started.

## Format

All tokens are little-endian 32-bit words.

| Token | Meaning |
|-------|---------|
| version | `0xFFFE_MMmm` vertex, `0xFFFF_MMmm` pixel |
| instruction | opcode in bits 0–15, length in bits 24–27 |
| comment | opcode `0xFFFE`, token count in bits 16–30 — `CTAB` lives in one |
| end | `0x0000FFFF` |

Instruction length counts the parameter tokens that follow, so stepping is
`offset += 4 + length * 4`.

## Usage

```sh
python tools/shader.py verify NNSTDSHADER
python tools/shader.py show   NNSTDSHADER <name-fragment>
```
