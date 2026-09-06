#!/usr/bin/env python3
"""Audit the player-only interval between the two later Helper exits.

The focused fifth-board Number Demo interval contains two Muncher moves, two
complete seven-record chews, two safe-zone expirations, and one surviving
Helper dwelling at the lower-right cell.  It lies after the withdrawn
continuous-replay boundary, so it is used only as isolated lossless visual and
callback evidence.
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
    "full-demo-post-helper-player-report.json"
)

WINDOW_FIRST_FRAME = 19_887
WINDOW_LAST_FRAME = 20_062
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 176,
    "nonuniform_2x_blocks": 71,
    "logical_run_count": 33,
}

# run, first complete frame, last complete frame, renderer FNV-64,
# player event, player state, row-2/column-1 safe, row-2/column-4 safe
EXPECTED_COMPLETE_STATES = {
    "left_phase_1":
        (0, 19_887, 19_888, 0xBAA12A5E87275937, "left_move", 1, True, True),
    "left_phase_2":
        (1, 19_889, 19_891, 0xB599FEB94CA4A9CC, "left_move", 2, True, True),
    "left_phase_3":
        (2, 19_892, 19_893, 0xCFC1D6028A440D05, "left_move", 3, True, True),
    "left_phase_4":
        (3, 19_894, 19_896, 0xFC6804E5FD0FBA5D, "left_move", 4, True, True),
    "left_phase_5":
        (4, 19_897, 19_898, 0xC2D9AD7119D6FEF1, "left_move", 5, True, True),
    "left_terminal":
        (5, 19_899, 19_900, 0x1AFF80CFED734128, "left_terminal", 10, True, True),
    "left_idle":
        (6, 19_901, 19_956, 0x287E576792F8AC39, "idle", 7, True, True),
    "first_chew_record_12_a":
        (7, 19_957, 19_958, 0xA48F9572A7C36CED, "first_chew", 12, True, True),
    "first_chew_record_13_a":
        (8, 19_959, 19_960, 0x2177A67B3D6EAFA9, "first_chew", 13, True, True),
    "first_chew_record_14_a":
        (10, 19_962, 19_963, 0xA48F9572A7C36CED, "first_chew", 14, True, True),
    "first_chew_record_13_b":
        (11, 19_964, 19_965, 0x2177A67B3D6EAFA9, "first_chew", 13, True, True),
    "first_chew_record_12_b":
        (12, 19_966, 19_968, 0xA48F9572A7C36CED, "first_chew", 12, True, True),
    "first_chew_record_13_c":
        (13, 19_969, 19_970, 0x2177A67B3D6EAFA9, "first_chew", 13, True, True),
    "first_chew_record_14_b":
        (14, 19_971, 19_972, 0xA48F9572A7C36CED, "first_chew", 14, True, True),
    "first_chew_terminal_record_13":
        (16, 19_974, 19_989, 0x2177A67B3D6EAFA9, "first_chew_terminal", 13, True, True),
    "first_safe_expired_phase_zero_hold":
        (17, 19_990, 19_992, 0x4205F77FE623F871, "down_phase_zero_hold", 13, False, True),
    "down_phase_1":
        (18, 19_993, 19_994, 0x305EA179C7999301, "down_move", 1, False, True),
    "down_phase_2":
        (19, 19_995, 19_996, 0x35818149BC8EB091, "down_move", 2, False, True),
    "down_phase_3":
        (21, 19_998, 19_999, 0x12A06652DA080F12, "down_move", 3, False, True),
    "down_phase_4":
        (22, 20_000, 20_001, 0xE052125FE78EB41D, "down_move", 4, False, True),
    "down_terminal":
        (23, 20_002, 20_004, 0xF525D8231CB89911, "down_terminal", 8, False, True),
    "down_idle":
        (24, 20_005, 20_040, 0x01E3949A5E8F15C9, "idle", 7, False, True),
    "second_chew_record_12_a":
        (25, 20_041, 20_042, 0x7C58F0B5238333A9, "second_chew", 12, False, True),
    "second_chew_record_13_a":
        (26, 20_043, 20_045, 0x3647A16545236C45, "second_chew", 13, False, True),
    "second_chew_record_14_a":
        (27, 20_046, 20_047, 0x7C58F0B5238333A9, "second_chew", 14, False, True),
    "second_chew_record_13_b":
        (28, 20_048, 20_049, 0x3647A16545236C45, "second_chew", 13, False, True),
    "second_chew_record_12_b":
        (29, 20_050, 20_052, 0x7C58F0B5238333A9, "second_chew", 12, False, True),
    "second_chew_record_13_c":
        (30, 20_053, 20_054, 0x3647A16545236C45, "second_chew", 13, False, True),
    "second_chew_record_14_b":
        (31, 20_055, 20_057, 0x7C58F0B5238333A9, "second_chew", 14, False, True),
    # Run 32 continues through frame 20062, but frames 20058-20061 are four
    # uniform copies of this exact complete terminal page. Frame 20062 keeps
    # the same top-left logical sample while partially erasing the other safe
    # zone on the lower half of five doubled blocks; it is classified below.
    "second_chew_terminal_record_13":
        (32, 20_058, 20_061, 0x3647A16545236C45, "second_chew_terminal", 13, False, True),
}

EXPECTED_TORN_RUNS = {
    "first_chew_13_to_14_torn":
        (9, 19_961, 19_961, 21, 0xA7AB8B004E7C9701),
    "first_chew_14_to_terminal_13_torn":
        (15, 19_973, 19_973, 20, 0x5BED4A291AF07A09),
    "down_phase_2_to_3_torn":
        (20, 19_997, 19_997, 25, 0xB6B1A17F33817CCD),
}

EXPECTED_NONUNIFORM_FRAMES = {
    19_961: 21,
    19_973: 20,
    19_997: 25,
    20_062: 5,
}
EXPECTED_SAFE_EXPIRY_TAIL = {
    "frame": 20_062,
    "nonuniform_2x_blocks": 5,
    "logical_block_coordinates": [
        [251, 115], [252, 115], [253, 115], [254, 115], [259, 115],
    ],
    "top_sample_rgb": [255, 255, 255],
    "bottom_sample_rgb": [0, 0, 121],
    "top_left_logical_renderer_fnv64": "0x3647a16545236c45",
}


def decode_frame_metadata(path: Path) -> tuple[dict[int, int], dict[str, object]]:
    selection = f"select=between(n\\,{WINDOW_FIRST_FRAME}\\,{WINDOW_LAST_FRAME})"
    result = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(path), "-map", "0:v:0",
            "-vf", selection, "-fps_mode", "passthrough", "-f", "rawvideo",
            "-pix_fmt", "rgb24", "pipe:1",
        ],
        check=True,
        capture_output=True,
    )
    if len(result.stdout) % common.CAPTURE_FRAME_BYTES:
        raise ValueError("focused raw-video decode ended with a partial frame")

    nonuniform_frames: dict[int, int] = {}
    tail: dict[str, object] = {}
    for offset in range(0, len(result.stdout), common.CAPTURE_FRAME_BYTES):
        frame_number = WINDOW_FIRST_FRAME + offset // common.CAPTURE_FRAME_BYTES
        captured = np.frombuffer(
            result.stdout[offset:offset + common.CAPTURE_FRAME_BYTES],
            dtype=np.uint8,
        ).reshape(common.CAPTURE_HEIGHT, common.CAPTURE_WIDTH, 3)
        top_left = captured[0::2, 0::2]
        top_right = captured[0::2, 1::2]
        bottom_left = captured[1::2, 0::2]
        bottom_right = captured[1::2, 1::2]
        mismatches = (
            np.any(top_left != top_right, axis=2) |
            np.any(top_left != bottom_left, axis=2) |
            np.any(top_left != bottom_right, axis=2)
        )
        count = int(np.count_nonzero(mismatches))
        if count:
            nonuniform_frames[frame_number] = count
        if frame_number == 20_062:
            rows, columns = np.where(mismatches)
            coordinates = [
                [int(column), int(row)]
                for row, column in zip(rows, columns)
            ]
            top_colors = np.unique(
                np.concatenate((top_left[mismatches], top_right[mismatches])),
                axis=0,
            )
            bottom_colors = np.unique(
                np.concatenate((bottom_left[mismatches], bottom_right[mismatches])),
                axis=0,
            )
            tail = {
                "frame": frame_number,
                "nonuniform_2x_blocks": count,
                "logical_block_coordinates": coordinates,
                "top_sample_rgb": top_colors[0].astype(int).tolist(),
                "bottom_sample_rgb": bottom_colors[0].astype(int).tolist(),
                "top_left_logical_renderer_fnv64": (
                    f"0x{common.renderer_fnv64(top_left.tobytes()):016x}"
                ),
            }
    return nonuniform_frames, tail


def selected_state(
    runs: list[common.Run],
    expected: tuple[int, int, int, int, str, int, bool, bool],
    nonuniform_frames: dict[int, int],
) -> tuple[dict[str, object], bool]:
    index, first, last, expected_hash, event, player_state, safe_left, safe_right = expected
    run = runs[index]
    is_tail_state = index == 32
    expected_run_last = last + 1 if is_tail_state else last
    expected_nonuniform = 5 if is_tail_state else 0
    actual_hash = common.renderer_fnv64(run.rgb)
    complete_frames_are_uniform = all(
        nonuniform_frames.get(frame, 0) == 0
        for frame in range(first, last + 1)
    )
    matched = (
        run.start == first and run.end == expected_run_last and
        run.nonuniform_blocks == expected_nonuniform and
        run.has_uniform_frame and actual_hash == expected_hash and
        complete_frames_are_uniform
    )
    return ({
        "run_index": index,
        "first_complete_frame": first,
        "last_complete_frame": last,
        "complete_uniform_frames": last - first + 1,
        "run_last_frame": run.end,
        "run_nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_hash:016x}",
        "logical_state": {
            "player_event": event,
            "player_state": player_state,
            "helper": "row_4_column_5_dwell",
            "safe_cells": [
                cell for cell, active in (
                    ([2, 1], safe_left), ([2, 4], safe_right)
                ) if active
            ],
        },
        "matched": matched,
    }, matched)


def torn_state(
    runs: list[common.Run], expected: tuple[int, int, int, int, int]
) -> tuple[dict[str, object], bool]:
    index, first, last, nonuniform, expected_hash = expected
    run = runs[index]
    actual_hash = common.renderer_fnv64(run.rgb)
    matched = (
        run.start == first and run.end == last and
        run.nonuniform_blocks == nonuniform and
        not run.has_uniform_frame and actual_hash == expected_hash
    )
    return ({
        "run_index": index,
        "first_frame": run.start,
        "last_frame": run.end,
        "frames": run.end - run.start + 1,
        "nonuniform_2x_blocks": run.nonuniform_blocks,
        "renderer_fnv64": f"0x{actual_hash:016x}",
        "classification": (
            "capture-only nonuniform 2x DOSBox-X refresh; not a complete "
            "logical framebuffer"
        ),
        "matched": matched,
    }, matched)


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)
    nonuniform_frames, safe_expiry_tail = decode_frame_metadata(CAPTURE)

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
    nonuniform_frames_match = nonuniform_frames == EXPECTED_NONUNIFORM_FRAMES
    safe_expiry_tail_matches = safe_expiry_tail == EXPECTED_SAFE_EXPIRY_TAIL

    selected_states: dict[str, dict[str, object]] = {}
    selected_states_match = True
    for name, expected in EXPECTED_COMPLETE_STATES.items():
        state, matched = selected_state(runs, expected, nonuniform_frames)
        selected_states[name] = state
        selected_states_match &= matched

    torn_runs: dict[str, dict[str, object]] = {}
    torn_runs_match = True
    for name, expected in EXPECTED_TORN_RUNS.items():
        state, matched = torn_state(runs, expected)
        torn_runs[name] = state
        torn_runs_match &= matched

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
                "Muncher moves left from row 2 column 3 to column 2",
                "Muncher chews and clears the correct value 1 at row 2 column 2",
                "safe zone at row 2 column 1 expires on the held phase-zero page",
                "Muncher moves down from row 2 column 2 to row 3 column 2",
                "Muncher chews and clears the correct value 1 at row 3 column 2",
                "safe zone at row 2 column 4 begins a capture-torn expiry",
                "Helper remains dwelling at row 4 column 5",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "nonuniform_frames": nonuniform_frames,
        "nonuniform_frames_match": nonuniform_frames_match,
        "capture_only_torn_refreshes": torn_runs,
        "torn_refreshes_match_source": torn_runs_match,
        "capture_only_safe_expiry_tail": {
            **safe_expiry_tail,
            "classification": (
                "capture-only lower-half update of five doubled blocks on the "
                "row-2/column-4 safe-zone outline; frames 20058-20061 already "
                "provide the complete logical terminal page"
            ),
            "matched": safe_expiry_tail_matches,
        },
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "capture_only_torn_refresh_count": len(EXPECTED_TORN_RUNS) + 1,
            "headless_gate": "game_render_state_test",
            "reason": (
                "native matches all 30 completed player/Helper/safe-zone pages "
                "in source order and excludes three torn actor transitions plus "
                "the five-block partial safe-zone expiry"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        nonuniform_frames_match,
        torn_runs_match,
        safe_expiry_tail_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
