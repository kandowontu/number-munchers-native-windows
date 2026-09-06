#!/usr/bin/env python3
"""Audit the continuous first-board interval after the departing-Reggie move."""

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
    "full-demo-post-departure-continuous-report.json"
)
WINDOW_FIRST_FRAME = 3_941
WINDOW_LAST_FRAME = 4_090

# start, end, accumulated nonuniform blocks, has uniform frame, FNV-64
EXPECTED_RUNS = (
    (3941, 3976, 0, True, 0xDD01E529CA1C4643),
    (3977, 3979, 0, True, 0x629739525E247327),
    (3980, 3981, 0, True, 0x8FACC08AFF61688F),
    (3982, 3983, 0, True, 0x629739525E247327),
    (3984, 3986, 0, True, 0x8FACC08AFF61688F),
    (3987, 3988, 0, True, 0x629739525E247327),
    (3989, 3991, 0, True, 0x8FACC08AFF61688F),
    (3992, 3993, 0, True, 0x629739525E247327),
    (3994, 4014, 0, True, 0x8FACC08AFF61688F),
    (4015, 4017, 0, True, 0x3B336C4F9B082F55),
    (4018, 4019, 0, True, 0x49B6C343A28BBDC5),
    (4020, 4022, 0, True, 0xF9B4D332C091913B),
    (4023, 4024, 0, True, 0x180AC3BF15DF9BD7),
    (4025, 4026, 0, True, 0xD6A3242E658BA2F5),
    (4027, 4027, 24, False, 0x84270A444992793F),
    (4028, 4029, 0, True, 0x6DE23C5730908925),
    (4030, 4036, 0, True, 0xA3D6FED9D8CA19FF),
    (4037, 4039, 0, True, 0xD037B3E0A3204D15),
    (4040, 4041, 0, True, 0xD67042A6FC52F892),
    (4042, 4043, 0, True, 0x59EA5889FA908926),
    (4044, 4044, 1, False, 0xCE8D89953E9A30F7),
    (4045, 4046, 0, True, 0x3CE3BAB96FF221F9),
    (4047, 4048, 0, True, 0x58B1872B25BFD9CE),
    (4049, 4051, 0, True, 0x479EEF29297D97ED),
    (4052, 4089, 0, True, 0x66CC834DE63CB2E2),
    (4090, 4090, 0, True, 0xD0A7034DB4C37224),
)
EXPECTED_NATIVE_HASHES = tuple(
    run[4] for index, run in enumerate(EXPECTED_RUNS)
    if index not in (14, 20)
)
EXPECTED_ARTIFACT_PARTITIONS = {
    14: {
        "previous_only": 61,
        "next_only": 419,
        "common": 63_513,
        "neither": 7,
        "neither_bbox": [116, 147, 116, 153],
    },
    20: {
        "previous_only": 194,
        "next_only": 93,
        "common": 63_705,
        "neither": 8,
        "neither_bbox": [212, 107, 212, 114],
    },
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

    artifacts = []
    artifacts_match = True
    for index, expected in EXPECTED_ARTIFACT_PARTITIONS.items():
        partition = logical_partition(
            runs[index - 1].rgb, runs[index].rgb, runs[index + 1].rgb
        )
        matched = partition == expected
        artifacts.append({
            "frame": runs[index].start,
            "run_index": index,
            "nonuniform_2x_blocks": runs[index].nonuniform_blocks,
            "renderer_fnv64":
                f"0x{common.renderer_fnv64(runs[index].rgb):016x}",
            **partition,
            "matched": matched,
            "classification": "capture-only incomplete dirty repaint",
        })
        artifacts_match &= matched

    native_hashes, native_starts, native_execution = (
        boardwide.read_native_sequence()
    )
    native_index = find_subsequence(native_hashes, EXPECTED_NATIVE_HASHES)
    native_match = native_index >= 0
    report = {
        "schema": "number-demo-post-departure-continuous-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 150,
            "nonuniform_2x_blocks": 25,
            "logical_run_count": 26,
        },
        "all_source_runs_match": runs_match,
        "classified_incomplete_pages": artifacts,
        "all_artifacts_match": artifacts_match,
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
                "firstDemoPostDepartureContinuousHashes",
        },
        "behavior": {
            "presentation_complete_pages": len(EXPECTED_NATIVE_HASHES),
            "new_focused_pages": 14,
            "classification": (
                "standing boundary, exact seven-record chew, two complete "
                "Muncher moves, and the pre-chew/right-exit boundary"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["focused_decode_matches"],
        runs_match,
        artifacts_match,
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
        "classified_artifacts": len(artifacts),
        "native_gate_passed": native_execution["test_passed"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
