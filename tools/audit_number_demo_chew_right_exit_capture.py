#!/usr/bin/env python3
"""Audit the first-Demo simultaneous Muncher chew and Reggie right exit."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-chew-right-exit-report.json"
)
WINDOW_FIRST_FRAME = 4090
WINDOW_LAST_FRAME = 4161
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 72,
    "nonuniform_2x_blocks": 1,
    "logical_run_count": 11,
}
EXPECTED_STATES = {
    "pre_action_dwell": (0, 4090, 4108, 0xD0A7034DB4C37224),
    "exit_phase_1_chew_record_12": (2, 4110, 4111, 0x1E7BB5780B03C6A7),
    "exit_phase_2_chew_record_13": (3, 4112, 4113, 0x2C32E321D97F24AB),
    "exit_phase_3_chew_record_14": (4, 4114, 4116, 0xC04CE8B24D4AE276),
    "exit_phase_4_chew_record_13": (5, 4117, 4118, 0x6AC19DD06E550ACD),
    "exit_phase_5_chew_record_12": (6, 4119, 4120, 0x91470BDD52EE50EF),
    "exit_phase_6_chew_record_13": (8, 4122, 4123, 0xEA530A5B4A61D313),
    "exit_removed_chew_record_14": (9, 4124, 4125, 0xE52D6002157A9FB5),
    "chew_terminal_record_13": (10, 4126, 4161, 0x693DD0C1EA6DD7C9),
}
EXPECTED_UNIFORM_PARTIAL = (1, 4109, 4109, 0x7D7D70B26A2BE0D7)
EXPECTED_PARTIAL_CLASSIFICATION = {
    "partial_vs_pre_action": {
        "changed_pixels": 362,
        "bbox": [173, 102, 308, 115],
    },
    "partial_vs_complete_phase_1": {
        "changed_pixels": 400,
        "bbox": [173, 87, 308, 106],
    },
    "pixels_matching_neither_pre_nor_complete": 0,
    "changed_from_pre_matching_complete": 362,
    "changed_from_complete_matching_pre": 400,
}
EXPECTED_NONUNIFORM = (7, 4121, 4121, 1, 0xC125D42A7084B8F5)


def partial_classification(runs: list[common.Run]) -> dict[str, object]:
    pre = np.frombuffer(runs[0].rgb, dtype=np.uint8).reshape(200, 320, 3)
    partial = np.frombuffer(runs[1].rgb, dtype=np.uint8).reshape(200, 320, 3)
    complete = np.frombuffer(runs[2].rgb, dtype=np.uint8).reshape(200, 320, 3)
    equals_pre = np.all(partial == pre, axis=2)
    equals_complete = np.all(partial == complete, axis=2)
    return {
        "partial_vs_pre_action": common.difference_geometry(
            runs[1].rgb, runs[0].rgb
        ),
        "partial_vs_complete_phase_1": common.difference_geometry(
            runs[1].rgb, runs[2].rgb
        ),
        "pixels_matching_neither_pre_nor_complete": int(
            np.count_nonzero(~(equals_pre | equals_complete))
        ),
        "changed_from_pre_matching_complete": int(
            np.count_nonzero((~equals_pre) & equals_complete)
        ),
        "changed_from_complete_matching_pre": int(
            np.count_nonzero((~equals_complete) & equals_pre)
        ),
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

    partial_index, partial_first, partial_last, partial_hash = EXPECTED_UNIFORM_PARTIAL
    partial_run = runs[partial_index]
    actual_partial_hash = common.renderer_fnv64(partial_run.rgb)
    classification = partial_classification(runs)
    partial_matches = (
        partial_run.start == partial_first and partial_run.end == partial_last and
        partial_run.nonuniform_blocks == 0 and partial_run.has_uniform_frame and
        actual_partial_hash == partial_hash and
        classification == EXPECTED_PARTIAL_CLASSIFICATION
    )
    partial_state = {
        "run_index": partial_index,
        "first_frame": partial_run.start,
        "last_frame": partial_run.end,
        "nonuniform_2x_blocks": partial_run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_partial_hash:016x}",
        "classification": (
            "uniform-2x partial dirty refresh: only lower strips of the final "
            "phase-1 chew/exit composite have replaced the pre-action pixels"
        ),
        "pixel_provenance": classification,
        "matched": partial_matches,
    }

    torn_index, torn_first, torn_last, torn_blocks, torn_hash = EXPECTED_NONUNIFORM
    torn_run = runs[torn_index]
    actual_torn_hash = common.renderer_fnv64(torn_run.rgb)
    torn_matches = (
        torn_run.start == torn_first and torn_run.end == torn_last and
        torn_run.nonuniform_blocks == torn_blocks and
        not torn_run.has_uniform_frame and actual_torn_hash == torn_hash
    )
    torn_state = {
        "run_index": torn_index,
        "first_frame": torn_run.start,
        "last_frame": torn_run.end,
        "nonuniform_2x_blocks": torn_run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_torn_hash:016x}",
        "matched": torn_matches,
    }

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
            "events": [
                "Muncher chews row 2 column 3 value 100",
                "Reggie regenerates row 2 column 5 from 232 to 3 and exits right",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_uniform_partial_refresh": partial_state,
        "excluded_nonuniform_refresh": torn_state,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 9,
            "exact_stable_states_presented": list(EXPECTED_STATES),
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "native matches the clean pre-action dwell and every complete "
                "simultaneous chew/right-exit state, while intentionally omitting "
                "the uniform partial and one nonuniform source refresh"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        partial_matches,
        torn_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
