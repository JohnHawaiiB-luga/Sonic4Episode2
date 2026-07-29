import contextlib
import io
import struct
import tempfile
import unittest
import wave
from pathlib import Path

from tools import cri


_TYPE_FORMATS = {
    0x04: "I",
    0x06: "Q",
    0x0A: "I",
    0x0B: "II",
}


def _build_utf(name, columns, rows):
    strings = bytearray()
    string_offsets = {}

    def intern(value):
        if value not in string_offsets:
            string_offsets[value] = len(strings)
            strings.extend(value.encode("utf-8"))
            strings.append(0)
        return string_offsets[value]

    name_offset = intern(name)
    for column, type_code in columns:
        intern(column)
        if type_code == 0x0A:
            for row in rows:
                intern(row[column])

    descriptors = bytearray()
    for column, type_code in columns:
        descriptors.append(0x50 | type_code)
        descriptors.extend(struct.pack(">I", intern(column)))

    row_width = sum(struct.calcsize(">" + _TYPE_FORMATS[t]) for _, t in columns)
    row_data = bytearray()
    data_pool = bytearray()
    for row in rows:
        for column, type_code in columns:
            value = row[column]
            if type_code == 0x0A:
                value = intern(value)
            elif type_code == 0x0B:
                payload = value
                value = (len(data_pool), len(payload))
                data_pool.extend(payload)
            values = value if isinstance(value, tuple) else (value,)
            row_data.extend(
                struct.pack(">" + _TYPE_FORMATS[type_code], *values)
            )

    rows_offset = 0x18 + len(descriptors)
    string_offset = rows_offset + len(row_data)
    data_offset = string_offset + len(strings)
    table_size = data_offset + len(data_pool)
    header = struct.pack(
        ">4s5IHHI",
        b"@UTF",
        table_size,
        rows_offset,
        string_offset,
        data_offset,
        name_offset,
        len(columns),
        row_width,
        len(rows),
    )
    return bytes(header + descriptors + row_data + strings + data_pool)


def _chunk(magic, table):
    return magic + b"\xff\0\0\0" + struct.pack("<I", len(table)) + b"\0" * 4 + table


def _cpk_fixture(files, declared_files=None, extracted_sizes=None):
    toc_offset = 0x200
    content_offset = 0x800
    file_offset = content_offset - toc_offset
    toc_rows = []
    for file_id, (directory, name, payload) in enumerate(files):
        extracted_size = (
            len(payload)
            if extracted_sizes is None
            else extracted_sizes[file_id]
        )
        toc_rows.append(
            {
                "DirName": directory,
                "FileName": name,
                "FileSize": len(payload),
                "ExtractSize": extracted_size,
                "FileOffset": file_offset,
                "ID": file_id,
            }
        )
        file_offset += len(payload)

    toc = _build_utf(
        "CpkTocInfo",
        [
            ("DirName", 0x0A),
            ("FileName", 0x0A),
            ("FileSize", 0x04),
            ("ExtractSize", 0x04),
            ("FileOffset", 0x06),
            ("ID", 0x04),
        ],
        toc_rows,
    )
    header = _build_utf(
        "CpkHeader",
        [
            ("ContentOffset", 0x06),
            ("TocOffset", 0x06),
            ("Files", 0x04),
        ],
        [
            {
                "ContentOffset": content_offset,
                "TocOffset": toc_offset,
                "Files": len(files) if declared_files is None else declared_files,
            }
        ],
    )

    archive = bytearray(file_offset + toc_offset)
    archive[: len(_chunk(b"CPK ", header))] = _chunk(b"CPK ", header)
    archive[toc_offset: toc_offset + len(_chunk(b"TOC ", toc))] = _chunk(
        b"TOC ", toc
    )
    at = content_offset
    for _, _, payload in files:
        archive[at: at + len(payload)] = payload
        at += len(payload)
    return bytes(archive)


def _adx_fixture(channels, sample_rate, sample_count):
    header = bytearray(struct.pack(
        ">HHBBBBIIHBB",
        0x8000,
        36,
        3,
        18,
        4,
        channels,
        sample_rate,
        sample_count,
        500,
        4,
        0,
    ))
    header.extend(b"\0" * 14)
    header.extend(b"(c)CRI")
    return bytes(header)


