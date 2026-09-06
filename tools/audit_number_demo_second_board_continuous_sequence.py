#!/usr/bin/env python3
"""Align the complete captured second-Demo lifecycle with native pages.

The scope starts on the first stable Prime board page and ends on the first
stable Equals-30 board page.  It therefore covers gameplay, correct and wrong
chews, feedback, the board-to-Hall Wipe, Hall, Hall-to-logo Wipe, logo, and the
logo-to-next-board Wipe in one deterministic sequence.
"""

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
    "full-demo-second-board-continuous-sequence-report.json"
)
FIRST_SOURCE_FRAME = 8_353
LAST_SOURCE_FRAME = 11_187
EXPECTED_SOURCE_FRAMES = LAST_SOURCE_FRAME - FIRST_SOURCE_FRAME + 1
SEQUENCE_LINE = re.compile(r"^SECOND_BOARD_PRESENTATION_SEQUENCE(?P<body>.*)$", re.M)
SEQUENCE_ITEM = re.compile(r"0x(?P<hash>[0-9a-fA-F]+)@(?P<frame>[0-9]+)")


def read_native_sequence(
    page_directory: Path | None = None,
) -> tuple[list[int], list[int], dict[str, object]]:
    if not NATIVE_TEST.is_file():
        raise FileNotFoundError(NATIVE_TEST)
    environment = os.environ.copy()
    environment["MUNCHERS_AUDIT_SECOND_BOARD_SEQUENCE"] = "1"
    if page_directory is not None:
        environment["MUNCHERS_AUDIT_SECOND_BOARD_PAGE_DIR"] = str(page_directory)
    result = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent, env=environment,
        capture_output=True, text=True, timeout=120,
    )
    combined = result.stdout + "\n" + result.stderr
    match = SEQUENCE_LINE.search(combined)
    if match is None:
        raise RuntimeError("native test did not emit the second-board sequence")
    items = list(SEQUENCE_ITEM.finditer(match.group("body")))
    hashes = [int(item.group("hash"), 16) for item in items]
    starts = [int(item.group("frame")) for item in items]
    if not hashes or len(hashes) != len(starts):
        raise RuntimeError("native second-board sequence was empty or malformed")
    return hashes, starts, {
        "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
        "exit_code": result.returncode,
        "test_passed": result.returncode == 0,
        "changed_page_count": len(hashes),
        "first_presentation_frame": starts[0],
        "last_presentation_frame": starts[-1],
    }


def main() -> None:
    # Reuse the streaming decoder and alignment helpers with this script's
    # independently fixed source interval.
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
        "schema": "number-second-demo-board-continuous-sequence-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "source_window": source_window,
        "source_window_matches": source_window == {
            "first_global_frame": FIRST_SOURCE_FRAME,
            "last_global_frame": LAST_SOURCE_FRAME,
            "decoded_frames": EXPECTED_SOURCE_FRAMES,
            "logical_run_count": 53,
            "nonuniform_2x_blocks": 2_080,
            "runs_with_uniform_frame": 42,
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
            "board_wide_lifecycle_parity_claim": False,
            "reason": (
                "source-only and native-only spans still require pixel-level "
                "capture-artifact or implementation-divergence reconciliation"
            ),
        },
    }
    report["alignment_metrics_match"] = all((
        native_execution["changed_page_count"] == 51,
        len(pairs) == 40,
        len(blocks) == 11,
        report["longest_contiguous_match_run_count"] == 26,
        len(source_only) == 10,
        len(native_only) == 9,
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
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
