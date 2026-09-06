#!/usr/bin/env python3
"""Audit the true opening interval of the first Number Munchers Demo board."""

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
    "full-demo-first-board-opening-continuous-report.json"
)
WINDOW_FIRST_FRAME = 2_390
WINDOW_LAST_FRAME = 2_544

# start, end, accumulated nonuniform blocks, has uniform frame, FNV-64
EXPECTED_RUNS = (
    (2390, 2456, 0, True, 0x8785F4AD483807BF),
    (2457, 2458, 0, True, 0xB785AC15F2CD658F),
    (2459, 2459, 20, False, 0x67439243B79475E8),
    (2460, 2461, 0, True, 0xFF7BC8DF6CCDB88B),
    (2462, 2463, 0, True, 0xF1600CD2200EAA14),
    (2464, 2466, 0, True, 0x6662F01075E622A3),
    (2467, 2468, 0, True, 0xBEE9A3D8E0784776),
    (2469, 2543, 0, True, 0x24E910F3A85FE37E),
    (2544, 2544, 0, True, 0xF1C25756D2406CEB),
)
EXPECTED_NATIVE_HASHES = tuple(
    run[4] for index, run in enumerate(EXPECTED_RUNS) if index != 2
)
EXPECTED_ARTIFACT_PARTITION = {
    "previous_only": 141,
    "next_only": 267,
    "common": 63_592,
    "neither": 0,
    "neither_bbox": None,
}


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

    artifact = runs[2]
    partition = logical_partition(runs[1].rgb, artifact.rgb, runs[3].rgb)
    partition_matches = partition == EXPECTED_ARTIFACT_PARTITION

    native_hashes, native_starts, native_execution = (
        boardwide.read_native_sequence()
    )
    native_index = find_subsequence(native_hashes, EXPECTED_NATIVE_HASHES)
    native_match = native_index >= 0
    artifact_hash = common.renderer_fnv64(artifact.rgb)
    artifact_absent = artifact_hash not in set(native_hashes)
    report = {
        "schema": "number-demo-first-board-opening-continuous-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 155,
            "nonuniform_2x_blocks": 20,
            "logical_run_count": 9,
        },
        "all_source_runs_match": runs_match,
        "frame_2459_scanout_splice": {
            "run_index": 2,
            "first_frame": artifact.start,
            "last_frame": artifact.end,
            "nonuniform_2x_blocks": artifact.nonuniform_blocks,
            "renderer_fnv64": f"0x{artifact_hash:016x}",
            **partition,
            "classification": "capture-only adjacent-page scanout splice",
            "absent_from_native": artifact_absent,
            "matched": partition_matches and artifact_absent,
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
            "permanent_gate": "firstBoardOpeningContinuousHashes",
        },
        "behavior": {
            "presentation_complete_pages": len(EXPECTED_NATIVE_HASHES),
            "new_focused_pages": len(EXPECTED_NATIVE_HASHES),
            "classification": (
                "initial board dwell and the first complete downward "
                "Muncher move"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["focused_decode_matches"],
        runs_match,
        partition_matches,
        artifact_absent,
        native_execution["test_passed"],
        native_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "source_complete_pages": len(EXPECTED_NATIVE_HASHES),
        "native_exact_contiguous_pages":
            len(EXPECTED_NATIVE_HASHES) if native_match else 0,
        "classified_artifacts": 1,
        "native_gate_passed": native_execution["test_passed"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
