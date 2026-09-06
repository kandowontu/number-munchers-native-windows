#!/usr/bin/env python3
"""Audit the isolated dual-Helper movement window in Number Demo.

The later Factors-of-63 board is not used as a continuous replay claim: the
earlier recorded route has already diverged.  It is still an authoritative
lossless visual/callback oracle for the two simultaneous Helper jobs.  Thirteen
complete composites are reproduced by the native runtime: ten clean logical
frames and three resident-surface player callbacks.  The intervening source
run is proven to be a horizontal scanout splice and remains capture-only.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-dual-helper-report.json"
)
WINDOW_FIRST_FRAME = 19_530
WINDOW_LAST_FRAME = 19_590
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 61,
    "nonuniform_2x_blocks": 0,
    "logical_run_count": 15,
}

EXPECTED_PRECEDING_CONTEXT = (
    "preceding_dual_helper_dwell",
    (0, 19_530, 19_537, 0x42B56B2EBADA0599),
)

# (run, first frame, last frame, renderer FNV-64, player state,
#  bottom Helper state, right Helper state)
EXPECTED_NATIVE_STATES = {
    "right_phase_1_bottom_dwell":
        (1, 19_538, 19_542, 0xD4034528DA613FF8, 0, 0, 1),
    "right_phase_2_bottom_dwell":
        (2, 19_543, 19_547, 0x1FD266544DFF2075, 0, 0, 2),
    "right_phase_3_bottom_phase_1":
        (3, 19_548, 19_551, 0x8EDA1CEE458F0401, 0, 1, 3),
    "right_phase_3_bottom_phase_2":
        (4, 19_552, 19_552, 0x4831E8442EDB5D40, 0, 2, 3),
    "right_phase_4_bottom_phase_2":
        (5, 19_553, 19_554, 0xCEB422873CD4A11B, 0, 2, 4),
    "player_down_phase_1_player_callback":
        (6, 19_555, 19_556, 0xAE8A73951B9B4AA3, 1, 2, 4),
    "player_down_phase_2_player_callback":
        (8, 19_558, 19_559, 0xDC02C1BBF91D4BB7, 2, 3, 5),
    "player_down_phase_3_player_callback":
        (9, 19_560, 19_561, 0xEB6C6357AA1A5774, 3, 3, 5),
    "player_down_phase_4_both_helpers_advanced":
        (10, 19_562, 19_564, 0x48E8900A6E905A37, 4, 4, 6),
    "player_down_terminal_both_helpers_advanced":
        (11, 19_565, 19_566, 0x57C79D1ED939F0FF, 5, 4, 6),
    "player_dwell_bottom_phase_5":
        (12, 19_567, 19_571, 0xB40CE1BC09770F0A, 6, 5, 6),
    "player_dwell_bottom_phase_6":
        (13, 19_572, 19_575, 0x0B17DA5BDC7883D7, 6, 6, 6),
    "player_dwell_both_helpers_dwell":
        (14, 19_576, 19_590, 0x1378AD0077B72D09, 6, 7, 6),
}

EXPECTED_HORIZONTAL_SCANOUT_TEAR = (
    "player_phase_1_to_phase_2_horizontal_scanout_splice",
    # run, first frame, last frame, renderer FNV-64, top run, bottom run,
    # first logical row taken from the bottom run
    (7, 19_557, 19_557, 0xB5173A153ED46719, 6, 8, 81),
)


def exact_source_state(
    runs: list[common.Run], expected: tuple[int, int, int, int]
) -> tuple[dict[str, object], bool]:
    return common.selected_state(runs, expected)


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

    preceding_name, preceding_expected = EXPECTED_PRECEDING_CONTEXT
    preceding, preceding_matches = exact_source_state(runs, preceding_expected)

    selected_states: dict[str, dict[str, object]] = {}
    selected_states_match = True
    for name, expected in EXPECTED_NATIVE_STATES.items():
        state, matched = exact_source_state(runs, expected[:4])
        state["logical_state"] = {
            "player": expected[4],
            "bottom_helper": expected[5],
            "right_helper": expected[6],
        }
        state["native_composition"] = (
            "resident_surface_player_callback"
            if name.endswith("_player_callback")
            else "clean_logical_frame"
        )
        selected_states[name] = state
        selected_states_match &= matched

    tear_name, tear_expected = EXPECTED_HORIZONTAL_SCANOUT_TEAR
    tear_state, tear_source_matches = exact_source_state(runs, tear_expected[:4])
    tear_run, top_run, bottom_run, first_bottom_row = (
        tear_expected[0], tear_expected[4], tear_expected[5], tear_expected[6]
    )
    top = np.frombuffer(runs[top_run].rgb, dtype=np.uint8).reshape(200, 320, 3)
    bottom = np.frombuffer(runs[bottom_run].rgb, dtype=np.uint8).reshape(200, 320, 3)
    observed_tear = np.frombuffer(
        runs[tear_run].rgb, dtype=np.uint8
    ).reshape(200, 320, 3)
    expected_tear = np.concatenate(
        (top[:first_bottom_row], bottom[first_bottom_row:]), axis=0
    )
    tear_splice_matches = bool(np.array_equal(observed_tear, expected_tear))
    tear_matches = tear_source_matches and tear_splice_matches
    tear_state.update({
        "classification": (
            "capture-only horizontal scanout tear between two exact native "
            "framebuffers; not a third logical callback state"
        ),
        "top_source_run": top_run,
        "top_rows": [0, first_bottom_row - 1],
        "bottom_source_run": bottom_run,
        "bottom_rows": [first_bottom_row, 199],
        "splice_matches_pixel_exact": tear_splice_matches,
    })

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
                "bottom-left Helper moves right and clears row 4 column 0 value 3",
                "right-edge Helper moves down and clears row 2 column 5 value 3",
                "Muncher moves down from row 1 column 5 to row 2 column 5",
            ],
            "safe_cells": [[2, 1], [2, 4]],
        },
        "preceding_context": {
            preceding_name: preceding,
        },
        "preceding_context_matches": preceding_matches,
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_scanout_tears": {tear_name: tear_state},
        "scanout_tears_match_source": tear_matches,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": 13,
            "exact_complete_states_presented": list(EXPECTED_NATIVE_STATES),
            "pending_complete_state_count": 0,
            "pending_complete_states": [],
            "capture_only_scanout_tear_count": 1,
            "capture_only_scanout_tears": [tear_name],
            "preceding_context_state_count": 1,
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "native exactly matches thirteen complete dual-Helper/player states, "
                "including three resident-surface player callbacks, and Word gates "
                "the same tuples and compositor; the only intervening source run "
                "is a pixel-exact row-81 scanout splice of adjacent matched states"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        preceding_matches,
        selected_states_match,
        tear_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
