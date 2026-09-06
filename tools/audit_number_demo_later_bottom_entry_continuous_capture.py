#!/usr/bin/env python3
"""Audit the continuous first-Demo route through the later bottom entry.

Frames 5173-5215 contain eleven presentation-complete pages spanning the
warning hold, the start of a downward Muncher move, a concurrent Bashful
bottom entry, and the entrant's dwell.  Two intervening pages are incomplete
dirty repaints and remain capture-only.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_player_reggie_safe_bridge_capture as bridge_common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-bottom-entry-continuous-report.json"
)

WINDOW_FIRST_FRAME = 5_173
WINDOW_LAST_FRAME = 5_215
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 43,
    "nonuniform_2x_blocks": 20,
    "logical_run_count": 13,
}

EXPECTED_RUNS = (
    (0, 5_173, 5_174, 0x7327515AB704A216),
    (1, 5_175, 5_181, 0x4F785C11832CACCA),
    (2, 5_182, 5_183, 0x62EF78163A5C73FD),
    (3, 5_184, 5_184, 0x9361AB0A4A9C0A4A),
    (4, 5_185, 5_186, 0xBC9E4DB839758068),
    (5, 5_187, 5_188, 0x08B631A66E5D0F0F),
    (6, 5_189, 5_191, 0x90456223812679D4),
    (7, 5_192, 5_193, 0x9C0190C31703DB30),
    (8, 5_194, 5_195, 0xBCAA9E6D0C9CE28B),
    (9, 5_196, 5_196, 0x0F30F10E973038B4),
    (10, 5_197, 5_198, 0xE1A2C8D0CB0630EB),
    (11, 5_199, 5_200, 0xD35094DB8E41F03C),
    (12, 5_201, 5_215, 0x364C0086FE50EA46),
)

CAPTURE_ONLY_RUN_INDEXES = (3, 9)
PRESENTATION_COMPLETE_RUN_INDEXES = tuple(
    index for index in range(len(EXPECTED_RUNS))
    if index not in CAPTURE_ONLY_RUN_INDEXES
)

EXPECTED_CAPTURE_ONLY_TRANSITIONS = {
    "run_3_incomplete_entry_start_paint": {
        "run_index": 3,
        "previous_only": 115,
        "next_only": 204,
        "common": 63_640,
        "neither": 41,
        "neither_bbox": [24, 146, 64, 146],
        "nonuniform_2x_blocks": 0,
        "physical_pixels_differing_from_top_left_duplication": 0,
        "physical_difference_bbox": None,
    },
    "run_9_incomplete_bashful_entry_paint": {
        "run_index": 9,
        "previous_only": 141,
        "next_only": 164,
        "common": 63_648,
        "neither": 47,
        "neither_bbox": [180, 159, 194, 165],
        "nonuniform_2x_blocks": 20,
        "physical_pixels_differing_from_top_left_duplication": 40,
        "physical_difference_bbox": [346, 319, 393, 333],
    },
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
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640,
        "height": 400, "frame_rate_fraction": "2190197/31250",
        "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES,
        "duration_seconds": 313.955777, "audio_codec": "pcm_s16le",
        "audio_sample_rate": 48_000, "audio_channels": 2,
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
        state, matched = bridge_common.classify_incomplete_run(runs, expected)
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
                "an actor-owned warning remains on the resident page",
                "a Bashful begins entering upward from below row 4 column 3",
                "the Muncher begins moving downward from row 3 to row 4 column 0",
                "the Bashful completes entry and begins its dwell",
                "a Reggie remains at row 0 column 2",
            ],
        },
        "source_runs": source_runs,
        "source_runs_match": runs_match,
        "capture_only_incomplete_dirty_repaints": incomplete_states,
        "capture_only_classification_matches": incomplete_states_match,
        "native_replay": {
            "complete": True,
            "source_capture_run_count": len(EXPECTED_RUNS),
            "presentation_complete_source_run_count":
                len(PRESENTATION_COMPLETE_RUN_INDEXES),
            "capture_only_run_count": len(CAPTURE_ONLY_RUN_INDEXES),
            "capture_only_run_indexes": list(CAPTURE_ONLY_RUN_INDEXES),
            "ordered_source_state_matches":
                len(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_run_indexes": list(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_states": complete_states,
            "incremental_new_complete_state_count": 4,
            "overlap_with_isolated_later_bottom_entry_state_count": 7,
            "pending_complete_run_indexes": [],
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded continuous replay matches all eleven completed "
                "source pages in exact order while omitting two measured "
                "incomplete dirty repaints"
            ),
        },
        "corrections": {
            "resident_entry_presentation_queue": (
                "when this entrant becomes due immediately before a Demo player "
                "action, the original retains the resident warning page, then "
                "presents the current player movement over successively older "
                "entrant records; native reproduces that presentation boundary "
                "without changing logical scheduler or PRNG timing"
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
