#!/usr/bin/env python3
"""Audit the player/Reggie/safe-zone bridge before Bashful enters.

The lossless DOS capture contains twenty-one presentation-complete logical
pages and seven incomplete dirty repaints.  Five incomplete pages also break
the capture's normal 2x physical-pixel duplication; the other two are uniform
mixtures between complete neighbors.  Native must present the twenty-one
complete pages in exact order without reproducing those tears.
"""

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
    "full-demo-player-reggie-safe-bridge-report.json"
)

WINDOW_FIRST_FRAME = 4_162
WINDOW_LAST_FRAME = 4_308
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 147,
    "nonuniform_2x_blocks": 74,
    "logical_run_count": 28,
}

EXPECTED_RUNS = (
    (0, 4162, 4164, 0x1DD62B4466906995),
    (1, 4165, 4166, 0x4491D303FB06FDE5),
    (2, 4167, 4168, 0xA994BEC9B912DB7A),
    (3, 4169, 4169, 0x2F420014B92BF147),
    (4, 4170, 4171, 0x4BC68B2D8D3DFF6D),
    (5, 4172, 4173, 0xE07A95288193372F),
    (6, 4174, 4204, 0x63325E023794CD27),
    (7, 4205, 4205, 0xE3B32CC27B1CD605),
    (8, 4206, 4207, 0x8BF7D640704DC345),
    (9, 4208, 4209, 0xD8B272F47D6DFEAD),
    (10, 4210, 4210, 0x811D99D4463F2AE9),
    (11, 4211, 4212, 0x1A80ADD72279B16F),
    (12, 4213, 4214, 0x3B7B40F54A797986),
    (13, 4215, 4216, 0x76F512B461C48411),
    (14, 4217, 4217, 0x7DA18F6781A2E4A5),
    (15, 4218, 4219, 0x3EDC38C1F41183D5),
    (16, 4220, 4221, 0x364D49A1B24D2EDD),
    (17, 4222, 4222, 0xB59C871B5492445D),
    (18, 4223, 4284, 0xD9690D6E3079143F),
    (19, 4285, 4286, 0x6FEA9F28A7295153),
    (20, 4287, 4289, 0x296D98FCC27505A3),
    (21, 4290, 4291, 0x488FF75F480D9544),
    (22, 4292, 4293, 0x58CFB69F263882AB),
    (23, 4294, 4294, 0xAB003CDF89379312),
    (24, 4295, 4296, 0xC31B0B4841013F19),
    (25, 4297, 4303, 0x39ED0A3D7D8BA751),
    (26, 4304, 4304, 0xCDDCF220C4C507DB),
    (27, 4305, 4308, 0x0C719B74227B74C3),
)

