#!/usr/bin/env python3
"""Audit the first full-Demo Reggie-on-Muncher collision sequence."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-first-player-collision-report.json"
)

WINDOW_FIRST_FRAME = 5_719
WINDOW_LAST_FRAME = 5_799
EXPECTED_WINDOW = {
    "first_global_frame": WINDOW_FIRST_FRAME,
    "last_global_frame": WINDOW_LAST_FRAME,
    "decoded_frames": 81,
    "nonuniform_2x_blocks": 15,
    "logical_run_count": 26,
}

OPEN_HASH = 0xD26DE8ADDAFE2FDE
CLOSED_HASH = 0x550D2185AA30525D

# run, first, last, renderer FNV-64, logical collision state
EXPECTED_COMPLETE_STATES = {
    "stationary_pre_bite":
        (0, 5_719, 5_720, 0xEAF1BECC0FDBAB4E, "pre_bite"),
    "bite_tick_00": (1, 5_721, 5_723, OPEN_HASH, "tick_00_open"),
    "bite_tick_01": (2, 5_724, 5_725, CLOSED_HASH, "tick_01_closed"),
    "bite_tick_02": (3, 5_726, 5_727, OPEN_HASH, "tick_02_open"),
    "bite_tick_03": (4, 5_728, 5_730, CLOSED_HASH, "tick_03_closed"),
    "bite_tick_04": (5, 5_731, 5_732, OPEN_HASH, "tick_04_open"),
    "bite_tick_05": (6, 5_733, 5_735, CLOSED_HASH, "tick_05_closed"),
    "bite_tick_06": (7, 5_736, 5_737, OPEN_HASH, "tick_06_open"),
    "bite_tick_07": (8, 5_738, 5_739, CLOSED_HASH, "tick_07_closed"),
    "bite_tick_08": (10, 5_741, 5_742, OPEN_HASH, "tick_08_open"),
    "bite_tick_09": (11, 5_743, 5_744, CLOSED_HASH, "tick_09_closed"),
    "bite_tick_10": (12, 5_745, 5_747, OPEN_HASH, "tick_10_open"),
    "bite_tick_11": (13, 5_748, 5_749, CLOSED_HASH, "tick_11_closed"),
    "bite_tick_12": (14, 5_750, 5_751, OPEN_HASH, "tick_12_open"),
    "bite_tick_13": (16, 5_753, 5_754, CLOSED_HASH, "tick_13_closed"),
    "bite_tick_14": (17, 5_755, 5_756, OPEN_HASH, "tick_14_open"),
    "bite_tick_15": (18, 5_757, 5_759, CLOSED_HASH, "tick_15_closed"),
    "bite_tick_16": (19, 5_760, 5_761, OPEN_HASH, "tick_16_open"),
    "bite_tick_17": (20, 5_762, 5_763, CLOSED_HASH, "tick_17_closed"),
    "bite_tick_18": (22, 5_765, 5_766, OPEN_HASH, "tick_18_open"),
    "bite_tick_19": (23, 5_767, 5_768, CLOSED_HASH, "tick_19_closed"),
    "bite_tick_20": (24, 5_769, 5_771, OPEN_HASH, "tick_20_open"),
    "feedback":
        (25, 5_772, 5_799, 0x19CAD4E7FDBFB3AA, "feedback"),
}

# run, first, last, nonuniform blocks, renderer hash, scanline,
# previous-pixel count, following-pixel count
EXPECTED_SCANOUT_SPLICES = (
    (9, 5_740, 5_740, 15, 0x2B6A687186698105, 65, 48, 248),
    (15, 5_752, 5_752, 0, 0xD709D3387F474E78, 70, 95, 201),
    (21, 5_764, 5_764, 0, 0x80ACA57ADA1D554B, 76, 169, 127),
)


def partition(runs: list[common.Run], index: int) -> dict[str, object]:
    shape = (common.LOGICAL_HEIGHT, common.LOGICAL_WIDTH, 3)
    previous = np.frombuffer(runs[index - 1].rgb, dtype=np.uint8).reshape(shape)
    partial = np.frombuffer(runs[index].rgb, dtype=np.uint8).reshape(shape)
    following = np.frombuffer(runs[index + 1].rgb, dtype=np.uint8).reshape(shape)
    changed = np.any(previous != following, axis=2)
    same_previous = np.all(partial == previous, axis=2)
    same_next = np.all(partial == following, axis=2)
    neither = ~(same_previous | same_next)
    splice_rows = [
        row for row in range(common.LOGICAL_HEIGHT + 1)
        if np.array_equal(partial[:row], previous[:row]) and
        np.array_equal(partial[row:], following[row:])
    ]
    return {
        "neighbor_changed_pixels": int(np.count_nonzero(changed)),
        "partial_previous_pixels": int(np.count_nonzero(
            same_previous & changed
        )),
        "partial_next_pixels": int(np.count_nonzero(same_next & changed)),
        "partial_neither_pixels": int(np.count_nonzero(neither)),
        "exact_horizontal_splice_row": (
            splice_rows[0] if len(splice_rows) == 1 else None
        ),
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
            "collision_state": expected[4],
            "selected_biter": "Reggie_row_1_column_2",
            "player": "hidden_row_1_column_2",
            "safe_zone": "row_2_column_1",
            "warning": True,
        }
        selected_states[name] = state
        selected_states_match &= matched

    splices: list[dict[str, object]] = []
    splices_match = True
    for expected in EXPECTED_SCANOUT_SPLICES:
        index, first, last, nonuniform, expected_hash = expected[:5]
        expected_row, expected_previous, expected_next = expected[5:]
        run = runs[index]
        run_hash = common.renderer_fnv64(run.rgb)
        pixels = partition(runs, index)
        expected_pixels = {
            "neighbor_changed_pixels": 296,
            "partial_previous_pixels": expected_previous,
            "partial_next_pixels": expected_next,
            "partial_neither_pixels": 0,
            "exact_horizontal_splice_row": expected_row,
        }
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == nonuniform and
            run_hash == expected_hash and pixels == expected_pixels
        )
        splices_match &= matched
        splices.append({
            "run_index": index,
            "first_frame": run.start,
            "last_frame": run.end,
            "frames": run.end - run.start + 1,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "uniform_2x": run.has_uniform_frame,
            "renderer_fnv64": f"0x{run_hash:016x}",
            "pixel_partition": pixels,
            "classification": (
                f"capture-only horizontal scanout splice at logical row "
                f"{expected_row}; every pixel belongs exactly to one of the "
                "adjacent completed bite pages"
            ),
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
        "oracle_scope": {
            "classification": "continuous first-board replay evidence",
            "continuous_replay_claim": True,
            "reason": (
                "the corrected deterministic native replay reaches this "
                "first-board collision organically and presents the exact "
                "completed source sequence"
            ),
        },
        "board": {
            "mode": "Multiples",
            "target": 5,
            "footer": "Demo",
            "events": [
                "Muncher reaches the resident Reggie at row 1 column 2",
                "Reggie presents one stationary collision page",
                "Reggie presents all 21 ordered bite callbacks",
                "the complete Oops/Trogglus normalus feedback page follows",
            ],
            "labels_row_major": [
                "21", "", "5", "", "133", "11",
                "", "", "", "", "", "",
                "", "", "", "", "", "134",
                "59", "211", "176", "14", "2", "230",
                "", "207", "105", "26", "125", "175",
            ],
        },
        "selected_complete_states": selected_states,
        "selected_states_match": selected_states_match,
        "capture_only_scanout_splices": splices,
        "capture_only_scanout_splices_match": splices_match,
        "native_replay": {
            "complete": True,
            "exact_complete_state_count": len(EXPECTED_COMPLETE_STATES),
            "incremental_new_complete_state_count": (
                len(EXPECTED_COMPLETE_STATES) - 1
            ),
            "overlap": (
                "the stationary pre-bite framebuffer is pixel-identical to "
                "the already gated collision-transient board"
            ),
            "exact_complete_states_presented": list(EXPECTED_COMPLETE_STATES),
            "capture_only_scanout_splice_count": len(EXPECTED_SCANOUT_SPLICES),
            "headless_gate": "game_render_state_test",
            "reason": (
                "native presents the stationary page, all 21 bite ticks, "
                "and feedback in exact source order without the three "
                "scanout splices"
            ),
        },
    }
    report["valid"] = all((
        source_matches,
        probe_matches,
        audio_matches,
        window_matches,
        selected_states_match,
        splices_match,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
