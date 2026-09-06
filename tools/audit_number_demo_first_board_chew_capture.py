#!/usr/bin/env python3
"""Audit the complete first-board Muncher chew in Number Demo."""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-board-chew-report.json"
)
WINDOW_FIRST_FRAME = 3977
WINDOW_LAST_FRAME = 4014
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 38,
    "nonuniform_2x_blocks": 0,
    "logical_run_count": 8,
}
EXPECTED_STATES = {
    "chew_tick_0_record_12": (0, 3977, 3979, 0x629739525E247327),
    "chew_tick_1_record_13": (1, 3980, 3981, 0x8FACC08AFF61688F),
    "chew_tick_2_record_14": (2, 3982, 3983, 0x629739525E247327),
    "chew_tick_3_record_13": (3, 3984, 3986, 0x8FACC08AFF61688F),
    "chew_tick_4_record_12": (4, 3987, 3988, 0x629739525E247327),
    "chew_tick_5_record_13": (5, 3989, 3991, 0x8FACC08AFF61688F),
    "chew_tick_6_record_14": (6, 3992, 3993, 0x629739525E247327),
    "terminal_record_13_hold": (7, 3994, 4014, 0x8FACC08AFF61688F),
}
EXPECTED_POSE_DIFFERENCE = {
    "changed_pixels": 240,
    "bbox": [221, 98, 246, 113],
}


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)

    source_matches = source_sha256 == common.EXPECTED_CAPTURE_SHA256
    probe_matches = probe == {
        "codec": "zmbv",
        "pixel_format": "bgr0",
        "width": 640,
        "height": 400,
        "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }
    audio_matches = audio == {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    window_matches = window == EXPECTED_WINDOW

    selected_states: dict[str, dict[str, object]] = {}
    selected_states_match = True
    for name, expected in EXPECTED_STATES.items():
        state, matched = common.selected_state(runs, expected)
        selected_states[name] = state
        selected_states_match &= matched

    pose_difference = common.difference_geometry(runs[0].rgb, runs[1].rgb)
    pose_difference_matches = pose_difference == EXPECTED_POSE_DIFFERENCE
    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_matches,
        "probe": probe,
        "probe_matches": probe_matches,
        "audio": audio,
        "audio_matches": audio_matches,
        "focused_decode": window,
        "focused_decode_matches": window_matches,
        "board": {
            "demo_board": 1,
            "level": 10,
            "mode": "Multiples",
            "target": 5,
            "event": "complete Muncher chew at row 2 column 4",
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "pose_difference": pose_difference,
        "pose_difference_matches": pose_difference_matches,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 8,
            "exact_stable_states_presented": list(EXPECTED_STATES),
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded first-Demo presenter matches the seven recovered "
                "12,13,14,13,12,13,14 chew intervals and the terminal record-13 hold"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        pose_difference_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
