#!/usr/bin/env python3
"""Audit the first-Demo Reggie-down and Muncher safe-entry bridge."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-reggie-down-safe-entry-report.json"
)

WINDOW_FIRST_FRAME = 5_446
WINDOW_LAST_FRAME = 5_659
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 214,
    "nonuniform_2x_blocks": 1,
    "logical_run_count": 16,
}

# name: run, first, last, renderer FNV-64, Reggie state, player state,
# safe-zone state, warning state.
EXPECTED_COMPLETE_STATES = {
    "reggie_source_dwell":
        (0, 5_446, 5_497, 0xF6FCBDFF7EC65128, 0, 0, False, False),
    "reggie_down_phase_1":
        (1, 5_498, 5_499, 0xC9B6C6488A99CFDA, 1, 0, False, False),
    "reggie_down_phase_2":
        (2, 5_500, 5_501, 0xD08F888BB4309ECE, 2, 0, False, False),
    "reggie_down_phase_3":
        (3, 5_502, 5_504, 0x034BD44A3D090056, 3, 0, False, False),
    "reggie_down_phase_4":
        (4, 5_505, 5_506, 0xEB1B9DB942524BB6, 4, 0, False, False),
    "reggie_down_phase_5":
        (5, 5_507, 5_509, 0xC2E6A0DC4A981512, 5, 0, False, False),
    "reggie_destination_dwell":
        (6, 5_510, 5_583, 0x002051F0A10A9B6E, 6, 0, False, False),
    "player_right_phase_1_safe_active":
        (7, 5_584, 5_585, 0xC26825AA87EB9654, 6, 1, True, False),
    "player_right_phase_2":
        (8, 5_586, 5_588, 0x7F60864972D563EF, 6, 2, True, False),
    "player_right_phase_3":
        (9, 5_589, 5_590, 0xFFAF14433FB4EA38, 6, 3, True, False),
    "player_right_phase_4":
        (10, 5_591, 5_593, 0x41BAE93C9CD82772, 6, 4, True, False),
    "player_right_phase_5":
        (11, 5_594, 5_595, 0xB481E31D458D5008, 6, 5, True, False),
    "player_right_endpoint_record_4":
        (12, 5_596, 5_597, 0x9D94CC7C50EEB633, 6, 6, True, False),
    "player_idle_in_safe_zone":
        (14, 5_599, 5_605, 0x3506FDB321EA1E12, 6, 7, True, False),
    "next_slot_warning":
        (15, 5_606, 5_659, 0x257CA66A2AAC6E80, 6, 7, True, True),
}

EXPECTED_TORN = {
    "run_index": 13,
    "first_frame": 5_598,
    "last_frame": 5_598,
    "nonuniform_2x_blocks": 1,
    "renderer_fnv64": 0x32D6BFB44656507E,
    "nonuniform_logical_blocks": [[77, 87]],
    "previous_only_physical_pixels": 0,
    "next_only_physical_pixels": 148,
    "common_physical_pixels": 255_806,
    "neither_physical_pixels": 46,
    "neither_physical_bbox": [144, 174, 225, 175],
}


def decode_physical_frame(frame: int) -> np.ndarray:
    selection = f"select=eq(n\\,{frame})"
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(CAPTURE), "-map", "0:v:0",
            "-vf", selection, "-frames:v", "1", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) != common.CAPTURE_FRAME_BYTES:
        raise ValueError(f"frame {frame} did not decode to one physical page")
    return np.frombuffer(result.stdout, dtype=np.uint8).reshape(
        common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3
    )


def classify_torn_frame(runs: list[common.Run]) -> tuple[dict[str, object], bool]:
    run = runs[EXPECTED_TORN["run_index"]]
    previous = decode_physical_frame(EXPECTED_TORN["first_frame"] - 1)
    torn = decode_physical_frame(EXPECTED_TORN["first_frame"])
    following = decode_physical_frame(EXPECTED_TORN["first_frame"] + 1)

    logical = torn[0::2, 0::2]
    nonuniform = (
        np.any(logical != torn[0::2, 1::2], axis=2) |
        np.any(logical != torn[1::2, 0::2], axis=2) |
        np.any(logical != torn[1::2, 1::2], axis=2)
    )
    rows, columns = np.where(nonuniform)
    nonuniform_blocks = [
        [int(column), int(row)] for row, column in zip(rows, columns)
    ]

    matches_previous = np.all(torn == previous, axis=2)
    matches_following = np.all(torn == following, axis=2)
    previous_only = matches_previous & ~matches_following
    next_only = matches_following & ~matches_previous
    common_pixels = matches_previous & matches_following
    neither = ~matches_previous & ~matches_following
    neither_rows, neither_columns = np.where(neither)
    neither_bbox = [
        int(neither_columns.min()), int(neither_rows.min()),
        int(neither_columns.max()), int(neither_rows.max()),
    ]
    actual_hash = common.renderer_fnv64(run.rgb)
    classification = {
        "run_index": EXPECTED_TORN["run_index"],
        "first_frame": run.start,
        "last_frame": run.end,
        "frames": run.end - run.start + 1,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64_from_top_left_samples": f"0x{actual_hash:016x}",
        "nonuniform_logical_blocks": nonuniform_blocks,
        "physical_pixel_partition": {
            "previous_only": int(np.count_nonzero(previous_only)),
            "next_only": int(np.count_nonzero(next_only)),
            "common": int(np.count_nonzero(common_pixels)),
            "neither": int(np.count_nonzero(neither)),
        },
        "neither_physical_bbox": neither_bbox,
        "classification": "capture-only incomplete dirty repaint",
        "reason": (
            "the page already contains the complete following player-idle "
            "pose, but also has a 46-physical-pixel horizontal strip that "
            "belongs to neither complete neighbor"
        ),
    }
    matched = all((
        run.start == EXPECTED_TORN["first_frame"],
        run.end == EXPECTED_TORN["last_frame"],
        run.nonuniform_blocks == EXPECTED_TORN["nonuniform_2x_blocks"],
        actual_hash == EXPECTED_TORN["renderer_fnv64"],
        nonuniform_blocks == EXPECTED_TORN["nonuniform_logical_blocks"],
        classification["physical_pixel_partition"]["previous_only"] ==
            EXPECTED_TORN["previous_only_physical_pixels"],
        classification["physical_pixel_partition"]["next_only"] ==
            EXPECTED_TORN["next_only_physical_pixels"],
        classification["physical_pixel_partition"]["common"] ==
            EXPECTED_TORN["common_physical_pixels"],
        classification["physical_pixel_partition"]["neither"] ==
            EXPECTED_TORN["neither_physical_pixels"],
        neither_bbox == EXPECTED_TORN["neither_physical_bbox"],
    ))
    classification["matched"] = matched
    return classification, matched


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
            "reggie_state": expected[4],
            "player_state": expected[5],
            "bashful": "row_4_column_2_left_dwell",
            "safe_zone": "row_2_column_1" if expected[6] else None,
            "warning": expected[7],
        }
        selected_states[name] = state
        selected_states_match &= matched

    torn_state, torn_state_matches = classify_torn_frame(runs)
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
                "the deterministic native replay reaches both neighboring "
                "audits organically and now matches all fifteen complete "
                "pages between their shared boundary hashes"
            ),
        },
        "board": {
            "mode": "Multiples",
            "target": 5,
            "footer": "Demo",
            "events": [
                "Reggie moves down from row 0 column 2 through all five vertical callbacks",
                "Reggie restores the source-cell 5 and dwells at row 1 column 2",
                "a safe zone activates at row 2 column 1 on the same tick that Demo installs the next player move",
                "the safe-only phase-zero repaint remains offscreen until the first player movement callback",
                "Muncher moves right through all five interpolated positions, endpoint record 4, and idle",
                "the next Troggle slot displays its warning",
                "Bashful dwells at row 4 column 2 throughout",
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
        "capture_only_incomplete_state_count": 1,
        "capture_only_incomplete_state": torn_state,
        "capture_only_incomplete_state_matches": torn_state_matches,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": (
                len(EXPECTED_COMPLETE_STATES) - 2
            ),
            "overlap": (
                "the first page closes the later Bashful left-trail audit; "
                "the last page opens the first-player right-approach audit"
            ),
            "presentation_fix": (
                "selector-5 phase zero now retains the pre-safe-zone page "
                "when safe activation and move installation share one tick, "
                "eliminating native-only hash 0x9d789ca35264aafe"
            ),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "headless_gate": "game_render_state_test",
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        torn_state_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
