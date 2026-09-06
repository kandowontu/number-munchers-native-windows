#!/usr/bin/env python3
"""Audit the first complete in-board Reggie trail in Number Demo.

This interval includes the preceding upward Muncher move, its directional and
standing terminal callback frames, Reggie's ``207`` to ``65`` trail, all six
rightward actor positions, and dwell. One uniform-2x partial actor repaint and
two nonuniform DOSBox-X refreshes are classified separately from the complete
logical framebuffers required from native.
"""

from __future__ import annotations

import json
from pathlib import Path

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-reggie-trail-report.json"
)

WINDOW_FIRST_FRAME = 3179
WINDOW_LAST_FRAME = 3208
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 30,
    "nonuniform_2x_blocks": 23,
    "logical_run_count": 16,
}

EXPECTED_STATES = {
    "player_move_phase_1": (0, 3179, 3180, 0xF492558DB25BE420),
    "player_move_phase_2": (1, 3181, 3183, 0x044CE8FDB154953B),
    "player_move_phase_3": (2, 3184, 3185, 0x13450CEEAF58C464),
    "player_move_phase_4": (3, 3186, 3187, 0x90C96DD35F2BDAE1),
    "player_directional_terminal": (5, 3189, 3190, 0xD8E6A13F6279775C),
    "player_idle_before_trail": (6, 3191, 3192, 0x6867628C7E648F88),
    "reggie_move_phase_1": (7, 3193, 3194, 0xE92FEC284BD9BFB1),
    "reggie_move_phase_2": (9, 3196, 3197, 0x602EAAC0E4A50BB1),
    "reggie_move_phase_3": (10, 3198, 3199, 0x0EFBA26D5447243B),
    "reggie_move_phase_4": (11, 3200, 3202, 0xB01A36CB2A818931),
    "reggie_move_phase_5": (12, 3203, 3204, 0x40DB124C7E3AC8B1),
    "reggie_move_phase_6": (13, 3205, 3206, 0x505D7DE14F423A31),
    "reggie_dwell": (15, 3208, 3208, 0x8AED25E1D63B0451),
}
EXPECTED_UNIFORM_PARTIAL = (8, 3195, 3195, 0x50E30AB14F6565CF)
EXPECTED_NONUNIFORM = (
    (4, 3188, 3188, 22, 0x88371A5AC259AC5F),
    (14, 3207, 3207, 1, 0x507DFB7552C1A3B2),
)
EXPECTED_PARTIAL_GEOMETRY = {
    "partial_vs_phase_1": {"changed_pixels": 426, "bbox": [37, 87, 68, 114]},
    "partial_vs_phase_2": {"changed_pixels": 68, "bbox": [41, 87, 68, 93]},
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
    for name, expected in EXPECTED_STATES.items():
        state, matched = common.selected_state(runs, expected)
        selected_states[name] = state
        selected_states_match &= matched

    partial_index, partial_first, partial_last, partial_hash = EXPECTED_UNIFORM_PARTIAL
    partial_run = runs[partial_index]
    actual_partial_hash = common.renderer_fnv64(partial_run.rgb)
    partial_geometry = {
        "partial_vs_phase_1": common.difference_geometry(
            partial_run.rgb, runs[EXPECTED_STATES["reggie_move_phase_1"][0]].rgb
        ),
        "partial_vs_phase_2": common.difference_geometry(
            partial_run.rgb, runs[EXPECTED_STATES["reggie_move_phase_2"][0]].rgb
        ),
    }
    partial_matches = (
        partial_run.start == partial_first and partial_run.end == partial_last and
        partial_run.nonuniform_blocks == 0 and partial_run.has_uniform_frame and
        actual_partial_hash == partial_hash and
        partial_geometry == EXPECTED_PARTIAL_GEOMETRY
    )
    partial_state = {
        "run_index": partial_index,
        "first_frame": partial_run.start,
        "last_frame": partial_run.end,
        "nonuniform_2x_blocks": partial_run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_partial_hash:016x}",
        "difference_geometry": partial_geometry,
        "classification": (
            "uniform-2x partial dirty repaint: phase-2 position is complete except "
            "for a retained 28x7 top actor band"
        ),
        "matched": partial_matches,
    }

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
            "event": "Reggie regenerates row 2 column 0 from 207 to 65 and moves right",
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "excluded_uniform_partial_refresh": partial_state,
        "excluded_nonuniform_refreshes": nonuniform_states,
        "native_replay": {
            "complete": True,
            "exact_stable_state_count": 13,
            "exact_stable_states_presented": list(EXPECTED_STATES),
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "reason": (
                "the seeded first-Demo presenter suppresses selector-5's logical "
                "player phase 0, exposes the complete player-idle callback before "
                "the later Reggie trail, and matches every complete movement/dwell state"
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
        nonuniform_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
