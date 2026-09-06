#!/usr/bin/env python3
"""Audit the first-Demo Bashful right-entry interval in Number Munchers.

Six source runs are completed 320x200 movement pages.  One intervening
one-frame run restores only four x=308 pixels before the next actor paint
overwrites them; it is a uniform capture of an incomplete dirty repaint and
remains source evidence rather than native presentation behavior.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-bashful-right-entry-report.json"
)

WINDOW_FIRST_FRAME = 4309
WINDOW_LAST_FRAME = 4339
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 31,
    "nonuniform_2x_blocks": 0,
    "logical_run_count": 7,
}

EXPECTED_COMPLETE_STATES = {
    "entry_phase_2_retained_right_rule": (
        0, 4309, 4310, 0x78BA03AD58531CF9
    ),
    "entry_phase_3": (2, 4312, 4313, 0x5CB927DC8AEB3669),
    "entry_phase_4": (3, 4314, 4315, 0xB3B10C32DEEE6853),
    "entry_phase_5": (4, 4316, 4318, 0x78B3A87806E074BF),
    "entry_phase_6": (5, 4319, 4320, 0xDDA5874EEF639E5F),
    "entry_dwell_right_rule_restored": (
        6, 4321, 4339, 0x3225D1E44387AA7D
    ),
}
EXPECTED_PARTIAL_REPAINT = (1, 4311, 4311, 0x0806CB7DC6681E51)


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

    partial_repaint, partial_repaint_matches = common.selected_state(
        runs, EXPECTED_PARTIAL_REPAINT
    )
    partial_repaint["classification"] = (
        "uniform one-frame incomplete dirty repaint: four right-rule pixels "
        "are restored between completed Bashful entry poses and overwritten "
        "by the next actor paint"
    )

    first = runs[0].rgb
    second = runs[1].rgb
    changed = []
    for pixel in range(common.LOGICAL_WIDTH * common.LOGICAL_HEIGHT):
        offset = pixel * 3
        if first[offset:offset + 3] != second[offset:offset + 3]:
            changed.append((pixel % common.LOGICAL_WIDTH,
                            pixel // common.LOGICAL_WIDTH))
    retained_rule_geometry = {
        "changed_pixels": len(changed),
        "bbox": [
            min(x for x, _ in changed),
            min(y for _, y in changed),
            max(x for x, _ in changed),
            max(y for _, y in changed),
        ],
    }
    retained_rule_geometry_matches = retained_rule_geometry == {
        "changed_pixels": 4,
        "bbox": [308, 112, 308, 115],
    }

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
                "Bashful enters leftward from the right edge at row 2 "
                "column 5"
            ),
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_partial_repaint": partial_repaint,
        "excluded_partial_repaint_matches": partial_repaint_matches,
        "retained_right_rule_geometry": retained_rule_geometry,
        "retained_right_rule_geometry_matches":
            retained_rule_geometry_matches,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "headless_gate": "game_render_state_test",
            "reason": (
                "the seeded first-Demo presenter matches every completed "
                "Bashful right-entry page and excludes the one-frame "
                "erase-between-poses repaint instead of reproducing flicker"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        partial_repaint_matches,
        retained_rule_geometry_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
