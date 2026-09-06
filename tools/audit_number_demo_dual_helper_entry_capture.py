#!/usr/bin/env python3
"""Audit simultaneous left/right Helper entries over a Demo Muncher walk."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-dual-helper-entry-report.json"
)

WINDOW_FIRST_FRAME = 19_420
WINDOW_LAST_FRAME = 19_480
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 61,
    "nonuniform_2x_blocks": 33,
    "logical_run_count": 16,
}

# run, first, last, renderer FNV-64, player state, left Helper state,
# right Helper state, warning visible
EXPECTED_COMPLETE_STATES = {
    "pre_entry_warning":
        (0, 19_420, 19_424, 0x07F4832492A772E9, 0, 0, 0, True),
    "player_right_phase_1_warning":
        (1, 19_425, 19_426, 0x005A082303ED2BAB, 1, 0, 0, True),
    "player_right_phase_2_left_phase_3":
        (3, 19_428, 19_429, 0xF7AA52D35DBA34B3, 2, 3, 1, False),
    "player_right_phase_3_left_phase_3":
        (4, 19_430, 19_431, 0xD6A522F946BF2C47, 3, 3, 1, False),
    "player_right_phase_4_left_phase_4":
        (6, 19_433, 19_434, 0x6DCFA997077BA745, 4, 4, 1, False),
    "player_right_phase_5_left_phase_4_right_phase_2":
        (7, 19_435, 19_436, 0xB9DB92597082506E, 5, 4, 2, False),
    "player_terminal_left_phase_5_right_phase_2":
        (8, 19_437, 19_439, 0xB139B4E58FEB6C46, 6, 5, 2, False),
    "player_idle_left_phase_5_right_phase_3":
        (9, 19_440, 19_441, 0xD2D72C6CD483A55B, 7, 5, 3, False),
    "player_idle_left_phase_6_right_phase_3":
        (10, 19_442, 19_443, 0xEB29E4F2D7A21B9E, 7, 6, 3, False),
    "player_idle_left_phase_6_right_phase_4":
        (12, 19_445, 19_448, 0xDF7DB7A8972B3979, 7, 6, 4, False),
    "player_idle_left_phase_6_right_phase_5":
        (13, 19_449, 19_453, 0xE05B93092D8A6EBA, 7, 6, 5, False),
    "player_idle_both_phase_6":
        (14, 19_454, 19_458, 0x9BE17FEB975AC459, 7, 6, 6, False),
    "player_idle_both_helpers_dwell":
        (15, 19_459, 19_480, 0x8AA8B06F7EC4DA3B, 7, 7, 7, False),
}

EXPECTED_PARTIAL_WARNING_ERASE = (
    2, 19_427, 19_427, 0xC316FC4F4967A9BE
)
EXPECTED_COMPLETED_WARNING_SURFACE_HASH = 0x321EDDC11C6BCA6E
EXPECTED_WARNING_ERASE_GEOMETRY = {
    "cleared_pixels": 26,
    "bbox": [0, 131, 15, 136],
    "source_color": [0, 0, 121],
    "completed_color": [255, 255, 255],
    "reconstructed_completed_renderer_fnv64": (
        "0x321eddc11c6bca6e"
    ),
}

EXPECTED_TORN_RUNS = {
    "player_phase_3_to_4_torn_refresh":
        (5, 19_432, 19_432, 11, 0x2146AC51AA5ABE43),
    "right_phase_3_to_4_torn_refresh":
        (11, 19_444, 19_444, 22, 0x55090172155454A0),
}


def torn_state(
    runs: list[common.Run], expected: tuple[int, int, int, int, int]
) -> tuple[dict[str, object], bool]:
    index, first, last, nonuniform, expected_hash = expected
    run = runs[index]
    actual_hash = common.renderer_fnv64(run.rgb)
    matched = (
        run.start == first and run.end == last and
        run.nonuniform_blocks == nonuniform and
        not run.has_uniform_frame and actual_hash == expected_hash
    )
    return ({
        "run_index": index,
        "first_frame": run.start,
        "last_frame": run.end,
        "frames": run.end - run.start + 1,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_hash:016x}",
        "classification": (
            "capture-only nonuniform 2x DOSBox-X refresh; not a complete "
            "logical framebuffer"
        ),
        "matched": matched,
    }, matched)


def warning_erase_geometry(runs: list[common.Run]) -> dict[str, object]:
    prior = np.frombuffer(runs[1].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    partial = np.frombuffer(runs[2].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    warning = np.zeros((common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH), dtype=bool)
    warning[62:137, 0:16] = True
    cleared = warning & np.any(prior != partial, axis=2)
    rows, columns = np.nonzero(cleared)
    reconstructed = partial.copy()
    reconstructed[cleared] = prior[cleared]
    source_colors = np.unique(partial[cleared], axis=0)
    completed_colors = np.unique(prior[cleared], axis=0)
    return {
        "cleared_pixels": int(np.count_nonzero(cleared)),
        "bbox": [
            int(columns.min()), int(rows.min()),
            int(columns.max()), int(rows.max()),
        ],
        "source_color": source_colors[0].astype(int).tolist(),
        "completed_color": completed_colors[0].astype(int).tolist(),
        "reconstructed_completed_renderer_fnv64": (
            f"0x{common.renderer_fnv64(reconstructed.tobytes()):016x}"
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
    for name, expected in EXPECTED_COMPLETE_STATES.items():
        state, matched = common.selected_state(runs, expected[:4])
        state["logical_state"] = {
            "player": expected[4],
            "left_helper": expected[5],
            "right_helper": expected[6],
            "warning": expected[7],
        }
        selected_states[name] = state
        selected_states_match &= matched

    partial_warning, partial_warning_matches = common.selected_state(
        runs, EXPECTED_PARTIAL_WARNING_ERASE
    )
    partial_warning["classification"] = (
        "uniform one-frame incomplete warning-box erase: the retained-player "
        "and left-Helper surface is complete, but only the warning outline's "
        "bottom 26 pixels have been cleared"
    )
    geometry = warning_erase_geometry(runs)
    geometry_matches = geometry == EXPECTED_WARNING_ERASE_GEOMETRY
    reconstructed_native_matches = (
        int(geometry["reconstructed_completed_renderer_fnv64"], 16) ==
        EXPECTED_COMPLETED_WARNING_SURFACE_HASH
    )

    torn_runs: dict[str, dict[str, object]] = {}
    torn_runs_match = True
    for name, expected in EXPECTED_TORN_RUNS.items():
        state, matched = torn_state(runs, expected)
        torn_runs[name] = state
        torn_runs_match &= matched

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
                "route, so only directly decoded frames and actor behavior are used"
            ),
        },
        "board": {
            "mode": "Factors",
            "target": 63,
            "footer": "Demo",
            "events": [
                "Muncher moves right from row 1 column 4 to column 5",
                "Helper enters from the left at row 4 column 0",
                "Helper enters from the right at row 2 column 5",
            ],
            "safe_cells": [[2, 1], [2, 4]],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_partial_warning_erase": partial_warning,
        "partial_warning_erase_matches": partial_warning_matches,
        "partial_warning_erase_geometry": geometry,
        "partial_warning_erase_geometry_matches": geometry_matches,
        "reconstructed_completed_warning_surface_matches_native_gate": (
            reconstructed_native_matches
        ),
        "capture_only_torn_refreshes": torn_runs,
        "torn_refreshes_match_source": torn_runs_match,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "capture_only_partial_repaint_count": 1,
            "capture_only_torn_refresh_count": len(EXPECTED_TORN_RUNS),
            "headless_gate": "game_render_state_test",
            "reason": (
                "native matches every completed dual-entry/player page in "
                "source order and excludes both torn refreshes plus the "
                "26-pixel partial warning erase"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        partial_warning_matches,
        geometry_matches,
        reconstructed_native_matches,
        torn_runs_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
