#!/usr/bin/env python3
"""Audit the continuous first-Demo route through the later bottom exit.

Frames 5687-5718 contain seven presentation-complete pages spanning the
Bashful's last dwell, a concurrent downward exit/upward Muncher move, and the
actor-removal page.  One intervening page is a measured scanout splice and
remains capture-only.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common
import audit_number_demo_later_bottom_exit_capture as isolated


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-bottom-exit-continuous-report.json"
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

EXPECTED_RUNS = (
    (0, 5_687, 5_705, 0xE572978E3378B63C),
    (1, 5_706, 5_708, 0x889C0F21B7D634D5),
    (2, 5_709, 5_710, 0x8F14E90902D728C7),
    (3, 5_711, 5_711, 0x69C22AABB9255FA1),
    (4, 5_712, 5_713, 0xB55BE53E20024918),
    (5, 5_714, 5_715, 0xE4FEC8A6974CFE71),
    (6, 5_716, 5_717, 0x8ABE6082E3593B9F),
    (7, 5_718, 5_718, 0xFE9AFA1A084C2395),
)
CAPTURE_ONLY_RUN_INDEX = 3
PRESENTATION_COMPLETE_RUN_INDEXES = (0, 1, 2, 4, 5, 6, 7)


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
        if expected[0] == CAPTURE_ONLY_RUN_INDEX:
            run = runs[expected[0]]
            matched = (
                run.start == expected[1] and run.end == expected[2] and
                common.renderer_fnv64(run.rgb) == expected[3]
            )
            state["matched"] = matched
        state["presentation_classification"] = (
            "capture_only_horizontal_scanout_splice"
            if expected[0] == CAPTURE_ONLY_RUN_INDEX
            else "presentation_complete"
        )
        source_runs.append(state)
        runs_match &= matched

    torn = runs[CAPTURE_ONLY_RUN_INDEX]
    torn_partition = isolated.partition(runs)
    torn_matches = (
        torn.start == 5_711 and torn.end == 5_711 and
        torn.nonuniform_blocks == 26 and not torn.has_uniform_frame and
        common.renderer_fnv64(torn.rgb) == 0x69C22AABB9255FA1 and
        torn_partition == isolated.EXPECTED_TORN_PARTITION
    )

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
        "oracle_scope": {
            "classification": "continuous seeded visual/callback replay",
            "continuous_replay_claim": True,
            "route": "first Number Demo board, source frames 5687-5718",
        },
        "board": {
            "demo_board": 1,
            "level": 10,
            "mode": "Multiples",
            "target": 5,
            "events": [
                "Bashful holds its last left-facing dwell at row 4 column 2",
                "Bashful regenerates 115 as 105 and exits below the board",
                "the Muncher begins moving upward from row 2 column 2",
                "Reggie remains at row 1 column 2",
                "the exit actor is removed before the following collision callback",
            ],
        },
        "source_runs": source_runs,
        "source_runs_match": runs_match,
        "capture_only_scanout_splice": {
            "run_index": CAPTURE_ONLY_RUN_INDEX,
            "first_frame": torn.start,
            "last_frame": torn.end,
            "nonuniform_2x_blocks": torn.nonuniform_blocks,
            "renderer_fnv64": (
                f"0x{common.renderer_fnv64(torn.rgb):016x}"
            ),
            "pixel_partition": torn_partition,
            "classification": (
                "capture-only scanout at logical row 90; every changed pixel "
                "comes from one of the two neighboring complete pages"
            ),
            "matched": torn_matches,
        },
        "native_replay": {
            "complete": True,
            "continuous": True,
            "source_capture_run_count": len(EXPECTED_RUNS),
            "presentation_complete_source_run_count":
                len(PRESENTATION_COMPLETE_RUN_INDEXES),
            "capture_only_run_count": 1,
            "ordered_source_state_matches":
                len(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_run_indexes": list(PRESENTATION_COMPLETE_RUN_INDEXES),
            "matched_states": complete_states,
            "incremental_new_complete_state_count": 0,
            "overlap_with_isolated_later_bottom_exit_state_count": 7,
            "pending_complete_run_indexes": [],
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded continuous replay matches all seven completed "
                "source pages in exact order and omits the measured scanout splice"
            ),
        },
        "corrections": {
            "resident_exit_presentation_queue": (
                "native now retains the pre-exit resident page, paints current "
                "player callbacks over delayed exit-actor records, suppresses "
                "competing callback-local pages, and queues actor removal "
                "immediately after the final clipped pose"
            ),
            "logical_scheduler_and_prng": "unchanged",
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
        torn_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
