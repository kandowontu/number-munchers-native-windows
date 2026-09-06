#!/usr/bin/env python3
"""Audit the later concurrent Bashful bottom-exit interval in Number Demo."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-bottom-exit-report.json"
)

WINDOW_FIRST_FRAME = 5_687
WINDOW_LAST_FRAME = 5_718
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 32,
    "nonuniform_2x_blocks": 26,
    "logical_run_count": 8,
}

# run, first, last, renderer FNV-64, player state, Bashful state
EXPECTED_COMPLETE_STATES = {
    "player_standing_bashful_left_dwell":
        (0, 5_687, 5_705, 0xE572978E3378B63C, 0, 0),
    "player_standing_bashful_bottom_exit_phase_1":
        (1, 5_706, 5_708, 0x889C0F21B7D634D5, 0, 1),
    "player_up_phase_1_bashful_bottom_exit_phase_2":
        (2, 5_709, 5_710, 0x8F14E90902D728C7, 1, 2),
    "player_up_phase_2_bashful_bottom_exit_phase_3":
        (4, 5_712, 5_713, 0xB55BE53E20024918, 2, 3),
    "player_up_phase_3_bashful_bottom_exit_phase_4":
        (5, 5_714, 5_715, 0xE4FEC8A6974CFE71, 3, 4),
    "player_up_phase_4_bashful_bottom_exit_phase_5":
        (6, 5_716, 5_717, 0x8ABE6082E3593B9F, 4, 5),
    "player_up_phase_5_bashful_removed":
        (7, 5_718, 5_718, 0xFE9AFA1A084C2395, 5, 6),
}

EXPECTED_TORN = (3, 5_711, 5_711, 26, 0x69C22AABB9255FA1)
EXPECTED_TORN_PARTITION = {
    "neighbor_changed_pixels": 518,
    "partial_previous_pixels": 75,
    "partial_next_pixels": 443,
    "partial_neither_pixels": 0,
    "exact_horizontal_splice_row": 90,
}


def partition(runs: list[common.Run]) -> dict[str, object]:
    shape = (common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    previous = np.frombuffer(runs[2].rgb, dtype=np.uint8).reshape(shape)
    partial = np.frombuffer(runs[3].rgb, dtype=np.uint8).reshape(shape)
    following = np.frombuffer(runs[4].rgb, dtype=np.uint8).reshape(shape)
    changed = np.any(previous != following, axis=2)
    same_previous = np.all(partial == previous, axis=2)
    same_next = np.all(partial == following, axis=2)
    neither = ~(same_previous | same_next)
    splice_rows = [
        row for row in range(common.LOGICAL_HEIGHT + 1)
        if np.array_equal(partial[:row], previous[:row]) and
        np.array_equal(partial[row:], following[row:])
    ]
    return {
        "neighbor_changed_pixels": int(np.count_nonzero(changed)),
        "partial_previous_pixels": int(np.count_nonzero(
            same_previous & changed
        )),
        "partial_next_pixels": int(np.count_nonzero(same_next & changed)),
        "partial_neither_pixels": int(np.count_nonzero(neither)),
        "exact_horizontal_splice_row": (
            splice_rows[0] if len(splice_rows) == 1 else None
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
            "player_up_state": expected[4],
            "bashful_bottom_exit_state": expected[5],
            "reggie": "row_1_column_2_down_dwell",
            "safe_zone": "row_2_column_1",
            "warning": True,
            "bashful_trail": "115_to_105" if expected[5] > 0 else "pending",
        }
        selected_states[name] = state
        selected_states_match &= matched

    index, first, last, nonuniform, expected_hash = EXPECTED_TORN
    torn = runs[index]
    torn_hash = common.renderer_fnv64(torn.rgb)
    torn_partition = partition(runs)
    torn_matches = (
        torn.start == first and torn.end == last and
        torn.nonuniform_blocks == nonuniform and
        not torn.has_uniform_frame and torn_hash == expected_hash and
        torn_partition == EXPECTED_TORN_PARTITION
    )

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
                "the directly decoded concurrent actor states close this "
                "source interval without reviving the withdrawn continuous "
                "Demo route"
            ),
        },
        "board": {
            "mode": "Multiples",
            "target": 5,
            "footer": "Demo",
            "events": [
                "Bashful dwells at row 4 column 2 after arriving from the right",
                "Bashful regenerates 115 as 105 and exits below the board",
                "Muncher starts upward from row 2 column 2",
                "Reggie dwells at row 1 column 2",
                "row 2 column 1 remains a safe zone",
                "a later actor slot keeps the Troggle warning visible",
            ],
            "labels_before_exit_row_major": [
                "21", "", "5", "", "133", "11",
                "", "", "", "", "", "",
                "", "", "", "", "", "134",
                "59", "211", "176", "14", "2", "230",
                "", "207", "115", "26", "125", "175",
            ],
            "exit_trail_cell": {
                "row": 4,
                "column": 2,
                "before": "115",
                "after": "105",
            },
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_scanout_splice": {
            "run_index": index,
            "first_frame": torn.start,
            "last_frame": torn.end,
            "frames": torn.end - torn.start + 1,
            "nonuniform_2x_blocks": torn.nonuniform_blocks,
            "renderer_fnv64": f"0x{torn_hash:016x}",
            "pixel_partition": torn_partition,
            "classification": (
                "capture-only scanout at logical row 90: every pixel above "
                "the boundary is the completed previous page and every "
                "pixel at or below it is the completed following page"
            ),
            "matched": torn_matches,
        },
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "capture_only_scanout_splice_count": 1,
            "headless_gate": "game_render_state_test",
            "reason": (
                "native matches all seven completed Bashful-exit/player/"
                "Reggie/safe-zone/warning pages and excludes the scanout splice"
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
