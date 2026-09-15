import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


SCHEMA_VERSION = 1
CATEGORIES = ("stage", "menu", "effect", "transition")
GATES = (
    "resource_seen",
    "state_observed",
    "render_captured",
    "backend_pixels_match",
    "native_pixels_match",
    "sequence_match",
)
MAX_RGBA_BYTES = 64 * 1024 * 1024
MAX_JSON_METADATA_BYTES = 64 * 1024 * 1024
MAX_ARTIFACT_BYTES = 512 * 1024 * 1024
MAX_JSONL_LINE_BYTES = 1024 * 1024
MAX_JSONL_BYTES = 512 * 1024 * 1024
MAX_JSONL_RECORDS = 10_000
MAX_CREDITABLE_OBSERVATION_BYTES = 16 * 1024 * 1024
MAX_REPORTED_OBSERVATION_ISSUES = 256
PRODUCER_ROLES = ("reference", "backend", "native")
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")


class CoverageError(ValueError):
    pass


def _sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def _is_integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _require_dict(value, label):
    if not isinstance(value, dict):
        raise CoverageError(f"{label} must be an object")
    return value


def _require_list(value, label):
    if not isinstance(value, list):
        raise CoverageError(f"{label} must be an array")
    return value


def _require_string(value, label):
    if not isinstance(value, str) or not value:
        raise CoverageError(f"{label} must be a nonempty string")
    return value


def _require_sha256(value, label):
    value = _require_string(value, label)
    if not _SHA256.fullmatch(value):
        raise CoverageError(f"{label} must be a lowercase SHA-256 hex string")
    return value


def _require_schema(document, label):
    if document.get("schema_version") != SCHEMA_VERSION:
        raise CoverageError(f"{label}.schema_version must be {SCHEMA_VERSION}")


def _require_identifier_list(value, label):
    identifiers = _require_list(value, label)
    if not identifiers:
        raise CoverageError(f"{label} must not be empty")
    for index, identifier in enumerate(identifiers):
        _require_string(identifier, f"{label}[{index}]")
    if len(set(identifiers)) != len(identifiers):
        raise CoverageError(f"{label} must not contain duplicate identifiers")
    return identifiers


def _expected_rgba_length(width, height, label):
    if not _is_integer(width) or width <= 0:
        raise CoverageError(f"{label}.width must be a positive integer")
    if not _is_integer(height) or height <= 0:
        raise CoverageError(f"{label}.height must be a positive integer")
    expected = width * height * 4
    if expected > MAX_RGBA_BYTES:
        raise CoverageError(
            f"{label} exceeds the {MAX_RGBA_BYTES}-byte RGBA8 comparison bound"
        )
    return expected


def _resolve_path(base, value, label):
    path = Path(_require_string(value, label))
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def _read_file(path, label, maximum_bytes=None):
    if maximum_bytes is None:
        maximum_bytes = MAX_JSON_METADATA_BYTES
    try:
        initial_size = path.stat().st_size
        if initial_size > maximum_bytes:
            raise CoverageError(
                f"{label} exceeds the {maximum_bytes}-byte read limit"
            )
        with path.open("rb") as stream:
            payload = stream.read(maximum_bytes + 1)
        final_size = path.stat().st_size
    except OSError as error:
        raise CoverageError(f"cannot read {label} {path}: {error}") from error
    if len(payload) > maximum_bytes:
        raise CoverageError(f"{label} exceeds the {maximum_bytes}-byte read limit")
    if final_size != initial_size or len(payload) != initial_size:
        raise CoverageError(f"{label} changed while reading")
    return payload


def _hash_file(path, label, maximum_bytes=None):
    if maximum_bytes is None:
        maximum_bytes = MAX_ARTIFACT_BYTES
    digest = hashlib.sha256()
    size = 0
    try:
        initial_size = path.stat().st_size
        if initial_size > maximum_bytes:
            raise CoverageError(
                f"{label} exceeds the {maximum_bytes}-byte hash limit"
            )
        with path.open("rb") as stream:
            while True:
                remaining = maximum_bytes - size
                chunk = stream.read(min(1024 * 1024, remaining + 1))
                if not chunk:
                    break
                size += len(chunk)
                if size > maximum_bytes:
                    raise CoverageError(
                        f"{label} exceeds the {maximum_bytes}-byte hash limit"
                    )
                digest.update(chunk)
        final_size = path.stat().st_size
    except OSError as error:
        raise CoverageError(f"cannot read {label} {path}: {error}") from error
    if final_size != initial_size or size != initial_size:
        raise CoverageError(f"{label} changed while hashing")
    return digest.hexdigest(), size


def _same_file(first, second):
    try:
        return first.samefile(second)
    except OSError:
        return first == second


def _producer_identity(producer):
    return (
        producer["build_sha256"],
        producer["source_manifest_sha256"],
    )


def _read_json_with_sha256(path, label):
    payload = _read_file(path, label)
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CoverageError(f"{label} is not UTF-8") from error
    try:
        return _require_dict(json.loads(text), label), _sha256(payload)
    except json.JSONDecodeError as error:
        raise CoverageError(f"{label} is not valid JSON: {error.msg}") from error


def _read_json(path, label):
    document, _ = _read_json_with_sha256(path, label)
    return document


def _write_json(path, document):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _emit_json(output, document):
    if output is None:
        print(json.dumps(document, indent=2, sort_keys=True))
    else:
        _write_json(output, document)


def _read_exact_rgba(path, width, height, label):
    expected = _expected_rgba_length(width, height, label)
    try:
        size = path.stat().st_size
    except OSError as error:
        raise CoverageError(f"cannot stat {label} {path}: {error}") from error
    if size != expected:
        raise CoverageError(
            f"{label} expected {expected} RGBA8 bytes, found {size}"
        )
    payload = _read_file(path, label, expected)
    if len(payload) != expected:
        raise CoverageError(
            f"{label} expected {expected} RGBA8 bytes, found {len(payload)}"
        )
    return payload, _sha256(payload), expected


def _hash_exact_rgba(path, width, height, label):
    expected = _expected_rgba_length(width, height, label)
    try:
        size = path.stat().st_size
    except OSError as error:
        raise CoverageError(f"cannot stat {label} {path}: {error}") from error
    if size != expected:
        raise CoverageError(
            f"{label} expected {expected} RGBA8 bytes, found {size}"
        )
    payload_sha256, actual_size = _hash_file(path, label)
    if actual_size != expected:
        raise CoverageError(
            f"{label} expected {expected} RGBA8 bytes, found {actual_size}"
        )
    return payload_sha256, expected


def _parse_trusted_producers(document):
    producers = _require_list(
        document.get("trusted_producers", []), "catalog.trusted_producers"
    )
    parsed = []
    producer_ids = set()
    for index, value in enumerate(producers):
        producer = _require_dict(value, f"catalog.trusted_producers[{index}]")
        producer_id = _require_string(
            producer.get("id"), f"catalog.trusted_producers[{index}].id"
        )
        if producer_id in producer_ids:
            raise CoverageError(f"catalog trusted producer ID is duplicated: {producer_id}")
        producer_ids.add(producer_id)
        role = _require_string(
            producer.get("role"), f"catalog.trusted_producers[{index}].role"
        )
        if role not in PRODUCER_ROLES:
            raise CoverageError(
                f"catalog trusted producer {producer_id} has unsupported role {role!r}"
            )
        parsed.append(
            {
                "id": producer_id,
                "role": role,
                "build_path": _require_string(
                    producer.get("build_path"),
                    f"catalog.trusted_producers[{index}].build_path",
                ),
                "build_sha256": _require_sha256(
                    producer.get("build_sha256"),
                    f"catalog.trusted_producers[{index}].build_sha256",
                ),
                "source_manifest_path": _require_string(
                    producer.get("source_manifest_path"),
                    f"catalog.trusted_producers[{index}].source_manifest_path",
                ),
                "source_manifest_sha256": _require_sha256(
                    producer.get("source_manifest_sha256"),
                    f"catalog.trusted_producers[{index}].source_manifest_sha256",
                ),
            }
        )
    return parsed


def _parse_sequence_expectation(value, label):
    if value is None:
        return None
    expectation = _require_dict(value, label)
    return {
        "reference_trace_path": _require_string(
            expectation.get("reference_trace_path"), f"{label}.reference_trace_path"
        ),
        "reference_trace_sha256": _require_sha256(
            expectation.get("reference_trace_sha256"),
            f"{label}.reference_trace_sha256",
        ),
    }