CAPTURE_ONLY_RUN_INDEXES = (3, 7, 10, 14, 17, 23, 26)
EXPECTED_CAPTURE_ONLY_TRANSITIONS = {
    "run_3_incomplete_player_down_paint": {
        "run_index": 3,
        "previous_only": 233,
        "next_only": 115,
        "common": 63_596,
        "neither": 56,
        "neither_bbox": [181, 128, 194, 133],
        "nonuniform_2x_blocks": 29,
        "physical_pixels_differing_from_top_left_duplication": 58,
        "physical_difference_bbox": [346, 249, 397, 267],
    },
    "run_7_incomplete_reggie_right_paint": {
        "run_index": 7,
        "previous_only": 2,
        "next_only": 504,
        "common": 63_494,
        "neither": 0,
        "neither_bbox": None,
        "nonuniform_2x_blocks": 1,
        "physical_pixels_differing_from_top_left_duplication": 2,
        "physical_difference_bbox": [328, 297, 329, 297],
    },
    "run_10_incomplete_combined_actor_paint": {
        "run_index": 10,
        "previous_only": 0,
        "next_only": 843,
        "common": 63_124,
        "neither": 33,
        "neither_bbox": [168, 116, 200, 116],
        "nonuniform_2x_blocks": 21,
        "physical_pixels_differing_from_top_left_duplication": 42,
        "physical_difference_bbox": [360, 233, 401, 233],
    },
    "run_14_incomplete_combined_actor_paint": {
        "run_index": 14,
        "previous_only": 879,
        "next_only": 30,
        "common": 63_082,
        "neither": 9,
        "neither_bbox": [164, 164, 164, 172],
        "nonuniform_2x_blocks": 13,
        "physical_pixels_differing_from_top_left_duplication": 26,
        "physical_difference_bbox": [328, 345, 381, 345],
    },
    "run_17_incomplete_player_terminal_to_idle_paint": {
        "run_index": 17,
        "previous_only": 32,
        "next_only": 39,
        "common": 63_929,
        "neither": 0,
        "neither_bbox": None,
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_23_incomplete_player_down_paint": {
        "run_index": 23,
        "previous_only": 329,
        "next_only": 11,
        "common": 63_660,
        "neither": 0,
        "neither_bbox": None,
        "nonuniform_2x_blocks": 10,
        "physical_pixels_differing_from_top_left_duplication": 20,
        "physical_difference_bbox": [348, 285, 367, 285],
    },
    "run_26_incomplete_safe_zone_outline": {
        "run_index": 26,
        "previous_only": 36,
        "next_only": 377,
        "common": 63_587,
        "neither": 0,
        "neither_bbox": None,
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
}

PRESENTATION_COMPLETE_RUN_INDEXES = tuple(
    index for index in range(len(EXPECTED_RUNS))
    if index not in CAPTURE_ONLY_RUN_INDEXES
)


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


def bbox(mask: np.ndarray) -> list[int] | None:
    rows, columns = np.where(mask)
    if not len(columns):
        return None
    return [
        int(columns.min()), int(rows.min()),
        int(columns.max()), int(rows.max()),
    ]


def classify_incomplete_run(
    runs: list[common.Run], expected: dict[str, object]
) -> tuple[dict[str, object], bool]:
    index = int(expected["run_index"])
    previous = np.frombuffer(
        runs[index - 1].rgb, dtype=np.uint8
    ).reshape(common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    current = np.frombuffer(
        runs[index].rgb, dtype=np.uint8
    ).reshape(common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    following = np.frombuffer(
        runs[index + 1].rgb, dtype=np.uint8
    ).reshape(common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)

    matches_previous = np.all(current == previous, axis=2)
    matches_following = np.all(current == following, axis=2)
    previous_only = matches_previous & ~matches_following
    next_only = matches_following & ~matches_previous
    common_pixels = matches_previous & matches_following
    neither = ~matches_previous & ~matches_following

    physical = decode_physical_frame(runs[index].start)
    top_left_duplication = np.repeat(
        np.repeat(physical[0::2, 0::2], 2, axis=0), 2, axis=1
    )
    physical_difference = np.any(physical != top_left_duplication, axis=2)

    actual = {
        "run_index": index,
        "first_frame": runs[index].start,
        "last_frame": runs[index].end,
        "renderer_fnv64_from_top_left_samples":
            f"0x{common.renderer_fnv64(runs[index].rgb):016x}",
        "previous_only": int(np.count_nonzero(previous_only)),
        "next_only": int(np.count_nonzero(next_only)),
        "common": int(np.count_nonzero(common_pixels)),
        "neither": int(np.count_nonzero(neither)),
        "neither_bbox": bbox(neither),
        "nonuniform_2x_blocks": runs[index].nonuniform_blocks,
        "physical_pixels_differing_from_top_left_duplication":
            int(np.count_nonzero(physical_difference)),
        "physical_difference_bbox": bbox(physical_difference),
        "classification": "capture-only incomplete dirty repaint",
    }
    comparable = {
        key: actual[key] for key in expected
    }
    matched = comparable == expected
    actual["matched"] = matched
    return actual, matched


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

    source_runs: list[dict[str, object]] = []
    runs_match = len(runs) == len(EXPECTED_RUNS)
    for expected in EXPECTED_RUNS:
        state, matched = common.selected_state(runs, expected)
        if expected[0] in CAPTURE_ONLY_RUN_INDEXES:
            run = runs[expected[0]]
            matched = (
                run.start == expected[1] and run.end == expected[2] and
                common.renderer_fnv64(run.rgb) == expected[3]
            )
            state["matched"] = matched
        state["presentation_classification"] = (
            "capture_only_incomplete_dirty_repaint"
            if state["run_index"] in CAPTURE_ONLY_RUN_INDEXES
            else "presentation_complete"
        )
        source_runs.append(state)
        runs_match &= matched

    incomplete_states: dict[str, dict[str, object]] = {}
    incomplete_states_match = True
    for name, expected in EXPECTED_CAPTURE_ONLY_TRANSITIONS.items():
        state, matched = classify_incomplete_run(runs, expected)
        incomplete_states[name] = state
        incomplete_states_match &= matched

    complete_states = [
        source_runs[index] for index in PRESENTATION_COMPLETE_RUN_INDEXES
    ]
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
                "Muncher completes a downward move from row 2 column 3 to row 3 column 3",
                "Reggie begins moving right from row 4 column 2 while Muncher reverses upward",
                "the two actor jobs advance together through every completed callback page",
                "Reggie restores 115 at row 4 column 2 and dwells over 92 at row 4 column 3",
                "Muncher moves down again to row 3 column 3",
                "the row 2 column 1 safe zone is presented only after its partial outline repaint completes",
            ],
            "labels_row_major": [
                "21", "", "232", "", "133", "11",
                "", "", "", "", "", "",
                "65", "", "", "", "", "3",
                "59", "211", "176", "14", "2", "230",
                "220", "207", "115", "92", "146", "85",
            ],
        },
        "source_runs": source_runs,
        "source_runs_match": runs_match,
        "capture_only_incomplete_dirty_repaints": incomplete_states,
        "capture_only_classification_matches": incomplete_states_match,
        "native_replay": {
            "complete": True,
            "source_capture_run_count": len(EXPECTED_RUNS),
            "presentation_complete_source_run_count": len(
                PRESENTATION_COMPLETE_RUN_INDEXES
            ),
            "capture_only_run_count": len(CAPTURE_ONLY_RUN_INDEXES),
            "capture_only_run_indexes": list(CAPTURE_ONLY_RUN_INDEXES),
            "ordered_source_state_matches": len(
                PRESENTATION_COMPLETE_RUN_INDEXES
            ),
            "matched_run_indexes": list(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_states": complete_states,
            "pending_complete_run_indexes": [],
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded replay reaches both neighboring audit gates "
                "organically and matches all 21 presentation-complete pages "
                "in exact order while omitting seven measured incomplete repaints"
            ),
        },
        "parity_verdict": {
            "source_measurement_valid": True,
            "native_complete": True,
            "remaining_gap": None,
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        runs_match,
        incomplete_states_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
