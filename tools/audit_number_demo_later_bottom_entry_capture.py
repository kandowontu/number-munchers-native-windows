#!/usr/bin/env python3
"""Audit the later concurrent Bashful bottom-entry interval in Number Demo."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-later-bottom-entry-report.json"
)

WINDOW_FIRST_FRAME = 5_188
WINDOW_LAST_FRAME = 5_215
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 28,
    "nonuniform_2x_blocks": 20,
    "logical_run_count": 8,
}

# run, first, last, renderer FNV-64, player state, Bashful entry state
EXPECTED_COMPLETE_STATES = {
    "player_down_phase_3_bashful_invisible_phase_1":
        (0, 5_188, 5_188, 0x08B631A66E5D0F0F, 3, 1),
    "player_down_phase_4_bashful_invisible_phase_1":
        (1, 5_189, 5_191, 0x90456223812679D4, 4, 1),
    "player_terminal_bashful_entry_phase_2":
        (2, 5_192, 5_193, 0x9C0190C31703DB30, 5, 2),
    "player_idle_bashful_entry_phase_3":
        (3, 5_194, 5_195, 0xBCAA9E6D0C9CE28B, 6, 3),
    "player_idle_bashful_entry_phase_4":
        (5, 5_197, 5_198, 0xE1A2C8D0CB0630EB, 6, 4),
    "player_idle_bashful_entry_phase_5":
        (6, 5_199, 5_200, 0xD35094DB8E41F03C, 6, 5),
    "player_idle_bashful_dwell":
        (7, 5_201, 5_215, 0x364C0086FE50EA46, 6, 6),
}

EXPECTED_TORN = (4, 5_196, 5_196, 20, 0x0F30F10E973038B4)
EXPECTED_TORN_PARTITION = {
    "neighbor_changed_pixels": 342,
    "partial_previous_pixels": 141,
    "partial_next_pixels": 164,
    "partial_neither_pixels": 47,
    "partial_neither_bbox": [180, 159, 194, 165],
}


def partition(runs: list[common.Run]) -> dict[str, object]:
    shape = (common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    previous = np.frombuffer(runs[3].rgb, dtype=np.uint8).reshape(shape)
    partial = np.frombuffer(runs[4].rgb, dtype=np.uint8).reshape(shape)
    following = np.frombuffer(runs[5].rgb, dtype=np.uint8).reshape(shape)
    changed = np.any(previous != following, axis=2)
    same_previous = np.all(partial == previous, axis=2)
    same_next = np.all(partial == following, axis=2)
    neither = ~(same_previous | same_next)
    rows, columns = np.where(neither)
    return {
        "neighbor_changed_pixels": int(np.count_nonzero(changed)),
        "partial_previous_pixels": int(np.count_nonzero(
            same_previous & changed
        )),
        "partial_next_pixels": int(np.count_nonzero(same_next & changed)),
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
        state, matched = common.selected_state(runs, expected[:4])
        state["logical_state"] = {
            "player_down_state": expected[4],
            "bashful_bottom_entry_state": expected[5],
            "reggie": "row_0_column_2_dwell",
            "warning": True,
        }
        selected_states[name] = state
        selected_states_match &= matched

    index, first, last, nonuniform, expected_hash = EXPECTED_TORN
    torn = runs[index]
    torn_hash = common.renderer_fnv64(torn.rgb)
    torn_partition = partition(runs)
    torn_matches = (
        torn.start == first and torn.end == last and
        torn.nonuniform_blocks == nonuniform and
        not torn.has_uniform_frame and torn_hash == expected_hash and
        torn_partition == EXPECTED_TORN_PARTITION
    )

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
                "the captured concurrent player position differs from the "
                "corrected continuous native route, so the directly decoded "
                "actor states are gated without reviving that withdrawn route"
            ),
        },
        "board": {
            "mode": "Multiples",
            "target": 5,
            "footer": "Demo",
            "events": [
                "Muncher completes a downward move from row 3 to row 4 column 0",
                "Bashful enters upward from below row 4 column 3",
                "Reggie dwells at row 0 column 2",
                "a later actor slot keeps the Troggle warning visible",
            ],
            "labels_row_major": [
                "21", "", "", "", "133", "11",
                "", "", "", "", "", "",
                "", "", "", "", "", "134",
                "59", "211", "176", "14", "2", "230",
                "220", "207", "115", "23", "125", "175",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_torn_refresh": {
            "run_index": index,
            "first_frame": torn.start,
            "last_frame": torn.end,
            "frames": torn.end - torn.start + 1,
            "nonuniform_2x_blocks": torn.nonuniform_blocks,
            "renderer_fnv64": f"0x{torn_hash:016x}",
            "pixel_partition": torn_partition,
            "classification": (
                "capture-only nonuniform Bashful phase-3-to-4 repaint with "
                "47 pixels belonging to neither completed neighboring page"
            ),
            "matched": torn_matches,
        },
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "capture_only_torn_refresh_count": 1,
            "headless_gate": "game_render_state_test",
            "reason": (
                "native matches all seven completed concurrent player/Reggie/"
                "Bashful/warning pages and excludes the torn entry repaint"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        torn_matches,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