def _parse_catalog(path):
    catalog_path = Path(path).resolve()
    document, catalog_sha256 = _read_json_with_sha256(catalog_path, "catalog")
    _require_schema(document, "catalog")
    reference_sha256 = _require_sha256(
        document.get("reference_sha256"), "catalog.reference_sha256"
    )
    full_unified_edition_scope_complete = document.get(
        "full_unified_edition_scope_complete", False
    )
    _require_boolean(
        full_unified_edition_scope_complete,
        "catalog.full_unified_edition_scope_complete",
    )
    open_dimensions = _require_list(
        document.get("open_dimensions", []), "catalog.open_dimensions"
    )
    for index, dimension in enumerate(open_dimensions):
        _require_string(dimension, f"catalog.open_dimensions[{index}]")
    targets = _require_list(document.get("targets"), "catalog.targets")
    parsed_targets = []
    target_ids = set()
    for index, value in enumerate(targets):
        target = _require_dict(value, f"catalog.targets[{index}]")
        target_id = _require_string(target.get("id"), f"catalog.targets[{index}].id")
        if target_id in target_ids:
            raise CoverageError(f"catalog target ID is duplicated: {target_id}")
        target_ids.add(target_id)
        category = _require_string(
            target.get("category"), f"catalog.targets[{index}].category"
        )
        if category not in CATEGORIES:
            raise CoverageError(
                f"catalog target {target_id} has unsupported category {category!r}"
            )
        label = _require_string(target.get("label"), f"catalog.targets[{index}].label")
        selectors = target.get("selectors")
        if selectors is not None:
            _require_dict(selectors, f"catalog.targets[{index}].selectors")
        sequence_expectation = _parse_sequence_expectation(
            target.get("sequence_expectation"),
            f"catalog.targets[{index}].sequence_expectation",
        )
        required_defined = "required_evidence" in target
        required_evidence = []
        if required_defined:
            required_evidence = _require_list(
                target["required_evidence"],
                f"catalog.targets[{index}].required_evidence",
            )
            for gate_index, gate in enumerate(required_evidence):
                _require_string(
                    gate,
                    f"catalog.targets[{index}].required_evidence[{gate_index}]",
                )
                if gate not in GATES:
                    raise CoverageError(
                        f"catalog target {target_id} requires unknown gate {gate!r}"
                    )
            if len(set(required_evidence)) != len(required_evidence):
                raise CoverageError(
                    f"catalog target {target_id} duplicates required evidence gates"
                )
        parsed_targets.append(
            {
                "id": target_id,
                "category": category,
                "label": label,
                "selectors": selectors,
                "sequence_expectation": sequence_expectation,
                "required_evidence": required_evidence,
                "acceptance_defined": required_defined and bool(required_evidence),
            }
        )
    trusted_producers = _parse_trusted_producers(document)
    return {
        "path": catalog_path,
        "sha256": catalog_sha256,
        "reference_sha256": reference_sha256,
        "targets": parsed_targets,
        "by_id": {target["id"]: target for target in parsed_targets},
        "trusted_producers": {
            producer["id"]: producer for producer in trusted_producers
        },
        "scope": {
            "full_unified_edition_scope_complete": full_unified_edition_scope_complete,
            "open_dimensions": open_dimensions,
            "complete": full_unified_edition_scope_complete and not open_dimensions,
        },
    }


def _inventory(catalog):
    category_target_counts = {
        category: sum(
            target["category"] == category for target in catalog["targets"]
        )
        for category in CATEGORIES
    }
    targets = []
    for target in catalog["targets"]:
        value = {
            "id": target["id"],
            "category": target["category"],
            "label": target["label"],
            "required_evidence": target["required_evidence"],
            "acceptance_defined": target["acceptance_defined"],
        }
        if target["selectors"] is not None:
            value["selectors"] = target["selectors"]
        if target["sequence_expectation"] is not None:
            value["sequence_expectation"] = target["sequence_expectation"]
        targets.append(value)
    return {
        "schema_version": SCHEMA_VERSION,
        "reference_sha256": catalog["reference_sha256"],
        "catalog_sha256": catalog["sha256"],
        "target_count": len(targets),
        "category_target_counts": category_target_counts,
        "trusted_producer_count": len(catalog["trusted_producers"]),
        "catalog_scope": catalog["scope"],
        "targets": targets,
    }


def _compare_payloads(reference, candidate):
    differing_pixels = 0
    max_channel_error = 0
    for offset in range(0, len(reference), 4):
        reference_pixel = reference[offset:offset + 4]
        candidate_pixel = candidate[offset:offset + 4]
        if reference_pixel != candidate_pixel:
            differing_pixels += 1
            for reference_channel, candidate_channel in zip(
                reference_pixel, candidate_pixel
            ):
                max_channel_error = max(
                    max_channel_error, abs(reference_channel - candidate_channel)
                )
    return differing_pixels, max_channel_error


def _make_comparison_receipt(args):
    if args.scope not in ("backend", "native"):
        raise CoverageError("comparison scope must be backend or native")
    reference_sha256 = _require_sha256(
        args.reference_sha256, "comparison.reference_sha256"
    )
    reference_source_sha256 = _require_sha256(
        args.reference_source_sha256, "comparison.reference_source_sha256"
    )
    candidate_source_sha256 = _require_sha256(
        args.candidate_source_sha256, "comparison.candidate_source_sha256"
    )
    target_ids = _require_identifier_list(args.target_ids, "comparison.target_ids")
    frame_id = _require_string(args.frame_id, "comparison.frame_id")
    state_id = _require_string(args.state_id, "comparison.state_id")
    reference_path = Path(args.reference).resolve()
    candidate_path = Path(args.candidate).resolve()
    reference, reference_payload_sha256, expected = _read_exact_rgba(
        reference_path, args.width, args.height, "reference image"
    )
    candidate, candidate_payload_sha256, candidate_expected = _read_exact_rgba(
        candidate_path, args.width, args.height, "candidate image"
    )
    if candidate_expected != expected:
        raise CoverageError("reference and candidate RGBA8 lengths differ")
    differing_pixels, max_channel_error = _compare_payloads(reference, candidate)
    reference_producer_receipt = args.reference_producer_receipt
    candidate_producer_receipt = args.candidate_producer_receipt
    if bool(reference_producer_receipt) != bool(candidate_producer_receipt):
        raise CoverageError(
            "reference and candidate producer receipts must be supplied together"
        )
    receipt = {
        "schema_version": SCHEMA_VERSION,
        "kind": "rgba_comparison",
        "reference_sha256": reference_sha256,
        "scope": args.scope,
        "target_ids": target_ids,
        "frame_id": frame_id,
        "state_id": state_id,
        "width": args.width,
        "height": args.height,
        "expected_byte_length": expected,
        "reference_path": str(reference_path),
        "candidate_path": str(candidate_path),
        "reference_payload_sha256": reference_payload_sha256,
        "candidate_payload_sha256": candidate_payload_sha256,
        "reference_source_sha256": reference_source_sha256,
        "candidate_source_sha256": candidate_source_sha256,
        "differing_pixels": differing_pixels,
        "max_channel_error": max_channel_error,
        "raw_bytes_equal": differing_pixels == 0,
    }
    if reference_producer_receipt:
        reference_producer_path = Path(reference_producer_receipt).resolve()
        candidate_producer_path = Path(candidate_producer_receipt).resolve()
        receipt["reference_producer_receipt_path"] = str(reference_producer_path)
        receipt["reference_producer_receipt_sha256"] = _sha256(
            _read_file(reference_producer_path, "reference producer receipt")
        )
        receipt["candidate_producer_receipt_path"] = str(candidate_producer_path)
        receipt["candidate_producer_receipt_sha256"] = _sha256(
            _read_file(candidate_producer_path, "candidate producer receipt")
        )
    return receipt


