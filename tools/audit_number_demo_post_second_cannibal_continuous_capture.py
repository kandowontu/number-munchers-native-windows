#!/usr/bin/env python3
"""Audit the continuous first-board interval after the second Cannibal."""

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
    "full-demo-post-second-cannibal-continuous-report.json"
)
WINDOW_FIRST_FRAME = 4_706
WINDOW_LAST_FRAME = 5_173

# start, end, accumulated nonuniform blocks, has uniform frame, FNV-64
EXPECTED_RUNS = (
    (4706, 4707, 0, True, 0x6068127ED60CAA9E),
    (4708, 4710, 0, True, 0x4B06F95FD290F2D6),
    (4711, 4712, 0, True, 0x6068127ED60CAA9E),
    (4713, 4715, 0, True, 0x4B06F95FD290F2D6),
    (4716, 4717, 0, True, 0x6068127ED60CAA9E),
    (4718, 4803, 0, True, 0x4B06F95FD290F2D6),
    (4804, 4806, 0, True, 0xE3A2ECF270A39A63),
    (4807, 4808, 0, True, 0x700A9EB63B7F9AC3),
    (4809, 4811, 0, True, 0x55C104A7965E8E46),
    (4812, 4813, 0, True, 0xA3CAB5651C5D758D),
    (4814, 4815, 0, True, 0x954312A9B8857423),
    (4816, 4818, 0, True, 0xA84B12122A1C711B),
    (4819, 4864, 0, True, 0xD341D2C43E2DF1FD),
    (4865, 4866, 0, True, 0x512AE94E92BACC53),
    (4867, 4869, 0, True, 0xE11AD48EB4E8A590),
    (4870, 4871, 0, True, 0x2B8D3F27EF095CD7),
    (4872, 4873, 0, True, 0x203BABE12EE81521),
    (4874, 4876, 0, True, 0xEA3A1CA245C9DFF7),
    (4877, 4878, 0, True, 0x50C033CFCD2D6A04),
    (4879, 4943, 0, True, 0xE084F4DFC9B38C21),
    (4944, 4945, 0, True, 0xA56D41419E7E7115),
    (4946, 4946, 2, False, 0x1F376745F8DC58A9),
    (4947, 4950, 0, True, 0xF6907E0BC7AD5B8B),
    (4951, 4953, 0, True, 0x41C53FAEFF24B63F),
    (4954, 4955, 0, True, 0x3F07B5FFD2C12C6F),
    (4956, 4957, 0, True, 0x4DAECAC897082870),
    (4958, 4958, 0, True, 0x2025189521121D8B),
    (4959, 4960, 0, True, 0x87FD439D18C59C6F),
    (4961, 4962, 0, True, 0xCDE78C4DF6BF8CF2),
    (4963, 4986, 0, True, 0x6DCD686CC87887FA),
    (4987, 4989, 0, True, 0xA514DA93F902A94D),
    (4990, 4991, 0, True, 0x1C32203D8651B312),
    (4992, 4994, 0, True, 0xB43C4A001B66A851),
    (4995, 4996, 0, True, 0x62BBC32DD2F0B75D),
    (4997, 4998, 0, True, 0x49D74DCB74AEE599),
    (4999, 5047, 0, True, 0xF6907E0BC7AD5B8B),
    (5048, 5049, 0, True, 0x6FD42D11FDC62815),
    (5050, 5051, 0, True, 0x97BA654BA5A6B086),
    (5052, 5054, 0, True, 0x7B5B1D7AF2E81727),
    (5055, 5056, 0, True, 0xEEC053901D6A935E),
    (5057, 5059, 0, True, 0xFEA658C2EFDAC9C1),
    (5060, 5061, 0, True, 0xE034EE4069D1C812),
    (5062, 5126, 0, True, 0xFC7E5A97228B2433),
    (5127, 5128, 0, True, 0xC8694767724B56BF),
    (5129, 5131, 0, True, 0xD328F52C66E3676F),
    (5132, 5133, 0, True, 0x5BBD8B44C9FB7E38),
    (5134, 5135, 0, True, 0x7C890527A99AB6F7),
    (5136, 5136, 0, True, 0xCEA90DA778DFC25F),
    (5137, 5138, 0, True, 0xDBC24A3454BAD8DD),
    (5139, 5165, 0, True, 0xD2A16F4D883BE485),
    (5166, 5167, 0, True, 0x2E623256E8419D36),
    (5168, 5170, 0, True, 0xEECA540C25C21A4E),
    (5171, 5171, 0, True, 0xAC62FB5F0BB81428),
    (5172, 5172, 16, False, 0x6E340FBD29D87A38),
    (5173, 5173, 0, True, 0x7327515AB704A216),
)

