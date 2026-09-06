#!/usr/bin/env python3
"""Audit the continuous player/entry route into the first cannibal event."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_player_reggie_safe_bridge_capture as bridge_common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
NATIVE_TEST = ROOT / "build" / "NumberMunchersRenderTests.exe"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-cannibal-approach-continuous-report.json"
)
WINDOW_FIRST_FRAME = 2_919
WINDOW_LAST_FRAME = 2_944
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 26,
    "nonuniform_2x_blocks": 17,
    "logical_run_count": 11,
}
EXPECTED_RUNS = (
    (0, 2_919, 2_920, 0xCD4B6197AAFF42D8),
    (1, 2_921, 2_923, 0xBB5F1D6DBFF1882F),
    (2, 2_924, 2_925, 0xAE867FE9848DD2AB),
    (3, 2_926, 2_928, 0x27348E311A6A7C8E),
    (4, 2_929, 2_930, 0xE44C66F6F7F5C1BD),
    (5, 2_931, 2_932, 0xA1CDFA908F89E64F),
    (6, 2_933, 2_935, 0xCAE38FCDD3CC6CBD),
    (7, 2_936, 2_937, 0x9D81A6334D541956),
    (8, 2_938, 2_939, 0xF4814097833D3B3B),
    (9, 2_940, 2_940, 0x1FAC963566116D67),
    (10, 2_941, 2_944, 0x378E270F4598AC61),
)
CAPTURE_ONLY_RUN_INDEXES = (9,)
EXPECTED_CAPTURE_ONLY_TRANSITION = {
    "run_index": 9,
    "previous_only": 369,
    "next_only": 111,
    "common": 63_520,
    "neither": 0,
    "neither_bbox": None,
    "nonuniform_2x_blocks": 17,
    "physical_pixels_differing_from_top_left_duplication": 34,
    "physical_difference_bbox": [48, 215, 99, 215],
}


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)

    expected_probe = {
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640,
        "height": 400, "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777, "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000, "audio_channels": 2,
    }
    expected_audio = {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    source_runs: list[dict[str, object]] = []
    runs_match = len(runs) == len(EXPECTED_RUNS)
    for expected in EXPECTED_RUNS:
        state, matched = common.selected_state(runs, expected)
        if expected[0] in CAPTURE_ONLY_RUN_INDEXES:
            run = runs[expected[0]]
            matched = (
                run.start == expected[1] and run.end == expected[2] and
                common.renderer_fnv64(run.rgb) == expected[3]
            )
            state["matched"] = matched
        state["presentation_classification"] = (
            "capture_only_incomplete_dirty_repaint"
            if expected[0] in CAPTURE_ONLY_RUN_INDEXES
            else "presentation_complete"
        )
        source_runs.append(state)
        runs_match &= matched

    incomplete, incomplete_match = bridge_common.classify_incomplete_run(
        runs, EXPECTED_CAPTURE_ONLY_TRANSITION
    )
    native = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent,
        capture_output=True, text=True, timeout=120,
    )
    complete_indexes = [
        index for index in range(len(EXPECTED_RUNS))
        if index not in CAPTURE_ONLY_RUN_INDEXES
    ]
    report = {
        "schema": "number-first-cannibal-approach-continuous-v1",
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "probe": probe,
        "probe_matches": probe == expected_probe,
        "audio": audio,
        "audio_matches": audio == expected_audio,
        "focused_decode": window,
        "focused_decode_matches": window == EXPECTED_WINDOW,
        "source_runs": source_runs,
        "source_runs_match": runs_match,
        "capture_only_incomplete_dirty_repaint": incomplete,
        "capture_only_classification_matches": incomplete_match,
        "native_replay": {
            "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
            "exit_code": native.returncode,
            "headless_gate_passed": native.returncode == 0,
            "presentation_complete_source_run_count": len(complete_indexes),
            "matched_run_indexes": complete_indexes,
            "incremental_new_complete_state_count": 9,
            "overlap_with_first_cannibal_state_count": 1,
            "reason": (
                "the seeded continuous replay matches all ten complete pages "
                "in exact order while excluding the measured torn refresh"
            ),
        },
        "correction": {
            "player_before_entry_resident_surface": (
                "the earlier player job presents its two dirty cells while "
                "the later entrant advances only the resident working page; "
                "the final player callback then releases that working page"
            ),
        },
    }
    report["valid"] = all((
        report["source_matches"], report["probe_matches"],
        report["audio_matches"], report["focused_decode_matches"],
        runs_match, incomplete_match, native.returncode == 0,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "valid": report["valid"],
        "complete_source_runs": len(complete_indexes),
        "capture_only_runs": len(CAPTURE_ONLY_RUN_INDEXES),
        "native_exit_code": native.returncode,
        "report": REPORT.relative_to(ROOT).as_posix(),
    }, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