def _validate_observation(document, label):
    document = _require_dict(document, label)
    _require_schema(document, label)
    _require_sha256(document.get("reference_sha256"), f"{label}.reference_sha256")
    _require_string(document.get("session_id"), f"{label}.session_id")
    sequence = document.get("sequence")
    if not _is_integer(sequence) or sequence < 0:
        raise CoverageError(f"{label}.sequence must be a nonnegative integer")
    kind = _require_string(document.get("kind"), f"{label}.kind")
    if kind not in ("state_observed", "resource_seen", "render_captured"):
        raise CoverageError(f"{label}.kind is unsupported: {kind!r}")
    _require_identifier_list(document.get("target_ids"), f"{label}.target_ids")
    evidence = _require_dict(document.get("evidence"), f"{label}.evidence")
    if kind == "resource_seen":
        _require_string(evidence.get("resource_id"), f"{label}.evidence.resource_id")
    elif kind == "state_observed":
        _require_string(evidence.get("state_id"), f"{label}.evidence.state_id")
    else:
        _validate_render_fields(evidence, label)
    return document


def _validate_render_fields(evidence, label):
    _require_string(evidence.get("render_path"), f"{label}.evidence.render_path")
    _require_sha256(evidence.get("render_sha256"), f"{label}.evidence.render_sha256")
    width = evidence.get("width")
    height = evidence.get("height")
    _expected_rgba_length(width, height, f"{label}.evidence")
    if evidence.get("format") != "rgba8":
        raise CoverageError(f"{label}.evidence.format must be 'rgba8'")
    _require_string(evidence.get("frame_id"), f"{label}.evidence.frame_id")
    _require_string(evidence.get("state_id"), f"{label}.evidence.state_id")
    receipt_path = "comparison_receipt_path" in evidence
    receipt_sha256 = "comparison_receipt_sha256" in evidence
    if receipt_path != receipt_sha256:
        raise CoverageError(
            f"{label}.evidence comparison receipt path and SHA-256 must appear together"
        )
    if receipt_path:
        _require_string(
            evidence["comparison_receipt_path"],
            f"{label}.evidence.comparison_receipt_path",
        )
        _require_sha256(
            evidence["comparison_receipt_sha256"],
            f"{label}.evidence.comparison_receipt_sha256",
        )


def _validate_manifest(document, label):
    document = _require_dict(document, label)
    _require_schema(document, label)
    _require_sha256(document.get("reference_sha256"), f"{label}.reference_sha256")
    _require_sha256(document.get("catalog_sha256"), f"{label}.catalog_sha256")
    _require_string(document.get("session_id"), f"{label}.session_id")
    _require_string(document.get("observations_path"), f"{label}.observations_path")
    _require_sha256(
        document.get("observations_sha256"), f"{label}.observations_sha256"
    )
    delivery = _require_dict(document.get("delivery"), f"{label}.delivery")
    for field in ("produced_count", "delivered_count", "dropped_count"):
        value = delivery.get(field)
        if not _is_integer(value) or value < 0:
            raise CoverageError(f"{label}.delivery.{field} must be a nonnegative integer")
    capture_complete = document.get("capture_complete", False)
    _require_boolean(capture_complete, f"{label}.capture_complete")
    capture_issues = _require_list(
        document.get("capture_issues", []), f"{label}.capture_issues"
    )
    for index, issue in enumerate(capture_issues):
        _require_string(issue, f"{label}.capture_issues[{index}]")
    sequence_receipts = document.get("sequence_receipts", [])
    _require_list(sequence_receipts, f"{label}.sequence_receipts")
    for index, binding in enumerate(sequence_receipts):
        binding = _require_dict(binding, f"{label}.sequence_receipts[{index}]")
        _require_string(binding.get("path"), f"{label}.sequence_receipts[{index}].path")
        _require_sha256(
            binding.get("sha256"), f"{label}.sequence_receipts[{index}].sha256"
        )
    producer_receipts = document.get("producer_receipts", [])
    _require_list(producer_receipts, f"{label}.producer_receipts")
    for index, binding in enumerate(producer_receipts):
        binding = _require_dict(binding, f"{label}.producer_receipts[{index}]")
        _require_string(binding.get("path"), f"{label}.producer_receipts[{index}].path")
        _require_sha256(
            binding.get("sha256"), f"{label}.producer_receipts[{index}].sha256"
        )
    return document


def _new_target_state(target):
    return {
        "id": target["id"],
        "category": target["category"],
        "label": target["label"],
        "required_evidence": target["required_evidence"],
        "acceptance_defined": target["acceptance_defined"],
        "gates": {
            gate: {"status": "missing", "evidence_count": 0} for gate in GATES
        },
        "unmet_required_evidence": [],
    }


def _mark_gate(target, gate, outcome):
    if outcome not in ("satisfied", "blocked", "failed", "open"):
        raise CoverageError(f"unsupported gate outcome {outcome!r}")
    result = target["gates"][gate]
    result["evidence_count"] += 1
    if outcome == "failed":
        result["status"] = "failed"
    elif result["status"] == "failed":
        return
    elif outcome == "satisfied":
        result["status"] = "satisfied"
    elif outcome == "blocked" and result["status"] in ("missing", "open"):
        result["status"] = "blocked"
    elif outcome == "open" and result["status"] == "missing":
        result["status"] = "open"


def _issue(path, reason, session_id=None, line=None, target_ids=None):
    result = {"manifest_path": str(path), "reason": reason}
    if session_id is not None:
        result["session_id"] = session_id
    if line is not None:
        result["line"] = line
    if target_ids is not None:
        result["target_ids"] = target_ids
    return result


def _add_reason(session, reason):
    if reason not in session["reasons"]:
        session["reasons"].append(reason)


def _append_observation_issue(report, key, value):
    issues = report[key]
    if len(issues) < MAX_REPORTED_OBSERVATION_ISSUES:
        issues.append(value)
        return
    report["observation_issue_overflow"][key] += 1


def _finalize_session_readiness(session):
    session["normalized_delivery_complete"] = not session["reasons"]
    session["fixture_complete"] = (
        session["capture_complete"] and not session["capture_issues"]
    )
    session["complete"] = (
        session["normalized_delivery_complete"] and session["fixture_complete"]
    )


def _stream_jsonl(path, handle_line):
    digest = hashlib.sha256()
    line_count = 0
    byte_count = 0
    try:
        initial_size = path.stat().st_size
        if initial_size > MAX_JSONL_BYTES:
            raise CoverageError(
                f"observation JSONL exceeds the {MAX_JSONL_BYTES}-byte limit"
            )
        with path.open("rb") as stream:
            def read_chunk(maximum_chunk_bytes):
                nonlocal byte_count
                remaining = MAX_JSONL_BYTES - byte_count
                chunk = stream.readline(min(maximum_chunk_bytes, remaining + 1))
                if not chunk:
                    return chunk
                byte_count += len(chunk)
                if byte_count > MAX_JSONL_BYTES:
                    raise CoverageError(
                        f"observation JSONL exceeds the {MAX_JSONL_BYTES}-byte limit"
                    )
                digest.update(chunk)
                return chunk

            while True:
                line = read_chunk(MAX_JSONL_LINE_BYTES + 1)
                if not line:
                    break
                line_count += 1
                too_long = len(line) > MAX_JSONL_LINE_BYTES
                while line and not line.endswith(b"\n") and too_long:
                    line = read_chunk(64 * 1024)
                handle_line(line_count, None if too_long else line, too_long)
        final_size = path.stat().st_size
    except OSError as error:
        raise CoverageError(f"cannot read observation JSONL {path}: {error}") from error
    if final_size != initial_size or byte_count != initial_size:
        raise CoverageError("observation JSONL changed while streaming")
    return digest.hexdigest(), line_count


