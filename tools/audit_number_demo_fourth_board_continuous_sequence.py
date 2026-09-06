#!/usr/bin/env python3
"""Align the captured restarted Multiples-of-15 Demo board through restore."""

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
    "full-demo-fourth-board-continuous-sequence-report.json"
)
FIRST_SOURCE_FRAME = 15_321
LAST_SOURCE_FRAME = 16_390
EXPECTED_SOURCE_FRAMES = LAST_SOURCE_FRAME - FIRST_SOURCE_FRAME + 1
SEQUENCE_LINE = re.compile(
    r"^RESTARTED_FOURTH_BOARD_PRESENTATION_SEQUENCE(?P<body>.*)$", re.M
)
SEQUENCE_ITEM = re.compile(r"0x(?P<hash>[0-9a-fA-F]+)@(?P<frame>[0-9]+)")
EXPECTED_SOURCE_ONLY_INDEXES = {
    1, 7, 14, 19, 36, 42, 44, 50, 58, 61, 63, 70, 86, 92, 98,
}
EXPECTED_NATIVE_CHANGED_PAGES = 86
EXPECTED_EXACT_LCS_RUNS = 86


def read_native_sequence(
    page_directory: Path | None = None,
) -> tuple[list[int], list[int], dict[str, object]]:
    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_RESTARTED_FOURTH_BOARD_SEQUENCE"] = "1"
    if page_directory is not None:
        environment["MUNCHERS_AUDIT_FOURTH_BOARD_PAGE_DIR"] = str(page_directory)
    result = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent, env=environment,
        capture_output=True, text=True, timeout=120,
    )
    combined = result.stdout + "\n" + result.stderr
    match = SEQUENCE_LINE.search(combined)
    if match is None:
        raise RuntimeError("native test did not emit the restarted fourth-board sequence")
    items = list(SEQUENCE_ITEM.finditer(match.group("body")))
    hashes = [int(item.group("hash"), 16) for item in items]
    starts = [int(item.group("frame")) for item in items]
    if not hashes or len(hashes) != len(starts):
        raise RuntimeError("native restarted fourth-board sequence was empty or malformed")
    return hashes, starts, {
        "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
        "exit_code": result.returncode,
        "test_passed": result.returncode == 0,
        "changed_page_count": len(hashes),
        "first_presentation_frame": starts[0],
        "last_presentation_frame": starts[-1],
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
        "schema": "number-fourth-demo-board-continuous-sequence-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "source_window_matches": source_window == {
            "first_global_frame": FIRST_SOURCE_FRAME,
            "last_global_frame": LAST_SOURCE_FRAME,
            "decoded_frames": EXPECTED_SOURCE_FRAMES,
            "logical_run_count": 101,
            "nonuniform_2x_blocks": 91,
            "runs_with_uniform_frame": 90,
            "runs_without_uniform_frame": 11,
        },
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
                "all 86 native changed pages occur in exact DOS order with no "
                "native-only page; the 15 source-only dirty/scanout artifacts "
                "are pinned by the companion pixel-reconciliation report"
            ),
        },
    }
    source_only_indexes = {
        index for first, last in alignment.missing_groups(
            len(source_runs), matched_source
        ) for index in range(first, last + 1)
    }
    report["alignment_metrics_match"] = all((
        native_execution["changed_page_count"] == EXPECTED_NATIVE_CHANGED_PAGES,
        len(pairs) == EXPECTED_EXACT_LCS_RUNS,
        len(matched_native) == len(native_hashes),
        len(matched_source) == EXPECTED_EXACT_LCS_RUNS,
        not native_only,
        source_only_indexes == EXPECTED_SOURCE_ONLY_INDEXES,
        source_hashes[0] == native_hashes[0] == 0x373EB5DB7AB6534B,
        source_hashes[-1] == native_hashes[-1] == 0x7A5DB4784587A379,
    ))
    report["valid"] = all((
        report["source_matches"], report["source_window_matches"],
        native_execution["test_passed"], report["alignment_metrics_match"],
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_runs": source_window["logical_run_count"],
        "native_pages": native_execution["changed_page_count"],
        "exact_lcs": len(pairs),
        "match_blocks": len(blocks),
        "longest_block": report["longest_contiguous_match_run_count"],
        "source_only_spans": len(source_only),
        "native_only_spans": len(native_only),
        "source_terminal_hash": f"0x{source_hashes[-1]:016x}",
        "native_terminal_hash": f"0x{native_hashes[-1]:016x}",
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