ARTIFACTS = {
    21: {
        "previous_only": 261,
        "next_only": 84,
        "common": 63_655,
        "neither": 0,
        "neither_bbox": None,
        "classification": "capture-only adjacent-page scanout splice",
    },
    26: {
        "previous_only": 98,
        "next_only": 238,
        "common": 63_623,
        "neither": 41,
        "neither_bbox": [72, 116, 112, 116],
        "classification": "capture-only uniform incomplete row repaint",
    },
    47: {
        "previous_only": 0,
        "next_only": 340,
        "common": 63_619,
        "neither": 41,
        "neither_bbox": [24, 116, 64, 116],
        "classification": "capture-only uniform incomplete row repaint",
    },
    53: {
        "previous_only": 390,
        "next_only": 0,
        "common": 63_592,
        "neither": 18,
        "neither_bbox": [37, 134, 49, 135],
        "classification": "capture-only incomplete dirty repaint",
    },
}

EXPECTED_NATIVE_HASHES = tuple(
    run[4] for index, run in enumerate(EXPECTED_RUNS)
    if index not in ARTIFACTS
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

    native_hashes, native_starts, native_execution = (
        boardwide.read_native_sequence()
    )
    native_hash_set = set(native_hashes)
    classified_artifacts = []
    artifacts_match = True
    for index, expected in ARTIFACTS.items():
        run = runs[index]
        actual = {
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "has_uniform_frame": run.has_uniform_frame,
            "renderer_fnv64":
                f"0x{common.renderer_fnv64(run.rgb):016x}",
            **logical_partition(
                runs[index - 1].rgb, run.rgb, runs[index + 1].rgb
            ),
            "classification": expected["classification"],
        }
        matched = all(
            actual[key] == value for key, value in expected.items()
        )
        absent_native = common.renderer_fnv64(run.rgb) not in native_hash_set
        actual["absent_from_native"] = absent_native
        actual["matched"] = matched and absent_native
        classified_artifacts.append(actual)
        artifacts_match &= matched and absent_native

    native_index = find_subsequence(native_hashes, EXPECTED_NATIVE_HASHES)
    native_match = native_index >= 0
    report = {
        "schema": "number-demo-post-second-cannibal-continuous-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches":
            source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "focused_decode": window,
        "focused_decode_matches": window == {
            "first_global_frame": WINDOW_FIRST_FRAME,
            "last_global_frame": WINDOW_LAST_FRAME,
            "decoded_frames": 468,
            "nonuniform_2x_blocks": 18,
            "logical_run_count": 55,
        },
        "all_source_runs_match": runs_match,
        "classified_capture_artifacts": classified_artifacts,
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
            "permanent_gate": "postSecondCannibalContinuousHashes",
        },
        "behavior": {
            "presentation_complete_pages": len(EXPECTED_NATIVE_HASHES),
            "new_focused_pages": len(EXPECTED_NATIVE_HASHES) - 2,
            "classification": (
                "second-Cannibal terminal, three complete Muncher moves, "
                "two Troggle moves, and the next bottom-entry boundary"
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
        "classified_artifacts": len(classified_artifacts),
        "native_gate_passed": native_execution["test_passed"],
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