def _load_manifest(path, catalog, report):
    manifest_path = Path(path).resolve()
    document = _validate_manifest(_read_json(manifest_path, "session manifest"), "manifest")
    session_id = document["session_id"]
    session = {
        "manifest_path": manifest_path,
        "document": document,
        "session_id": session_id,
        "reasons": [],
        "records": [],
        "producer_receipts": {},
        "identity_verified": True,
        "normalized_delivery_complete": False,
        "capture_complete": document.get("capture_complete", False),
        "capture_issues": document.get("capture_issues", []),
        "fixture_complete": False,
        "complete": False,
        "observation_line_count": 0,
        "credited_record_count": 0,
        "credited_observation_bytes": 0,
        "parsed_observation_count": 0,
        "next_observation_sequence": 0,
        "sequence_order_valid": True,
        "record_limit_exceeded": False,
    }
    if document["reference_sha256"] != catalog["reference_sha256"]:
        reason = "manifest reference_sha256 does not match catalog"
        session["identity_verified"] = False
        _add_reason(session, reason)
        report["rejected_sessions"].append(
            _issue(manifest_path, reason, session_id=session_id)
        )
    if document["catalog_sha256"] != catalog["sha256"]:
        reason = "manifest catalog_sha256 does not match catalog"
        session["identity_verified"] = False
        _add_reason(session, reason)
        report["rejected_sessions"].append(
            _issue(manifest_path, reason, session_id=session_id)
        )
    observations_path = _resolve_path(
        manifest_path.parent, document["observations_path"], "manifest.observations_path"
    )
    delivery = document["delivery"]
    if delivery["produced_count"] != delivery["delivered_count"] + delivery["dropped_count"]:
        _add_reason(session, "delivery counts do not balance")
        report["rejected_sessions"].append(
            _issue(manifest_path, "delivery counts do not balance", session_id=session_id)
        )
    if delivery["dropped_count"]:
        _add_reason(session, "delivery reported dropped observations")

    def handle_line(line_number, raw_line, too_long):
        if line_number > MAX_JSONL_RECORDS:
            if not session["record_limit_exceeded"]:
                session["record_limit_exceeded"] = True
                _add_reason(
                    session,
                    f"observation JSONL exceeds {MAX_JSONL_RECORDS}-record limit",
                )
            return
        if too_long:
            reason = (
                f"observation JSONL line exceeds {MAX_JSONL_LINE_BYTES}-byte limit"
            )
            _add_reason(session, "malformed observations are present")
            _append_observation_issue(
                report,
                "rejected_observations",
                _issue(manifest_path, reason, session_id, line_number)
            )
            return
        try:
            line = raw_line.decode("utf-8").rstrip("\r\n")
        except UnicodeDecodeError:
            reason = "observation JSONL line is not UTF-8"
            _add_reason(session, "malformed observations are present")
            _append_observation_issue(
                report,
                "rejected_observations",
                _issue(manifest_path, reason, session_id, line_number)
            )
            return
        if not line.strip():
            reason = "blank JSONL record"
            _add_reason(session, "malformed observations are present")
            _append_observation_issue(
                report,
                "rejected_observations",
                _issue(manifest_path, reason, session_id, line_number)
            )
            return
        try:
            observation = _validate_observation(
                json.loads(line), f"observation line {line_number}"
            )
        except (CoverageError, json.JSONDecodeError) as error:
            if isinstance(error, json.JSONDecodeError):
                reason = f"invalid JSON: {error.msg}"
            else:
                reason = str(error)
            _add_reason(session, "malformed observations are present")
            _append_observation_issue(
                report,
                "rejected_observations",
                _issue(manifest_path, reason, session_id, line_number)
            )
            return
        if observation["sequence"] != session["next_observation_sequence"]:
            session["sequence_order_valid"] = False
        session["next_observation_sequence"] += 1
        session["parsed_observation_count"] += 1
        target_ids = observation["target_ids"]
        if observation["reference_sha256"] != catalog["reference_sha256"]:
            reason = "observation reference_sha256 does not match catalog"
            _add_reason(session, "version-mismatched observations are present")
            _append_observation_issue(
                report,
                "rejected_observations",
                _issue(manifest_path, reason, session_id, line_number, target_ids)
            )
            return
        if observation["session_id"] != session_id:
            reason = "observation session_id does not match manifest"
            _add_reason(session, "session-mismatched observations are present")
            _append_observation_issue(
                report,
                "rejected_observations",
                _issue(manifest_path, reason, session_id, line_number, target_ids)
            )
            return
        unknown_target_ids = [
            target_id for target_id in target_ids if target_id not in catalog["by_id"]
        ]
        if unknown_target_ids:
            reason = "observation references unknown catalog target IDs"
            _add_reason(session, "unmapped observations are present")
            _append_observation_issue(
                report,
                "unmapped_observations",
                {
                    **_issue(manifest_path, reason, session_id, line_number, target_ids),
                    "unknown_target_ids": unknown_target_ids,
                }
            )
            return
        if (
            session["credited_observation_bytes"] + len(raw_line)
            > MAX_CREDITABLE_OBSERVATION_BYTES
        ):
            _add_reason(
                session,
                "creditable observations exceed the byte budget",
            )
            return
        session["records"].append(
            {
                "line": line_number,
                "observation": observation,
                "observations_path": observations_path,
                "render": None,
                "render_error": None,
                "comparison": None,
                "comparison_error": None,
                "provenance_status": None,
                "provenance_error": None,
            }
        )
        session["credited_record_count"] += 1
        session["credited_observation_bytes"] += len(raw_line)

    try:
        observations_sha256, line_count = _stream_jsonl(observations_path, handle_line)
    except CoverageError as error:
        reason = str(error)
        session["identity_verified"] = False
        _add_reason(session, reason)
        report["rejected_sessions"].append(
            _issue(manifest_path, reason, session_id=session_id)
        )
        return session
    session["observation_line_count"] = line_count
    if observations_sha256 != document["observations_sha256"]:
        reason = "observation JSONL SHA-256 does not match manifest"
        session["identity_verified"] = False
        _add_reason(session, reason)
        report["rejected_sessions"].append(
            _issue(manifest_path, reason, session_id=session_id)
        )
    if delivery["delivered_count"] != line_count:
        _add_reason(session, "delivery delivered_count does not match JSONL line count")
        report["rejected_sessions"].append(
            _issue(
                manifest_path,
                "delivery delivered_count does not match JSONL line count",
                session_id=session_id,
            )
        )
    if (
        session["parsed_observation_count"] != delivery["delivered_count"]
        or not session["sequence_order_valid"]
    ):
        _add_reason(
            session,
            "observation sequence is not contiguous from zero in physical delivery order",
        )
    return session


def _verify_render_evidence(record):
    observation = record["observation"]
    evidence = observation["evidence"]
    render_path = _resolve_path(
        record["observations_path"].parent,
        evidence["render_path"],
        "render evidence path",
    )
    payload_sha256, expected = _hash_exact_rgba(
        render_path,
        evidence["width"],
        evidence["height"],
        "render evidence",
    )
    if payload_sha256 != evidence["render_sha256"]:
        raise CoverageError("render evidence SHA-256 does not match render_path")
    return {
        "path": render_path,
        "payload_sha256": payload_sha256,
        "expected_byte_length": expected,
        "width": evidence["width"],
        "height": evidence["height"],
    }


def _require_boolean(value, label):
    if not isinstance(value, bool):
        raise CoverageError(f"{label} must be a boolean")
    return value


def _require_nonnegative_integer(value, label):
    if not _is_integer(value) or value < 0:
        raise CoverageError(f"{label} must be a nonnegative integer")
    return value


def _validate_catalog_producers(catalog, report):
    verified = {}
    identities = {}
    rejected_duplicate_ids = set()
    for producer_id, policy in catalog["trusted_producers"].items():
        try:
            build_path = _resolve_path(
                catalog["path"].parent,
                policy["build_path"],
                f"catalog trusted producer {producer_id} build_path",
            )
            build_sha256, _ = _hash_file(build_path, f"trusted producer build {producer_id}")
            if build_sha256 != policy["build_sha256"]:
                raise CoverageError(
                    f"trusted producer {producer_id} build SHA-256 does not match catalog"
                )
            source_manifest_path = _resolve_path(
                catalog["path"].parent,
                policy["source_manifest_path"],
                f"catalog trusted producer {producer_id} source_manifest_path",
            )
            source_manifest_sha256, _ = _hash_file(
                source_manifest_path, f"trusted producer source manifest {producer_id}"
            )
            if source_manifest_sha256 != policy["source_manifest_sha256"]:
                raise CoverageError(
                    f"trusted producer {producer_id} source manifest SHA-256 does not match catalog"
                )
        except CoverageError as error:
            report["rejected_producers"].append(
                {
                    "producer_id": producer_id,
                    "catalog_path": str(catalog["path"]),
                    "reason": str(error),
                }
            )
            continue
        candidate = {
            **policy,
            "build_path": build_path,
            "source_manifest_path": source_manifest_path,
        }
        identity = _producer_identity(candidate)
        duplicate_ids = identities.get(identity)
        if duplicate_ids is None:
            identities[identity] = [producer_id]
            verified[producer_id] = candidate
            continue
        duplicate_ids.append(producer_id)
        for duplicate_id in duplicate_ids:
            verified.pop(duplicate_id, None)
            if duplicate_id in rejected_duplicate_ids:
                continue
            rejected_duplicate_ids.add(duplicate_id)
            report["rejected_producers"].append(
                {
                    "producer_id": duplicate_id,
                    "catalog_path": str(catalog["path"]),
                    "reason": (
                        "trusted producer build and source manifest identity is "
                        "shared by multiple catalog producer IDs"
                    ),
                }
            )
    build_roles = {}
    for producer_id, policy in verified.items():
        build_roles.setdefault(policy["build_sha256"], []).append(producer_id)
    for producer_ids in build_roles.values():
        roles = {verified[producer_id]["role"] for producer_id in producer_ids}
        if "reference" not in roles or not roles.intersection(("backend", "native")):
            continue
        for producer_id in producer_ids:
            verified.pop(producer_id)
            report["rejected_producers"].append(
                {
                    "producer_id": producer_id,
                    "catalog_path": str(catalog["path"]),
                    "reason": (
                        "trusted producer build SHA-256 is shared by reference and "
                        "candidate catalog producer roles"
                    ),
                }
            )
    return verified


