#!/usr/bin/env python3
"""Audit the continuous first-Demo chew and early bottom-entry overlap."""

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
    "full-demo-early-bottom-entry-continuous-report.json"
)
WINDOW_FIRST_FRAME = 2_840
WINDOW_LAST_FRAME = 2_918
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 79,
    "nonuniform_2x_blocks": 23,
    "logical_run_count": 16,
}
EXPECTED_RUNS = (
    (0, 2_840, 2_846, 0x4F73AA52E893FF06),
    (1, 2_847, 2_848, 0x5A20108E52669299),
    (2, 2_849, 2_850, 0x5AB9F311800D5403),
    (3, 2_851, 2_851, 0x6BCA2ABC388AEF47),
    (4, 2_852, 2_853, 0x9C56E9B502682745),
    (5, 2_854, 2_855, 0x0C5717288217E3A2),
    (6, 2_856, 2_858, 0xD68C66339F73174B),
    (7, 2_859, 2_860, 0xB8270ABED0E3BAD1),
    (8, 2_861, 2_863, 0x396DCEE5CE2EB935),
    (9, 2_864, 2_865, 0xB8270ABED0E3BAD1),
    (10, 2_866, 2_867, 0x9B285BE3F8494708),
    (11, 2_868, 2_869, 0x4DCB586C49227EB7),
    (12, 2_870, 2_870, 0x0C58DB712458F653),
    (13, 2_871, 2_872, 0x596CA0BAAAB14F93),
    (14, 2_873, 2_874, 0x7D512488E348373C),
    (15, 2_875, 2_918, 0xB5E9980AA54703C6),
)
CAPTURE_ONLY_RUN_INDEXES = (3, 12)
EXPECTED_CAPTURE_ONLY_TRANSITIONS = {
    "run_3_single_block_player_repaint": {
        "run_index": 3,
        "previous_only": 326,
        "next_only": 14,
        "common": 63_658,
        "neither": 2,
        "neither_bbox": [32, 103, 32, 104],
        "nonuniform_2x_blocks": 1,
        "physical_pixels_differing_from_top_left_duplication": 2,
        "physical_difference_bbox": [48, 217, 49, 217],
    },
    "run_12_torn_bottom_entry_repaint": {
        "run_index": 12,
        "previous_only": 89,
        "next_only": 216,
        "common": 63_652,
        "neither": 43,
        "neither_bbox": [36, 159, 50, 163],
        "nonuniform_2x_blocks": 22,
        "physical_pixels_differing_from_top_left_duplication": 44,
        "physical_difference_bbox": [58, 327, 101, 327],
    },
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

    incomplete: dict[str, dict[str, object]] = {}
    incomplete_match = True
    for name, expected in EXPECTED_CAPTURE_ONLY_TRANSITIONS.items():
        state, matched = bridge_common.classify_incomplete_run(runs, expected)
        incomplete[name] = state
        incomplete_match &= matched

    native = subprocess.run(
        [str(NATIVE_TEST)], cwd=NATIVE_TEST.parent,
        capture_output=True, text=True, timeout=120,
    )
    complete_indexes = [
        index for index in range(len(EXPECTED_RUNS))
        if index not in CAPTURE_ONLY_RUN_INDEXES
    ]
    report = {
        "schema": "number-early-bottom-entry-continuous-v1",
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
        "capture_only_incomplete_dirty_repaints": incomplete,
        "capture_only_classification_matches": incomplete_match,
        "native_replay": {
            "executable": NATIVE_TEST.relative_to(ROOT).as_posix(),
            "exit_code": native.returncode,
            "headless_gate_passed": native.returncode == 0,
            "presentation_complete_source_run_count": len(complete_indexes),
            "matched_run_indexes": complete_indexes,
            "incremental_new_complete_state_count": 8,
            "overlap_with_isolated_bottom_entry_state_count": 6,
            "reason": (
                "the seeded continuous replay matches all fourteen complete "
                "pages in exact order while excluding the two measured "
                "incomplete refreshes"
            ),
        },
        "correction": {
            "selector_5_entry_lag_scope": (
                "predict an imminent Demo chew without advancing the live "
                "PRNG and reserve retained-entry lag for movement callbacks"
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
