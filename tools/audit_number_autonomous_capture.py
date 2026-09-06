#!/usr/bin/env python3
"""Align the seeded autonomous Number DOS capture with native presentation."""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path

import audit_number_demo_first_board_continuous_sequence as alignment


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis/number-live/attract-continuation/original-number-autonomous-f585.avi"
STATE = ROOT / "analysis/number-live/attract-continuation/original-number-autonomous-f585.state.txt"
NATIVE = ROOT / "analysis/number-live/attract-continuation/native/number-autonomous-pages.tsv"
REPORT = ROOT / "analysis/number-live/attract-continuation/report.json"
EXPECTED_SHA256 = "A680FB7A38456F0143378D3BE171249F0D5C97DF8EAF93458A00CC882005C903"
EXPECTED_SIZE = 75_133_426
EXPECTED_FRAMES = 25_600


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def read_native() -> tuple[list[int], list[int], list[dict[str, str]]]:
    with NATIVE.open("r", encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    return (
        [int(row["hash"], 16) for row in rows],
        [int(row["sample"]) for row in rows],
        rows,
    )


def summarize_missing_source(
    runs: list[alignment.SourceRun], matched: set[int]
) -> list[dict[str, object]]:
    spans: list[dict[str, object]] = []
    for first, last in alignment.missing_groups(len(runs), matched):
        spans.append({
            "source_run_start": first,
            "source_run_end": last,
            "source_frame_start": runs[first].start,
            "source_frame_end": runs[last].end,
            "run_count": last - first + 1,
            "runs_with_uniform_frame": sum(
                runs[index].has_uniform_frame for index in range(first, last + 1)
            ),
            "nonuniform_2x_blocks": sum(
                runs[index].nonuniform_blocks for index in range(first, last + 1)
            ),
            "hashes": [
                f"0x{runs[index].fnv64:016x}" for index in range(first, last + 1)
            ],
        })
    return spans


def summarize_missing_native(
    hashes: list[int], starts: list[int], matched: set[int]
) -> list[dict[str, object]]:
    spans: list[dict[str, object]] = []
    for first, last in alignment.missing_groups(len(hashes), matched):
        spans.append({
            "native_page_start": first,
            "native_page_end": last,
            "native_sample_start": starts[first],
            "native_sample_end": starts[last],
            "page_count": last - first + 1,
            "hashes": [f"0x{hashes[index]:016x}" for index in range(first, last + 1)],
        })
    return spans


def main() -> int:
    missing = [str(path) for path in (CAPTURE, STATE, NATIVE) if not path.is_file()]
    if missing:
        raise FileNotFoundError(", ".join(missing))

    alignment.CAPTURE = CAPTURE
    alignment.FIRST_SOURCE_FRAME = 0
    alignment.LAST_SOURCE_FRAME = EXPECTED_FRAMES - 1
    runs, source_window = alignment.decode_source_runs()
    source_hashes = [run.fnv64 for run in runs]
    native_hashes, native_starts, native_rows = read_native()
    pairs = alignment.lcs_pairs(native_hashes, source_hashes)
    matched_native = {native for native, _ in pairs}
    matched_source = {source for _, source in pairs}
    blocks = []
    for group in alignment.contiguous_pairs(pairs):
        deltas = [runs[source].start - native_starts[native] for native, source in group]
        blocks.append({
            "native_page_start": group[0][0],
            "native_page_end": group[-1][0],
            "source_run_start": group[0][1],
            "source_run_end": group[-1][1],
            "source_frame_start": runs[group[0][1]].start,
            "source_frame_end": runs[group[-1][1]].end,
            "run_count": len(group),
            "source_minus_native_sample_min": min(deltas),
            "source_minus_native_sample_max": max(deltas),
        })

    source_sha = sha256_file(CAPTURE)
    source_only = summarize_missing_source(runs, matched_source)
    native_only = summarize_missing_native(native_hashes, native_starts, matched_native)
    report = {
        "schema": "number-autonomous-f585-alignment-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha,
        "source_size": CAPTURE.stat().st_size,
        "source_artifact_verified": (
            source_sha == EXPECTED_SHA256 and CAPTURE.stat().st_size == EXPECTED_SIZE and
            source_window["decoded_frames"] == EXPECTED_FRAMES
        ),
        "source_state": STATE.read_text(encoding="utf-8").splitlines(),
        "source_window": source_window,
        "native": NATIVE.relative_to(ROOT).as_posix(),
        "native_changed_pages": len(native_hashes),
        "native_first": native_rows[0],
        "native_last": native_rows[-1],
        "exact_lcs_run_count": len(pairs),
        "matched_native_pages": len(matched_native),
        "matched_source_runs": len(matched_source),
        "continuous_match_blocks": blocks,
        "longest_contiguous_match_run_count": max(
            (block["run_count"] for block in blocks), default=0
        ),
        "source_only_spans": source_only,
        "native_only_spans": native_only,
        "source_only_run_count": len(source_hashes) - len(matched_source),
        "native_only_page_count": len(native_hashes) - len(matched_native),
        "preliminary_valid": False,
        "reason": (
            "Alignment inventory only. Every unmatched page must be classified or fixed "
            "before this autonomous continuation can carry a parity claim."
        ),
    }
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "source_runs": len(source_hashes),
        "native_pages": len(native_hashes),
        "exact_lcs": len(pairs),
        "source_only": report["source_only_run_count"],
        "native_only": report["native_only_page_count"],
        "longest_block": report["longest_contiguous_match_run_count"],
        "source_only_spans": len(source_only),
        "native_only_spans": len(native_only),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