def _read_bound_json(path, expected_sha256, label):
    raw = _read_file(path, label)
    actual_sha256 = _sha256(raw)
    if actual_sha256 != expected_sha256:
        raise CoverageError(f"{label} SHA-256 does not match its binding")
    try:
        return _require_dict(json.loads(raw.decode("utf-8")), label), actual_sha256
    except UnicodeDecodeError as error:
        raise CoverageError(f"{label} is not UTF-8") from error
    except json.JSONDecodeError as error:
        raise CoverageError(f"{label} is not valid JSON: {error.msg}") from error


def _validate_producer_receipt(receipt, receipt_path, policy):
    _require_schema(receipt, "producer receipt")
    if receipt.get("kind") != "render_producer_receipt":
        raise CoverageError("producer receipt.kind must be 'render_producer_receipt'")
    producer_id = _require_string(receipt.get("producer_id"), "producer receipt.producer_id")
    if producer_id != policy["id"]:
        raise CoverageError("producer receipt producer_id does not match catalog policy")
    if receipt.get("role") != policy["role"]:
        raise CoverageError("producer receipt role does not match catalog policy")
    build_path = _resolve_path(
        receipt_path.parent, receipt.get("build_path"), "producer receipt.build_path"
    )
    if not _same_file(build_path, policy["build_path"]):
        raise CoverageError("producer receipt build_path does not match catalog policy")
    if _require_sha256(receipt.get("build_sha256"), "producer receipt.build_sha256") != policy[
        "build_sha256"
    ]:
        raise CoverageError("producer receipt build SHA-256 does not match catalog policy")
    source_manifest_path = _resolve_path(
        receipt_path.parent,
        receipt.get("source_manifest_path"),
        "producer receipt.source_manifest_path",
    )
    if not _same_file(source_manifest_path, policy["source_manifest_path"]):
        raise CoverageError(
            "producer receipt source_manifest_path does not match catalog policy"
        )
    source_manifest_sha256 = _require_sha256(
        receipt.get("source_manifest_sha256"),
        "producer receipt.source_manifest_sha256",
    )
    if source_manifest_sha256 != policy["source_manifest_sha256"]:
        raise CoverageError(
            "producer receipt source manifest SHA-256 does not match catalog policy"
        )
    artifact_path = _resolve_path(
        receipt_path.parent, receipt.get("artifact_path"), "producer receipt.artifact_path"
    )
    artifact_sha256, _ = _hash_file(artifact_path, "producer receipt artifact")
    if artifact_sha256 != _require_sha256(
        receipt.get("artifact_sha256"), "producer receipt.artifact_sha256"
    ):
        raise CoverageError("producer receipt artifact SHA-256 does not match artifact_path")
    return {
        "producer_id": producer_id,
        "role": policy["role"],
        "build_path": build_path,
        "build_sha256": policy["build_sha256"],
        "source_manifest_path": source_manifest_path,
        "source_manifest_sha256": source_manifest_sha256,
        "artifact_path": artifact_path,
        "artifact_sha256": artifact_sha256,
        "target_ids": _require_identifier_list(
            receipt.get("target_ids"), "producer receipt.target_ids"
        ),
        "frame_id": _require_string(receipt.get("frame_id"), "producer receipt.frame_id"),
        "state_id": _require_string(receipt.get("state_id"), "producer receipt.state_id"),
    }


def _validate_manifest_producer_receipts(session, verified_producers, report):
    for binding in session["document"].get("producer_receipts", []):
        receipt_path = None
        try:
            binding = _require_dict(binding, "producer receipt binding")
            receipt_path = _resolve_path(
                session["manifest_path"].parent,
                binding.get("path"),
                "producer receipt binding.path",
            )
            expected_sha256 = _require_sha256(
                binding.get("sha256"), "producer receipt binding.sha256"
            )
            receipt, actual_sha256 = _read_bound_json(
                receipt_path, expected_sha256, "producer receipt"
            )
            producer_id = _require_string(
                receipt.get("producer_id"), "producer receipt.producer_id"
            )
            policy = verified_producers.get(producer_id)
            if policy is None:
                raise CoverageError("producer receipt has no verified catalog producer policy")
            parsed = _validate_producer_receipt(receipt, receipt_path, policy)
            session["producer_receipts"][(str(receipt_path), actual_sha256)] = parsed
        except CoverageError as error:
            report["rejected_producer_receipts"].append(
                {
                    "manifest_path": str(session["manifest_path"]),
                    "session_id": session["session_id"],
                    "receipt_path": None if receipt_path is None else str(receipt_path),
                    "reason": str(error),
                }
            )


def _parse_comparison_provenance(receipt, receipt_path):
    bindings = {}
    for side in ("reference", "candidate"):
        path_field = f"{side}_producer_receipt_path"
        sha256_field = f"{side}_producer_receipt_sha256"
        path_present = path_field in receipt
        sha256_present = sha256_field in receipt
        if path_present != sha256_present:
            raise CoverageError(
                f"comparison receipt {path_field} and {sha256_field} must appear together"
            )
        if path_present:
            bindings[side] = {
                "path": _resolve_path(
                    receipt_path.parent,
                    receipt[path_field],
                    f"comparison receipt.{path_field}",
                ),
                "sha256": _require_sha256(
                    receipt[sha256_field], f"comparison receipt.{sha256_field}"
                ),
            }
    if not bindings:
        return None
    if set(bindings) != {"reference", "candidate"}:
        raise CoverageError(
            "comparison receipt must bind both reference and candidate producer receipts"
        )
    return bindings


