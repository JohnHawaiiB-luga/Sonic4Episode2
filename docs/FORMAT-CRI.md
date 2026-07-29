# CRI audio containers (`.CSB` / `.CPK`)

Every sound in the game is CRI ADX2: cue sheets in `SOUND/*.CSB` and the streamed
music in a single 137 MB `SOUND/SONICDL_SNG01.CPK`.

Status: **VERIFIED**. All **8 containers parse, 0 failed**, exposing **949 cues**
across seven cue sheets.

## The @UTF table

Both formats are built from one primitive: a big-endian typed table with a
schema, a string pool and a data pool.

| Offset | Type | Field |
|--------|------|-------|
| `0x00` | `char[4]` | `@UTF` |
| `0x04` | `u32` | table size, from `0x08` onward |
| `0x08` | `u32` | rows offset |
| `0x0C` | `u32` | string pool offset |
| `0x10` | `u32` | data pool offset |
| `0x14` | `u32` | table name, into the string pool |
| `0x18` | `u16` | column count |
| `0x1A` | `u16` | row width |
| `0x1C` | `u32` | row count |

**Every offset is relative to `0x08`**, not to the start of the table.

Then one descriptor per column: a flags byte, a `u32` name offset, and for
constant columns the value inline.

### Storage classes — the trap

The flag byte's high nibble is the storage class, and the values are **not** a
dense enumeration:

| Value | Meaning |
|-------|---------|
| `0x10` | zero — no storage at all |
| `0x30` | constant — one value in the schema, shared by every row |
| `0x50` | per row |

Guessing `0x10`/`0x20`/`0x30` misaligns the name offset and yields a table whose
columns all have empty names while otherwise parsing "successfully". That is the
tell.

The low nibble is the type: `0`–`7` integers of ascending width, `8` float,
`9` double, `0xA` string offset, `0xB` a data offset/length pair.

## Cue sheets (`.CSB`)

A `TBLCSB` table whose six rows each name a sub-table held as raw bytes:

| Sub-table | Contents |
|-----------|----------|
| `INFO` | format version |
| `CUE` | the names the game triggers sounds by |
| `SYNTH` | 89 columns of per-cue mixing — volume, pitch, envelope, filters, 3D |
| `SOUND_ELEMENT` | the actual waveforms: channels, sample rate, streaming flag |
| `ISAAC` | interactive audio, empty in this build |
| `VOICE_LIMIT_GROUP` | voice limits, empty in this build |

Cue counts: `EP2_SND_FX_Z1` 152, `Z2` 138, `Z3` 130, `Z4` 134, `ZF` 129,
`EP1_SND_FX` 199, `SONICDL_SNG01` 67 — **949 total**.

Music cues are named plainly: `ep2_sng_title`, `ep2_sng_menu`,
`ep2_sng_worldmap`, `ep2_sng_z1a1`. Sound effects use interface names like
`Cursol` (sic), `Ok`, `Cancel`, `Window`.

`SOUND_ELEMENT` gives the waveform properties — the music is **48 kHz stereo with
the streaming flag set**, and each entry links to an `.aax` name such as
`Synth/Mixed00_EP2_Title_wav.aax`. The audio itself is not stored here; the
streaming flag means it lives in the `.CPK`.

## The archive (`.CPK`)

A 16-byte wrapper — magic, flags, table size — around an `@UTF` table named
`CpkHeader`, 35 columns wide: `TocOffset`, `ItocOffset`, `EtocOffset`,
`ContentOffset`, `TotalFiles`, `Align`, `Codec` and so on.

## Where the audio actually lives — two populations, one codec

Worth stating plainly because the CPK is the obvious half and the smaller one.

**The CPK is the music.** 55 files carrying 94 streams, all ADX: 53 files (92
streams) stereo 48 kHz, one mono and one stereo at 44.1 kHz. 94 streams across 55
files means multi-stream containers, most likely intro-plus-loop pairs.

**The CSB banks are the sound effects, embedded directly.** 711 ADX waveforms sit
inside the `.CSB` files themselves rather than in any archive:

| Bank | Embedded ADX |
|------|-------------:|
| `EP1_SND_FX.CSB` | 79 |
| `EP2_SND_FX_Z1.CSB` | 135 |
| `EP2_SND_FX_Z2.CSB` | 117 |
| `EP2_SND_FX_Z3.CSB` | 127 |
| `EP2_SND_FX_Z4.CSB` | 121 |
| `EP2_SND_FX_ZF.CSB` | 132 |
| **Total** | **711** |

Status **VERIFIED, 711 of 711**. Each is located by the literal `(c)CRI` in the
ADX header: the string sits at `copyright_offset - 2` from the file start, so a
hit at absolute position `p` implies a header beginning at
`p - (copyright_offset - 2)`, and every one of the 711 has the `0x8000` magic
exactly there with a sane rate and channel count. 691 are mono and 20 stereo;
rates are 11,025 (5), 16,000 (8), 22,050 (87), 32,000 (390), 44,100 (150) and
48,000 (71). `SONICDL_SNG01.CSB` embeds none, which fits — its audio is the
streamed CPK, so that file is metadata alone.

**There is no HCA anywhere in the build.** Every waveform is ADX, so one decoder
serves both populations and the harder proprietary codec never has to be touched.

## Still open

- **Decoding ADX to PCM.** The waveform codec itself is untouched. The binary
  statically links CRI's decoders, and the plan is to replace them with an
  independent implementation rather than reverse the middleware.
- **Extracting the 711 CSB-embedded effects.** Located and counted, not yet
  written out.
- **Loop points** for the streamed music. ADX carries loop metadata in its header;
  where a track loops is data and should be read rather than invented.

## Usage

```sh
python tools/cri.py verify SOUND
python tools/cri.py cues   SOUND
python tools/cri.py show   SOUND/SONICDL_SNG01.CSB
```
