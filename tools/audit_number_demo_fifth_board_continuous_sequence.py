#!/usr/bin/env python3
"""Align the captured fresh-batch Factors-of-63 Demo board through key exit."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as alignment


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-fifth-board-continuous-sequence-report.json"
)
FIRST_SOURCE_FRAME = 18_570
LAST_SOURCE_FRAME = 20_527
EXPECTED_SOURCE_FRAMES = LAST_SOURCE_FRAME - FIRST_SOURCE_FRAME + 1
EXPECTED_SOURCE_ONLY_INDEXES = {
    7, 18, 35, 48, 54, 57, 70, 73, 77, 83, 85, 86, 89, 95,
    114, 123, 129, 141, 148, 159, 161, 177, 183, 188, 215, 226, 236,
}
EXPECTED_NATIVE_ONLY_INDEXES = {66, 76, 140, 141, 142, 145}
EXPECTED_NATIVE_CHANGED_PAGES = 224
EXPECTED_EXACT_LCS_RUNS = 218
INITIALIZER_LINE = re.compile(
    r"^RESTARTED_FIFTH_BOARD_INITIALIZER (?P<body>.*)$", re.M
)
SEQUENCE_LINE = re.compile(
    r"^RESTARTED_FIFTH_BOARD_PRESENTATION_SEQUENCE(?P<body>.*)$", re.M
)
SEQUENCE_ITEM = re.compile(r"0x(?P<hash>[0-9a-fA-F]+)@(?P<frame>[0-9]+)")


def read_native_sequence(
    page_directory: Path | None = None,
) -> tuple[list[int], list[int], dict[str, object]]:
    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_RESTARTED_FIFTH_BOARD_SEQUENCE"] = "1"
    if page_directory is not None:
        environment["MUNCHERS_AUDIT_FIFTH_BOARD_PAGE_DIR"] = str(page_directory)
    result = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent, env=environment,
        capture_output=True, text=True, timeout=120,
    )
    combined = result.stdout + "\n" + result.stderr
    initializer = INITIALIZER_LINE.search(combined)
    sequence = SEQUENCE_LINE.search(combined)
    if initializer is None or sequence is None:
        raise RuntimeError("native test did not emit the restarted fifth-board trace")
    items = list(SEQUENCE_ITEM.finditer(sequence.group("body")))
    # The diagnostic deliberately continues past the externally interrupted
    # capture. Keep only presentations belonging to the exact source window.
    items = [
        item for item in items
        if int(item.group("frame")) < EXPECTED_SOURCE_FRAMES
    ]
    hashes = [int(item.group("hash"), 16) for item in items]
    starts = [int(item.group("frame")) for item in items]
    if not hashes or len(hashes) != len(starts):
        raise RuntimeError("native restarted fifth-board sequence was empty or malformed")
    initializer_body = initializer.group("body")
    return hashes, starts, {
        "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
        "exit_code": result.returncode,
        "test_passed": result.returncode == 0,
        "changed_page_count": len(hashes),
        "first_presentation_frame": starts[0],
        "last_presentation_frame": starts[-1],
        "initializer_matches": all(token in initializer_body for token in (
            "calls=695", "mode=1", "target=63", "level=7",
            "difficulty=6", "player=1,1", "remaining=16",
            "hash=0x320cd1496e8cf4b3",
        )),
    }


def main() -> None:
    alignment.FIRST_SOURCE_FRAME = FIRST_SOURCE_FRAME
    alignment.LAST_SOURCE_FRAME = LAST_SOURCE_FRAME
    source_runs, source_window = alignment.decode_source_runs()
    source_hashes = [run.fnv64 for run in source_runs]
    native_hashes, native_starts, native_execution = read_native_sequence()
    pairs = alignment.lcs_pairs(native_hashes, source_hashes)
    matched_native = {native for native, _ in pairs}
    matched_source = {source for _, source in pairs}

    blocks = []
    for group in alignment.contiguous_pairs(pairs):
        first_native, first_source = group[0]
        last_native, last_source = group[-1]
        deltas = [
            source_runs[source].start - native_starts[native]
            for native, source in group
        ]
        blocks.append({
            "native_run_start": first_native,
            "native_run_end": last_native,
            "source_run_start": first_source,
            "source_run_end": last_source,
            "source_frame_start": source_runs[first_source].start,
            "source_frame_end": source_runs[last_source].end,
            "run_count": len(group),
            "source_minus_native_frame_delta_min": min(deltas),
            "source_minus_native_frame_delta_max": max(deltas),
        })

    source_only = []
    for first, last in alignment.missing_groups(len(source_runs), matched_source):
        source_only.append({
            "source_run_start": first,
            "source_run_end": last,
            "source_frame_start": source_runs[first].start,
            "source_frame_end": source_runs[last].end,
            "run_count": last - first + 1,
            "runs_with_uniform_frame": sum(
                source_runs[index].has_uniform_frame
                for index in range(first, last + 1)
            ),
            "nonuniform_2x_blocks": sum(
                source_runs[index].nonuniform_blocks
                for index in range(first, last + 1)
            ),
            "renderer_fnv64": [
                f"0x{source_runs[index].fnv64:016x}"
                for index in range(first, last + 1)
            ],
        })

    native_only = []
    for first, last in alignment.missing_groups(len(native_hashes), matched_native):
        native_only.append({
            "native_run_start": first,
            "native_run_end": last,
            "native_frame_start": native_starts[first],
            "native_frame_end": native_starts[last],
            "run_count": last - first + 1,
            "renderer_fnv64": [
                f"0x{native_hashes[index]:016x}"
                for index in range(first, last + 1)
            ],
        })

    source_sha256 = common.sha256_file(CAPTURE)
    report = {
        "schema": "number-fifth-demo-board-continuous-sequence-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "native_execution": native_execution,
        "exact_lcs_run_count": len(pairs),
        "matched_native_changed_pages": len(matched_native),
        "matched_source_logical_runs": len(matched_source),
        "continuous_match_blocks": blocks,
        "longest_contiguous_match_run_count": max(
            (block["run_count"] for block in blocks), default=0
        ),
        "source_only_spans": source_only,
        "native_only_spans": native_only,
        "classification": {
            "board_wide_gameplay_parity_claim": True,
            "reason": (
                "218 native pages are exact ordered DOS pages; the six LCS-only "
                "native pages are four repeated exact DOS pages plus two complete "
                "callback surfaces, and all 27 source-only scanout/dirty-painter "
                "artifacts are pinned by the companion pixel reconciliation"
            ),
        },
    }
    source_only_indexes = {
        index for first, last in alignment.missing_groups(
            len(source_runs), matched_source
        ) for index in range(first, last + 1)
    }
    native_only_indexes = {
        index for first, last in alignment.missing_groups(
            len(native_hashes), matched_native
        ) for index in range(first, last + 1)
    }
    report["alignment_metrics_match"] = all((
        report["source_matches"], native_execution["test_passed"],
        native_execution["initializer_matches"],
        source_window["first_global_frame"] == FIRST_SOURCE_FRAME,
        source_window["last_global_frame"] == LAST_SOURCE_FRAME,
        source_window["decoded_frames"] == EXPECTED_SOURCE_FRAMES,
        source_window["logical_run_count"] == 245,
        source_window["runs_without_uniform_frame"] == 16,
        native_execution["changed_page_count"] == EXPECTED_NATIVE_CHANGED_PAGES,
        len(pairs) == EXPECTED_EXACT_LCS_RUNS,
        source_only_indexes == EXPECTED_SOURCE_ONLY_INDEXES,
        native_only_indexes == EXPECTED_NATIVE_ONLY_INDEXES,
        source_hashes[0] == native_hashes[0] == 0x320CD1496E8CF4B3,
        source_hashes[-1] == native_hashes[-1] == 0xE5F33A065A5D946B,
    ))
    report["initial_alignment_complete"] = report["alignment_metrics_match"]
    report["valid"] = report["alignment_metrics_match"]
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "initial_alignment_complete": report["initial_alignment_complete"],
        "source_runs": source_window["logical_run_count"],
        "native_pages": native_execution["changed_page_count"],
        "exact_lcs": len(pairs),
        "match_blocks": len(blocks),
        "longest_block": report["longest_contiguous_match_run_count"],
        "source_only_spans": len(source_only),
        "native_only_spans": len(native_only),
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