def _verify_comparison_receipt(record, catalog):
    observation = record["observation"]
    evidence = observation["evidence"]
    receipt_path = _resolve_path(
        record["observations_path"].parent,
        evidence["comparison_receipt_path"],
        "comparison receipt path",
    )
    raw_receipt = _read_file(receipt_path, "comparison receipt")
    if _sha256(raw_receipt) != evidence["comparison_receipt_sha256"]:
        raise CoverageError("comparison receipt SHA-256 does not match render evidence")
    try:
        receipt = _require_dict(
            json.loads(raw_receipt.decode("utf-8")), "comparison receipt"
        )
    except UnicodeDecodeError as error:
        raise CoverageError("comparison receipt is not UTF-8") from error
    except json.JSONDecodeError as error:
        raise CoverageError(f"comparison receipt is not valid JSON: {error.msg}") from error
    _require_schema(receipt, "comparison receipt")
    if receipt.get("kind") != "rgba_comparison":
        raise CoverageError("comparison receipt.kind must be 'rgba_comparison'")
    if receipt.get("reference_sha256") != catalog["reference_sha256"]:
        raise CoverageError("comparison receipt reference_sha256 does not match catalog")
    scope = receipt.get("scope")
    if scope not in ("backend", "native"):
        raise CoverageError("comparison receipt.scope must be backend or native")
    receipt_target_ids = _require_identifier_list(
        receipt.get("target_ids"), "comparison receipt.target_ids"
    )
    if set(receipt_target_ids) != set(observation["target_ids"]):
        raise CoverageError("comparison receipt target_ids do not match observation")
    if any(target_id not in catalog["by_id"] for target_id in receipt_target_ids):
        raise CoverageError("comparison receipt references an unknown catalog target")
    if receipt.get("frame_id") != evidence["frame_id"]:
        raise CoverageError("comparison receipt frame_id does not match render evidence")
    if receipt.get("state_id") != evidence["state_id"]:
        raise CoverageError("comparison receipt state_id does not match render evidence")
    width = receipt.get("width")
    height = receipt.get("height")
    expected = _expected_rgba_length(width, height, "comparison receipt")
    if width != evidence["width"] or height != evidence["height"]:
        raise CoverageError("comparison receipt dimensions do not match render evidence")
    if receipt.get("expected_byte_length") != expected:
        raise CoverageError("comparison receipt expected_byte_length is incorrect")
    reference_path = _resolve_path(
        receipt_path.parent, receipt.get("reference_path"), "comparison receipt reference_path"
    )
    candidate_path = _resolve_path(
        receipt_path.parent, receipt.get("candidate_path"), "comparison receipt candidate_path"
    )
    reference, reference_payload_sha256, reference_expected = _read_exact_rgba(
        reference_path, width, height, "comparison receipt reference image"
    )
    candidate, candidate_payload_sha256, candidate_expected = _read_exact_rgba(
        candidate_path, width, height, "comparison receipt candidate image"
    )
    if reference_expected != expected or candidate_expected != expected:
        raise CoverageError("comparison receipt image lengths do not match")
    if reference_payload_sha256 != _require_sha256(
        receipt.get("reference_payload_sha256"),
        "comparison receipt.reference_payload_sha256",
    ):
        raise CoverageError("comparison receipt reference payload SHA-256 is incorrect")
    if candidate_payload_sha256 != _require_sha256(
        receipt.get("candidate_payload_sha256"),
        "comparison receipt.candidate_payload_sha256",
    ):
        raise CoverageError("comparison receipt candidate payload SHA-256 is incorrect")
    reference_source_sha256 = _require_sha256(
        receipt.get("reference_source_sha256"),
        "comparison receipt.reference_source_sha256",
    )
    candidate_source_sha256 = _require_sha256(
        receipt.get("candidate_source_sha256"),
        "comparison receipt.candidate_source_sha256",
    )
    if not _same_file(candidate_path, record["render"]["path"]):
        raise CoverageError("comparison receipt candidate_path does not match render evidence")
    if candidate_payload_sha256 != record["render"]["payload_sha256"]:
        raise CoverageError("comparison receipt candidate payload differs from render evidence")
    differing_pixels, max_channel_error = _compare_payloads(reference, candidate)
    if _require_nonnegative_integer(
        receipt.get("differing_pixels"), "comparison receipt.differing_pixels"
    ) != differing_pixels:
        raise CoverageError("comparison receipt differing_pixels is incorrect")
    if _require_nonnegative_integer(
        receipt.get("max_channel_error"), "comparison receipt.max_channel_error"
    ) != max_channel_error:
        raise CoverageError("comparison receipt max_channel_error is incorrect")
    if _require_boolean(
        receipt.get("raw_bytes_equal"), "comparison receipt.raw_bytes_equal"
    ) != (differing_pixels == 0):
        raise CoverageError("comparison receipt raw_bytes_equal is incorrect")
    provenance = _parse_comparison_provenance(receipt, receipt_path)
    return {
        "path": receipt_path,
        "sha256": _sha256(raw_receipt),
        "scope": scope,
        "raw_bytes_equal": differing_pixels == 0,
        "reference_path": reference_path,
        "candidate_path": candidate_path,
        "reference_payload_sha256": reference_payload_sha256,
        "candidate_payload_sha256": candidate_payload_sha256,
        "reference_source_sha256": reference_source_sha256,
        "candidate_source_sha256": candidate_source_sha256,
        "provenance": provenance,
        "frame_id": evidence["frame_id"],
        "state_id": evidence["state_id"],
    }


def _validate_render_records(session, catalog, report):
    for record in session["records"]:
        observation = record["observation"]
        if observation["kind"] != "render_captured":
            continue
        try:
            record["render"] = _verify_render_evidence(record)
        except CoverageError as error:
            record["render_error"] = str(error)
            report["rejected_observations"].append(
                _issue(
                    session["manifest_path"],
                    str(error),
                    session["session_id"],
                    record["line"],
                    observation["target_ids"],
                )
            )
            continue
        evidence = observation["evidence"]
        if "comparison_receipt_path" not in evidence:
            continue
        try:
            record["comparison"] = _verify_comparison_receipt(record, catalog)
        except CoverageError as error:
            record["comparison_error"] = str(error)
            report["rejected_comparisons"].append(
                _issue(
                    session["manifest_path"],
                    str(error),
                    session["session_id"],
                    record["line"],
                    observation["target_ids"],
                )
            )


def _verify_comparison_provenance(record, session):
    comparison = record["comparison"]
    if comparison["provenance"] is None:
        return "open", "comparison receipt has no producer provenance contract"
    reference_binding = comparison["provenance"]["reference"]
    candidate_binding = comparison["provenance"]["candidate"]
    reference = session["producer_receipts"].get(
        (str(reference_binding["path"]), reference_binding["sha256"])
    )
    candidate = session["producer_receipts"].get(
        (str(candidate_binding["path"]), candidate_binding["sha256"])
    )
    if reference is None or candidate is None:
        raise CoverageError(
            "comparison producer receipts are not hash-bound by the session manifest"
        )
    if reference["role"] != "reference":
        raise CoverageError("comparison reference producer does not have reference role")
    if candidate["role"] != comparison["scope"]:
        raise CoverageError(
            "comparison candidate producer role does not match comparison scope"
        )
    if reference["producer_id"] == candidate["producer_id"]:
        raise CoverageError("comparison reference and candidate producers must be distinct")
    if reference["build_sha256"] == candidate["build_sha256"]:
        raise CoverageError("comparison reference and candidate producers share a trusted build")
    if _producer_identity(reference) == _producer_identity(candidate):
        raise CoverageError(
            "comparison reference and candidate producers share a trusted build and source identity"
        )
    if _same_file(comparison["reference_path"], comparison["candidate_path"]):
        raise CoverageError("comparison reference and candidate artifacts must be distinct")
    if not _same_file(reference["artifact_path"], comparison["reference_path"]):
        raise CoverageError(
            "reference producer artifact does not match comparison reference artifact"
        )
    if not _same_file(candidate["artifact_path"], comparison["candidate_path"]):
        raise CoverageError(
            "candidate producer artifact does not match comparison candidate artifact"
        )
    if reference["artifact_sha256"] != comparison["reference_payload_sha256"]:
        raise CoverageError("reference producer artifact SHA-256 does not match comparison")
    if candidate["artifact_sha256"] != comparison["candidate_payload_sha256"]:
        raise CoverageError("candidate producer artifact SHA-256 does not match comparison")
    observation = record["observation"]
    expected_target_ids = set(observation["target_ids"])
    for side, producer in (("reference", reference), ("candidate", candidate)):
        if set(producer["target_ids"]) != expected_target_ids:
            raise CoverageError(
                f"{side} producer target_ids do not match comparison observation"
            )
        if producer["frame_id"] != comparison["frame_id"]:
            raise CoverageError(
                f"{side} producer frame_id does not match comparison observation"
            )
        if producer["state_id"] != comparison["state_id"]:
            raise CoverageError(
                f"{side} producer state_id does not match comparison observation"
            )
    if reference["source_manifest_sha256"] != comparison["reference_source_sha256"]:
        raise CoverageError("reference producer source manifest does not match comparison")
    if candidate["source_manifest_sha256"] != comparison["candidate_source_sha256"]:
        raise CoverageError("candidate producer source manifest does not match comparison")
    return "satisfied", None


