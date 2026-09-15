import contextlib
import hashlib
import io
import json
import tempfile
import unittest
from unittest import mock
from pathlib import Path

from tools import runtime_coverage


def _sha256(payload):
    return hashlib.sha256(payload).hexdigest()


class RuntimeCoverageTests(unittest.TestCase):
    def setUp(self):
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary_directory.name)
        self.reference_sha256 = _sha256(b"retail-reference-build")
        self.reference_source_sha256 = _sha256(b"retail-frame-source")
        self.native_source_sha256 = _sha256(b"native-frame-source")
        self.backend_source_sha256 = _sha256(b"backend-frame-source")
        self._producers = {
            "reference": self._producer_policy(
                "retail-reference",
                "reference",
                b"retail-reference-build-receipt",
                b"retail-frame-source",
            ),
            "native": self._producer_policy(
                "native-host",
                "native",
                b"native-host-build-receipt",
                b"native-frame-source",
            ),
            "backend": self._producer_policy(
                "backend-replay",
                "backend",
                b"backend-replay-build-receipt",
                b"backend-frame-source",
            ),
        }
        self._render_index = 0
        self._producer_receipt_index = 0
        self._active_catalog = None

    def tearDown(self):
        self._temporary_directory.cleanup()

    def _write_bytes(self, name, payload):
        path = self.root / name
        path.write_bytes(payload)
        return path

    def _producer_policy(self, producer_id, role, build_payload, source_payload):
        build_path = self._write_bytes("{}-build.bin".format(producer_id), build_payload)
        source_path = self._write_bytes(
            "{}-source.json".format(producer_id), source_payload
        )
        return {
            "id": producer_id,
            "role": role,
            "build_path": build_path.name,
            "build_sha256": _sha256(build_payload),
            "source_manifest_path": source_path.name,
            "source_manifest_sha256": _sha256(source_payload),
        }

    @property
    def _trusted_producers(self):
        return list(self._producers.values())

    def _write_json(self, name, value):
        path = self.root / name
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return path

    def _write_jsonl(self, name, values):
        path = self.root / name
        path.write_text(
            "".join(json.dumps(value, sort_keys=True) + "\n" for value in values),
            encoding="utf-8",
        )
        return path

    def _run(self, args):
        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            try:
                code = runtime_coverage.main(args)
            except SystemExit as error:
                code = error.code
        return code, stdout.getvalue(), stderr.getvalue()

    def _target(
        self, target_id, category, required_evidence=None, sequence_expectation=None
    ):
        target = {
            "id": target_id,
            "category": category,
            "label": target_id.replace(".", " ").title(),
        }
        if required_evidence is not None:
            target["required_evidence"] = required_evidence
        if sequence_expectation is not None:
            target["sequence_expectation"] = sequence_expectation
        return target

    def _catalog(
        self,
        targets,
        name="catalog.json",
        trusted_producers=None,
        full_unified_edition_scope_complete=True,
        open_dimensions=None,
    ):
        catalog = self._write_json(
            name,
            {
                "schema_version": 1,
                "reference_sha256": self.reference_sha256,
                "targets": targets,
                "full_unified_edition_scope_complete": full_unified_edition_scope_complete,
                "open_dimensions": [] if open_dimensions is None else open_dimensions,
                **(
                    {"trusted_producers": trusted_producers}
                    if trusted_producers is not None
                    else {}
                ),
            },
        )
        self._active_catalog = catalog
        return catalog

    def _active_catalog_sha256(self):
        if self._active_catalog is None:
            raise AssertionError("a catalog must be created before its manifest")
        return _sha256(self._active_catalog.read_bytes())

    def _observation(self, session_id, sequence, kind, target_ids, evidence, reference=None):
        return {
            "schema_version": 1,
            "reference_sha256": reference or self.reference_sha256,
            "session_id": session_id,
            "sequence": sequence,
            "kind": kind,
            "target_ids": target_ids,
            "evidence": evidence,
        }

    def _manifest(
        self,
        name,
        session_id,
        observations,
        delivery=None,
        sequence_receipts=None,
        observations_sha256=None,
        capture_complete=None,
        capture_issues=None,
        producer_receipts=None,
        catalog_sha256=None,
    ):
        observations_path = self._write_jsonl(name + ".jsonl", observations)
        line_count = len(observations)
        if delivery is None:
            delivery = {
                "produced_count": line_count,
                "delivered_count": line_count,
                "dropped_count": 0,
            }
        return self._write_json(
            name + ".manifest.json",
            {
                "schema_version": 1,
                "reference_sha256": self.reference_sha256,
                "catalog_sha256": catalog_sha256 or self._active_catalog_sha256(),
                "session_id": session_id,
                "observations_path": observations_path.name,
                "observations_sha256": observations_sha256
                or _sha256(observations_path.read_bytes()),
                "delivery": delivery,
                **(
                    {"capture_complete": capture_complete}
                    if capture_complete is not None
                    else {}
                ),
                **(
                    {"sequence_receipts": sequence_receipts}
                    if sequence_receipts is not None
                    else {}
                ),
                **(
                    {"capture_issues": capture_issues}
                    if capture_issues is not None
                    else {}
                ),
                **(
                    {"producer_receipts": producer_receipts}
                    if producer_receipts is not None
                    else {}
                ),
            },
        )

    def _write_producer_receipt(
        self, policy, artifact_path, target_ids, frame_id, state_id
    ):
        index = self._producer_receipt_index
        self._producer_receipt_index += 1
        return self._write_json(
            "producer-receipt-{}.json".format(index),
            {
                "schema_version": 1,
                "kind": "render_producer_receipt",
                "producer_id": policy["id"],
                "role": policy["role"],
                "build_path": policy["build_path"],
                "build_sha256": policy["build_sha256"],
                "source_manifest_path": policy["source_manifest_path"],
                "source_manifest_sha256": policy["source_manifest_sha256"],
                "artifact_path": artifact_path.name,
                "artifact_sha256": _sha256(artifact_path.read_bytes()),
                "target_ids": target_ids,
                "frame_id": frame_id,
                "state_id": state_id,
            },
        )

    def _producer_bindings(self, *comparisons):
        paths = []
        for comparison in comparisons:
            paths.extend(comparison.get("producer_receipt_paths", []))
        return [
            {"path": path.name, "sha256": _sha256(path.read_bytes())}
            for path in paths
        ]

    def _sequence_trace(self, target_id, frames, name="reference-trace.json"):
        path = self._write_json(
            name,
            {
                "schema_version": 1,
                "kind": "sequence_reference_trace",
                "reference_sha256": self.reference_sha256,
                "target_id": target_id,
                "frames": frames,
            },
        )
        return {
            "reference_trace_path": path.name,
            "reference_trace_sha256": _sha256(path.read_bytes()),
        }

    def _sequence_receipt(self, name, session_id, target_id, trace, frames):
        expected_frames = []
        for sequence, comparison in frames:
            expected_frames.append(
                {
                    "sequence": sequence,
                    "frame_id": comparison["frame_id"],
                    "state_id": comparison["state_id"],
                    "comparison_receipt_path": comparison["receipt_path"].name,
                    "comparison_receipt_sha256": _sha256(
                        comparison["receipt_path"].read_bytes()
                    ),
                }
            )
        return self._write_json(
            name,
            {
                "schema_version": 1,
                "kind": "sequence_receipt",
                "reference_sha256": self.reference_sha256,
                "reference_trace_sha256": trace["reference_trace_sha256"],
                "session_id": session_id,
                "target_id": target_id,
                "expected_frames": expected_frames,
            },
        )

    def _compare(
        self,
        target_ids,
        scope="native",
        reference_payload=b"\x00\x00\x00\xff",
        candidate_payload=b"\x00\x00\x00\xff",
        width=1,
        height=1,
        frame_id=None,
        state_id=None,
        with_provenance=False,
        same_artifact=False,
    ):
        index = self._render_index
        self._render_index += 1
        frame_id = frame_id or "frame-{}".format(index)
        state_id = state_id or "state-{}".format(index)
        reference_path = self._write_bytes("reference-{}.rgba".format(index), reference_payload)
        candidate_path = (
            reference_path
            if same_artifact
            else self._write_bytes("render-{}.rgba".format(index), candidate_payload)
        )
        receipt_path = self.root / "comparison-{}.json".format(index)
        reference_producer_receipt = None
        candidate_producer_receipt = None
        candidate_policy = self._producers[scope]
        if with_provenance:
            reference_producer_receipt = self._write_producer_receipt(
                self._producers["reference"],
                reference_path,
                target_ids,
                frame_id,
                state_id,
            )
            candidate_producer_receipt = self._write_producer_receipt(
                candidate_policy,
                candidate_path,
                target_ids,
                frame_id,
                state_id,
            )
        arguments = [
            "compare-rgba",
            "--reference",
            str(reference_path),
            "--candidate",
            str(candidate_path),
            "--width",
            str(width),
            "--height",
            str(height),
            "--scope",
            scope,
            "--reference-sha256",
            self.reference_sha256,
            "--reference-source-sha256",
            self._producers["reference"]["source_manifest_sha256"],
            "--candidate-source-sha256",
            candidate_policy["source_manifest_sha256"],
            "--frame-id",
            frame_id,
            "--state-id",
            state_id,
            "--output",
            str(receipt_path),
        ]
        for target_id in target_ids:
            arguments.extend(("--target-id", target_id))
        if with_provenance:
            arguments.extend(
                (
                    "--reference-producer-receipt",
                    str(reference_producer_receipt),
                    "--candidate-producer-receipt",
                    str(candidate_producer_receipt),
                )
            )
        code, _, stderr = self._run(arguments)
        self.assertEqual(0, code, stderr)
        return {
            "receipt_path": receipt_path,
            "receipt": json.loads(receipt_path.read_text(encoding="utf-8")),
            "candidate_path": candidate_path,
            "frame_id": frame_id,
            "state_id": state_id,
            "producer_receipt_paths": [
                path
                for path in (reference_producer_receipt, candidate_producer_receipt)
                if path is not None
            ],
        }

    def _render_evidence(self, comparison):
        candidate_path = comparison["candidate_path"]
        receipt_path = comparison["receipt_path"]
        return {
            "render_path": candidate_path.name,
            "render_sha256": _sha256(candidate_path.read_bytes()),
            "width": comparison["receipt"]["width"],
            "height": comparison["receipt"]["height"],
            "format": "rgba8",
            "frame_id": comparison["frame_id"],
            "state_id": comparison["state_id"],
            "comparison_receipt_path": receipt_path.name,
            "comparison_receipt_sha256": _sha256(receipt_path.read_bytes()),
        }

    @staticmethod
    def _target_report(report, target_id):
        return next(target for target in report["targets"] if target["id"] == target_id)

    def _report(self, catalog, manifests, require_complete=False):
        output_path = self.root / "report.json"
        arguments = ["report", "--catalog", str(catalog), "--output", str(output_path)]
        for manifest in manifests:
            arguments.extend(("--manifest", str(manifest)))
        if require_complete:
            arguments.append("--require-complete")
        code, _, stderr = self._run(arguments)
        self.assertTrue(output_path.exists(), stderr)
        return code, json.loads(output_path.read_text(encoding="utf-8")), stderr

    def test_compare_records_exact_differences_without_tolerance(self):
        comparison = self._compare(
            ["stage.demo"],
            scope="backend",
            reference_payload=b"\x00\x10\x20\xff\x01\x02\x03\x04",
            candidate_payload=b"\x00\x10\x20\xff\x01\x05\x03\x04",
            width=2,
            height=1,
        )

        receipt = comparison["receipt"]
        self.assertFalse(receipt["raw_bytes_equal"])
        self.assertEqual(1, receipt["differing_pixels"])
        self.assertEqual(3, receipt["max_channel_error"])
        self.assertEqual("backend", receipt["scope"])
        self.assertEqual(["stage.demo"], receipt["target_ids"])
        self.assertEqual(self.reference_sha256, receipt["reference_sha256"])
        self.assertEqual(self.reference_source_sha256, receipt["reference_source_sha256"])
        self.assertEqual(self.backend_source_sha256, receipt["candidate_source_sha256"])
        self.assertEqual(
            _sha256(b"\x00\x10\x20\xff\x01\x02\x03\x04"),
            receipt["reference_payload_sha256"],
        )
        self.assertEqual(
            _sha256(b"\x00\x10\x20\xff\x01\x05\x03\x04"),
            receipt["candidate_payload_sha256"],
        )
        self.assertNotIn("passed", receipt)

    def test_compare_rejects_empty_undersized_and_trailing_rgba_payloads(self):
        reference_path = self._write_bytes("reference.rgba", b"\x00\x00\x00\xff")
        for label, candidate_payload in (
            ("empty", b""),
            ("undersized", b"\x00\x00\x00"),
            ("oversized", b"\x00\x00\x00\xff\x00"),
        ):
            with self.subTest(label=label):
                candidate_path = self._write_bytes(label + ".rgba", candidate_payload)
                output_path = self.root / (label + ".json")
                code, _, stderr = self._run(
                    [
                        "compare-rgba",
                        "--reference",
                        str(reference_path),
                        "--candidate",
                        str(candidate_path),
                        "--width",
                        "1",
                        "--height",
                        "1",
                        "--scope",
                        "native",
                        "--reference-sha256",
                        self.reference_sha256,
                        "--reference-source-sha256",
                        self.reference_source_sha256,
                        "--candidate-source-sha256",
                        self.native_source_sha256,
                        "--target-id",
                        "menu.demo",
                        "--frame-id",
                        "frame",
                        "--state-id",
                        "state",
                        "--output",
                        str(output_path),
                    ]
                )
                self.assertNotEqual(0, code)
                self.assertIn("expected 4 RGBA8 bytes", stderr)
                self.assertFalse(output_path.exists())

    def test_inventory_lists_only_catalogued_targets_and_categories(self):
        catalog = self._catalog(
            [
                self._target("stage.demo", "stage", ["resource_seen"]),
                self._target("menu.demo", "menu", ["state_observed"]),
                self._target("effect.demo", "effect", ["render_captured"]),
                self._target("transition.demo", "transition", ["sequence_match"]),
            ]
        )
        output_path = self.root / "inventory.json"
        code, _, stderr = self._run(
            ["inventory", "--catalog", str(catalog), "--output", str(output_path)]
        )

        self.assertEqual(0, code, stderr)
        inventory = json.loads(output_path.read_text(encoding="utf-8"))
        self.assertEqual(4, inventory["target_count"])
        self.assertEqual(
            {"effect": 1, "menu": 1, "stage": 1, "transition": 1},
            inventory["category_target_counts"],
        )
        self.assertNotIn("full_game_completion_percentage", inventory)
        self.assertEqual(
            ["effect.demo", "menu.demo", "stage.demo", "transition.demo"],
            sorted(target["id"] for target in inventory["targets"]),
        )

    def test_inventory_and_report_emit_json_when_output_is_omitted(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        code, stdout, stderr = self._run(["inventory", "--catalog", str(catalog)])
        self.assertEqual(0, code, stderr)
        self.assertEqual(1, json.loads(stdout)["target_count"])

        session_id = "stdout-report"
        manifest = self._manifest(
            "stdout-report",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "resource"},
                )
            ],
            capture_complete=True,
        )
        code, stdout, stderr = self._run(
            [
                "report",
                "--catalog",
                str(catalog),
                "--manifest",
                str(manifest),
                "--require-complete",
            ]
        )
        self.assertEqual(0, code, stderr)
        self.assertEqual("stage.demo", json.loads(stdout)["targets"][0]["id"])

    def test_require_complete_rejects_open_or_contradictory_catalog_scope(self):
        for name, scope_complete, open_dimensions in (
            ("open", False, ["uncovered edition variants"]),
            ("contradictory", True, ["uncovered edition variants"]),
        ):
            with self.subTest(name=name):
                catalog = self._catalog(
                    [self._target("stage.demo", "stage", ["resource_seen"])],
                    name="{}-scope-catalog.json".format(name),
                    full_unified_edition_scope_complete=scope_complete,
                    open_dimensions=open_dimensions,
                )
                session_id = "{}-scope-session".format(name)
                manifest = self._manifest(
                    "{}-scope-session".format(name),
                    session_id,
                    [
                        self._observation(
                            session_id,
                            0,
                            "resource_seen",
                            ["stage.demo"],
                            {"resource_id": "complete-target-gate"},
                        )
                    ],
                    capture_complete=True,
                )

                code, report, _ = self._report(
                    catalog, [manifest], require_complete=True
                )

                self.assertEqual(1, code)
                self.assertEqual(
                    "satisfied",
                    self._target_report(report, "stage.demo")["gates"]
                    ["resource_seen"]["status"],
                )
                self.assertFalse(report["catalog_scope"]["complete"])
                self.assertFalse(report["require_complete"]["catalog_scope_complete"])
                self.assertFalse(report["require_complete"]["satisfied"])

    def test_report_credits_backend_and_native_matches_separately(self):
        catalog = self._catalog(
            [
                self._target(
                    "stage.demo",
                    "stage",
                    [
                        "resource_seen",
                        "state_observed",
                        "render_captured",
                        "backend_pixels_match",
                    ],
                ),
                self._target("menu.demo", "menu", ["native_pixels_match"]),
            ],
            trusted_producers=self._trusted_producers,
        )
        backend = self._compare(
            ["stage.demo"], scope="backend", with_provenance=True
        )
        native = self._compare(
            ["menu.demo"], scope="native", with_provenance=True
        )
        session_id = "session-complete"
        manifest = self._manifest(
            "complete",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "resource-stage"},
                ),
                self._observation(
                    session_id,
                    1,
                    "state_observed",
                    ["stage.demo"],
                    {"state_id": "stage-ready"},
                ),
                self._observation(
                    session_id,
                    2,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(backend),
                ),
                self._observation(
                    session_id,
                    3,
                    "render_captured",
                    ["menu.demo"],
                    self._render_evidence(native),
                ),
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(backend, native),
        )

        code, report, stderr = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(0, code, stderr)
        stage = self._target_report(report, "stage.demo")
        menu = self._target_report(report, "menu.demo")
        self.assertEqual("satisfied", stage["gates"]["resource_seen"]["status"])
        self.assertEqual("satisfied", stage["gates"]["state_observed"]["status"])
        self.assertEqual("satisfied", stage["gates"]["render_captured"]["status"])
        self.assertEqual("satisfied", stage["gates"]["backend_pixels_match"]["status"])
        self.assertEqual("missing", stage["gates"]["native_pixels_match"]["status"])
        self.assertEqual("satisfied", menu["gates"]["native_pixels_match"]["status"])
        self.assertEqual("missing", menu["gates"]["backend_pixels_match"]["status"])
        self.assertEqual("missing", menu["gates"]["state_observed"]["status"])
        self.assertEqual(
            100.0,
            report["category_gate_percentages"]["stage"]["gates"]
            ["backend_pixels_match"]["percentage"],
        )
        self.assertNotIn("full_game_completion_percentage", report)

    def test_capture_completeness_is_separate_from_normalized_delivery(self):
        catalog = self._catalog(
            [
                self._target(
                    "stage.demo",
                    "stage",
                    ["render_captured", "native_pixels_match"],
                )
            ]
        )
        incomplete_capture = self._compare(["stage.demo"], scope="native")
        session_id = "capture-not-confirmed"
        manifest = self._manifest(
            "capture-not-confirmed",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(incomplete_capture),
                )
            ],
            capture_complete=False,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertTrue(report["sessions"][0]["normalized_delivery_complete"])
        self.assertFalse(report["sessions"][0]["capture_complete"])
        self.assertFalse(report["sessions"][0]["fixture_complete"])
        stage = self._target_report(report, "stage.demo")
        self.assertEqual("blocked", stage["gates"]["render_captured"]["status"])
        self.assertEqual("blocked", stage["gates"]["native_pixels_match"]["status"])

        default_capture = self._compare(["stage.demo"], scope="native")
        default_session_id = "capture-defaulted"
        default_manifest = self._manifest(
            "capture-defaulted",
            default_session_id,
            [
                self._observation(
                    default_session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(default_capture),
                )
            ],
        )

        code, report, _ = self._report(
            catalog, [default_manifest], require_complete=True
        )
        self.assertEqual(1, code)
        self.assertFalse(report["sessions"][0]["capture_complete"])
        self.assertFalse(report["sessions"][0]["fixture_complete"])

        issue_capture = self._compare(["stage.demo"], scope="native")
        issue_session_id = "capture-issues"
        issue_manifest = self._manifest(
            "capture-issues",
            issue_session_id,
            [
                self._observation(
                    issue_session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(issue_capture),
                )
            ],
            capture_complete=True,
            capture_issues=["draw-state teardown was not observed"],
        )

        code, report, _ = self._report(catalog, [issue_manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertTrue(report["sessions"][0]["normalized_delivery_complete"])
        self.assertTrue(report["sessions"][0]["capture_complete"])
        self.assertFalse(report["sessions"][0]["fixture_complete"])
        self.assertEqual(
            ["draw-state teardown was not observed"],
            report["sessions"][0]["capture_issues"],
        )

    def test_raw_comparison_without_producer_contract_stays_open(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["native_pixels_match"])]
        )
        comparison = self._compare(["stage.demo"], scope="native")
        session_id = "raw-only-comparison"
        manifest = self._manifest(
            "raw-only-comparison",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(comparison),
                )
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "open",
            self._target_report(report, "stage.demo")["gates"]
            ["native_pixels_match"]["status"],
        )

    def test_sequence_receipt_without_catalog_trace_stays_open(self):
        catalog = self._catalog(
            [self._target("transition.demo", "transition", ["sequence_match"])]
        )
        first = self._compare(
            ["transition.demo"], scope="native", frame_id="fade-0", state_id="fade"
        )
        second = self._compare(
            ["transition.demo"], scope="native", frame_id="fade-1", state_id="fade"
        )
        session_id = "self-authored-sequence"
        sequence_path = self._write_json(
            "self-authored-sequence.json",
            {
                "schema_version": 1,
                "kind": "sequence_receipt",
                "reference_sha256": self.reference_sha256,
                "session_id": session_id,
                "target_id": "transition.demo",
                "expected_frames": [
                    {
                        "sequence": 0,
                        "frame_id": first["frame_id"],
                        "state_id": first["state_id"],
                        "comparison_receipt_path": first["receipt_path"].name,
                        "comparison_receipt_sha256": _sha256(
                            first["receipt_path"].read_bytes()
                        ),
                    },
                    {
                        "sequence": 1,
                        "frame_id": second["frame_id"],
                        "state_id": second["state_id"],
                        "comparison_receipt_path": second["receipt_path"].name,
                        "comparison_receipt_sha256": _sha256(
                            second["receipt_path"].read_bytes()
                        ),
                    },
                ],
            },
        )
        manifest = self._manifest(
            "self-authored-sequence",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(first),
                ),
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(second),
                ),
            ],
            sequence_receipts=[
                {"path": sequence_path.name, "sha256": _sha256(sequence_path.read_bytes())}
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "open",
            self._target_report(report, "transition.demo")["gates"]
            ["sequence_match"]["status"],
        )

    def test_equal_and_unequal_comparisons_make_match_failure_sticky(self):
        for order in ("equal-first", "unequal-first"):
            with self.subTest(order=order):
                catalog = self._catalog(
                    [self._target("stage.demo", "stage", ["native_pixels_match"])],
                    trusted_producers=self._trusted_producers,
                )
                equal = self._compare(
                    ["stage.demo"], scope="native", with_provenance=True
                )
                unequal = self._compare(
                    ["stage.demo"],
                    scope="native",
                    reference_payload=b"\x00\x00\x00\xff",
                    candidate_payload=b"\x00\x00\x00\xfe",
                    with_provenance=True,
                )
                comparisons = (
                    (equal, unequal) if order == "equal-first" else (unequal, equal)
                )
                session_id = "sticky-" + order
                manifest = self._manifest(
                    "sticky-" + order,
                    session_id,
                    [
                        self._observation(
                            session_id,
                            sequence,
                            "render_captured",
                            ["stage.demo"],
                            self._render_evidence(comparison),
                        )
                        for sequence, comparison in enumerate(comparisons)
                    ],
                    capture_complete=True,
                    producer_receipts=self._producer_bindings(equal, unequal),
                )

                code, report, _ = self._report(
                    catalog, [manifest], require_complete=True
                )
                self.assertEqual(1, code)
                self.assertEqual(
                    "failed",
                    self._target_report(report, "stage.demo")["gates"]
                    ["native_pixels_match"]["status"],
                )

    def test_same_artifact_cannot_credit_native_match(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["native_pixels_match"])],
            trusted_producers=self._trusted_producers,
        )
        comparison = self._compare(
            ["stage.demo"],
            scope="native",
            with_provenance=True,
            same_artifact=True,
        )
        session_id = "same-artifact"
        manifest = self._manifest(
            "same-artifact",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(comparison),
                )
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(comparison),
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "blocked",
            self._target_report(report, "stage.demo")["gates"]
            ["native_pixels_match"]["status"],
        )
        self.assertTrue(report["rejected_provenance"])

    def test_copied_build_and_source_identity_cannot_credit_native_match(self):
        duplicate_native = self._producer_policy(
            "native-duplicate",
            "native",
            b"retail-reference-build-receipt",
            b"retail-frame-source",
        )
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["native_pixels_match"])],
            trusted_producers=[self._producers["reference"], duplicate_native],
        )
        comparison = self._compare(
            ["stage.demo"], scope="native", with_provenance=True
        )
        candidate_producer_path = comparison["producer_receipt_paths"][1]
        candidate_producer = json.loads(
            candidate_producer_path.read_text(encoding="utf-8")
        )
        candidate_producer.update(
            {
                "producer_id": duplicate_native["id"],
                "role": duplicate_native["role"],
                "build_path": duplicate_native["build_path"],
                "build_sha256": duplicate_native["build_sha256"],
                "source_manifest_path": duplicate_native["source_manifest_path"],
                "source_manifest_sha256": duplicate_native["source_manifest_sha256"],
            }
        )
        self._write_json(candidate_producer_path.name, candidate_producer)
        comparison_receipt = json.loads(
            comparison["receipt_path"].read_text(encoding="utf-8")
        )
        comparison_receipt["candidate_source_sha256"] = duplicate_native[
            "source_manifest_sha256"
        ]
        comparison_receipt["candidate_producer_receipt_sha256"] = _sha256(
            candidate_producer_path.read_bytes()
        )
        self._write_json(comparison["receipt_path"].name, comparison_receipt)
        comparison["receipt"] = comparison_receipt
        session_id = "duplicate-producer-identity"
        manifest = self._manifest(
            "duplicate-producer-identity",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(comparison),
                )
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(comparison),
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "blocked",
            self._target_report(report, "stage.demo")["gates"]
            ["native_pixels_match"]["status"],
        )
        self.assertTrue(report["rejected_producers"])

    def test_shared_build_with_different_source_cannot_credit_native_match(self):
        duplicate_native = self._producer_policy(
            "native-shared-build",
            "native",
            b"retail-reference-build-receipt",
            b"different-native-source-manifest",
        )
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["native_pixels_match"])],
            trusted_producers=[self._producers["reference"], duplicate_native],
        )
        comparison = self._compare(
            ["stage.demo"], scope="native", with_provenance=True
        )
        candidate_producer_path = comparison["producer_receipt_paths"][1]
        candidate_producer = json.loads(
            candidate_producer_path.read_text(encoding="utf-8")
        )
        candidate_producer.update(
            {
                "producer_id": duplicate_native["id"],
                "role": duplicate_native["role"],
                "build_path": duplicate_native["build_path"],
                "build_sha256": duplicate_native["build_sha256"],
                "source_manifest_path": duplicate_native["source_manifest_path"],
                "source_manifest_sha256": duplicate_native["source_manifest_sha256"],
            }
        )
        self._write_json(candidate_producer_path.name, candidate_producer)
        comparison_receipt = json.loads(
            comparison["receipt_path"].read_text(encoding="utf-8")
        )
        comparison_receipt["candidate_source_sha256"] = duplicate_native[
            "source_manifest_sha256"
        ]
        comparison_receipt["candidate_producer_receipt_sha256"] = _sha256(
            candidate_producer_path.read_bytes()
        )
        self._write_json(comparison["receipt_path"].name, comparison_receipt)
        comparison["receipt"] = comparison_receipt
        session_id = "shared-build-different-source"
        manifest = self._manifest(
            "shared-build-different-source",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(comparison),
                )
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(comparison),
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "blocked",
            self._target_report(report, "stage.demo")["gates"]
            ["native_pixels_match"]["status"],
        )
        self.assertTrue(report["rejected_producers"])

    def test_missing_catalog_producer_source_cannot_credit_native_match(self):
        missing_native = {
            **self._producers["native"],
            "source_manifest_path": "missing-native-source.json",
        }
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["native_pixels_match"])],
            trusted_producers=[self._producers["reference"], missing_native],
        )
        comparison = self._compare(
            ["stage.demo"], scope="native", with_provenance=True
        )
        session_id = "missing-producer-source"
        manifest = self._manifest(
            "missing-producer-source",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(comparison),
                )
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(comparison),
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "blocked",
            self._target_report(report, "stage.demo")["gates"]
            ["native_pixels_match"]["status"],
        )
        self.assertTrue(report["rejected_producers"])

    def test_render_verifier_does_not_retain_rgba_payload(self):
        render_path = self._write_bytes("transient.rgba", b"\x00\x00\x00\xff")
        record = {
            "observations_path": self.root / "observations.jsonl",
            "observation": self._observation(
                "transient-render",
                0,
                "render_captured",
                ["stage.demo"],
                {
                    "render_path": render_path.name,
                    "render_sha256": _sha256(render_path.read_bytes()),
                    "width": 1,
                    "height": 1,
                    "format": "rgba8",
                    "frame_id": "frame",
                    "state_id": "state",
                },
            ),
        }

        render = runtime_coverage._verify_render_evidence(record)

        self.assertNotIn("payload", render)
        self.assertEqual(_sha256(render_path.read_bytes()), render["payload_sha256"])

    def test_metadata_reads_have_a_bound_and_reject_growth(self):
        metadata_path = self._write_bytes("oversized-metadata.json", b"12345")

        with self.subTest("metadata byte limit"):
            with mock.patch.object(
                runtime_coverage, "MAX_JSON_METADATA_BYTES", 4, create=True
            ):
                with self.assertRaisesRegex(runtime_coverage.CoverageError, "exceeds"):
                    runtime_coverage._read_file(metadata_path, "metadata")

        class GrowingMetadata:
            def __init__(self):
                self.stat_calls = 0

            def __str__(self):
                return "growing-metadata"

            def stat(self):
                self.stat_calls += 1
                size = 4 if self.stat_calls == 1 else 5
                return type("Stat", (), {"st_size": size})()

            def open(self, mode):
                return io.BytesIO(b"data")

            def read_bytes(self):
                return b"data"

        with self.subTest("metadata growth after precheck"):
            with self.assertRaisesRegex(
                runtime_coverage.CoverageError, "changed while reading"
            ):
                runtime_coverage._read_file(GrowingMetadata(), "metadata")

    def test_artifact_hashes_have_a_bound_and_reject_growth(self):
        artifact_path = self._write_bytes("oversized-artifact.bin", b"12345")

        with self.subTest("artifact byte limit"):
            with mock.patch.object(
                runtime_coverage, "MAX_ARTIFACT_BYTES", 4, create=True
            ):
                with self.assertRaisesRegex(runtime_coverage.CoverageError, "exceeds"):
                    runtime_coverage._hash_file(artifact_path, "artifact")

        class GrowingArtifact:
            def __init__(self):
                self.stat_calls = 0

            def __str__(self):
                return "growing-artifact"

            def stat(self):
                self.stat_calls += 1
                size = 4 if self.stat_calls == 1 else 5
                return type("Stat", (), {"st_size": size})()

            def open(self, mode):
                return io.BytesIO(b"data")

        with self.subTest("artifact growth after precheck"):
            with self.assertRaisesRegex(
                runtime_coverage.CoverageError, "changed while hashing"
            ):
                runtime_coverage._hash_file(GrowingArtifact(), "artifact")

    def test_jsonl_byte_limit_invalidates_identity_and_rejects_growth(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "jsonl-byte-limit"
        manifest = self._manifest(
            "jsonl-byte-limit",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "stage-resource"},
                )
            ],
            capture_complete=True,
        )

        with self.subTest("JSONL byte limit"):
            with mock.patch.object(runtime_coverage, "MAX_JSONL_BYTES", 1, create=True):
                code, report, _ = self._report(catalog, [manifest], require_complete=True)

            self.assertEqual(1, code)
            self.assertFalse(report["sessions"][0]["identity_verified"])
            self.assertEqual(
                "missing",
                self._target_report(report, "stage.demo")["gates"]
                ["resource_seen"]["status"],
            )

        class GrowingJsonl:
            def __init__(self):
                self.stat_calls = 0

            def __str__(self):
                return "growing-observations.jsonl"

            def stat(self):
                self.stat_calls += 1
                size = 3 if self.stat_calls == 1 else 4
                return type("Stat", (), {"st_size": size})()

            def open(self, mode):
                return io.BytesIO(b"{}\n")

        with self.subTest("JSONL growth after precheck"):
            with self.assertRaisesRegex(
                runtime_coverage.CoverageError, "changed while streaming"
            ):
                runtime_coverage._stream_jsonl(GrowingJsonl(), lambda *_: None)

    def test_jsonl_record_limit_preserves_prefix_and_rejects_session(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "record-limit"
        observations = [
            self._observation(
                session_id,
                sequence,
                "resource_seen",
                ["stage.demo"],
                {"resource_id": "resource-{}".format(sequence)},
            )
            for sequence in range(3)
        ]
        manifest = self._manifest(
            "record-limit", session_id, observations, capture_complete=True
        )

        with mock.patch.object(runtime_coverage, "MAX_JSONL_RECORDS", 1, create=True):
            code, report, _ = self._report(catalog, [manifest], require_complete=True)

        self.assertEqual(1, code)
        self.assertEqual(
            "satisfied",
            self._target_report(report, "stage.demo")["gates"]["resource_seen"]["status"],
        )
        self.assertEqual(3, report["sessions"][0]["observation_line_count"])
        self.assertEqual(1, report["sessions"][0]["credited_record_count"])
        self.assertFalse(report["sessions"][0]["normalized_delivery_complete"])

    def test_jsonl_malformed_issues_stop_at_record_limit(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "malformed-record-limit"
        observations_path = self.root / "malformed-limit.jsonl"
        observations_path.write_text("{\n{\n{\n{\n", encoding="utf-8")
        manifest = self._write_json(
            "malformed-limit.manifest.json",
            {
                "schema_version": 1,
                "reference_sha256": self.reference_sha256,
                "catalog_sha256": self._active_catalog_sha256(),
                "session_id": session_id,
                "observations_path": observations_path.name,
                "observations_sha256": _sha256(observations_path.read_bytes()),
                "delivery": {"produced_count": 4, "delivered_count": 4, "dropped_count": 0},
                "capture_complete": True,
            },
        )

        with mock.patch.object(runtime_coverage, "MAX_JSONL_RECORDS", 3), mock.patch.object(
            runtime_coverage, "MAX_REPORTED_OBSERVATION_ISSUES", 1
        ):
            code, report, _ = self._report(catalog, [manifest], require_complete=True)

        self.assertEqual(1, code)
        self.assertEqual(1, len(report["rejected_observations"]))
        self.assertEqual(2, report["observation_issue_overflow"]["rejected_observations"])
        self.assertEqual(4, report["sessions"][0]["observation_line_count"])
        self.assertFalse(report["sessions"][0]["normalized_delivery_complete"])

    def test_transport_sequence_requires_physical_order(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["render_captured"])]
        )
        first = self._compare(["stage.demo"], frame_id="first", state_id="state")
        second = self._compare(["stage.demo"], frame_id="second", state_id="state")
        session_id = "out-of-order-transport"
        manifest = self._manifest(
            "out-of-order-transport",
            session_id,
            [
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(first),
                ),
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(second),
                ),
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertFalse(report["sessions"][0]["normalized_delivery_complete"])
        self.assertEqual(
            "blocked",
            self._target_report(report, "stage.demo")["gates"]["render_captured"]["status"],
        )

    def test_sequence_receipt_rejects_extra_native_frame(self):
        first = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-0",
            state_id="fade",
            with_provenance=True,
        )
        second = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-1",
            state_id="fade",
            with_provenance=True,
        )
        extra = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-extra",
            state_id="fade",
            with_provenance=True,
        )
        trace = self._sequence_trace(
            "transition.demo",
            [
                {"sequence": 0, "frame_id": "fade-0", "state_id": "fade"},
                {"sequence": 1, "frame_id": "fade-1", "state_id": "fade"},
            ],
            "extra-frame-trace.json",
        )
        catalog = self._catalog(
            [
                self._target(
                    "transition.demo",
                    "transition",
                    ["sequence_match"],
                    sequence_expectation=trace,
                )
            ],
            trusted_producers=self._trusted_producers,
        )
        session_id = "extra-native-frame"
        sequence_receipt = self._sequence_receipt(
            "extra-frame-sequence.json",
            session_id,
            "transition.demo",
            trace,
            [(0, first), (1, second)],
        )
        manifest = self._manifest(
            "extra-native-frame",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(first),
                ),
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(second),
                ),
                self._observation(
                    session_id,
                    2,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(extra),
                ),
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(first, second, extra),
            sequence_receipts=[
                {"path": sequence_receipt.name, "sha256": _sha256(sequence_receipt.read_bytes())}
            ],
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "blocked",
            self._target_report(report, "transition.demo")["gates"]
            ["sequence_match"]["status"],
        )
        self.assertTrue(report["rejected_sequence_receipts"])

    def test_backend_replay_cannot_credit_native_and_undefined_gates_fail_complete(self):
        catalog = self._catalog(
            [
                self._target("menu.demo", "menu", ["native_pixels_match"]),
                self._target("effect.undefined", "effect"),
            ]
        )
        backend = self._compare(["menu.demo"], scope="backend")
        session_id = "backend-only"
        manifest = self._manifest(
            "backend-only",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["menu.demo"],
                    self._render_evidence(backend),
                )
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        menu = self._target_report(report, "menu.demo")
        self.assertEqual("open", menu["gates"]["backend_pixels_match"]["status"])
        self.assertEqual("missing", menu["gates"]["native_pixels_match"]["status"])
        self.assertFalse(self._target_report(report, "effect.undefined")["acceptance_defined"])
        self.assertEqual(
            ["effect.undefined"], report["require_complete"]["undefined_targets"]
        )

    def test_report_rechecks_raw_render_and_comparison_receipt_not_passed_flag(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["render_captured", "backend_pixels_match"])]
        )
        comparison = self._compare(["stage.demo"], scope="backend")
        candidate_path = comparison["candidate_path"]
        candidate_path.write_bytes(b"\x00\x00\x00\xfe")
        receipt = comparison["receipt"]
        receipt["passed"] = True
        comparison["receipt_path"].write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        session_id = "forged-pass"
        manifest = self._manifest(
            "forged-pass",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(comparison),
                )
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest])
        self.assertEqual(1, code)
        stage = self._target_report(report, "stage.demo")
        self.assertEqual("satisfied", stage["gates"]["render_captured"]["status"])
        self.assertEqual("blocked", stage["gates"]["backend_pixels_match"]["status"])
        self.assertTrue(report["rejected_comparisons"])

    def test_render_captured_requires_exact_payload_length_and_bound_hash(self):
        catalog = self._catalog(
            [self._target("effect.demo", "effect", ["render_captured"])]
        )
        oversized = self._write_bytes("oversized-render.rgba", b"\x00\x00\x00\xff\x00")
        wrong_hash = self._write_bytes("wrong-hash-render.rgba", b"\x00\x00\x00\xff")
        session_id = "invalid-render-binding"
        manifest = self._manifest(
            "invalid-render-binding",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["effect.demo"],
                    {
                        "render_path": oversized.name,
                        "render_sha256": _sha256(oversized.read_bytes()),
                        "width": 1,
                        "height": 1,
                        "format": "rgba8",
                        "frame_id": "oversized",
                        "state_id": "effect",
                    },
                ),
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["effect.demo"],
                    {
                        "render_path": wrong_hash.name,
                        "render_sha256": _sha256(b"wrong payload hash"),
                        "width": 1,
                        "height": 1,
                        "format": "rgba8",
                        "frame_id": "wrong-hash",
                        "state_id": "effect",
                    },
                ),
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest])
        self.assertEqual(1, code)
        effect = self._target_report(report, "effect.demo")
        self.assertEqual("blocked", effect["gates"]["render_captured"]["status"])
        self.assertEqual(2, len(report["rejected_observations"]))

    def test_unknown_and_wrong_reference_observations_are_explicit_and_not_denominators(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "unmapped"
        manifest = self._manifest(
            "unmapped",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["unknown.target"],
                    {"resource_id": "unknown-resource"},
                ),
                self._observation(
                    session_id,
                    1,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "wrong-reference"},
                    reference=_sha256(b"other-reference"),
                ),
            ],
        )

        code, report, _ = self._report(catalog, [manifest])
        self.assertEqual(1, code)
        self.assertEqual(1, report["category_gate_percentages"]["stage"]["target_count"])
        self.assertEqual(1, len(report["unmapped_observations"]))
        self.assertEqual(1, len(report["rejected_observations"]))
        self.assertEqual(
            "missing",
            self._target_report(report, "stage.demo")["gates"]["resource_seen"]["status"],
        )

    def test_unmapped_data_cannot_leave_required_coverage_marked_satisfied(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "unmapped-with-valid-target"
        manifest = self._manifest(
            "unmapped-with-valid-target",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "known-resource"},
                ),
                self._observation(
                    session_id,
                    1,
                    "resource_seen",
                    ["unknown.target"],
                    {"resource_id": "unknown-resource"},
                ),
            ],
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "satisfied",
            self._target_report(report, "stage.demo")["gates"]["resource_seen"]["status"],
        )
        self.assertTrue(report["require_complete"]["evidence_errors_present"])
        self.assertFalse(report["require_complete"]["satisfied"])

    def test_manifest_hash_mismatch_is_explicit_and_blocks_high_validation(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "manifest-hash-mismatch"
        manifest = self._manifest(
            "manifest-hash-mismatch",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "valid-resource"},
                )
            ],
            observations_sha256=_sha256(b"wrong observation file"),
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertFalse(report["sessions"][0]["normalized_delivery_complete"])
        self.assertEqual(
            "blocked",
            self._target_report(report, "stage.demo")["gates"]["resource_seen"]["status"],
        )
        self.assertTrue(report["rejected_sessions"])
        self.assertFalse(report["require_complete"]["satisfied"])

    def test_identity_failures_cannot_credit_resource_or_state(self):
        for failure in ("manifest-reference", "observation-digest", "stream-hash"):
            with self.subTest(failure=failure):
                catalog = self._catalog(
                    [
                        self._target("stage.demo", "stage", ["resource_seen"]),
                        self._target("menu.demo", "menu", ["state_observed"]),
                    ]
                )
                session_id = "identity-{}".format(failure)
                manifest = self._manifest(
                    "identity-{}".format(failure),
                    session_id,
                    [
                        self._observation(
                            session_id,
                            0,
                            "resource_seen",
                            ["stage.demo"],
                            {"resource_id": "stage-resource"},
                        ),
                        self._observation(
                            session_id,
                            1,
                            "state_observed",
                            ["menu.demo"],
                            {"state_id": "menu-state"},
                        ),
                    ],
                    observations_sha256=(
                        _sha256(b"wrong observation digest")
                        if failure == "observation-digest"
                        else None
                    ),
                    capture_complete=True,
                )
                if failure == "manifest-reference":
                    document = json.loads(manifest.read_text(encoding="utf-8"))
                    document["reference_sha256"] = _sha256(b"wrong manifest reference")
                    self._write_json(manifest.name, document)

                if failure == "stream-hash":
                    def fail_after_records(path, handle_line):
                        for line_number, raw_line in enumerate(
                            Path(path).read_bytes().splitlines(), start=1
                        ):
                            handle_line(line_number, raw_line, False)
                        raise runtime_coverage.CoverageError("simulated stream hash failure")

                    with mock.patch.object(runtime_coverage, "_stream_jsonl", fail_after_records):
                        code, report, _ = self._report(
                            catalog, [manifest], require_complete=True
                        )
                else:
                    code, report, _ = self._report(
                        catalog, [manifest], require_complete=True
                    )

                self.assertEqual(1, code)
                self.assertFalse(report["sessions"][0]["identity_verified"])
                self.assertEqual(
                    "blocked",
                    self._target_report(report, "stage.demo")["gates"]
                    ["resource_seen"]["status"],
                )
                self.assertEqual(
                    "blocked",
                    self._target_report(report, "menu.demo")["gates"]
                    ["state_observed"]["status"],
                )

    def test_manifest_catalog_binding_rejects_changed_selectors_or_requirements(self):
        for mutation in ("selectors", "requirements"):
            with self.subTest(mutation=mutation):
                catalog = self._catalog(
                    [
                        self._target(
                            "stage.demo",
                            "stage",
                            ["resource_seen", "state_observed"],
                        )
                    ],
                    name="catalog-binding-{}.json".format(mutation),
                )
                session_id = "catalog-binding-{}".format(mutation)
                manifest = self._manifest(
                    "catalog-binding-{}".format(mutation),
                    session_id,
                    [
                        self._observation(
                            session_id,
                            0,
                            "resource_seen",
                            ["stage.demo"],
                            {"resource_id": "stage-resource"},
                        ),
                        self._observation(
                            session_id,
                            1,
                            "state_observed",
                            ["stage.demo"],
                            {"state_id": "stage-state"},
                        ),
                    ],
                    capture_complete=True,
                )
                changed_catalog = json.loads(catalog.read_text(encoding="utf-8"))
                if mutation == "selectors":
                    changed_catalog["targets"][0]["selectors"] = {
                        "scene": "changed-selector"
                    }
                else:
                    changed_catalog["targets"][0]["required_evidence"] = [
                        "state_observed",
                        "resource_seen",
                    ]
                self._write_json(catalog.name, changed_catalog)

                code, report, _ = self._report(catalog, [manifest], require_complete=True)

                self.assertEqual(1, code)
                self.assertFalse(report["sessions"][0]["identity_verified"])
                self.assertEqual(
                    "blocked",
                    self._target_report(report, "stage.demo")["gates"]
                    ["resource_seen"]["status"],
                )
                self.assertEqual(
                    "blocked",
                    self._target_report(report, "stage.demo")["gates"]
                    ["state_observed"]["status"],
                )

    def test_incomplete_delivery_keeps_resource_state_but_blocks_render_validation(self):
        catalog = self._catalog(
            [
                self._target(
                    "stage.demo",
                    "stage",
                    [
                        "resource_seen",
                        "state_observed",
                        "render_captured",
                        "native_pixels_match",
                    ],
                )
            ]
        )
        native = self._compare(["stage.demo"], scope="native")
        session_id = "dropped-delivery"
        manifest = self._manifest(
            "dropped-delivery",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "resource"},
                ),
                self._observation(
                    session_id,
                    1,
                    "state_observed",
                    ["stage.demo"],
                    {"state_id": "state"},
                ),
                self._observation(
                    session_id,
                    2,
                    "render_captured",
                    ["stage.demo"],
                    self._render_evidence(native),
                ),
            ],
            delivery={"produced_count": 4, "delivered_count": 3, "dropped_count": 1},
            capture_complete=True,
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        stage = self._target_report(report, "stage.demo")
        self.assertEqual("satisfied", stage["gates"]["resource_seen"]["status"])
        self.assertEqual("satisfied", stage["gates"]["state_observed"]["status"])
        self.assertEqual("blocked", stage["gates"]["render_captured"]["status"])
        self.assertEqual("blocked", stage["gates"]["native_pixels_match"]["status"])
        self.assertFalse(report["sessions"][0]["complete"])

    def test_sequence_receipt_requires_contiguous_native_frames(self):
        first = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-0",
            state_id="fade",
            with_provenance=True,
        )
        second = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-1",
            state_id="fade",
            with_provenance=True,
        )
        trace = self._sequence_trace(
            "transition.demo",
            [
                {"sequence": 0, "frame_id": "fade-0", "state_id": "fade"},
                {"sequence": 1, "frame_id": "fade-1", "state_id": "fade"},
                {"sequence": 2, "frame_id": "fade-2", "state_id": "fade"},
            ],
            "truncated-trace.json",
        )
        catalog = self._catalog(
            [
                self._target(
                    "transition.demo",
                    "transition",
                    ["sequence_match"],
                    sequence_expectation=trace,
                )
            ],
            trusted_producers=self._trusted_producers,
        )
        session_id = "transition-gap"
        sequence_path = self._write_json(
            "sequence-gap.json",
            {
                "schema_version": 1,
                "kind": "sequence_receipt",
                "reference_sha256": self.reference_sha256,
                "reference_trace_sha256": trace["reference_trace_sha256"],
                "session_id": session_id,
                "target_id": "transition.demo",
                "expected_frames": [
                    {
                        "sequence": 0,
                        "frame_id": first["frame_id"],
                        "state_id": first["state_id"],
                        "comparison_receipt_path": first["receipt_path"].name,
                        "comparison_receipt_sha256": _sha256(
                            first["receipt_path"].read_bytes()
                        ),
                    },
                    {
                        "sequence": 1,
                        "frame_id": second["frame_id"],
                        "state_id": second["state_id"],
                        "comparison_receipt_path": second["receipt_path"].name,
                        "comparison_receipt_sha256": _sha256(
                            second["receipt_path"].read_bytes()
                        ),
                    },
                ],
            },
        )
        manifest = self._manifest(
            "transition-gap",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(first),
                ),
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(second),
                ),
            ],
            sequence_receipts=[
                {"path": sequence_path.name, "sha256": _sha256(sequence_path.read_bytes())}
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(first, second),
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        transition = self._target_report(report, "transition.demo")
        self.assertEqual("blocked", transition["gates"]["sequence_match"]["status"])
        self.assertTrue(report["rejected_sequence_receipts"])

    def test_sequence_receipt_rejects_subsequence_of_catalog_trace(self):
        second = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-1",
            state_id="fade",
            with_provenance=True,
        )
        third = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-2",
            state_id="fade",
            with_provenance=True,
        )
        trace = self._sequence_trace(
            "transition.demo",
            [
                {"sequence": 0, "frame_id": "fade-0", "state_id": "fade"},
                {"sequence": 1, "frame_id": "fade-1", "state_id": "fade"},
                {"sequence": 2, "frame_id": "fade-2", "state_id": "fade"},
            ],
            "subsequence-trace.json",
        )
        catalog = self._catalog(
            [
                self._target(
                    "transition.demo",
                    "transition",
                    ["sequence_match"],
                    sequence_expectation=trace,
                )
            ],
            trusted_producers=self._trusted_producers,
        )
        session_id = "transition-subsequence"
        sequence_path = self._sequence_receipt(
            "subsequence-receipt.json",
            session_id,
            "transition.demo",
            trace,
            [(1, second), (2, third)],
        )
        manifest = self._manifest(
            "transition-subsequence",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["transition.demo"],
                    {"resource_id": "transition-bootstrap"},
                ),
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(second),
                ),
                self._observation(
                    session_id,
                    2,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(third),
                ),
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(second, third),
            sequence_receipts=[
                {"path": sequence_path.name, "sha256": _sha256(sequence_path.read_bytes())}
            ],
        )

        code, report, _ = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(1, code)
        self.assertEqual(
            "blocked",
            self._target_report(report, "transition.demo")["gates"]
            ["sequence_match"]["status"],
        )
        self.assertTrue(report["rejected_sequence_receipts"])

    def test_contiguous_sequence_credits_native_transition(self):
        first = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-0",
            state_id="fade",
            with_provenance=True,
        )
        second = self._compare(
            ["transition.demo"],
            scope="native",
            frame_id="fade-1",
            state_id="fade",
            with_provenance=True,
        )
        trace = self._sequence_trace(
            "transition.demo",
            [
                {"sequence": 0, "frame_id": "fade-0", "state_id": "fade"},
                {"sequence": 1, "frame_id": "fade-1", "state_id": "fade"},
            ],
            "complete-trace.json",
        )
        catalog = self._catalog(
            [
                self._target(
                    "transition.demo",
                    "transition",
                    ["sequence_match"],
                    sequence_expectation=trace,
                )
            ],
            trusted_producers=self._trusted_producers,
        )
        session_id = "transition-complete"
        sequence_path = self._write_json(
            "sequence-complete.json",
            {
                "schema_version": 1,
                "kind": "sequence_receipt",
                "reference_sha256": self.reference_sha256,
                "reference_trace_sha256": trace["reference_trace_sha256"],
                "session_id": session_id,
                "target_id": "transition.demo",
                "expected_frames": [
                    {
                        "sequence": 0,
                        "frame_id": first["frame_id"],
                        "state_id": first["state_id"],
                        "comparison_receipt_path": first["receipt_path"].name,
                        "comparison_receipt_sha256": _sha256(
                            first["receipt_path"].read_bytes()
                        ),
                    },
                    {
                        "sequence": 1,
                        "frame_id": second["frame_id"],
                        "state_id": second["state_id"],
                        "comparison_receipt_path": second["receipt_path"].name,
                        "comparison_receipt_sha256": _sha256(
                            second["receipt_path"].read_bytes()
                        ),
                    },
                ],
            },
        )
        manifest = self._manifest(
            "transition-complete",
            session_id,
            [
                self._observation(
                    session_id,
                    0,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(first),
                ),
                self._observation(
                    session_id,
                    1,
                    "render_captured",
                    ["transition.demo"],
                    self._render_evidence(second),
                ),
            ],
            sequence_receipts=[
                {"path": sequence_path.name, "sha256": _sha256(sequence_path.read_bytes())}
            ],
            capture_complete=True,
            producer_receipts=self._producer_bindings(first, second),
        )

        code, report, stderr = self._report(catalog, [manifest], require_complete=True)
        self.assertEqual(0, code, stderr)
        transition = self._target_report(report, "transition.demo")
        self.assertEqual("satisfied", transition["gates"]["sequence_match"]["status"])

    def test_malformed_jsonl_preserves_valid_observation_but_returns_nonzero(self):
        catalog = self._catalog(
            [self._target("stage.demo", "stage", ["resource_seen"])]
        )
        session_id = "malformed-jsonl"
        observations_path = self.root / "malformed.jsonl"
        observations_path.write_text(
            json.dumps(
                self._observation(
                    session_id,
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "valid-resource"},
                )
            )
            + "\n{\n",
            encoding="utf-8",
        )
        manifest = self._write_json(
            "malformed.manifest.json",
            {
                "schema_version": 1,
                "reference_sha256": self.reference_sha256,
                "catalog_sha256": self._active_catalog_sha256(),
                "session_id": session_id,
                "observations_path": observations_path.name,
                "observations_sha256": _sha256(observations_path.read_bytes()),
                "delivery": {"produced_count": 2, "delivered_count": 2, "dropped_count": 0},
            },
        )

        code, report, _ = self._report(catalog, [manifest])
        self.assertEqual(1, code)
        self.assertEqual("satisfied", self._target_report(report, "stage.demo")["gates"]["resource_seen"]["status"])
        self.assertEqual(1, len(report["rejected_observations"]))
        self.assertFalse(report["sessions"][0]["complete"])

    def test_multiple_manifests_accumulate_independent_complete_sessions(self):
        catalog = self._catalog(
            [
                self._target("stage.demo", "stage", ["resource_seen"]),
                self._target("menu.demo", "menu", ["state_observed"]),
            ]
        )
        stage_manifest = self._manifest(
            "stage-session",
            "stage-session",
            [
                self._observation(
                    "stage-session",
                    0,
                    "resource_seen",
                    ["stage.demo"],
                    {"resource_id": "stage-resource"},
                )
            ],
            capture_complete=True,
        )
        menu_manifest = self._manifest(
            "menu-session",
            "menu-session",
            [
                self._observation(
                    "menu-session",
                    0,
                    "state_observed",
                    ["menu.demo"],
                    {"state_id": "menu-state"},
                )
            ],
            capture_complete=True,
        )

        code, report, stderr = self._report(
            catalog, [stage_manifest, menu_manifest], require_complete=True
        )
        self.assertEqual(0, code, stderr)
        self.assertEqual(2, len(report["sessions"]))
        self.assertEqual(
            "satisfied",
            self._target_report(report, "stage.demo")["gates"]["resource_seen"]["status"],
        )
        self.assertEqual(
            "satisfied",
            self._target_report(report, "menu.demo")["gates"]["state_observed"]["status"],
        )


if __name__ == "__main__":
    unittest.main()
