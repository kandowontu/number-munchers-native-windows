#!/usr/bin/env python3
"""Audit Bashful's second complete first-board trail in Number Demo."""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-second-bashful-trail-report.json"
)
WINDOW_FIRST_FRAME = 3740
WINDOW_LAST_FRAME = 3815
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 76,
    "nonuniform_2x_blocks": 19,
    "logical_run_count": 9,
}
EXPECTED_STATES = {
    "pre_trail_dwell": (0, 3740, 3793, 0xA6C7A8866BCAE5F5),
    "bashful_move_phase_1": (1, 3794, 3795, 0x6F7A7394886841E7),
    "bashful_move_phase_2": (3, 3797, 3798, 0xEF3FF5FF0D3FA9D5),
    "bashful_move_phase_3": (4, 3799, 3800, 0xAB71D999BF2856BB),
    "bashful_move_phase_4": (5, 3801, 3803, 0x088730934A538989),
    "bashful_move_phase_5": (6, 3804, 3805, 0x075359E422075A8D),
    "bashful_move_phase_6": (7, 3806, 3808, 0xB16B07BB61C205AD),
    "bashful_dwell": (8, 3809, 3815, 0x8D188D13D9079609),
}
EXPECTED_TORN = (2, 3796, 3796, 19, 0xCBF6684E91945D71)


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

    torn_index, torn_first, torn_last, torn_nonuniform, torn_hash = EXPECTED_TORN
    torn_run = runs[torn_index]
    actual_torn_hash = common.renderer_fnv64(torn_run.rgb)
    torn_matches = (
        torn_run.start == torn_first and torn_run.end == torn_last and
        torn_run.nonuniform_blocks == torn_nonuniform and
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
            "event": "Bashful regenerates bottom-left 150 to 220 and moves right",
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_torn_refresh": torn_state,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 8,
            "exact_stable_states_presented": list(EXPECTED_STATES),
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "the seeded first-Demo replay matches the pre-trail dwell, exact "
                "150-to-220 regeneration, all six rightward Bashful phases, and dwell"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        torn_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
