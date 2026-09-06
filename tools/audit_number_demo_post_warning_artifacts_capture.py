#!/usr/bin/env python3
"""Classify the three source-only pages after the first warning boundary.

The board-wide LCS initially stopped at source frame 3462.  This focused
window proves that frames 3462, 3474, and 3491 are incomplete scanout/dirty
repaints between complete neighboring pages, rather than native game states.
"""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-post-warning-artifacts-report.json"
)
WINDOW_FIRST_FRAME = 3_420
WINDOW_LAST_FRAME = 3_495

# start, end, accumulated nonuniform blocks, has uniform frame, FNV-64
EXPECTED_RUNS = (
    (3420, 3447, 0, True, 0xC5E137D2F2F9F94E),
    (3448, 3450, 0, True, 0xCF7DA6DC97DF80CB),
    (3451, 3452, 0, True, 0x7091B1353DF99977),
    (3453, 3454, 0, True, 0xCF7DA6DC97DF80CB),
    (3455, 3457, 0, True, 0x7091B1353DF99977),
    (3458, 3459, 0, True, 0xCF7DA6DC97DF80CB),
    (3460, 3461, 0, True, 0x7091B1353DF99977),
    (3462, 3462, 13, False, 0xA28159955F44371B),
    (3463, 3464, 0, True, 0xA886DA9D768010D9),
    (3465, 3466, 0, True, 0x505D7F9E6145E8B5),
    (3467, 3469, 0, True, 0x009C56D40700AC5B),
    (3470, 3471, 0, True, 0x48504F8999A7BC35),
    (3472, 3473, 0, True, 0x7876069A21E9E625),
    (3474, 3474, 24, False, 0x4CEC0220B933E3E3),
    (3475, 3476, 0, True, 0x14F89AA6421A0835),
    (3477, 3483, 0, True, 0xDB0F63C0319A50FB),
    (3484, 3486, 0, True, 0xE7BB12AF6E5EEED7),
    (3487, 3488, 0, True, 0x7FB83CAAD6C536E3),
    (3489, 3490, 0, True, 0xD038E477AB58D768),
    (3491, 3491, 0, True, 0x73E9E2D460B497B3),
    (3492, 3493, 0, True, 0x267729814EDBFF2F),
    (3494, 3495, 0, True, 0xF34FFD9250965616),
)

EXPECTED_ARTIFACTS = {
    "frame_3462_scanout_splice": {
        "run_index": 7,
        "previous_only": 346,
        "next_only": 400,
        "common": 63_254,
        "neither": 0,
        "neither_bbox": None,
        "classification": "capture-only adjacent-page scanout splice",
    },
    "frame_3474_incomplete_dirty_repaint": {
        "run_index": 13,
        "previous_only": 200,
        "next_only": 280,
        "common": 63_511,
        "neither": 9,
        "neither_bbox": [116, 91, 116, 99],
        "classification": "capture-only incomplete dirty repaint",
    },
    "frame_3491_incomplete_row_repaint": {
        "run_index": 19,
        "previous_only": 56,
        "next_only": 280,
        "common": 63_623,
        "neither": 41,
        "neither_bbox": [168, 56, 208, 56],
        "classification": "capture-only uniform incomplete row repaint",
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

    artifacts: dict[str, dict[str, object]] = {}
    artifacts_match = True
    for name, expected in EXPECTED_ARTIFACTS.items():
        index = int(expected["run_index"])
        actual = {
            "run_index": index,
            "first_frame": runs[index].start,
            "last_frame": runs[index].end,
            "nonuniform_2x_blocks": runs[index].nonuniform_blocks,
            "renderer_fnv64_from_top_left_samples":
                f"0x{common.renderer_fnv64(runs[index].rgb):016x}",
            **logical_partition(
                runs[index - 1].rgb,
                runs[index].rgb,
                runs[index + 1].rgb,
            ),
            "classification": expected["classification"],
        }
        matched = all(
            actual[key] == value
            for key, value in expected.items()
        )
        actual["matched"] = matched
        artifacts[name] = actual
        artifacts_match &= matched

    native = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent,
        capture_output=True, text=True, timeout=120,
    )
    report = {
        "schema": "number-demo-post-warning-artifacts-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 76,
            "nonuniform_2x_blocks": 37,
            "logical_run_count": 22,
        },
        "all_runs_match": runs_match,
        "classified_source_only_pages": artifacts,
        "all_artifacts_match": artifacts_match,
        "native_headless_gate": {
            "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
            "exit_code": native.returncode,
            "passed": native.returncode == 0,
        },
        "classification": {
            "new_native_pages_required": 0,
            "reason": (
                "all three formerly source-only pages split completed "
                "neighbor states or contain measured partial dirty pixels"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["focused_decode_matches"],
        runs_match,
        artifacts_match,
        native.returncode == 0,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "logical_runs": len(runs),
        "classified_artifacts": len(artifacts),
        "native_gate_passed": native.returncode == 0,
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
