#!/usr/bin/env python3
"""Audit the first full-Demo rightward Muncher approach near collision."""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-player-right-approach-report.json"
)

WINDOW_FIRST_FRAME = 5_660
WINDOW_LAST_FRAME = 5_687
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 28,
    "nonuniform_2x_blocks": 0,
    "logical_run_count": 8,
}

# run, first, last, renderer FNV-64, player state
EXPECTED_COMPLETE_STATES = {
    "player_standing_row_2_column_1":
        (0, 5_660, 5_672, 0x257CA66A2AAC6E80, 0),
    "player_right_phase_1":
        (1, 5_673, 5_674, 0x448AA93616A62B0A, 1),
    "player_right_phase_2":
        (2, 5_675, 5_677, 0x7586D4DECF80D10D, 2),
    "player_right_phase_3":
        (3, 5_678, 5_679, 0x0529E61003FEE875, 3),
    "player_right_phase_4":
        (4, 5_680, 5_682, 0xCE02DC247CBCBBD0, 4),
    "player_right_phase_5":
        (5, 5_683, 5_684, 0x25D12539700E6796, 5),
    "player_right_endpoint_record_4":
        (6, 5_685, 5_686, 0x9B96DA287406F039, 6),
    "player_idle_row_2_column_2":
        (7, 5_687, 5_687, 0xE572978E3378B63C, 7),
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
    for name, expected in EXPECTED_COMPLETE_STATES.items():
        state, matched = common.selected_state(runs, expected[:4])
        state["logical_state"] = {
            "player_right_state": expected[4],
            "reggie": "row_1_column_2_down_dwell",
            "bashful": "row_4_column_2_left_dwell",
            "safe_zone": "row_2_column_1",
            "warning": True,
        }
        selected_states[name] = state
        selected_states_match &= matched

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
            "classification": "continuous first-board replay evidence",
            "continuous_replay_claim": True,
            "reason": (
                "the corrected deterministic native replay reaches this "
                "first-board approach organically and matches every source page"
            ),
        },
        "board": {
            "mode": "Multiples",
            "target": 5,
            "footer": "Demo",
            "events": [
                "Muncher stands on the safe zone at row 2 column 1",
                "Muncher moves right through all five interpolated positions",
                "Muncher presents the right-facing endpoint record at column 2",
                "Muncher returns to the idle record at row 2 column 2",
                "Reggie and Bashful dwell throughout while the warning remains",
            ],
            "labels_row_major": [
                "21", "", "5", "", "133", "11",
                "", "", "", "", "", "",
                "", "", "", "", "", "134",
                "59", "211", "176", "14", "2", "230",
                "", "207", "115", "26", "125", "175",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_incomplete_state_count": 0,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": (
                len(EXPECTED_COMPLETE_STATES) - 1
            ),
            "overlap": (
                "the terminal idle page is the opening page of the later "
                "Bashful bottom-exit audit"
            ),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "headless_gate": "game_render_state_test",
            "reason": (
                "native matches every complete rightward-approach page with "
                "no source refresh exclusions"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
