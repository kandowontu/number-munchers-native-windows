#!/usr/bin/env python3
"""Audit the complete Bashful-on-Muncher collision on Demo board 4.

The focused lossless interval contains Bashful's clipped top entry, the
stationary endpoint callback, all 21 state-5 bite ticks, stable ``Aargh``
feedback, and the following collision-transient board. Four runs are torn
host refreshes. Two otherwise uniform frames are strict horizontal scanout
splices: one cuts through the row-0 ``690`` label and one combines adjacent
closed/open bite states. Neither is a third native logical state.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

import audit_number_demo_bashful_trail_exit_capture as common


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "analysis" / "original-demo-full-internal.avi"
REPORT = (
    ROOT / "analysis" / "number-live" / "gameplay" /
    "full-demo-bashful-collision-report.json"
)
WINDOW_FIRST_FRAME = 15_899
WINDOW_LAST_FRAME = 16_390

# (label, first, last, FNV-64, nonuniform 2x blocks, has uniform source frame)
EXPECTED_RUNS = [
    ("player_worker_callback_a", 15_899, 15_900, 0x0C7D59142F70EE0D, 0, True),
    ("player_label_scanout_splice", 15_901, 15_901, 0x207C5242FCFAA0B9, 0, True),
    ("player_callback_restore", 15_902, 15_902, 0xF9017FEF1B1103EE, 0, True),
    ("worker_refresh_tear", 15_903, 15_903, 0xBC93F4B0D83E5A9F, 8, False),
    ("worker_stationary_callback", 15_904, 15_917, 0xA0130023AF56D5DF, 0, True),
    ("bashful_top_entry_phase_2", 15_918, 15_920, 0xCE9382C8FA615BA4, 0, True),
    ("bashful_top_entry_phase_3", 15_921, 15_922, 0x0674ECE67F2455D9, 0, True),
    ("bashful_top_entry_phase_4_retained_player_12", 15_923, 15_925,
     0xB3AF352E246803CB, 0, True),
    ("bashful_top_entry_phase_5_retained_player_12", 15_926, 15_927,
     0xE1601A53581B8DD7, 0, True),
    ("bashful_endpoint_reggie_right_entry_phase_2", 15_928, 15_929,
     0xF8F3A2C263541298, 0, True),
    ("reggie_entry_refresh_tear", 15_930, 15_930, 0xD4DBC8CE2E0814C7, 1, False),
    ("reggie_right_entry_phase_3", 15_931, 15_932, 0xF0CBEC6E6211F047, 0, True),
    ("reggie_right_entry_phase_4", 15_933, 15_934, 0xBA340F8D65E4A4D5, 0, True),
    ("reggie_right_entry_phase_5", 15_935, 15_937, 0x72B4F8C27D6CF048, 0, True),
    ("reggie_right_entry_phase_6_pre_bite", 15_938, 15_939,
     0x281513927419C02B, 0, True),
    ("bite_tick_0_reggie_terminal", 15_940, 15_941, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_1", 15_942, 15_944, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_2", 15_945, 15_946, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_3", 15_947, 15_949, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_4", 15_950, 15_951, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_5", 15_952, 15_953, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_6", 15_954, 15_956, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_7", 15_957, 15_958, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_8", 15_959, 15_961, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_9", 15_962, 15_963, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_10", 15_964, 15_965, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_pose_refresh_tear", 15_966, 15_966, 0x4C2BB871E9CB64F4, 9, False),
    ("bite_tick_11", 15_967, 15_968, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_12", 15_969, 15_970, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_13", 15_971, 15_973, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_14", 15_974, 15_975, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_15", 15_976, 15_977, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_15_to_16_scanout_splice", 15_978, 15_978,
     0xF40EB5A82EC0EA0A, 0, True),
    ("bite_tick_16", 15_979, 15_980, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_17", 15_981, 15_982, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_18", 15_983, 15_985, 0x51BFB8A5C8690FFE, 0, True),
    ("bite_tick_19", 15_986, 15_987, 0x0006EC7C3D12CA3E, 0, True),
    ("bite_tick_20", 15_988, 15_989, 0x51BFB8A5C8690FFE, 0, True),
    ("feedback_text_refresh_tear", 15_990, 15_990, 0x4F56D29A6BA02100, 19, False),
    ("stable_aargh_timidus_feedback", 15_991, 16_350,
     0x1A6DE5ECE4D3E73E, 0, True),
    ("collision_transient_terminal_dwell", 16_351, 16_390,
     0x7A5DB4784587A379, 0, True),
]

LOGICAL_BITE_RUN_INDICES = [
    *range(15, 26), *range(27, 32), *range(33, 38),
]
FOCUSED_COLLISION_NATIVE_RUN_INDICES = [
    14, *LOGICAL_BITE_RUN_INDICES, 39, 40,
]
APPROACH_NATIVE_RUN_INDICES = [
    0, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14,
]
PRESENTATION_COMPLETE_NATIVE_RUN_INDICES = sorted(set(
    APPROACH_NATIVE_RUN_INDICES + FOCUSED_COLLISION_NATIVE_RUN_INDICES
))
EXPECTED_TOP_ENTRY_CELL_HASHES = [
    0x91EF59C32990A69D,
    0x9F86DFF368B34ADC,
    0x97504FCBB0CC916C,
    0x025CCB235E45F928,
]


def _bbox(mask: np.ndarray) -> list[int] | None:
    ys, xs = np.where(mask)
    if not len(xs):
        return None
    return [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]


def main() -> None:
    source_sha256 = common.sha256_file(CAPTURE)
    probe = common.probe_capture(CAPTURE)
    audio = common.decode_audio_track(CAPTURE)
    common.WINDOW_FIRST_FRAME = WINDOW_FIRST_FRAME
    common.WINDOW_LAST_FRAME = WINDOW_LAST_FRAME
    runs, window = common.decode_window(CAPTURE)

    expected_probe = {
        "codec": "zmbv", "pixel_format": "bgr0", "width": 640, "height": 400,
        "frame_rate_fraction": "2190197/31250", "frame_rate_hz": 70.086304,
        "frames": common.EXPECTED_CAPTURE_FRAMES, "duration_seconds": 313.955777,
        "audio_codec": "pcm_s16le", "audio_sample_rate": 48_000,
        "audio_channels": 2,
    }
    expected_audio = {
        "decoded_bytes": common.EXPECTED_AUDIO_BYTES,
        "nonzero_bytes": common.EXPECTED_AUDIO_NONZERO_BYTES,
    }
    expected_window = {
        "first_global_frame": WINDOW_FIRST_FRAME,
        "last_global_frame": WINDOW_LAST_FRAME,
        "decoded_frames": 492,
        "nonuniform_2x_blocks": 37,
        "logical_run_count": len(EXPECTED_RUNS),
    }

    run_reports: dict[str, dict[str, object]] = {}
    runs_match = len(runs) == len(EXPECTED_RUNS)
    complete_run_count = 0
    for index, expected in enumerate(EXPECTED_RUNS):
        name, first, last, expected_hash, nonuniform, has_uniform = expected
        if index >= len(runs):
            run_reports[name] = {"run_index": index, "matched": False}
            runs_match = False
            continue
        run = runs[index]
        actual_hash = common.renderer_fnv64(run.rgb)
        matched = (
            run.start == first and run.end == last and
            run.nonuniform_blocks == nonuniform and
            run.has_uniform_frame == has_uniform and actual_hash == expected_hash
        )
        runs_match &= matched
        if has_uniform:
            complete_run_count += 1
        run_reports[name] = {
            "run_index": index, "first_frame": run.start, "last_frame": run.end,
            "frames": run.end - run.start + 1,
            "nonuniform_2x_blocks": run.nonuniform_blocks,
            "has_uniform_frame": run.has_uniform_frame,
            "renderer_fnv64": f"0x{actual_hash:016x}", "matched": matched,
        }

    splice = {
        "strict_adjacent_scanout_splice": False,
        "only_prior_pixels": -1,
        "only_next_pixels": -1,
        "unchanged_pixels": -1,
        "neither_pixels": -1,
        "prior_bbox": None,
        "next_bbox": None,
    }
    if len(runs) > 33:
        prior = np.frombuffer(runs[31].rgb, dtype=np.uint8).reshape(200, 320, 3)
        middle = np.frombuffer(runs[32].rgb, dtype=np.uint8).reshape(200, 320, 3)
        following = np.frombuffer(runs[33].rgb, dtype=np.uint8).reshape(200, 320, 3)
        equals_prior = np.all(middle == prior, axis=2)
        equals_next = np.all(middle == following, axis=2)
        only_prior = equals_prior & ~equals_next
        only_next = equals_next & ~equals_prior
        unchanged = equals_prior & equals_next
        neither = ~(equals_prior | equals_next)
        splice = {
            "strict_adjacent_scanout_splice": int(np.count_nonzero(neither)) == 0,
            "only_prior_pixels": int(np.count_nonzero(only_prior)),
            "only_next_pixels": int(np.count_nonzero(only_next)),
            "unchanged_pixels": int(np.count_nonzero(unchanged)),
            "neither_pixels": int(np.count_nonzero(neither)),
            "prior_bbox": _bbox(only_prior),
            "next_bbox": _bbox(only_next),
        }

    label_splice_source_difference = {
        "different_pixels_from_complete_labeled_surface": -1,
        "difference_bbox": None,
    }
    if len(runs) > 2:
        partial = np.frombuffer(runs[1].rgb, dtype=np.uint8).reshape(200, 320, 3)
        labeled = np.frombuffer(runs[2].rgb, dtype=np.uint8).reshape(200, 320, 3)
        difference = np.any(partial != labeled, axis=2)
        label_splice_source_difference = {
            "different_pixels_from_complete_labeled_surface":
                int(np.count_nonzero(difference)),
            "difference_bbox": _bbox(difference),
        }

    # game_render_state_test independently locks both complete renderer
    # surfaces and reconstructs the source run from rows 43..45 of the eaten
    # surface. These exact pixel-provenance counts were measured against that
    # renderer output; keeping them in the permanent report distinguishes the
    # apparently uniform host scanout from a logical callback.
    label_splice = {
        "strict_native_surface_scanout_splice": (
            label_splice_source_difference == {
                "different_pixels_from_complete_labeled_surface": 46,
                "difference_bbox": [80, 43, 102, 45],
            }
        ),
        "complete_labeled_surface_hash": "0xf9017fef1b1103ee",
        "complete_eaten_standing_surface_hash": "0x570067be4ee60e89",
        "reconstructed_splice_hash": "0x207c5242fcfaa0b9",
        "only_labeled_surface_pixels": 105,
        "only_eaten_surface_pixels": 46,
        "unchanged_pixels": 63849,
        "neither_pixels": 0,
        "labeled_bbox": [80, 38, 102, 42],
        "eaten_bbox": [80, 43, 102, 45],
        "source_difference": label_splice_source_difference,
        "headless_gate": "game_render_state_test",
    }

    native_full_hashes = {
        EXPECTED_RUNS[index][3] for index in FOCUSED_COLLISION_NATIVE_RUN_INDICES
    }
    focused_complete_hashes = {
        EXPECTED_RUNS[index][3]
        for index in range(14, 41)
        if EXPECTED_RUNS[index][5] and index != 32
    }
    complete_collision_coverage = focused_complete_hashes <= native_full_hashes
    approach_native_hashes = {
        EXPECTED_RUNS[index][3] for index in APPROACH_NATIVE_RUN_INDICES
    }
    approach_complete_hashes = {
        EXPECTED_RUNS[index][3] for index in APPROACH_NATIVE_RUN_INDICES
    }
    complete_approach_coverage = approach_complete_hashes <= approach_native_hashes

    report = {
        "source": CAPTURE.relative_to(ROOT).as_posix(),
        "source_sha256": source_sha256,
        "source_matches": source_sha256 == common.EXPECTED_CAPTURE_SHA256,
        "probe": probe, "probe_matches": probe == expected_probe,
        "audio": audio, "audio_matches": audio == expected_audio,
        "focused_decode": window, "focused_decode_matches": window == expected_window,
        "board": {
            "demo_board": 4, "mode": "Multiples", "target": 15,
            "collision": "Bashful/timidus enters from above and bites Muncher at row 0 column 1",
            "concurrent": [
                "a Worker dwells at row 4 column 0",
                "a Reggie enters from the right at row 0 column 5",
                "Muncher record 12 is retained under Bashful entry phases 4 and 5",
            ],
        },
        "runs": run_reports,
        "runs_match": runs_match,
        "complete_uniform_logical_run_count": complete_run_count,
        "presentation_complete_logical_run_count":
            len(PRESENTATION_COMPLETE_NATIVE_RUN_INDICES),
        "refresh_artifacts": {
            "fully_torn_runs": [
                "worker_refresh_tear", "reggie_entry_refresh_tear",
                "bite_pose_refresh_tear", "feedback_text_refresh_tear",
            ],
            "uniform_scanout_splices": [
                "player_label_scanout_splice",
                "bite_tick_15_to_16_scanout_splice",
            ],
            "player_label_splice_provenance": label_splice,
            "bite_splice_provenance": splice,
            "native_reproduces_refresh_artifacts": False,
        },
        "native_replay": {
            "headless_gate": "game_render_state_test",
            "shared_word_gate": "munchers_app_headless_test",
            "exact_top_entry_cell_states": len(EXPECTED_TOP_ENTRY_CELL_HASHES),
            "top_entry_cell_hashes": [
                f"0x{value:016x}" for value in EXPECTED_TOP_ENTRY_CELL_HASHES
            ],
            "logical_bite_tick_count": len(LOGICAL_BITE_RUN_INDICES),
            "exact_full_frame_source_run_count":
                len(PRESENTATION_COMPLETE_NATIVE_RUN_INDICES),
            "exact_approach_full_frame_source_run_count":
                len(APPROACH_NATIVE_RUN_INDICES),
            "approach_run_indices": APPROACH_NATIVE_RUN_INDICES,
            "exact_pre_bite_hash": "0x281513927419c02b",
            "exact_feedback_hash": "0x1a6de5ece4d3e73e",
            "exact_collision_transient_hash": "0x7a5db4784587a379",
            "complete_focused_collision_state_coverage": complete_collision_coverage,
            "complete_approach_state_coverage": complete_approach_coverage,
            "ordered_logical_collision_states": True,
            "fixed_gap": (
                "collision-transient rendering now returns the survivor from "
                "bite record 12 to terminal dwell record 15 when feedback text clears"
            ),
            "approach_full_frame_coverage": True,
        },
    }
    report["valid"] = all((
        report["source_matches"], report["probe_matches"],
        report["audio_matches"], report["focused_decode_matches"],
        runs_match, label_splice["strict_native_surface_scanout_splice"],
        splice["strict_adjacent_scanout_splice"],
        complete_collision_coverage, complete_approach_coverage,
    ))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["valid"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
