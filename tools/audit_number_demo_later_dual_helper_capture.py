#!/usr/bin/env python3
"""Audit the later concurrent Helper trail/exit interval in Number Demo."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-dual-helper-report.json"
)

WINDOW_FIRST_FRAME = 19_690
WINDOW_LAST_FRAME = 19_886
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 197,
    "nonuniform_2x_blocks": 35,
    "logical_run_count": 30,
}

# run, first, last, renderer FNV-64, player state, bottom Helper state,
# right Helper state, target-63 cleared
EXPECTED_COMPLETE_STATES = {
    "opening_both_helpers_dwell":
        (0, 19_690, 19_703, 0xA7E11BBF5589D93D, 0, 0, 0, False),
    "bottom_right_phase_1":
        (1, 19_704, 19_708, 0x45F6C73E4816D7EE, 0, 1, 0, False),
    "bottom_right_phase_2":
        (2, 19_709, 19_712, 0x92CBD0F8C33D42DB, 0, 2, 0, False),
    "bottom_right_phase_3":
        (4, 19_714, 19_717, 0xDAB0E582C72E7AE0, 0, 3, 0, False),
    "bottom_right_phase_4":
        (5, 19_718, 19_722, 0x9EDE04DF3E141AC1, 0, 4, 0, False),
    "bottom_right_phase_5":
        (6, 19_723, 19_727, 0x1BF2E2DCD9FC4EC4, 0, 5, 0, False),
    "bottom_right_phase_6":
        (7, 19_728, 19_730, 0x9EC8EB6B65A89811, 0, 6, 0, False),
    "player_left_phase_1_bottom_phase_6":
        (8, 19_731, 19_732, 0x395197778FE62868, 1, 6, 0, False),
    "player_left_phase_2_bottom_dwell":
        (9, 19_733, 19_734, 0xE85483197037ACFD, 2, 7, 0, False),
    "player_left_phase_3":
        (11, 19_736, 19_737, 0x416C0289C1F46CE0, 3, 7, 0, False),
    "player_left_phase_4":
        (12, 19_738, 19_739, 0xFBD47A9775A7CD01, 4, 7, 0, False),
    "player_left_phase_5":
        (13, 19_740, 19_742, 0x4D0D359DBDD1F15E, 5, 7, 0, False),
    "player_left_terminal":
        (14, 19_743, 19_744, 0xA7032F4B0697BDD8, 6, 7, 0, False),
    "player_idle_at_target":
        (15, 19_745, 19_807, 0xB896FFE2B812E1A5, 7, 7, 0, False),
    "chew_record_12":
        (16, 19_808, 19_809, 0x66E29E37D8962617, 12, 7, 0, True),
    "chew_record_13":
        (17, 19_810, 19_811, 0x3AD4BCD952E8E00B, 13, 7, 0, True),
    "chew_record_14":
        (18, 19_812, 19_814, 0x66E29E37D8962617, 14, 7, 0, True),
    "chew_record_13_right_down_phase_1":
        (19, 19_815, 19_816, 0x4EA920A9BAEBA5EC, 13, 7, 1, True),
    "chew_record_12_right_down_phase_1":
        (20, 19_817, 19_818, 0xD2522199B3AC4588, 12, 7, 1, True),
    "chew_record_13_right_down_phase_2":
        (22, 19_820, 19_821, 0xD33983E72897D019, 13, 7, 2, True),
    "chew_record_14_right_down_phase_2":
        (23, 19_822, 19_823, 0x1112CEB0AEF08D8D, 14, 7, 2, True),
    "chew_terminal_bottom_exit_1_right_down_3":
        (24, 19_824, 19_828, 0x2AC17CBCA9A231B7, 13, 8, 3, True),
    "bottom_exit_2_right_down_4":
        (25, 19_829, 19_833, 0x3A09873EC9F0D9A0, 13, 9, 4, True),
    "bottom_exit_3_right_down_5":
        (26, 19_834, 19_838, 0x864C153A430A0187, 13, 10, 5, True),
    "bottom_exit_4_right_dwell":
        (27, 19_839, 19_842, 0xD427E384EE41C0C5, 13, 11, 6, True),
    "bottom_exit_5_right_dwell":
        (28, 19_843, 19_847, 0x79EF2B6F138CC59B, 13, 12, 6, True),
    "terminal_bottom_removed_right_dwell":
        (29, 19_848, 19_886, 0xD9AB09F4F4958CB5, 13, 13, 6, True),
}

EXPECTED_TORN_RUNS = {
    "bottom_right_phase_2_to_3_torn":
        (3, 19_713, 19_713, 15, 0x855F6C7E2B0E20C9),
    "chew_right_down_transition_torn":
        (21, 19_819, 19_819, 20, 0xF89618956CDA6FBE),
}

EXPECTED_PARTIAL_PLAYER_PAINT = (
    10, 19_735, 19_735, 0x76F809D7CFB47D26
)
EXPECTED_PARTIAL_PARTITION = {
    "phase_2_to_phase_3_changed_pixels": 333,
    "partial_phase_2_pixels": 0,
    "partial_phase_3_pixels": 333,
    "partial_neither_pixels": 15,
    "partial_neither_bbox": [213, 87, 221, 93],
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


def partial_partition(runs: list[common.Run]) -> dict[str, object]:
    phase_2 = np.frombuffer(runs[9].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    partial = np.frombuffer(runs[10].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    phase_3 = np.frombuffer(runs[11].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    changed = np.any(phase_2 != phase_3, axis=2)
    same_phase_2 = np.all(partial == phase_2, axis=2)
    same_phase_3 = np.all(partial == phase_3, axis=2)
    neither = ~(same_phase_2 | same_phase_3)
    rows, columns = np.nonzero(neither)
    return {
        "phase_2_to_phase_3_changed_pixels": int(np.count_nonzero(changed)),
        "partial_phase_2_pixels": int(np.count_nonzero(
            same_phase_2 & changed
        )),
        "partial_phase_3_pixels": int(np.count_nonzero(
            same_phase_3 & changed
        )),
        "partial_neither_pixels": int(np.count_nonzero(neither)),
        "partial_neither_bbox": [
            int(columns.min()), int(rows.min()),
            int(columns.max()), int(rows.max()),
        ],
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
            "bottom_helper": expected[5],
            "right_helper": expected[6],
            "target_63_cleared": expected[7],
        }
        selected_states[name] = state
        selected_states_match &= matched

    partial, partial_matches = common.selected_state(
        runs, EXPECTED_PARTIAL_PLAYER_PAINT
    )
    partial["classification"] = (
        "uniform one-frame incomplete player dirty repaint: all 333 completed "
        "phase-3 transition pixels are present plus a 15-pixel strip that "
        "belongs to neither completed neighboring framebuffer"
    )
    partition = partial_partition(runs)
    partition_matches = partition == EXPECTED_PARTIAL_PARTITION

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
                "bottom Helper clears 8 and moves right from column 1 to 2",
                "Muncher moves left from row 2 column 4 to the target at column 3",
                "Muncher completes the seven-record chew and clears 63",
                "right Helper clears 12 and moves down from row 3 to row 4",
                "bottom Helper clears 30 and exits below row 4 column 2",
            ],
            "safe_cells": [[2, 1], [2, 4]],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_partial_player_paint": partial,
        "partial_player_paint_matches": partial_matches,
        "partial_player_paint_partition": partition,
        "partial_player_paint_partition_matches": partition_matches,
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
                "native matches every completed Helper/player page in source "
                "order and excludes both torn refreshes plus the 15-pixel "
                "incomplete player paint"
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
        partition_matches,
        torn_runs_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
