#!/usr/bin/env python3
"""Audit the first-board Muncher crossing a departing Reggie trail."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_first_board_continuous_sequence as boardwide


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-departing-reggie-player-overlap-report.json"
)
WINDOW_FIRST_FRAME = 3_884
WINDOW_LAST_FRAME = 3_941

# start, end, accumulated nonuniform blocks, has uniform frame, FNV-64
EXPECTED_RUNS = (
    (3884, 3885, 0, True, 0xB555A565272C576E),
    (3886, 3926, 0, True, 0xFBDB404C2920434F),
    (3927, 3928, 0, True, 0xF76F906D9E9B233E),
    (3929, 3930, 0, True, 0xC7BAC9D6D2402C5B),
    (3931, 3931, 16, False, 0x8D954DC8C9C8615C),
    (3932, 3933, 0, True, 0xB9FA6D42D3AF4DE3),
    (3934, 3935, 0, True, 0x206150B8F750C989),
    (3936, 3938, 0, True, 0xCC3707259E7C2F75),
    (3939, 3940, 0, True, 0x6F5E7EFAA3B03FD5),
    (3941, 3941, 0, True, 0xDD01E529CA1C4643),
)
EXPECTED_NATIVE_HASHES = (
    0xB555A565272C576E,
    0xFBDB404C2920434F,
    0xF76F906D9E9B233E,
    0xC7BAC9D6D2402C5B,
    0xB9FA6D42D3AF4DE3,
    0x206150B8F750C989,
    0xCC3707259E7C2F75,
    0x6F5E7EFAA3B03FD5,
    0xDD01E529CA1C4643,
)


def logical_partition(
    previous_rgb: bytes, partial_rgb: bytes, following_rgb: bytes
) -> dict[str, object]:
    shape = (common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    previous = np.frombuffer(previous_rgb, dtype=np.uint8).reshape(shape)
    partial = np.frombuffer(partial_rgb, dtype=np.uint8).reshape(shape)
    following = np.frombuffer(following_rgb, dtype=np.uint8).reshape(shape)
    previous_match = np.all(partial == previous, axis=2)
    following_match = np.all(partial == following, axis=2)
    neither = ~previous_match & ~following_match
    rows, columns = np.where(neither)
    return {
        "previous_only": int(np.count_nonzero(
            previous_match & ~following_match
        )),
        "next_only": int(np.count_nonzero(
            following_match & ~previous_match
        )),
        "common": int(np.count_nonzero(
            previous_match & following_match
        )),
        "neither": int(np.count_nonzero(neither)),
        "neither_bbox": (
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

    torn_partition = logical_partition(
        runs[3].rgb, runs[4].rgb, runs[5].rgb
    )
    torn_matches = torn_partition == {
        "previous_only": 589,
        "next_only": 116,
        "common": 63_184,
        "neither": 111,
        "neither_bbox": [235, 96, 260, 111],
    }

    native_hashes, native_starts, native_execution = (
        boardwide.read_native_sequence()
    )
    native_index = find_subsequence(native_hashes, EXPECTED_NATIVE_HASHES)
    native_match = native_index >= 0
    report = {
        "schema": "number-demo-departing-reggie-player-overlap-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 58,
            "nonuniform_2x_blocks": 16,
            "logical_run_count": 10,
        },
        "all_source_runs_match": runs_match,
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
        "frame_3931_incomplete_refresh": {
            **torn_partition,
            "matched": torn_matches,
            "classification": (
                "capture-only incomplete Muncher/Troggle dirty repaint"
            ),
        },
        "native_replay": {
            **native_execution,
            "exact_contiguous_subsequence_matched": native_match,
            "subsequence_run_start": native_index if native_match else None,
            "subsequence_run_end": (
                native_index + len(EXPECTED_NATIVE_HASHES) - 1
                if native_match else None
            ),
            "native_presentation_frame_start": (
                native_starts[native_index] if native_match else None
            ),
            "native_presentation_frame_end": (
                native_starts[
                    native_index + len(EXPECTED_NATIVE_HASHES) - 1
                ] if native_match else None
            ),
            "matched_renderer_fnv64": [
                f"0x{value:016x}" for value in EXPECTED_NATIVE_HASHES
            ] if native_match else [],
            "permanent_gate":
                "firstDemoDepartingEnemyOverlapHashes",
        },
        "behavior": {
            "player_move": [1, 4, 2, 4],
            "departing_reggie_move": [2, 4, 2, 5],
            "complete_callback_pages": 8,
            "classification": (
                "the Troggle clears only its previous/current 41x29 actor "
                "boxes, retaining the next Muncher paint in the older trail"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["focused_decode_matches"],
        runs_match,
        torn_matches,
        native_execution["test_passed"],
        native_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_complete_pages": sum(run.has_uniform_frame for run in runs),
        "native_exact_contiguous_pages":
            len(EXPECTED_NATIVE_HASHES) if native_match else 0,
        "native_gate_passed": native_execution["test_passed"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
