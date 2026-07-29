import contextlib
import io
import struct
import tempfile
import unittest
from pathlib import Path

from tools import cri


_TYPE_FORMATS = {
    0x04: "I",
    0x06: "Q",
    0x0A: "I",
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
    for row in rows:
        for column, type_code in columns:
            value = row[column]
            if type_code == 0x0A:
                value = intern(value)
            row_data.extend(struct.pack(">" + _TYPE_FORMATS[type_code], value))

    rows_offset = 0x18 + len(descriptors)
    string_offset = rows_offset + len(row_data)
    data_offset = string_offset + len(strings)
    header = struct.pack(
        ">4s5IHHI",
        b"@UTF",
        data_offset,
        rows_offset,
        string_offset,
        data_offset,
        name_offset,
        len(columns),
        row_width,
        len(rows),
    )
    return bytes(header + descriptors + row_data + strings)


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


if __name__ == "__main__":
    unittest.main()