def _adx_block(scale, nibbles):
    values = [*nibbles, *([0] * (32 - len(nibbles)))]
    block = bytearray(struct.pack(">H", scale))
    for index in range(0, 32, 2):
        block.append((values[index] & 0x0F) << 4 | (values[index + 1] & 0x0F))
    return bytes(block)


def _aax_fixture(streams):
    return _build_utf(
        "AAX",
        [
            ("data", 0x0B),
            ("lpflg", 0x04),
        ],
        [
            {
                "data": payload,
                "lpflg": loop_flag,
            }
            for payload, loop_flag in streams
        ],
    )


class CriExtractionTests(unittest.TestCase):
    def test_extract_command_writes_each_toc_file(self):
        archive = _cpk_fixture(
            [
                ("Synth", "one.aax", b"first"),
                ("Music", "two.adx", b"second"),
            ]
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "fixture.cpk"
            output = root / "out"
            source.write_bytes(archive)
            stdout = io.StringIO()
            stderr = io.StringIO()

            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    result = cri.main(["extract", str(source), str(output)])
                except SystemExit as exc:
                    result = exc.code

            self.assertEqual(0, result, stderr.getvalue())
            self.assertEqual(b"first", (output / "Synth" / "one.aax").read_bytes())
            self.assertEqual(b"second", (output / "Music" / "two.adx").read_bytes())
            self.assertEqual(
                ["Music/two.adx", "Synth/one.aax"],
                sorted(
                    path.relative_to(output).as_posix()
                    for path in output.rglob("*")
                    if path.is_file()
                ),
            )
            self.assertIn("2 files extracted", stdout.getvalue())

    def test_declared_file_count_must_match_toc_rows(self):
        archive = _cpk_fixture(
            [("Synth", "only.aax", b"payload")],
            declared_files=2,
        )

        with self.assertRaisesRegex(
                cri.CriError, "declares 2 files but TOC has 1 row"):
            cri.cpk_entries(archive)

    def test_extract_rejects_parent_directory_paths(self):
        archive = _cpk_fixture(
            [("..", "escape.aax", b"payload")]
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "out"

            with self.assertRaisesRegex(cri.CriError, "unsafe CPK output path"):
                cri.extract_cpk(archive, output)

            self.assertFalse((root / "escape.aax").exists())

    def test_extract_rejects_compressed_entries_without_a_decoder(self):
        archive = _cpk_fixture(
            [("Synth", "packed.aax", b"packed")],
            extracted_sizes=[12],
        )

        with self.assertRaisesRegex(
                cri.CriError, "is compressed \\(6 stored, 12 extracted\\)"):
            cri.cpk_entries(archive)

    def test_toc_entry_must_fit_inside_archive(self):
        archive = _cpk_fixture(
            [("Synth", "truncated.aax", b"payload")]
        )[:-1]

        with self.assertRaisesRegex(cri.CriError, "outside .* bytes"):
            cri.cpk_entries(archive)

    def test_duplicate_output_paths_are_rejected(self):
        archive = _cpk_fixture(
            [
                ("Synth", "same.aax", b"first"),
                ("Synth", "same.aax", b"second"),
            ]
        )

        with self.assertRaisesRegex(
                cri.CriError, "duplicate CPK output path 'Synth/same.aax'"):
            cri.cpk_entries(archive)


class CriIdentificationTests(unittest.TestCase):
    def test_identify_rejects_unknown_audio_magic(self):
        payload = bytearray(_adx_fixture(2, 48000, 2000))
        payload[:2] = b"\x48\x43"
        aax = _aax_fixture([(bytes(payload), 0)])

        with self.assertRaisesRegex(cri.CriError, "unsupported audio magic"):
            cri.identify_aax(aax)

    def test_identify_command_reports_adx_census(self):
        archive = _cpk_fixture(
            [
                (
                    "Synth",
                    "mono.aax",
                    _aax_fixture([
                        (_adx_fixture(1, 44100, 1000), 0),
                    ]),
                ),
                (
                    "Synth",
                    "stereo.aax",
                    _aax_fixture([
                        (_adx_fixture(2, 48000, 2000), 0),
                        (_adx_fixture(2, 48000, 3000), 1),
                    ]),
                ),
            ]
        )

        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "fixture.cpk"
            source.write_bytes(archive)
            stdout = io.StringIO()
            stderr = io.StringIO()

            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    result = cri.main(["identify", str(source)])
                except SystemExit as exc:
                    result = exc.code

        self.assertEqual(0, result, stderr.getvalue())
        lines = stdout.getvalue().splitlines()
        self.assertIn(
            "Synth/mono.aax: ADX, 44100 Hz, 1 channel, 1 stream",
            lines,
        )
        self.assertIn(
            "Synth/stereo.aax: ADX, 48000 Hz, 2 channels, 2 streams",
            lines,
        )
        self.assertIn("2 files, 3 streams", lines)
        self.assertIn("ADX: 2 files, 3 streams", lines)
        self.assertIn(
            "44100 Hz, 1 channel: 1 file, 1 stream",
            lines,
        )
        self.assertIn(
            "48000 Hz, 2 channels: 1 file, 2 streams",
            lines,
        )


class CriDecodingTests(unittest.TestCase):
    def test_decode_command_writes_declared_wave_frames(self):
        source_data = (
            _adx_fixture(1, 16000, 3)
            + _adx_block(1000, [1, 0, -1])
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.adx"
            output = root / "decoded.wav"
            source.write_bytes(source_data)
            stdout = io.StringIO()
            stderr = io.StringIO()

            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    result = cri.main(["decode", str(source), str(output)])
                except SystemExit as exc:
                    result = exc.code

            self.assertEqual(0, result, stderr.getvalue())
            with wave.open(str(output), "rb") as decoded:
                self.assertEqual(1, decoded.getnchannels())
                self.assertEqual(2, decoded.getsampwidth())
                self.assertEqual(16000, decoded.getframerate())
                self.assertEqual(3, decoded.getnframes())
                self.assertEqual(
                    struct.pack("<3h", 1000, 1476, 634),
                    decoded.readframes(3),
                )
            self.assertIn("3 samples decoded", stdout.getvalue())

    def test_decode_adx_recovers_declared_mono_samples(self):
        source = (
            _adx_fixture(1, 44100, 10)
            + _adx_block(1000, [1, -1, 2, -2, 3, -3, 4, -4, 7, -8])
        )

        decoded = cri.decode_adx(source)

        self.assertEqual(1, decoded.channels)
        self.assertEqual(44100, decoded.sample_rate)
        self.assertEqual(10, decoded.sample_count)
        self.assertEqual(
            (1000, 790, 2613, 2045, 4567, 3538, 6674, 5114, 10807, 7251),
            struct.unpack("<10h", decoded.pcm_s16le),
        )

    def test_decode_adx_interleaves_stereo_blocks(self):
        source = (
            _adx_fixture(2, 44100, 3)
            + _adx_block(1000, [1, 2, 3])
            + _adx_block(500, [-1, -2, -3])
        )

        decoded = cri.decode_adx(source)

        self.assertEqual(2, decoded.channels)
        self.assertEqual(3, decoded.sample_count)
        self.assertEqual(
            (1000, -500, 3790, -1896, 8984, -4495),
            struct.unpack("<6h", decoded.pcm_s16le),
        )

    def test_decode_adx_clips_to_signed_16_bit(self):
        source = (
            _adx_fixture(1, 44100, 2)
            + _adx_block(32767, [7, -8])
        )

        decoded = cri.decode_adx(source)

        self.assertEqual(
            (32767, -32768),
            struct.unpack("<2h", decoded.pcm_s16le),
        )

    def test_decode_adx_rejects_truncated_sample_data(self):
        source = (
            _adx_fixture(1, 44100, 33)
            + _adx_block(1000, [1])
        )

        with self.assertRaisesRegex(cri.CriError, "sample data is truncated"):
            cri.decode_adx(source)

    def test_decode_adx_rejects_early_end_marker(self):
        source = (
            _adx_fixture(1, 44100, 1)
            + _adx_block(0x8001, [0])
        )

        with self.assertRaisesRegex(cri.CriError, "ends before declared sample"):
            cri.decode_adx(source)

    def test_decode_adx_preserves_silent_blocks(self):
        source = (
            _adx_fixture(1, 44100, 32)
            + _adx_block(1000, [])
        )

        decoded = cri.decode_adx(source)

        self.assertEqual(b"\0" * 64, decoded.pcm_s16le)


if __name__ == "__main__":
    unittest.main()
