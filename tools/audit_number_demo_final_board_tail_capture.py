#!/usr/bin/env python3
"""Audit the final gameplay-board tail in the supplied Number Demo capture."""

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
    "full-demo-final-board-tail-report.json"
)

WINDOW_FIRST_FRAME = 20_188
WINDOW_LAST_FRAME = 20_527
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 340,
    "nonuniform_2x_blocks": 1,
    "logical_run_count": 32,
}

# run, first, last, renderer FNV-64, event, player state
EXPECTED_COMPLETE_STATES = {
    "first_left_phase_1":
        (0, 20_188, 20_189, 0xF8D2505FC9AF5883, "first_left", 1),
    "first_left_phase_2":
        (1, 20_190, 20_191, 0x8D67DE4BE0BC2094, "first_left", 2),
    "first_left_phase_3":
        (3, 20_193, 20_194, 0x99DEE31553FB2311, "first_left", 3),
    "first_left_phase_4":
        (4, 20_195, 20_196, 0xD5A9FE11D11DC74B, "first_left", 4),
    "first_left_phase_5":
        (5, 20_197, 20_199, 0x5FB0AC3F5EAF5ADC, "first_left", 5),
    "first_left_terminal":
        (6, 20_200, 20_201, 0xD927D391CA2D7B8E, "first_left_terminal", 10),
    "first_left_idle":
        (7, 20_202, 20_276, 0xFF72D12B66633813, "idle", 7),
    "down_phase_1":
        (8, 20_277, 20_278, 0x2E0F015FAB52A884, "down", 1),
    "down_phase_2":
        (9, 20_279, 20_280, 0x25D2E2F155528B75, "down", 2),
    "down_phase_3":
        (10, 20_281, 20_283, 0x1CD43B9EBBB3CF52, "down", 3),
    "down_phase_4":
        (11, 20_284, 20_285, 0xC3A0C7B6A0AA8999, "down", 4),
    "down_terminal":
        (12, 20_286, 20_287, 0x2819EFC68BF4F085, "down_terminal", 8),
    "down_idle":
        (14, 20_289, 20_319, 0x15740A4A7D463C2D, "idle", 7),
    "second_left_phase_1":
        (15, 20_320, 20_321, 0xF4182218E992A0B6, "second_left", 1),
    "second_left_phase_2":
        (16, 20_322, 20_324, 0x2855AF3FFDA9B44C, "second_left", 2),
    "second_left_phase_3":
        (17, 20_325, 20_326, 0x1EB6875F775F09B5, "second_left", 3),
    "second_left_phase_4":
        (18, 20_327, 20_328, 0x8B2E3717F7A8E89B, "second_left", 4),
    "second_left_phase_5":
        (19, 20_329, 20_331, 0xFFA2A0976EA24CA0, "second_left", 5),
    "second_left_terminal":
        (20, 20_332, 20_333, 0xC53655CB27E6B29E, "second_left_terminal", 10),
    "second_left_idle":
        (21, 20_334, 20_386, 0x7D2581572BE7D287, "idle", 7),
    "chew_record_12_a":
        (22, 20_387, 20_388, 0xE22858A2A40D0F9D, "chew", 12),
    "chew_record_13_a":
        (24, 20_390, 20_391, 0x6EDFE44064BE85CD, "chew", 13),
    "chew_record_14_a":
        (25, 20_392, 20_393, 0xE22858A2A40D0F9D, "chew", 14),
    "chew_record_13_b":
        (26, 20_394, 20_396, 0x6EDFE44064BE85CD, "chew", 13),
    "chew_record_12_b":
        (27, 20_397, 20_398, 0xE22858A2A40D0F9D, "chew", 12),
    "chew_record_13_c":
        (28, 20_399, 20_401, 0x6EDFE44064BE85CD, "chew", 13),
    "chew_record_14_b":
        (29, 20_402, 20_403, 0xE22858A2A40D0F9D, "chew", 14),
    "chew_terminal_record_13":
        (30, 20_404, 20_449, 0x6EDFE44064BE85CD, "chew_terminal", 13),
    "next_troggle_warning":
        (31, 20_450, 20_527, 0xE5F33A065A5D946B, "warning", 7),
}

EXPECTED_PARTIALS = {
    "first_left_phase_3_border_partial": {
        "run": (2, 20_192, 20_192, 1, 0x78B9F5142AEAA591),
        "neighbors": (1, 3),
        "partition": {
            "neighbor_changed_pixels": 343,
            "partial_previous_pixels": 0,
            "partial_next_pixels": 343,
            "partial_neither_pixels": 4,
            "partial_neither_bbox": [116, 87, 116, 90],
        },
        "classification": (
            "one-block nonuniform incomplete player repaint: the complete next "
            "phase is present plus four vertical-border pixels belonging to "
            "neither completed neighbor"
        ),
    },
    "down_terminal_to_idle_partial": {
        "run": (13, 20_288, 20_288, 0, 0x21641F48379BBD1B),
        "neighbors": (12, 14),
        "partition": {
            "neighbor_changed_pixels": 39,
            "partial_previous_pixels": 0,
            "partial_next_pixels": 39,
            "partial_neither_pixels": 16,
            "partial_neither_bbox": [88, 133, 95, 135],
        },
        "classification": (
            "uniform incomplete terminal-to-idle player repaint: all 39 "
            "completed idle-delta pixels are present plus 16 pixels belonging "
            "to neither completed neighbor"
        ),
    },
    "chew_12_to_13_horizontal_splice": {
        "run": (23, 20_389, 20_389, 0, 0x778CFF2762CF0564),
        "neighbors": (22, 24),
        "partition": {
            "neighbor_changed_pixels": 240,
            "partial_previous_pixels": 135,
            "partial_next_pixels": 105,
            "partial_neither_pixels": 0,
            "partial_neither_bbox": None,
        },
        "classification": (
            "uniform incomplete horizontal player repaint: the upper actor "
            "portion remains on record 12 while the lower portion has reached "
            "record 13"
        ),
    },
}

