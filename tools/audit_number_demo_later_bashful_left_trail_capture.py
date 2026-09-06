#!/usr/bin/env python3
"""Audit the later first-Demo Bashful 23-to-26 leftward trail."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-bashful-left-trail-report.json"
)

WINDOW_FIRST_FRAME = 5386
WINDOW_LAST_FRAME = 5445
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 60,
    "nonuniform_2x_blocks": 0,
    "logical_run_count": 9,
}
EXPECTED_COMPLETE_STATES = {
    "pre_trail_dwell": (0, 5386, 5424, 0xB63A35E2DB896FDF),
    "left_phase_1": (1, 5425, 5426, 0xBBFC9AEB9BE72DDB),
    "left_phase_2": (2, 5427, 5429, 0xD2B7AE9542C00588),
    "left_phase_3": (3, 5430, 5431, 0x69C5A4DF86F95156),
    "left_phase_4": (4, 5432, 5434, 0xC0D1489AB64ECA36),
    "left_phase_5": (5, 5435, 5436, 0xEA113826B1A5EFD9),
    "left_phase_6": (6, 5437, 5438, 0xE5AAF2EA06CD3586),
    "terminal_dwell_26_visible": (
        8, 5440, 5445, 0xF6FCBDFF7EC65128
    ),
}
EXPECTED_PARTIAL_REPAINT = (7, 5439, 5439, 0x92AF2EA2EBF86EFA)
EXPECTED_PARTITION = {
    "phase_6_to_terminal_changed_pixels": 85,
    "partial_phase_6_pixels": 0,
    "partial_terminal_pixels": 85,
    "partial_neither_pixels": 13,
    "partial_neither_bbox": [129, 151, 148, 151],
}


def partial_partition(runs: list[common.Run]) -> dict[str, object]:
    phase_6 = np.frombuffer(runs[6].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    partial = np.frombuffer(runs[7].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    terminal = np.frombuffer(runs[8].rgb, dtype=np.uint8).reshape(
        common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3
    )
    changed = np.any(phase_6 != terminal, axis=2)
    same_phase_6 = np.all(partial == phase_6, axis=2)
    same_terminal = np.all(partial == terminal, axis=2)
    neither = ~(same_phase_6 | same_terminal)
    rows, columns = np.nonzero(neither)
    return {
        "phase_6_to_terminal_changed_pixels": int(np.count_nonzero(changed)),
        "partial_phase_6_pixels": int(np.count_nonzero(
            same_phase_6 & changed
        )),
        "partial_terminal_pixels": int(np.count_nonzero(
            same_terminal & changed
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
        state, matched = common.selected_state(runs, expected)
        selected_states[name] = state
        selected_states_match &= matched

    partial, partial_matches = common.selected_state(
        runs, EXPECTED_PARTIAL_REPAINT
    )
    partial["classification"] = (
        "uniform one-frame incomplete terminal dirty repaint: the completed "
        "terminal pixels are present, plus a 13-pixel one-row paint strip "
        "that belongs to neither completed neighboring surface"
    )
    partition = partial_partition(runs)
    partition_matches = partition == EXPECTED_PARTITION

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
            "event": (
                "Bashful regenerates bottom-row column 3 from 23 to 26 "
                "and moves left into column 2"
            ),
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_partial_repaint": partial,
        "excluded_partial_repaint_matches": partial_matches,
        "partial_repaint_partition": partition,
        "partial_repaint_partition_matches": partition_matches,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": 7,
            "overlap": (
                "pre_trail_dwell is the terminal page already counted by "
                "the third cannibal/player-walk audit; its 39-frame hold "
                "closes every source frame through the left-trail callback"
            ),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded first-Demo presenter matches every completed "
                "page in exact order and excludes the one-frame terminal "
                "dirty repaint"
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
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
