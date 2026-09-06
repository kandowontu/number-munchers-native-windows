#!/usr/bin/env python3
"""Audit the adjacent later Reggie trail and Bashful top exit in Number Demo.

The lossless interval contains eight complete Reggie crossing states, five
clipped Bashful exit poses, and the terminal top-right payload repaint. Two
nonuniform DOSBox-X refreshes are retained as capture evidence rather than
reproduced by the native double-buffered presenter.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-reggie-top-exit-report.json"
)

WINDOW_FIRST_FRAME = 3816
WINDOW_LAST_FRAME = 3870
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 55,
    "nonuniform_2x_blocks": 43,
    "logical_run_count": 16,
}

EXPECTED_STATES = {
    "pre_reggie_dwell": (0, 3816, 3817, 0x56588D1C3C07B155),
    "reggie_move_phase_1": (2, 3819, 3820, 0x2B8A6091BBC16843),
    "reggie_move_phase_2": (3, 3821, 3822, 0x9687ED7086B3ED13),
    "reggie_move_phase_3": (4, 3823, 3825, 0xF57AE1C0A5F12B77),
    "reggie_move_phase_4": (5, 3826, 3827, 0x5A1C5815FFF1D413),
    "reggie_move_phase_5": (6, 3828, 3829, 0x123DFD255115D1A3),
    "reggie_move_phase_6": (8, 3831, 3832, 0xC84B45ED1FA8BA43),
    "reggie_dwell_before_top_exit": (9, 3833, 3839, 0xC58469C69DE839C9),
    "bashful_top_exit_phase_1": (10, 3840, 3842, 0x13AE5F670E4DDA67),
    "bashful_top_exit_phase_2": (11, 3843, 3844, 0xA0F217F26983DA49),
    "bashful_top_exit_phase_3": (12, 3845, 3847, 0x9A5F740B9CC0FDD5),
    "bashful_top_exit_phase_4": (13, 3848, 3849, 0xF55632728D63A0A0),
    "bashful_top_exit_phase_5": (14, 3850, 3851, 0x7DB49D2A001BF27F),
    "bashful_terminal_top_right_repaint": (
        15, 3852, 3870, 0x46881471FF4615DF
    ),
}
EXPECTED_NONUNIFORM = (
    (1, 3818, 3818, 23, 0xF427C24F7D7B91D1),
    (7, 3830, 3830, 20, 0x241DB89F35909DF1),
)
NATIVE_CALLBACK_COMPLETE_HASHES = (
    "0xe23a4b5428ad3deb",
    "0xa420cc163f3eea11",
)


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
    for name, expected in EXPECTED_STATES.items():
        state, matched = common.selected_state(runs, expected)
        selected_states[name] = state
        selected_states_match &= matched

    nonuniform_states = []
    nonuniform_match = True
    for index, first, last, expected_nonuniform, expected_hash in EXPECTED_NONUNIFORM:
        run = runs[index]
        actual_hash = common.renderer_fnv64(run.rgb)
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == expected_nonuniform and
            not run.has_uniform_frame and actual_hash == expected_hash
        )
        nonuniform_match &= matched
        nonuniform_states.append({
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "renderer_fnv64": f"0x{actual_hash:016x}",
            "classification": "nonuniform 2x partial dirty refresh",
            "matched": matched,
        })

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
                "Reggie moves right from row 2 column 3 to column 4",
                "Bashful exits upward from row 0 column 5",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_nonuniform_refreshes": nonuniform_states,
        "native_replay": {
            "complete": True,
            "exact_stable_source_state_count": 14,
            "exact_stable_source_states_observed_in_order": list(EXPECTED_STATES),
            "complete_native_callback_hashes_without_stable_source_frames":
                list(NATIVE_CALLBACK_COMPLETE_HASHES),
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "the seeded first-Demo presenter matches every complete source "
                "state in order across the second Reggie crossing and the full "
                "five-phase clipped top exit; native keeps its two callback-local "
                "composites complete while the DOS capture records partial refreshes"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        nonuniform_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
