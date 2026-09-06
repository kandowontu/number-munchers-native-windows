#!/usr/bin/env python3
"""Classify source-only refresh artifacts before the departing-Reggie overlap."""

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
    "full-demo-pre-departing-enemy-artifacts-report.json"
)
WINDOW_FIRST_FRAME = 3_607
WINDOW_LAST_FRAME = 3_885

# Each entry names the source-only logical sample and its immediately adjacent
# complete source pages. Partition counts cover all 64,000 logical pixels.
EXPECTED_ARTIFACTS = {
    3_616: {
        "run_index": 4,
        "nonuniform_2x_blocks": 28,
        "has_uniform_frame": False,
        "previous_hash": 0xA6C4EF0F11C40D88,
        "artifact_hash": 0x65C5FF7DD07313A7,
        "next_hash": 0x0735FC0A797BE176,
        "previous_only": 136,
        "next_only": 115,
        "common": 63_709,
        "neither": 40,
        "neither_bbox": [212, 73, 246, 75],
        "classification": "capture-only incomplete dirty repaint",
    },
    3_717: {
        "run_index": 21,
        "nonuniform_2x_blocks": 1,
        "has_uniform_frame": False,
        "previous_hash": 0xC809A5658B1E3737,
        "artifact_hash": 0xD8E1999101A16117,
        "next_hash": 0xB21D2F5DA1CA9639,
        "previous_only": 287,
        "next_only": 60,
        "common": 63_650,
        "neither": 3,
        "neither_bbox": [260, 80, 260, 82],
        "classification": "capture-only incomplete edge-column repaint",
    },
    3_729: {
        "run_index": 27,
        "nonuniform_2x_blocks": 0,
        "has_uniform_frame": True,
        "previous_hash": 0x85D0067CB06909D8,
        "artifact_hash": 0x4100C0798200D126,
        "next_hash": 0xF3E86DA512073934,
        "previous_only": 277,
        "next_only": 0,
        "common": 63_722,
        "neither": 1,
        "neither_bbox": [260, 85, 260, 85],
        "classification": "capture-only uniform incomplete dirty repaint",
    },
    3_796: {
        "run_index": 31,
        "nonuniform_2x_blocks": 19,
        "has_uniform_frame": False,
        "previous_hash": 0x6F7A7394886841E7,
        "artifact_hash": 0xCBF6684E91945D71,
        "next_hash": 0xEF3FF5FF0D3FA9D5,
        "previous_only": 483,
        "next_only": 0,
        "common": 63_506,
        "neither": 11,
        "neither_bbox": [68, 164, 68, 174],
        "classification": "capture-only incomplete edge-column repaint",
    },
    3_818: {
        "run_index": 39,
        "nonuniform_2x_blocks": 23,
        "has_uniform_frame": False,
        "previous_hash": 0x56588D1C3C07B155,
        "artifact_hash": 0xF427C24F7D7B91D1,
        "next_hash": 0x2B8A6091BBC16843,
        "previous_only": 391,
        "next_only": 355,
        "common": 63_254,
        "neither": 0,
        "neither_bbox": None,
        "classification": "capture-only adjacent-page scanout splice",
    },
    3_830: {
        "run_index": 45,
        "nonuniform_2x_blocks": 20,
        "has_uniform_frame": False,
        "previous_hash": 0x123DFD255115D1A3,
        "artifact_hash": 0x241DB89F35909DF1,
        "next_hash": 0xC84B45ED1FA8BA43,
        "previous_only": 606,
        "next_only": 114,
        "common": 63_269,
        "neither": 11,
        "neither_bbox": [212, 97, 212, 107],
        "classification": "capture-only incomplete dirty repaint",
    },
    3_871: {
        "run_index": 54,
        "nonuniform_2x_blocks": 10,
        "has_uniform_frame": False,
        "previous_hash": 0x46881471FF4615DF,
        "artifact_hash": 0xE8CCE1B228FDCDF5,
        "next_hash": 0x0994900450405F6D,
        "previous_only": 74,
        "next_only": 265,
        "common": 63_658,
        "neither": 3,
        "neither_bbox": [260, 67, 260, 69],
        "classification": "capture-only incomplete edge-column repaint",
    },
    3_883: {
        "run_index": 60,
        "nonuniform_2x_blocks": 4,
        "has_uniform_frame": False,
        "previous_hash": 0xD8FC36BB76410CF1,
        "artifact_hash": 0xA28C9DE6E1C72BED,
        "next_hash": 0xB555A565272C576E,
        "previous_only": 172,
        "next_only": 160,
        "common": 63_666,
        "neither": 2,
        "neither_bbox": [260, 72, 260, 73],
        "classification": "capture-only incomplete edge-column repaint",
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
    native_hashes, _, native_execution = boardwide.read_native_sequence()
    native_hash_set = set(native_hashes)

    artifacts: list[dict[str, object]] = []
    artifacts_match = True
    for frame, expected in EXPECTED_ARTIFACTS.items():
        index = int(expected["run_index"])
        run = runs[index]
        actual = {
            "frame": frame,
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "has_uniform_frame": run.has_uniform_frame,
            "previous_hash": common.renderer_fnv64(runs[index - 1].rgb),
            "artifact_hash": common.renderer_fnv64(run.rgb),
            "next_hash": common.renderer_fnv64(runs[index + 1].rgb),
            **logical_partition(
                runs[index - 1].rgb, run.rgb, runs[index + 1].rgb
            ),
            "classification": expected["classification"],
        }
        matched = all(actual[key] == value for key, value in expected.items())
        absent_native = int(actual["artifact_hash"]) not in native_hash_set
        actual["artifact_absent_from_native"] = absent_native
        actual["matched"] = matched and absent_native
        for key in ("previous_hash", "artifact_hash", "next_hash"):
            actual[key] = f"0x{int(actual[key]):016x}"
        artifacts.append(actual)
        artifacts_match &= matched and absent_native

    report = {
        "schema": "number-demo-pre-departing-enemy-artifacts-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 279,
            "nonuniform_2x_blocks": 105,
            "logical_run_count": 62,
        },
        "classified_source_only_pages": artifacts,
        "all_artifacts_match": artifacts_match,
        "native_replay": {
            **native_execution,
            "all_artifact_hashes_absent": all(
                item["artifact_absent_from_native"] for item in artifacts
            ),
        },
        "classification": {
            "new_native_pages_required": 0,
            "reason": (
                "all eight pages are measured partial dirty paints or "
                "adjacent-page scanout splices, not complete callback pages"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"],
        report["focused_decode_matches"],
        artifacts_match,
        native_execution["test_passed"],
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "logical_runs": len(runs),
        "classified_artifacts": len(artifacts),
        "native_gate_passed": native_execution["test_passed"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