EXPECTED_NONUNIFORM_DETAIL = {
    "frame": 20_192,
    "logical_block_coordinates": [[116, 90]],
    "top_sample_rgb": [0, 0, 121],
    "bottom_sample_rgb": [255, 85, 255],
}


def partition(
    runs: list[common.Run], previous: int, partial: int, following: int
) -> dict[str, object]:
    shape = (common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    before = np.frombuffer(runs[previous].rgb, dtype=np.uint8).reshape(shape)
    middle = np.frombuffer(runs[partial].rgb, dtype=np.uint8).reshape(shape)
    after = np.frombuffer(runs[following].rgb, dtype=np.uint8).reshape(shape)
    changed = np.any(before != after, axis=2)
    same_before = np.all(middle == before, axis=2)
    same_after = np.all(middle == after, axis=2)
    neither = ~(same_before | same_after)
    rows, columns = np.where(neither)
    bbox = None if not len(columns) else [
        int(columns.min()), int(rows.min()),
        int(columns.max()), int(rows.max()),
    ]
    return {
        "neighbor_changed_pixels": int(np.count_nonzero(changed)),
        "partial_previous_pixels": int(np.count_nonzero(same_before & changed)),
        "partial_next_pixels": int(np.count_nonzero(same_after & changed)),
        "partial_neither_pixels": int(np.count_nonzero(neither)),
        "partial_neither_bbox": bbox,
    }


def partial_state(
    runs: list[common.Run], specification: dict[str, object]
) -> tuple[dict[str, object], bool]:
    index, first, last, nonuniform, expected_hash = specification["run"]
    previous, following = specification["neighbors"]
    run = runs[index]
    actual_hash = common.renderer_fnv64(run.rgb)
    actual_partition = partition(runs, previous, index, following)
    matched = (
        run.start == first and run.end == last and
        run.nonuniform_blocks == nonuniform and
        actual_hash == expected_hash and
        actual_partition == specification["partition"]
    )
    return ({
        "run_index": index,
        "first_frame": run.start,
        "last_frame": run.end,
        "frames": run.end - run.start + 1,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_hash:016x}",
        "pixel_partition": actual_partition,
        "classification": specification["classification"],
        "matched": matched,
    }, matched)


def decode_nonuniform_detail(path: Path) -> dict[str, object]:
    frame = 20_192
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-vf", f"select=eq(n\\,{frame})", "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    captured = np.frombuffer(result.stdout, dtype=np.uint8).reshape(
        common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3
    )
    top_left = captured[0::2, 0::2]
    top_right = captured[0::2, 1::2]
    bottom_left = captured[1::2, 0::2]
    bottom_right = captured[1::2, 1::2]
    mismatches = (
        np.any(top_left != top_right, axis=2) |
        np.any(top_left != bottom_left, axis=2) |
        np.any(top_left != bottom_right, axis=2)
    )
    rows, columns = np.where(mismatches)
    coordinates = [
        [int(column), int(row)] for row, column in zip(rows, columns)
    ]
    top_colors = np.unique(
        np.concatenate((top_left[mismatches], top_right[mismatches])), axis=0
    )
    bottom_colors = np.unique(
        np.concatenate((bottom_left[mismatches], bottom_right[mismatches])), axis=0
    )
    return {
        "frame": frame,
        "logical_block_coordinates": coordinates,
        "top_sample_rgb": top_colors[0].astype(int).tolist(),
        "bottom_sample_rgb": bottom_colors[0].astype(int).tolist(),
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
            "event": expected[4],
            "player_state": expected[5],
            "remaining_helper_count": 0,
            "active_safe_zone_count": 0,
        }
        selected_states[name] = state
        selected_states_match &= matched

    partials: dict[str, dict[str, object]] = {}
    partials_match = True
    for name, specification in EXPECTED_PARTIALS.items():
        state, matched = partial_state(runs, specification)
        partials[name] = state
        partials_match &= matched

    nonuniform_detail = decode_nonuniform_detail(CAPTURE)
    nonuniform_detail_matches = nonuniform_detail == EXPECTED_NONUNIFORM_DETAIL

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
                "Muncher moves left from row 2 column 2 to column 1",
                "Muncher moves down from row 2 column 1 to row 3 column 1",
                "Muncher moves left from row 3 column 1 to column 0",
                "Muncher chews and clears the correct value 21",
                "the next Troggle warning appears with no actor yet visible",
            ],
            "remaining_helpers": 0,
            "active_safe_zones": 0,
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_incomplete_refreshes": partials,
        "incomplete_refreshes_match_source": partials_match,
        "nonuniform_block_detail": nonuniform_detail,
        "nonuniform_block_detail_matches": nonuniform_detail_matches,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "capture_only_incomplete_refresh_count": len(EXPECTED_PARTIALS),
            "headless_gate": "game_render_state_test",
            "reason": (
                "native matches all 29 completed movement/chew/warning pages "
                "in source order and excludes all three incomplete actor paints"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        partials_match,
        nonuniform_detail_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