def _credit_observations(session, targets, report):
    for record in session["records"]:
        observation = record["observation"]
        comparison = record["comparison"]
        if not session["identity_verified"]:
            for target_id in observation["target_ids"]:
                target = targets[target_id]
                if observation["kind"] == "resource_seen":
                    _mark_gate(target, "resource_seen", "blocked")
                elif observation["kind"] == "state_observed":
                    _mark_gate(target, "state_observed", "blocked")
                else:
                    _mark_gate(target, "render_captured", "blocked")
                    if comparison is not None:
                        gate = (
                            "backend_pixels_match"
                            if comparison["scope"] == "backend"
                            else "native_pixels_match"
                        )
                        _mark_gate(target, gate, "blocked")
            continue
        comparison_outcome = None
        if observation["kind"] == "render_captured" and comparison is not None:
            if not session["complete"]:
                comparison_outcome = "blocked"
                record["provenance_status"] = "blocked"
            else:
                try:
                    comparison_outcome, provenance_error = _verify_comparison_provenance(
                        record, session
                    )
                    record["provenance_status"] = comparison_outcome
                    record["provenance_error"] = provenance_error
                except CoverageError as error:
                    comparison_outcome = "blocked"
                    record["provenance_status"] = "blocked"
                    record["provenance_error"] = str(error)
                    report["rejected_provenance"].append(
                        _issue(
                            session["manifest_path"],
                            str(error),
                            session["session_id"],
                            record["line"],
                            observation["target_ids"],
                        )
                    )
        for target_id in observation["target_ids"]:
            target = targets[target_id]
            if observation["kind"] == "resource_seen":
                _mark_gate(target, "resource_seen", "satisfied")
                continue
            if observation["kind"] == "state_observed":
                _mark_gate(target, "state_observed", "satisfied")
                continue
            if record["render_error"] is not None:
                _mark_gate(target, "render_captured", "blocked")
                continue
            _mark_gate(
                target,
                "render_captured",
                "satisfied" if session["complete"] else "blocked",
            )
            if comparison is None:
                if record["comparison_error"] is not None:
                    _mark_gate(target, "backend_pixels_match", "blocked")
                    _mark_gate(target, "native_pixels_match", "blocked")
                continue
            gate = (
                "backend_pixels_match"
                if comparison["scope"] == "backend"
                else "native_pixels_match"
            )
            if comparison_outcome != "satisfied":
                _mark_gate(target, gate, comparison_outcome)
            elif comparison["raw_bytes_equal"]:
                _mark_gate(target, gate, "satisfied")
            else:
                _mark_gate(target, gate, "failed")


def _normalize_sequence_frames(frames, label):
    frames = _require_list(frames, label)
    if not frames:
        raise CoverageError(f"{label} must not be empty")
    normalized = []
    for index, value in enumerate(frames):
        frame = _require_dict(value, f"{label}[{index}]")
        sequence = frame.get("sequence")
        if not _is_integer(sequence) or sequence < 0:
            raise CoverageError(f"{label}[{index}].sequence must be nonnegative")
        normalized.append(
            {
                "sequence": sequence,
                "frame_id": _require_string(
                    frame.get("frame_id"), f"{label}[{index}].frame_id"
                ),
                "state_id": _require_string(
                    frame.get("state_id"), f"{label}[{index}].state_id"
                ),
            }
        )
    sequences = [frame["sequence"] for frame in normalized]
    if sequences != list(range(sequences[0], sequences[0] + len(sequences))):
        raise CoverageError(f"{label} contains a sequence gap")
    return normalized


def _load_catalog_sequence_trace(catalog, target):
    expectation = target["sequence_expectation"]
    if expectation is None:
        return None
    trace_path = _resolve_path(
        catalog["path"].parent,
        expectation["reference_trace_path"],
        f"catalog target {target['id']} reference_trace_path",
    )
    trace, trace_sha256 = _read_bound_json(
        trace_path,
        expectation["reference_trace_sha256"],
        "catalog reference sequence trace",
    )
    _require_schema(trace, "catalog reference sequence trace")
    if trace.get("kind") != "sequence_reference_trace":
        raise CoverageError(
            "catalog reference sequence trace.kind must be 'sequence_reference_trace'"
        )
    if trace.get("reference_sha256") != catalog["reference_sha256"]:
        raise CoverageError("catalog reference sequence trace reference_sha256 does not match")
    if trace.get("target_id") != target["id"]:
        raise CoverageError("catalog reference sequence trace target_id does not match")
    return {
        "path": trace_path,
        "sha256": trace_sha256,
        "frames": _normalize_sequence_frames(
            trace.get("frames"), "catalog reference sequence trace.frames"
        ),
    }


def _mark_sequence_open(target, report, reason):
    _mark_gate(target, "sequence_match", "open")
    value = {"target_id": target["id"], "reason": reason}
    if value not in report["open_sequence_expectations"]:
        report["open_sequence_expectations"].append(value)


def _validate_sequence_receipt(session, binding, catalog, targets, report):
    manifest_path = session["manifest_path"]
    session_id = session["session_id"]
    target_id = None
    try:
        binding = _require_dict(binding, "sequence receipt binding")
        receipt_path = _resolve_path(
            manifest_path.parent,
            binding.get("path"),
            "sequence receipt binding.path",
        )
        expected_sha256 = _require_sha256(
            binding.get("sha256"), "sequence receipt binding.sha256"
        )
        receipt, _ = _read_bound_json(receipt_path, expected_sha256, "sequence receipt")
        _require_schema(receipt, "sequence receipt")
        if receipt.get("kind") != "sequence_receipt":
            raise CoverageError("sequence receipt.kind must be 'sequence_receipt'")
        if receipt.get("reference_sha256") != catalog["reference_sha256"]:
            raise CoverageError("sequence receipt reference_sha256 does not match catalog")
        if receipt.get("session_id") != session_id:
            raise CoverageError("sequence receipt session_id does not match manifest")
        target_id = _require_string(receipt.get("target_id"), "sequence receipt.target_id")
        if target_id not in targets:
            report["unmapped_sequence_receipts"].append(
                _issue(manifest_path, "sequence receipt target is unknown", session_id)
            )
            return
        target = catalog["by_id"][target_id]
        trace = _load_catalog_sequence_trace(catalog, target)
        if trace is None:
            _mark_sequence_open(
                targets[target_id],
                report,
                "catalog target has no authoritative reference sequence trace",
            )
            return
        if _require_sha256(
            receipt.get("reference_trace_sha256"),
            "sequence receipt.reference_trace_sha256",
        ) != trace["sha256"]:
            raise CoverageError("sequence receipt reference_trace_sha256 does not match catalog")
        frames = _normalize_sequence_frames(
            receipt.get("expected_frames"), "sequence receipt.expected_frames"
        )
        if frames != trace["frames"]:
            raise CoverageError(
                "sequence receipt expected_frames does not match catalog reference trace"
            )
        receipt_frames = _require_list(
            receipt.get("expected_frames"), "sequence receipt.expected_frames"
        )
        matching_records = []
        for record in session["records"]:
            observation = record["observation"]
            if (
                observation["kind"] != "render_captured"
                or target_id not in observation["target_ids"]
            ):
                continue
            evidence = observation["evidence"]
            matching_records.append(record)
        observed_frames = [
            {
                "sequence": record["observation"]["sequence"],
                "frame_id": record["observation"]["evidence"]["frame_id"],
                "state_id": record["observation"]["evidence"]["state_id"],
            }
            for record in matching_records
        ]
        if observed_frames != trace["frames"]:
            raise CoverageError(
                "target render observations do not exactly match the catalog reference trace"
            )
        for index, (frame, record) in enumerate(zip(frames, matching_records)):
            receipt_frame = _require_dict(
                receipt_frames[index], f"sequence receipt.expected_frames[{index}]"
            )
            receipt_path_for_frame = _resolve_path(
                receipt_path.parent,
                receipt_frame.get("comparison_receipt_path"),
                f"sequence receipt.expected_frames[{index}].comparison_receipt_path",
            )
            receipt_sha256_for_frame = _require_sha256(
                receipt_frame.get("comparison_receipt_sha256"),
                f"sequence receipt.expected_frames[{index}].comparison_receipt_sha256",
            )
            comparison = record["comparison"]
            if comparison is None:
                raise CoverageError("sequence receipt frame has no valid comparison receipt")
            if comparison["scope"] != "native":
                raise CoverageError("sequence receipt frame comparison is not native")
            if record["provenance_status"] == "open":
                _mark_sequence_open(
                    targets[target_id],
                    report,
                    "native sequence frame has no producer provenance contract",
                )
                return
            if record["provenance_status"] != "satisfied":
                raise CoverageError("sequence receipt frame has no verified native provenance")
            if not comparison["raw_bytes_equal"]:
                raise CoverageError("sequence receipt frame does not have equal raw RGBA8 bytes")
            if comparison["path"] != receipt_path_for_frame:
                raise CoverageError("sequence receipt frame comparison path does not match observation")
            if comparison["sha256"] != receipt_sha256_for_frame:
                raise CoverageError("sequence receipt frame comparison SHA-256 does not match observation")
        _mark_gate(
            targets[target_id],
            "sequence_match",
            "satisfied" if session["complete"] else "blocked",
        )
    except CoverageError as error:
        reason = str(error)
        report["rejected_sequence_receipts"].append(
            _issue(manifest_path, reason, session_id)
        )
        if target_id in targets:
            _mark_gate(targets[target_id], "sequence_match", "blocked")


