#!/usr/bin/env python3
"""Audit the complete Helper bottom exit overlapping an upward Muncher move.

The later Factors-of-63 Demo board lies after the withdrawn divergent route,
so this is an isolated lossless visual/callback oracle rather than a continuous
replay claim.  All twelve uniform source framebuffers are exact native states.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-helper-bottom-exit-report.json"
)
WINDOW_FIRST_FRAME = 20_063
WINDOW_LAST_FRAME = 20_187
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 125,
    "nonuniform_2x_blocks": 0,
    "logical_run_count": 12,
}

# (run, first frame, last frame, renderer FNV-64,
#  Helper state: 0=dwell, 1..5=bottom exit, 6=removed,
#  player state: 0=standing, 1..4=up move, 5=terminal, 6=standing)
EXPECTED_NATIVE_STATES = {
    "helper_dwell_player_standing":
        (0, 20_063, 20_102, 0xE0BB6C5F518833DD, 0, 0),
    "helper_bottom_exit_phase_1_player_standing":
        (1, 20_103, 20_107, 0x1172D382B2104A14, 1, 0),
    "helper_bottom_exit_phase_2_player_standing":
        (2, 20_108, 20_112, 0x1E62E00601EA5C36, 2, 0),
    "helper_bottom_exit_phase_3_player_standing":
        (3, 20_113, 20_117, 0x61E333825DF371B4, 3, 0),
    "helper_bottom_exit_phase_4_player_standing":
        (4, 20_118, 20_119, 0xCFD92ACA64FF0BA5, 4, 0),
    "helper_bottom_exit_phase_4_player_up_phase_1":
        (5, 20_120, 20_121, 0x2FC4BA91EBF3DCFF, 4, 1),
    "helper_bottom_exit_phase_5_player_up_phase_1":
        (6, 20_122, 20_122, 0xD508C19E27D44259, 5, 1),
    "helper_bottom_exit_phase_5_player_up_phase_2":
        (7, 20_123, 20_124, 0x8C5B9B657A1B4716, 5, 2),
    "helper_bottom_exit_phase_5_player_up_phase_3":
        (8, 20_125, 20_126, 0x1D5F563C12383951, 5, 3),
    "helper_removed_player_up_phase_4":
        (9, 20_127, 20_129, 0xCE768FDCA57065C7, 6, 4),
    "helper_removed_player_up_terminal":
        (10, 20_130, 20_131, 0xCBE291FAE3BF9B93, 6, 5),
    "helper_removed_player_standing":
        (11, 20_132, 20_187, 0x13653C36323B3829, 6, 6),
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
    states_match = True
    for name, expected in EXPECTED_NATIVE_STATES.items():
        state, matched = common.selected_state(runs, expected[:4])
        state["logical_state"] = {
            "helper": expected[4],
            "player": expected[5],
        }
        selected_states[name] = state
        states_match &= matched

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
        "oracle_scope": {
            "classification": "isolated visual/callback evidence",
            "continuous_replay_claim": False,
            "reason": (
                "the Factors-of-63 board occurs after the withdrawn divergent "
                "route, so only directly decoded frame and actor behavior is used"
            ),
        },
        "board": {
            "mode": "Factors",
            "target": 63,
            "footer": "Demo",
            "events": [
                "Helper clears row 4 column 5 value 7 and exits downward",
                "Muncher moves upward from row 3 column 2 to row 2 column 2",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": states_match,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_NATIVE_STATES),
            "exact_complete_states_presented": list(EXPECTED_NATIVE_STATES),
            "pending_complete_state_count": 0,
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "native exactly matches the Helper dwell, all five clipped "
                "bottom-exit phases, destructive trail, terminal removal, and "
                "every overlapping upward player phase; Word gates the shared "
                "geometry and state tuples"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        states_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
