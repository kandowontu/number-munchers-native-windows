#!/usr/bin/env python3
"""Audit the first-board chew across two overlapping Troggle entries."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-chew-entry-boundary-continuous-report.json"
)
WINDOW_FIRST_FRAME = 3_520
WINDOW_LAST_FRAME = 3_606

# start, end, accumulated nonuniform blocks, has uniform frame, FNV-64
EXPECTED_RUNS = (
    (3520, 3543, 0, True, 0x3D3DB73670066F0A),
    (3544, 3545, 0, True, 0x8A48D1DD7CB50364),
    (3546, 3548, 0, True, 0x1683BB2C4202D4A3),
    (3549, 3550, 0, True, 0x9142307DC36A7501),
    (3551, 3551, 0, True, 0x383516F5CF1A8D6F),
    (3552, 3553, 0, True, 0x8E1C5070FC3883A3),
    (3554, 3555, 0, True, 0x383516F5CF1A8D6F),
    (3556, 3558, 0, True, 0x6E040C01569E47CD),
    (3559, 3560, 0, True, 0x9D3A5C2FA8506FD9),
    (3561, 3563, 0, True, 0xDB89BAAFF479C4FF),
    (3564, 3565, 0, True, 0x0D4CA7ED5432E65D),
    (3566, 3568, 1, True, 0xBEF955A089D70533),
    (3569, 3570, 0, True, 0x065E4DC3D00563A3),
    (3571, 3572, 0, True, 0x0F5E344E7A5C6143),
    (3573, 3606, 0, True, 0x3D13C3BDF171E891),
)
EXPECTED_HASHES = tuple(run[4] for run in EXPECTED_RUNS)


def physical_nonuniform_blocks(frame_number: int) -> dict[str, object]:
    selection = f"select=eq(n\\,{frame_number})"
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(CAPTURE), "-map", "0:v:0",
            "-vf", selection, "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) != common.CAPTURE_FRAME_BYTES:
        raise ValueError("focused physical decode did not return one frame")
    captured = np.frombuffer(result.stdout, dtype=np.uint8).reshape(
        common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3
    )
    logical = captured[0::2, 0::2]
    mismatches = (
        np.any(logical != captured[0::2, 1::2], axis=2) |
        np.any(logical != captured[1::2, 0::2], axis=2) |
        np.any(logical != captured[1::2, 1::2], axis=2)
    )
    rows, columns = np.where(mismatches)
    return {
        "frame": frame_number,
        "nonuniform_2x_blocks": int(np.count_nonzero(mismatches)),
        "logical_bbox": (
            [int(columns.min()), int(rows.min()),
             int(columns.max()), int(rows.max())]
            if len(rows) else None
        ),
    }


def find_subsequence(sequence: list[int], expected: tuple[int, ...]) -> int:
    for index in range(len(sequence) - len(expected) + 1):
        if tuple(sequence[index:index + len(expected)]) == expected:
            return index
    return -1


def main() -> None:
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)
    source_sha256 = common.sha256_file(CAPTURE)
    actual_runs = tuple(
        (run.start, run.end, run.nonuniform_blocks,
         run.has_uniform_frame, common.renderer_fnv64(run.rgb))
        for run in runs
    )
    runs_match = actual_runs == EXPECTED_RUNS

    physical_frames = [
        physical_nonuniform_blocks(frame) for frame in range(3566, 3569)
    ]
    physical_frames_match = physical_frames == [
        {"frame": 3566, "nonuniform_2x_blocks": 0, "logical_bbox": None},
        {"frame": 3567, "nonuniform_2x_blocks": 0, "logical_bbox": None},
        {"frame": 3568, "nonuniform_2x_blocks": 1,
         "logical_bbox": [308, 55, 308, 55]},
    ]

    native_hashes, native_starts, native_execution = (
        boardwide.read_native_sequence()
    )
    native_index = find_subsequence(native_hashes, EXPECTED_HASHES)
    native_match = native_index >= 0
    native_evidence = {
        **native_execution,
        "exact_contiguous_subsequence_matched": native_match,
        "subsequence_run_start": native_index if native_match else None,
        "subsequence_run_end": (
            native_index + len(EXPECTED_HASHES) - 1
            if native_match else None
        ),
        "native_presentation_frame_start": (
            native_starts[native_index] if native_match else None
        ),
        "native_presentation_frame_end": (
            native_starts[native_index + len(EXPECTED_HASHES) - 1]
            if native_match else None
        ),
        "matched_renderer_fnv64": [
            f"0x{value:016x}" for value in EXPECTED_HASHES
        ] if native_match else [],
        "permanent_gate": "firstBoardChewEntryBoundaryHashes",
    }

    report = {
        "schema": "number-demo-chew-entry-boundary-continuous-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 87,
            "nonuniform_2x_blocks": 1,
            "logical_run_count": 15,
        },
        "all_complete_page_runs_match": runs_match,
        "source_runs": [
            {
                "run_index": index,
                "first_frame": run.start,
                "last_frame": run.end,
                "nonuniform_2x_blocks": run.nonuniform_blocks,
                "has_uniform_frame": run.has_uniform_frame,
                "renderer_fnv64":
                    f"0x{common.renderer_fnv64(run.rgb):016x}",
            }
            for index, run in enumerate(runs)
        ],
        "mixed_physical_run_3566_3568": {
            "frames": physical_frames,
            "matched": physical_frames_match,
            "classification": (
                "presentation-complete: two uniform copies establish the "
                "page despite one torn physical frame"
            ),
        },
        "native_replay": native_evidence,
        "behavior": {
            "player_cell": [1, 3],
            "complete_source_pages": 15,
            "classification": (
                "pre-chew dwell, resident bottom-left entry terminal, seven "
                "chew callbacks, warning removal, and new right-edge entry"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["focused_decode_matches"],
        runs_match,
        physical_frames_match,
        native_execution["test_passed"],
        native_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_complete_pages": len(runs),
        "native_exact_contiguous_pages":
            len(EXPECTED_HASHES) if native_match else 0,
        "native_gate_passed": native_execution["test_passed"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