def _category_gate_percentages(targets):
    result = {}
    for category in CATEGORIES:
        category_targets = [
            target for target in targets.values() if target["category"] == category
        ]
        gates = {}
        for gate in GATES:
            satisfied = sum(
                target["gates"][gate]["status"] == "satisfied"
                for target in category_targets
            )
            total = len(category_targets)
            gates[gate] = {
                "satisfied_targets": satisfied,
                "target_count": total,
                "percentage": None
                if total == 0
                else round(satisfied * 100.0 / total, 2),
            }
        result[category] = {"target_count": len(category_targets), "gates": gates}
    return result


def _finish_requirements(scope, targets, require_complete):
    undefined_targets = []
    unmet_targets = []
    for target in targets.values():
        unmet = [
            gate
            for gate in target["required_evidence"]
            if target["gates"][gate]["status"] != "satisfied"
        ]
        target["unmet_required_evidence"] = unmet
        if not target["acceptance_defined"]:
            undefined_targets.append(target["id"])
        elif unmet:
            unmet_targets.append({"id": target["id"], "missing_gates": unmet})
    catalog_requirements_satisfied = (
        bool(targets) and not undefined_targets and not unmet_targets
    )
    scope_unmet = []
    if not scope["full_unified_edition_scope_complete"]:
        scope_unmet.append("full_unified_edition_scope_complete is false")
    scope_unmet.extend(scope["open_dimensions"])
    return {
        "requested": require_complete,
        "catalog_requirements_satisfied": catalog_requirements_satisfied,
        "catalog_scope_complete": scope["complete"],
        "open_dimensions": scope["open_dimensions"],
        "scope_unmet": scope_unmet,
        "evidence_errors_present": False,
        "satisfied": catalog_requirements_satisfied and scope["complete"],
        "undefined_targets": undefined_targets,
        "unmet_targets": unmet_targets,
        "empty_catalog": not bool(targets),
    }


def _build_report(catalog, manifest_paths, require_complete):
    targets = {
        target["id"]: _new_target_state(target) for target in catalog["targets"]
    }
    report = {
        "schema_version": SCHEMA_VERSION,
        "reference_sha256": catalog["reference_sha256"],
        "catalog_sha256": catalog["sha256"],
        "catalog_scope": catalog["scope"],
        "sessions": [],
        "targets": [],
        "category_gate_percentages": {},
        "unmapped_observations": [],
        "rejected_observations": [],
        "observation_issue_overflow": {
            "unmapped_observations": 0,
            "rejected_observations": 0,
        },
        "rejected_comparisons": [],
        "rejected_producers": [],
        "rejected_producer_receipts": [],
        "rejected_provenance": [],
        "rejected_sequence_receipts": [],
        "unmapped_sequence_receipts": [],
        "open_sequence_expectations": [],
        "rejected_sessions": [],
        "require_complete": {},
    }
    for target_id, target in targets.items():
        if (
            "sequence_match" in target["required_evidence"]
            and catalog["by_id"][target_id]["sequence_expectation"] is None
        ):
            _mark_sequence_open(
                target,
                report,
                "catalog target has no authoritative reference sequence trace",
            )
    verified_producers = _validate_catalog_producers(catalog, report)
    sessions = []
    for manifest_path in manifest_paths:
        try:
            session = _load_manifest(manifest_path, catalog, report)
        except CoverageError as error:
            report["rejected_sessions"].append(
                _issue(Path(manifest_path).resolve(), str(error))
            )
            continue
        _finalize_session_readiness(session)
        _validate_manifest_producer_receipts(session, verified_producers, report)
        _validate_render_records(session, catalog, report)
        sessions.append(session)
        report["sessions"].append(
            {
                "manifest_path": str(session["manifest_path"]),
                "session_id": session["session_id"],
                "identity_verified": session["identity_verified"],
                "normalized_delivery_complete": session[
                    "normalized_delivery_complete"
                ],
                "normalized_delivery_reasons": session["reasons"],
                "capture_complete": session["capture_complete"],
                "capture_issues": session["capture_issues"],
                "fixture_complete": session["fixture_complete"],
                "complete": session["complete"],
                "reasons": session["reasons"],
                "observation_line_count": session["observation_line_count"],
                "credited_record_count": session["credited_record_count"],
                "credited_observation_bytes": session[
                    "credited_observation_bytes"
                ],
                "record_limit_exceeded": session["record_limit_exceeded"],
            }
        )
        _credit_observations(session, targets, report)
    for session in sessions:
        for binding in session["document"].get("sequence_receipts", []):
            _validate_sequence_receipt(session, binding, catalog, targets, report)
    requirements = _finish_requirements(catalog["scope"], targets, require_complete)
    report["targets"] = list(targets.values())
    report["category_gate_percentages"] = _category_gate_percentages(targets)
    report["require_complete"] = requirements
    has_data_errors = any(
        report[key]
        for key in (
            "unmapped_observations",
            "rejected_observations",
            "rejected_comparisons",
            "rejected_producers",
            "rejected_producer_receipts",
            "rejected_provenance",
            "rejected_sequence_receipts",
            "unmapped_sequence_receipts",
            "rejected_sessions",
        )
    )
    has_data_errors = has_data_errors or any(
        not session["normalized_delivery_complete"] for session in sessions
    )
    has_data_errors = has_data_errors or any(
        report["observation_issue_overflow"].values()
    )
    report["require_complete"]["evidence_errors_present"] = has_data_errors
    report["require_complete"]["satisfied"] = (
        report["require_complete"]["catalog_requirements_satisfied"]
        and report["require_complete"]["catalog_scope_complete"]
        and not has_data_errors
    )
    return report, has_data_errors


def cmd_inventory(args):
    catalog = _parse_catalog(Path(args.catalog))
    document = _inventory(catalog)
    _emit_json(args.output, document)
    return 0


def cmd_compare_rgba(args):
    document = _make_comparison_receipt(args)
    _write_json(args.output, document)
    return 0


def cmd_report(args):
    catalog = _parse_catalog(Path(args.catalog))
    document, has_data_errors = _build_report(
        catalog, args.manifests, args.require_complete
    )
    _emit_json(args.output, document)
    if has_data_errors:
        return 1
    if args.require_complete and not document["require_complete"]["satisfied"]:
        return 1
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Bounded runtime coverage and raw RGBA8 comparison tool"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    inventory = subparsers.add_parser("inventory", help="validate and list a catalog")
    inventory.add_argument("--catalog", required=True)
    inventory.add_argument("--output")
    inventory.set_defaults(func=cmd_inventory)

    compare = subparsers.add_parser(
        "compare-rgba", help="compare exact bounded raw RGBA8 payloads"
    )
    compare.add_argument("--reference", required=True)
    compare.add_argument("--candidate", required=True)
    compare.add_argument("--width", type=int, required=True)
    compare.add_argument("--height", type=int, required=True)
    compare.add_argument("--scope", choices=("backend", "native"), required=True)
    compare.add_argument("--reference-sha256", required=True)
    compare.add_argument("--reference-source-sha256", required=True)
    compare.add_argument("--candidate-source-sha256", required=True)
    compare.add_argument("--reference-producer-receipt")
    compare.add_argument("--candidate-producer-receipt")
    compare.add_argument("--target-id", dest="target_ids", action="append", required=True)
    compare.add_argument("--frame-id", required=True)
    compare.add_argument("--state-id", required=True)
    compare.add_argument("--output", required=True)
    compare.set_defaults(func=cmd_compare_rgba)

    report = subparsers.add_parser("report", help="report catalogued runtime evidence")
    report.add_argument("--catalog", required=True)
    report.add_argument("--manifest", dest="manifests", action="append", required=True)
    report.add_argument("--output")
    report.add_argument("--require-complete", action="store_true")
    report.set_defaults(func=cmd_report)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except CoverageError as error:
        print(f"runtime_coverage: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
